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
#include <bus/pci/pcib_private.h>

#include "pcib_if.h"

#include <dev/misc/ecc/ecc_e5_reg.h>

#define UBOX_READ(dev, ofs, w)				\
	pcib_read_config((dev), pci_get_bus((dev)),	\
	    PCISLOT_E5_UBOX0, PCIFUNC_E5_UBOX0, (ofs), w)
#define UBOX_READ_2(dev, ofs)		UBOX_READ((dev), (ofs), 2)
#define UBOX_READ_4(dev, ofs)		UBOX_READ((dev), (ofs), 4)

#define IMC_CPGC_READ(dev, ofs, w)			\
	pcib_read_config((dev), pci_get_bus((dev)),	\
	    PCISLOT_E5_IMC_CPGC, PCIFUNC_E5_IMC_CPGC, (ofs), w)
#define IMC_CPGC_READ_2(dev, ofs)	IMC_CPGC_READ((dev), (ofs), 2)
#define IMC_CPGC_READ_4(dev, ofs)	IMC_CPGC_READ((dev), (ofs), 4)

#define IMC_CTAD_READ(dev, c, ofs, w)			\
	pcib_read_config((dev), pci_get_bus((dev)),	\
	    PCISLOT_E5_IMC_CTAD, PCIFUNC_E5_IMC_CTAD((c)), (ofs), w)
#define IMC_CTAD_READ_2(dev, c, ofs)	IMC_CTAD_READ((dev), (c), (ofs), 2)
#define IMC_CTAD_READ_4(dev, c, ofs)	IMC_CTAD_READ((dev), (c), (ofs), 4)

struct ecc_e5_type {
	uint16_t	did;
	int		slot;
	int		func;
	int		chan;
	const char	*desc;
};

struct ecc_e5_rank {
	int		rank_dimm;	/* owner dimm */
	int		rank_dimm_rank;	/* rank within the owner dimm */
};

struct ecc_e5_softc {
	device_t		ecc_dev;
	int			ecc_chan;
	int			ecc_node;
	int			ecc_rank_cnt;
	struct ecc_e5_rank	ecc_rank[PCI_E5_IMC_ERROR_RANK_MAX];
	struct callout		ecc_callout;
};

#define ecc_printf(sc, fmt, arg...) \
	device_printf((sc)->ecc_dev, fmt , ##arg)

static int	ecc_e5_probe(device_t);
static int	ecc_e5_attach(device_t);
static int	ecc_e5_detach(device_t);
static void	ecc_e5_shutdown(device_t);

static void	ecc_e5_callout(void *);

#define ECC_E5_TYPE_V2(c) \
{ \
	.did	= PCI_E5_IMC_ERROR_CHN##c##_DID_ID, \
	.slot	= PCISLOT_E5_IMC_ERROR, \
	.func	= PCIFUNC_E5_IMC_ERROR_CHN##c, \
	.chan	= c, \
	.desc	= "Intel E5 v2 ECC" \
}

#define ECC_E5_TYPE_END		{ 0, 0, 0, 0, NULL }

static const struct ecc_e5_type ecc_types[] = {
	ECC_E5_TYPE_V2(0),
	ECC_E5_TYPE_V2(1),
	ECC_E5_TYPE_V2(2),
	ECC_E5_TYPE_V2(3),

	ECC_E5_TYPE_END
};

#undef ECC_E5_TYPE_V2
#undef ECC_E5_TYPE_END

static device_method_t ecc_e5_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		ecc_e5_probe),
	DEVMETHOD(device_attach,	ecc_e5_attach),
	DEVMETHOD(device_detach,	ecc_e5_detach),
	DEVMETHOD(device_shutdown,	ecc_e5_shutdown),
	DEVMETHOD(device_suspend,	bus_generic_suspend),
	DEVMETHOD(device_resume,	bus_generic_resume),
	DEVMETHOD_END
};

static driver_t ecc_e5_driver = {
	"ecc",
	ecc_e5_methods,
	sizeof(struct ecc_e5_softc)
};
static devclass_t ecc_devclass;
DRIVER_MODULE(ecc_e5, pci, ecc_e5_driver, ecc_devclass, NULL, NULL);
MODULE_DEPEND(ecc_e5, pci, 1, 1, 1);

