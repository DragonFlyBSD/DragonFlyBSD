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
 * $DragonFly: src/sys/net/if_ethersubr.c,v 1.23 2004/12/24 04:54:49 dillon Exp $
 */

#include "opt_atalk.h"
#include "opt_inet.h"
#include "opt_inet6.h"
#include "opt_ipx.h"
#include "opt_bdg.h"
#include "opt_netgraph.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/sysctl.h>

#include <net/if.h>
#include <net/netisr.h>
#include <net/route.h>
#include <net/if_llc.h>
#include <net/if_dl.h>
#include <net/if_types.h>
#include <net/bpf.h>
#include <net/ethernet.h>
#include <net/bridge/bridge.h>

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

#ifdef IPX
#include <netproto/ipx/ipx.h>
#include <netproto/ipx/ipx_if.h>
int (*ef_inputp)(struct ifnet*, struct ether_header *eh, struct mbuf *m);
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

/* netgraph node hooks for ng_ether(4) */
void	(*ng_ether_input_p)(struct ifnet *ifp,
		struct mbuf **mp, struct ether_header *eh);
void	(*ng_ether_input_orphan_p)(struct ifnet *ifp,
		struct mbuf *m, struct ether_header *eh);
int	(*ng_ether_output_p)(struct ifnet *ifp, struct mbuf **mp);
void	(*ng_ether_attach_p)(struct ifnet *ifp);
void	(*ng_ether_detach_p)(struct ifnet *ifp);

int	(*vlan_input_p)(struct ether_header *eh, struct mbuf *m);
int	(*vlan_input_tag_p)(struct mbuf *m, uint16_t t);

static int	ether_output(struct ifnet *, struct mbuf *, struct sockaddr *,
			     struct rtentry *);

/*
 * bridge support
 */
int do_bridge;
bridge_in_t *bridge_in_ptr;
bdg_forward_t *bdg_forward_ptr;
bdgtakeifaces_t *bdgtakeifaces_ptr;
struct bdg_softc *ifp2sc;

static	int ether_resolvemulti(struct ifnet *, struct sockaddr **,
		struct sockaddr *);
const uint8_t	etherbroadcastaddr[ETHER_ADDR_LEN] = {
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff
};

#define gotoerr(e) do { error = (e); goto bad;} while (0)
#define IFP2AC(ifp) ((struct arpcom *)(ifp))

int
ether_ipfw_chk(struct mbuf **m0, struct ifnet *dst,
	struct ip_fw **rule, struct ether_header *eh, int shared);
static int ether_ipfw;

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

	if ((ifp->if_flags & (IFF_UP | IFF_RUNNING)) != (IFF_UP | IFF_RUNNING))
		gotoerr(ENETDOWN);

	M_PREPEND(m, sizeof(struct ether_header), MB_DONTWAIT);
	if (m == NULL)
		gotoerr(ENOBUFS);
	eh = mtod(m, struct ether_header *);
	edst = eh->ether_dhost;

	/* Fill in the destination ethernet address and frame type. */
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
			return (0);		/* Something bad happened. */
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
			llc.llc_dsap = llc.llc_ssap = LLC_SNAP_LSAP;
			llc.llc_control = LLC_UI;
			bcopy(at_org_code, llc.llc_snap_org_code,
			      sizeof at_org_code);
			llc.llc_snap_ether_type = htons(ETHERTYPE_AT);
			bcopy(&llc, mtod(m, caddr_t), sizeof(struct llc));
			eh->ether_type = htons(m->m_pkthdr.len);
			hlen = sizeof(struct llc) + ETHER_HDR_LEN;
		} else {
			eh->ether_type = htons(ETHERTYPE_AT);
		}
		if (!aarpresolve(ac, m, (struct sockaddr_at *)dst, edst))
			return (0);
		break;
	  }
