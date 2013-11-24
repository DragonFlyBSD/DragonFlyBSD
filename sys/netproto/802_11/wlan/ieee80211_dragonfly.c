/*-
 * Copyright (c) 2003-2009 Sam Leffler, Errno Consulting
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD: head/sys/net80211/ieee80211_freebsd.c 202612 2010-01-19 05:00:57Z thompsa $
 */

/*
 * IEEE 802.11 support (DragonFlyBSD-specific code)
 */
#include "opt_wlan.h"

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/systm.h> 
#include <sys/linker.h>
#include <sys/mbuf.h>   
#include <sys/module.h>
#include <sys/proc.h>
#include <sys/sysctl.h>

#include <sys/socket.h>

#include <net/bpf.h>
#include <net/if.h>
#include <net/if_dl.h>
#include <net/if_clone.h>
#include <net/if_media.h>
#include <net/if_types.h>
#include <net/ethernet.h>
#include <net/route.h>
#include <net/ifq_var.h>

#include <netproto/802_11/ieee80211_var.h>
#include <netproto/802_11/ieee80211_input.h>

SYSCTL_NODE(_net, OID_AUTO, wlan, CTLFLAG_RD, 0, "IEEE 80211 parameters");

#ifdef IEEE80211_DEBUG
int	ieee80211_debug = 0;
SYSCTL_INT(_net_wlan, OID_AUTO, debug, CTLFLAG_RW, &ieee80211_debug,
	    0, "debugging printfs");
#endif

int	ieee80211_force_swcrypto = 0;
SYSCTL_INT(_net_wlan, OID_AUTO, force_swcrypto, CTLFLAG_RW,
	    &ieee80211_force_swcrypto, 0, "force software crypto");

MALLOC_DEFINE(M_80211_COM, "80211com", "802.11 com state");


static int	wlan_clone_destroy(struct ifnet *);
static int	wlan_clone_create(struct if_clone *, int, caddr_t);

static struct if_clone wlan_cloner = 
	IF_CLONE_INITIALIZER("wlan", wlan_clone_create, wlan_clone_destroy,
	    0, IF_MAXUNIT);

struct lwkt_serialize wlan_global_serializer = LWKT_SERIALIZE_INITIALIZER;

/*
 * Allocate/free com structure in conjunction with ifnet;
 * these routines are registered with if_register_com_alloc
 * below and are called automatically by the ifnet code
 * when the ifnet of the parent device is created.
 */
static void *
wlan_alloc(u_char type, struct ifnet *ifp)
{
	struct ieee80211com *ic;

	ic = kmalloc(sizeof(struct ieee80211com), M_80211_COM, M_WAITOK|M_ZERO);
	ic->ic_ifp = ifp;

	return (ic);
}

static void
wlan_free(void *ic, u_char type)
{
	kfree(ic, M_80211_COM);
}

static int
wlan_clone_create(struct if_clone *ifc, int unit, caddr_t params)
{
	struct ieee80211_clone_params cp;
	struct ieee80211vap *vap;
	struct ieee80211com *ic;
	struct ifnet *ifp;
	int error;

	error = copyin(params, &cp, sizeof(cp));
	if (error)
		return error;
	ifp = ifunit(cp.icp_parent);
	if (ifp == NULL)
		return ENXIO;
	/* XXX move printfs to DIAGNOSTIC before release */
	if (ifp->if_type != IFT_IEEE80211) {
		if_printf(ifp, "%s: reject, not an 802.11 device\n", __func__);
		return ENXIO;
	}
	if (cp.icp_opmode >= IEEE80211_OPMODE_MAX) {
		if_printf(ifp, "%s: invalid opmode %d\n",
		    __func__, cp.icp_opmode);
		return EINVAL;
	}
	ic = ifp->if_l2com;
	if ((ic->ic_caps & ieee80211_opcap[cp.icp_opmode]) == 0) {
		if_printf(ifp, "%s mode not supported\n",
		    ieee80211_opmode_name[cp.icp_opmode]);
		return EOPNOTSUPP;
	}
	if ((cp.icp_flags & IEEE80211_CLONE_TDMA) &&
#ifdef IEEE80211_SUPPORT_TDMA
	    (ic->ic_caps & IEEE80211_C_TDMA) == 0
#else
	    (1)
#endif
	) {
		if_printf(ifp, "TDMA not supported\n");
		return EOPNOTSUPP;
	}
	vap = ic->ic_vap_create(ic, ifc->ifc_name, unit,
			cp.icp_opmode, cp.icp_flags, cp.icp_bssid,
			cp.icp_flags & IEEE80211_CLONE_MACADDR ?
			    cp.icp_macaddr : (const uint8_t *)IF_LLADDR(ifp));
	return (vap == NULL ? EIO : 0);
}

static int
wlan_clone_destroy(struct ifnet *ifp)
{
	struct ieee80211vap *vap = ifp->if_softc;
	struct ieee80211com *ic = vap->iv_ic;

	wlan_serialize_enter();	/* WARNING must be global serializer */
	ic->ic_vap_delete(vap);
	wlan_serialize_exit();

	return 0;
}

