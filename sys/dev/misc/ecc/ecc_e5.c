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
#include <sys/cpu_topology.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/queue.h>
#include <sys/sensors.h>

#include <bus/pci/pcivar.h>
#include <bus/pci/pcireg.h>
#include <bus/pci/pcibus.h>
#include <bus/pci/pci_cfgreg.h>
#include <bus/pci/pcib_private.h>

#include "pcib_if.h"

#include <dev/misc/dimm/dimm.h>
#include <dev/misc/ecc/e5_imc_reg.h>
#include <dev/misc/ecc/e5_imc_var.h>

struct ecc_e5_dimm {
	TAILQ_ENTRY(ecc_e5_dimm) dimm_link;
	struct dimm_softc	*dimm_softc;
	struct ksensor		dimm_sensor;
};

struct ecc_e5_rank {
	struct ecc_e5_dimm *rank_dimm_sc;
};

struct ecc_e5_softc {
	device_t		ecc_dev;
	const struct e5_imc_chan *ecc_chan;
	int			ecc_node;
	int			ecc_rank_cnt;
	struct ecc_e5_rank	ecc_rank[PCI_E5_IMC_ERROR_RANK_MAX];
	struct sensor_task	*ecc_senstask;
	TAILQ_HEAD(, ecc_e5_dimm) ecc_dimm;
};

