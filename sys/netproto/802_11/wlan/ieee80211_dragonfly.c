/*
 * Copyright (c) 2003-2005 Sam Leffler, Errno Consulting
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
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
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
 * $FreeBSD: src/sys/net80211/ieee80211_freebsd.c,v 1.7.2.2 2005/12/22 19:22:51 sam Exp $
 * $DragonFly: src/sys/netproto/802_11/wlan/ieee80211_dragonfly.c,v 1.1 2006/05/18 13:51:46 sephe Exp $
 */

/*
 * IEEE 802.11 support (DragonFlyBSD-specific code)
 */
#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/systm.h> 
#include <sys/linker.h>
#include <sys/mbuf.h>   
#include <sys/module.h>
#include <sys/proc.h>
#include <sys/sysctl.h>

#include <sys/socket.h>

#include <net/if.h>
#include <net/if_arp.h>
#include <net/if_media.h>
#include <net/ethernet.h>
#include <net/route.h>

#include <netproto/802_11/ieee80211_var.h>

#define if_link_state_change(ifp, state)	((void)0)

SYSCTL_NODE(_net, OID_AUTO, wlan, CTLFLAG_RD, 0, "IEEE 80211 parameters");

#ifdef IEEE80211_DEBUG
int	ieee80211_debug = 0;
SYSCTL_INT(_net_wlan, OID_AUTO, debug, CTLFLAG_RW, &ieee80211_debug,
	    0, "debugging printfs");
#endif

static int
ieee80211_sysctl_inact(SYSCTL_HANDLER_ARGS)
{
	int inact = (*(int *)arg1) * IEEE80211_INACT_WAIT;
	int error;

	error = sysctl_handle_int(oidp, &inact, 0, req);
	if (error || !req->newptr)
		return error;
	*(int *)arg1 = inact / IEEE80211_INACT_WAIT;
	return 0;
}

static int
ieee80211_sysctl_parent(SYSCTL_HANDLER_ARGS)
{
	struct ieee80211com *ic = arg1;
	const char *name = ic->ic_ifp->if_xname;

	return SYSCTL_OUT(req, name, strlen(name));
}

void
ieee80211_sysctl_attach(struct ieee80211com *ic)
{
	struct sysctl_ctx_list *ctx;
	struct sysctl_oid *oid;
	char num[14];			/* sufficient for 32 bits */

	ctx = malloc(sizeof(struct sysctl_ctx_list), M_DEVBUF,
		     M_WAITOK | M_ZERO);
	sysctl_ctx_init(ctx);

	snprintf(num, sizeof(num), "%u", ic->ic_vap);
	oid = SYSCTL_ADD_NODE(ctx, &SYSCTL_NODE_CHILDREN(_net, wlan),
		OID_AUTO, num, CTLFLAG_RD, NULL, "");
	if (oid == NULL)
		return;

	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(oid), OID_AUTO,
		"%parent", CTLFLAG_RD, ic, 0, ieee80211_sysctl_parent, "A",
		"parent device");
#ifdef IEEE80211_DEBUG
	ic->ic_debug = ieee80211_debug;
	SYSCTL_ADD_INT(ctx, SYSCTL_CHILDREN(oid), OID_AUTO,
		"debug", CTLFLAG_RW, &ic->ic_debug, 0,
		"control debugging printfs");
#endif
	/* XXX inherit from tunables */
	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(oid), OID_AUTO,
		"inact_run", CTLTYPE_INT | CTLFLAG_RW, &ic->ic_inact_run, 0,
		ieee80211_sysctl_inact, "I",
		"station inactivity timeout (sec)");
	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(oid), OID_AUTO,
		"inact_probe", CTLTYPE_INT | CTLFLAG_RW, &ic->ic_inact_probe, 0,
		ieee80211_sysctl_inact, "I",
		"station inactivity probe timeout (sec)");
	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(oid), OID_AUTO,
		"inact_auth", CTLTYPE_INT | CTLFLAG_RW, &ic->ic_inact_auth, 0,
		ieee80211_sysctl_inact, "I",
		"station authentication timeout (sec)");
	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(oid), OID_AUTO,
		"inact_init", CTLTYPE_INT | CTLFLAG_RW, &ic->ic_inact_init, 0,
		ieee80211_sysctl_inact, "I",
		"station initial state timeout (sec)");
	SYSCTL_ADD_INT(ctx, SYSCTL_CHILDREN(oid), OID_AUTO,
		"driver_caps", CTLFLAG_RW, &ic->ic_caps, 0,
		"driver capabilities");
	SYSCTL_ADD_INT(ctx, SYSCTL_CHILDREN(oid), OID_AUTO,
		"bmiss_max", CTLFLAG_RW, &ic->ic_bmiss_max, 0,
		"consecutive beacon misses before scanning");

	ic->ic_sysctl = ctx;
}

