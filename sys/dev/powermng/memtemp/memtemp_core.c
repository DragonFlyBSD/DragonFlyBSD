/*
 * Copyright (c) 2015 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Sepherosa Ziehau <sepherosa@gmail.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name of The DragonFly Project nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific, prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bitops.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/sensors.h>

#include <bus/pci/pcivar.h>
#include <bus/pci/pcireg.h>
#include <bus/pci/pcibus.h>
#include <bus/pci/pci_cfgreg.h>

#include "coremctl_if.h"
#include "pcib_if.h"

#include <dev/misc/coremctl/coremctl_reg.h>
#include <dev/misc/dimm/dimm.h>

struct memtemp_core_softc;

struct memtemp_core_dimm {
	TAILQ_ENTRY(memtemp_core_dimm)	dimm_link;
	struct ksensor			dimm_sensor;
	struct memtemp_core_softc	*dimm_parent;
	int				dimm_reg;
	uint32_t			dimm_mask;

	struct dimm_softc		*dimm_softc;
};

struct memtemp_core_type {
	uint16_t	did;
	const char	*desc;
};

struct memtemp_core_softc {
	device_t			temp_dev;
	device_t			temp_parent;
	TAILQ_HEAD(, memtemp_core_dimm)	temp_dimm;
};

static int	memtemp_core_probe(device_t);
static int	memtemp_core_attach(device_t);
static int	memtemp_core_detach(device_t);

static void	memtemp_core_chan_attach(struct memtemp_core_softc *, int);
static void	memtemp_core_dimm_attach(struct memtemp_core_softc *,
		    int, int, int);
static void	memtemp_core_sensor_task(void *);

static const struct memtemp_core_type memtemp_core_types[] = {
	{ PCI_E3V3_MEMCTL_DID,
	  "Intel E3 v3 memory thermal sensor" },

	{ PCI_COREV3_MEMCTL_DID,
	  "Intel i3/i5/i7 Haswell memory thermal sensor" },

	{ 0, NULL } /* required last entry */
};

static device_method_t memtemp_core_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		memtemp_core_probe),
	DEVMETHOD(device_attach,	memtemp_core_attach),
	DEVMETHOD(device_detach,	memtemp_core_detach),
	DEVMETHOD(device_shutdown,	bus_generic_shutdown),
	DEVMETHOD(device_suspend,	bus_generic_suspend),
	DEVMETHOD(device_resume,	bus_generic_resume),
	DEVMETHOD_END
};

static driver_t memtemp_core_driver = {
	"memtemp",
	memtemp_core_methods,
	sizeof(struct memtemp_core_softc)
};
static devclass_t memtemp_devclass;
DRIVER_MODULE(memtemp_core, coremctl, memtemp_core_driver, memtemp_devclass,
    NULL, NULL);
MODULE_DEPEND(memtemp_core, pci, 1, 1, 1);
MODULE_DEPEND(memtemp_core, coremctl, 1, 1, 1);
MODULE_DEPEND(memtemp_core, dimm, 1, 1, 1);

static __inline uint32_t
CSR_READ_4(struct memtemp_core_softc *sc, int ofs)
{
	uint32_t val;
	int error;

	error = COREMCTL_MCH_READ(sc->temp_parent, ofs, &val);
	KASSERT(!error, ("mch read failed"));

	return val;
}

static __inline void
CSR_WRITE_4(struct memtemp_core_softc *sc, int ofs, uint32_t val)
{
	int error;

	error = COREMCTL_MCH_WRITE(sc->temp_parent, ofs, val);
	KASSERT(!error, ("mch write failed"));
}

static int
memtemp_core_probe(device_t dev)
{
	const struct memtemp_core_type *t;
	uint16_t did;

	if (pci_get_vendor(dev) != PCI_CORE_MEMCTL_VID)
		return ENXIO;

	did = pci_get_device(dev);
	for (t = memtemp_core_types; t->desc != NULL; ++t) {
		if (t->did == did) {
			device_set_desc(dev, t->desc);
			return 0;
		}
	}
	return ENXIO;
}

static int
memtemp_core_attach(device_t dev)
{
	struct memtemp_core_softc *sc = device_get_softc(dev);
	int i;

	sc->temp_dev = dev;
	sc->temp_parent = device_get_parent(dev);
	TAILQ_INIT(&sc->temp_dimm);

	for (i = 0; i < PCI_CORE_MEMCTL_CHN_MAX; ++i)
		memtemp_core_chan_attach(sc, i);

	return 0;
}

