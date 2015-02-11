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
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/bitops.h>

#include <bus/pci/pcivar.h>
#include <bus/pci/pcireg.h>
#include <bus/pci/pcibus.h>
#include <bus/pci/pci_cfgreg.h>

#include <vm/pmap.h>

#include "coremctl_if.h"
#include "pcib_if.h"

#include <dev/misc/coremctl/coremctl_reg.h>

#define COREMCTL_VER_1	1	/* Sandy Bridge */
#define COREMCTL_VER_2	2	/* Ivy Bridge */
#define COREMCTL_VER_3	3	/* Haswell */

struct coremctl_type {
	uint16_t	did;
	const char	*desc;
	int		ver;		/* COREMCTL_VER_ */
};

struct coremctl_softc {
	device_t	sc_dev;
	int		sc_ver;	/* COREMCTL_VER_ */
	device_t	sc_ecc;
	volatile uint8_t *sc_mch;
};

#define CSR_READ_4(sc, ofs)	(*(volatile uint32_t *)((sc)->sc_mch + (ofs)))

static void	coremctl_identify(driver_t *, device_t);
static int	coremctl_probe(device_t);
static int	coremctl_attach(device_t);
static int	coremctl_detach(device_t);
static int	coremctl_mch_readreg(device_t, int, uint32_t *);
static int	coremctl_pci_read_ivar(device_t, device_t, int, uintptr_t *);
static uint32_t	coremctl_pci_read_config(device_t, device_t, int, int);
static void	coremctl_pci_write_config(device_t, device_t, int, uint32_t,
		    int);

static void	coremctl_chaninfo(struct coremctl_softc *, uint32_t,
		    const char *);

static const struct coremctl_type coremctl_types[] = {
	{ PCI_E3V1_MEMCTL_DID, "Intel E3 memory controller",
	  COREMCTL_VER_1 },

	{ PCI_E3V2_MEMCTL_DID, "Intel E3 v2 memory controller",
	  COREMCTL_VER_2 },

	{ PCI_E3V3_MEMCTL_DID, "Intel E3 v3 memory controller",
	  COREMCTL_VER_3 },

	{ PCI_COREV3_MEMCTL_DID, "Intel i3/i5/i7 Haswell memory controller",
	  COREMCTL_VER_3 },

	{ 0, NULL, 0 } /* required last entry */
};

static device_method_t coremctl_methods[] = {
	/* Device interface */
	DEVMETHOD(device_identify,	coremctl_identify),
	DEVMETHOD(device_probe,		coremctl_probe),
	DEVMETHOD(device_attach,	coremctl_attach),
	DEVMETHOD(device_detach,	coremctl_detach),
	DEVMETHOD(device_shutdown,	bus_generic_shutdown),
	DEVMETHOD(device_suspend,	bus_generic_suspend),
	DEVMETHOD(device_resume,	bus_generic_resume),

	/* Bus interface */
	DEVMETHOD(bus_read_ivar,	coremctl_pci_read_ivar),

	/* PCI interface */
	DEVMETHOD(pci_read_config,	coremctl_pci_read_config),
	DEVMETHOD(pci_write_config,	coremctl_pci_write_config),

	/* Core memory controller interface */
	DEVMETHOD(coremctl_mch_read,	coremctl_mch_readreg),

	DEVMETHOD_END
};

static driver_t coremctl_driver = {
	"coremctl",
	coremctl_methods,
	sizeof(struct coremctl_softc)
};
static devclass_t coremctl_devclass;

DRIVER_MODULE(coremctl, hostb, coremctl_driver, coremctl_devclass, NULL, NULL);
MODULE_VERSION(coremctl, 1);
MODULE_DEPEND(coremctl, pci, 1, 1, 1);

static void
coremctl_identify(driver_t *driver, device_t parent)
{
	const struct coremctl_type *t;
	uint16_t did;

	/* Already identified */
	if (device_find_child(parent, "coremctl", -1) != NULL)
		return;

	if (pci_get_vendor(parent) != PCI_CORE_MEMCTL_VID)
		return;

	did = pci_get_device(parent);
	for (t = coremctl_types; t->desc != NULL; ++t) {
		if (t->did == did) {
			if (device_add_child(parent, "coremctl", -1) == NULL)
				device_printf(parent, "add coremctl failed\n");
			return;
		}
	}
}

