/*
 * Copyright 2001 Wasabi Systems, Inc.
 * All rights reserved.
 *
 * Written by Jason R. Thorpe for Wasabi Systems, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed for the NetBSD Project by
 *	Wasabi Systems, Inc.
 * 4. The name of Wasabi Systems, Inc. may not be used to endorse
 *    or promote products derived from this software without specific prior
 *    written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY WASABI SYSTEMS, INC. ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL WASABI SYSTEMS, INC
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Copyright (c) 1999, 2000 Jason L. Wright (jason@thought.net)
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by Jason L. Wright
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * $OpenBSD: if_bridge.h,v 1.14 2001/03/22 03:48:29 jason Exp $
 * $NetBSD: if_bridgevar.h,v 1.4 2003/07/08 07:13:50 itojun Exp $
 * $FreeBSD: src/sys/net/if_bridgevar.h,v 1.4 2005/07/06 01:24:45 thompsa Exp $
 * $DragonFly: src/sys/net/bridge/if_bridgevar.h,v 1.10 2008/11/26 12:49:43 sephe Exp $
 */

#ifndef _NET_IF_BRIDGEVAR_H
#define _NET_IF_BRIDGEVAR_H

/*
 * Data structure and control definitions for bridge interfaces.
 */

#include <sys/callout.h>
#include <sys/queue.h>

/*
 * Commands used in the SIOCSDRVSPEC ioctl.  Note the lookup of the
 * bridge interface itself is keyed off the ifdrv structure.
 */
#define	BRDGADD			0	/* add bridge member (ifbreq) */
#define	BRDGDEL			1	/* delete bridge member (ifbreq) */
#define	BRDGGIFFLGS		2	/* get member if flags (ifbreq) */
#define	BRDGSIFFLGS		3	/* set member if flags (ifbreq) */
#define	BRDGSCACHE		4	/* set cache size (ifbrparam) */
#define	BRDGGCACHE		5	/* get cache size (ifbrparam) */
#define	BRDGGIFS		6	/* get member list (ifbifconf) */
#define	BRDGRTS			7	/* get address list (ifbaconf) */
#define	BRDGSADDR		8	/* set static address (ifbareq) */
#define	BRDGSTO			9	/* set cache timeout (ifbrparam) */
#define	BRDGGTO			10	/* get cache timeout (ifbrparam) */
#define	BRDGDADDR		11	/* delete address (ifbareq) */
#define	BRDGFLUSH		12	/* flush address cache (ifbreq) */

#define	BRDGGPRI		13	/* get priority (ifbrparam) */
#define	BRDGSPRI		14	/* set priority (ifbrparam) */
#define	BRDGGHT			15	/* get hello time (ifbrparam) */
#define	BRDGSHT			16	/* set hello time (ifbrparam) */
#define	BRDGGFD			17	/* get forward delay (ifbrparam) */
#define	BRDGSFD			18	/* set forward delay (ifbrparam) */
#define	BRDGGMA			19	/* get max age (ifbrparam) */
#define	BRDGSMA			20	/* set max age (ifbrparam) */
#define	BRDGSIFPRIO		21	/* set if priority (ifbreq) */
#define	BRDGSIFCOST		22	/* set if path cost (ifbreq) */
#define	BRDGADDS		23	/* add bridge span member (ifbreq) */
#define	BRDGDELS		24	/* delete bridge span member (ifbreq) */
#define BRDGSBONDWGHT		25	/* set bonding weighting (ifbreq) */

/*
 * Generic bridge control request.
 */
