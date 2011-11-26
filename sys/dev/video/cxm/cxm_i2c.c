/*
 * Copyright (c) 2003, 2004, 2005
 *	John Wehle <john@feith.com>.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by John Wehle.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.	IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * I2c routines for the Conexant MPEG-2 Codec driver.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/conf.h>
#include <sys/uio.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/poll.h>
#include <sys/select.h>
#include <sys/resource.h>
#include <sys/bus.h>
#include <sys/rman.h>

#include <machine/clock.h>

#include <bus/pci/pcireg.h>
#include <bus/pci/pcivar.h>

#include <dev/video/cxm/cxm.h>

#include <bus/iicbus/iiconf.h>

#include "iicbb_if.h"


static int	cxm_iic_probe(device_t dev);
static int	cxm_iic_attach(device_t dev);
static int	cxm_iic_detach(device_t dev);
static void	cxm_iic_child_detached(device_t dev, device_t child);

static int	cxm_iic_callback(device_t, int, caddr_t *);
static int	cxm_iic_reset(device_t, u_char, u_char, u_char *);
static int	cxm_iic_getscl(device_t);
static int	cxm_iic_getsda(device_t);
static void	cxm_iic_setscl(device_t, int);
static void	cxm_iic_setsda(device_t, int);

static device_method_t cxm_iic_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,         cxm_iic_probe),
	DEVMETHOD(device_attach,        cxm_iic_attach),
	DEVMETHOD(device_detach,        cxm_iic_detach),

	/* bus interface */
	DEVMETHOD(bus_child_detached,   cxm_iic_child_detached),
	DEVMETHOD(bus_print_child,      bus_generic_print_child),
	DEVMETHOD(bus_driver_added,     bus_generic_driver_added),

	/* iicbb interface */
	DEVMETHOD(iicbb_callback,       cxm_iic_callback),
	DEVMETHOD(iicbb_reset,          cxm_iic_reset),
	DEVMETHOD(iicbb_getscl,         cxm_iic_getscl),
	DEVMETHOD(iicbb_getsda,         cxm_iic_getsda),
	DEVMETHOD(iicbb_setscl,         cxm_iic_setscl),
	DEVMETHOD(iicbb_setsda,         cxm_iic_setsda),

	{ 0, 0 }
};

static driver_t cxm_iic_driver = {
	"cxm_iic",
	cxm_iic_methods,
	sizeof(struct cxm_iic_softc),
};

static devclass_t cxm_iic_devclass;

DRIVER_MODULE(cxm_iic, cxm, cxm_iic_driver, cxm_iic_devclass, NULL, NULL);
MODULE_VERSION(cxm_iic, 1);
MODULE_DEPEND(cxm_iic, iicbb, IICBB_MINVER, IICBB_PREFVER, IICBB_MAXVER);


/*
 * the boot time probe routine.
 *
 * The cxm_iic device is only probed after it has
 * been established that the cxm device is present
 * which means that the cxm_iic device * must *
 * be present since it's built into the cxm hardware.
 */
static int
cxm_iic_probe(device_t dev)
{
	device_set_desc(dev, "Conexant iTVC15 / iTVC16 I2C controller");

	return 0;
}


/*
 * the attach routine.
 */
static int
cxm_iic_attach(device_t dev)
{
	device_t *kids;
	device_t iicbus;
	int error;
	int numkids;
	int i;
	int unit;
	bus_space_handle_t *bhandlep;
	bus_space_tag_t *btagp;
	struct cxm_iic_softc *sc;
	device_t child;

	/* Get the device data */
	sc = device_get_softc(dev);
	unit = device_get_unit(dev);

	/* retrieve the cxm btag and bhandle */
	if (BUS_READ_IVAR(device_get_parent(dev), dev,
			  CXM_IVAR_BTAG, (uintptr_t *)&btagp)
	    || BUS_READ_IVAR(device_get_parent(dev), dev,
			     CXM_IVAR_BHANDLE, (uintptr_t *)&bhandlep)) {
		device_printf(dev,
			      "could not retrieve bus space information\n");
		return ENXIO;
	}

	sc->btag = *btagp;
	sc->bhandle = *bhandlep;

	/* add bit-banging generic code onto cxm_iic interface */
	sc->iicbb = device_add_child(dev, "iicbb", -1);

	if (!sc->iicbb) {
		device_printf(dev, "could not add iicbb\n");
		return ENXIO;
	}

	/* probed and attached the bit-banging code */
	error = device_probe_and_attach(sc->iicbb);

	if (error) {
		device_printf(dev, "could not attach iicbb\n");
		goto fail;
	}

	/* locate iicbus which was attached by the bit-banging code */
	iicbus = NULL;
	device_get_children(sc->iicbb, &kids, &numkids);
	for (i = 0; i < numkids; i++)
		if (strcmp(device_get_name(kids[i]), "iicbus") == 0) {
			iicbus = kids[i];
			break;
		}
	kfree(kids, M_TEMP);

	if (!iicbus) {
		device_printf(dev, "could not find iicbus\n");
		error = ENXIO;
		goto fail;
	}

	if (BUS_WRITE_IVAR(device_get_parent(dev), dev,
			   CXM_IVAR_IICBUS, (uintptr_t)&iicbus)) {
		device_printf(dev, "could not store iicbus information\n");
		error = ENXIO;
		goto fail;
	}

	return 0;

fail:
	/*
	 * Detach the children before recursively deleting
	 * in case a child has a pointer to a grandchild
	 * which is used by the child's detach routine.
	 *
	 * Remember the child before detaching so we can
	 * delete it (bus_generic_detach indirectly zeroes
	 * sc->child_dev).
	 */
	child = sc->iicbb;
	bus_generic_detach(dev);
	if (child)
		device_delete_child(dev, child);

	return error;
}