static int
coremctl_probe(device_t dev)
{
	const struct coremctl_type *t;
	uint16_t did;

	if (pci_get_vendor(dev) != PCI_CORE_MEMCTL_VID)
		return ENXIO;

	did = pci_get_device(dev);
	for (t = coremctl_types; t->desc != NULL; ++t) {
		if (t->did == did) {
			struct coremctl_softc *sc = device_get_softc(dev);

			device_set_desc(dev, t->desc);
			sc->sc_ver = t->ver;
			return 0;
		}
	}
	return ENXIO;
}

static int
coremctl_attach(device_t dev)
{
	struct coremctl_softc *sc = device_get_softc(dev);
	uint32_t capa, dmfc, mch_barlo, mch_barhi;
	uint64_t mch_bar;
	int dmfc_parsed = 1;

	sc->sc_dev = dev;

	capa = pci_read_config(dev, PCI_CORE_CAPID0_A, 4);

	if (sc->sc_ver == COREMCTL_VER_1) {
		dmfc = __SHIFTOUT(capa, PCI_CORE_CAPID0_A_DMFC);
	} else { /* v2/v3 */
		uint32_t capb;

		capb = pci_read_config(dev, PCI_CORE_CAPID0_B, 4);
		dmfc = __SHIFTOUT(capb, PCI_CORE_CAPID0_B_DMFC);
	}

	if (dmfc == PCI_CORE_CAPID0_DMFC_1067) {
		device_printf(dev, "CAP DDR3 1067 ");
	} else if (dmfc == PCI_CORE_CAPID0_DMFC_1333) {
		device_printf(dev, "CAP DDR3 1333 ");
	} else {
		if (sc->sc_ver == COREMCTL_VER_1) {
			if (dmfc == PCI_CORE_CAPID0_DMFC_V1_ALL)
				device_printf(dev, "no CAP ");
			else
				dmfc_parsed = 0;
		} else { /* v2/v3 */
			if (dmfc == PCI_CORE_CAPID0_DMFC_1600)
				device_printf(dev, "CAP DDR3 1600 ");
			else if (dmfc == PCI_CORE_CAPID0_DMFC_1867)
				device_printf(dev, "CAP DDR3 1867 ");
			else if (dmfc == PCI_CORE_CAPID0_DMFC_2133)
				device_printf(dev, "CAP DDR3 2133 ");
			else if (dmfc == PCI_CORE_CAPID0_DMFC_2400)
				device_printf(dev, "CAP DDR3 2400 ");
			else if (dmfc == PCI_CORE_CAPID0_DMFC_2667)
				device_printf(dev, "CAP DDR3 2667 ");
			else if (dmfc == PCI_CORE_CAPID0_DMFC_2933)
				device_printf(dev, "CAP DDR3 2933 ");
			else
				dmfc_parsed = 0;
		}
	}
	if (!dmfc_parsed) {
		device_printf(dev, "unknown DMFC %#x\n", dmfc);
		return 0;
	}

	if (capa & PCI_CORE_CAPID0_A_ECCDIS) {
		kprintf("NON-ECC\n");
	} else {
		kprintf("ECC\n");
		sc->sc_ecc = device_add_child(dev, "ecc", -1);
		if (sc->sc_ecc == NULL)
			device_printf(dev, "add ecc failed\n");
	}

	mch_barlo = pci_read_config(dev, PCI_CORE_MCHBAR_LO, 4);
	mch_barhi = pci_read_config(dev, PCI_CORE_MCHBAR_HI, 4);

	mch_bar = (uint64_t)mch_barlo | (((uint64_t)mch_barhi) << 32);
	if (bootverbose)
		device_printf(dev, "MCHBAR 0x%jx\n", (uintmax_t)mch_bar);

	if (mch_bar & PCI_CORE_MCHBAR_LO_EN) {
		uint64_t map_addr = mch_bar & PCI_CORE_MCHBAR_ADDRMASK;

		sc->sc_mch = pmap_mapdev_uncacheable(map_addr, MCH_CORE_SIZE);

		if (bootverbose) {
			uint32_t dimm_ch0, dimm_ch1;

			dimm_ch0 = CSR_READ_4(sc, MCH_CORE_DIMM_CH0);
			dimm_ch1 = CSR_READ_4(sc, MCH_CORE_DIMM_CH1);

			coremctl_chaninfo(sc, dimm_ch0, "channel0");
			coremctl_chaninfo(sc, dimm_ch1, "channel1");
		}
	} else {
		device_printf(dev, "MCHBAR is not enabled\n");
	}

	bus_generic_attach(dev);

	return 0;
}

