/*
 * Copyright (c) 1982, 1989, 1993
 *	The Regents of the University of California.  All rights reserved.
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
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)if_ethersubr.c	8.1 (Berkeley) 6/10/93
 * $FreeBSD: src/sys/net/if_ethersubr.c,v 1.70.2.33 2003/04/28 15:45:53 archie Exp $
 * $DragonFly: src/sys/net/if_ethersubr.c,v 1.75 2008/07/07 22:02:10 nant Exp $
 */

#include "opt_atalk.h"
#include "opt_inet.h"
#include "opt_inet6.h"
#include "opt_ipx.h"
#include "opt_mpls.h"
#include "opt_netgraph.h"
#include "opt_carp.h"
#include "opt_ethernet.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/globaldata.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/msgport.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/sysctl.h>
#include <sys/thread.h>
#include <sys/thread2.h>

#include <net/if.h>
#include <net/netisr.h>
#include <net/route.h>
#include <net/if_llc.h>
#include <net/if_dl.h>
#include <net/if_types.h>
#include <net/ifq_var.h>
#include <net/bpf.h>
#include <net/ethernet.h>
#include <net/vlan/if_vlan_ether.h>
#include <net/netmsg2.h>

#if defined(INET) || defined(INET6)
#include <netinet/in.h>
#include <netinet/in_var.h>
#include <netinet/if_ether.h>
#include <net/ipfw/ip_fw.h>
#include <net/dummynet/ip_dummynet.h>
#endif
#ifdef INET6
#include <netinet6/nd6.h>
#endif

#ifdef CARP
#include <netinet/ip_carp.h>
#endif

#ifdef IPX
#include <netproto/ipx/ipx.h>
#include <netproto/ipx/ipx_if.h>
int (*ef_inputp)(struct ifnet*, const struct ether_header *eh, struct mbuf *m);
int (*ef_outputp)(struct ifnet *ifp, struct mbuf **mp, struct sockaddr *dst,
		  short *tp, int *hlen);
#endif

#ifdef NS
#include <netns/ns.h>
#include <netns/ns_if.h>
ushort ns_nettype;
int ether_outputdebug = 0;
int ether_inputdebug = 0;
#endif

#ifdef NETATALK
#include <netproto/atalk/at.h>
#include <netproto/atalk/at_var.h>
#include <netproto/atalk/at_extern.h>

#define	llc_snap_org_code	llc_un.type_snap.org_code
#define	llc_snap_ether_type	llc_un.type_snap.ether_type

extern u_char	at_org_code[3];
extern u_char	aarp_org_code[3];
#endif /* NETATALK */

#ifdef MPLS
#include <netproto/mpls/mpls.h>
#endif

/* netgraph node hooks for ng_ether(4) */
void	(*ng_ether_input_p)(struct ifnet *ifp, struct mbuf **mp);
void	(*ng_ether_input_orphan_p)(struct ifnet *ifp,
		struct mbuf *m, const struct ether_header *eh);
int	(*ng_ether_output_p)(struct ifnet *ifp, struct mbuf **mp);
void	(*ng_ether_attach_p)(struct ifnet *ifp);
void	(*ng_ether_detach_p)(struct ifnet *ifp);

int	(*vlan_input_p)(struct mbuf *, struct mbuf_chain *);
void	(*vlan_input2_p)(struct mbuf *);

static int ether_output(struct ifnet *, struct mbuf *, struct sockaddr *,
			struct rtentry *);
static void ether_restore_header(struct mbuf **, const struct ether_header *,
				 const struct ether_header *);
static void ether_demux_chain(struct ifnet *, struct mbuf *,
			      struct mbuf_chain *);

/*
 * if_bridge support
 */
struct mbuf *(*bridge_input_p)(struct ifnet *, struct mbuf *);
int (*bridge_output_p)(struct ifnet *, struct mbuf *);
void (*bridge_dn_p)(struct mbuf *, struct ifnet *);

static int ether_resolvemulti(struct ifnet *, struct sockaddr **,
			      struct sockaddr *);

const uint8_t etherbroadcastaddr[ETHER_ADDR_LEN] = {
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff
};

#define gotoerr(e) do { error = (e); goto bad; } while (0)
#define IFP2AC(ifp) ((struct arpcom *)(ifp))

static boolean_t ether_ipfw_chk(struct mbuf **m0, struct ifnet *dst,
				struct ip_fw **rule,
				const struct ether_header *eh);

static int ether_ipfw;
static u_int ether_restore_hdr;
static u_int ether_prepend_hdr;

SYSCTL_DECL(_net_link);
SYSCTL_NODE(_net_link, IFT_ETHER, ether, CTLFLAG_RW, 0, "Ethernet");
SYSCTL_INT(_net_link_ether, OID_AUTO, ipfw, CTLFLAG_RW,
	   &ether_ipfw, 0, "Pass ether pkts through firewall");
SYSCTL_UINT(_net_link_ether, OID_AUTO, restore_hdr, CTLFLAG_RW,
	    &ether_restore_hdr, 0, "# of ether header restoration");
SYSCTL_UINT(_net_link_ether, OID_AUTO, prepend_hdr, CTLFLAG_RW,
	    &ether_prepend_hdr, 0,
	    "# of ether header restoration which prepends mbuf");

/*
 * Ethernet output routine.
 * Encapsulate a packet of type family for the local net.
 * Use trailer local net encapsulation if enough data in first
 * packet leaves a multiple of 512 bytes of data in remainder.
 * Assumes that ifp is actually pointer to arpcom structure.
 */
static int
ether_output(struct ifnet *ifp, struct mbuf *m, struct sockaddr *dst,
	     struct rtentry *rt)
{
	struct ether_header *eh, *deh;
	u_char *edst;
	int loop_copy = 0;
	int hlen = ETHER_HDR_LEN;	/* link layer header length */
	struct arpcom *ac = IFP2AC(ifp);
	int error;

	ASSERT_NOT_SERIALIZED(ifp->if_serializer);

	if (ifp->if_flags & IFF_MONITOR)
		gotoerr(ENETDOWN);
	if ((ifp->if_flags & (IFF_UP | IFF_RUNNING)) != (IFF_UP | IFF_RUNNING))
		gotoerr(ENETDOWN);

	M_PREPEND(m, sizeof(struct ether_header), MB_DONTWAIT);
	if (m == NULL)
		return (ENOBUFS);
	eh = mtod(m, struct ether_header *);
	edst = eh->ether_dhost;

