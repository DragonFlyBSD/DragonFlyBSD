/*
 * Copyright (c) 2014 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Matthew Dillon <dillon@backplane.com>
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
/*
 * Intel 4th generation mobile cpus integrated I2C device, smbus driver.
 *
 * See ig4_reg.h for datasheet reference and notes.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/errno.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/syslog.h>
#include <sys/bus.h>

#include <sys/rman.h>

#include <bus/pci/pcivar.h>
#include <bus/pci/pcireg.h>
#include <bus/smbus/smbconf.h>

#include "smbus_if.h"

#include "ig4_reg.h"
#include "ig4_var.h"

static int ig4iic_pci_detach(device_t dev);

#define PCI_CHIP_LYNXPT_LP_I2C_1	0x9c618086
#define PCI_CHIP_LYNXPT_LP_I2C_2	0x9c628086

static
int
ig4iic_pci_probe(device_t dev)
{
	switch(pci_get_devid(dev)) {
	case PCI_CHIP_LYNXPT_LP_I2C_1:
		device_set_desc(dev, "Intel Lynx Point-LP I2C Controller-1");
		break;
	case PCI_CHIP_LYNXPT_LP_I2C_2:
		device_set_desc(dev, "Intel Lynx Point-LP I2C Controller-2");
		break;
	default:
		return(ENXIO);
	}
	return BUS_PROBE_DEFAULT;
}

static
int
ig4iic_pci_attach(device_t dev)
{
	ig4iic_softc_t *sc = device_get_softc(dev);
	u_int irq_flags;
	int msi_enable = 1;
	int error;

	bzero(sc, sizeof(*sc));

	lockinit(&sc->lk, "ig4iic", 0, LK_CANRECURSE);

	sc->dev = dev;
	sc->regs_rid = PCIR_BAR(0);
	sc->regs_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY,
					  &sc->regs_rid, RF_ACTIVE);
	if (sc->regs_res == NULL) {
		device_printf(dev, "unable to map registers");
		ig4iic_pci_detach(dev);
		return (ENXIO);
	}
	sc->intr_type = pci_alloc_1intr(dev, msi_enable,
					&sc->intr_rid, &irq_flags);
	sc->intr_res = bus_alloc_resource_any(dev, SYS_RES_IRQ,
					  &sc->intr_rid, irq_flags);
	if (sc->intr_res == NULL) {
		device_printf(dev, "unable to map interrupt");
		ig4iic_pci_detach(dev);
		return (ENXIO);
	}
	sc->regs_t = rman_get_bustag(sc->regs_res);
	sc->regs_h = rman_get_bushandle(sc->regs_res);
	sc->pci_attached = 1;

	error = ig4iic_attach(sc);
	if (error)
		ig4iic_pci_detach(dev);

	return error;
}

static
int
ig4iic_pci_detach(device_t dev)
{
	ig4iic_softc_t *sc = device_get_softc(dev);
	int error;

	if (sc->pci_attached) {
		error = ig4iic_detach(sc);
		if (error)
			return error;
		sc->pci_attached = 0;
	}

	if (sc->intr_res) {
		bus_release_resource(dev, SYS_RES_IRQ,
				     sc->intr_rid, sc->intr_res);
		sc->intr_res = NULL;
	}
	if (sc->intr_type == PCI_INTR_TYPE_MSI)
		pci_release_msi(dev);
	if (sc->regs_res) {
		bus_release_resource(dev, SYS_RES_MEMORY,
				     sc->regs_rid, sc->regs_res);
		sc->regs_res = NULL;
	}
	sc->regs_t = 0;
	sc->regs_h = 0;
	lockuninit(&sc->lk);

	return 0;
}

static device_method_t ig4iic_pci_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe, ig4iic_pci_probe),
	DEVMETHOD(device_attach, ig4iic_pci_attach),
	DEVMETHOD(device_detach, ig4iic_pci_detach),

	/* Bus methods */
	DEVMETHOD(bus_print_child, bus_generic_print_child),

	/* SMBus methods from ig4_smb.c */
	DEVMETHOD(smbus_callback, ig4iic_smb_callback),
	DEVMETHOD(smbus_quick, ig4iic_smb_quick),
	DEVMETHOD(smbus_sendb, ig4iic_smb_sendb),
	DEVMETHOD(smbus_recvb, ig4iic_smb_recvb),
	DEVMETHOD(smbus_writeb, ig4iic_smb_writeb),
	DEVMETHOD(smbus_writew, ig4iic_smb_writew),
	DEVMETHOD(smbus_readb, ig4iic_smb_readb),
	DEVMETHOD(smbus_readw, ig4iic_smb_readw),
	DEVMETHOD(smbus_pcall, ig4iic_smb_pcall),
	DEVMETHOD(smbus_bwrite, ig4iic_smb_bwrite),
	DEVMETHOD(smbus_bread, ig4iic_smb_bread),
	DEVMETHOD(smbus_trans, ig4iic_smb_trans),
	DEVMETHOD_END
};

static driver_t ig4iic_pci_driver = {
        "ig4iic",
        ig4iic_pci_methods,
        sizeof(struct ig4iic_softc)
};

static devclass_t ig4iic_pci_devclass;

DRIVER_MODULE(ig4iic, pci, ig4iic_pci_driver, ig4iic_pci_devclass, NULL, NULL);
MODULE_DEPEND(ig4iic, pci, 1, 1, 1);
MODULE_DEPEND(ig4iic, smbus, SMBUS_MINVER, SMBUS_PREFVER, SMBUS_MAXVER);
MODULE_VERSION(ig4iic, 1);
