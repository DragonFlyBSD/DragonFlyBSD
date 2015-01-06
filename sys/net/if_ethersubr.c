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
 * 3. Neither the name of the University nor the names of its contributors
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
 */

#include "opt_inet.h"
#include "opt_inet6.h"
#include "opt_mpls.h"
#include "opt_netgraph.h"
#include "opt_carp.h"
#include "opt_rss.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/globaldata.h>
#include <sys/kernel.h>
#include <sys/ktr.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/msgport.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/sysctl.h>
#include <sys/thread.h>

#include <sys/thread2.h>
#include <sys/mplock2.h>

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
#include <net/vlan/if_vlan_var.h>
#include <net/netmsg2.h>
#include <net/netisr2.h>

#if defined(INET) || defined(INET6)
#include <netinet/in.h>
#include <netinet/ip_var.h>
#include <netinet/tcp_var.h>
#include <netinet/if_ether.h>
#include <netinet/ip_flow.h>
#include <net/ipfw/ip_fw.h>
#include <net/dummynet/ip_dummynet.h>
#endif
#ifdef INET6
#include <netinet6/nd6.h>
#endif

#ifdef CARP
#include <netinet/ip_carp.h>
#endif

#ifdef MPLS
#include <netproto/mpls/mpls.h>
#endif

/* netgraph node hooks for ng_ether(4) */
void	(*ng_ether_input_p)(struct ifnet *ifp, struct mbuf **mp);
void	(*ng_ether_input_orphan_p)(struct ifnet *ifp, struct mbuf *m);
int	(*ng_ether_output_p)(struct ifnet *ifp, struct mbuf **mp);
void	(*ng_ether_attach_p)(struct ifnet *ifp);
void	(*ng_ether_detach_p)(struct ifnet *ifp);

void	(*vlan_input_p)(struct mbuf *);

static int ether_output(struct ifnet *, struct mbuf *, struct sockaddr *,
			struct rtentry *);
static void ether_restore_header(struct mbuf **, const struct ether_header *,
				 const struct ether_header *);
static int ether_characterize(struct mbuf **);
static void ether_dispatch(int, struct mbuf *, int);

/*
 * if_bridge support
 */
struct mbuf *(*bridge_input_p)(struct ifnet *, struct mbuf *);
int (*bridge_output_p)(struct ifnet *, struct mbuf *);
void (*bridge_dn_p)(struct mbuf *, struct ifnet *);
struct ifnet *(*bridge_interface_p)(void *if_bridge);

static int ether_resolvemulti(struct ifnet *, struct sockaddr **,
			      struct sockaddr *);

/*
 * if_lagg(4) support
 */
void	(*lagg_input_p)(struct ifnet *, struct mbuf *); 
int (*lagg_output_p)(struct ifnet *, struct mbuf *);

const uint8_t etherbroadcastaddr[ETHER_ADDR_LEN] = {
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff
};

#define gotoerr(e) do { error = (e); goto bad; } while (0)
#define IFP2AC(ifp) ((struct arpcom *)(ifp))

static boolean_t ether_ipfw_chk(struct mbuf **m0, struct ifnet *dst,
				struct ip_fw **rule,
				const struct ether_header *eh);

static int ether_ipfw;
static u_long ether_restore_hdr;
static u_long ether_prepend_hdr;
static u_long ether_input_wronghash;
static int ether_debug;

#ifdef RSS_DEBUG
static u_long ether_pktinfo_try;
static u_long ether_pktinfo_hit;
static u_long ether_rss_nopi;
static u_long ether_rss_nohash;
static u_long ether_input_requeue;
#endif
static u_long ether_input_wronghwhash;
static int ether_input_ckhash;

#define ETHER_TSOLEN_DEFAULT	(4 * ETHERMTU)

static int ether_tsolen_default = ETHER_TSOLEN_DEFAULT;
TUNABLE_INT("net.link.ether.tsolen", &ether_tsolen_default);

SYSCTL_DECL(_net_link);
SYSCTL_NODE(_net_link, IFT_ETHER, ether, CTLFLAG_RW, 0, "Ethernet");
SYSCTL_INT(_net_link_ether, OID_AUTO, debug, CTLFLAG_RW,
    &ether_debug, 0, "Ether debug");
SYSCTL_INT(_net_link_ether, OID_AUTO, ipfw, CTLFLAG_RW,
    &ether_ipfw, 0, "Pass ether pkts through firewall");