void
ieee80211_sysctl_detach(struct ieee80211com *ic)
{
	if (ic->ic_sysctl != NULL) {
		sysctl_ctx_free(ic->ic_sysctl);
		free(ic->ic_sysctl, M_DEVBUF);
		ic->ic_sysctl = NULL;
	}
}

int
ieee80211_node_dectestref(struct ieee80211_node *ni)
{
	/* XXX need equivalent of atomic_dec_and_test */
	atomic_subtract_int(&ni->ni_refcnt, 1);
	return atomic_cmpset_int(&ni->ni_refcnt, 0, 1);
}

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
ieee80211_getmgtframe(uint8_t **frm, u_int pktlen)
{
	struct mbuf *m;
	u_int len;

	/*
	 * NB: we know the mbuf routines will align the data area
	 *     so we don't need to do anything special.
	 */
	/* XXX 4-address frame? */
	len = roundup(sizeof(struct ieee80211_frame) + pktlen, 4);
	KASSERT(len <= MCLBYTES, ("802.11 mgt frame too large: %u", len));
	if (len < MINCLSIZE) {
		m = m_gethdr(M_NOWAIT, MT_HEADER);
		/*
		 * Align the data in case additional headers are added.
		 * This should only happen when a WEP header is added
		 * which only happens for shared key authentication mgt
		 * frames which all fit in MHLEN.
		 */
		if (m != NULL)
			MH_ALIGN(m, len);
	} else
		m = m_getcl(M_NOWAIT, MT_HEADER, M_PKTHDR);
	if (m != NULL) {
		m->m_data += sizeof(struct ieee80211_frame);
		*frm = m->m_data;
	}
	return m;
}

#include <sys/libkern.h>

void
get_random_bytes(void *p, size_t n)
{
	uint8_t *dp = p;

	while (n > 0) {
		uint32_t v = arc4random();
		size_t nb = n > sizeof(uint32_t) ? sizeof(uint32_t) : n;

		bcopy(&v, dp, n > sizeof(uint32_t) ? sizeof(uint32_t) : n);
		dp += sizeof(uint32_t), n -= nb;
	}
}

void
ieee80211_notify_node_join(struct ieee80211com *ic, struct ieee80211_node *ni,
			   int newassoc)
{
	struct ifnet *ifp = ic->ic_ifp;
	struct ieee80211_join_event iev;

	memset(&iev, 0, sizeof(iev));
	if (ni == ic->ic_bss) {
		IEEE80211_ADDR_COPY(iev.iev_addr, ni->ni_bssid);
		rt_ieee80211msg(ifp, newassoc ?
			RTM_IEEE80211_ASSOC : RTM_IEEE80211_REASSOC,
			&iev, sizeof(iev));
		if_link_state_change(ifp, LINK_STATE_UP);
	} else {
		IEEE80211_ADDR_COPY(iev.iev_addr, ni->ni_macaddr);
		rt_ieee80211msg(ifp, newassoc ?
			RTM_IEEE80211_JOIN : RTM_IEEE80211_REJOIN,
			&iev, sizeof(iev));
	}
}

void
ieee80211_notify_node_leave(struct ieee80211com *ic, struct ieee80211_node *ni)
{
	struct ifnet *ifp = ic->ic_ifp;
	struct ieee80211_leave_event iev;

	if (ni == ic->ic_bss) {
		rt_ieee80211msg(ifp, RTM_IEEE80211_DISASSOC, NULL, 0);
		if_link_state_change(ifp, LINK_STATE_DOWN);
	} else {
		/* fire off wireless event station leaving */
		memset(&iev, 0, sizeof(iev));
		IEEE80211_ADDR_COPY(iev.iev_addr, ni->ni_macaddr);
		rt_ieee80211msg(ifp, RTM_IEEE80211_LEAVE, &iev, sizeof(iev));
	}
}

void
ieee80211_notify_scan_done(struct ieee80211com *ic)
{
	struct ifnet *ifp = ic->ic_ifp;

	IEEE80211_DPRINTF(ic, IEEE80211_MSG_SCAN,
		"%s: notify scan done\n", ic->ic_ifp->if_xname);

	/* dispatch wireless event indicating scan completed */
	rt_ieee80211msg(ifp, RTM_IEEE80211_SCAN, NULL, 0);
}