const char *wlan_last_enter_func;
const char *wlan_last_exit_func;
/*
 * These serializer functions are used by wlan and all drivers.
 */
void
_wlan_serialize_enter(const char *funcname)
{
	lwkt_serialize_enter(&wlan_global_serializer);
	wlan_last_enter_func = funcname;
}

void
_wlan_serialize_exit(const char *funcname)
{
	lwkt_serialize_exit(&wlan_global_serializer);
	wlan_last_exit_func = funcname;
}

int
wlan_serialize_sleep(void *ident, int flags, const char *wmesg, int timo)
{
	return(zsleep(ident, &wlan_global_serializer, flags, wmesg, timo));
}

/*
 * condition-var functions which interlock the ic lock (which is now
 * just wlan_global_serializer)
 */
void
wlan_cv_init(struct cv *cv, const char *desc)
{
	cv->cv_desc = desc;
	cv->cv_waiters = 0;
}

int
wlan_cv_timedwait(struct cv *cv, int ticks)
{
	int error;

	++cv->cv_waiters;
	error = wlan_serialize_sleep(cv, 0, cv->cv_desc, ticks);
	return (error);
}

void
wlan_cv_wait(struct cv *cv)
{
	++cv->cv_waiters;
	wlan_serialize_sleep(cv, 0, cv->cv_desc, 0);
}

void
wlan_cv_signal(struct cv *cv, int broadcast)
{
	if (cv->cv_waiters) {
		if (broadcast) {
			cv->cv_waiters = 0;
			wakeup(cv);
		} else {
			--cv->cv_waiters;
			wakeup_one(cv);
		}
	}
}

/*
 * Misc
 */
void
ieee80211_vap_destroy(struct ieee80211vap *vap)
{
	wlan_assert_serialized();
	wlan_serialize_exit();
	if_clone_destroy(vap->iv_ifp->if_xname);
	wlan_serialize_enter();
}

/*
 * NOTE: This handler is used generally to convert milliseconds
 *	 to ticks for various simple sysctl variables and does not
 *	 need to be serialized.
 */
int
ieee80211_sysctl_msecs_ticks(SYSCTL_HANDLER_ARGS)
{
	int msecs = ticks_to_msecs(*(int *)arg1);
	int error, t;

	error = sysctl_handle_int(oidp, &msecs, 0, req);
	if (error == 0 && req->newptr) {
		t = msecs_to_ticks(msecs);
		*(int *)arg1 = (t < 1) ? 1 : t;
	}

	return error;
}

static int
ieee80211_sysctl_inact(SYSCTL_HANDLER_ARGS)
{
	int inact = (*(int *)arg1) * IEEE80211_INACT_WAIT;
	int error;

	error = sysctl_handle_int(oidp, &inact, 0, req);
	wlan_serialize_enter();
	if (error == 0 && req->newptr)
		*(int *)arg1 = inact / IEEE80211_INACT_WAIT;
	wlan_serialize_exit();

	return error;
}

static int
ieee80211_sysctl_parent(SYSCTL_HANDLER_ARGS)
{
	struct ieee80211com *ic = arg1;
	const char *name = ic->ic_ifp->if_xname;

	return SYSCTL_OUT(req, name, strlen(name));
}

static int
ieee80211_sysctl_radar(SYSCTL_HANDLER_ARGS)
{
	struct ieee80211com *ic = arg1;
	int t = 0, error;

	error = sysctl_handle_int(oidp, &t, 0, req);
	wlan_serialize_enter();
	if (error == 0 && req->newptr)
		ieee80211_dfs_notify_radar(ic, ic->ic_curchan);
	wlan_serialize_exit();

	return error;
}

void
ieee80211_sysctl_attach(struct ieee80211com *ic)
{
}

void
ieee80211_sysctl_detach(struct ieee80211com *ic)
{
}

void
ieee80211_sysctl_vattach(struct ieee80211vap *vap)
{
	struct ifnet *ifp = vap->iv_ifp;
	struct sysctl_ctx_list *ctx;
	struct sysctl_oid *oid;
	char num[14];			/* sufficient for 32 bits */

	ctx = (struct sysctl_ctx_list *) kmalloc(sizeof(struct sysctl_ctx_list),
		M_DEVBUF, M_INTWAIT | M_ZERO);
	if (ctx == NULL) {
		if_printf(ifp, "%s: cannot allocate sysctl context!\n",
			__func__);
		return;
	}
	sysctl_ctx_init(ctx);
	ksnprintf(num, sizeof(num), "%u", ifp->if_dunit);
	oid = SYSCTL_ADD_NODE(ctx, &SYSCTL_NODE_CHILDREN(_net, wlan),
		OID_AUTO, num, CTLFLAG_RD, NULL, "");
	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(oid), OID_AUTO,
		"%parent", CTLFLAG_RD, vap->iv_ic, 0,
		ieee80211_sysctl_parent, "A", "parent device");
	SYSCTL_ADD_INT(ctx, SYSCTL_CHILDREN(oid), OID_AUTO,
		"driver_caps", CTLFLAG_RW, &vap->iv_caps, 0,
		"driver capabilities");
