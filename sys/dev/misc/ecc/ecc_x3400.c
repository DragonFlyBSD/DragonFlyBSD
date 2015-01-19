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

#include <dev/misc/ecc/ecc_x3400_reg.h>

#define MC_READ_2(ofs) \
	pci_cfgregread(PCIBUS_X3400UC, PCISLOT_X3400UC_MC, \
	    PCIFUNC_X3400UC_MC, (ofs), 2)
#define MC_READ_4(ofs) \
	pci_cfgregread(PCIBUS_X3400UC, PCISLOT_X3400UC_MC, \
	    PCIFUNC_X3400UC_MC, (ofs), 4)

#define MCT2_READ_2(ofs) \
	pci_cfgregread(PCIBUS_X3400UC, PCISLOT_X3400UC_MCT2, \
	    PCIFUNC_X3400UC_MCT2, (ofs), 2)
#define MCT2_READ_4(ofs) \
	pci_cfgregread(PCIBUS_X3400UC, PCISLOT_X3400UC_MCT2, \
	    PCIFUNC_X3400UC_MCT2, (ofs), 4)
#define MCT2_WRITE_4(ofs, data) \
	pci_cfgregwrite(PCIBUS_X3400UC, PCISLOT_X3400UC_MCT2, \
	    PCIFUNC_X3400UC_MCT2, (ofs), data, 4)

struct ecc_x3400_memctrl {
	uint16_t	vid;
	uint16_t	did;
	const char	*desc;
};

struct ecc_x3400_softc {
	device_t	ecc_mydev;
	struct callout	ecc_callout;
	int		ecc_dimms;
};

#define ecc_printf(sc, fmt, arg...) \
	device_printf((sc)->ecc_mydev, fmt , ##arg)

static int	ecc_x3400_probe(device_t);
static int	ecc_x3400_attach(device_t);
static int	ecc_x3400_detach(device_t);
static void	ecc_x3400_shutdown(device_t);

static void	ecc_x3400_status(struct ecc_x3400_softc *);
static void	ecc_x3400_status_ch(struct ecc_x3400_softc *, int, int);
static void	ecc_x3400_callout(void *);
static void	ecc_x3400_stop(device_t);

static const struct ecc_x3400_memctrl ecc_memctrls[] = {
	{ 0x8086, 0xd130, "Intel X3400 memory controller" },
	{ 0, 0, NULL } /* required last entry */
};

static device_method_t ecc_x3400_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		ecc_x3400_probe),
	DEVMETHOD(device_attach,	ecc_x3400_attach),
	DEVMETHOD(device_detach,	ecc_x3400_detach),
	DEVMETHOD(device_shutdown,	ecc_x3400_shutdown),
	DEVMETHOD(device_suspend,	bus_generic_suspend),
	DEVMETHOD(device_resume,	bus_generic_resume),
	DEVMETHOD_END
};

static driver_t ecc_x3400_driver = {
	"ecc",
	ecc_x3400_methods,
	sizeof(struct ecc_x3400_softc)
};
static devclass_t ecc_devclass;
DRIVER_MODULE(ecc_x3400, hostb, ecc_x3400_driver, ecc_devclass, NULL, NULL);
MODULE_DEPEND(ecc_x3400, pci, 1, 1, 1);

static int
ecc_x3400_probe(device_t dev)
{
	const struct ecc_x3400_memctrl *mc;
	uint16_t vid, did;

	vid = pci_get_vendor(dev);
	did = pci_get_device(dev);

	for (mc = ecc_memctrls; mc->desc != NULL; ++mc) {
		if (mc->vid == vid && mc->did == did) {
			struct ecc_x3400_softc *sc = device_get_softc(dev);

			if (MC_READ_2(PCIR_VENDOR) != PCI_X3400UC_MC_VID_ID ||
			    MC_READ_2(PCIR_DEVICE) != PCI_X3400UC_MC_DID_ID)
				return ENXIO;
			if (MCT2_READ_2(PCIR_VENDOR) !=
			    PCI_X3400UC_MCT2_VID_ID ||
			    MCT2_READ_2(PCIR_DEVICE) !=
			    PCI_X3400UC_MCT2_DID_ID)
				return ENXIO;

			device_set_desc(dev, mc->desc);
			sc->ecc_mydev = dev;
			return 0;
		}
	}
	return ENXIO;
}