#endif /* NETATALK */
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
			eh->ether_type = htons(m->m_pkthdr.len);
			cp = mtod(m, u_char *);
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
#endif /* NS */
	case pseudo_AF_HDRCMPLT:
	case AF_UNSPEC:
		loop_copy = -1; /* if this is for us, don't do it */
		deh = (struct ether_header *)dst->sa_data;
		memcpy(edst, deh->ether_dhost, ETHER_ADDR_LEN);
		eh->ether_type = deh->ether_type;
		break;

	default:
		printf("%s: can't handle af%d\n", ifp->if_xname,
			dst->sa_family);
		gotoerr(EAFNOSUPPORT);
	}

	if (dst->sa_family == pseudo_AF_HDRCMPLT)	/* unlikely */
		memcpy(eh->ether_shost,
		       ((struct ether_header *)dst->sa_data)->ether_shost,
		       ETHER_ADDR_LEN);
	else
		memcpy(eh->ether_shost, ac->ac_enaddr, ETHER_ADDR_LEN);

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

	/* Handle ng_ether(4) processing, if any */
	if (ng_ether_output_p != NULL) {
		if ((error = (*ng_ether_output_p)(ifp, &m)) != 0) {
bad:			if (m != NULL)
				m_freem(m);
			return (error);
		}
		if (m == NULL)
			return (0);
	}

	/* Continue with link-layer output */
	return ether_output_frame(ifp, m);
}

/*
 * Ethernet link layer output routine to send a raw frame to the device.
 *
 * This assumes that the 14 byte Ethernet header is present and contiguous
 * in the first mbuf (if BRIDGE'ing).
 */
int
ether_output_frame(struct ifnet *ifp, struct mbuf *m)
{
	struct ip_fw *rule = NULL;
	int error = 0;
	int s;

	/* Extract info from dummynet tag, ignore others */
	for (; m->m_type == MT_TAG; m = m->m_next)
		if (m->m_flags == PACKET_TAG_DUMMYNET)
			rule = ((struct dn_pkt *)m)->rule;

	if (rule)	/* packet was already bridged */
		goto no_bridge;

	if (BDG_ACTIVE(ifp) ) {
		struct ether_header *eh; /* a ptr suffices */

		m->m_pkthdr.rcvif = NULL;
		eh = mtod(m, struct ether_header *);
		m_adj(m, ETHER_HDR_LEN);
		m = bdg_forward_ptr(m, eh, ifp);
		if (m != NULL)
			m_freem(m);
		return (0);
	}

no_bridge:
	s = splimp();
	if (IPFW_LOADED && ether_ipfw != 0) {
		struct ether_header save_eh, *eh;

		eh = mtod(m, struct ether_header *);
		save_eh = *eh;
		m_adj(m, ETHER_HDR_LEN);
		if (ether_ipfw_chk(&m, ifp, &rule, eh, 0) == 0) {
			if (m) {
				m_freem(m);
				return ENOBUFS; /* pkt dropped */
			} else
				return 0;	/* consumed e.g. in a pipe */
		}
		/* packet was ok, restore the ethernet header */
		if ((void *)(eh + 1) == (void *)m->m_data) {
			m->m_data -= ETHER_HDR_LEN ;
			m->m_len += ETHER_HDR_LEN ;
			m->m_pkthdr.len += ETHER_HDR_LEN ;
		} else {
			M_PREPEND(m, ETHER_HDR_LEN, MB_DONTWAIT);
			if (m == NULL) /* nope... */
				return ENOBUFS;
			bcopy(&save_eh, mtod(m, struct ether_header *),
			    ETHER_HDR_LEN);
		}
	}

	/*
	 * Queue message on interface, update output statistics if
	 * successful, and start output if interface not yet active.
	 */
	if (!IF_HANDOFF(&ifp->if_snd, m, ifp))
		error = ENOBUFS;
	splx(s);
	return (error);
}

/*
 * ipfw processing for ethernet packets (in and out).
 * The second parameter is NULL from ether_demux, and ifp from
 * ether_output_frame. This section of code could be used from
 * bridge.c as well as long as we use some extra info
 * to distinguish that case from ether_output_frame();
 */
