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
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/sensors.h>
#include <sys/sysctl.h>
#include <sys/systm.h>

#include <dev/misc/dimm/dimm.h>

#define DIMM_TEMP_HIWAT_DEFAULT	85
#define DIMM_TEMP_LOWAT_DEFAULT	75

struct dimm_softc {
	TAILQ_ENTRY(dimm_softc) dimm_link;
	int			dimm_node;
	int			dimm_chan;
	int			dimm_slot;
	int			dimm_temp_hiwat;
	int			dimm_temp_lowat;
	int			dimm_id;
	int			dimm_ref;
	int			dimm_ecc_cnt;

	struct ksensordev	dimm_sensdev;
	uint32_t		dimm_sens_taskflags;	/* DIMM_SENS_TF_ */

	struct sysctl_ctx_list	dimm_sysctl_ctx;
	struct sysctl_oid	*dimm_sysctl_tree;
};
TAILQ_HEAD(dimm_softc_list, dimm_softc);

#define DIMM_SENS_TF_TEMP_CRIT		0x1
#define DIMM_SENS_TF_ECC_CRIT		0x2

static void	dimm_mod_unload(void);

/* In the ascending order of dimm_softc.dimm_id */
static struct dimm_softc_list	dimm_softc_list;

static SYSCTL_NODE(_hw, OID_AUTO, dimminfo, CTLFLAG_RD, NULL,
    "DIMM information");

struct dimm_softc *
dimm_create(int node, int chan, int slot)
{
	struct dimm_softc *sc, *after = NULL;
	int dimm_id = 0;

	SYSCTL_XLOCK();

	TAILQ_FOREACH(sc, &dimm_softc_list, dimm_link) {
		/*
		 * Already exists; done.
		 */
		if (sc->dimm_node == node && sc->dimm_chan == chan &&
		    sc->dimm_slot == slot) {
			KASSERT(sc->dimm_ref > 0, ("invalid dimm reference %d",
			    sc->dimm_ref));
			sc->dimm_ref++;
			SYSCTL_XUNLOCK();
			return sc;
		}

		/*
		 * Find the lowest usable id.
		 */
		if (sc->dimm_id == dimm_id) {
			++dimm_id;
			after = sc;
		}
	}

	sc = kmalloc(sizeof(*sc), M_DEVBUF, M_WAITOK | M_ZERO);
	sc->dimm_node = node;
	sc->dimm_chan = chan;
	sc->dimm_slot = slot;
	sc->dimm_id = dimm_id;
	sc->dimm_ref = 1;
	sc->dimm_temp_hiwat = DIMM_TEMP_HIWAT_DEFAULT;
	sc->dimm_temp_lowat = DIMM_TEMP_LOWAT_DEFAULT;

	ksnprintf(sc->dimm_sensdev.xname, sizeof(sc->dimm_sensdev.xname),
	    "dimm%d", sc->dimm_id);

	/*
	 * Create sysctl tree for the location information.  Use
	 * same name as the sensor device.
	 */
	sysctl_ctx_init(&sc->dimm_sysctl_ctx);
	sc->dimm_sysctl_tree = SYSCTL_ADD_NODE(&sc->dimm_sysctl_ctx,
	    SYSCTL_STATIC_CHILDREN(_hw_dimminfo), OID_AUTO,
	    sc->dimm_sensdev.xname, CTLFLAG_RD, 0, "");
	if (sc->dimm_sysctl_tree != NULL) {
		SYSCTL_ADD_INT(&sc->dimm_sysctl_ctx,
		    SYSCTL_CHILDREN(sc->dimm_sysctl_tree), OID_AUTO,
		    "node", CTLFLAG_RD, &sc->dimm_node, 0,
		    "CPU node of this DIMM");
		SYSCTL_ADD_INT(&sc->dimm_sysctl_ctx,
		    SYSCTL_CHILDREN(sc->dimm_sysctl_tree), OID_AUTO,
		    "chan", CTLFLAG_RD, &sc->dimm_chan, 0,
		    "channel of this DIMM");
		SYSCTL_ADD_INT(&sc->dimm_sysctl_ctx,
		    SYSCTL_CHILDREN(sc->dimm_sysctl_tree), OID_AUTO,
		    "slot", CTLFLAG_RD, &sc->dimm_slot, 0,
		    "slot of this DIMM");
		SYSCTL_ADD_INT(&sc->dimm_sysctl_ctx,
		    SYSCTL_CHILDREN(sc->dimm_sysctl_tree), OID_AUTO,
		    "temp_hiwat", CTLFLAG_RW, &sc->dimm_temp_hiwat, 0,
		    "Raise alarm once DIMM temperature is above this value "
		    "(unit: C)");
		SYSCTL_ADD_INT(&sc->dimm_sysctl_ctx,
		    SYSCTL_CHILDREN(sc->dimm_sysctl_tree), OID_AUTO,
		    "temp_lowat", CTLFLAG_RW, &sc->dimm_temp_lowat, 0,
		    "Cancel alarm once DIMM temperature is below this value "
		    "(unit: C)");
	}

	if (after == NULL) {
		KKASSERT(sc->dimm_id == 0);
		TAILQ_INSERT_HEAD(&dimm_softc_list, sc, dimm_link);
	} else {
		TAILQ_INSERT_AFTER(&dimm_softc_list, after, sc, dimm_link);
	}

	sensordev_install(&sc->dimm_sensdev);

	SYSCTL_XUNLOCK();
	return sc;
}