	/*
	 * Fill in the destination ethernet address and frame type.
	 */
	switch (dst->sa_family) {
#ifdef INET
	case AF_INET:
		if (!arpresolve(ifp, rt, m, dst, edst))
			return (0);	/* if not yet resolved */
		eh->ether_type = htons(ETHERTYPE_IP);
		break;
#endif
#ifdef INET6
	case AF_INET6:
		if (!nd6_storelladdr(&ac->ac_if, rt, m, dst, edst))
			return (0);		/* Something bad happenned. */
		eh->ether_type = htons(ETHERTYPE_IPV6);
		break;
#endif
#ifdef IPX
	case AF_IPX:
		if (ef_outputp != NULL) {
			error = ef_outputp(ifp, &m, dst, &eh->ether_type,
					   &hlen);
			if (error)
				goto bad;
		} else {
			eh->ether_type = htons(ETHERTYPE_IPX);
			bcopy(&(((struct sockaddr_ipx *)dst)->sipx_addr.x_host),
			      edst, ETHER_ADDR_LEN);
		}
		break;
#endif
#ifdef NETATALK
	case AF_APPLETALK: {
		struct at_ifaddr *aa;

		if ((aa = at_ifawithnet((struct sockaddr_at *)dst)) == NULL) {
			error = 0;	/* XXX */
			goto bad;
		}
		/*
		 * In the phase 2 case, need to prepend an mbuf for
		 * the llc header.  Since we must preserve the value
		 * of m, which is passed to us by value, we m_copy()
		 * the first mbuf, and use it for our llc header.
		 */
		if (aa->aa_flags & AFA_PHASE2) {
			struct llc llc;

			M_PREPEND(m, sizeof(struct llc), MB_DONTWAIT);
			eh = mtod(m, struct ether_header *);
			edst = eh->ether_dhost;
			llc.llc_dsap = llc.llc_ssap = LLC_SNAP_LSAP;
			llc.llc_control = LLC_UI;
			bcopy(at_org_code, llc.llc_snap_org_code,
			      sizeof at_org_code);
			llc.llc_snap_ether_type = htons(ETHERTYPE_AT);
			bcopy(&llc,
			      mtod(m, caddr_t) + sizeof(struct ether_header),
			      sizeof(struct llc));
			eh->ether_type = htons(m->m_pkthdr.len);
			hlen = sizeof(struct llc) + ETHER_HDR_LEN;
		} else {
			eh->ether_type = htons(ETHERTYPE_AT);
		}
		if (!aarpresolve(ac, m, (struct sockaddr_at *)dst, edst))
			return (0);
		break;
	  }
#endif
#ifdef NS
	case AF_NS:
		switch(ns_nettype) {
		default:
		case 0x8137:	/* Novell Ethernet_II Ethernet TYPE II */
			eh->ether_type = 0x8137;
			break;
		case 0x0:	/* Novell 802.3 */
			eh->ether_type = htons(m->m_pkthdr.len);
			break;
		case 0xe0e0:	/* Novell 802.2 and Token-Ring */
			M_PREPEND(m, 3, MB_DONTWAIT);
			eh = mtod(m, struct ether_header *);
			edst = eh->ether_dhost;
			eh->ether_type = htons(m->m_pkthdr.len);
			cp = mtod(m, u_char *) + sizeof(struct ether_header);
			*cp++ = 0xE0;
			*cp++ = 0xE0;
			*cp++ = 0x03;
			break;
		}
		bcopy(&(((struct sockaddr_ns *)dst)->sns_addr.x_host), edst,
		      ETHER_ADDR_LEN);
		/*
		 * XXX if ns_thishost is the same as the node's ethernet
		 * address then just the default code will catch this anyhow.
		 * So I'm not sure if this next clause should be here at all?
		 * [JRE]
		 */
		if (bcmp(edst, &ns_thishost, ETHER_ADDR_LEN) == 0) {
			m->m_pkthdr.rcvif = ifp;
			netisr_dispatch(NETISR_NS, m);
			return (error);
		}
		if (bcmp(edst, &ns_broadhost, ETHER_ADDR_LEN) == 0)
			m->m_flags |= M_BCAST;
		break;
#endif
#ifdef MPLS
	case AF_MPLS:
	{
		struct sockaddr *sa_gw;

		if (rt)
			sa_gw = (struct sockaddr *)rt->rt_gateway;
		else {
			/* We realy need a gateway. */
			m_freem(m);
			return (0);
		}

		switch (sa_gw->sa_family) {
			case AF_INET:
				if (!arpresolve(ifp, rt, m, sa_gw, edst))
					return (0);
				break;
			default:
				kprintf("ether_output: address family not supported to forward mpls packets: %d.\n", sa_gw->sa_family);
				m_freem(m);
				return (0);
		}
		eh->ether_type = htons(ETHERTYPE_MPLS); /* XXX how about multicast? */
		break;
	}
#endif
	case pseudo_AF_HDRCMPLT:
	case AF_UNSPEC:
		loop_copy = -1; /* if this is for us, don't do it */
		deh = (struct ether_header *)dst->sa_data;
		memcpy(edst, deh->ether_dhost, ETHER_ADDR_LEN);
		eh->ether_type = deh->ether_type;
		break;

	default:
		if_printf(ifp, "can't handle af%d\n", dst->sa_family);
		gotoerr(EAFNOSUPPORT);
	}

	if (dst->sa_family == pseudo_AF_HDRCMPLT)	/* unlikely */
		memcpy(eh->ether_shost,
		       ((struct ether_header *)dst->sa_data)->ether_shost,
		       ETHER_ADDR_LEN);
	else
		memcpy(eh->ether_shost, ac->ac_enaddr, ETHER_ADDR_LEN);

	/*
	 * Bridges require special output handling.
	 */
	if (ifp->if_bridge) {
		KASSERT(bridge_output_p != NULL,
			("%s: if_bridge not loaded!", __func__));
		return bridge_output_p(ifp, m);
	}

	/*
	 * If a simplex interface, and the packet is being sent to our
	 * Ethernet address or a broadcast address, loopback a copy.
	 * XXX To make a simplex device behave exactly like a duplex
	 * device, we should copy in the case of sending to our own
	 * ethernet address (thus letting the original actually appear
	 * on the wire). However, we don't do that here for security
	 * reasons and compatibility with the original behavior.
	 */
	if ((ifp->if_flags & IFF_SIMPLEX) && (loop_copy != -1)) {
		int csum_flags = 0;

		if (m->m_pkthdr.csum_flags & CSUM_IP)
			csum_flags |= (CSUM_IP_CHECKED | CSUM_IP_VALID);
		if (m->m_pkthdr.csum_flags & CSUM_DELAY_DATA)
			csum_flags |= (CSUM_DATA_VALID | CSUM_PSEUDO_HDR);
		if ((m->m_flags & M_BCAST) || (loop_copy > 0)) {
			struct mbuf *n;

			if ((n = m_copypacket(m, MB_DONTWAIT)) != NULL) {
				n->m_pkthdr.csum_flags |= csum_flags;
				if (csum_flags & CSUM_DATA_VALID)
					n->m_pkthdr.csum_data = 0xffff;
				if_simloop(ifp, n, dst->sa_family, hlen);
			} else
				ifp->if_iqdrops++;
		} else if (bcmp(eh->ether_dhost, eh->ether_shost,
				ETHER_ADDR_LEN) == 0) {
			m->m_pkthdr.csum_flags |= csum_flags;
			if (csum_flags & CSUM_DATA_VALID)
				m->m_pkthdr.csum_data = 0xffff;
			if_simloop(ifp, m, dst->sa_family, hlen);
			return (0);	/* XXX */
		}
	}

#ifdef CARP
	if (ifp->if_carp && (error = carp_output(ifp, m, dst, NULL)))
		goto bad;
#endif
 

	/* Handle ng_ether(4) processing, if any */
	if (ng_ether_output_p != NULL) {
		if ((error = (*ng_ether_output_p)(ifp, &m)) != 0)
			goto bad;
		if (m == NULL)
			return (0);
	}

	/* Continue with link-layer output */
	return ether_output_frame(ifp, m);

bad:
	m_freem(m);
	return (error);
}