int
ether_ipfw_chk(
	struct mbuf **m0,
	struct ifnet *dst,
	struct ip_fw **rule,
	struct ether_header *eh,
	int shared)
{
	struct ether_header save_eh = *eh;	/* might be a ptr in m */
	struct ip_fw_args args;
	int i;

	if (*rule != NULL && fw_one_pass)
		return 1; /* dummynet packet, already partially processed */

	/*
	 * I need some amt of data to be contiguous, and in case others need
	 * the packet (shared==1) also better be in the first mbuf.
	 */
	i = min((*m0)->m_pkthdr.len, max_protohdr);
	if (shared || (*m0)->m_len < i) {
		*m0 = m_pullup(*m0, i);
		if (*m0 == NULL)
			return 0;
	}

	args.m = *m0;		/* the packet we are looking at		*/
	args.oif = dst;		/* destination, if any			*/
	args.divert_rule = 0;	/* we do not support divert yet		*/
	args.rule = *rule;	/* matching rule to restart		*/
	args.next_hop = NULL;	/* we do not support forward yet	*/
	args.eh = &save_eh;	/* MAC header for bridged/MAC packets	*/
	i = ip_fw_chk_ptr(&args);
	*m0 = args.m;
	*rule = args.rule;

	if ((i & IP_FW_PORT_DENY_FLAG) || *m0 == NULL) /* drop */
		return 0;

	if (i == 0) /* a PASS rule.  */
		return 1;

	if (DUMMYNET_LOADED && (i & IP_FW_PORT_DYNT_FLAG)) {
		/*
		 * Pass the pkt to dummynet, which consumes it.
		 * If shared, make a copy and keep the original.
		 */
		struct mbuf *m ;

		if (shared) {
			m = m_copypacket(*m0, MB_DONTWAIT);
			if (m == NULL)
				return 0;
		} else {
			m = *m0 ; /* pass the original to dummynet */
			*m0 = NULL ; /* and nothing back to the caller */
		}
		/*
		 * Prepend the header, optimize for the common case of
		 * eh pointing into the mbuf.
		 */
		if ((void *)(eh + 1) == (void *)m->m_data) {
			m->m_data -= ETHER_HDR_LEN ;
			m->m_len += ETHER_HDR_LEN ;
			m->m_pkthdr.len += ETHER_HDR_LEN ;
		} else {
			M_PREPEND(m, ETHER_HDR_LEN, MB_DONTWAIT);
			if (m == NULL) /* nope... */
				return 0;
			bcopy(&save_eh, mtod(m, struct ether_header *),
			    ETHER_HDR_LEN);
		}
		ip_dn_io_ptr(m, (i & 0xffff),
			dst ? DN_TO_ETH_OUT: DN_TO_ETH_DEMUX, &args);
		return 0;
	}
	/*
	 * XXX at some point add support for divert/forward actions.
	 * If none of the above matches, we have to drop the pkt.
	 */
	return 0;
}

/*
 * XXX merge this function with ether_input.
 */
static void
ether_input_internal(struct ifnet *ifp, struct mbuf *m)
{
	ether_input(ifp, NULL, m);
}

/*
 * Process a received Ethernet packet. We have two different interfaces:
 * one (conventional) assumes the packet in the mbuf, with the ethernet
 * header provided separately in *eh. The second one (new) has everything
 * in the mbuf, and we can tell it because eh == NULL.
 * The caller MUST MAKE SURE that there are at least
 * sizeof(struct ether_header) bytes in the first mbuf.
 *
 * This allows us to concentrate in one place a bunch of code which
 * is replicated in all device drivers. Also, many functions called
 * from ether_input() try to put the eh back into the mbuf, so we
 * can later propagate the 'contiguous packet' interface to them,
 * and handle the old interface just here.
 *
 * NOTA BENE: for many drivers "eh" is a pointer into the first mbuf or
 * cluster, right before m_data. So be very careful when working on m,
 * as you could destroy *eh !!
 *
 * First we perform any link layer operations, then continue
 * to the upper layers with ether_demux().
 */
void
ether_input(struct ifnet *ifp, struct ether_header *eh, struct mbuf *m)
{
	struct ether_header save_eh;

	if (eh == NULL) {
		if (m->m_len < sizeof(struct ether_header)) {
			/* XXX error in the caller. */
			m_freem(m);
			return;
		}
		m->m_pkthdr.rcvif = ifp;
		eh = mtod(m, struct ether_header *);
		m_adj(m, sizeof(struct ether_header));
		/* XXX */
		/* m->m_pkthdr.len = m->m_len; */
	}

	/* Check for a BPF tap */
	if (ifp->if_bpf != NULL) {
		struct m_hdr mh;

		/* This kludge is OK; BPF treats the "mbuf" as read-only */
		mh.mh_next = m;
		mh.mh_data = (char *)eh;
		mh.mh_len = ETHER_HDR_LEN;
		bpf_mtap(ifp, (struct mbuf *)&mh);
	}

	ifp->if_ibytes += m->m_pkthdr.len + sizeof (*eh);

	/* Handle ng_ether(4) processing, if any */
	if (ng_ether_input_p != NULL) {
		(*ng_ether_input_p)(ifp, &m, eh);
		if (m == NULL)
			return;
	}

	/* Check for bridging mode */
	if (BDG_ACTIVE(ifp) ) {
		struct ifnet *bif;

		/* Check with bridging code */
		if ((bif = bridge_in_ptr(ifp, eh)) == BDG_DROP) {
			m_freem(m);
			return;
		}
		if (bif != BDG_LOCAL) {
			save_eh = *eh ; /* because it might change */
			m = bdg_forward_ptr(m, eh, bif); /* needs forwarding */
			/*
			 * Do not continue if bdg_forward_ptr() processed our
			 * packet (and cleared the mbuf pointer m) or if
			 * it dropped (m_free'd) the packet itself.
			 */
			if (m == NULL) {
			    if (bif == BDG_BCAST || bif == BDG_MCAST)
				printf("bdg_forward drop MULTICAST PKT\n");
			    return;
			}
			eh = &save_eh ;
		}
		if (bif == BDG_LOCAL || bif == BDG_BCAST || bif == BDG_MCAST)
			goto recvLocal;		/* receive locally */

		/* If not local and not multicast, just drop it */
		if (m != NULL)
			m_freem(m);
		return;
	}

recvLocal:
	/* Continue with upper layer processing */
	ether_demux(ifp, eh, m);
}