int
dimm_destroy(struct dimm_softc *sc)
{
	SYSCTL_XLOCK();

	KASSERT(sc->dimm_ref > 0, ("invalid dimm reference %d", sc->dimm_ref));
	sc->dimm_ref--;
	if (sc->dimm_ref > 0) {
		SYSCTL_XUNLOCK();
		return EAGAIN;
	}

	sensordev_deinstall(&sc->dimm_sensdev);

	TAILQ_REMOVE(&dimm_softc_list, sc, dimm_link);
	if (sc->dimm_sysctl_tree != NULL)
		sysctl_ctx_free(&sc->dimm_sysctl_ctx);
	kfree(sc, M_DEVBUF);

	SYSCTL_XUNLOCK();
	return 0;
}

void
dimm_sensor_attach(struct dimm_softc *sc, struct ksensor *sens)
{
	sensor_attach(&sc->dimm_sensdev, sens);
}

void
dimm_sensor_detach(struct dimm_softc *sc, struct ksensor *sens)
{
	sensor_detach(&sc->dimm_sensdev, sens);
}

void
dimm_set_temp_thresh(struct dimm_softc *sc, int hiwat, int lowat)
{
	sc->dimm_temp_hiwat = hiwat;
	sc->dimm_temp_lowat = lowat;
}

void
dimm_sensor_temp(struct dimm_softc *sc, struct ksensor *sens, int temp)
{
	if (temp >= sc->dimm_temp_hiwat &&
	    (sc->dimm_sens_taskflags & DIMM_SENS_TF_TEMP_CRIT) == 0) {
		char temp_str[16], data[64];

		ksnprintf(temp_str, sizeof(temp_str), "%d", temp);
		ksnprintf(data, sizeof(data), "node=%d channel=%d dimm=%d",
		    sc->dimm_node, sc->dimm_chan, sc->dimm_slot);
		devctl_notify("memtemp", "Thermal", temp_str, data);

		kprintf("dimm%d: node%d channel%d DIMM%d "
		    "temperature (%dC) is too high (>= %dC)\n",
		    sc->dimm_id, sc->dimm_node, sc->dimm_chan, sc->dimm_slot,
		    temp, sc->dimm_temp_hiwat);

		sc->dimm_sens_taskflags |= DIMM_SENS_TF_TEMP_CRIT;
	} else if ((sc->dimm_sens_taskflags & DIMM_SENS_TF_TEMP_CRIT) &&
	     temp < sc->dimm_temp_lowat) {
		sc->dimm_sens_taskflags &= ~DIMM_SENS_TF_TEMP_CRIT;
	}

	if (sc->dimm_sens_taskflags & DIMM_SENS_TF_TEMP_CRIT)
		sens->status = SENSOR_S_CRIT;
	else
		sens->status = SENSOR_S_OK;
	sens->flags &= ~SENSOR_FINVALID;
	sens->value = (temp * 1000000) + 273150000;
}

void
dimm_sensor_ecc_set(struct dimm_softc *sc, struct ksensor *sens,
    int ecc_cnt, boolean_t crit)
{
	if (crit && (sc->dimm_sens_taskflags & DIMM_SENS_TF_ECC_CRIT) == 0) {
		/* TODO devctl(4) */
		sc->dimm_sens_taskflags |= DIMM_SENS_TF_ECC_CRIT;
	} else if (!crit && (sc->dimm_sens_taskflags & DIMM_SENS_TF_ECC_CRIT)) {
		sc->dimm_sens_taskflags &= ~DIMM_SENS_TF_ECC_CRIT;
	}
	sc->dimm_ecc_cnt = ecc_cnt;

	if (sc->dimm_sens_taskflags & DIMM_SENS_TF_ECC_CRIT)
		sens->status = SENSOR_S_CRIT;
	else
		sens->status = SENSOR_S_OK;
	sens->flags &= ~SENSOR_FINVALID;
	sens->value = ecc_cnt;
}

static void
dimm_mod_unload(void)
{
	struct dimm_softc *sc;

	SYSCTL_XLOCK();

	while ((sc = TAILQ_FIRST(&dimm_softc_list)) != NULL) {
		int error;

		error = dimm_destroy(sc);
		KASSERT(!error, ("dimm%d is still referenced, ref %d",
		    sc->dimm_id, sc->dimm_ref));
	}

	SYSCTL_XUNLOCK();
}

static int
dimm_mod_event(module_t mod, int type, void *unused)
{
	switch (type) {
	case MOD_LOAD:
		TAILQ_INIT(&dimm_softc_list);
		return 0;

	case MOD_UNLOAD:
		dimm_mod_unload();
		return 0;

	default:
		return 0;
	}
}

static moduledata_t dimm_mod = {
	"dimm",
	dimm_mod_event,
	0
};
DECLARE_MODULE(dimm, dimm_mod, SI_SUB_PRE_DRIVERS, SI_ORDER_ANY);
MODULE_VERSION(dimm, 1);