/*
 * Ethernet link layer output routine to send a raw frame to the device.
 *
 * This assumes that the 14 byte Ethernet header is present and contiguous
 * in the first mbuf.
 */
int
ether_output_frame(struct ifnet *ifp, struct mbuf *m)
{
	struct ip_fw *rule = NULL;
	int error = 0;
	struct altq_pktattr pktattr;
	struct m_tag *mtag;

	ASSERT_NOT_SERIALIZED(ifp->if_serializer);

	/* Extract info from dummynet tag */
	mtag = m_tag_find(m, PACKET_TAG_DUMMYNET, NULL);
	if (mtag != NULL) {
		rule = ((struct dn_pkt *)m_tag_data(mtag))->dn_priv;

		m_tag_delete(m, mtag);
		mtag = NULL;
	}

	if (ifq_is_enabled(&ifp->if_snd))
		altq_etherclassify(&ifp->if_snd, m, &pktattr);
	crit_enter();
	if (IPFW_LOADED && ether_ipfw != 0) {
		struct ether_header save_eh, *eh;

		eh = mtod(m, struct ether_header *);
		save_eh = *eh;
		m_adj(m, ETHER_HDR_LEN);
		if (!ether_ipfw_chk(&m, ifp, &rule, eh)) {
			crit_exit();
			if (m != NULL) {
				m_freem(m);
				return ENOBUFS; /* pkt dropped */
			} else
				return 0;	/* consumed e.g. in a pipe */
		}

		/* packet was ok, restore the ethernet header */
		ether_restore_header(&m, eh, &save_eh);
		if (m == NULL) {
			crit_exit();
			return ENOBUFS;
		}
	}
	crit_exit();

	/*
	 * Queue message on interface, update output statistics if
	 * successful, and start output if interface not yet active.
	 */
	error = ifq_dispatch(ifp, m, &pktattr);
	return (error);
}

/*
 * ipfw processing for ethernet packets (in and out).
 * The second parameter is NULL from ether_demux(), and ifp from
 * ether_output_frame().
 */
static boolean_t
ether_ipfw_chk(struct mbuf **m0, struct ifnet *dst, struct ip_fw **rule,
	       const struct ether_header *eh)
{
	struct ether_header save_eh = *eh;	/* might be a ptr in m */
	struct ip_fw_args args;
	struct m_tag *mtag;
	int i;

	if (*rule != NULL && fw_one_pass)
		return TRUE; /* dummynet packet, already partially processed */

	/*
	 * I need some amount of data to be contiguous.
	 */
	i = min((*m0)->m_pkthdr.len, max_protohdr);
	if ((*m0)->m_len < i) {
		*m0 = m_pullup(*m0, i);
		if (*m0 == NULL)
			return FALSE;
	}

	args.m = *m0;		/* the packet we are looking at		*/
	args.oif = dst;		/* destination, if any			*/
	if ((mtag = m_tag_find(*m0, PACKET_TAG_IPFW_DIVERT, NULL)) != NULL)
		m_tag_delete(*m0, mtag);
	args.rule = *rule;	/* matching rule to restart		*/
	args.next_hop = NULL;	/* we do not support forward yet	*/
	args.eh = &save_eh;	/* MAC header for bridged/MAC packets	*/
	i = ip_fw_chk_ptr(&args);
	*m0 = args.m;
	*rule = args.rule;

	if ((i & IP_FW_PORT_DENY_FLAG) || *m0 == NULL)	/* drop */
		return FALSE;

	if (i == 0)					/* a PASS rule.  */
		return TRUE;

	if (i & IP_FW_PORT_DYNT_FLAG) {
		/*
		 * Pass the pkt to dummynet, which consumes it.
		 */
		struct mbuf *m;

		m = *m0;	/* pass the original to dummynet */
		*m0 = NULL;	/* and nothing back to the caller */

		ether_restore_header(&m, eh, &save_eh);
		if (m == NULL)
			return FALSE;

		ip_fw_dn_io_ptr(m, (i & 0xffff),
			dst ? DN_TO_ETH_OUT: DN_TO_ETH_DEMUX, &args);
		return FALSE;
	}
	/*
	 * XXX at some point add support for divert/forward actions.
	 * If none of the above matches, we have to drop the pkt.
	 */
	return FALSE;
}

/*
 * Process a received Ethernet packet.
 *
 * The ethernet header is assumed to be in the mbuf so the caller
 * MUST MAKE SURE that there are at least sizeof(struct ether_header)
 * bytes in the first mbuf.
 *
 * This allows us to concentrate in one place a bunch of code which
 * is replicated in all device drivers. Also, many functions called
 * from ether_input() try to put the eh back into the mbuf, so we
 * can later propagate the 'contiguous packet' interface to them.
 *
 * NOTA BENE: for all drivers "eh" is a pointer into the first mbuf or
 * cluster, right before m_data. So be very careful when working on m,
 * as you could destroy *eh !!
 *
 * First we perform any link layer operations, then continue to the
 * upper layers with ether_demux().
 */
void
ether_input_chain(struct ifnet *ifp, struct mbuf *m, struct mbuf_chain *chain)
{
	struct ether_header *eh;

	ASSERT_SERIALIZED(ifp->if_serializer);
	M_ASSERTPKTHDR(m);

	/* Discard packet if interface is not up */
	if (!(ifp->if_flags & IFF_UP)) {
		m_freem(m);
		return;
	}

	if (m->m_len < sizeof(struct ether_header)) {
		/* XXX error in the caller. */
		m_freem(m);
		return;
	}
	eh = mtod(m, struct ether_header *);

	if (ntohs(eh->ether_type) == ETHERTYPE_VLAN &&
	    (m->m_flags & M_VLANTAG) == 0) {
		/*
		 * Extract vlan tag if hardware does not do it for us
		 */
		vlan_ether_decap(&m);
		if (m == NULL)
			return;
		eh = mtod(m, struct ether_header *);
	}

	m->m_pkthdr.rcvif = ifp;

	if (ETHER_IS_MULTICAST(eh->ether_dhost)) {
		if (bcmp(ifp->if_broadcastaddr, eh->ether_dhost,
			 ifp->if_addrlen) == 0)
			m->m_flags |= M_BCAST;
		else
			m->m_flags |= M_MCAST;
		ifp->if_imcasts++;
	}

	ETHER_BPF_MTAP(ifp, m);

	ifp->if_ibytes += m->m_pkthdr.len;

	if (ifp->if_flags & IFF_MONITOR) {
		/*
		 * Interface marked for monitoring; discard packet.
		 */
		 m_freem(m);
		 return;
	}

	/*
	 * Tap the packet off here for a bridge.  bridge_input()
	 * will return NULL if it has consumed the packet, otherwise
	 * it gets processed as normal.  Note that bridge_input()
	 * will always return the original packet if we need to
	 * process it locally.
	 */
	if (ifp->if_bridge) {
		KASSERT(bridge_input_p != NULL,
			("%s: if_bridge not loaded!", __func__));

		if(m->m_flags & M_PROTO1) {
			m->m_flags &= ~M_PROTO1;
		} else {
			/* clear M_PROMISC, in case the packets comes from a vlan */
			/* m->m_flags &= ~M_PROMISC; */
			lwkt_serialize_exit(ifp->if_serializer);
			m = bridge_input_p(ifp, m);
			lwkt_serialize_enter(ifp->if_serializer);
			if (m == NULL)
				return;

			KASSERT(ifp == m->m_pkthdr.rcvif,
				("bridge_input_p changed rcvif\n"));

			/* 'm' may be changed by bridge_input_p() */
			eh = mtod(m, struct ether_header *);
		}
	}

	/* Handle ng_ether(4) processing, if any */
	if (ng_ether_input_p != NULL) {
		ng_ether_input_p(ifp, &m);
		if (m == NULL)
			return;

		/* 'm' may be changed by ng_ether_input_p() */
		eh = mtod(m, struct ether_header *);
	}

	/* Continue with upper layer processing */
	ether_demux_chain(ifp, m, chain);
}