/*
 * Upper layer processing for a received Ethernet packet.
 */
void
ether_demux(struct ifnet *ifp, struct ether_header *eh, struct mbuf *m)
{
	int isr;
	u_short ether_type;
#if defined(NETATALK)
	struct llc *l;
#endif
	struct ip_fw *rule = NULL;

	/* Extract info from dummynet tag, ignore others */
	for (;m->m_type == MT_TAG; m = m->m_next)
		if (m->m_flags == PACKET_TAG_DUMMYNET) {
			rule = ((struct dn_pkt *)m)->rule;
			ifp = m->m_next->m_pkthdr.rcvif;
		}

	if (rule)	/* packet was already bridged */
		goto post_stats;

	/*
	 * Discard packet if upper layers shouldn't see it because
	 * it was unicast to a different Ethernet address.  If the
	 * driver is working properly, then this situation can only
	 * happen when the interface is in promiscuous mode.
	 */
	if (!BDG_ACTIVE(ifp) &&
	    ((ifp->if_flags & (IFF_PROMISC | IFF_PPROMISC)) == IFF_PROMISC) &&
	    (eh->ether_dhost[0] & 1) == 0 &&
	    bcmp(eh->ether_dhost, IFP2AC(ifp)->ac_enaddr, ETHER_ADDR_LEN)) {
		m_freem(m);
		return;
	}
	/* Discard packet if interface is not up */
	if (!(ifp->if_flags & IFF_UP)) {
		m_freem(m);
		return;
	}
	if (eh->ether_dhost[0] & 1) {
		if (bcmp(ifp->if_broadcastaddr, eh->ether_dhost,
			 ifp->if_addrlen) == 0)
			m->m_flags |= M_BCAST;
		else
			m->m_flags |= M_MCAST;
	}
	if (m->m_flags & (M_BCAST|M_MCAST))
		ifp->if_imcasts++;

post_stats:
	if (IPFW_LOADED && ether_ipfw != 0) {
		if (ether_ipfw_chk(&m, NULL, &rule, eh, 0 ) == 0) {
			if (m)
				m_freem(m);
			return;
		}
	}

	ether_type = ntohs(eh->ether_type);

	switch (ether_type) {
#ifdef INET
	case ETHERTYPE_IP:
		if (ipflow_fastforward(m))
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
#ifdef IPX
	case ETHERTYPE_IPX:
		if (ef_inputp && ef_inputp(ifp, eh, m) == 0)
			return;
		isr = NETISR_IPX;
		break;
#endif
#ifdef INET6
	case ETHERTYPE_IPV6:
		isr = NETISR_IPV6;
		break;
#endif
#ifdef NS
	case 0x8137: /* Novell Ethernet_II Ethernet TYPE II */
		isr = NETISR_NS;
		break;

#endif /* NS */
#ifdef NETATALK
	case ETHERTYPE_AT:
		isr = NETISR_ATALK1;
		break;
	case ETHERTYPE_AARP:
		isr = NETISR_AARP;
		break;
#endif /* NETATALK */
	case ETHERTYPE_VLAN:
		/* XXX lock ? */
		if (vlan_input_p != NULL)
			(*vlan_input_p)(eh, m);
		else {
			m->m_pkthdr.rcvif->if_noproto++;
			m_freem(m);
		}
		/* XXX unlock ? */
		return;
	default:
#ifdef IPX
		if (ef_inputp && ef_inputp(ifp, eh, m) == 0)
			return;
#endif /* IPX */
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
#endif /* NS */
#ifdef NETATALK
		if (ether_type > ETHERMTU)
			goto dropanyway;
		l = mtod(m, struct llc *);
		if (l->llc_dsap == LLC_SNAP_LSAP &&
		    l->llc_ssap == LLC_SNAP_LSAP &&
		    l->llc_control == LLC_UI) {
			if (bcmp(&(l->llc_snap_org_code)[0], at_org_code,
			    sizeof(at_org_code)) == 0 &&
			    ntohs(l->llc_snap_ether_type) == ETHERTYPE_AT) {
				m_adj(m, sizeof(struct llc));
				isr = NETISR_ATALK2;
				break;
			}
			if (bcmp(&(l->llc_snap_org_code)[0], aarp_org_code,
			    sizeof(aarp_org_code)) == 0 &&
			    ntohs(l->llc_snap_ether_type) == ETHERTYPE_AARP) {
				m_adj(m, sizeof(struct llc));
				isr = NETISR_AARP;
				break;
			}
		}
dropanyway:
#endif /* NETATALK */
		if (ng_ether_input_orphan_p != NULL)
			(*ng_ether_input_orphan_p)(ifp, m, eh);
		else
			m_freem(m);
		return;
	}
	netisr_dispatch(isr, m);
}