#define ecc_printf(sc, fmt, arg...) \
	device_printf((sc)->ecc_dev, fmt , ##arg)

static int	ecc_e5_probe(device_t);
static int	ecc_e5_attach(device_t);
static int	ecc_e5_detach(device_t);
static void	ecc_e5_shutdown(device_t);

static void	ecc_e5_sensor_task(void *);

#define ECC_E5_CHAN(v, imc, c, c_ext)				\
{								\
	.did		= PCI_E5V##v##_IMC##imc##_ERROR_CHN##c##_DID_ID, \
	.slot		= PCISLOT_E5V##v##_IMC##imc##_ERROR_CHN##c, \
	.func		= PCIFUNC_E5V##v##_IMC##imc##_ERROR_CHN##c, \
	.desc		= "Intel E5 v" #v " ECC",		\
								\
	E5_IMC_CHAN_FIELDS(v, imc, c, c_ext)			\
}

#define ECC_E5_CHAN_V2(c)		ECC_E5_CHAN(2, 0, c, c)
#define ECC_E5_CHAN_IMC0_V3(c)		ECC_E5_CHAN(3, 0, c, c)
#define ECC_E5_CHAN_IMC1_V3(c, c_ext)	ECC_E5_CHAN(3, 1, c, c_ext)
#define ECC_E5_CHAN_END			E5_IMC_CHAN_END

static const struct e5_imc_chan ecc_e5_chans[] = {
	ECC_E5_CHAN_V2(0),
	ECC_E5_CHAN_V2(1),
	ECC_E5_CHAN_V2(2),
	ECC_E5_CHAN_V2(3),

	ECC_E5_CHAN_IMC0_V3(0),
	ECC_E5_CHAN_IMC0_V3(1),
	ECC_E5_CHAN_IMC0_V3(2),
	ECC_E5_CHAN_IMC0_V3(3),
	ECC_E5_CHAN_IMC1_V3(0, 2),	/* IMC1 chan0 -> channel2 */
	ECC_E5_CHAN_IMC1_V3(1, 3),	/* IMC1 chan1 -> channel3 */

	ECC_E5_CHAN_END
};

#undef ECC_E5_CHAN_END
#undef ECC_E5_CHAN_V2
#undef ECC_E5_CHAN

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
MODULE_DEPEND(ecc_e5, dimm, 1, 1, 1);

static int
ecc_e5_probe(device_t dev)
{
	const struct e5_imc_chan *c;
	uint16_t vid, did;
	int slot, func;

	vid = pci_get_vendor(dev);
	if (vid != PCI_E5_IMC_VID_ID)
		return ENXIO;

	did = pci_get_device(dev);
	slot = pci_get_slot(dev);
	func = pci_get_function(dev);

	for (c = ecc_e5_chans; c->desc != NULL; ++c) {
		if (c->did == did && c->slot == slot && c->func == func) {
			struct ecc_e5_softc *sc = device_get_softc(dev);
			int node;

			node = e5_imc_node_probe(dev, c);
			if (node < 0)
				break;

			device_set_desc(dev, c->desc);

			sc->ecc_chan = c;
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
	int dimm, rank, error, cpuid;
	const cpu_node_t *node;
	uint32_t mcmtr;

	TAILQ_INIT(&sc->ecc_dimm);
	sc->ecc_dev = dev;

	mcmtr = IMC_CPGC_READ_4(sc->ecc_dev, sc->ecc_chan,
	    PCI_E5_IMC_CPGC_MCMTR);
	if (bootverbose) {
		if (sc->ecc_chan->ver == E5_IMC_CHAN_VER3 &&
		    (mcmtr & PCI_E5V3_IMC_CPGC_MCMTR_DDR4))
			ecc_printf(sc, "DDR4\n");
		if (__SHIFTOUT(mcmtr, PCI_E5_IMC_CPGC_MCMTR_IMC_MODE) ==
		    PCI_E5_IMC_CPGC_MCMTR_IMC_MODE_DDR3) {
			ecc_printf(sc, "native %s\n",
			    sc->ecc_chan->ver == E5_IMC_CHAN_VER2 ?
			    "DDR3" : "DDR");
		}
	}

	rank = 0;
	for (dimm = 0; dimm < PCI_E5_IMC_CHN_DIMM_MAX; ++dimm) {
		struct ecc_e5_dimm *dimm_sc;
		struct ksensor *sens;
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
		case PCI_E5V3_IMC_CTAD_DIMMMTR_RANK_CNT_8R:
			if (sc->ecc_chan->ver >= E5_IMC_CHAN_VER3) {
				rank_cnt = 8;
				break;
			}
			/* FALL THROUGH */
		default:
			ecc_printf(sc, "unknown rank count 0x%x\n", val);
			error = ENXIO;
			goto failed;
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
			error = ENXIO;
			goto failed;
		}

		val = __SHIFTOUT(dimmmtr, PCI_E5_IMC_CTAD_DIMMMTR_DDR3_DNSTY);
		switch (val) {
		case PCI_E5_IMC_CTAD_DIMMMTR_DDR3_DNSTY_2G:
			density = 2;
			break;
		case PCI_E5_IMC_CTAD_DIMMMTR_DDR3_DNSTY_4G:
			density = 4;
			break;
		case PCI_E5_IMC_CTAD_DIMMMTR_DDR3_DNSTY_8G:
			density = 8;
			break;
		case PCI_E5_IMC_CTAD_DIMMMTR_DDR3_DNSTY_1G:
			if (sc->ecc_chan->ver < E5_IMC_CHAN_VER3) {
				density = 1;
				break;
			}
			/* FALL THROUGH */
		default:
			ecc_printf(sc, "unknown ddr3 density 0x%x\n", val);
			error = ENXIO;
			goto failed;
		}

		if (bootverbose) {
			ecc_printf(sc, "DIMM%d %dGB, %d%s, density %dGB\n",
			    dimm, density * rank_cnt * 2,
			    rank_cnt, width, density);
		}

		dimm_sc = kmalloc(sizeof(*dimm_sc), M_DEVBUF,
		    M_WAITOK | M_ZERO);
		dimm_sc->dimm_softc =
		    dimm_create(sc->ecc_node, sc->ecc_chan->chan_ext, dimm);

		sens = &dimm_sc->dimm_sensor;
		ksnprintf(sens->desc, sizeof(sens->desc),
		    "node%d chan%d DIMM%d ecc",
		    sc->ecc_node, sc->ecc_chan->chan_ext, dimm);
		sens->type = SENSOR_ECC;
		sensor_set(sens, 0, SENSOR_S_OK);
		dimm_sensor_attach(dimm_sc->dimm_softc, sens);

		TAILQ_INSERT_TAIL(&sc->ecc_dimm, dimm_sc, dimm_link);

		for (r = 0; r < rank_cnt; ++r) {
			struct ecc_e5_rank *rk;

			if (rank >= PCI_E5_IMC_ERROR_RANK_MAX) {
				ecc_printf(sc, "too many ranks\n");
				error = ENXIO;
				goto failed;
			}

			rk = &sc->ecc_rank[rank];
			rk->rank_dimm_sc = dimm_sc;
			++rank;
		}
	}
	sc->ecc_rank_cnt = rank;

	if ((mcmtr & PCI_E5_IMC_CPGC_MCMTR_ECC_EN) == 0) {
		ecc_printf(sc, "ECC is not enabled\n");
		return 0;
	}

	for (rank = 0; rank < sc->ecc_rank_cnt; ++rank) {
		const struct ecc_e5_rank *rk = &sc->ecc_rank[rank];
		uint32_t thr, mask;
		int ofs;

		ofs = PCI_E5_IMC_ERROR_COR_ERR_TH(rank / 2);
		if (rank & 1)
			mask = PCI_E5_IMC_ERROR_COR_ERR_TH_HI;
		else
			mask = PCI_E5_IMC_ERROR_COR_ERR_TH_LO;

		thr = pci_read_config(sc->ecc_dev, ofs, 4);
		dimm_set_ecc_thresh(rk->rank_dimm_sc->dimm_softc,
		    __SHIFTOUT(thr, mask));
	}

	cpuid = -1;
	node = get_cpu_node_by_chipid(sc->ecc_node);
	if (node != NULL && node->child_no > 0) {
		cpuid = BSRCPUMASK(node->members);
		if (bootverbose) {
			device_printf(dev, "node%d chan%d -> cpu%d\n",
			    sc->ecc_node, sc->ecc_chan->chan_ext, cpuid);
		}
	}
	sc->ecc_senstask = sensor_task_register2(sc, ecc_e5_sensor_task,
	    1, cpuid);

	return 0;
failed:
	ecc_e5_detach(dev);
	return error;
}

static void
ecc_e5_sensor_task(void *xsc)
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
			struct ecc_e5_dimm *dimm_sc = rk->rank_dimm_sc;
			uint32_t err, mask;
			int ofs, ecc_cnt;

			ofs = PCI_E5_IMC_ERROR_COR_ERR_CNT(rank / 2);
			if (rank & 1)
				mask = PCI_E5_IMC_ERROR_COR_ERR_CNT_HI;
			else
				mask = PCI_E5_IMC_ERROR_COR_ERR_CNT_LO;

			err = pci_read_config(sc->ecc_dev, ofs, 4);
			ecc_cnt = __SHIFTOUT(err, mask);

			dimm_sensor_ecc_set(dimm_sc->dimm_softc,
			    &dimm_sc->dimm_sensor, ecc_cnt, TRUE);
		} else {
			ecc_printf(sc, "channel%d rank%d critical error\n",
			    sc->ecc_chan->chan_ext, rank);
		}
	}

	if (val & PCI_E5_IMC_ERROR_COR_ERR_STAT_RANKS) {
		pci_write_config(sc->ecc_dev, PCI_E5_IMC_ERROR_COR_ERR_STAT,
		    val, 4);
	}
}

static void
ecc_e5_stop(device_t dev)
{
	struct ecc_e5_softc *sc = device_get_softc(dev);

	if (sc->ecc_senstask != NULL) {
		sensor_task_unregister2(sc->ecc_senstask);
		sc->ecc_senstask = NULL;
	}
}

static int
ecc_e5_detach(device_t dev)
{
	struct ecc_e5_softc *sc = device_get_softc(dev);
	struct ecc_e5_dimm *dimm_sc;

	ecc_e5_stop(dev);

	while ((dimm_sc = TAILQ_FIRST(&sc->ecc_dimm)) != NULL) {
		TAILQ_REMOVE(&sc->ecc_dimm, dimm_sc, dimm_link);
		dimm_sensor_detach(dimm_sc->dimm_softc, &dimm_sc->dimm_sensor);
		dimm_destroy(dimm_sc->dimm_softc);

		kfree(dimm_sc, M_DEVBUF);
	}
	return 0;
}

static void
ecc_e5_shutdown(device_t dev)
{
	ecc_e5_stop(dev);
}