void
ether_input(struct ifnet *ifp, struct mbuf *m)
{
	ether_input_chain(ifp, m, NULL);
}

/*
 * Upper layer processing for a received Ethernet packet.
 */
static void
ether_demux_chain(struct ifnet *ifp, struct mbuf *m, struct mbuf_chain *chain)
{
	struct ether_header save_eh, *eh;
	int isr;
	u_short ether_type;
	struct ip_fw *rule = NULL;
	struct m_tag *mtag;
#ifdef NETATALK
	struct llc *l;
#endif

	M_ASSERTPKTHDR(m);
	KASSERT(m->m_len >= ETHER_HDR_LEN,
		("ether header is no contiguous!\n"));

	eh = mtod(m, struct ether_header *);
	save_eh = *eh;

	/* XXX old crufty stuff, needs to be removed */
	m_adj(m, sizeof(struct ether_header));

	/* Extract info from dummynet tag */
	mtag = m_tag_find(m, PACKET_TAG_DUMMYNET, NULL);
	if (mtag != NULL) {
		rule = ((struct dn_pkt *)m_tag_data(mtag))->dn_priv;
		KKASSERT(ifp == NULL);
		ifp = m->m_pkthdr.rcvif;

		m_tag_delete(m, mtag);
		mtag = NULL;
	}
	if (rule)	/* packet is passing the second time */
		goto post_stats;

#ifdef CARP
	/*
	 * XXX: Okay, we need to call carp_forus() and - if it is for
	 * us jump over code that does the normal check
	 * "ac_enaddr == ether_dhost". The check sequence is a bit
	 * different from OpenBSD, so we jump over as few code as
	 * possible, to catch _all_ sanity checks. This needs
	 * evaluation, to see if the carp ether_dhost values break any
	 * of these checks!
	 */
	if (ifp->if_carp && carp_forus(ifp->if_carp, eh->ether_dhost))
		goto post_stats;
#endif

	/*
	 * Discard packet if upper layers shouldn't see it because
	 * it was unicast to a different Ethernet address.  If the
	 * driver is working properly, then this situation can only
	 * happen when the interface is in promiscuous mode.
	 */
	if (((ifp->if_flags & (IFF_PROMISC | IFF_PPROMISC)) == IFF_PROMISC) &&
	    (eh->ether_dhost[0] & 1) == 0 &&
	    bcmp(eh->ether_dhost, IFP2AC(ifp)->ac_enaddr, ETHER_ADDR_LEN)) {
		m_freem(m);
		return;
	}

post_stats:
	if (IPFW_LOADED && ether_ipfw != 0) {
		if (!ether_ipfw_chk(&m, NULL, &rule, eh)) {
			m_freem(m);
			return;
		}
	}

	ether_type = ntohs(save_eh.ether_type);

	if (m->m_flags & M_VLANTAG) {
		if (ether_type == ETHERTYPE_VLAN) {
			/*
			 * To prevent possible dangerous recursion,
			 * we don't do vlan-in-vlan
			 */
			m->m_pkthdr.rcvif->if_noproto++;
			m_freem(m);
			return;
		}

		if (vlan_input_p != NULL) {
			ether_restore_header(&m, eh, &save_eh);
			if (m != NULL)
				vlan_input_p(m, chain);
		} else {
			m->m_pkthdr.rcvif->if_noproto++;
			m_freem(m);
		}
		return;
	}
	KKASSERT(ether_type != ETHERTYPE_VLAN);

	switch (ether_type) {
#ifdef INET
	case ETHERTYPE_IP:
		if (ipflow_fastforward(m, ifp->if_serializer))
			return;
		isr = NETISR_IP;
		break;

	case ETHERTYPE_ARP:
		if (ifp->if_flags & IFF_NOARP) {
			/* Discard packet if ARP is disabled on interface */
			m_freem(m);
			return;
		}
		isr = NETISR_ARP;
		break;
#endif

#ifdef INET6
	case ETHERTYPE_IPV6:
		isr = NETISR_IPV6;
		break;
#endif

#ifdef IPX
	case ETHERTYPE_IPX:
		if (ef_inputp && ef_inputp(ifp, &save_eh, m) == 0)
			return;
		isr = NETISR_IPX;
		break;
#endif

#ifdef NS
	case 0x8137: /* Novell Ethernet_II Ethernet TYPE II */
		isr = NETISR_NS;
		break;

#endif

#ifdef NETATALK
	case ETHERTYPE_AT:
		isr = NETISR_ATALK1;
		break;
	case ETHERTYPE_AARP:
		isr = NETISR_AARP;
		break;
#endif

#ifdef MPLS
	case ETHERTYPE_MPLS:
	case ETHERTYPE_MPLS_MCAST:
		isr = NETISR_MPLS;
		break;
#endif

	default:
#ifdef IPX
		if (ef_inputp && ef_inputp(ifp, &save_eh, m) == 0)
			return;
#endif
#ifdef NS
		checksum = mtod(m, ushort *);
		/* Novell 802.3 */
		if ((ether_type <= ETHERMTU) &&
		    ((*checksum == 0xffff) || (*checksum == 0xE0E0))) {
			if (*checksum == 0xE0E0) {
				m->m_pkthdr.len -= 3;
				m->m_len -= 3;
				m->m_data += 3;
			}
			isr = NETISR_NS;
			break;
		}
#endif
#ifdef NETATALK
		if (ether_type > ETHERMTU)
			goto dropanyway;
		l = mtod(m, struct llc *);
		if (l->llc_dsap == LLC_SNAP_LSAP &&
		    l->llc_ssap == LLC_SNAP_LSAP &&
		    l->llc_control == LLC_UI) {
			if (bcmp(&(l->llc_snap_org_code)[0], at_org_code,
				 sizeof at_org_code) == 0 &&
			    ntohs(l->llc_snap_ether_type) == ETHERTYPE_AT) {
				m_adj(m, sizeof(struct llc));
				isr = NETISR_ATALK2;
				break;
			}
			if (bcmp(&(l->llc_snap_org_code)[0], aarp_org_code,
				 sizeof aarp_org_code) == 0 &&
			    ntohs(l->llc_snap_ether_type) == ETHERTYPE_AARP) {
				m_adj(m, sizeof(struct llc));
				isr = NETISR_AARP;
				break;
			}
		}
dropanyway:
#endif
		if (ng_ether_input_orphan_p != NULL)
			(*ng_ether_input_orphan_p)(ifp, m, &save_eh);
		else
			m_freem(m);
		return;
	}

#ifdef ETHER_INPUT_CHAIN
	if (chain != NULL) {
		struct mbuf_chain *c;
		lwkt_port_t port;
		int cpuid;

		port = netisr_mport(isr, &m);
		if (port == NULL)
			return;

		m->m_pkthdr.header = port; /* XXX */
		cpuid = port->mpu_td->td_gd->gd_cpuid;

		c = &chain[cpuid];
		if (c->mc_head == NULL) {
			c->mc_head = c->mc_tail = m;
		} else {
			c->mc_tail->m_nextpkt = m;
			c->mc_tail = m;
		}
		m->m_nextpkt = NULL;
	} else
#endif	/* ETHER_INPUT_CHAIN */
		netisr_dispatch(isr, m);
}