static int
ecc_e5_probe(device_t dev)
{
	const struct ecc_e5_type *t;
	uint16_t vid, did;
	int slot, func;

	vid = pci_get_vendor(dev);
	if (vid != PCI_E5_VID_ID)
		return ENXIO;

	did = pci_get_device(dev);
	slot = pci_get_slot(dev);
	func = pci_get_function(dev);

	for (t = ecc_types; t->desc != NULL; ++t) {
		if (t->did == did && t->slot == slot && t->func == func) {
			struct ecc_e5_softc *sc = device_get_softc(dev);
			char desc[32];
			uint32_t val;
			int node, dimm;

			/* Check CPGC vid/did */
			if (IMC_CPGC_READ_2(dev, PCIR_VENDOR) !=
			    PCI_E5_VID_ID ||
			    IMC_CPGC_READ_2(dev, PCIR_DEVICE) !=
			    PCI_E5_IMC_CPGC_DID_ID)
				break;

			/* Is this channel disabled */
			val = IMC_CPGC_READ_4(dev, PCI_E5_IMC_CPGC_MCMTR);
			if (val & PCI_E5_IMC_CPGC_MCMTR_CHN_DISABLE(t->chan))
				break;

			/* Check CTAD vid/did */
			if (IMC_CTAD_READ_2(dev, t->chan, PCIR_VENDOR) !=
			    PCI_E5_VID_ID ||
			    IMC_CTAD_READ_2(dev, t->chan, PCIR_DEVICE) !=
			    PCI_E5_IMC_CTAD_DID_ID(t->chan))
				break;

			/* Are there any DIMMs populated? */
			for (dimm = 0; dimm < PCI_E5_IMC_DIMM_MAX; ++dimm) {
				val = IMC_CTAD_READ_4(dev, t->chan,
				    PCI_E5_IMC_CTAD_DIMMMTR(dimm));
				if (val & PCI_E5_IMC_CTAD_DIMMMTR_DIMM_POP)
					break;
			}
			if (dimm == PCI_E5_IMC_DIMM_MAX)
				break;

			/* Check UBOX vid/did */
			if (UBOX_READ_2(dev, PCIR_VENDOR) != PCI_E5_VID_ID ||
			    UBOX_READ_2(dev, PCIR_DEVICE) !=
			    PCI_E5_UBOX0_DID_ID)
				break;

			val = UBOX_READ_4(dev, PCI_E5_UBOX0_CPUNODEID);
			node = __SHIFTOUT(val,
			    PCI_E5_UBOX0_CPUNODEID_LCLNODEID);

			ksnprintf(desc, sizeof(desc), "%s node%d channel%d",
			    t->desc, node, t->chan);
			device_set_desc_copy(dev, desc);

			sc->ecc_chan = t->chan;
			sc->ecc_node = node;
			return 0;
		}
	}
	return ENXIO;
}