struct ifbreq {
	char		ifbr_ifsname[IFNAMSIZ];	/* member if name */
	uint32_t	ifbr_ifsflags;		/* member if flags */
	uint8_t		ifbr_state;		/* member if STP state */
	uint8_t		ifbr_priority;		/* member if STP priority */
	uint8_t		ifbr_path_cost;		/* member if STP cost */
	uint8_t		ifbr_portno;		/* member if port number */
	uint64_t	ifbr_designated_root;	/* synthesized */
	uint64_t	ifbr_designated_bridge;
	uint32_t	ifbr_designated_cost;
	uint16_t	ifbr_designated_port;
	uint16_t	unused01;
	uint64_t	ifbr_peer_root;		/* from peer */
	uint64_t	ifbr_peer_bridge;	/* from peer */
	uint32_t	ifbr_peer_cost;		/* from peer */
	uint16_t	ifbr_peer_port;		/* from peer */
	uint16_t	unused02;
	uint16_t	ifbr_bond_weight;
	uint16_t	unused03;
	uint32_t	unused04[8];
};

/* BRDGGIFFLAGS, BRDGSIFFLAGS */
#define	IFBIF_LEARNING		0x01	/* if can learn */
#define	IFBIF_DISCOVER		0x02	/* if sends packets w/ unknown dest. */
#define	IFBIF_STP		0x04	/* if participates in spanning tree */
#define	IFBIF_SPAN		0x08	/* if is a span port */
#define	IFBIF_DESIGNATED	0x10	/* mostly age timer expired */
#define	IFBIF_ROOT		0x20	/* selected root or near-root */

#define IFBIF_KEEPMASK		(IFBIF_SPAN | IFBIF_DESIGNATED | IFBIF_ROOT)

#define	IFBIFBITS	"\020\1LEARNING\2DISCOVER\3STP\4SPAN\5DESIGNATED" \
			"\6ROOT"

/* BRDGFLUSH */
#define	IFBF_FLUSHDYN		0x00	/* flush learned addresses only */
#define	IFBF_FLUSHALL		0x01	/* flush all addresses */
#define IFBF_FLUSHSYNC		0x02	/* synchronized flush */

/* STP port states */
#define	BSTP_IFSTATE_DISABLED	0
#define	BSTP_IFSTATE_LISTENING	1
#define	BSTP_IFSTATE_LEARNING	2
#define	BSTP_IFSTATE_FORWARDING	3
#define	BSTP_IFSTATE_BLOCKING	4
#define	BSTP_IFSTATE_BONDED	5	/* link2 bonding mode */
#define	BSTP_IFSTATE_L1BLOCKING	6	/* link1 blocking mode no-activity */

/*
 * Interface list structure.
 */
struct ifbifconf {
	uint32_t	ifbic_len;	/* buffer size */
	union {
		caddr_t	ifbicu_buf;
		struct ifbreq *ifbicu_req;
	} ifbic_ifbicu;
#define	ifbic_buf	ifbic_ifbicu.ifbicu_buf
#define	ifbic_req	ifbic_ifbicu.ifbicu_req
};

/*
 * Bridge address request.
 */
struct ifbareq {
	char		ifba_ifsname[IFNAMSIZ];	/* member if name */
	unsigned long	ifba_expire;		/* address expire time */
	uint8_t		ifba_flags;		/* address flags */
	uint8_t		ifba_dst[ETHER_ADDR_LEN];/* destination address */
};

#define	IFBAF_TYPEMASK	0x03	/* address type mask */
#define	IFBAF_DYNAMIC	0x00	/* dynamically learned address */
#define	IFBAF_STATIC	0x01	/* static address */

#define	IFBAFBITS	"\020\1STATIC"

/*
 * Address list structure.
 */
struct ifbaconf {
	uint32_t	ifbac_len;	/* buffer size */
	union {
		caddr_t ifbacu_buf;
		struct ifbareq *ifbacu_req;
	} ifbac_ifbacu;
#define	ifbac_buf	ifbac_ifbacu.ifbacu_buf
#define	ifbac_req	ifbac_ifbacu.ifbacu_req
};

/*
 * Bridge parameter structure.
 */