void
ether_demux(struct ifnet *ifp, struct mbuf *m)
{
	ether_demux_chain(ifp, m, NULL);
}

/*
 * Perform common duties while attaching to interface list
 */

void
ether_ifattach(struct ifnet *ifp, uint8_t *lla, lwkt_serialize_t serializer)
{
	ether_ifattach_bpf(ifp, lla, DLT_EN10MB, sizeof(struct ether_header),
			   serializer);
}

void
ether_ifattach_bpf(struct ifnet *ifp, uint8_t *lla, u_int dlt, u_int hdrlen,
		   lwkt_serialize_t serializer)
{
	struct sockaddr_dl *sdl;

	ifp->if_type = IFT_ETHER;
	ifp->if_addrlen = ETHER_ADDR_LEN;
	ifp->if_hdrlen = ETHER_HDR_LEN;
	if_attach(ifp, serializer);
	ifp->if_mtu = ETHERMTU;
	if (ifp->if_baudrate == 0)
		ifp->if_baudrate = 10000000;
	ifp->if_output = ether_output;
	ifp->if_input = ether_input;
	ifp->if_resolvemulti = ether_resolvemulti;
	ifp->if_broadcastaddr = etherbroadcastaddr;
	sdl = IF_LLSOCKADDR(ifp);
	sdl->sdl_type = IFT_ETHER;
	sdl->sdl_alen = ifp->if_addrlen;
	bcopy(lla, LLADDR(sdl), ifp->if_addrlen);
	/*
	 * XXX Keep the current drivers happy.
	 * XXX Remove once all drivers have been cleaned up
	 */
	if (lla != IFP2AC(ifp)->ac_enaddr)
		bcopy(lla, IFP2AC(ifp)->ac_enaddr, ifp->if_addrlen);
	bpfattach(ifp, dlt, hdrlen);
	if (ng_ether_attach_p != NULL)
		(*ng_ether_attach_p)(ifp);

	if_printf(ifp, "MAC address: %6D\n", lla, ":");
}

/*
 * Perform common duties while detaching an Ethernet interface
 */
void
ether_ifdetach(struct ifnet *ifp)
{
	if_down(ifp);

	if (ng_ether_detach_p != NULL)
		(*ng_ether_detach_p)(ifp);
	bpfdetach(ifp);
	if_detach(ifp);
}

int
ether_ioctl(struct ifnet *ifp, int command, caddr_t data)
{
	struct ifaddr *ifa = (struct ifaddr *) data;
	struct ifreq *ifr = (struct ifreq *) data;
	int error = 0;

#define IF_INIT(ifp) \
do { \
	if (((ifp)->if_flags & IFF_UP) == 0) { \
		(ifp)->if_flags |= IFF_UP; \
		(ifp)->if_init((ifp)->if_softc); \
	} \
} while (0)

	ASSERT_SERIALIZED(ifp->if_serializer);

	switch (command) {
	case SIOCSIFADDR:
		switch (ifa->ifa_addr->sa_family) {
#ifdef INET
		case AF_INET:
			IF_INIT(ifp);	/* before arpwhohas */
			arp_ifinit(ifp, ifa);
			break;
#endif
#ifdef IPX
		/*
		 * XXX - This code is probably wrong
		 */
		case AF_IPX:
			{
			struct ipx_addr *ina = &IA_SIPX(ifa)->sipx_addr;
			struct arpcom *ac = IFP2AC(ifp);

			if (ipx_nullhost(*ina))
				ina->x_host = *(union ipx_host *) ac->ac_enaddr;
			else
				bcopy(ina->x_host.c_host, ac->ac_enaddr,
				      sizeof ac->ac_enaddr);

			IF_INIT(ifp);	/* Set new address. */
			break;
			}
#endif
#ifdef NS
		/*
		 * XXX - This code is probably wrong
		 */
		case AF_NS:
		{
			struct ns_addr *ina = &(IA_SNS(ifa)->sns_addr);
			struct arpcom *ac = IFP2AC(ifp);

			if (ns_nullhost(*ina))
				ina->x_host = *(union ns_host *)(ac->ac_enaddr);
			else
				bcopy(ina->x_host.c_host, ac->ac_enaddr,
				      sizeof ac->ac_enaddr);

			/*
			 * Set new address
			 */
			IF_INIT(ifp);
			break;
		}
#endif
		default:
			IF_INIT(ifp);
			break;
		}
		break;

	case SIOCGIFADDR:
		bcopy(IFP2AC(ifp)->ac_enaddr,
		      ((struct sockaddr *)ifr->ifr_data)->sa_data,
		      ETHER_ADDR_LEN);
		break;

	case SIOCSIFMTU:
		/*
		 * Set the interface MTU.
		 */
		if (ifr->ifr_mtu > ETHERMTU) {
			error = EINVAL;
		} else {
			ifp->if_mtu = ifr->ifr_mtu;
		}
		break;
	default:
		error = EINVAL;
		break;
	}
	return (error);

#undef IF_INIT
}

int
ether_resolvemulti(
	struct ifnet *ifp,
	struct sockaddr **llsa,
	struct sockaddr *sa)
{
	struct sockaddr_dl *sdl;
	struct sockaddr_in *sin;
#ifdef INET6
	struct sockaddr_in6 *sin6;
#endif
	u_char *e_addr;

	switch(sa->sa_family) {
	case AF_LINK:
		/*
		 * No mapping needed. Just check that it's a valid MC address.
		 */
		sdl = (struct sockaddr_dl *)sa;
		e_addr = LLADDR(sdl);
		if ((e_addr[0] & 1) != 1)
			return EADDRNOTAVAIL;
		*llsa = 0;
		return 0;

#ifdef INET
	case AF_INET:
		sin = (struct sockaddr_in *)sa;
		if (!IN_MULTICAST(ntohl(sin->sin_addr.s_addr)))
			return EADDRNOTAVAIL;
		MALLOC(sdl, struct sockaddr_dl *, sizeof *sdl, M_IFMADDR,
		       M_WAITOK | M_ZERO);
		sdl->sdl_len = sizeof *sdl;
		sdl->sdl_family = AF_LINK;
		sdl->sdl_index = ifp->if_index;
		sdl->sdl_type = IFT_ETHER;
		sdl->sdl_alen = ETHER_ADDR_LEN;
		e_addr = LLADDR(sdl);
		ETHER_MAP_IP_MULTICAST(&sin->sin_addr, e_addr);
		*llsa = (struct sockaddr *)sdl;
		return 0;
#endif
#ifdef INET6
	case AF_INET6:
		sin6 = (struct sockaddr_in6 *)sa;
		if (IN6_IS_ADDR_UNSPECIFIED(&sin6->sin6_addr)) {
			/*
			 * An IP6 address of 0 means listen to all
			 * of the Ethernet multicast address used for IP6.
			 * (This is used for multicast routers.)
			 */
			ifp->if_flags |= IFF_ALLMULTI;
			*llsa = 0;
			return 0;
		}
		if (!IN6_IS_ADDR_MULTICAST(&sin6->sin6_addr))
			return EADDRNOTAVAIL;
		MALLOC(sdl, struct sockaddr_dl *, sizeof *sdl, M_IFMADDR,
		       M_WAITOK | M_ZERO);
		sdl->sdl_len = sizeof *sdl;
		sdl->sdl_family = AF_LINK;
		sdl->sdl_index = ifp->if_index;
		sdl->sdl_type = IFT_ETHER;
		sdl->sdl_alen = ETHER_ADDR_LEN;
		e_addr = LLADDR(sdl);
		ETHER_MAP_IPV6_MULTICAST(&sin6->sin6_addr, e_addr);
		*llsa = (struct sockaddr *)sdl;
		return 0;
#endif

	default:
		/*
		 * Well, the text isn't quite right, but it's the name
		 * that counts...
		 */
		return EAFNOSUPPORT;
	}
}