SYSCTL_ULONG(_net_link_ether, OID_AUTO, restore_hdr, CTLFLAG_RW,
    &ether_restore_hdr, 0, "# of ether header restoration");
SYSCTL_ULONG(_net_link_ether, OID_AUTO, prepend_hdr, CTLFLAG_RW,
    &ether_prepend_hdr, 0,
    "# of ether header restoration which prepends mbuf");
SYSCTL_ULONG(_net_link_ether, OID_AUTO, input_wronghash, CTLFLAG_RW,
    &ether_input_wronghash, 0, "# of input packets with wrong hash");
SYSCTL_INT(_net_link_ether, OID_AUTO, tsolen, CTLFLAG_RW,
    &ether_tsolen_default, 0, "Default max TSO length");

#ifdef RSS_DEBUG
SYSCTL_ULONG(_net_link_ether, OID_AUTO, rss_nopi, CTLFLAG_RW,
    &ether_rss_nopi, 0, "# of packets do not have pktinfo");
SYSCTL_ULONG(_net_link_ether, OID_AUTO, rss_nohash, CTLFLAG_RW,
    &ether_rss_nohash, 0, "# of packets do not have hash");
SYSCTL_ULONG(_net_link_ether, OID_AUTO, pktinfo_try, CTLFLAG_RW,
    &ether_pktinfo_try, 0,
    "# of tries to find packets' msgport using pktinfo");
SYSCTL_ULONG(_net_link_ether, OID_AUTO, pktinfo_hit, CTLFLAG_RW,
    &ether_pktinfo_hit, 0,
    "# of packets whose msgport are found using pktinfo");
SYSCTL_ULONG(_net_link_ether, OID_AUTO, input_requeue, CTLFLAG_RW,
    &ether_input_requeue, 0, "# of input packets gets requeued");
#endif
SYSCTL_ULONG(_net_link_ether, OID_AUTO, input_wronghwhash, CTLFLAG_RW,
    &ether_input_wronghwhash, 0, "# of input packets with wrong hw hash");
SYSCTL_INT(_net_link_ether, OID_AUTO, always_ckhash, CTLFLAG_RW,
    &ether_input_ckhash, 0, "always check hash");

#define ETHER_KTR_STR		"ifp=%p"
#define ETHER_KTR_ARGS	struct ifnet *ifp
#ifndef KTR_ETHERNET
#define KTR_ETHERNET		KTR_ALL
#endif
KTR_INFO_MASTER(ether);
KTR_INFO(KTR_ETHERNET, ether, pkt_beg, 0, ETHER_KTR_STR, ETHER_KTR_ARGS);
KTR_INFO(KTR_ETHERNET, ether, pkt_end, 1, ETHER_KTR_STR, ETHER_KTR_ARGS);
KTR_INFO(KTR_ETHERNET, ether, disp_beg, 2, ETHER_KTR_STR, ETHER_KTR_ARGS);
KTR_INFO(KTR_ETHERNET, ether, disp_end, 3, ETHER_KTR_STR, ETHER_KTR_ARGS);
#define logether(name, arg)	KTR_LOG(ether_ ## name, arg)

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

	ASSERT_IFNET_NOT_SERIALIZED_ALL(ifp);

	if (ifp->if_flags & IFF_MONITOR)
		gotoerr(ENETDOWN);
	if ((ifp->if_flags & (IFF_UP | IFF_RUNNING)) != (IFF_UP | IFF_RUNNING))
		gotoerr(ENETDOWN);

	M_PREPEND(m, sizeof(struct ether_header), MB_DONTWAIT);
	if (m == NULL)
		return (ENOBUFS);
	m->m_pkthdr.csum_lhlen = sizeof(struct ether_header);
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
#ifdef MPLS
		if (m->m_flags & M_MPLSLABELED)
			eh->ether_type = htons(ETHERTYPE_MPLS);
		else
#endif
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
#if 0 /* XXX */
	if (ifp->if_lagg) {
		KASSERT(lagg_output_p != NULL,
			("%s: if_lagg not loaded!", __func__));
		return lagg_output_p(ifp, m);
	}