#ifdef IEEE80211_DEBUG
	vap->iv_debug = ieee80211_debug;
	SYSCTL_ADD_INT(ctx, SYSCTL_CHILDREN(oid), OID_AUTO,
		"debug", CTLFLAG_RW, &vap->iv_debug, 0,
		"control debugging printfs");
#endif
	SYSCTL_ADD_INT(ctx, SYSCTL_CHILDREN(oid), OID_AUTO,
		"bmiss_max", CTLFLAG_RW, &vap->iv_bmiss_max, 0,
		"consecutive beacon misses before scanning");
	/* XXX inherit from tunables */
	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(oid), OID_AUTO,
		"inact_run", CTLTYPE_INT | CTLFLAG_RW, &vap->iv_inact_run, 0,
		ieee80211_sysctl_inact, "I",
		"station inactivity timeout (sec)");
	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(oid), OID_AUTO,
		"inact_probe", CTLTYPE_INT | CTLFLAG_RW, &vap->iv_inact_probe, 0,
		ieee80211_sysctl_inact, "I",
		"station inactivity probe timeout (sec)");
	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(oid), OID_AUTO,
		"inact_auth", CTLTYPE_INT | CTLFLAG_RW, &vap->iv_inact_auth, 0,
		ieee80211_sysctl_inact, "I",
		"station authentication timeout (sec)");
	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(oid), OID_AUTO,
		"inact_init", CTLTYPE_INT | CTLFLAG_RW, &vap->iv_inact_init, 0,
		ieee80211_sysctl_inact, "I",
		"station initial state timeout (sec)");
	if (vap->iv_htcaps & IEEE80211_HTC_HT) {
		SYSCTL_ADD_INT(ctx, SYSCTL_CHILDREN(oid), OID_AUTO,
			"ampdu_mintraffic_bk", CTLFLAG_RW,
			&vap->iv_ampdu_mintraffic[WME_AC_BK], 0,
			"BK traffic tx aggr threshold (pps)");
		SYSCTL_ADD_INT(ctx, SYSCTL_CHILDREN(oid), OID_AUTO,
			"ampdu_mintraffic_be", CTLFLAG_RW,
			&vap->iv_ampdu_mintraffic[WME_AC_BE], 0,
			"BE traffic tx aggr threshold (pps)");
		SYSCTL_ADD_INT(ctx, SYSCTL_CHILDREN(oid), OID_AUTO,
			"ampdu_mintraffic_vo", CTLFLAG_RW,
			&vap->iv_ampdu_mintraffic[WME_AC_VO], 0,
			"VO traffic tx aggr threshold (pps)");
		SYSCTL_ADD_INT(ctx, SYSCTL_CHILDREN(oid), OID_AUTO,
			"ampdu_mintraffic_vi", CTLFLAG_RW,
			&vap->iv_ampdu_mintraffic[WME_AC_VI], 0,
			"VI traffic tx aggr threshold (pps)");
	}
	if (vap->iv_caps & IEEE80211_C_DFS) {
		SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(oid), OID_AUTO,
			"radar", CTLTYPE_INT | CTLFLAG_RW, vap->iv_ic, 0,
			ieee80211_sysctl_radar, "I", "simulate radar event");
	}
	vap->iv_sysctl = ctx;
	vap->iv_oid = oid;
}

void
ieee80211_sysctl_vdetach(struct ieee80211vap *vap)
{

	if (vap->iv_sysctl != NULL) {
		sysctl_ctx_free(vap->iv_sysctl);
		kfree(vap->iv_sysctl, M_DEVBUF);
		vap->iv_sysctl = NULL;
	}
}

int
ieee80211_node_dectestref(struct ieee80211_node *ni)
{
	/* XXX need equivalent of atomic_dec_and_test */
	atomic_subtract_int(&ni->ni_refcnt, 1);
	return atomic_cmpset_int(&ni->ni_refcnt, 0, 1);
}