struct ifbrparam {
	union {
		uint32_t ifbrpu_int32;
		uint16_t ifbrpu_int16;
		uint8_t ifbrpu_int8;
	} ifbrp_ifbrpu;
};
#define	ifbrp_csize	ifbrp_ifbrpu.ifbrpu_int32	/* cache size */
#define	ifbrp_ctime	ifbrp_ifbrpu.ifbrpu_int32	/* cache time (sec) */
#define	ifbrp_prio	ifbrp_ifbrpu.ifbrpu_int16	/* bridge priority */
#define	ifbrp_hellotime	ifbrp_ifbrpu.ifbrpu_int8	/* hello time (sec) */
#define	ifbrp_fwddelay	ifbrp_ifbrpu.ifbrpu_int8	/* fwd time (sec) */
#define	ifbrp_maxage	ifbrp_ifbrpu.ifbrpu_int8	/* max age (sec) */

#ifdef _KERNEL
/*
 * Timekeeping structure used in spanning tree code.
 */
struct bridge_timer {
	uint16_t	active;
	uint16_t	value;
};

struct bstp_config_unit {
	uint64_t	cu_rootid;
	uint64_t	cu_bridge_id;
	uint32_t	cu_root_path_cost;
	uint16_t	cu_message_age;
	uint16_t	cu_max_age;
	uint16_t	cu_hello_time;
	uint16_t	cu_forward_delay;
	uint16_t	cu_port_id;
	uint8_t		cu_message_type;
	uint8_t		cu_topology_change_acknowledgment;
	uint8_t		cu_topology_change;
};

struct bstp_tcn_unit {
	uint8_t		tu_message_type;
};

/*
 * Bridge interface entry.
 */
struct bridge_ifinfo {
	uint64_t		bifi_peer_root;
	uint64_t		bifi_peer_bridge;
	uint32_t		bifi_peer_cost;
	uint16_t		bifi_peer_port;
	uint16_t		bifi_unused02;
	uint16_t		bifi_port_id;
	uint32_t		bifi_path_cost;
	struct bridge_timer	bifi_hold_timer;
	struct bridge_timer	bifi_message_age_timer;
	struct bridge_timer	bifi_forward_delay_timer;
	struct bridge_timer	bifi_link1_timer;
	struct bstp_config_unit	bifi_config_bpdu;
	uint32_t		bifi_flags;	/* member if flags */
	uint8_t			bifi_state;
	uint8_t			bifi_topology_change_acknowledge;
	uint8_t			bifi_config_pending;
	uint8_t			bifi_change_detection_enabled;
	uint8_t			bifi_priority;
	struct ifnet		*bifi_ifp;	/* member if */
	int			bifi_mutecap;	/* member muted caps */
	int			bifi_bond_weight; /* when link2 active */
};

#define bif_peer_root			bif_info->bifi_peer_root
#define bif_peer_bridge			bif_info->bifi_peer_bridge
#define bif_peer_cost			bif_info->bifi_peer_cost
#define bif_peer_port			bif_info->bifi_peer_port
#define bif_path_cost			bif_info->bifi_path_cost
#define bif_hold_timer			bif_info->bifi_hold_timer
#define bif_message_age_timer		bif_info->bifi_message_age_timer
#define bif_forward_delay_timer		bif_info->bifi_forward_delay_timer
#define bif_link1_timer			bif_info->bifi_link1_timer
#define bif_config_bpdu			bif_info->bifi_config_bpdu
#define bif_port_id			bif_info->bifi_port_id
#define bif_state			bif_info->bifi_state
#define bif_flags			bif_info->bifi_flags
#define bif_topology_change_acknowledge	\
	bif_info->bifi_topology_change_acknowledge
#define bif_config_pending		bif_info->bifi_config_pending
#define bif_change_detection_enabled	bif_info->bifi_change_detection_enabled
#define bif_priority			bif_info->bifi_priority
#define bif_message_age_timer		bif_info->bifi_message_age_timer
#define bif_bond_weight			bif_info->bifi_bond_weight

/*
 * Bridge interface list entry.
 */
struct bridge_iflist {
	TAILQ_ENTRY(bridge_iflist) bif_next;
	struct ifnet		*bif_ifp;	/* member if */
	int			bif_onlist;
	struct bridge_ifinfo	*bif_info;
	int			bif_bond_count;	/* when link2 active */
};
TAILQ_HEAD(bridge_iflist_head, bridge_iflist);