#endif

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
				IFNET_STAT_INC(ifp, iqdrops, 1);
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
	if (ifp->if_type == IFT_CARP) {
		ifp = carp_parent(ifp);
		if (ifp == NULL)
			gotoerr(ENETUNREACH);

		ac = IFP2AC(ifp);

		/*
		 * Check precondition again
		 */
		ASSERT_IFNET_NOT_SERIALIZED_ALL(ifp);

		if (ifp->if_flags & IFF_MONITOR)
			gotoerr(ENETDOWN);
		if ((ifp->if_flags & (IFF_UP | IFF_RUNNING)) !=
		    (IFF_UP | IFF_RUNNING))
			gotoerr(ENETDOWN);
	}
#endif

	/* Handle ng_ether(4) processing, if any */
	if (ng_ether_output_p != NULL) {
		/*
		 * Hold BGL and recheck ng_ether_output_p
		 */
		get_mplock();
		if (ng_ether_output_p != NULL) {
			if ((error = ng_ether_output_p(ifp, &m)) != 0) {
				rel_mplock();
				goto bad;
			}
			if (m == NULL) {
				rel_mplock();
				return (0);
			}
		}
		rel_mplock();
	}

	/* Continue with link-layer output */
	return ether_output_frame(ifp, m);

bad:
	m_freem(m);
	return (error);
}

/*
 * Returns the bridge interface an ifp is associated
 * with.
 *
 * Only call if ifp->if_bridge != NULL.
 */