/* XXX this breaks ALTQ's packet scheduler */
void
ieee80211_flush_ifq(struct ifaltq *ifq, struct ieee80211vap *vap)
{
	struct ieee80211_node *ni;
	struct mbuf *m, **mprev;
	struct ifaltq_subque *ifsq = ifq_get_subq_default(ifq);

	wlan_assert_serialized();

	ALTQ_SQ_LOCK(ifsq);

	/*
	 * Fix normal queue
	 */
	mprev = &ifsq->ifsq_norm_head;
	while ((m = *mprev) != NULL) {
		ni = (struct ieee80211_node *)m->m_pkthdr.rcvif;
		if (ni != NULL && ni->ni_vap == vap) {
			*mprev = m->m_nextpkt;		/* remove from list */
			ALTQ_SQ_CNTR_DEC(ifsq, m->m_pkthdr.len);

			m_freem(m);
			ieee80211_free_node(ni);	/* reclaim ref */
		} else
			mprev = &m->m_nextpkt;
	}
	/* recalculate tail ptr */
	m = ifsq->ifsq_norm_head;
	for (; m != NULL && m->m_nextpkt != NULL; m = m->m_nextpkt)
		;
	ifsq->ifsq_norm_tail = m;

	/*
	 * Fix priority queue
	 */
	mprev = &ifsq->ifsq_prio_head;
	while ((m = *mprev) != NULL) {
		ni = (struct ieee80211_node *)m->m_pkthdr.rcvif;
		if (ni != NULL && ni->ni_vap == vap) {
			*mprev = m->m_nextpkt;		/* remove from list */
			ALTQ_SQ_CNTR_DEC(ifsq, m->m_pkthdr.len);
			ALTQ_SQ_PRIO_CNTR_DEC(ifsq, m->m_pkthdr.len);

			m_freem(m);
			ieee80211_free_node(ni);	/* reclaim ref */
		} else
			mprev = &m->m_nextpkt;
	}
	/* recalculate tail ptr */
	m = ifsq->ifsq_prio_head;
	for (; m != NULL && m->m_nextpkt != NULL; m = m->m_nextpkt)
		;
	ifsq->ifsq_prio_tail = m;

	ALTQ_SQ_UNLOCK(ifsq);
}

/*
 * As above, for mbufs allocated with m_gethdr/MGETHDR
 * or initialized by M_COPY_PKTHDR.
 */
#define	MC_ALIGN(m, len)						\
do {									\
	(m)->m_data += (MCLBYTES - (len)) &~ (sizeof(long) - 1);	\
} while (/* CONSTCOND */ 0)

/*
 * Allocate and setup a management frame of the specified
 * size.  We return the mbuf and a pointer to the start
 * of the contiguous data area that's been reserved based
 * on the packet length.  The data area is forced to 32-bit
 * alignment and the buffer length to a multiple of 4 bytes.
 * This is done mainly so beacon frames (that require this)
 * can use this interface too.
 */
struct mbuf *
ieee80211_getmgtframe(uint8_t **frm, int headroom, int pktlen)
{
	struct mbuf *m;
	u_int len;

	/*
	 * NB: we know the mbuf routines will align the data area
	 *     so we don't need to do anything special.
	 */
	len = roundup2(headroom + pktlen, 4);
	KASSERT(len <= MCLBYTES, ("802.11 mgt frame too large: %u", len));
	if (len < MINCLSIZE) {
		m = m_gethdr(MB_DONTWAIT, MT_DATA);
		/*
		 * Align the data in case additional headers are added.
		 * This should only happen when a WEP header is added
		 * which only happens for shared key authentication mgt
		 * frames which all fit in MHLEN.
		 */
		if (m != NULL)
			MH_ALIGN(m, len);
	} else {
		m = m_getcl(MB_DONTWAIT, MT_DATA, M_PKTHDR);
		if (m != NULL)
			MC_ALIGN(m, len);
	}
	if (m != NULL) {
		m->m_data += headroom;
		*frm = m->m_data;
	}
	return m;
}

/*
 * Re-align the payload in the mbuf.  This is mainly used (right now)
 * to handle IP header alignment requirements on certain architectures.
 */
struct mbuf *
ieee80211_realign(struct ieee80211vap *vap, struct mbuf *m, size_t align)
{
	int pktlen, space;
	struct mbuf *n = NULL;

	pktlen = m->m_pkthdr.len;
	space = pktlen + align;
	if (space < MINCLSIZE)
		n = m_gethdr(MB_DONTWAIT, MT_DATA);
#ifdef notyet
	else {
		n = m_getjcl(MB_DONTWAIT, MT_DATA, M_PKTHDR,
		    space <= MCLBYTES ?     MCLBYTES :
#if MJUMPAGESIZE != MCLBYTES
		    space <= MJUMPAGESIZE ? MJUMPAGESIZE :
#endif
		    space <= MJUM9BYTES ?   MJUM9BYTES : MJUM16BYTES);
	}
#endif
	if (__predict_true(n != NULL)) {
		m_move_pkthdr(n, m);
		n->m_data = (caddr_t)(ALIGN(n->m_data + align) - align);
		m_copydata(m, 0, pktlen, mtod(n, caddr_t));
		n->m_len = pktlen;
	} else {
		IEEE80211_DISCARD(vap, IEEE80211_MSG_ANY,
		    mtod(m, const struct ieee80211_frame *), NULL,
		    "%s", "no mbuf to realign");
		vap->iv_stats.is_rx_badalign++;
	}
	m_freem(m);
	return n;
}

