/*
 * Copyright (c) 2004 INRIA
 * Copyright (c) 2002-2005 Sam Leffler, Errno Consulting
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    similar to the "NO WARRANTY" disclaimer below ("Disclaimer") and any
 *    redistribution must be conditioned upon including a substantially
 *    similar Disclaimer requirement for further binary redistribution.
 * 3. Neither the names of the above-listed copyright holders nor the names
 *    of any contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") version 2 as published by the Free
 * Software Foundation.
 *
 * NO WARRANTY
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF NONINFRINGEMENT, MERCHANTIBILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY,
 * OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGES.
 *
 * $FreeBSD: src/sys/dev/ath/ath_rate/amrr/amrr.c,v 1.8.2.3 2006/02/24 19:51:11 sam Exp $
 * $DragonFly: src/sys/netproto/802_11/wlan_ratectl/amrr/ieee80211_ratectl_amrr.c,v 1.2 2006/09/05 00:55:48 dillon Exp $
 */

/*
 * AMRR rate control. See:
 * http://www-sop.inria.fr/rapports/sophia/RR-5208.html
 * "IEEE 802.11 Rate Adaptation: A Practical Approach" by
 *    Mathieu Lacage, Hossein Manshaei, Thierry Turletti
 */

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/sysctl.h>
#include <sys/serialize.h>

#include <net/if.h>
#include <net/if_media.h>
#include <net/if_arp.h>

#include <netproto/802_11/ieee80211_var.h>
#include <netproto/802_11/wlan_ratectl/amrr/ieee80211_ratectl_amrr.h>

#define	AMRR_DEBUG
#ifdef AMRR_DEBUG
#define	DPRINTF(asc, lv, fmt, ...) do {		\
	if ((asc)->debug >= lv)			\
		printf(fmt, __VA_ARGS__);	\
} while (0)
#else
#define	DPRINTF(asc, lv, fmt, ...)
#endif

static void	*amrr_attach(struct ieee80211com *);
static void	amrr_detach(void *);
static void	amrr_data_alloc(struct ieee80211_node *);
static void	amrr_data_free(struct ieee80211_node *);
static void	amrr_data_dup(const struct ieee80211_node *,
			      struct ieee80211_node *);
static void	amrr_newstate(void *, enum ieee80211_state);
static void	amrr_tx_complete(void *, struct ieee80211_node *, int,
				 const struct ieee80211_ratectl_res[],
				 int, int, int, int);
static void	amrr_newassoc(void *, struct ieee80211_node *, int);
static int	amrr_findrate(void *, struct ieee80211_node *, int,
			      int[], int);

static void	amrr_sysctl_attach(struct amrr_softc *);
static void	amrr_update(struct amrr_softc *, struct ieee80211_node *, int);
static void	amrr_start(struct amrr_softc *, struct ieee80211_node *);
static void	amrr_tick(void *);
static void	amrr_ratectl(void *, struct ieee80211_node *);

static const struct ieee80211_ratectl amrr = {
	.rc_name	= "amrr",
	.rc_ratectl	= IEEE80211_RATECTL_AMRR,
	.rc_attach	= amrr_attach,
	.rc_detach	= amrr_detach,
	.rc_data_alloc	= amrr_data_alloc,
	.rc_data_free	= amrr_data_free,
	.rc_data_dup	= amrr_data_dup,
	.rc_newstate	= amrr_newstate,
	.rc_tx_complete	= amrr_tx_complete,
	.rc_newassoc	= amrr_newassoc,
	.rc_findrate	= amrr_findrate
};

static u_int	amrr_nrefs;

MALLOC_DEFINE(M_AMRR_RATECTL_DATA, "amrr_ratectl_data",
	      "amrr rate control data");

static int
amrr_findrate(void *arg __unused, struct ieee80211_node *ni,
	      int frame_len __unused, int rateidx[], int rateidx_len)
{
	int i, rate_idx = ni->ni_txrate;

	for (i = 0; i < rateidx_len && i < IEEE80211_AMRR_RATEIDX_MAX; ++i) {
		if (rate_idx < 0)
			break;
		rateidx[i] = rate_idx--;
	}
	if (rateidx_len > 1)
		rateidx[rateidx_len - 1] = 0;
	return i;
}

static void
amrr_tx_complete(void *arg __unused, struct ieee80211_node *ni,
		 int frame_len __unused,
		 const struct ieee80211_ratectl_res res[],
		 int res_len, int short_retries __unused,
		 int long_retries __unused, int is_fail)
{
	struct amrr_data *ad = ni->ni_rate_data;
	int i;

	if (ad == NULL)
		return;

	for (i = 0; i < res_len && i < IEEE80211_AMRR_RATEIDX_MAX; ++i)
		ad->ad_tx_try_cnt[i]++;
	if (is_fail)
		ad->ad_tx_failure_cnt++;
}