/*
 * Bridge route info.
 */
struct bridge_rtinfo {
	struct ifnet		*bri_ifp;	/* destination if */
	unsigned long		bri_expire;	/* expiration time */
	uint8_t			bri_flags;	/* address flags */
	uint8_t			bri_dead;
	uint8_t			bri_pad[2];
};

/*
 * Bridge route node.
 */
struct bridge_rtnode {
	LIST_ENTRY(bridge_rtnode) brt_hash;	/* hash table linkage */
	LIST_ENTRY(bridge_rtnode) brt_list;	/* list linkage */
	uint8_t			brt_addr[ETHER_ADDR_LEN];
	uint8_t			brt_pad[2];
	struct bridge_rtinfo	*brt_info;
};
LIST_HEAD(bridge_rtnode_head, bridge_rtnode);

/*
 * Software state for each bridge.
 */
struct bridge_softc {
	struct arpcom           sc_arp;
	struct ifnet		*sc_ifp;	/* make this an interface */
	LIST_ENTRY(bridge_softc) sc_list;
	uint64_t		sc_bridge_id;
	uint64_t		sc_designated_root;
	uint64_t		sc_designated_bridge;
	uint32_t		sc_designated_cost;	/* root path cost */
	uint16_t		sc_designated_port;
	uint16_t		sc_unused01;
	struct bridge_iflist	*sc_root_port;
	uint16_t		sc_max_age;
	uint16_t		sc_hello_time;
	uint16_t		sc_forward_delay;
	uint16_t		sc_bridge_max_age;
	uint16_t		sc_bridge_hello_time;
	uint16_t		sc_bridge_forward_delay;
	uint16_t		sc_topology_change_time;
	uint16_t		sc_hold_time;
	uint16_t		sc_bridge_priority;
	uint8_t			sc_topology_change_detected;
	uint8_t			sc_topology_change;
	struct bridge_timer	sc_hello_timer;
	struct bridge_timer	sc_topology_change_timer;
	struct bridge_timer	sc_tcn_timer;
	uint32_t		sc_brtmax;	/* max # of addresses */
	uint32_t		sc_brtcnt;	/* cur. # of addresses */
	uint32_t		sc_brttimeout;	/* rt timeout in seconds */
	struct callout		sc_brcallout;	/* bridge callout */
	struct netmsg_base	sc_brtimemsg;	/* bridge callout msg */
	struct callout		sc_bstpcallout;	/* STP callout */
	struct netmsg_base	sc_bstptimemsg;	/* STP callout msg */
	struct bridge_iflist_head *sc_iflists;	/* percpu member if lists */
	struct bridge_rtnode_head **sc_rthashs;	/* percpu forwarding tables */
	struct bridge_rtnode_head *sc_rtlists;	/* percpu lists of the above */
	uint32_t		sc_rthash_key;	/* key for hash */
	struct bridge_iflist_head sc_spanlist;	/* span ports list */
	int			sc_span;	/* has span ports */
	struct bridge_timer	sc_link_timer;
};
#define sc_if                   sc_arp.ac_if

#define BRIDGE_CFGCPU		0
#define BRIDGE_CFGPORT		cpu_portfn(BRIDGE_CFGCPU)

extern const uint8_t bstp_etheraddr[];

void	bridge_rtdelete(struct bridge_softc *, struct ifnet *ifp, int);

extern	void	(*bstp_linkstate_p)(struct ifnet *ifp, int state);

void	bstp_initialization(struct bridge_softc *);
void	bstp_linkstate(struct ifnet *, int);
void	bstp_stop(struct bridge_softc *);
void	bstp_input(struct bridge_softc *, struct bridge_iflist *,
		   struct mbuf *);
void	bstp_tick_handler(netmsg_t);
int	bstp_supersedes_port_info(struct bridge_softc *,
		   struct bridge_iflist *);


void	bridge_enqueue(struct ifnet *, struct mbuf *);

#endif /* _KERNEL */

#endif	/* !_NET_IF_BRIDGEVAR_H */
