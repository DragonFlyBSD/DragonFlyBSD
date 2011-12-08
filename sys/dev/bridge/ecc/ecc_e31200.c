/*
 * Copyright (c) 2011 The DragonFly Project.  All rights reserved.
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
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/bitops.h>

#include <bus/pci/pcivar.h>
#include <bus/pci/pcireg.h>
#include <bus/pci/pcibus.h>
#include <bus/pci/pci_cfgreg.h>
#include <bus/pci/pcib_private.h>

#include <vm/pmap.h>

#include "pcib_if.h"

#include <dev/bridge/ecc/ecc_e31200_reg.h>

struct ecc_e31200_memctrl {
	uint16_t	vid;
	uint16_t	did;
	const char	*desc;
};

struct ecc_e31200_softc {
	device_t	ecc_device;
	device_t	ecc_mydev;
	struct callout	ecc_callout;
	volatile uint8_t *ecc_addr;
};

#define CSR_READ_4(sc, ofs)	(*(volatile uint32_t *)((sc)->ecc_addr + (ofs)))

#define ecc_printf(sc, fmt, arg...) \
	device_printf((sc)->ecc_mydev, fmt , ##arg)

static int	ecc_e31200_probe(device_t);
static int	ecc_e31200_attach(device_t);

static void	ecc_e31200_chaninfo(struct ecc_e31200_softc *, uint32_t,
		    const char *);
static void	ecc_e31200_status(struct ecc_e31200_softc *);
static void	ecc_e31200_callout(void *);
static void	ecc_e31200_errlog(struct ecc_e31200_softc *);
static void	ecc_e31200_errlog_ch(struct ecc_e31200_softc *, int, int,
		    const char *);

static const struct ecc_e31200_memctrl ecc_memctrls[] = {
	{ 0x8086, 0x0108, "Intel E3-1200 memory controller" },
	{ 0, 0, NULL } /* required last entry */
};

static device_method_t ecc_e31200_methods[] = {
        /* Device interface */
        DEVMETHOD(device_probe,		ecc_e31200_probe),
        DEVMETHOD(device_attach,	ecc_e31200_attach),
        DEVMETHOD(device_shutdown,	bus_generic_shutdown),
        DEVMETHOD(device_suspend,	bus_generic_suspend),
        DEVMETHOD(device_resume,	bus_generic_resume),
        { 0, 0 }
};

static driver_t ecc_e31200_driver = {
	"ecc",
	ecc_e31200_methods,
	sizeof(struct ecc_e31200_softc)
};
static devclass_t ecc_devclass;
DRIVER_MODULE(ecc_e31200, hostb, ecc_e31200_driver, ecc_devclass, NULL, NULL);
MODULE_DEPEND(ecc_e31200, pci, 1, 1, 1);

static int
ecc_e31200_probe(device_t dev)
{
	const struct ecc_e31200_memctrl *mc;
	uint16_t vid, did;

	vid = pci_get_vendor(dev);
	did = pci_get_device(dev);

	for (mc = ecc_memctrls; mc->desc != NULL; ++mc) {
		if (mc->vid == vid && mc->did == did) {
			struct ecc_e31200_softc *sc = device_get_softc(dev);

			device_set_desc(dev, mc->desc);
			sc->ecc_mydev = dev;
			sc->ecc_device = device_get_parent(dev);
			return (0);
		}
	}
	return (ENXIO);
}

