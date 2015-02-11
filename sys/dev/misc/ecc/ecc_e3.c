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

#include <vm/pmap.h>

#include "coremctl_if.h"
#include "pcib_if.h"

#include <dev/misc/coremctl/coremctl_reg.h>

#define ECC_E3_VER_1	1	/* Sandy Bridge */
#define ECC_E3_VER_2	2	/* Ivy Bridge */
#define ECC_E3_VER_3	3	/* Haswell */

struct ecc_e3_type {
	uint16_t	did;
	const char	*desc;
	int		ver;		/* ECC_E3_VER_ */
};

struct ecc_e3_softc {
	device_t	ecc_dev;
	device_t	ecc_parent;	/* non-NULL if parent has MCHBAR */
	struct callout	ecc_callout;
	int		ecc_ver;	/* ECC_E3_VER_ */
};

#define ecc_printf(sc, fmt, arg...) \
	device_printf((sc)->ecc_dev, fmt , ##arg)

static int	ecc_e3_probe(device_t);
static int	ecc_e3_attach(device_t);
static int	ecc_e3_detach(device_t);
static void	ecc_e3_shutdown(device_t);

static void	ecc_e3_chaninfo(struct ecc_e3_softc *, uint32_t, const char *);
static void	ecc_e3_status(struct ecc_e3_softc *);
static void	ecc_e3_callout(void *);
static void	ecc_e3_errlog(struct ecc_e3_softc *);
static void	ecc_e3_errlog_ch(struct ecc_e3_softc *, int, int, const char *);

static const struct ecc_e3_type ecc_e3_types[] = {
	{ PCI_E3V1_MEMCTL_DID, "Intel E3 ECC", ECC_E3_VER_1 },
	{ PCI_E3V2_MEMCTL_DID, "Intel E3 v2 ECC", ECC_E3_VER_2 },
	{ PCI_E3V3_MEMCTL_DID, "Intel E3 v3 ECC", ECC_E3_VER_3 },
	{ 0, NULL, 0 } /* required last entry */
};

static device_method_t ecc_e3_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		ecc_e3_probe),
	DEVMETHOD(device_attach,	ecc_e3_attach),
	DEVMETHOD(device_detach,	ecc_e3_detach),
	DEVMETHOD(device_shutdown,	ecc_e3_shutdown),
	DEVMETHOD(device_suspend,	bus_generic_suspend),
	DEVMETHOD(device_resume,	bus_generic_resume),
	DEVMETHOD_END
};

static driver_t ecc_e3_driver = {
	"ecc",
	ecc_e3_methods,
	sizeof(struct ecc_e3_softc)
};
static devclass_t ecc_devclass;
DRIVER_MODULE(ecc_e3, coremctl, ecc_e3_driver, ecc_devclass, NULL, NULL);
MODULE_DEPEND(ecc_e3, pci, 1, 1, 1);
MODULE_DEPEND(ecc_e3, coremctl, 1, 1, 1);

static __inline uint32_t
CSR_READ_4(struct ecc_e3_softc *sc, int ofs)
{
	uint32_t val;
	int error;

	error = COREMCTL_MCH_READ(sc->ecc_parent, ofs, &val);
	KASSERT(!error, ("mch read failed"));

	return val;
}

static int
ecc_e3_probe(device_t dev)
{
	const struct ecc_e3_type *t;
	uint16_t did;

	if (pci_get_vendor(dev) != PCI_CORE_MEMCTL_VID)
		return ENXIO;

	did = pci_get_device(dev);
	for (t = ecc_e3_types; t->desc != NULL; ++t) {
		if (t->did == did) {
			struct ecc_e3_softc *sc = device_get_softc(dev);

			device_set_desc(dev, t->desc);
			sc->ecc_ver = t->ver;
			return 0;
		}
	}
	return ENXIO;
}

static int
ecc_e3_attach(device_t dev)
{
	struct ecc_e3_softc *sc = device_get_softc(dev);
	uint32_t val;
	int error;

	callout_init_mp(&sc->ecc_callout);
	sc->ecc_dev = dev;

	/* Probe the existance of MCHBAR */
	error = COREMCTL_MCH_READ(device_get_parent(dev), MCH_CORE_DIMM_CH0,
	    &val);
	if (!error)
		sc->ecc_parent = device_get_parent(dev);

	if (sc->ecc_parent != NULL) {
		uint32_t dimm_ch0, dimm_ch1;
		int ecc_active;

		if (bootverbose) {
			ecc_printf(sc, "LOG0_C0 %#x\n",
			    CSR_READ_4(sc, MCH_E3_ERRLOG0_C0));
			ecc_printf(sc, "LOG0_C1 %#x\n",
			    CSR_READ_4(sc, MCH_E3_ERRLOG0_C1));
		}

		dimm_ch0 = CSR_READ_4(sc, MCH_CORE_DIMM_CH0);
		dimm_ch1 = CSR_READ_4(sc, MCH_CORE_DIMM_CH1);

		if (bootverbose) {
			ecc_e3_chaninfo(sc, dimm_ch0, "channel0");
			ecc_e3_chaninfo(sc, dimm_ch1, "channel1");
		}

		ecc_active = 1;
		if (sc->ecc_ver == ECC_E3_VER_1 ||
		    sc->ecc_ver == ECC_E3_VER_2) {
			if (((dimm_ch0 | dimm_ch1) & MCH_E3_DIMM_ECC) ==
			    MCH_E3_DIMM_ECC_NONE) {
				ecc_active = 0;
				ecc_printf(sc, "No ECC active\n");
			}
		} else { /* v3 */
			uint32_t ecc_mode0, ecc_mode1;

			ecc_mode0 = __SHIFTOUT(dimm_ch0, MCH_E3_DIMM_ECC);
			ecc_mode1 = __SHIFTOUT(dimm_ch1, MCH_E3_DIMM_ECC);

			/*
			 * Only active ALL/NONE is supported
			 */

			if (ecc_mode0 != MCH_E3_DIMM_ECC_NONE &&
			    ecc_mode0 != MCH_E3_DIMM_ECC_ALL) {
				ecc_active = 0;
				ecc_printf(sc, "channel0, invalid ECC "
				    "active 0x%x\n", ecc_mode0);
			}
			if (ecc_mode1 != MCH_E3_DIMM_ECC_NONE &&
			    ecc_mode1 != MCH_E3_DIMM_ECC_ALL) {
				ecc_active = 0;
				ecc_printf(sc, "channel1, invalid ECC "
				    "active 0x%x\n", ecc_mode1);
			}

			if (ecc_mode0 == MCH_E3_DIMM_ECC_NONE &&
			    ecc_mode1 == MCH_E3_DIMM_ECC_NONE) {
				ecc_active = 0;
				ecc_printf(sc, "No ECC active\n");
			}
		}

		if (!ecc_active)
			return 0;
	} else {
		ecc_printf(sc, "MCHBAR is not enabled\n");
	}

	ecc_e3_status(sc);
	callout_reset(&sc->ecc_callout, hz, ecc_e3_callout, sc);

	return 0;
}