#if 0
/*
 * This is for reference.  We have a table-driven version
 * of the little-endian crc32 generator, which is faster
 * than the double-loop.
 */
uint32_t
ether_crc32_le(const uint8_t *buf, size_t len)
{
	uint32_t c, crc, carry;
	size_t i, j;

	crc = 0xffffffffU;	/* initial value */

	for (i = 0; i < len; i++) {
		c = buf[i];
		for (j = 0; j < 8; j++) {
			carry = ((crc & 0x01) ? 1 : 0) ^ (c & 0x01);
			crc >>= 1;
			c >>= 1;
			if (carry)
				crc = (crc ^ ETHER_CRC_POLY_LE);
		}
	}

	return (crc);
}
#else
uint32_t
ether_crc32_le(const uint8_t *buf, size_t len)
{
	static const uint32_t crctab[] = {
		0x00000000, 0x1db71064, 0x3b6e20c8, 0x26d930ac,
		0x76dc4190, 0x6b6b51f4, 0x4db26158, 0x5005713c,
		0xedb88320, 0xf00f9344, 0xd6d6a3e8, 0xcb61b38c,
		0x9b64c2b0, 0x86d3d2d4, 0xa00ae278, 0xbdbdf21c
	};
	uint32_t crc;
	size_t i;

	crc = 0xffffffffU;	/* initial value */

	for (i = 0; i < len; i++) {
		crc ^= buf[i];
		crc = (crc >> 4) ^ crctab[crc & 0xf];
		crc = (crc >> 4) ^ crctab[crc & 0xf];
	}

	return (crc);
}
#endif

uint32_t
ether_crc32_be(const uint8_t *buf, size_t len)
{
	uint32_t c, crc, carry;
	size_t i, j;

	crc = 0xffffffffU;	/* initial value */

	for (i = 0; i < len; i++) {
		c = buf[i];
		for (j = 0; j < 8; j++) {
			carry = ((crc & 0x80000000U) ? 1 : 0) ^ (c & 0x01);
			crc <<= 1;
			c >>= 1;
			if (carry)
				crc = (crc ^ ETHER_CRC_POLY_BE) | carry;
		}
	}

	return (crc);
}

/*
 * find the size of ethernet header, and call classifier
 */
void
altq_etherclassify(struct ifaltq *ifq, struct mbuf *m,
		   struct altq_pktattr *pktattr)
{
	struct ether_header *eh;
	uint16_t ether_type;
	int hlen, af, hdrsize;
	caddr_t hdr;

	hlen = sizeof(struct ether_header);
	eh = mtod(m, struct ether_header *);

	ether_type = ntohs(eh->ether_type);
	if (ether_type < ETHERMTU) {
		/* ick! LLC/SNAP */
		struct llc *llc = (struct llc *)(eh + 1);
		hlen += 8;

		if (m->m_len < hlen ||
		    llc->llc_dsap != LLC_SNAP_LSAP ||
		    llc->llc_ssap != LLC_SNAP_LSAP ||
		    llc->llc_control != LLC_UI)
			goto bad;  /* not snap! */

		ether_type = ntohs(llc->llc_un.type_snap.ether_type);
	}

	if (ether_type == ETHERTYPE_IP) {
		af = AF_INET;
		hdrsize = 20;  /* sizeof(struct ip) */
#ifdef INET6
	} else if (ether_type == ETHERTYPE_IPV6) {
		af = AF_INET6;
		hdrsize = 40;  /* sizeof(struct ip6_hdr) */
#endif
	} else
		goto bad;

	while (m->m_len <= hlen) {
		hlen -= m->m_len;
		m = m->m_next;
	}
	hdr = m->m_data + hlen;
	if (m->m_len < hlen + hdrsize) {
		/*
		 * ip header is not in a single mbuf.  this should not
		 * happen in the current code.
		 * (todo: use m_pulldown in the future)
		 */
		goto bad;
	}
	m->m_data += hlen;
	m->m_len -= hlen;
	ifq_classify(ifq, m, af, pktattr);
	m->m_data -= hlen;
	m->m_len += hlen;

	return;

bad:
	pktattr->pattr_class = NULL;
	pktattr->pattr_hdr = NULL;
	pktattr->pattr_af = AF_UNSPEC;
}

static void
ether_restore_header(struct mbuf **m0, const struct ether_header *eh,
		     const struct ether_header *save_eh)
{
	struct mbuf *m = *m0;

	ether_restore_hdr++;

	/*
	 * Prepend the header, optimize for the common case of
	 * eh pointing into the mbuf.
	 */
	if ((const void *)(eh + 1) == (void *)m->m_data) {
		m->m_data -= ETHER_HDR_LEN;
		m->m_len += ETHER_HDR_LEN;
		m->m_pkthdr.len += ETHER_HDR_LEN;
	} else {
		ether_prepend_hdr++;

		M_PREPEND(m, ETHER_HDR_LEN, MB_DONTWAIT);
		if (m != NULL) {
			bcopy(save_eh, mtod(m, struct ether_header *),
			      ETHER_HDR_LEN);
		}
	}
	*m0 = m;
}

#ifdef ETHER_INPUT_CHAIN

static void
ether_input_ipifunc(void *arg)
{
	struct mbuf *m, *next;
	lwkt_port_t port;

	m = arg;
	do {
		next = m->m_nextpkt;
		m->m_nextpkt = NULL;

		port = m->m_pkthdr.header;
		m->m_pkthdr.header = NULL;

		lwkt_sendmsg(port,
		&m->m_hdr.mh_netmsg.nm_netmsg.nm_lmsg);

		m = next;
	} while (m != NULL);
}

void
ether_input_dispatch(struct mbuf_chain *chain)
{
#ifdef SMP
	int i;

	for (i = 0; i < ncpus; ++i) {
		if (chain[i].mc_head != NULL) {
			lwkt_send_ipiq(globaldata_find(i),
			ether_input_ipifunc, chain[i].mc_head);
		}
	}
#else
	if (chain->mc_head != NULL)
		ether_input_ipifunc(chain->mc_head);
#endif
}

