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

static int setup_none(device_t dev);
static int setup_amd64(device_t dev);
static void poll_amd64(void *dev_arg);

struct pci_memory_controller { 
	uint16_t	vid;
	uint16_t	did;
	const char	*desc;
	int (*setup)(device_t dev);
};

struct pci_ecc_softc {
	struct pci_memory_controller *config;
	struct callout poll_callout;
	int poll_enable;
};

static struct pci_memory_controller mem_controllers[] = {
	/* AMD */
	{ 0x1022, 0x7006, "AMD 751", setup_none },
	{ 0x1022, 0x700c, "AMD 762", setup_none },
	{ 0x1022, 0x700e, "AMD 761", setup_none },
	{ 0x1022, 0x1100, "AMD 8000", setup_amd64 },
	{ 0x1022, 0x7454, "AMD 8000", setup_amd64 }
};

static int
pci_ecc_probe(device_t dev)
{
	struct pci_ecc_softc *sc;
	uint16_t vid;
	uint16_t did;
	int i;

	vid = pci_get_vendor(dev);
	did = pci_get_device(dev);

	for (i = 0; i < NELEM(mem_controllers); ++i) {
		if (mem_controllers[i].vid == vid &&
		    mem_controllers[i].did == did
		) {
			sc = device_get_softc(dev);
			sc->config = &mem_controllers[i];
			return(0);
		}
	}
	return (ENXIO);
}

static int
pci_ecc_attach(device_t dev)
{
	struct pci_ecc_softc *sc = device_get_softc(dev);

	return (sc->config->setup(dev));
}

static device_method_t pci_ecc_methods[] = {
        /* Device interface */
        DEVMETHOD(device_probe,         pci_ecc_probe),
        DEVMETHOD(device_attach,        pci_ecc_attach),
        DEVMETHOD(device_shutdown,      bus_generic_shutdown),
        DEVMETHOD(device_suspend,       bus_generic_suspend),
        DEVMETHOD(device_resume,        bus_generic_resume),
        { 0, 0 }
};

static driver_t pci_ecc_driver = {
	"ecc",
	pci_ecc_methods,
	sizeof(struct pci_ecc_softc)
};
static devclass_t ecc_devclass;
DRIVER_MODULE(ecc, pci, pci_ecc_driver, ecc_devclass, 0, 0);
MODULE_DEPEND(ecc, pci, 1, 1, 1);

/*
 * Architecture-specific procedures
 */
static
int
setup_none(device_t dev)
{
	return(0);
}

static
int
setup_amd64(device_t dev)
{
	struct pci_ecc_softc *sc = device_get_softc(dev);
	uint32_t draminfo;
	uint32_t eccinfo;
	int bus = pci_get_bus(dev);
	int slot = pci_get_slot(dev);

	/*
	 * The memory bridge is recognized as four PCI devices
	 * using function codes 0, 1, 2, and 3.  We probe for the
	 * device at function code 0 and assume that all four exist.
	 */
	draminfo = pcib_read_config(dev, bus, slot, 2, 0x90, 4);
	eccinfo = pcib_read_config(dev, bus, slot, 3, 0x44, 4);

	device_printf(dev, "attached %s memory controller\n", sc->config->desc);
	if ((draminfo >> 17) & 1)
		device_printf(dev, "memory type: ECC\n");
	else
		device_printf(dev, "memory type: NON-ECC\n");
	switch((eccinfo >> 22) & 3) {
	case 0:
		device_printf(dev, "ecc mode: DISABLED\n");
		break;
	case 1:
		device_printf(dev, "ecc mode: ENABLED/CORRECT-MODE\n");
		sc->poll_enable = 1;
		break;
	case 2:
		device_printf(dev, "ecc mode: ENABLED/RESERVED (disabled)\n");
		break;
	case 3:
		device_printf(dev, "ecc mode: ENABLED/CHIPKILL-MODE\n");
		sc->poll_enable = 1;
		break;
	}

	/*
	 * Enable ECC logging and clear any previous error.
	 */
	if (sc->poll_enable) {
		uint64_t v64;
		uint32_t v32;

		v64 = rdmsr(0x017B);
		wrmsr(0x17B, (v64 & ~0xFFFFFFFFLL) | 0x00000010LL);
		v32 = pcib_read_config(dev, bus, slot, 3, 0x4C, 4);
		v32 &= 0x7F801EFC;
		pcib_write_config(dev, bus, slot, 3, 0x4C, v32, 4);

		callout_init(&sc->poll_callout);
		callout_reset(&sc->poll_callout, hz, poll_amd64, dev);
	}
	return(0);
}

static
void
poll_amd64(void *dev_arg)
{
	device_t dev = dev_arg;
	struct pci_ecc_softc *sc = device_get_softc(dev);
	int bus = pci_get_bus(dev);
	int slot = pci_get_slot(dev);
	uint32_t v32;
	uint32_t addr;

	/*
	 * The address calculation is not entirely correct.  We need to
	 * look at the AMD chipset documentation.
	 */
	v32 = pcib_read_config(dev, bus, slot, 3, 0x4C, 4);
	if ((v32 & 0x80004000) == 0x80004000) {
		addr = pcib_read_config(dev, bus, slot, 3, 0x50, 4);
		device_printf(dev, "Correctable ECC error at %08x\n", addr);
		pcib_write_config(dev, bus, slot, 3, 0x4C, v32 & 0x7F801EFC, 4);
	} else if ((v32 & 0x80002000) == 0x80002000) {
		addr = pcib_read_config(dev, bus ,slot, 3, 0x50, 4);
		device_printf(dev, "Uncorrectable ECC error at %08x\n", addr);
		pcib_write_config(dev, bus, slot, 3, 0x4C, v32 & 0x7F801EFC, 4);
	}
	callout_reset(&sc->poll_callout, hz, poll_amd64, dev);
}