/*
 * the detach routine.
 */
static int
cxm_iic_detach(device_t dev)
{
	struct cxm_iic_softc *sc;
	device_t child;

	/* Get the device data */
	sc = device_get_softc(dev);

	BUS_WRITE_IVAR(device_get_parent(dev), dev, CXM_IVAR_IICBUS, 0);

	/*
	 * Detach the children before recursively deleting
	 * in case a child has a pointer to a grandchild
	 * which is used by the child's detach routine.
	 *
	 * Remember the child before detaching so we can
	 * delete it (bus_generic_detach indirectly zeroes
	 * sc->child_dev).
	 */
	child = sc->iicbb;
	bus_generic_detach(dev);
	if (child)
		device_delete_child(dev, child);

	return 0;
}


/*
 * the child detached routine.
 */
static void
cxm_iic_child_detached(device_t dev, device_t child)
{
	struct cxm_iic_softc *sc;

	/* Get the device data */
	sc = device_get_softc(dev);

	if (child == sc->iicbb)
		sc->iicbb = NULL;
}


static int
cxm_iic_callback(device_t dev, int index, caddr_t *data)
{
	return 0;
}


static int
cxm_iic_reset(device_t dev, u_char speed, u_char addr, u_char * oldaddr)
{
	struct cxm_iic_softc *sc;

	/* Get the device data */
	sc = (struct cxm_iic_softc *)device_get_softc(dev);

	/* Set scl to 1 */
	CSR_WRITE_4(sc, CXM_REG_I2C_SETSCL, ~(int)1);

	/* Set sda to 1 */
	CSR_WRITE_4(sc, CXM_REG_I2C_SETSDA, ~(int)1);

	/*
	 * PCI writes may be buffered so force the
	 * write to complete by reading the last
	 * location written.
	 */

	CSR_READ_4(sc, CXM_REG_I2C_SETSDA);

	/* Wait for 10 usec */
	DELAY(10);

	return IIC_ENOADDR;
}


static int
cxm_iic_getscl(device_t dev)
{
	struct cxm_iic_softc *sc;

	/* Get the device data */
	sc = (struct cxm_iic_softc *)device_get_softc(dev);

	/* Get sda */
	return CSR_READ_1(sc, CXM_REG_I2C_GETSCL);
}


static int
cxm_iic_getsda(device_t dev)
{
	struct cxm_iic_softc *sc;

	/* Get the device data */
	sc = (struct cxm_iic_softc *)device_get_softc(dev);

	/* Get sda */
	return CSR_READ_1(sc, CXM_REG_I2C_GETSDA);
}


static void
cxm_iic_setscl(device_t dev, int val)
{
	struct cxm_iic_softc *sc;

	/* Get the device data */
	sc = (struct cxm_iic_softc *)device_get_softc(dev);

	/* Set scl to the requested value */
	CSR_WRITE_4(sc, CXM_REG_I2C_SETSCL, ~(int)(val ? 1 : 0));

	/*
	 * PCI writes may be buffered so force the
	 * write to complete by reading the last
	 * location written.
	 */

	CSR_READ_4(sc, CXM_REG_I2C_SETSCL);
}


static void
cxm_iic_setsda(device_t dev, int val)
{
	struct cxm_iic_softc *sc;

	/* Get the device data */
	sc = (struct cxm_iic_softc *)device_get_softc(dev);

	/* Set sda to the requested value */
	CSR_WRITE_4(sc, CXM_REG_I2C_SETSDA, ~(int)(val ? 1 : 0));

	/*
	 * PCI writes may be buffered so force the
	 * write to complete by reading the last
	 * location written.
	 */

	CSR_READ_4(sc, CXM_REG_I2C_SETSDA);
}
