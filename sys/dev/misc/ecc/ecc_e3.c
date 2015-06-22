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
#include <sys/sensors.h>

#include <bus/pci/pcivar.h>
#include <bus/pci/pcireg.h>
#include <bus/pci/pcibus.h>
#include <bus/pci/pci_cfgreg.h>

#include <vm/pmap.h>

#include "coremctl_if.h"
#include "pcib_if.h"

#include <dev/misc/dimm/dimm.h>
#include <dev/misc/coremctl/coremctl_reg.h>

#define ECC_E3_VER_1	1	/* Sandy Bridge */
#define ECC_E3_VER_2	2	/* Ivy Bridge */
#define ECC_E3_VER_3	3	/* Haswell */

#define ECC_E3_THRESH_DEFAULT	5

#define ECC_E3_CHAN_MAX		2
#define ECC_E3_CHAN_DIMM_MAX	2
#define ECC_E3_DIMM_RANK_MAX	2
#define ECC_E3_CHAN_RANK_MAX	(ECC_E3_CHAN_DIMM_MAX  * ECC_E3_DIMM_RANK_MAX)

struct ecc_e3_type {
	uint16_t	did;
	const char	*desc;
	int		ver;		/* ECC_E3_VER_ */
};

struct ecc_e3_dimm {
	TAILQ_ENTRY(ecc_e3_dimm) dimm_link;
	struct dimm_softc	*dimm_softc;
	struct ksensor		dimm_sensor;
};

struct ecc_e3_rank {
	struct ecc_e3_dimm	*rank_dimm_sc;
};

struct ecc_e3_chan {
	int			chan_id;
	int			chan_errlog0;
	int			chan_rank_cnt;
	struct ecc_e3_rank	chan_rank[ECC_E3_CHAN_RANK_MAX];
};

struct ecc_e3_softc {
	device_t	ecc_dev;
	device_t	ecc_parent;	/* non-NULL if parent has MCHBAR */
	int		ecc_ver;	/* ECC_E3_VER_ */
	uint32_t	ecc_flags;	/* ECC_E3_FLAG_ */

	struct ecc_e3_chan ecc_chan[ECC_E3_CHAN_MAX];
	TAILQ_HEAD(, ecc_e3_dimm) ecc_dimm;

	/*
	 * If the parent does not have MCHBAR,
	 * i.e. no DIMM location information
	 * for the ECC errors, fallback to the
	 * sensor and counters below.
	 */
	struct ksensordev ecc_sensdev;
	struct ksensor	ecc_sens;
	int		ecc_count;
	int		ecc_thresh;
};

#define ECC_E3_FLAG_SENSTASK	0x1
#define ECC_E3_FLAG_CRIT	0x2

#define ecc_printf(sc, fmt, arg...) \
	device_printf((sc)->ecc_dev, fmt , ##arg)

static int	ecc_e3_probe(device_t);
static int	ecc_e3_attach(device_t);
static int	ecc_e3_detach(device_t);
static void	ecc_e3_shutdown(device_t);

static void	ecc_e3_attach_ch(struct ecc_e3_softc *, struct ecc_e3_chan *,
		    int, uint32_t, int);
static void	ecc_e3_errlog(struct ecc_e3_softc *, boolean_t);
static void	ecc_e3_errlog_ch(struct ecc_e3_softc *, struct ecc_e3_chan *,
		    boolean_t);
static void	ecc_e3_stop(struct ecc_e3_softc *);

static void	ecc_e3_sensor_task(void *);
static void	ecc_e3_sensor_update(struct ecc_e3_softc *, boolean_t);

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

	TAILQ_INIT(&sc->ecc_dimm);
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

		ecc_e3_attach_ch(sc, &sc->ecc_chan[0], 0, dimm_ch0,
		    MCH_E3_ERRLOG0_C0);
		ecc_e3_attach_ch(sc, &sc->ecc_chan[1], 1, dimm_ch1,
		    MCH_E3_ERRLOG0_C1);

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

		/*
		 * Add hw.sensors.eccN.ecc0 MIB.
		 */
		strlcpy(sc->ecc_sensdev.xname, device_get_nameunit(dev),
		    sizeof(sc->ecc_sensdev.xname));
		strlcpy(sc->ecc_sens.desc, "node0 ecc",
		    sizeof(sc->ecc_sens.desc));
		sc->ecc_sens.type = SENSOR_ECC;
		sensor_set(&sc->ecc_sens, 0, SENSOR_S_OK);
		sensor_attach(&sc->ecc_sensdev, &sc->ecc_sens);
		sensordev_install(&sc->ecc_sensdev);

		sc->ecc_thresh = ECC_E3_THRESH_DEFAULT;
		SYSCTL_ADD_INT(device_get_sysctl_ctx(dev),
		    SYSCTL_CHILDREN(device_get_sysctl_tree(dev)),
		    OID_AUTO, "thresh", CTLFLAG_RW, &sc->ecc_thresh, 0,
		    "Raise alarm once number of ECC errors "
		    "goes above this value");
	}

	sc->ecc_flags |= ECC_E3_FLAG_SENSTASK;
	sensor_task_register(sc, ecc_e3_sensor_task, 1);

	return 0;
}