static void
amrr_newassoc(void *arg, struct ieee80211_node *ni, int isnew)
{
	if (isnew)
		amrr_start(arg, ni);
}

/*
 * The code below assumes that we are dealing with hardware multi rate retry
 * I have no idea what will happen if you try to use this module with another
 * type of hardware. Your machine might catch fire or it might work with
 * horrible performance...
 */
static void
amrr_update(struct amrr_softc *asc, struct ieee80211_node *ni, int rate)
{
	struct amrr_data *ad = ni->ni_rate_data;

	DPRINTF(asc, 5, "%s: set xmit rate for %6D to %dM\n",
		__func__, ni->ni_macaddr, ":",
		ni->ni_rates.rs_nrates > 0 ?
		IEEE80211_RS_RATE(&ni->ni_rates, rate) / 2 : 0);

	ni->ni_txrate = rate;

	if (ad == NULL) {
		amrr_data_alloc(ni);
		ad = ni->ni_rate_data;
		if (ad == NULL)
			return;
	}

	ad->ad_tx_try_cnt[0] = 0;
	ad->ad_tx_try_cnt[1] = 0;
	ad->ad_tx_try_cnt[2] = 0;
	ad->ad_tx_try_cnt[3] = 0;
	ad->ad_tx_failure_cnt = 0;
  	ad->ad_success = 0;
  	ad->ad_recovery = 0;
  	ad->ad_success_threshold = asc->min_success_threshold;
}

/*
 * Set the starting transmit rate for a node.
 */
static void
amrr_start(struct amrr_softc *asc, struct ieee80211_node *ni)
{
#define	RATE(_ix)	IEEE80211_RS_RATE(&ni->ni_rates, (_ix))
	struct ieee80211com *ic = asc->ic;
	int srate;

	KASSERT(ni->ni_rates.rs_nrates > 0, ("no rates"));

	if (ic->ic_fixed_rate == IEEE80211_FIXED_RATE_NONE) {
		/*
		 * For adhoc or ibss mode, start from the lowest rate.
		 */
		if (ic->ic_opmode == IEEE80211_M_AHDEMO ||
		    ic->ic_opmode == IEEE80211_M_IBSS) {
			amrr_update(asc, ni, 0);
			return;
		}

		/*
		 * No fixed rate is requested. For 11b start with
		 * the highest negotiated rate; otherwise, for 11g
		 * and 11a, we start "in the middle" at 24Mb or 36Mb.
		 */
		srate = ni->ni_rates.rs_nrates - 1;
		if (ic->ic_curmode != IEEE80211_MODE_11B) {
			/*
			 * Scan the negotiated rate set to find the
			 * closest rate.
			 */
			/* NB: the rate set is assumed sorted */
			for (; srate >= 0 && RATE(srate) > 72; srate--)
				;
			KASSERT(srate >= 0, ("bogus rate set"));
		}
	} else {
		/*
		 * A fixed rate is to be used; ic_fixed_rate is an
		 * index into the supported rate set.  Convert this
		 * to the index into the negotiated rate set for
		 * the node.  We know the rate is there because the
		 * rate set is checked when the station associates.
		 */
		const struct ieee80211_rateset *rs =
			&ic->ic_sup_rates[ic->ic_curmode];
		int r = IEEE80211_RS_RATE(rs, ic->ic_fixed_rate);

		/* NB: the rate set is assumed sorted */
		srate = ni->ni_rates.rs_nrates - 1;
		for (; srate >= 0 && RATE(srate) != r; srate--)
			;
		KASSERT(srate >= 0,
			("fixed rate %d not in rate set", ic->ic_fixed_rate));
	}
	amrr_update(asc, ni, srate);
#undef RATE
}

static void
amrr_rate_cb(void *arg, struct ieee80211_node *ni)
{
	amrr_update(arg, ni, 0);
}

/*
 * Reset the rate control state for each 802.11 state transition.
 */
static void
amrr_newstate(void *arg, enum ieee80211_state state)
{
	struct amrr_softc *asc = arg;
	struct ieee80211com *ic = asc->ic;
	struct ieee80211_node *ni;

	if (state == IEEE80211_S_INIT) {
		callout_stop(&asc->timer);
		return;
	}

	if (ic->ic_opmode == IEEE80211_M_STA) {
		/*
		 * Reset local xmit state; this is really only
		 * meaningful when operating in station mode.
		 */
		ni = ic->ic_bss;
		if (state == IEEE80211_S_RUN)
			amrr_start(asc, ni);
		else
			amrr_update(asc, ni, 0);
	} else {
		/*
		 * When operating as a station the node table holds
		 * the AP's that were discovered during scanning.
		 * For any other operating mode we want to reset the
		 * tx rate state of each node.
		 */
		ieee80211_iterate_nodes(&ic->ic_sta, amrr_rate_cb, asc);
		amrr_update(asc, ic->ic_bss, 0);
	}
	if (ic->ic_fixed_rate == IEEE80211_FIXED_RATE_NONE &&
	    state == IEEE80211_S_RUN) {
		int interval;

		/*
		 * Start the background rate control thread if we
		 * are not configured to use a fixed xmit rate.
		 */
		interval = asc->interval;
		if (ic->ic_opmode == IEEE80211_M_STA)
			interval /= 2;
		callout_reset(&asc->timer, (interval * hz) / 1000,
			      amrr_tick, asc);
	}
}

