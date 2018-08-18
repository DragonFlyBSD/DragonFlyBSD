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
#include <sys/serialize.h>
#include <sys/syslog.h>
#include <sys/bus.h>

#include <sys/rman.h>

#include "opt_acpi.h"
#include "acpi.h"
#include <dev/acpica/acpivar.h>

#include <bus/pci/pcivar.h>

#include <bus/smbus/smbconf.h>

#include "smbus_if.h"

#include "ig4_reg.h"
#include "ig4_var.h"

ACPI_MODULE_NAME("ig4iic");

static int ig4iic_acpi_probe(device_t dev);
static int ig4iic_acpi_attach(device_t dev);
static int ig4iic_acpi_detach(device_t dev);

static char *ig4iic_ids[] = {
	"INT33C2",
	"INT33C3",
	"INT3432",
	"INT3433",
	"80860F41",
	"808622C1",
	NULL
};

static
int
ig4iic_acpi_probe(device_t dev)
{

        if (acpi_disabled("ig4iic") ||
            ACPI_ID_PROBE(device_get_parent(dev), dev, ig4iic_ids) == NULL)
                return (ENXIO);

	device_set_desc(dev, "Intel SoC I2C Controller");

	return (BUS_PROBE_DEFAULT);
}

static
int
ig4iic_acpi_attach(device_t dev)
{
	ig4iic_softc_t *sc = device_get_softc(dev);
	int error;

	lwkt_serialize_init(&sc->slz);

	sc->dev = dev;
	/* All the HIDs matched are Atom SOCs. */
	sc->version = IG4_ATOM;
	sc->regs_rid = 0;
	sc->regs_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY,
					  &sc->regs_rid, RF_ACTIVE);
	if (sc->regs_res == NULL) {
		device_printf(dev, "unable to map registers");
		ig4iic_acpi_detach(dev);
		return (ENXIO);
	}
	sc->intr_rid = 0;
	sc->intr_res = bus_alloc_resource_any(dev, SYS_RES_IRQ,
					  &sc->intr_rid, RF_ACTIVE);
	if (sc->intr_res == NULL) {
		device_printf(dev, "unable to map interrupt");
		ig4iic_acpi_detach(dev);
		return (ENXIO);
	}
	sc->regs_t = rman_get_bustag(sc->regs_res);
	sc->regs_h = rman_get_bushandle(sc->regs_res);
	sc->pci_attached = 1;

	/* power up the controller */
	pci_set_powerstate(dev, PCI_POWERSTATE_D0);

	error = ig4iic_attach(sc);
	if (error)
		ig4iic_acpi_detach(dev);

	return error;
}

static
int
ig4iic_acpi_detach(device_t dev)
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
	if (sc->regs_res) {
		bus_release_resource(dev, SYS_RES_MEMORY,
				     sc->regs_rid, sc->regs_res);
		sc->regs_res = NULL;
	}
	sc->regs_t = 0;
	sc->regs_h = 0;

	pci_set_powerstate(dev, PCI_POWERSTATE_D3);

	return 0;
}

static device_method_t ig4iic_acpi_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe, ig4iic_acpi_probe),
	DEVMETHOD(device_attach, ig4iic_acpi_attach),
	DEVMETHOD(device_detach, ig4iic_acpi_detach),

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

static driver_t ig4iic_acpi_driver = {
        "ig4iic",
        ig4iic_acpi_methods,
        sizeof(struct ig4iic_softc),
	.gpri = KOBJ_GPRI_ACPI+1
};

static devclass_t ig4iic_acpi_devclass;

DRIVER_MODULE(ig4iic, acpi, ig4iic_acpi_driver, ig4iic_acpi_devclass, NULL, NULL);
MODULE_DEPEND(ig4iic, acpi, 1, 1, 1);
MODULE_DEPEND(ig4iic, smbacpi, 1, 1, 1);
