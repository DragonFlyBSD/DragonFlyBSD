/*
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
 * $FreeBSD: src/sys/dev/ath/ath_rate/onoe/onoe.c,v 1.8.2.3 2006/02/24 19:51:11 sam Exp $
 * $DragonFly: src/sys/netproto/802_11/wlan_ratectl/onoe/ieee80211_ratectl_onoe.c,v 1.3 2006/09/05 03:48:13 dillon Exp $
 */

/*
 * Atsushi Onoe's rate control algorithm.
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
#include <netproto/802_11/wlan_ratectl/onoe/ieee80211_ratectl_onoe.h>

#define ONOE_DEBUG

#ifdef ONOE_DEBUG
#define	DPRINTF(osc, lv, fmt, ...) do {		\
	if ((osc)->debug >= lv)			\
		printf(fmt, __VA_ARGS__);	\
} while (0)
#else
#define	DPRINTF(osc, lv, fmt, ...)
#endif

/*
 * Default parameters for the rate control algorithm.  These are
 * all tunable with sysctls.  The rate controller runs periodically
 * (each ath_rateinterval ms) analyzing transmit statistics for each
 * neighbor/station (when operating in station mode this is only the AP).
 * If transmits look to be working well over a sampling period then
 * it gives a "raise rate credit".  If transmits look to not be working
 * well than it deducts a credit.  If the credits cross a threshold then
 * the transmit rate is raised.  Various error conditions force the
 * the transmit rate to be dropped.
 *
 * The decision to issue/deduct a credit is based on the errors and
 * retries accumulated over the sampling period.  ath_rate_raise defines
 * the percent of retransmits for which a credit is issued/deducted.
 * ath_rate_raise_threshold defines the threshold on credits at which
 * the transmit rate is increased.
 *
 * XXX this algorithm is flawed.
 */

static void	*onoe_attach(struct ieee80211com *);
static void	onoe_detach(void *);
static void	onoe_data_free(struct ieee80211_node *);
static void	onoe_data_alloc(struct ieee80211_node *);
static void	onoe_data_dup(const struct ieee80211_node *,
			      struct ieee80211_node *);
static void	onoe_newstate(void *, enum ieee80211_state);
static void	onoe_tx_complete(void *, struct ieee80211_node *, int,
				 const struct ieee80211_ratectl_res[],
				 int, int, int, int);
static void	onoe_newassoc(void *, struct ieee80211_node *, int);
static int	onoe_findrate(void *, struct ieee80211_node *, int,
			      int[], int);

static void	onoe_sysctl_attach(struct onoe_softc *);
static void	onoe_update(struct onoe_softc *, struct ieee80211_node *, int);
static void	onoe_start(struct onoe_softc *, struct ieee80211_node *);
static void	onoe_tick(void *);
static void	onoe_ratectl(void *, struct ieee80211_node *);

static const struct ieee80211_ratectl onoe = {
	.rc_name	= "onoe",
	.rc_ratectl	= IEEE80211_RATECTL_ONOE,
	.rc_attach	= onoe_attach,
	.rc_detach	= onoe_detach,
	.rc_data_alloc	= onoe_data_alloc,
	.rc_data_free	= onoe_data_free,
	.rc_data_dup	= onoe_data_dup,
	.rc_newstate	= onoe_newstate,
	.rc_tx_complete	= onoe_tx_complete,
	.rc_newassoc	= onoe_newassoc,
	.rc_findrate	= onoe_findrate
};

static u_int	onoe_nrefs;

MALLOC_DEFINE(M_ONOE_RATECTL_DATA, "onoe_ratectl_data",
	      "onoe rate control data");

static void
onoe_tx_complete(void *arg __unused, struct ieee80211_node *ni,
		 int frame_len __unused,
		 const struct ieee80211_ratectl_res res[] __unused,
		 int res_len __unused,
		 int short_retries, int long_retries, int is_fail)
{
	struct onoe_data *od = ni->ni_rate_data;

	if (od == NULL)
		return;

	if (is_fail)
		od->od_tx_err++;
	else
		od->od_tx_ok++;

	od->od_tx_retr += long_retries + short_retries;
}