int
ieee80211_add_callback(struct mbuf *m,
	void (*func)(struct ieee80211_node *, void *, int), void *arg)
{
	struct m_tag *mtag;
	struct ieee80211_cb *cb;

	mtag = m_tag_alloc(MTAG_ABI_NET80211, NET80211_TAG_CALLBACK,
			sizeof(struct ieee80211_cb), M_INTWAIT);
	if (mtag == NULL)
		return 0;

	cb = (struct ieee80211_cb *)(mtag+1);
	cb->func = func;
	cb->arg = arg;
	m_tag_prepend(m, mtag);
	m->m_flags |= M_TXCB;
	return 1;
}

void
ieee80211_tx_complete(struct ieee80211_node *ni, struct mbuf *m, int status)
{
	if (ni != NULL) {
		if (m->m_flags & M_TXCB)
			ieee80211_process_callback(ni, m, status);
		ieee80211_free_node(ni);
	}
	m_freem(m);
}

void
ieee80211_process_callback(struct ieee80211_node *ni,
	struct mbuf *m, int status)
{
	struct m_tag *mtag;

	mtag = m_tag_locate(m, MTAG_ABI_NET80211, NET80211_TAG_CALLBACK, NULL);
	if (mtag != NULL) {
		struct ieee80211_cb *cb = (struct ieee80211_cb *)(mtag+1);
		cb->func(ni, cb->arg, status);
	}
}

#include <sys/libkern.h>

void
get_random_bytes(void *p, size_t n)
{
	uint8_t *dp = p;

	while (n > 0) {
		uint32_t v = karc4random();
		size_t nb = n > sizeof(uint32_t) ? sizeof(uint32_t) : n;
		bcopy(&v, dp, n > sizeof(uint32_t) ? sizeof(uint32_t) : n);
		dp += sizeof(uint32_t), n -= nb;
	}
}

/*
 * Helper function for events that pass just a single mac address.
 */
static void
notify_macaddr(struct ifnet *ifp, int op, const uint8_t mac[IEEE80211_ADDR_LEN])
{
	struct ieee80211_join_event iev;

	memset(&iev, 0, sizeof(iev));
	IEEE80211_ADDR_COPY(iev.iev_addr, mac);
	rt_ieee80211msg(ifp, op, &iev, sizeof(iev));
}

void
ieee80211_notify_node_join(struct ieee80211_node *ni, int newassoc)
{
	struct ieee80211vap *vap = ni->ni_vap;
	struct ifnet *ifp = vap->iv_ifp;

	IEEE80211_NOTE(vap, IEEE80211_MSG_NODE, ni, "%snode join",
	    (ni == vap->iv_bss) ? "bss " : "");

	if (ni == vap->iv_bss) {
		notify_macaddr(ifp, newassoc ?
		    RTM_IEEE80211_ASSOC : RTM_IEEE80211_REASSOC, ni->ni_bssid);
		if_link_state_change(ifp);
	} else {
		notify_macaddr(ifp, newassoc ?
		    RTM_IEEE80211_JOIN : RTM_IEEE80211_REJOIN, ni->ni_macaddr);
	}
}

void
ieee80211_notify_node_leave(struct ieee80211_node *ni)
{
	struct ieee80211vap *vap = ni->ni_vap;
	struct ifnet *ifp = vap->iv_ifp;

	IEEE80211_NOTE(vap, IEEE80211_MSG_NODE, ni, "%snode leave",
	    (ni == vap->iv_bss) ? "bss " : "");

	if (ni == vap->iv_bss) {
		rt_ieee80211msg(ifp, RTM_IEEE80211_DISASSOC, NULL, 0);
		if_link_state_change(ifp);
	} else {
		/* fire off wireless event station leaving */
		notify_macaddr(ifp, RTM_IEEE80211_LEAVE, ni->ni_macaddr);
	}
}

void
ieee80211_notify_scan_done(struct ieee80211vap *vap)
{
	struct ifnet *ifp = vap->iv_ifp;

	IEEE80211_DPRINTF(vap, IEEE80211_MSG_SCAN, "%s\n", "notify scan done");

	/* dispatch wireless event indicating scan completed */
	rt_ieee80211msg(ifp, RTM_IEEE80211_SCAN, NULL, 0);
}

void
ieee80211_notify_replay_failure(struct ieee80211vap *vap,
	const struct ieee80211_frame *wh, const struct ieee80211_key *k,
	u_int64_t rsc, int tid)
{
	struct ifnet *ifp = vap->iv_ifp;

	IEEE80211_NOTE_MAC(vap, IEEE80211_MSG_CRYPTO, wh->i_addr2,
	    "%s replay detected <rsc %ju, csc %ju, keyix %u rxkeyix %u>",
	    k->wk_cipher->ic_name, (intmax_t) rsc,
	    (intmax_t) k->wk_keyrsc[tid],
	    k->wk_keyix, k->wk_rxkeyix);

	if (ifp != NULL) {		/* NB: for cipher test modules */
		struct ieee80211_replay_event iev;

		IEEE80211_ADDR_COPY(iev.iev_dst, wh->i_addr1);
		IEEE80211_ADDR_COPY(iev.iev_src, wh->i_addr2);
		iev.iev_cipher = k->wk_cipher->ic_cipher;
		if (k->wk_rxkeyix != IEEE80211_KEYIX_NONE)
			iev.iev_keyix = k->wk_rxkeyix;
		else
			iev.iev_keyix = k->wk_keyix;
		iev.iev_keyrsc = k->wk_keyrsc[tid];
		iev.iev_rsc = rsc;
		rt_ieee80211msg(ifp, RTM_IEEE80211_REPLAY, &iev, sizeof(iev));
	}
}