/* 
 * Examine and potentially adjust the transmit rate.
 */
static void
amrr_ratectl(void *arg, struct ieee80211_node *ni)
{
	struct amrr_softc *asc = arg;
	struct amrr_data *ad = ni->ni_rate_data;
	int old_rate;

	if (ad == NULL) {
		/* We are not ready to go, set TX rate to lowest one */
		ni->ni_txrate = 0;
		return;
	}

#define is_success(ad) \
(ad->ad_tx_try_cnt[1]  < (ad->ad_tx_try_cnt[0] / 10))
#define is_enough(ad) \
(ad->ad_tx_try_cnt[0] > 10)
#define is_failure(ad) \
(ad->ad_tx_try_cnt[1] > (ad->ad_tx_try_cnt[0] / 3))
#define is_max_rate(ni) \
((ni->ni_txrate + 1) >= ni->ni_rates.rs_nrates)
#define is_min_rate(ni) \
(ni->ni_txrate == 0)

	old_rate = ni->ni_txrate;
  
  	DPRINTF(asc, 10, "cnt0: %d cnt1: %d cnt2: %d cnt3: %d -- "
		"threshold: %d\n",
		ad->ad_tx_try_cnt[0],
		ad->ad_tx_try_cnt[1],
		ad->ad_tx_try_cnt[2],
		ad->ad_tx_try_cnt[3],
		ad->ad_success_threshold);

  	if (is_success(ad) && is_enough(ad)) {
		ad->ad_success++;
		if (ad->ad_success == ad->ad_success_threshold &&
  		    !is_max_rate(ni)) {
  			ad->ad_recovery = 1;
  			ad->ad_success = 0;
  			ni->ni_txrate++;
			DPRINTF(asc, 5, "increase rate to %d\n", ni->ni_txrate);
  		} else {
			ad->ad_recovery = 0;
		}
	} else if (is_failure(ad)) {
  		ad->ad_success = 0;
		if (!is_min_rate(ni)) {
  			if (ad->ad_recovery) {
  				/* recovery failure. */
  				ad->ad_success_threshold *= 2;
  				ad->ad_success_threshold =
					min(ad->ad_success_threshold,
					    (u_int)asc->max_success_threshold);
 				DPRINTF(asc, 5, "decrease rate recovery thr: "
					"%d\n", ad->ad_success_threshold);
  			} else {
  				/* simple failure. */
 				ad->ad_success_threshold =
					asc->min_success_threshold;
 				DPRINTF(asc, 5, "decrease rate normal thr: "
					"%d\n", ad->ad_success_threshold);
  			}
			ad->ad_recovery = 0;
  			ni->ni_txrate--;
   		} else {
			ad->ad_recovery = 0;
		}
   	}
	if (is_enough(ad) || old_rate != ni->ni_txrate) {
		/* reset counters. */
		ad->ad_tx_try_cnt[0] = 0;
		ad->ad_tx_try_cnt[1] = 0;
		ad->ad_tx_try_cnt[2] = 0;
		ad->ad_tx_try_cnt[3] = 0;
		ad->ad_tx_failure_cnt = 0;
	}
	if (old_rate != ni->ni_txrate)
		amrr_update(asc, ni, ni->ni_txrate);
}

static void
amrr_tick(void *arg)
{
	struct amrr_softc *asc = arg;
	struct ieee80211com *ic = asc->ic;
	struct ifnet *ifp = &ic->ic_if;
	int interval;

	lwkt_serialize_enter(ifp->if_serializer);

	if (ifp->if_flags & IFF_RUNNING) {
		if (ic->ic_opmode == IEEE80211_M_STA)
			amrr_ratectl(asc, ic->ic_bss);	/* NB: no reference */
		else
			ieee80211_iterate_nodes(&ic->ic_sta, amrr_ratectl, asc);
	}
	interval = asc->interval;
	if (ic->ic_opmode == IEEE80211_M_STA)
		interval /= 2;
	callout_reset(&asc->timer, (interval * hz) / 1000, amrr_tick, asc);

	lwkt_serialize_exit(ifp->if_serializer);
}