static int
ecc_x3400_attach(device_t dev)
{
	struct ecc_x3400_softc *sc = device_get_softc(dev);
	uint32_t val, dimms;

	callout_init_mp(&sc->ecc_callout);

	val = MC_READ_4(PCI_X3400UC_MC_CTRL);
	if ((val & PCI_X3400UC_MC_CTRL_ECCEN) == 0) {
		device_printf(dev, "ECC checking is not enabled\n");
		return 0;
	}

	val = MC_READ_4(PCI_X3400UC_MC_STS);
	if ((val & PCI_X3400UC_MC_STS_ECCEN) == 0) {
		device_printf(dev, "ECC is not enabled\n");
		return 0;
	}

	val = MC_READ_4(PCI_X3400UC_MC_MAX_DOD);
	dimms = __SHIFTOUT(val, PCI_X3400UC_MC_MAX_DOD_DIMMS);
	sc->ecc_dimms = dimms + 1;
	device_printf(dev, "max dimms %d\n", sc->ecc_dimms);

	callout_reset(&sc->ecc_callout, hz, ecc_x3400_callout, sc);

	return 0;
}

static void
ecc_x3400_callout(void *xsc)
{
	struct ecc_x3400_softc *sc = xsc;

	ecc_x3400_status(sc);
	callout_reset(&sc->ecc_callout, hz, ecc_x3400_callout, sc);
}

static void
ecc_x3400_status(struct ecc_x3400_softc *sc)
{
	ecc_x3400_status_ch(sc, PCI_X3400UC_MCT2_COR_ECC_CNT_0, 0);
	ecc_x3400_status_ch(sc, PCI_X3400UC_MCT2_COR_ECC_CNT_1, 1);
	ecc_x3400_status_ch(sc, PCI_X3400UC_MCT2_COR_ECC_CNT_2, 2);
	ecc_x3400_status_ch(sc, PCI_X3400UC_MCT2_COR_ECC_CNT_3, 3);
}

static void
ecc_x3400_status_ch(struct ecc_x3400_softc *sc, int ofs, int idx)
{
	uint32_t cor, err0, err1;
	const char *desc0 = NULL, *desc1 = NULL;

	cor = MCT2_READ_4(ofs);
	if (cor == 0)
		return;

	if (sc->ecc_dimms > 2) {
		switch (idx) {
		case 0:
			desc0 = "channel0, DIMM0";
			desc1 = "channel0, DIMM1";
			break;

		case 1:
			desc0 = "channel0, DIMM2";
			break;

		case 2:
			desc0 = "channel1, DIMM0";
			desc1 = "channel1, DIMM1";
			break;

		case 3:
			desc0 = "channel1, DIMM2";
			break;

		default:
			panic("unsupported index %d", idx);
		}
	} else {
		switch (idx) {
		case 0:
			desc0 = "channel0, DIMM0 RANK 0/1";
			desc1 = "channel0, DIMM0 RANK 2/3";
			break;

		case 1:
			desc0 = "channel0, DIMM1 RANK 0/1";
			desc1 = "channel0, DIMM1 RANK 2/3";
			break;

		case 2:
			desc0 = "channel1, DIMM0 RANK 0/1";
			desc1 = "channel1, DIMM0 RANK 2/3";
			break;

		case 3:
			desc0 = "channel1, DIMM1 RANK 0/1";
			desc1 = "channel1, DIMM1 RANK 2/3";
			break;

		default:
			panic("unsupported index %d", idx);
		}
	}

	err0 = __SHIFTOUT(cor, PCI_X3400UC_MCT2_COR_DIMM0);
	if (cor & PCI_X3400UC_MCT2_COR_DIMM0_OV)
		ecc_printf(sc, "%s has too many errors\n", desc0);
	else if (err0)
		ecc_printf(sc, "%s has %d errors", desc0, err0);

	if (desc1 != NULL) {
		err1 = __SHIFTOUT(cor, PCI_X3400UC_MCT2_COR_DIMM1);
		if (cor & PCI_X3400UC_MCT2_COR_DIMM1_OV)
			ecc_printf(sc, "%s has too many errors\n", desc1);
		else if (err1)
			ecc_printf(sc, "%s has %d errors\n", desc1, err1);
	}

	MCT2_WRITE_4(ofs, 0);
}

static void
ecc_x3400_stop(device_t dev)
{
	struct ecc_x3400_softc *sc = device_get_softc(dev);

	callout_stop_sync(&sc->ecc_callout);
}

static int
ecc_x3400_detach(device_t dev)
{
	ecc_x3400_stop(dev);
	return 0;
}

static void
ecc_x3400_shutdown(device_t dev)
{
	ecc_x3400_stop(dev);
}