static void
ecc_e3_callout(void *xsc)
{
	struct ecc_e3_softc *sc = xsc;

	ecc_e3_status(sc);
	callout_reset(&sc->ecc_callout, hz, ecc_e3_callout, sc);
}

static void
ecc_e3_status(struct ecc_e3_softc *sc)
{
	device_t dev = sc->ecc_dev;
	uint16_t errsts;

	errsts = pci_read_config(dev, PCI_E3_ERRSTS, 2);
	if (errsts & PCI_E3_ERRSTS_DMERR)
		ecc_printf(sc, "Uncorrectable multilple-bit ECC error\n");
	else if (errsts & PCI_E3_ERRSTS_DSERR)
		ecc_printf(sc, "Correctable single-bit ECC error\n");

	if (errsts & (PCI_E3_ERRSTS_DSERR | PCI_E3_ERRSTS_DMERR)) {
		if (sc->ecc_parent != NULL)
			ecc_e3_errlog(sc);

		/* Clear pending errors */
		pci_write_config(dev, PCI_E3_ERRSTS, errsts, 2);
	}
}

static void
ecc_e3_chaninfo(struct ecc_e3_softc *sc, uint32_t dimm_ch, const char *desc)
{
	int size_a, size_b, ecc;

	size_a = __SHIFTOUT(dimm_ch, MCH_CORE_DIMM_A_SIZE);
	size_b = __SHIFTOUT(dimm_ch, MCH_CORE_DIMM_B_SIZE);
	if (size_a == 0 && size_b == 0)
		return;

	ecc = __SHIFTOUT(dimm_ch, MCH_E3_DIMM_ECC);
	if (ecc == MCH_E3_DIMM_ECC_NONE) {
		ecc_printf(sc, "%s, no ECC active\n", desc);
	} else if (ecc == MCH_E3_DIMM_ECC_ALL) {
		ecc_printf(sc, "%s, ECC active IO/logic\n", desc);
	} else {
		if (sc->ecc_ver == ECC_E3_VER_1 ||
		    sc->ecc_ver == ECC_E3_VER_2) {
			if (ecc == MCH_E3_DIMM_ECC_IO)
				ecc_printf(sc, "%s, ECC active IO\n", desc);
			else
				ecc_printf(sc, "%s, ECC active logic\n", desc);
		} else { /* v3 */
			ecc_printf(sc, "%s, invalid ECC active 0x%x\n",
			    desc, ecc);
		}
	}
}

static void
ecc_e3_errlog(struct ecc_e3_softc *sc)
{
	ecc_e3_errlog_ch(sc, MCH_E3_ERRLOG0_C0, MCH_E3_ERRLOG1_C0,
	    "channel0");
	ecc_e3_errlog_ch(sc, MCH_E3_ERRLOG0_C1, MCH_E3_ERRLOG1_C1,
	    "channel1");
}

static void
ecc_e3_errlog_ch(struct ecc_e3_softc *sc, int err0_ofs, int err1_ofs,
    const char *desc)
{
	uint32_t err0, err1;

	err0 = CSR_READ_4(sc, err0_ofs);
	if ((err0 & (MCH_E3_ERRLOG0_CERRSTS | MCH_E3_ERRLOG0_MERRSTS)) == 0)
		return;

	err1 = CSR_READ_4(sc, err1_ofs);

	ecc_printf(sc, "%s error @bank %d, rank %d, chunk %d, syndrome %d, "
	    "row %d, col %d\n", desc,
	    __SHIFTOUT(err0, MCH_E3_ERRLOG0_ERRBANK),
	    __SHIFTOUT(err0, MCH_E3_ERRLOG0_ERRRANK),
	    __SHIFTOUT(err0, MCH_E3_ERRLOG0_ERRCHUNK),
	    __SHIFTOUT(err0, MCH_E3_ERRLOG0_ERRSYND),
	    __SHIFTOUT(err1, MCH_E3_ERRLOG1_ERRROW),
	    __SHIFTOUT(err1, MCH_E3_ERRLOG1_ERRCOL));
}

static int
ecc_e3_detach(device_t dev)
{
	struct ecc_e3_softc *sc = device_get_softc(dev);

	callout_stop_sync(&sc->ecc_callout);
	return 0;
}

static void
ecc_e3_shutdown(device_t dev)
{
	struct ecc_e3_softc *sc = device_get_softc(dev);

	callout_stop_sync(&sc->ecc_callout);
}