void
ieee80211_notify_michael_failure(struct ieee80211vap *vap,
	const struct ieee80211_frame *wh, u_int keyix)
{
	struct ifnet *ifp = vap->iv_ifp;

	IEEE80211_NOTE_MAC(vap, IEEE80211_MSG_CRYPTO, wh->i_addr2,
	    "michael MIC verification failed <keyix %u>", keyix);
	vap->iv_stats.is_rx_tkipmic++;

	if (ifp != NULL) {		/* NB: for cipher test modules */
		struct ieee80211_michael_event iev;

		IEEE80211_ADDR_COPY(iev.iev_dst, wh->i_addr1);
		IEEE80211_ADDR_COPY(iev.iev_src, wh->i_addr2);
		iev.iev_cipher = IEEE80211_CIPHER_TKIP;
		iev.iev_keyix = keyix;
		rt_ieee80211msg(ifp, RTM_IEEE80211_MICHAEL, &iev, sizeof(iev));
	}
}

void
ieee80211_notify_wds_discover(struct ieee80211_node *ni)
{
	struct ieee80211vap *vap = ni->ni_vap;
	struct ifnet *ifp = vap->iv_ifp;

	notify_macaddr(ifp, RTM_IEEE80211_WDS, ni->ni_macaddr);
}

void
ieee80211_notify_csa(struct ieee80211com *ic,
	const struct ieee80211_channel *c, int mode, int count)
{
	struct ifnet *ifp = ic->ic_ifp;
	struct ieee80211_csa_event iev;

	memset(&iev, 0, sizeof(iev));
	iev.iev_flags = c->ic_flags;
	iev.iev_freq = c->ic_freq;
	iev.iev_ieee = c->ic_ieee;
	iev.iev_mode = mode;
	iev.iev_count = count;
	rt_ieee80211msg(ifp, RTM_IEEE80211_CSA, &iev, sizeof(iev));
}

void
ieee80211_notify_radar(struct ieee80211com *ic,
	const struct ieee80211_channel *c)
{
	struct ifnet *ifp = ic->ic_ifp;
	struct ieee80211_radar_event iev;

	memset(&iev, 0, sizeof(iev));
	iev.iev_flags = c->ic_flags;
	iev.iev_freq = c->ic_freq;
	iev.iev_ieee = c->ic_ieee;
	rt_ieee80211msg(ifp, RTM_IEEE80211_RADAR, &iev, sizeof(iev));
}

void
ieee80211_notify_cac(struct ieee80211com *ic,
	const struct ieee80211_channel *c, enum ieee80211_notify_cac_event type)
{
	struct ifnet *ifp = ic->ic_ifp;
	struct ieee80211_cac_event iev;

	memset(&iev, 0, sizeof(iev));
	iev.iev_flags = c->ic_flags;
	iev.iev_freq = c->ic_freq;
	iev.iev_ieee = c->ic_ieee;
	iev.iev_type = type;
	rt_ieee80211msg(ifp, RTM_IEEE80211_CAC, &iev, sizeof(iev));
}

void
ieee80211_notify_node_deauth(struct ieee80211_node *ni)
{
	struct ieee80211vap *vap = ni->ni_vap;
	struct ifnet *ifp = vap->iv_ifp;

	IEEE80211_NOTE(vap, IEEE80211_MSG_NODE, ni, "%s", "node deauth");

	notify_macaddr(ifp, RTM_IEEE80211_DEAUTH, ni->ni_macaddr);
}

void
ieee80211_notify_node_auth(struct ieee80211_node *ni)
{
	struct ieee80211vap *vap = ni->ni_vap;
	struct ifnet *ifp = vap->iv_ifp;

	IEEE80211_NOTE(vap, IEEE80211_MSG_NODE, ni, "%s", "node auth");

	notify_macaddr(ifp, RTM_IEEE80211_AUTH, ni->ni_macaddr);
}

void
ieee80211_notify_country(struct ieee80211vap *vap,
	const uint8_t bssid[IEEE80211_ADDR_LEN], const uint8_t cc[2])
{
	struct ifnet *ifp = vap->iv_ifp;
	struct ieee80211_country_event iev;

	memset(&iev, 0, sizeof(iev));
	IEEE80211_ADDR_COPY(iev.iev_addr, bssid);
	iev.iev_cc[0] = cc[0];
	iev.iev_cc[1] = cc[1];
	rt_ieee80211msg(ifp, RTM_IEEE80211_COUNTRY, &iev, sizeof(iev));
}