static int
ecc_e5_attach(device_t dev)
{
	struct ecc_e5_softc *sc = device_get_softc(dev);
	uint32_t mcmtr;
	int dimm, rank;

	callout_init_mp(&sc->ecc_callout);
	sc->ecc_dev = dev;

	mcmtr = IMC_CPGC_READ_4(sc->ecc_dev, PCI_E5_IMC_CPGC_MCMTR);
	if (bootverbose) {
		if (__SHIFTOUT(mcmtr, PCI_E5_IMC_CPGC_MCMTR_IMC_MODE) ==
		    PCI_E5_IMC_CPGC_MCMTR_IMC_MODE_DDR3)
			ecc_printf(sc, "native DDR3\n");
	}

	rank = 0;
	for (dimm = 0; dimm < PCI_E5_IMC_DIMM_MAX; ++dimm) {
		const char *width;
		uint32_t dimmmtr;
		int rank_cnt, r;
		int density;
		int val;

		dimmmtr = IMC_CTAD_READ_4(sc->ecc_dev, sc->ecc_chan,
		    PCI_E5_IMC_CTAD_DIMMMTR(dimm));

		if ((dimmmtr & PCI_E5_IMC_CTAD_DIMMMTR_DIMM_POP) == 0)
			continue;

		val = __SHIFTOUT(dimmmtr, PCI_E5_IMC_CTAD_DIMMMTR_RANK_CNT);
		switch (val) {
		case PCI_E5_IMC_CTAD_DIMMMTR_RANK_CNT_SR:
			rank_cnt = 1;
			break;
		case PCI_E5_IMC_CTAD_DIMMMTR_RANK_CNT_DR:
			rank_cnt = 2;
			break;
		case PCI_E5_IMC_CTAD_DIMMMTR_RANK_CNT_QR:
			rank_cnt = 4;
			break;
		default:
			ecc_printf(sc, "unknown rank count 0x%x\n", val);
			return ENXIO;
		}

		val = __SHIFTOUT(dimmmtr, PCI_E5_IMC_CTAD_DIMMMTR_DDR3_WIDTH);
		switch (val) {
		case PCI_E5_IMC_CTAD_DIMMMTR_DDR3_WIDTH_4:
			width = "x4";
			break;
		case PCI_E5_IMC_CTAD_DIMMMTR_DDR3_WIDTH_8:
			width = "x8";
			break;
		case PCI_E5_IMC_CTAD_DIMMMTR_DDR3_WIDTH_16:
			width = "x16";
			break;
		default:
			ecc_printf(sc, "unknown ddr3 width 0x%x\n", val);
			return ENXIO;
		}

		val = __SHIFTOUT(dimmmtr, PCI_E5_IMC_CTAD_DIMMMTR_DDR3_DNSTY);
		switch (val) {
		case PCI_E5_IMC_CTAD_DIMMMTR_DDR3_DNSTY_1G:
			density = 1;
			break;
		case PCI_E5_IMC_CTAD_DIMMMTR_DDR3_DNSTY_2G:
			density = 2;
			break;
		case PCI_E5_IMC_CTAD_DIMMMTR_DDR3_DNSTY_4G:
			density = 4;
			break;
		case PCI_E5_IMC_CTAD_DIMMMTR_DDR3_DNSTY_8G:
			density = 8;
			break;
		default:
			ecc_printf(sc, "unknown ddr3 density 0x%x\n", val);
			return ENXIO;
		}

		if (bootverbose) {
			ecc_printf(sc, "DIMM%d %dGB, %d%s, density %dGB\n",
			    dimm, density * rank_cnt * 2,
			    rank_cnt, width, density);
		}

		for (r = 0; r < rank_cnt; ++r) {
			struct ecc_e5_rank *rk;

			if (rank >= PCI_E5_IMC_ERROR_RANK_MAX) {
				ecc_printf(sc, "too many ranks\n");
				return ENXIO;
			}
			rk = &sc->ecc_rank[rank];

			rk->rank_dimm = dimm;
			rk->rank_dimm_rank = r;

			++rank;
		}
	}
	sc->ecc_rank_cnt = rank;

	if ((mcmtr & PCI_E5_IMC_CPGC_MCMTR_ECC_EN) == 0) {
		ecc_printf(sc, "ECC is not enabled\n");
		return 0;
	}

	if (bootverbose) {
		for (rank = 0; rank < sc->ecc_rank_cnt; ++rank) {
			const struct ecc_e5_rank *rk = &sc->ecc_rank[rank];
			uint32_t thr, mask;
			int ofs;

			ofs = PCI_E5_IMC_ERROR_COR_ERR_TH(rank / 2);
			if (rank & 1)
				mask = PCI_E5_IMC_ERROR_COR_ERR_TH_HI;
			else
				mask = PCI_E5_IMC_ERROR_COR_ERR_TH_HI;

			thr = pci_read_config(sc->ecc_dev, ofs, 4);
			ecc_printf(sc, "DIMM%d rank%d, "
			    "corrected error threshold %d\n",
			    rk->rank_dimm, rk->rank_dimm_rank,
			    __SHIFTOUT(thr, mask));
		}
	}

	callout_reset(&sc->ecc_callout, hz, ecc_e5_callout, sc);
	return 0;
}

static void
ecc_e5_callout(void *xsc)
{
	struct ecc_e5_softc *sc = xsc;
	uint32_t err_ranks, val;

	val = pci_read_config(sc->ecc_dev, PCI_E5_IMC_ERROR_COR_ERR_STAT, 4);

	err_ranks = (val & PCI_E5_IMC_ERROR_COR_ERR_STAT_RANKS);
	while (err_ranks != 0) {
		int rank;

		rank = ffs(err_ranks) - 1;
		err_ranks &= ~(1 << rank);

		if (rank < sc->ecc_rank_cnt) {
			const struct ecc_e5_rank *rk = &sc->ecc_rank[rank];
			uint32_t err, mask;
			int ofs;

			ofs = PCI_E5_IMC_ERROR_COR_ERR_CNT(rank / 2);
			if (rank & 1)
				mask = PCI_E5_IMC_ERROR_COR_ERR_CNT_HI;
			else
				mask = PCI_E5_IMC_ERROR_COR_ERR_CNT_LO;

			err = pci_read_config(sc->ecc_dev, ofs, 4);
			ecc_printf(sc, "node%d channel%d DIMM%d rank%d, "
			    "too many errors %d",
			    sc->ecc_node, sc->ecc_chan,
			    rk->rank_dimm, rk->rank_dimm_rank,
			    __SHIFTOUT(err, mask));
		}
	}

	if (val & PCI_E5_IMC_ERROR_COR_ERR_STAT_RANKS) {
		pci_write_config(sc->ecc_dev, PCI_E5_IMC_ERROR_COR_ERR_STAT,
		    val, 4);
	}
	callout_reset(&sc->ecc_callout, hz, ecc_e5_callout, sc);
}

static void
ecc_e5_stop(device_t dev)
{
	struct ecc_e5_softc *sc = device_get_softc(dev);

	callout_stop_sync(&sc->ecc_callout);
}

static int
ecc_e5_detach(device_t dev)
{
	ecc_e5_stop(dev);
	return 0;
}

static void
ecc_e5_shutdown(device_t dev)
{
	ecc_e5_stop(dev);
}