void
ieee80211_notify_replay_failure(struct ieee80211com *ic,
	const struct ieee80211_frame *wh, const struct ieee80211_key *k,
	uint64_t rsc)
{
	struct ifnet *ifp = ic->ic_ifp;

	IEEE80211_DPRINTF(ic, IEEE80211_MSG_CRYPTO,
	    "[%6D] %s replay detected <rsc %ju, csc %ju, keyix %u rxkeyix %u>\n",
	    wh->i_addr2, ":", k->wk_cipher->ic_name,
	    (intmax_t) rsc, (intmax_t) k->wk_keyrsc,
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
		iev.iev_keyrsc = k->wk_keyrsc;
		iev.iev_rsc = rsc;
		rt_ieee80211msg(ifp, RTM_IEEE80211_REPLAY, &iev, sizeof(iev));
	}
}

void
ieee80211_notify_michael_failure(struct ieee80211com *ic,
	const struct ieee80211_frame *wh, u_int keyix)
{
	struct ifnet *ifp = ic->ic_ifp;

	IEEE80211_DPRINTF(ic, IEEE80211_MSG_CRYPTO,
		"[%6D] michael MIC verification failed <keyix %u>\n",
	       wh->i_addr2, ":", keyix);
	ic->ic_stats.is_rx_tkipmic++;

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
ieee80211_load_module(const char *modname)
{
#ifdef notyet
	struct thread *td = curthread;

	if (suser(td) == 0 && securelevel_gt(td->td_ucred, 0) == 0) {
		crit_enter();	/* NB: need BGL here */
		linker_load_module(modname, NULL, NULL, NULL, NULL);
		crit_exit();
	}
#else
	printf("%s: load the %s module by hand for now.\n", __func__, modname);
#endif
}

/*
 * Append the specified data to the indicated mbuf chain,
 * Extend the mbuf chain if the new data does not fit in
 * existing space.
 *
 * Return 1 if able to complete the job; otherwise 0.
 */
int
ieee80211_mbuf_append(struct mbuf *m0, int len, const uint8_t *cp)
{
	struct mbuf *m, *n;
	int remainder, space;

	for (m = m0; m->m_next != NULL; m = m->m_next)
		;
	remainder = len;
	space = M_TRAILINGSPACE(m);
	if (space > 0) {
		/*
		 * Copy into available space.
		 */
		if (space > remainder)
			space = remainder;
		bcopy(cp, mtod(m, caddr_t) + m->m_len, space);
		m->m_len += space;
		cp += space, remainder -= space;
	}
	while (remainder > 0) {
		/*
		 * Allocate a new mbuf; could check space
		 * and allocate a cluster instead.
		 */
		n = m_get(MB_DONTWAIT, m->m_type);
		if (n == NULL)
			break;
		n->m_len = min(MLEN, remainder);
		bcopy(cp, mtod(n, caddr_t), n->m_len);
		cp += n->m_len, remainder -= n->m_len;
		m->m_next = n;
		m = n;
	}
	if (m0->m_flags & M_PKTHDR)
		m0->m_pkthdr.len += len - remainder;
	return (remainder == 0);
}

/*
 * Create a writable copy of the mbuf chain.  While doing this
 * we compact the chain with a goal of producing a chain with
 * at most two mbufs.  The second mbuf in this chain is likely
 * to be a cluster.  The primary purpose of this work is to create
 * a writable packet for encryption, compression, etc.  The
 * secondary goal is to linearize the data so the data can be
 * passed to crypto hardware in the most efficient manner possible.
 */
struct mbuf *
ieee80211_mbuf_clone(struct mbuf *m0, int how)
{
	struct mbuf *m, *mprev;
	struct mbuf *n, *mfirst, *mlast;
	int len, off;

	mprev = NULL;
	for (m = m0; m != NULL; m = mprev->m_next) {
		/*
		 * Regular mbufs are ignored unless there's a cluster
		 * in front of it that we can use to coalesce.  We do
		 * the latter mainly so later clusters can be coalesced
		 * also w/o having to handle them specially (i.e. convert
		 * mbuf+cluster -> cluster).  This optimization is heavily
		 * influenced by the assumption that we're running over
		 * Ethernet where MCLBYTES is large enough that the max
		 * packet size will permit lots of coalescing into a
		 * single cluster.  This in turn permits efficient
		 * crypto operations, especially when using hardware.
		 */
		if ((m->m_flags & M_EXT) == 0) {
			if (mprev && (mprev->m_flags & M_EXT) &&
			    m->m_len <= M_TRAILINGSPACE(mprev)) {
				/* XXX: this ignores mbuf types */
				memcpy(mtod(mprev, caddr_t) + mprev->m_len,
				       mtod(m, caddr_t), m->m_len);
				mprev->m_len += m->m_len;
				mprev->m_next = m->m_next;	/* unlink from chain */
				m_free(m);			/* reclaim mbuf */
			} else {
				mprev = m;
			}
			continue;
		}
		/*
		 * Writable mbufs are left alone (for now).
		 */
		if (M_WRITABLE(m)) {
			mprev = m;
			continue;
		}

		/*
		 * Not writable, replace with a copy or coalesce with
		 * the previous mbuf if possible (since we have to copy
		 * it anyway, we try to reduce the number of mbufs and
		 * clusters so that future work is easier).
		 */
		KASSERT(m->m_flags & M_EXT, ("m_flags 0x%x", m->m_flags));
		/* NB: we only coalesce into a cluster or larger */
		if (mprev != NULL && (mprev->m_flags & M_EXT) &&
		    m->m_len <= M_TRAILINGSPACE(mprev)) {
			/* XXX: this ignores mbuf types */
			memcpy(mtod(mprev, caddr_t) + mprev->m_len,
			       mtod(m, caddr_t), m->m_len);
			mprev->m_len += m->m_len;
			mprev->m_next = m->m_next;	/* unlink from chain */
			m_free(m);			/* reclaim mbuf */
			continue;
		}

		/*
		 * Allocate new space to hold the copy...
		 */
		/* XXX why can M_PKTHDR be set past the first mbuf? */
		if (mprev == NULL && (m->m_flags & M_PKTHDR)) {
			/*
			 * NB: if a packet header is present we must
			 * allocate the mbuf separately from any cluster
			 * because M_MOVE_PKTHDR will smash the data
			 * pointer and drop the M_EXT marker.
			 */
			MGETHDR(n, how, m->m_type);
			if (n == NULL) {
				m_freem(m0);
				return (NULL);
			}
			M_MOVE_PKTHDR(n, m);
			MCLGET(n, how);
			if ((n->m_flags & M_EXT) == 0) {
				m_free(n);
				m_freem(m0);
				return (NULL);
			}
		} else {
			n = m_getcl(how, m->m_type, m->m_flags);
			if (n == NULL) {
				m_freem(m0);
				return (NULL);
			}
		}
		/*
		 * ... and copy the data.  We deal with jumbo mbufs
		 * (i.e. m_len > MCLBYTES) by splitting them into
		 * clusters.  We could just malloc a buffer and make
		 * it external but too many device drivers don't know
		 * how to break up the non-contiguous memory when
		 * doing DMA.
		 */
		len = m->m_len;
		off = 0;
		mfirst = n;
		mlast = NULL;
		for (;;) {
			int cc = min(len, MCLBYTES);
			memcpy(mtod(n, caddr_t), mtod(m, caddr_t) + off, cc);
			n->m_len = cc;
			if (mlast != NULL)
				mlast->m_next = n;
			mlast = n;	

			len -= cc;
			if (len <= 0)
				break;
			off += cc;

			n = m_getcl(how, m->m_type, m->m_flags);
			if (n == NULL) {
				m_freem(mfirst);
				m_freem(m0);
				return (NULL);
			}
		}
		n->m_next = m->m_next; 
		if (mprev == NULL)
			m0 = mfirst;		/* new head of chain */
		else
			mprev->m_next = mfirst;	/* replace old mbuf */
		m_free(m);			/* release old mbuf */
		mprev = mfirst;
	}
	return (m0);
}

/*
 * Module glue.
 *
 * NB: the module name is "wlan" for compatibility with NetBSD.
 */
static int
wlan_modevent(module_t mod, int type, void *unused)
{
	switch (type) {
	case MOD_LOAD:
		if (bootverbose)
			printf("wlan: <802.11 Link Layer>\n");
		return 0;
	case MOD_UNLOAD:
		return 0;
	}
	return EINVAL;
}

static moduledata_t wlan_mod = {
	"wlan",
	wlan_modevent,
	0
};
DECLARE_MODULE(wlan, wlan_mod, SI_SUB_DRIVERS, SI_ORDER_FIRST);
MODULE_VERSION(wlan, 1);
MODULE_DEPEND(wlan, crypto, 1, 1, 1);