static void
onoe_newassoc(void *arg, struct ieee80211_node *ni, int is_new)
{
	if (is_new)
		onoe_start(arg, ni);
}

static int
onoe_findrate(void *arg __unused, struct ieee80211_node *ni,
	      int frame_len __unused, int rateidx[], int rateidx_len)
{
	int i, rate_idx = ni->ni_txrate;

	for (i = 0; i < rateidx_len; ++i) {
		if (rate_idx < 0)
			break;
		rateidx[i] = rate_idx--;
	}
	if (rateidx_len > 1)
		rateidx[rateidx_len - 1] = 0;
	return i;
}

static void
onoe_update(struct onoe_softc *osc, struct ieee80211_node *ni, int nrate)
{
	struct onoe_data *od = ni->ni_rate_data;

	DPRINTF(osc, 1, "%s: set xmit rate for %6D to %dM\n", __func__,
		ni->ni_macaddr, ":",
		ni->ni_rates.rs_nrates > 0 ?
		IEEE80211_RS_RATE(&ni->ni_rates, nrate) / 2 : 0);

	ni->ni_txrate = nrate;

	if (od == NULL) {
		onoe_data_alloc(ni);
	} else {
		od->od_tx_ok = 0;
		od->od_tx_err = 0;
		od->od_tx_retr = 0;
		od->od_tx_upper = 0;
	}
}

/*
 * Set the starting transmit rate for a node.
 */