void
ieee80211_notify_radio(struct ieee80211com *ic, int state)
{
	struct ifnet *ifp = ic->ic_ifp;
	struct ieee80211_radio_event iev;

	memset(&iev, 0, sizeof(iev));
	iev.iev_state = state;
	rt_ieee80211msg(ifp, RTM_IEEE80211_RADIO, &iev, sizeof(iev));
}

int
ieee80211_handoff(struct ifnet *dst_ifp, struct mbuf *m)
{
        struct mbuf *m0;

	/* We may be sending a fragment so traverse the mbuf */
	wlan_assert_serialized();
	wlan_serialize_exit();
	for (; m; m = m0) {
		struct altq_pktattr pktattr;

		m0 = m->m_nextpkt;
		m->m_nextpkt = NULL;

		if (ifq_is_enabled(&dst_ifp->if_snd))
			altq_etherclassify(&dst_ifp->if_snd, m, &pktattr);

		ifq_dispatch(dst_ifp, m, &pktattr);
	}
	wlan_serialize_enter();

	return (0);
}

/* IEEE Std 802.11a-1999, page 9, table 79 */
#define IEEE80211_OFDM_SYM_TIME                 4
#define IEEE80211_OFDM_PREAMBLE_TIME            16
#define IEEE80211_OFDM_SIGNAL_TIME              4
/* IEEE Std 802.11g-2003, page 44 */
#define IEEE80211_OFDM_SIGNAL_EXT_TIME          6

/* IEEE Std 802.11a-1999, page 7, figure 107 */
#define IEEE80211_OFDM_PLCP_SERVICE_NBITS       16
#define IEEE80211_OFDM_TAIL_NBITS               6

#define IEEE80211_OFDM_NBITS(frmlen) \
	(IEEE80211_OFDM_PLCP_SERVICE_NBITS + \
	((frmlen) * NBBY) + \
	IEEE80211_OFDM_TAIL_NBITS)

#define IEEE80211_OFDM_NBITS_PER_SYM(kbps) \
	(((kbps) * IEEE80211_OFDM_SYM_TIME) / 1000)

#define IEEE80211_OFDM_NSYMS(kbps, frmlen) \
	howmany(IEEE80211_OFDM_NBITS((frmlen)), \
	IEEE80211_OFDM_NBITS_PER_SYM((kbps)))

#define IEEE80211_OFDM_TXTIME(kbps, frmlen) \
	(IEEE80211_OFDM_PREAMBLE_TIME + \
	IEEE80211_OFDM_SIGNAL_TIME + \
	(IEEE80211_OFDM_NSYMS((kbps), (frmlen)) * IEEE80211_OFDM_SYM_TIME))

/* IEEE Std 802.11b-1999, page 28, subclause 18.3.4 */
#define IEEE80211_CCK_PREAMBLE_LEN      144
#define IEEE80211_CCK_PLCP_HDR_TIME     48
#define IEEE80211_CCK_SHPREAMBLE_LEN    72
#define IEEE80211_CCK_SHPLCP_HDR_TIME   24

#define IEEE80211_CCK_NBITS(frmlen)     ((frmlen) * NBBY)
#define IEEE80211_CCK_TXTIME(kbps, frmlen) \
	(((IEEE80211_CCK_NBITS((frmlen)) * 1000) + (kbps) - 1) / (kbps))

uint16_t
ieee80211_txtime(struct ieee80211_node *ni, u_int len, uint8_t rs_rate,
		uint32_t flags)
{
	struct ieee80211vap *vap = ni->ni_vap;
	uint16_t txtime;
	int rate;

	rs_rate &= IEEE80211_RATE_VAL;
	rate = rs_rate * 500;   /* ieee80211 rate -> kbps */

	if (vap->iv_ic->ic_phytype == IEEE80211_T_OFDM) {
		/*
		 * IEEE Std 802.11a-1999, page 37, equation (29)
		 * IEEE Std 802.11g-2003, page 44, equation (42)
		 */
		txtime = IEEE80211_OFDM_TXTIME(rate, len);
		if (vap->iv_ic->ic_curmode == IEEE80211_MODE_11G)
			txtime += IEEE80211_OFDM_SIGNAL_EXT_TIME;
	} else {
		/*
		 * IEEE Std 802.11b-1999, page 28, subclause 18.3.4
		 * IEEE Std 802.11g-2003, page 45, equation (43)
		 */
		if (vap->iv_ic->ic_phytype == IEEE80211_T_OFDM_QUARTER+1)
			++len;
		txtime = IEEE80211_CCK_TXTIME(rate, len);

		/*
		 * Short preamble is not applicable for DS 1Mbits/s
		 */
		if (rs_rate != 2 && (flags & IEEE80211_F_SHPREAMBLE)) {
			txtime += IEEE80211_CCK_SHPREAMBLE_LEN +
				IEEE80211_CCK_SHPLCP_HDR_TIME;
		} else {
			txtime += IEEE80211_CCK_PREAMBLE_LEN +
			IEEE80211_CCK_PLCP_HDR_TIME;
		}
	}
	return txtime;
}