static void
memtemp_core_chan_attach(struct memtemp_core_softc *sc, int chan)
{
	int dimm_ch_reg, dimm_chtemp_reg;
	int dimma_id, dimmb_id;
	int size_a, size_b;
	uint32_t dimm_ch;

	if (chan == 0) {
		dimm_ch_reg = MCH_CORE_DIMM_CH0;
		dimm_chtemp_reg = MCH_CORE_DIMM_TEMP_CH0;
	} else {
		KASSERT(chan == 1, ("unsupport channel%d", chan));
		dimm_ch_reg = MCH_CORE_DIMM_CH1;
		dimm_chtemp_reg = MCH_CORE_DIMM_TEMP_CH1;
	}

	dimm_ch = CSR_READ_4(sc, dimm_ch_reg);

	size_a = __SHIFTOUT(dimm_ch, MCH_CORE_DIMM_A_SIZE);
	size_b = __SHIFTOUT(dimm_ch, MCH_CORE_DIMM_B_SIZE);
	if (size_a == 0 && size_b == 0)
		return;

	dimma_id = 0;
	dimmb_id = 1;
	if (dimm_ch & MCH_CORE_DIMM_A_SELECT) {
		dimma_id = 1;
		dimmb_id = 0;
	}

	if (size_a != 0)
		memtemp_core_dimm_attach(sc, chan, dimma_id, dimm_chtemp_reg);
	if (size_b != 0)
		memtemp_core_dimm_attach(sc, chan, dimmb_id, dimm_chtemp_reg);
}

static void
memtemp_core_dimm_attach(struct memtemp_core_softc *sc, int chan, int dimm_id,
    int dimm_reg)
{
	struct memtemp_core_dimm *dimm_sc;
	struct ksensor *sens;

	dimm_sc = kmalloc(sizeof(*dimm_sc), M_DEVBUF, M_WAITOK | M_ZERO);
	dimm_sc->dimm_parent = sc;
	dimm_sc->dimm_reg = dimm_reg;
	if (dimm_id == 0) {
		dimm_sc->dimm_mask = MCH_CORE_DIMM_TEMP_DIMM0;
	} else {
		KASSERT(dimm_id == 1, ("unsupported DIMM%d", dimm_id));
		dimm_sc->dimm_mask = MCH_CORE_DIMM_TEMP_DIMM1;
	}

	dimm_sc->dimm_softc = dimm_create(0, chan, dimm_id);

	sens = &dimm_sc->dimm_sensor;
	ksnprintf(sens->desc, sizeof(sens->desc), "chan%d DIMM%d",
	    chan, dimm_id);
	sens->type = SENSOR_TEMP;
	dimm_sensor_attach(dimm_sc->dimm_softc, sens);
	sensor_task_register(dimm_sc, memtemp_core_sensor_task, 5);

	TAILQ_INSERT_TAIL(&sc->temp_dimm, dimm_sc, dimm_link);
}

static int
memtemp_core_detach(device_t dev)
{
	struct memtemp_core_softc *sc = device_get_softc(dev);
	struct memtemp_core_dimm *dimm_sc;

	while ((dimm_sc = TAILQ_FIRST(&sc->temp_dimm)) != NULL) {
		TAILQ_REMOVE(&sc->temp_dimm, dimm_sc, dimm_link);

		sensor_task_unregister(dimm_sc);
		dimm_sensor_detach(dimm_sc->dimm_softc, &dimm_sc->dimm_sensor);
		dimm_destroy(dimm_sc->dimm_softc);

		kfree(dimm_sc, M_DEVBUF);
	}
	return 0;
}

static void
memtemp_core_sensor_task(void *xdimm_sc)
{
	struct memtemp_core_dimm *dimm_sc = xdimm_sc;
	uint32_t val;
	int temp;

	val = CSR_READ_4(dimm_sc->dimm_parent, dimm_sc->dimm_reg);
	temp = __SHIFTOUT(val, dimm_sc->dimm_mask);

	dimm_sensor_temp(dimm_sc->dimm_softc, &dimm_sc->dimm_sensor, temp);
}
