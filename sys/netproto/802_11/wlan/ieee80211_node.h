/*
 * Copyright (c) 2001 Atsushi Onoe
 * Copyright (c) 2002, 2003 Sam Leffler, Errno Consulting
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
 * Alternatively, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") version 2 as published by the Free
 * Software Foundation.
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
 * $FreeBSD: src/sys/net80211/ieee80211_node.h,v 1.10 2004/04/05 22:10:26 sam Exp $
 * $DragonFly: src/sys/netproto/802_11/wlan/Attic/ieee80211_node.h,v 1.1 2004/07/26 16:30:17 joerg Exp $
 */

#ifndef _NETPROTO_802_11_IEEE80211_NODE_H_
#define	_NETPROTO_802_11_IEEE80211_NODE_H_

#define	IEEE80211_PSCAN_WAIT 	5		/* passive scan wait */
#define	IEEE80211_TRANS_WAIT 	5		/* transition wait */
#define	IEEE80211_INACT_WAIT	5		/* inactivity timer interval */
#define	IEEE80211_INACT_MAX	(300/IEEE80211_INACT_WAIT)

#define	IEEE80211_NODE_HASHSIZE	32
/* simple hash is enough for variation of macaddr */
#define	IEEE80211_NODE_HASH(addr)	\
	(((uint8_t *)(addr))[IEEE80211_ADDR_LEN - 1] % IEEE80211_NODE_HASHSIZE)

#define	IEEE80211_RATE_SIZE	8		/* 802.11 standard */
#define	IEEE80211_RATE_MAXSIZE	15		/* max rates we'll handle */

struct ieee80211_rateset {
	uint8_t			rs_nrates;
	uint8_t			rs_rates[IEEE80211_RATE_MAXSIZE];
};

/*
 * Node specific information.  Note that drivers are expected
 * to derive from this structure to add device-specific per-node
 * state.  This is done by overriding the ic_node_* methods in
 * the ieee80211com structure.
 */
struct ieee80211_node {
	TAILQ_ENTRY(ieee80211_node)	ni_list;
	LIST_ENTRY(ieee80211_node)	ni_hash;
	u_int			ni_refcnt;
	u_int			ni_scangen;	/* gen# for timeout scan */

	/* hardware */
	uint32_t		ni_rstamp;	/* recv timestamp */
	uint8_t			ni_rssi;	/* recv ssi */

	/* header */
	uint8_t			ni_macaddr[IEEE80211_ADDR_LEN];
	uint8_t			ni_bssid[IEEE80211_ADDR_LEN];

	/* beacon, probe response */
	uint8_t			ni_tstamp[8];	/* from last rcv'd beacon */
	uint16_t		ni_intval;	/* beacon interval */
	uint16_t		ni_capinfo;	/* capabilities */
	uint8_t			ni_esslen;
	uint8_t			ni_essid[IEEE80211_NWID_LEN];
	struct ieee80211_rateset ni_rates;	/* negotiated rate set */
	uint8_t			*ni_country;	/* country information XXX */
	struct ieee80211_channel *ni_chan;
	uint16_t		ni_fhdwell;	/* FH only */
	uint8_t			ni_fhindex;	/* FH only */
	uint8_t			ni_erp;		/* 11g only */

#ifdef notyet
	/* DTIM and contention free period (CFP) */
	uint8_t			ni_dtimperiod;
	uint8_t			ni_cfpperiod;	/* # of DTIMs between CFPs */
	uint16_t		ni_cfpduremain;	/* remaining cfp duration */
	uint16_t		ni_cfpmaxduration;/* max CFP duration in TU */
	uint16_t		ni_nextdtim;	/* time to next DTIM */
	uint16_t		ni_timoffset;
#endif

	/* others */
	uint16_t		ni_associd;	/* assoc response */
	uint16_t		ni_txseq;	/* seq to be transmitted */
	uint16_t		ni_rxseq;	/* seq previous received */
	int			ni_fails;	/* failure count to associate */
	int			ni_inact;	/* inactivity mark count */
	int			ni_txrate;	/* index to ni_rates[] */
};

static __inline struct ieee80211_node *
ieee80211_ref_node(struct ieee80211_node *ni)
{
	atomic_add_int(&ni->ni_refcnt, 1);
	return ni;
}

static __inline void
ieee80211_unref_node(struct ieee80211_node **ni)
{
	atomic_subtract_int(&(*ni)->ni_refcnt, 1);
	*ni = NULL;			/* guard against use */
}

struct ieee80211com;

#ifdef MALLOC_DECLARE
MALLOC_DECLARE(M_80211_NODE);
#endif

extern	void ieee80211_node_attach(struct ifnet *);
extern	void ieee80211_node_lateattach(struct ifnet *);
extern	void ieee80211_node_detach(struct ifnet *);

extern	void ieee80211_begin_scan(struct ifnet *);
extern	void ieee80211_next_scan(struct ifnet *);
extern	void ieee80211_end_scan(struct ifnet *);
extern	struct ieee80211_node *ieee80211_alloc_node(struct ieee80211com *,
		uint8_t *);
extern	struct ieee80211_node *ieee80211_dup_bss(struct ieee80211com *,
		uint8_t *);
extern	struct ieee80211_node *ieee80211_find_node(struct ieee80211com *,
		uint8_t *);
extern	struct ieee80211_node *ieee80211_find_txnode(struct ieee80211com *,
		uint8_t *);
extern	struct ieee80211_node * ieee80211_lookup_node(struct ieee80211com *,
		uint8_t *macaddr, struct ieee80211_channel *);
extern	void ieee80211_free_node(struct ieee80211com *,
		struct ieee80211_node *);
extern	void ieee80211_free_allnodes(struct ieee80211com *);
typedef void ieee80211_iter_func(void *, struct ieee80211_node *);
extern	void ieee80211_iterate_nodes(struct ieee80211com *ic,
		ieee80211_iter_func *, void *);
extern	void ieee80211_timeout_nodes(struct ieee80211com *);

extern	void ieee80211_create_ibss(struct ieee80211com* ,
		struct ieee80211_channel *);
#endif /* _NETPROTO_802_11_IEEE80211_NODE_H_ */