static void
coremctl_chaninfo(struct coremctl_softc *sc, uint32_t dimm_ch,
    const char *desc)
{
	int size_a, size_b;
	int dimma_id, dimmb_id;

	dimma_id = 0;
	dimmb_id = 1;
	if (dimm_ch & MCH_CORE_DIMM_A_SELECT) {
		dimma_id = 1;
		dimmb_id = 0;
	}

	size_a = __SHIFTOUT(dimm_ch, MCH_CORE_DIMM_A_SIZE);
	if (size_a != 0) {
		device_printf(sc->sc_dev, "%s, DIMM%d %dMB %dx%d\n", desc,
		    dimma_id, size_a * MCH_CORE_DIMM_SIZE_UNIT,
		    (dimm_ch & MCH_CORE_DIMM_A_DUAL_RANK) ? 2 : 1,
		    (dimm_ch & MCH_CORE_DIMM_A_X16) ? 16 : 8);
	}

	size_b = __SHIFTOUT(dimm_ch, MCH_CORE_DIMM_B_SIZE);
	if (size_b != 0) {
		device_printf(sc->sc_dev, "%s, DIMM%d %dMB %dx%d\n", desc,
		    dimmb_id, size_b * MCH_CORE_DIMM_SIZE_UNIT,
		    (dimm_ch & MCH_CORE_DIMM_B_DUAL_RANK) ? 2 : 1,
		    (dimm_ch & MCH_CORE_DIMM_B_X16) ? 16 : 8);
	}

	if (size_a == 0 && size_b == 0)
		return;

	if (sc->sc_ver == COREMCTL_VER_1 || sc->sc_ver == COREMCTL_VER_2) {
		/* This bit is v3 only */
		dimm_ch &= ~MCH_CORE_DIMM_HORI;
	}
	if (dimm_ch & (MCH_CORE_DIMM_ENHI | MCH_CORE_DIMM_RI |
	    MCH_CORE_DIMM_HORI)) {
		device_printf(sc->sc_dev, "%s", desc);
		if (dimm_ch & MCH_CORE_DIMM_RI)
			kprintf(", rank interleave");
		if (dimm_ch & MCH_CORE_DIMM_ENHI)
			kprintf(", enhanced interleave");
		if (dimm_ch & MCH_CORE_DIMM_HORI)
			kprintf(", high order rank interleave");
		kprintf("\n");
	}
}

static int
coremctl_detach(device_t dev)
{
	struct coremctl_softc *sc = device_get_softc(dev);

	if (sc->sc_ecc != NULL)
		device_delete_child(dev, sc->sc_ecc);
	bus_generic_detach(dev);

	if (sc->sc_mch != NULL)
		pmap_unmapdev((vm_offset_t)sc->sc_mch, MCH_CORE_SIZE);
	return 0;
}

static int
coremctl_mch_readreg(device_t dev, int reg, uint32_t *val)
{
	struct coremctl_softc *sc = device_get_softc(dev);

	if (sc->sc_mch == NULL)
		return EOPNOTSUPP;

	*val = CSR_READ_4(sc, reg);
	return 0;
}

static int
coremctl_pci_read_ivar(device_t dev, device_t child, int which,
    uintptr_t *result)
{
	return BUS_READ_IVAR(device_get_parent(dev), dev, which, result);
}

static uint32_t
coremctl_pci_read_config(device_t dev, device_t child, int reg, int width)
{
	return pci_read_config(dev, reg, width);
}

static void
coremctl_pci_write_config(device_t dev, device_t child, int reg, uint32_t val,
    int width)
{
	pci_write_config(dev, reg, val, width);
}