struct ifnet *
ether_bridge_interface(struct ifnet *ifp)
{
	if (bridge_interface_p)
		return(bridge_interface_p(ifp->if_bridge));
	return (ifp);
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

	ASSERT_IFNET_NOT_SERIALIZED_ALL(ifp);

	if (m->m_pkthdr.fw_flags & DUMMYNET_MBUF_TAGGED) {
		struct m_tag *mtag;

		/* Extract info from dummynet tag */
		mtag = m_tag_find(m, PACKET_TAG_DUMMYNET, NULL);
		KKASSERT(mtag != NULL);
		rule = ((struct dn_pkt *)m_tag_data(mtag))->dn_priv;
		KKASSERT(rule != NULL);

		m_tag_delete(m, mtag);
		m->m_pkthdr.fw_flags &= ~DUMMYNET_MBUF_TAGGED;
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
	struct ether_header save_eh = *eh;	/* might be a ptr in *m0 */
	struct ip_fw_args args;
	struct m_tag *mtag;
	struct mbuf *m;
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

	/*
	 * Clean up tags
	 */
	if ((mtag = m_tag_find(*m0, PACKET_TAG_IPFW_DIVERT, NULL)) != NULL)
		m_tag_delete(*m0, mtag);
	if ((*m0)->m_pkthdr.fw_flags & IPFORWARD_MBUF_TAGGED) {
		mtag = m_tag_find(*m0, PACKET_TAG_IPFORWARD, NULL);
		KKASSERT(mtag != NULL);
		m_tag_delete(*m0, mtag);
		(*m0)->m_pkthdr.fw_flags &= ~IPFORWARD_MBUF_TAGGED;
	}

	args.m = *m0;		/* the packet we are looking at		*/
	args.oif = dst;		/* destination, if any			*/
	args.rule = *rule;	/* matching rule to restart		*/
	args.eh = &save_eh;	/* MAC header for bridged/MAC packets	*/
	i = ip_fw_chk_ptr(&args);
	*m0 = args.m;
	*rule = args.rule;

	if (*m0 == NULL)
		return FALSE;

	switch (i) {
	case IP_FW_PASS:
		return TRUE;

	case IP_FW_DIVERT:
	case IP_FW_TEE:
	case IP_FW_DENY:
		/*
		 * XXX at some point add support for divert/forward actions.
		 * If none of the above matches, we have to drop the pkt.
		 */
		return FALSE;

	case IP_FW_DUMMYNET:
		/*
		 * Pass the pkt to dummynet, which consumes it.
		 */
		m = *m0;	/* pass the original to dummynet */
		*m0 = NULL;	/* and nothing back to the caller */

		ether_restore_header(&m, eh, &save_eh);
		if (m == NULL)
			return FALSE;

		ip_fw_dn_io_ptr(m, args.cookie,
				dst ? DN_TO_ETH_OUT: DN_TO_ETH_DEMUX, &args);
		ip_dn_queue(m);
		return FALSE;

	default:
		panic("unknown ipfw return value: %d", i);
	}
}

/*
 * Perform common duties while attaching to interface list
 */
void
ether_ifattach(struct ifnet *ifp, const uint8_t *lla,
    lwkt_serialize_t serializer)
{
	ether_ifattach_bpf(ifp, lla, DLT_EN10MB, sizeof(struct ether_header),
	    serializer);
}

void
ether_ifattach_bpf(struct ifnet *ifp, const uint8_t *lla,
    u_int dlt, u_int hdrlen, lwkt_serialize_t serializer)
{
	struct sockaddr_dl *sdl;
	char ethstr[ETHER_ADDRSTRLEN + 1];
	struct ifaltq *ifq;
	int i;

	ifp->if_type = IFT_ETHER;
	ifp->if_addrlen = ETHER_ADDR_LEN;
	ifp->if_hdrlen = ETHER_HDR_LEN;
	if_attach(ifp, serializer);
	ifq = &ifp->if_snd;
	for (i = 0; i < ifq->altq_subq_cnt; ++i) {
		struct ifaltq_subque *ifsq = ifq_get_subq(ifq, i);

		ifsq->ifsq_maxbcnt = ifsq->ifsq_maxlen *
		    (ETHER_MAX_LEN - ETHER_CRC_LEN);
	}
	ifp->if_mtu = ETHERMTU;
	if (ifp->if_tsolen <= 0) {
		if ((ether_tsolen_default / ETHERMTU) < 2) {
			kprintf("ether TSO maxlen %d -> %d\n",
			    ether_tsolen_default, ETHER_TSOLEN_DEFAULT);
			ether_tsolen_default = ETHER_TSOLEN_DEFAULT;
		}
		ifp->if_tsolen = ether_tsolen_default;
	}
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

	if_printf(ifp, "MAC address: %s\n", kether_ntoa(lla, ethstr));
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
ether_ioctl(struct ifnet *ifp, u_long command, caddr_t data)
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

	ASSERT_IFNET_SERIALIZED_ALL(ifp);

	switch (command) {
	case SIOCSIFADDR:
		switch (ifa->ifa_addr->sa_family) {
#ifdef INET
		case AF_INET:
			IF_INIT(ifp);	/* before arpwhohas */
			arp_ifinit(ifp, ifa);
			break;
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
#ifdef INET
	struct sockaddr_in *sin;
#endif
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
		*llsa = NULL;
		return 0;

#ifdef INET
	case AF_INET:
		sin = (struct sockaddr_in *)sa;
		if (!IN_MULTICAST(ntohl(sin->sin_addr.s_addr)))
			return EADDRNOTAVAIL;
		sdl = kmalloc(sizeof *sdl, M_IFMADDR, M_WAITOK | M_ZERO);
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
			*llsa = NULL;
			return 0;
		}
		if (!IN6_IS_ADDR_MULTICAST(&sin6->sin6_addr))
			return EADDRNOTAVAIL;
		sdl = kmalloc(sizeof *sdl, M_IFMADDR, M_WAITOK | M_ZERO);
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

/*
 * Upper layer processing for a received Ethernet packet.
 */
void
ether_demux_oncpu(struct ifnet *ifp, struct mbuf *m)
{
	struct ether_header *eh;
	int isr, discard = 0;
	u_short ether_type;
	struct ip_fw *rule = NULL;

	M_ASSERTPKTHDR(m);
	KASSERT(m->m_len >= ETHER_HDR_LEN,
		("ether header is not contiguous!"));

	eh = mtod(m, struct ether_header *);

	if (m->m_pkthdr.fw_flags & DUMMYNET_MBUF_TAGGED) {
		struct m_tag *mtag;

		/* Extract info from dummynet tag */
		mtag = m_tag_find(m, PACKET_TAG_DUMMYNET, NULL);
		KKASSERT(mtag != NULL);
		rule = ((struct dn_pkt *)m_tag_data(mtag))->dn_priv;
		KKASSERT(rule != NULL);

		m_tag_delete(m, mtag);
		m->m_pkthdr.fw_flags &= ~DUMMYNET_MBUF_TAGGED;

		/* packet is passing the second time */
		goto post_stats;
	}

	/*
	 * We got a packet which was unicast to a different Ethernet
	 * address.  If the driver is working properly, then this
	 * situation can only happen when the interface is in
	 * promiscuous mode.  We defer the packet discarding until the
	 * vlan processing is done, so that vlan/bridge or vlan/netgraph
	 * could work.
	 */
	if (((ifp->if_flags & (IFF_PROMISC | IFF_PPROMISC)) == IFF_PROMISC) &&
	    !ETHER_IS_MULTICAST(eh->ether_dhost) &&
	    bcmp(eh->ether_dhost, IFP2AC(ifp)->ac_enaddr, ETHER_ADDR_LEN)) {
		if (ether_debug & 1) {
			kprintf("%02x:%02x:%02x:%02x:%02x:%02x "
				"%02x:%02x:%02x:%02x:%02x:%02x "
				"%04x vs %02x:%02x:%02x:%02x:%02x:%02x\n",
				eh->ether_dhost[0],
				eh->ether_dhost[1],
				eh->ether_dhost[2],
				eh->ether_dhost[3],
				eh->ether_dhost[4],
				eh->ether_dhost[5],
				eh->ether_shost[0],
				eh->ether_shost[1],
				eh->ether_shost[2],
				eh->ether_shost[3],
				eh->ether_shost[4],
				eh->ether_shost[5],
				eh->ether_type,
				((u_char *)IFP2AC(ifp)->ac_enaddr)[0],
				((u_char *)IFP2AC(ifp)->ac_enaddr)[1],
				((u_char *)IFP2AC(ifp)->ac_enaddr)[2],
				((u_char *)IFP2AC(ifp)->ac_enaddr)[3],
				((u_char *)IFP2AC(ifp)->ac_enaddr)[4],
				((u_char *)IFP2AC(ifp)->ac_enaddr)[5]
			);
		}
		if ((ether_debug & 2) == 0)
			discard = 1;
	}

post_stats:
	if (IPFW_LOADED && ether_ipfw != 0 && !discard) {
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

        /* Handle input from a lagg(4) port */
        if (ifp->if_type == IFT_IEEE8023ADLAG) {
                KASSERT(lagg_input_p != NULL,
                    ("%s: if_lagg not loaded!", __func__));
                (*lagg_input_p)(ifp, m);
		return;
        }

	if (m->m_flags & M_VLANTAG) {
		void (*vlan_input_func)(struct mbuf *);

		vlan_input_func = vlan_input_p;
		if (vlan_input_func != NULL) {
			vlan_input_func(m);
		} else {
			IFNET_STAT_INC(m->m_pkthdr.rcvif, noproto, 1);
			m_freem(m);
		}
		return;
	}

	/*
	 * If we have been asked to discard this packet
	 * (e.g. not for us), drop it before entering
	 * the upper layer.
	 */
	if (discard) {
		m_freem(m);
		return;
	}

	/*
	 * Clear protocol specific flags,
	 * before entering the upper layer.
	 */
	m->m_flags &= ~M_ETHER_FLAGS;

	/* Strip ethernet header. */
	m_adj(m, sizeof(struct ether_header));

	switch (ether_type) {
#ifdef INET
	case ETHERTYPE_IP:
		if ((m->m_flags & M_LENCHECKED) == 0) {
			if (!ip_lengthcheck(&m, 0))
				return;
		}
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

#ifdef INET6
	case ETHERTYPE_IPV6:
		isr = NETISR_IPV6;
		break;
#endif

#ifdef MPLS
	case ETHERTYPE_MPLS:
	case ETHERTYPE_MPLS_MCAST:
		/* Should have been set by ether_input(). */
		KKASSERT(m->m_flags & M_MPLSLABELED);
		isr = NETISR_MPLS;
		break;
#endif

	default:
		/*
		 * The accurate msgport is not determined before
		 * we reach here, so recharacterize packet.
		 */
		m->m_flags &= ~M_HASH;
		if (ng_ether_input_orphan_p != NULL) {
			/*
			 * Put back the ethernet header so netgraph has a
			 * consistent view of inbound packets.
			 */
			M_PREPEND(m, ETHER_HDR_LEN, MB_DONTWAIT);
			if (m == NULL) {
				/*
				 * M_PREPEND frees the mbuf in case of failure.
				 */
				return;
			}
			/*
			 * Hold BGL and recheck ng_ether_input_orphan_p
			 */
			get_mplock();
			if (ng_ether_input_orphan_p != NULL) {
				ng_ether_input_orphan_p(ifp, m);
				rel_mplock();
				return;
			}
			rel_mplock();
		}
		m_freem(m);
		return;
	}

	if (m->m_flags & M_HASH) {
		if (&curthread->td_msgport ==
		    netisr_hashport(m->m_pkthdr.hash)) {
			netisr_handle(isr, m);
			return;
		} else {
			/*
			 * XXX Something is wrong,
			 * we probably should panic here!
			 */
			m->m_flags &= ~M_HASH;
			atomic_add_long(&ether_input_wronghash, 1);
		}
	}
#ifdef RSS_DEBUG
	atomic_add_long(&ether_input_requeue, 1);
#endif
	netisr_queue(isr, m);
}

/*
 * First we perform any link layer operations, then continue to the
 * upper layers with ether_demux_oncpu().
 */
static void
ether_input_oncpu(struct ifnet *ifp, struct mbuf *m)
{
#ifdef CARP
	void *carp;
#endif

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

		if(m->m_flags & M_ETHER_BRIDGED) {
			m->m_flags &= ~M_ETHER_BRIDGED;
		} else {
			m = bridge_input_p(ifp, m);
			if (m == NULL)
				return;

			KASSERT(ifp == m->m_pkthdr.rcvif,
				("bridge_input_p changed rcvif"));
		}
	}

#ifdef CARP
	carp = ifp->if_carp;
	if (carp) {
		m = carp_input(carp, m);
		if (m == NULL)
			return;
		KASSERT(ifp == m->m_pkthdr.rcvif,
		    ("carp_input changed rcvif"));
	}
#endif

	/* Handle ng_ether(4) processing, if any */
	if (ng_ether_input_p != NULL) {
		/*
		 * Hold BGL and recheck ng_ether_input_p
		 */
		get_mplock();
		if (ng_ether_input_p != NULL)
			ng_ether_input_p(ifp, &m);
		rel_mplock();

		if (m == NULL)
			return;
	}

	/* Continue with upper layer processing */
	ether_demux_oncpu(ifp, m);
}

/*
 * Perform certain functions of ether_input():
 * - Test IFF_UP
 * - Update statistics
 * - Run bpf(4) tap if requested
 * Then pass the packet to ether_input_oncpu().
 *
 * This function should be used by pseudo interface (e.g. vlan(4)),
 * when it tries to claim that the packet is received by it.
 *
 * REINPUT_KEEPRCVIF
 * REINPUT_RUNBPF
 */
void
ether_reinput_oncpu(struct ifnet *ifp, struct mbuf *m, int reinput_flags)
{
	/* Discard packet if interface is not up */
	if (!(ifp->if_flags & IFF_UP)) {
		m_freem(m);
		return;
	}

	/*
	 * Change receiving interface.  The bridge will often pass a flag to
	 * ask that this not be done so ARPs get applied to the correct
	 * side.
	 */
	if ((reinput_flags & REINPUT_KEEPRCVIF) == 0 ||
	    m->m_pkthdr.rcvif == NULL) {
		m->m_pkthdr.rcvif = ifp;
	}

	/* Update statistics */
	IFNET_STAT_INC(ifp, ipackets, 1);
	IFNET_STAT_INC(ifp, ibytes, m->m_pkthdr.len);
	if (m->m_flags & (M_MCAST | M_BCAST))
		IFNET_STAT_INC(ifp, imcasts, 1);

	if (reinput_flags & REINPUT_RUNBPF)
		BPF_MTAP(ifp, m);

	ether_input_oncpu(ifp, m);
}

static __inline boolean_t
ether_vlancheck(struct mbuf **m0)
{
	struct mbuf *m = *m0;
	struct ether_header *eh;
	uint16_t ether_type;

	eh = mtod(m, struct ether_header *);
	ether_type = ntohs(eh->ether_type);

	if (ether_type == ETHERTYPE_VLAN && (m->m_flags & M_VLANTAG) == 0) {
		/*
		 * Extract vlan tag if hardware does not do it for us
		 */
		vlan_ether_decap(&m);
		if (m == NULL)
			goto failed;

		eh = mtod(m, struct ether_header *);
		ether_type = ntohs(eh->ether_type);
	}

	if (ether_type == ETHERTYPE_VLAN && (m->m_flags & M_VLANTAG)) {
		/*
		 * To prevent possible dangerous recursion,
		 * we don't do vlan-in-vlan
		 */
		IFNET_STAT_INC(m->m_pkthdr.rcvif, noproto, 1);
		goto failed;
	}
	KKASSERT(ether_type != ETHERTYPE_VLAN);

	m->m_flags |= M_ETHER_VLANCHECKED;
	*m0 = m;
	return TRUE;
failed:
	if (m != NULL)
		m_freem(m);
	*m0 = NULL;
	return FALSE;
}

static void
ether_input_handler(netmsg_t nmsg)
{
	struct netmsg_packet *nmp = &nmsg->packet;	/* actual size */
	struct ether_header *eh;
	struct ifnet *ifp;
	struct mbuf *m;

	m = nmp->nm_packet;
	M_ASSERTPKTHDR(m);

	if ((m->m_flags & M_ETHER_VLANCHECKED) == 0) {
		if (!ether_vlancheck(&m)) {
			KKASSERT(m == NULL);
			return;
		}
	}
	if ((m->m_flags & (M_HASH | M_CKHASH)) == (M_HASH | M_CKHASH) ||
	    __predict_false(ether_input_ckhash)) {
		int isr;

		/*
		 * Need to verify the hash supplied by the hardware
		 * which could be wrong.
		 */
		m->m_flags &= ~(M_HASH | M_CKHASH);
		isr = ether_characterize(&m);
		if (m == NULL)
			return;
		KKASSERT(m->m_flags & M_HASH);

		if (netisr_hashcpu(m->m_pkthdr.hash) != mycpuid) {
			/*
			 * Wrong hardware supplied hash; redispatch
			 */
			ether_dispatch(isr, m, -1);
			if (__predict_false(ether_input_ckhash))
				atomic_add_long(&ether_input_wronghwhash, 1);
			return;
		}
	}
	ifp = m->m_pkthdr.rcvif;

	eh = mtod(m, struct ether_header *);
	if (ETHER_IS_MULTICAST(eh->ether_dhost)) {
		if (bcmp(ifp->if_broadcastaddr, eh->ether_dhost,
			 ifp->if_addrlen) == 0)
			m->m_flags |= M_BCAST;
		else
			m->m_flags |= M_MCAST;
		IFNET_STAT_INC(ifp, imcasts, 1);
	}

	ether_input_oncpu(ifp, m);
}

/*
 * Send the packet to the target netisr msgport
 *
 * At this point the packet must be characterized (M_HASH set),
 * so we know which netisr to send it to.
 */
static void
ether_dispatch(int isr, struct mbuf *m, int cpuid)
{
	struct netmsg_packet *pmsg;
	int target_cpuid;

	KKASSERT(m->m_flags & M_HASH);
	target_cpuid = netisr_hashcpu(m->m_pkthdr.hash);

	pmsg = &m->m_hdr.mh_netmsg;
	netmsg_init(&pmsg->base, NULL, &netisr_apanic_rport,
		    0, ether_input_handler);
	pmsg->nm_packet = m;
	pmsg->base.lmsg.u.ms_result = isr;

	logether(disp_beg, NULL);
	if (target_cpuid == cpuid) {
		lwkt_sendmsg_oncpu(netisr_cpuport(target_cpuid),
		    &pmsg->base.lmsg);
	} else {
		lwkt_sendmsg(netisr_cpuport(target_cpuid),
		    &pmsg->base.lmsg);
	}
	logether(disp_end, NULL);
}

/*
 * Process a received Ethernet packet.
 *
 * The ethernet header is assumed to be in the mbuf so the caller
 * MUST MAKE SURE that there are at least sizeof(struct ether_header)
 * bytes in the first mbuf.
 *
 * If the caller knows that the current thread is stick to the current
 * cpu, e.g. the interrupt thread or the netisr thread, the current cpuid
 * (mycpuid) should be passed through 'cpuid' argument.  Else -1 should
 * be passed as 'cpuid' argument.
 */
void
ether_input(struct ifnet *ifp, struct mbuf *m, const struct pktinfo *pi,
    int cpuid)
{
	int isr;

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

	m->m_pkthdr.rcvif = ifp;

	logether(pkt_beg, ifp);

	ETHER_BPF_MTAP(ifp, m);

	IFNET_STAT_INC(ifp, ibytes, m->m_pkthdr.len);

	if (ifp->if_flags & IFF_MONITOR) {
		struct ether_header *eh;

		eh = mtod(m, struct ether_header *);
		if (ETHER_IS_MULTICAST(eh->ether_dhost))
			IFNET_STAT_INC(ifp, imcasts, 1);

		/*
		 * Interface marked for monitoring; discard packet.
		 */
		m_freem(m);

		logether(pkt_end, ifp);
		return;
	}

	/*
	 * If the packet has been characterized (pi->pi_netisr / M_HASH)
	 * we can dispatch it immediately with trivial checks.
	 */
	if (pi != NULL && (m->m_flags & M_HASH)) {
#ifdef RSS_DEBUG
		atomic_add_long(&ether_pktinfo_try, 1);
#endif
		netisr_hashcheck(pi->pi_netisr, m, pi);
		if (m->m_flags & M_HASH) {
			ether_dispatch(pi->pi_netisr, m, cpuid);
#ifdef RSS_DEBUG
			atomic_add_long(&ether_pktinfo_hit, 1);
#endif
			logether(pkt_end, ifp);
			return;
		}
	}
#ifdef RSS_DEBUG
	else if (ifp->if_capenable & IFCAP_RSS) {
		if (pi == NULL)
			atomic_add_long(&ether_rss_nopi, 1);
		else
			atomic_add_long(&ether_rss_nohash, 1);
	}
#endif

	/*
	 * Packet hash will be recalculated by software, so clear
	 * the M_HASH and M_CKHASH flag set by the driver; the hash
	 * value calculated by the hardware may not be exactly what
	 * we want.
	 */
	m->m_flags &= ~(M_HASH | M_CKHASH);

	if (!ether_vlancheck(&m)) {
		KKASSERT(m == NULL);
		logether(pkt_end, ifp);
		return;
	}

	isr = ether_characterize(&m);
	if (m == NULL) {
		logether(pkt_end, ifp);
		return;
	}

	/*
	 * Finally dispatch it
	 */
	ether_dispatch(isr, m, cpuid);

	logether(pkt_end, ifp);
}

static int
ether_characterize(struct mbuf **m0)
{
	struct mbuf *m = *m0;
	struct ether_header *eh;
	uint16_t ether_type;
	int isr;

	eh = mtod(m, struct ether_header *);
	ether_type = ntohs(eh->ether_type);

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

#ifdef MPLS
	case ETHERTYPE_MPLS:
	case ETHERTYPE_MPLS_MCAST:
		m->m_flags |= M_MPLSLABELED;
		isr = NETISR_MPLS;
		break;
#endif

	default:
		/*
		 * NETISR_MAX is an invalid value; it is chosen to let
		 * netisr_characterize() know that we have no clear
		 * idea where this packet should go.
		 */
		isr = NETISR_MAX;
		break;
	}

	/*
	 * Ask the isr to characterize the packet since we couldn't.
	 * This is an attempt to optimally get us onto the correct protocol
	 * thread.
	 */
	netisr_characterize(isr, &m, sizeof(struct ether_header));

	*m0 = m;
	return isr;
}

static void
ether_demux_handler(netmsg_t nmsg)
{
	struct netmsg_packet *nmp = &nmsg->packet;	/* actual size */
	struct ifnet *ifp;
	struct mbuf *m;

	m = nmp->nm_packet;
	M_ASSERTPKTHDR(m);
	ifp = m->m_pkthdr.rcvif;

	ether_demux_oncpu(ifp, m);
}

void
ether_demux(struct mbuf *m)
{
	struct netmsg_packet *pmsg;
	int isr;

	isr = ether_characterize(&m);
	if (m == NULL)
		return;

	KKASSERT(m->m_flags & M_HASH);
	pmsg = &m->m_hdr.mh_netmsg;
	netmsg_init(&pmsg->base, NULL, &netisr_apanic_rport,
	    0, ether_demux_handler);
	pmsg->nm_packet = m;
	pmsg->base.lmsg.u.ms_result = isr;

	lwkt_sendmsg(netisr_hashport(m->m_pkthdr.hash), &pmsg->base.lmsg);
}

u_char *
kether_aton(const char *macstr, u_char *addr)
{
        unsigned int o0, o1, o2, o3, o4, o5;
        int n;

        if (macstr == NULL || addr == NULL)
                return NULL;

        n = ksscanf(macstr, "%x:%x:%x:%x:%x:%x", &o0, &o1, &o2,
            &o3, &o4, &o5);
        if (n != 6)
                return NULL;

        addr[0] = o0;
        addr[1] = o1;
        addr[2] = o2;
        addr[3] = o3;
        addr[4] = o4;
        addr[5] = o5;

        return addr;
}

char *
kether_ntoa(const u_char *addr, char *buf)
{
        int len = ETHER_ADDRSTRLEN + 1;
        int n;

        n = ksnprintf(buf, len, "%02x:%02x:%02x:%02x:%02x:%02x", addr[0],
            addr[1], addr[2], addr[3], addr[4], addr[5]);

        if (n < 17)
                return NULL;

        return buf;
}

MODULE_VERSION(ether, 1);