static void
amrr_sysctl_attach(struct amrr_softc *asc)
{
	asc->debug = 0;
	asc->interval = 1000;
	asc->max_success_threshold = 10;
	asc->min_success_threshold = 1;

	sysctl_ctx_init(&asc->sysctl_ctx);
	asc->sysctl_oid = SYSCTL_ADD_NODE(&asc->sysctl_ctx,
		SYSCTL_CHILDREN(asc->ic->ic_sysctl_oid),
		OID_AUTO, "amrr_ratectl", CTLFLAG_RD, 0, "");
	if (asc->sysctl_oid == NULL) {
		printf("wlan_ratectl_amrr: create sysctl tree failed\n");
		return;
	}

	SYSCTL_ADD_INT(&asc->sysctl_ctx, SYSCTL_CHILDREN(asc->sysctl_oid),
		       OID_AUTO, "interval", CTLFLAG_RW,
		       &asc->interval, 0,
		       "rate control: operation interval (ms)");

	/* XXX bounds check values */
	SYSCTL_ADD_INT(&asc->sysctl_ctx, SYSCTL_CHILDREN(asc->sysctl_oid),
		       OID_AUTO, "max_sucess_threshold", CTLFLAG_RW,
		       &asc->max_success_threshold, 0, "");

	SYSCTL_ADD_INT(&asc->sysctl_ctx, SYSCTL_CHILDREN(asc->sysctl_oid),
		       OID_AUTO, "min_sucess_threshold", CTLFLAG_RW,
		       &asc->min_success_threshold, 0, "");

	SYSCTL_ADD_INT(&asc->sysctl_ctx, SYSCTL_CHILDREN(asc->sysctl_oid),
		       OID_AUTO, "debug", CTLFLAG_RW,
		       &asc->debug, 0, "debug level");
}

static void *
amrr_attach(struct ieee80211com *ic)
{
	struct amrr_softc *asc;

	amrr_nrefs++;

	asc = kmalloc(sizeof(struct amrr_softc), M_DEVBUF, M_WAITOK | M_ZERO);

	asc->ic = ic;
	callout_init(&asc->timer);
	amrr_sysctl_attach(asc);

	amrr_newstate(asc, ic->ic_state);

	return asc;
}

static void
_amrr_data_free(void *arg __unused, struct ieee80211_node *ni)
{
	amrr_data_free(ni);
}

void
amrr_detach(void *arg)
{
	struct amrr_softc *asc = arg;
	struct ieee80211com *ic = asc->ic;

	amrr_newstate(asc, IEEE80211_S_INIT);

	ieee80211_iterate_nodes(&ic->ic_sta, _amrr_data_free, NULL);
	ieee80211_iterate_nodes(&ic->ic_scan, _amrr_data_free, NULL);

	if (asc->sysctl_oid != NULL)
		sysctl_ctx_free(&asc->sysctl_ctx);
	kfree(asc, M_DEVBUF);

	amrr_nrefs--;
}

static void
amrr_data_free(struct ieee80211_node *ni)
{
	if (ni->ni_rate_data != NULL) {
		kfree(ni->ni_rate_data, M_AMRR_RATECTL_DATA);
		ni->ni_rate_data = NULL;
	}
}

static void
amrr_data_alloc(struct ieee80211_node *ni)
{
	KKASSERT(ni->ni_rate_data == NULL);
	ni->ni_rate_data = malloc(sizeof(struct amrr_data),
				  M_AMRR_RATECTL_DATA, M_NOWAIT | M_ZERO);
}

static void
amrr_data_dup(const struct ieee80211_node *oni, struct ieee80211_node *nni)
{
	if (oni->ni_rate_data == NULL || nni->ni_rate_data == NULL)
		return;

	bcopy(oni->ni_rate_data, nni->ni_rate_data, sizeof(struct amrr_data));
}

/*
 * Module glue.
 */
static int
amrr_modevent(module_t mod, int type, void *unused)
{
	switch (type) {
	case MOD_LOAD:
		ieee80211_ratectl_register(&amrr);
		return 0;
	case MOD_UNLOAD:
		if (amrr_nrefs) {
			printf("wlan_ratectl_amrr: still in use "
			       "(%u dynamic refs)\n", amrr_nrefs);
			return EBUSY;
		}
		ieee80211_ratectl_unregister(&amrr);
		return 0;
	}
	return EINVAL;
}

static moduledata_t amrr_mod = {
	"wlan_ratectl_amrr",
	amrr_modevent,
	0
};
DECLARE_MODULE(wlan_ratectl_amrr, amrr_mod, SI_SUB_DRIVERS, SI_ORDER_FIRST);
MODULE_VERSION(wlan_ratectl_amrr, 1);
MODULE_DEPEND(wlan_ratectl_amrr, wlan, 1, 1, 1);