static int
ecc_e31200_attach(device_t dev)
{
	struct ecc_e31200_softc *sc = device_get_softc(dev);
	uint32_t capa, dmfc, mch_barlo, mch_barhi;
	uint64_t mch_bar;
	int bus, slot;

	dev = sc->ecc_device; /* XXX */

	bus = pci_get_bus(dev);
	slot = pci_get_slot(dev);

	capa = pcib_read_config(dev, bus, slot, 0, PCI_E31200_CAPID0_A, 4);

	dmfc = __SHIFTOUT(capa, PCI_E31200_CAPID0_A_DMFC);
	if (dmfc == PCI_E31200_CAPID0_A_DMFC_1333) {
		ecc_printf(sc, "CAP DDR3 1333 ");
	} else if (dmfc == PCI_E31200_CAPID0_A_DMFC_1067) {
		ecc_printf(sc, "CAP DDR3 1067 ");
	} else if (dmfc == PCI_E31200_CAPID0_A_DMFC_ALL) {
		ecc_printf(sc, "no CAP ");
	} else {
		ecc_printf(sc, "unknown DMFC %#x\n", dmfc);
		return 0;
	}

	if (capa & PCI_E31200_CAPID0_A_ECCDIS) {
		kprintf("NON-ECC\n");
		return 0;
	} else {
		kprintf("ECC\n");
	}

	mch_barlo = pcib_read_config(dev, bus, slot, 0,
	    PCI_E31200_MCHBAR_LO, 4);
	mch_barhi = pcib_read_config(dev, bus, slot, 0,
	    PCI_E31200_MCHBAR_HI, 4);

	mch_bar = (uint64_t)mch_barlo | (((uint64_t)mch_barhi) << 32);
	if (bootverbose)
		ecc_printf(sc, "MCHBAR %jx\n", (uintmax_t)mch_bar);

	if (mch_bar & PCI_E31200_MCHBAR_LO_EN) {
		uint64_t map_addr = mch_bar & PCI_E31200_MCHBAR_ADDRMASK;
		uint32_t dimm_ch0, dimm_ch1;

		sc->ecc_addr = pmap_mapdev_uncacheable(map_addr,
		    MCH_E31200_SIZE);

		if (bootverbose) {
			ecc_printf(sc, "LOG0_C0 %#x\n",
			    CSR_READ_4(sc, MCH_E31200_ERRLOG0_C0));
			ecc_printf(sc, "LOG0_C1 %#x\n",
			    CSR_READ_4(sc, MCH_E31200_ERRLOG0_C1));
		}

		dimm_ch0 = CSR_READ_4(sc, MCH_E31200_DIMM_CH0);
		dimm_ch1 = CSR_READ_4(sc, MCH_E31200_DIMM_CH1);

		if (bootverbose) {
			ecc_e31200_chaninfo(sc, dimm_ch0, "channel0");
			ecc_e31200_chaninfo(sc, dimm_ch1, "channel1");
		}

		if (((dimm_ch0 | dimm_ch1) & MCH_E31200_DIMM_ECC) == 0) {
			ecc_printf(sc, "No ECC active\n");
			pmap_unmapdev((vm_offset_t)sc->ecc_addr,
			    MCH_E31200_SIZE);
			return 0;
		}
	}

	ecc_e31200_status(sc);
	callout_init_mp(&sc->ecc_callout);
	callout_reset(&sc->ecc_callout, hz, ecc_e31200_callout, sc);

	return 0;
}

static void
ecc_e31200_callout(void *xsc)
{
	struct ecc_e31200_softc *sc = xsc;

	ecc_e31200_status(sc);
	callout_reset(&sc->ecc_callout, hz, ecc_e31200_callout, sc);
}

static void
ecc_e31200_status(struct ecc_e31200_softc *sc)
{
	device_t dev = sc->ecc_device;
	uint16_t errsts;
	int bus, slot;

	bus = pci_get_bus(dev);
	slot = pci_get_slot(dev);

	errsts = pcib_read_config(dev, bus, slot, 0, 0xc8, 2);
	if (errsts & 0x2)
		ecc_printf(sc, "Uncorrectable ECC error\n");
	else if (errsts & 0x1)
		ecc_printf(sc, "Correctable ECC error\n");

	if (errsts & 0x3) {
		if (sc->ecc_addr != NULL)
			ecc_e31200_errlog(sc);

		/* Clear pending errors */
		pcib_write_config(dev, bus, slot, 0, 0xc8, errsts, 2);
	}
}