static void
onoe_start(struct onoe_softc *osc, struct ieee80211_node *ni)
{
#define	RATE(_ix)	IEEE80211_RS_RATE(&ni->ni_rates, (_ix))
	struct ieee80211com *ic = osc->ic;
	int srate;

	KASSERT(ni->ni_rates.rs_nrates > 0, ("no rates"));
	if (ic->ic_fixed_rate == IEEE80211_FIXED_RATE_NONE) {
		/*
		 * For adhoc or ibss mode, start from the lowest rate.
		 */
		if (ic->ic_opmode == IEEE80211_M_AHDEMO ||
		    ic->ic_opmode == IEEE80211_M_IBSS) {
			onoe_update(osc, ni, 0);
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
	onoe_update(osc, ni, srate);
#undef RATE
}

static void
onoe_rate_cb(void *arg, struct ieee80211_node *ni)
{
	onoe_update(arg, ni, 0);
}

static void
onoe_newstate(void *arg, enum ieee80211_state state)
{
	struct onoe_softc *osc = arg;
	struct ieee80211com *ic = osc->ic;
	struct ieee80211_node *ni;

	if (state == IEEE80211_S_INIT) {
		callout_stop(&osc->timer);
		return;
	}

	if (ic->ic_opmode == IEEE80211_M_STA) {
		/*
		 * Reset local xmit state; this is really only
		 * meaningful when operating in station mode.
		 */
		ni = ic->ic_bss;
		if (state == IEEE80211_S_RUN)
			onoe_start(osc, ni);
		else
			onoe_update(osc, ni, 0);
	} else {
		/*
		 * When operating as a station the node table holds
		 * the AP's that were discovered during scanning.
		 * For any other operating mode we want to reset the
		 * tx rate state of each node.
		 */
		ieee80211_iterate_nodes(&ic->ic_sta, onoe_rate_cb, osc);
		onoe_update(osc, ic->ic_bss, 0);
	}

	if (ic->ic_fixed_rate == IEEE80211_FIXED_RATE_NONE &&
	    state == IEEE80211_S_RUN) {
		int interval;

		/*
		 * Start the background rate control thread if we
		 * are not configured to use a fixed xmit rate.
		 */
		interval = osc->interval;
		if (ic->ic_opmode == IEEE80211_M_STA)
			interval /= 2;
		callout_reset(&osc->timer, (interval * hz) / 1000,
			      onoe_tick, osc);
	}
}

static void
onoe_ratectl(void *arg, struct ieee80211_node *ni)
{
	struct onoe_softc *osc = arg;
	struct onoe_data *od = ni->ni_rate_data;
	struct ieee80211_rateset *rs = &ni->ni_rates;
	int dir = 0, nrate, enough;

	if (od == NULL) {
		/* We are no ready to go, set TX rate to lowest one */
		ni->ni_txrate = 0;
		return;
	}

	/*
	 * Rate control
	 * XXX: very primitive version.
	 */
	enough = (od->od_tx_ok + od->od_tx_err >= 10);

	/* no packet reached -> down */
	if (od->od_tx_err > 0 && od->od_tx_ok == 0)
		dir = -1;

	/* all packets needs retry in average -> down */
	if (enough && od->od_tx_ok < od->od_tx_retr)
		dir = -1;

	/* no error and less than rate_raise% of packets need retry -> up */
	if (enough && od->od_tx_err == 0 &&
	    od->od_tx_retr < (od->od_tx_ok * osc->raise) / 100)
		dir = 1;

	DPRINTF(osc, 10, "%6D: ok %d err %d retr %d upper %d dir %d\n",
		ni->ni_macaddr, ":",
		od->od_tx_ok, od->od_tx_err, od->od_tx_retr,
		od->od_tx_upper, dir);

	nrate = ni->ni_txrate;
	switch (dir) {
	case 0:
		if (enough && od->od_tx_upper > 0)
			od->od_tx_upper--;
		break;
	case -1:
		if (nrate > 0)
			nrate--;
		od->od_tx_upper = 0;
		break;
	case 1:
		/* raise rate if we hit rate_raise_threshold */
		if (++od->od_tx_upper < osc->raise_threshold)
			break;
		od->od_tx_upper = 0;
		if (nrate + 1 < rs->rs_nrates)
			nrate++;
		break;
	}

	if (nrate != ni->ni_txrate) {
		DPRINTF(osc, 5, "%s: %dM -> %dM (%d ok, %d err, %d retr)\n",
			__func__,
			IEEE80211_RS_RATE(rs, ni->ni_txrate) / 2,
			IEEE80211_RS_RATE(rs, nrate) / 2,
			od->od_tx_ok, od->od_tx_err, od->od_tx_retr);
		onoe_update(osc, ni, nrate);
	} else if (enough) {
		od->od_tx_ok = od->od_tx_err = od->od_tx_retr = 0;
	}
}

static void
onoe_tick(void *arg)
{
	struct onoe_softc *osc = arg;
	struct ieee80211com *ic = osc->ic;
	struct ifnet *ifp = &ic->ic_if;
	int interval;

	lwkt_serialize_enter(ifp->if_serializer);

	if (ifp->if_flags & IFF_RUNNING) {
		if (ic->ic_opmode == IEEE80211_M_STA)
			onoe_ratectl(osc, ic->ic_bss);	/* NB: no reference */
		else
			ieee80211_iterate_nodes(&ic->ic_sta, onoe_ratectl, osc);
	}

	interval = osc->interval;
	if (ic->ic_opmode == IEEE80211_M_STA)
		interval /= 2;
	callout_reset(&osc->timer, (interval * hz) / 1000, onoe_tick, osc);

	lwkt_serialize_exit(ifp->if_serializer);
}

static void
onoe_sysctl_attach(struct onoe_softc *osc)
{
	osc->interval = 1000;
	osc->raise = 10;
	osc->raise_threshold = 10;
	osc->debug = 0;

	sysctl_ctx_init(&osc->sysctl_ctx);
	osc->sysctl_oid = SYSCTL_ADD_NODE(&osc->sysctl_ctx,
		SYSCTL_CHILDREN(osc->ic->ic_sysctl_oid),
		OID_AUTO, "onoe_ratectl", CTLFLAG_RD, 0, "");
	if (osc->sysctl_oid == NULL) {
		printf("wlan_ratectl_onoe: create sysctl tree failed\n");
		return;
	}

	SYSCTL_ADD_INT(&osc->sysctl_ctx, SYSCTL_CHILDREN(osc->sysctl_oid),
		       OID_AUTO, "interval", CTLFLAG_RW, &osc->interval, 0,
		       "rate control: operation interval (ms)");

	/* XXX bounds check values */
	SYSCTL_ADD_INT(&osc->sysctl_ctx, SYSCTL_CHILDREN(osc->sysctl_oid),
		       OID_AUTO, "raise", CTLFLAG_RW, &osc->raise, 0,
		       "rate control: "
		       "retry threshold to credit rate raise (%%)");

	SYSCTL_ADD_INT(&osc->sysctl_ctx, SYSCTL_CHILDREN(osc->sysctl_oid),
		       OID_AUTO, "raise_threshold", CTLFLAG_RW,
		       &osc->raise_threshold, 0,
		       "rate control: # good periods before raising rate");

	SYSCTL_ADD_INT(&osc->sysctl_ctx, SYSCTL_CHILDREN(osc->sysctl_oid),
		       OID_AUTO, "debug", CTLFLAG_RW, &osc->debug, 0,
		       "rate control: debug level");
}

static void *
onoe_attach(struct ieee80211com *ic)
{
	struct onoe_softc *osc;

	onoe_nrefs++;

	osc = kmalloc(sizeof(struct onoe_softc), M_DEVBUF, M_WAITOK | M_ZERO);

	osc->ic = ic;
	callout_init(&osc->timer);
	onoe_sysctl_attach(osc);

	onoe_newstate(osc, ic->ic_state);

	return osc;
}

static void
_onoe_data_free(void *arg __unused, struct ieee80211_node *ni)
{
	onoe_data_free(ni);
}

static void
onoe_detach(void *arg)
{
	struct onoe_softc *osc = arg;
	struct ieee80211com *ic = osc->ic;

	onoe_newstate(osc, IEEE80211_S_INIT);

	ieee80211_iterate_nodes(&ic->ic_sta, _onoe_data_free, NULL);
	ieee80211_iterate_nodes(&ic->ic_scan, _onoe_data_free, NULL);

	if (osc->sysctl_oid != NULL)
		sysctl_ctx_free(&osc->sysctl_ctx);
	kfree(osc, M_DEVBUF);

	onoe_nrefs--;
}

static void
onoe_data_free(struct ieee80211_node *ni)
{
	if (ni->ni_rate_data != NULL) {
		kfree(ni->ni_rate_data, M_ONOE_RATECTL_DATA);
		ni->ni_rate_data = NULL;
	}
}

static void
onoe_data_alloc(struct ieee80211_node *ni)
{
	KKASSERT(ni->ni_rate_data == NULL);
	ni->ni_rate_data = kmalloc(sizeof(struct onoe_data),
				  M_ONOE_RATECTL_DATA, M_NOWAIT | M_ZERO);
}

static void
onoe_data_dup(const struct ieee80211_node *oni, struct ieee80211_node *nni)
{
	if (oni->ni_rate_data == NULL || nni->ni_rate_data == NULL)
		return;

	bcopy(oni->ni_rate_data, nni->ni_rate_data, sizeof(struct onoe_data));
}

static int
onoe_modevent(module_t mod, int type, void *unused)
{
	switch (type) {
	case MOD_LOAD:
		ieee80211_ratectl_register(&onoe);
		return 0;
	case MOD_UNLOAD:
		if (onoe_nrefs) {
			printf("wlan_ratectl_onoe: still in use "
			       "(%u dynamic refs)\n", onoe_nrefs);
			return EBUSY;
		}
		ieee80211_ratectl_unregister(&onoe);
		return 0;
	}
	return EINVAL;
}

static moduledata_t onoe_mod = {
	"wlan_ratectl_onoe",
	onoe_modevent,
	0
};
DECLARE_MODULE(wlan_ratectl_onoe, onoe_mod, SI_SUB_DRIVERS, SI_ORDER_FIRST);
MODULE_VERSION(wlan_ratectl_onoe, 1);
MODULE_DEPEND(wlan_ratectl_onoe, wlan, 1, 1, 1);