/*
 * Perform common duties while attaching to interface list
 */

void
ether_ifattach(struct ifnet *ifp, uint8_t *lla)
{
	ether_ifattach_bpf(ifp, lla, DLT_EN10MB, sizeof(struct ether_header));
}

void
ether_ifattach_bpf(struct ifnet *ifp, uint8_t *lla, u_int dlt, u_int hdrlen)
{
	struct ifaddr *ifa;
	struct sockaddr_dl *sdl;

	ifp->if_output = ether_output;
	ifp->if_input = ether_input_internal;
	ifp->if_type = IFT_ETHER;
	ifp->if_addrlen = ETHER_ADDR_LEN;
	ifp->if_broadcastaddr = etherbroadcastaddr;
	ifp->if_hdrlen = 14;
	if_attach(ifp);
	ifp->if_mtu = ETHERMTU;
	ifp->if_resolvemulti = ether_resolvemulti;
	if (ifp->if_baudrate == 0)
		ifp->if_baudrate = 10000000;
	ifa = ifnet_addrs[ifp->if_index - 1];
	KASSERT(ifa != NULL, ("%s: no lladdr!\n", __FUNCTION__));
	sdl = (struct sockaddr_dl *)ifa->ifa_addr;
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
	if (BDG_LOADED)
		bdgtakeifaces_ptr();

	if_printf(ifp, "MAC address: %6D\n", lla, ":");
}

/*
 * Perform common duties while detaching an Ethernet interface
 */
void
ether_ifdetach(struct ifnet *ifp)
{
	int s;

	s = splnet();
	if_down(ifp);
	splx(s);

	if (ng_ether_detach_p != NULL)
		(*ng_ether_detach_p)(ifp);
	bpfdetach(ifp);
	if_detach(ifp);
	if (BDG_LOADED)
		bdgtakeifaces_ptr();
}

SYSCTL_DECL(_net_link);
SYSCTL_NODE(_net_link, IFT_ETHER, ether, CTLFLAG_RW, 0, "Ethernet");
SYSCTL_INT(_net_link_ether, OID_AUTO, ipfw, CTLFLAG_RW,
	    &ether_ipfw,0,"Pass ether pkts through firewall");

int
ether_ioctl(struct ifnet *ifp, int command, caddr_t data)
{
	struct ifaddr *ifa = (struct ifaddr *) data;
	struct ifreq *ifr = (struct ifreq *) data;
	int error = 0;

	switch (command) {
	case SIOCSIFADDR:
		ifp->if_flags |= IFF_UP;

		switch (ifa->ifa_addr->sa_family) {
#ifdef INET
		case AF_INET:
			ifp->if_init(ifp->if_softc);	/* before arpwhohas */
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

			ifp->if_init(ifp->if_softc);	/* Set new address. */
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
			ifp->if_init(ifp->if_softc);
			break;
		}
#endif
		default:
			ifp->if_init(ifp->if_softc);
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
		       M_WAITOK|M_ZERO);
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
		       M_WAITOK|M_ZERO);
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