void
ether_input_chain_init(struct mbuf_chain *chain)
{
#ifdef SMP
	int i;

	for (i = 0; i < ncpus; ++i)
		chain[i].mc_head = chain[i].mc_tail = NULL;
#else
	chain->mc_head = chain->mc_tail = NULL;
#endif
}

#endif	/* ETHER_INPUT_CHAIN */

#ifdef ETHER_INPUT2

static void
ether_demux_oncpu(struct ifnet *ifp, struct mbuf *m)
{
	struct ether_header *eh;
	int isr, redispatch;
	u_short ether_type;
	struct ip_fw *rule = NULL;
	struct m_tag *mtag;
#ifdef NETATALK
	struct llc *l;
#endif

	M_ASSERTPKTHDR(m);
	KASSERT(m->m_len >= ETHER_HDR_LEN,
		("ether header is no contiguous!\n"));

	eh = mtod(m, struct ether_header *);

	/* Extract info from dummynet tag */
	mtag = m_tag_find(m, PACKET_TAG_DUMMYNET, NULL);
	if (mtag != NULL) {
		rule = ((struct dn_pkt *)m_tag_data(mtag))->dn_priv;
		KKASSERT(ifp == NULL);
		ifp = m->m_pkthdr.rcvif;

		m_tag_delete(m, mtag);
		mtag = NULL;
	}
	if (rule)	/* packet is passing the second time */
		goto post_stats;

#ifdef CARP
	/*
	 * XXX: Okay, we need to call carp_forus() and - if it is for
	 * us jump over code that does the normal check
	 * "ac_enaddr == ether_dhost". The check sequence is a bit
	 * different from OpenBSD, so we jump over as few code as
	 * possible, to catch _all_ sanity checks. This needs
	 * evaluation, to see if the carp ether_dhost values break any
	 * of these checks!
	 */
	if (ifp->if_carp && carp_forus(ifp->if_carp, eh->ether_dhost))
		goto post_stats;
#endif

	/*
	 * Discard packet if upper layers shouldn't see it because
	 * it was unicast to a different Ethernet address.  If the
	 * driver is working properly, then this situation can only
	 * happen when the interface is in promiscuous mode.
	 */
	if (((ifp->if_flags & (IFF_PROMISC | IFF_PPROMISC)) == IFF_PROMISC) &&
	    (eh->ether_dhost[0] & 1) == 0 &&
	    bcmp(eh->ether_dhost, IFP2AC(ifp)->ac_enaddr, ETHER_ADDR_LEN)) {
		m_freem(m);
		return;
	}

post_stats:
	if (IPFW_LOADED && ether_ipfw != 0) {
		struct ether_header save_eh = *eh;

		/* XXX old crufty stuff, needs to be removed */
		m_adj(m, sizeof(struct ether_header));

		if (!ether_ipfw_chk(&m, NULL, &rule, eh)) {
			m_freem(m);
			return;
		}

		ether_restore_header(&m, eh, &save_eh);
		if (m == NULL)
			return;
		eh = mtod(m, struct ether_header *);
	}

	ether_type = ntohs(eh->ether_type);
	KKASSERT(ether_type != ETHERTYPE_VLAN);

	if (m->m_flags & M_VLANTAG) {
		if (vlan_input2_p != NULL) {
			vlan_input2_p(m);
		} else {
			m->m_pkthdr.rcvif->if_noproto++;
			m_freem(m);
		}
		return;
	}

	m_adj(m, sizeof(struct ether_header));
	redispatch = 0;

	switch (ether_type) {
#ifdef INET
	case ETHERTYPE_IP:
#ifdef notyet
		if (ipflow_fastforward(m, ifp->if_serializer))
			return;
#endif
		isr = NETISR_IP;
		break;

	case ETHERTYPE_ARP:
		if (ifp->if_flags & IFF_NOARP) {
			/* Discard packet if ARP is disabled on interface */
			m_freem(m);
			return;
		}
		isr = NETISR_ARP;
		break;
#endif

#ifdef INET6
	case ETHERTYPE_IPV6:
		isr = NETISR_IPV6;
		break;
#endif

#ifdef IPX
	case ETHERTYPE_IPX:
		if (ef_inputp && ef_inputp(ifp, eh, m) == 0)
			return;
		isr = NETISR_IPX;
		break;
#endif

#ifdef NS
	case 0x8137: /* Novell Ethernet_II Ethernet TYPE II */
		isr = NETISR_NS;
		break;

#endif

#ifdef NETATALK
	case ETHERTYPE_AT:
		isr = NETISR_ATALK1;
		break;
	case ETHERTYPE_AARP:
		isr = NETISR_AARP;
		break;
#endif

	default:
		/*
		 * The accurate msgport is not determined before
		 * we reach here, so redo the dispatching
		 */
		redispatch = 1;
#ifdef IPX
		if (ef_inputp && ef_inputp(ifp, eh, m) == 0)
			return;
#endif
#ifdef NS
		checksum = mtod(m, ushort *);
		/* Novell 802.3 */
		if ((ether_type <= ETHERMTU) &&
		    ((*checksum == 0xffff) || (*checksum == 0xE0E0))) {
			if (*checksum == 0xE0E0) {
				m->m_pkthdr.len -= 3;
				m->m_len -= 3;
				m->m_data += 3;
			}
			isr = NETISR_NS;
			break;
		}
#endif
#ifdef NETATALK
		if (ether_type > ETHERMTU)
			goto dropanyway;
		l = mtod(m, struct llc *);
		if (l->llc_dsap == LLC_SNAP_LSAP &&
		    l->llc_ssap == LLC_SNAP_LSAP &&
		    l->llc_control == LLC_UI) {
			if (bcmp(&(l->llc_snap_org_code)[0], at_org_code,
				 sizeof at_org_code) == 0 &&
			    ntohs(l->llc_snap_ether_type) == ETHERTYPE_AT) {
				m_adj(m, sizeof(struct llc));
				isr = NETISR_ATALK2;
				break;
			}
			if (bcmp(&(l->llc_snap_org_code)[0], aarp_org_code,
				 sizeof aarp_org_code) == 0 &&
			    ntohs(l->llc_snap_ether_type) == ETHERTYPE_AARP) {
				m_adj(m, sizeof(struct llc));
				isr = NETISR_AARP;
				break;
			}
		}
dropanyway:
#endif
		if (ng_ether_input_orphan_p != NULL)
			ng_ether_input_orphan_p(ifp, m, eh);
		else
			m_freem(m);
		return;
	}

	if (!redispatch)
		netisr_run(isr, m);
	else
		netisr_dispatch(isr, m);
}

void
ether_input_oncpu(struct ifnet *ifp, struct mbuf *m)
{
	if ((ifp->if_flags & (IFF_UP | IFF_MONITOR)) != IFF_UP) {
		/*
		 * Receiving interface's flags are changed, when this
		 * packet is waiting for processing; discard it.
		 */
		m_freem(m);
		return;
	}

	/*
	 * Tap the packet off here for a bridge.  bridge_input()
	 * will return NULL if it has consumed the packet, otherwise
	 * it gets processed as normal.  Note that bridge_input()
	 * will always return the original packet if we need to
	 * process it locally.
	 */
	if (ifp->if_bridge) {
		KASSERT(bridge_input_p != NULL,
			("%s: if_bridge not loaded!", __func__));

		if(m->m_flags & M_PROTO1) {
			m->m_flags &= ~M_PROTO1;
		} else {
			/* clear M_PROMISC, in case the packets comes from a vlan */
			/* m->m_flags &= ~M_PROMISC; */
			m = bridge_input_p(ifp, m);
			if (m == NULL)
				return;

			KASSERT(ifp == m->m_pkthdr.rcvif,
				("bridge_input_p changed rcvif\n"));
		}
	}

	/* Handle ng_ether(4) processing, if any */
	if (ng_ether_input_p != NULL) {
		ng_ether_input_p(ifp, &m);
		if (m == NULL)
			return;
	}

	/* Continue with upper layer processing */
	ether_demux_oncpu(ifp, m);
}

