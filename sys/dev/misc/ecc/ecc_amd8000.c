/*
 * Copyright (c) 2006 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Matthew Dillon <dillon@backplane.com>.   AMD register addresses and
 * values were pulled from MemTest-86 and Linux.
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
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/malloc.h>

#include <bus/pci/pcivar.h>
#include <bus/pci/pcireg.h>
#include <bus/pci/pcibus.h>
#include <bus/pci/pci_cfgreg.h>
#include <bus/pci/pcib_private.h>

#include "pcib_if.h"

struct ecc_amd8000_memctrl {
	uint16_t	vid;
	uint16_t	did;
	const char	*desc;
};

struct ecc_amd8000_softc {
	device_t	ecc_device;
	device_t	ecc_mydev;
	struct callout	ecc_callout;
};

#define ecc_printf(sc, fmt, arg...) \
	device_printf((sc)->ecc_mydev, fmt , ##arg)

static void	ecc_amd8000_callout(void *);
static void	ecc_amd8000_stop(device_t);

static int	ecc_amd8000_probe(device_t);
static int	ecc_amd8000_attach(device_t);
static int	ecc_amd8000_detach(device_t);
static void	ecc_amd8000_shutdown(device_t);

static const struct ecc_amd8000_memctrl ecc_memctrls[] = {
	{ 0x1022, 0x1100, "AMD 8000 memory controller" },
	{ 0x1022, 0x7454, "AMD 8151 memory controller" },
	{ 0, 0, NULL } /* required last entry */
};

static device_method_t ecc_amd8000_methods[] = {
        /* Device interface */
	DEVMETHOD(device_probe,		ecc_amd8000_probe),
	DEVMETHOD(device_attach,	ecc_amd8000_attach),
	DEVMETHOD(device_detach,	ecc_amd8000_detach),
	DEVMETHOD(device_shutdown,	ecc_amd8000_shutdown),
	DEVMETHOD(device_suspend,	bus_generic_suspend),
	DEVMETHOD(device_resume,	bus_generic_resume),
	DEVMETHOD_END
};

static driver_t ecc_amd8000_driver = {
	"ecc",
	ecc_amd8000_methods,
	sizeof(struct ecc_amd8000_softc)
};
static devclass_t ecc_devclass;
DRIVER_MODULE(ecc_amd8000, pci, ecc_amd8000_driver, ecc_devclass, NULL, NULL);
MODULE_DEPEND(ecc_amd8000, pci, 1, 1, 1);

static int
ecc_amd8000_probe(device_t dev)
{
	const struct ecc_amd8000_memctrl *mc;
	uint16_t vid, did;

	vid = pci_get_vendor(dev);
	did = pci_get_device(dev);

	for (mc = ecc_memctrls; mc->desc != NULL; ++mc) {
		if (mc->vid == vid && mc->did == did) {
			struct ecc_amd8000_softc *sc = device_get_softc(dev);

			device_set_desc(dev, mc->desc);
			sc->ecc_mydev = dev;
			sc->ecc_device = device_get_parent(dev);
			return (0);
		}
	}
	return (ENXIO);
}

static int
ecc_amd8000_attach(device_t dev)
{
	struct ecc_amd8000_softc *sc = device_get_softc(dev);
	uint32_t draminfo, eccinfo;
	int bus, slot, poll = 0;

	callout_init_mp(&sc->ecc_callout);

	dev = sc->ecc_device; /* XXX */

	bus = pci_get_bus(dev);
	slot = pci_get_slot(dev);

	/*
	 * The memory bridge is recognized as four PCI devices
	 * using function codes 0, 1, 2, and 3.  We probe for the
	 * device at function code 0 and assume that all four exist.
	 */
	draminfo = pcib_read_config(dev, bus, slot, 2, 0x90, 4);
	eccinfo = pcib_read_config(dev, bus, slot, 3, 0x44, 4);

	if ((draminfo >> 17) & 1)
		ecc_printf(sc, "memory type: ECC\n");
	else
		ecc_printf(sc, "memory type: NON-ECC\n");
	switch((eccinfo >> 22) & 3) {
	case 0:
		ecc_printf(sc, "ecc mode: DISABLED\n");
		break;
	case 1:
		ecc_printf(sc, "ecc mode: ENABLED/CORRECT-MODE\n");
		poll = 1;
		break;
	case 2:
		ecc_printf(sc, "ecc mode: ENABLED/RESERVED (disabled)\n");
		break;
	case 3:
		ecc_printf(sc, "ecc mode: ENABLED/CHIPKILL-MODE\n");
		poll = 1;
		break;
	}

	/*
	 * Enable ECC logging and clear any previous error.
	 */
	if (poll) {
		uint64_t v64;
		uint32_t v32;

		v64 = rdmsr(0x017B);
		wrmsr(0x17B, (v64 & ~0xFFFFFFFFLL) | 0x00000010LL);
		v32 = pcib_read_config(dev, bus, slot, 3, 0x4C, 4);
		v32 &= 0x7F801EFC;
		pcib_write_config(dev, bus, slot, 3, 0x4C, v32, 4);

		callout_reset(&sc->ecc_callout, hz, ecc_amd8000_callout, sc);
	}
	return (0);
}

static void
ecc_amd8000_callout(void *xsc)
{
	struct ecc_amd8000_softc *sc = xsc;
	device_t dev = sc->ecc_device;
	uint32_t v32, addr;
	int bus, slot;

	bus = pci_get_bus(dev);
	slot = pci_get_slot(dev);

	/*
	 * The address calculation is not entirely correct.  We need to
	 * look at the AMD chipset documentation.
	 */
	v32 = pcib_read_config(dev, bus, slot, 3, 0x4C, 4);
	if ((v32 & 0x80004000) == 0x80004000) {
		addr = pcib_read_config(dev, bus, slot, 3, 0x50, 4);
		ecc_printf(sc, "Correctable ECC error at %08x\n", addr);
		pcib_write_config(dev, bus, slot, 3, 0x4C, v32 & 0x7F801EFC, 4);
	} else if ((v32 & 0x80002000) == 0x80002000) {
		addr = pcib_read_config(dev, bus ,slot, 3, 0x50, 4);
		ecc_printf(sc, "Uncorrectable ECC error at %08x\n", addr);
		pcib_write_config(dev, bus, slot, 3, 0x4C, v32 & 0x7F801EFC, 4);
	}
	callout_reset(&sc->ecc_callout, hz, ecc_amd8000_callout, sc);
}

static void
ecc_amd8000_stop(device_t dev)
{
	struct ecc_amd8000_softc *sc = device_get_softc(dev);

	callout_stop_sync(&sc->ecc_callout);
}

static int
ecc_amd8000_detach(device_t dev)
{
	ecc_amd8000_stop(dev);
	return 0;
}

static void
ecc_amd8000_shutdown(device_t dev)
{
	ecc_amd8000_stop(dev);
}