static void
ecc_e31200_chaninfo(struct ecc_e31200_softc *sc, uint32_t dimm_ch,
    const char *desc)
{
	int size_a, size_b, ecc;

	size_a = __SHIFTOUT(dimm_ch, MCH_E31200_DIMM_A_SIZE);
	if (size_a != 0) {
		ecc_printf(sc, "%s, DIMM A %dMB %s %s\n", desc,
		    size_a * MCH_E31200_DIMM_SIZE_UNIT,
		    (dimm_ch & MCH_E31200_DIMM_A_X16) ? "X16" : "X8",
		    (dimm_ch & MCH_E31200_DIMM_A_DUAL_RANK) ?
		        "DUAL" : "SINGLE");
	}

	size_b = __SHIFTOUT(dimm_ch, MCH_E31200_DIMM_B_SIZE);
	if (size_b != 0) {
		ecc_printf(sc, "%s, DIMM B %dMB %s %s\n", desc,
		    size_b * MCH_E31200_DIMM_SIZE_UNIT,
		    (dimm_ch & MCH_E31200_DIMM_B_X16) ? "X16" : "X8",
		    (dimm_ch & MCH_E31200_DIMM_B_DUAL_RANK) ?
		        "DUAL" : "SINGLE");
	}

	if (size_a == 0 && size_b == 0)
		return;

	ecc = __SHIFTOUT(dimm_ch, MCH_E31200_DIMM_ECC);
	if (ecc == MCH_E31200_DIMM_ECC_NONE)
		ecc_printf(sc, "%s, no ECC active\n", desc);
	else if (ecc == MCH_E31200_DIMM_ECC_IO)
		ecc_printf(sc, "%s, ECC active IO\n", desc);
	else if (ecc == MCH_E31200_DIMM_ECC_LOGIC)
		ecc_printf(sc, "%s, ECC active logic\n", desc);
	else
		ecc_printf(sc, "%s, ECC active IO/logic\n", desc);

	if (dimm_ch & (MCH_E31200_DIMM_ENHI | MCH_E31200_DIMM_RI)) {
		ecc_printf(sc, "%s", desc);
		if (dimm_ch & MCH_E31200_DIMM_RI)
			kprintf(", rank interleave");
		if (dimm_ch & MCH_E31200_DIMM_ENHI)
			kprintf(", enhanced interleave");
		kprintf("\n");
	}
}

static void
ecc_e31200_errlog(struct ecc_e31200_softc *sc)
{
	ecc_e31200_errlog_ch(sc, MCH_E31200_ERRLOG0_C0, MCH_E31200_ERRLOG1_C0,
	    "channel0");
	ecc_e31200_errlog_ch(sc, MCH_E31200_ERRLOG0_C1, MCH_E31200_ERRLOG1_C1,
	    "channel1");
}

static void
ecc_e31200_errlog_ch(struct ecc_e31200_softc *sc,
    int err0_ofs, int err1_ofs, const char *desc)
{
	uint32_t err0, err1;

	err0 = CSR_READ_4(sc, err0_ofs);
	if ((err0 & (MCH_E31200_ERRLOG0_CERRSTS | MCH_E31200_ERRLOG0_MERRSTS))
	    == 0)
		return;

	err1 = CSR_READ_4(sc, err1_ofs);

	ecc_printf(sc, "%s error @bank %d, rank %d, chunk %d, syndrome %d, "
	    "row %d, col %d\n", desc,
	    __SHIFTOUT(err0, MCH_E31200_ERRLOG0_ERRBANK),
	    __SHIFTOUT(err0, MCH_E31200_ERRLOG0_ERRRANK),
	    __SHIFTOUT(err0, MCH_E31200_ERRLOG0_ERRCHUNK),
	    __SHIFTOUT(err0, MCH_E31200_ERRLOG0_ERRSYND),
	    __SHIFTOUT(err1, MCH_E31200_ERRLOG1_ERRROW),
	    __SHIFTOUT(err1, MCH_E31200_ERRLOG1_ERRCOL));
}