static void
ether_input_handler(struct netmsg *nmsg)
{
	struct netmsg_packet *nmp = (struct netmsg_packet *)nmsg;
	struct ifnet *ifp;
	struct mbuf *m;

	m = nmp->nm_packet;
	M_ASSERTPKTHDR(m);
	ifp = m->m_pkthdr.rcvif;

	ether_input_oncpu(ifp, m);
}

static __inline void
ether_init_netpacket(int num, struct mbuf *m)
{
	struct netmsg_packet *pmsg;

	pmsg = &m->m_hdr.mh_netmsg;
	netmsg_init(&pmsg->nm_netmsg, &netisr_apanic_rport, 0,
		    ether_input_handler);
	pmsg->nm_packet = m;
	pmsg->nm_netmsg.nm_lmsg.u.ms_result = num;
}

static __inline struct lwkt_port *
ether_mport(int num, struct mbuf **m0)
{
	struct lwkt_port *port;
	struct mbuf *m = *m0;

	if (num == NETISR_MAX) {
		/*
		 * All packets whose target msgports can't be
		 * determined here are dispatched to netisr0,
		 * where further dispatching may happen.
		 */
		return cpu_portfn(0);
	}

	port = netisr_find_port(num, &m);
	if (port == NULL)
		return NULL;

	*m0 = m;
	return port;
}

void
ether_input_chain2(struct ifnet *ifp, struct mbuf *m, struct mbuf_chain *chain)
{
	struct ether_header *eh, *save_eh, save_eh0;
	struct lwkt_port *port;
	uint16_t ether_type;
	int isr;

	ASSERT_SERIALIZED(ifp->if_serializer);
	M_ASSERTPKTHDR(m);

	/* Discard packet if interface is not up */
	if (!(ifp->if_flags & IFF_UP)) {
		m_freem(m);
		return;
	}

	if (m->m_len < sizeof(struct ether_header)) {
		/* XXX error in the caller. */
		m_freem(m);
		return;
	}
	eh = mtod(m, struct ether_header *);

	m->m_pkthdr.rcvif = ifp;

	if (ETHER_IS_MULTICAST(eh->ether_dhost)) {
		if (bcmp(ifp->if_broadcastaddr, eh->ether_dhost,
			 ifp->if_addrlen) == 0)
			m->m_flags |= M_BCAST;
		else
			m->m_flags |= M_MCAST;
		ifp->if_imcasts++;
	}

	ETHER_BPF_MTAP(ifp, m);

	ifp->if_ibytes += m->m_pkthdr.len;

	if (ifp->if_flags & IFF_MONITOR) {
		/*
		 * Interface marked for monitoring; discard packet.
		 */
		m_freem(m);
		return;
	}

	if (ntohs(eh->ether_type) == ETHERTYPE_VLAN &&
	    (m->m_flags & M_VLANTAG) == 0) {
		/*
		 * Extract vlan tag if hardware does not do it for us
		 */
		vlan_ether_decap(&m);
		if (m == NULL)
			return;
		eh = mtod(m, struct ether_header *);
	}
	ether_type = ntohs(eh->ether_type);

	if ((m->m_flags & M_VLANTAG) && ether_type == ETHERTYPE_VLAN) {
		/*
		 * To prevent possible dangerous recursion,
		 * we don't do vlan-in-vlan
		 */
		ifp->if_noproto++;
		m_freem(m);
		return;
	}
	KKASSERT(ether_type != ETHERTYPE_VLAN);

	/*
	 * Map ether type to netisr id.
	 */
	switch (ether_type) {
#ifdef INET
	case ETHERTYPE_IP:
		isr = NETISR_IP;
		break;

	case ETHERTYPE_ARP:
		isr = NETISR_ARP;
		break;
#endif

#ifdef INET6
	case ETHERTYPE_IPV6:
		isr = NETISR_IPV6;
		break;
#endif

#ifdef IPX
	case ETHERTYPE_IPX:
		isr = NETISR_IPX;
		break;
#endif

#ifdef NS
	case 0x8137: /* Novell Ethernet_II Ethernet TYPE II */
		isr = NETISR_NS;
		break;
#endif

#ifdef NETATALK
	case ETHERTYPE_AT:
		isr = NETISR_ATALK1;
		break;
	case ETHERTYPE_AARP:
		isr = NETISR_AARP;
		break;
#endif

	default:
		/*
		 * NETISR_MAX is an invalid value; it is chosen to let
		 * ether_mport() know that we are not able to decide
		 * this packet's msgport here.
		 */
		isr = NETISR_MAX;
		break;
	}

	/*
	 * If the packet is in contiguous memory, following
	 * m_adj() could ensure that the hidden ether header
	 * will not be destroyed, else we will have to save
	 * the ether header for the later restoration.
	 */
	if (m->m_pkthdr.len != m->m_len) {
		save_eh0 = *eh;
		save_eh = &save_eh0;
	} else {
		save_eh = NULL;
	}

	/*
	 * Temporarily remove ether header; ether_mport()
	 * expects a packet without ether header.
	 */
	m_adj(m, sizeof(struct ether_header));

	/*
	 * Find the packet's target msgport.
	 */
	port = ether_mport(isr, &m);
	if (port == NULL) {
		KKASSERT(m == NULL);
		return;
	}

	/*
	 * Restore ether header.
	 */
	if (save_eh != NULL) {
		ether_restore_header(&m, eh, save_eh);
		if (m == NULL)
			return;
	} else {
		m->m_data -= ETHER_HDR_LEN;
		m->m_len += ETHER_HDR_LEN;
		m->m_pkthdr.len += ETHER_HDR_LEN;
	}

	/*
	 * Initialize mbuf's netmsg packet _after_ possible
	 * ether header restoration, else the initialized
	 * netmsg packet may be lost during ether header
	 * restoration.
	 */
	ether_init_netpacket(isr, m);

#ifdef ETHER_INPUT_CHAIN
	if (chain != NULL) {
		struct mbuf_chain *c;
		int cpuid;

		m->m_pkthdr.header = port; /* XXX */
		cpuid = port->mpu_td->td_gd->gd_cpuid;

		c = &chain[cpuid];
		if (c->mc_head == NULL) {
			c->mc_head = c->mc_tail = m;
		} else {
			c->mc_tail->m_nextpkt = m;
			c->mc_tail = m;
		}
		m->m_nextpkt = NULL;
	} else
#endif	/* ETHER_INPUT_CHAIN */
		lwkt_sendmsg(port, &m->m_hdr.mh_netmsg.nm_netmsg.nm_lmsg);
}

#endif	/* ETHER_INPUT2 */