void
ieee80211_load_module(const char *modname)
{

#ifdef notyet
	(void)kern_kldload(curthread, modname, NULL);
#else
	kprintf("%s: load the %s module by hand for now.\n", __func__, modname);
#endif
}

static eventhandler_tag wlan_bpfevent;
static eventhandler_tag wlan_ifllevent;

static void
bpf_track_event(void *arg, struct ifnet *ifp, int dlt, int attach)
{
	/* NB: identify vap's by if_start */

	wlan_serialize_enter();
	if (dlt == DLT_IEEE802_11_RADIO && ifp->if_start == ieee80211_start) {
		struct ieee80211vap *vap = ifp->if_softc;
		/*
		 * Track bpf radiotap listener state.  We mark the vap
		 * to indicate if any listener is present and the com
		 * to indicate if any listener exists on any associated
		 * vap.  This flag is used by drivers to prepare radiotap
		 * state only when needed.
		 */
		if (attach) {
			ieee80211_syncflag_ext(vap, IEEE80211_FEXT_BPF);
			if (vap->iv_opmode == IEEE80211_M_MONITOR)
				atomic_add_int(&vap->iv_ic->ic_montaps, 1);
		} else if (!vap->iv_rawbpf) {
			ieee80211_syncflag_ext(vap, -IEEE80211_FEXT_BPF);
			if (vap->iv_opmode == IEEE80211_M_MONITOR)
				atomic_subtract_int(&vap->iv_ic->ic_montaps, 1);
		}
	}
	wlan_serialize_exit();
}

static void
wlan_iflladdr_event(void *arg __unused, struct ifnet *ifp)
{
	struct ieee80211com *ic = ifp->if_l2com;
	struct ieee80211vap *vap, *next;

	wlan_serialize_enter();
	if (ifp->if_type != IFT_IEEE80211 || ic == NULL) {
		wlan_serialize_exit();
		return;
	}

	TAILQ_FOREACH_MUTABLE(vap, &ic->ic_vaps, iv_next, next) {
		/*
		 * If the MAC address has changed on the parent and it was
		 * copied to the vap on creation then re-sync.
		 */
		if (vap->iv_ic == ic &&
		    (vap->iv_flags_ext & IEEE80211_FEXT_UNIQMAC) == 0) {
			IEEE80211_ADDR_COPY(vap->iv_myaddr, IF_LLADDR(ifp));
			wlan_serialize_exit();
			if_setlladdr(vap->iv_ifp, IF_LLADDR(ifp),
				     IEEE80211_ADDR_LEN);
			wlan_serialize_enter();
		}
	}
	wlan_serialize_exit();
}

/*
 * Module glue.
 *
 * NB: the module name is "wlan" for compatibility with NetBSD.
 */
static int
wlan_modevent(module_t mod, int type, void *unused)
{
	int error;

	wlan_serialize_enter();

	switch (type) {
	case MOD_LOAD:
		if (bootverbose)
			kprintf("wlan: <802.11 Link Layer>\n");
		wlan_bpfevent = EVENTHANDLER_REGISTER(bpf_track,
					bpf_track_event, 0,
					EVENTHANDLER_PRI_ANY);
		if (wlan_bpfevent == NULL) {
			error = ENOMEM;
			break;
		}
		wlan_ifllevent = EVENTHANDLER_REGISTER(iflladdr_event,
					wlan_iflladdr_event, NULL,
					EVENTHANDLER_PRI_ANY);
		if (wlan_ifllevent == NULL) {
			EVENTHANDLER_DEREGISTER(bpf_track, wlan_bpfevent);
			error = ENOMEM;
			break;
		}
		if_clone_attach(&wlan_cloner);
		if_register_com_alloc(IFT_IEEE80211, wlan_alloc, wlan_free);
		error = 0;
		break;
	case MOD_UNLOAD:
		if_deregister_com_alloc(IFT_IEEE80211);
		if_clone_detach(&wlan_cloner);
		EVENTHANDLER_DEREGISTER(bpf_track, wlan_bpfevent);
		EVENTHANDLER_DEREGISTER(iflladdr_event, wlan_ifllevent);
		error = 0;
		break;
	default:
		error = EINVAL;
		break;
	}
	wlan_serialize_exit();

	return error;
}

static moduledata_t wlan_mod = {
	"wlan",
	wlan_modevent,
	0
};
DECLARE_MODULE(wlan, wlan_mod, SI_SUB_DRIVERS, SI_ORDER_FIRST);
MODULE_VERSION(wlan, 1);
MODULE_DEPEND(wlan, ether, 1, 1, 1);