static void
ecc_e3_sensor_task(void *xsc)
{
	struct ecc_e3_softc *sc = xsc;
	device_t dev = sc->ecc_dev;
	uint16_t errsts;

	errsts = pci_read_config(dev, PCI_E3_ERRSTS, 2);
	if (errsts & (PCI_E3_ERRSTS_DSERR | PCI_E3_ERRSTS_DMERR)) {
		boolean_t crit = FALSE;

		if (errsts & PCI_E3_ERRSTS_DMERR)
			crit = TRUE;

		if (sc->ecc_parent != NULL)
			ecc_e3_errlog(sc, crit);
		else
			ecc_e3_sensor_update(sc, crit);

		/* Clear pending errors */
		pci_write_config(dev, PCI_E3_ERRSTS, errsts, 2);
	}
}

static void
ecc_e3_attach_ch(struct ecc_e3_softc *sc, struct ecc_e3_chan *chan,
    int chanid, uint32_t dimm_ch, int errlog0)
{
	int dimm_size[ECC_E3_CHAN_DIMM_MAX];
	uint32_t dimm_szmask[ECC_E3_CHAN_DIMM_MAX];
	uint32_t dimm_dlrank[ECC_E3_CHAN_DIMM_MAX];
	int rank, dimm;

	dimm_szmask[0] = MCH_CORE_DIMM_A_SIZE;
	dimm_dlrank[0] = MCH_CORE_DIMM_A_DUAL_RANK;
	dimm_szmask[1] = MCH_CORE_DIMM_B_SIZE;
	dimm_dlrank[1] = MCH_CORE_DIMM_B_DUAL_RANK;
	if (dimm_ch & MCH_CORE_DIMM_A_SELECT) {
		dimm_szmask[0] = MCH_CORE_DIMM_B_SIZE;
		dimm_dlrank[0] = MCH_CORE_DIMM_B_DUAL_RANK;
		dimm_szmask[1] = MCH_CORE_DIMM_A_SIZE;
		dimm_dlrank[1] = MCH_CORE_DIMM_A_DUAL_RANK;
	}

	dimm_size[0] = __SHIFTOUT(dimm_ch, dimm_szmask[0]);
	dimm_size[1] = __SHIFTOUT(dimm_ch, dimm_szmask[1]);
	if (dimm_size[0] == 0 && dimm_size[1] == 0)
		return;

	if (bootverbose) {
		int ecc;

		ecc = __SHIFTOUT(dimm_ch, MCH_E3_DIMM_ECC);
		if (ecc == MCH_E3_DIMM_ECC_NONE) {
			ecc_printf(sc, "channel%d, no ECC active\n", chanid);
		} else if (ecc == MCH_E3_DIMM_ECC_ALL) {
			ecc_printf(sc, "channel%d, ECC active IO/logic\n",
			    chanid);
		} else {
			if (sc->ecc_ver == ECC_E3_VER_1 ||
			    sc->ecc_ver == ECC_E3_VER_2) {
				if (ecc == MCH_E3_DIMM_ECC_IO) {
					ecc_printf(sc, "channel%d, "
					    "ECC active IO\n", chanid);
				} else {
					ecc_printf(sc, "channel%d, "
					    "ECC active logic\n", chanid);
				}
			} else { /* v3 */
				ecc_printf(sc, "channel%d, "
				    "invalid ECC active 0x%x\n", chanid, ecc);
			}
		}
	}

	chan->chan_id = chanid;
	chan->chan_errlog0 = errlog0;

	rank = 0;
	for (dimm = 0; dimm < ECC_E3_CHAN_DIMM_MAX; ++dimm) {
		struct ecc_e3_dimm *dimm_sc;
		struct ecc_e3_rank *rk;
		struct ksensor *sens;

		if (dimm_size[dimm] == 0)
			continue;

		dimm_sc = kmalloc(sizeof(*dimm_sc), M_DEVBUF,
		    M_WAITOK | M_ZERO);
		dimm_sc->dimm_softc = dimm_create(0, chanid, dimm);

		sens = &dimm_sc->dimm_sensor;
		ksnprintf(sens->desc, sizeof(sens->desc),
		    "node0 chan%d DIMM%d ecc", chanid, dimm);
		sens->type = SENSOR_ECC;
		sensor_set(sens, 0, SENSOR_S_OK);
		dimm_sensor_attach(dimm_sc->dimm_softc, sens);

		TAILQ_INSERT_TAIL(&sc->ecc_dimm, dimm_sc, dimm_link);

		KKASSERT(rank < ECC_E3_CHAN_RANK_MAX - 1);
		rk = &chan->chan_rank[rank];
		rank++;
		rk->rank_dimm_sc = dimm_sc;
		if (dimm_ch & dimm_dlrank[dimm]) {
			rk = &chan->chan_rank[rank];
			rank++;
			rk->rank_dimm_sc = dimm_sc;
		}
	}
	chan->chan_rank_cnt = rank;
}

static void
ecc_e3_errlog(struct ecc_e3_softc *sc, boolean_t crit)
{
	int i;

	for (i = 0; i < ECC_E3_CHAN_MAX; ++i) {
		struct ecc_e3_chan *chan = &sc->ecc_chan[i];

		if (chan->chan_errlog0 != 0)
			ecc_e3_errlog_ch(sc, chan, crit);
	}
}

static void
ecc_e3_errlog_ch(struct ecc_e3_softc *sc, struct ecc_e3_chan *chan,
    boolean_t crit)
{
	uint32_t err0;
	int rank;

	err0 = CSR_READ_4(sc, chan->chan_errlog0);
	if ((err0 & (MCH_E3_ERRLOG0_CERRSTS | MCH_E3_ERRLOG0_MERRSTS)) == 0)
		return;

	rank = __SHIFTOUT(err0, MCH_E3_ERRLOG0_ERRRANK);
	if (rank >= chan->chan_rank_cnt) {
		ecc_printf(sc, "channel%d rank%d %serror\n", chan->chan_id,
		    rank, crit ? "critical " : "");
	} else {
		struct ecc_e3_dimm *dimm_sc;

		dimm_sc = chan->chan_rank[rank].rank_dimm_sc;
		dimm_sensor_ecc_add(dimm_sc->dimm_softc, &dimm_sc->dimm_sensor,
		    1, crit);
	}
}

static int
ecc_e3_detach(device_t dev)
{
	struct ecc_e3_softc *sc = device_get_softc(dev);

	ecc_e3_stop(sc);

	if (sc->ecc_parent != NULL) {
		struct ecc_e3_dimm *dimm_sc;

		while ((dimm_sc = TAILQ_FIRST(&sc->ecc_dimm)) != NULL) {
			TAILQ_REMOVE(&sc->ecc_dimm, dimm_sc, dimm_link);
			dimm_sensor_detach(dimm_sc->dimm_softc,
			    &dimm_sc->dimm_sensor);
			dimm_destroy(dimm_sc->dimm_softc);

			kfree(dimm_sc, M_DEVBUF);
		}
	} else {
		sensordev_deinstall(&sc->ecc_sensdev);
	}
	return 0;
}

static void
ecc_e3_shutdown(device_t dev)
{
	ecc_e3_stop(device_get_softc(dev));
}

static void
ecc_e3_stop(struct ecc_e3_softc *sc)
{
	if (sc->ecc_flags & ECC_E3_FLAG_SENSTASK)
		sensor_task_unregister(sc);
}

static void
ecc_e3_sensor_update(struct ecc_e3_softc *sc, boolean_t crit)
{
	enum sensor_status status;

	sc->ecc_count++;
	if (!crit && sc->ecc_count >= sc->ecc_thresh)
		crit = TRUE;

	if (crit && (sc->ecc_flags & ECC_E3_FLAG_CRIT) == 0) {
		char ecc_str[16];

		ksnprintf(ecc_str, sizeof(ecc_str), "%d", sc->ecc_count);
		devctl_notify("ecc", "ECC", ecc_str, "node=0");

		ecc_printf(sc, "too many ECC errors %d\n", sc->ecc_count);
		sc->ecc_flags |= ECC_E3_FLAG_CRIT;
	}

	if (sc->ecc_flags & ECC_E3_FLAG_CRIT)
		status = SENSOR_S_CRIT;
	else
		status = SENSOR_S_OK;
	sensor_set(&sc->ecc_sens, sc->ecc_count, status);
}
