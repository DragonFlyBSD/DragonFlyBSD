/*
 * Copyright (c) 2004, 2005 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Jeffrey M. Hsu.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
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

/*
 * Copyright (c) 1982, 1986, 1988, 1993
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
 *	@(#)if_ether.c	8.1 (Berkeley) 6/10/93
 * $FreeBSD: src/sys/netinet/if_ether.c,v 1.64.2.23 2003/04/11 07:23:15 fjoe Exp $
 */

/*
 * Ethernet address resolution protocol.
 * TODO:
 *	add "inuse/lock" bit (or ref. count) along with valid bit
 */

#include "opt_inet.h"
#include "opt_carp.h"

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/queue.h>
#include <sys/sysctl.h>
#include <sys/systm.h>
#include <sys/mbuf.h>
#include <sys/malloc.h>
#include <sys/socket.h>
#include <sys/syslog.h>
#include <sys/lock.h>

#include <net/if.h>
#include <net/if_dl.h>
#include <net/if_types.h>
#include <net/route.h>
#include <net/netisr.h>
#include <net/if_llc.h>

#include <netinet/in.h>
#include <netinet/in_var.h>
#include <netinet/if_ether.h>

#include <sys/thread2.h>
#include <sys/msgport2.h>
#include <net/netmsg2.h>
#include <net/netisr2.h>
#include <sys/mplock2.h>

#ifdef CARP
#include <netinet/ip_carp.h>
#endif

#define SIN(s) ((struct sockaddr_in *)s)
#define SDL(s) ((struct sockaddr_dl *)s)

SYSCTL_DECL(_net_link_ether);
SYSCTL_NODE(_net_link_ether, PF_INET, inet, CTLFLAG_RW, 0, "");

/* timer values */
static int arpt_prune = (5*60*1); /* walk list every 5 minutes */
static int arpt_keep = (20*60); /* once resolved, good for 20 more minutes */
static int arpt_down = 20;	/* once declared down, don't send for 20 sec */

SYSCTL_INT(_net_link_ether_inet, OID_AUTO, prune_intvl, CTLFLAG_RW,
	   &arpt_prune, 0, "");
SYSCTL_INT(_net_link_ether_inet, OID_AUTO, max_age, CTLFLAG_RW,
	   &arpt_keep, 0, "");
SYSCTL_INT(_net_link_ether_inet, OID_AUTO, host_down_time, CTLFLAG_RW,
	   &arpt_down, 0, "");

#define	rt_expire	rt_rmx.rmx_expire

struct llinfo_arp {
	LIST_ENTRY(llinfo_arp) la_le;
	struct	rtentry *la_rt;
	struct	mbuf *la_hold;	/* last packet until resolved/timeout */
	struct	lwkt_port *la_msgport; /* last packet's msgport */
	u_short	la_preempt;	/* countdown for pre-expiry arps */
	u_short	la_asked;	/* #times we QUERIED following expiration */
};

static	LIST_HEAD(, llinfo_arp) llinfo_arp_list[MAXCPU];

static int	arp_maxtries = 5;
static int	useloopback = 1; /* use loopback interface for local traffic */
static int	arp_proxyall = 0;
static int	arp_refresh = 60; /* refresh arp cache ~60 (not impl yet) */
static int	arp_restricted_match = 0;

SYSCTL_INT(_net_link_ether_inet, OID_AUTO, maxtries, CTLFLAG_RW,
	   &arp_maxtries, 0, "ARP resolution attempts before returning error");
SYSCTL_INT(_net_link_ether_inet, OID_AUTO, useloopback, CTLFLAG_RW,
	   &useloopback, 0, "Use the loopback interface for local traffic");
SYSCTL_INT(_net_link_ether_inet, OID_AUTO, proxyall, CTLFLAG_RW,
	   &arp_proxyall, 0, "Enable proxy ARP for all suitable requests");
SYSCTL_INT(_net_link_ether_inet, OID_AUTO, restricted_match, CTLFLAG_RW,
	   &arp_restricted_match, 0, "Only match against the sender");
SYSCTL_INT(_net_link_ether_inet, OID_AUTO, refresh, CTLFLAG_RW,
	   &arp_refresh, 0, "Preemptively refresh the ARP");

static void	arp_rtrequest(int, struct rtentry *);
static void	arprequest(struct ifnet *, const struct in_addr *,
			   const struct in_addr *, const u_char *);
static void	arprequest_async(struct ifnet *, const struct in_addr *,
				 const struct in_addr *, const u_char *);
static void	arpintr(netmsg_t msg);
static void	arptfree(struct llinfo_arp *);
static void	arptimer(void *);
static struct llinfo_arp *
		arplookup(in_addr_t, boolean_t, boolean_t, boolean_t);
#ifdef INET
static void	in_arpinput(struct mbuf *);
static void	in_arpreply(struct mbuf *m, in_addr_t, in_addr_t);
static void	arp_update_msghandler(netmsg_t);
static void	arp_reply_msghandler(netmsg_t);
#endif

static struct callout	arptimer_ch[MAXCPU];

/*
 * Timeout routine.  Age arp_tab entries periodically.
 */
/* ARGSUSED */
static void
arptimer(void *ignored_arg)
{
	struct llinfo_arp *la, *nla;

	crit_enter();
	LIST_FOREACH_MUTABLE(la, &llinfo_arp_list[mycpuid], la_le, nla) {
		if (la->la_rt->rt_expire && la->la_rt->rt_expire <= time_second)
			arptfree(la);
	}
	callout_reset(&arptimer_ch[mycpuid], arpt_prune * hz, arptimer, NULL);
	crit_exit();
}

/*
 * Parallel to llc_rtrequest.
 *
 * Called after a route is successfully added to the tree to fix-up the
 * route and initiate arp operations if required.
 */
static void
arp_rtrequest(int req, struct rtentry *rt)
{
	struct sockaddr *gate = rt->rt_gateway;
	struct llinfo_arp *la = rt->rt_llinfo;

	struct sockaddr_dl null_sdl = { sizeof null_sdl, AF_LINK };
	static boolean_t arpinit_done[MAXCPU];

	if (!arpinit_done[mycpuid]) {
		arpinit_done[mycpuid] = TRUE;
		callout_init(&arptimer_ch[mycpuid]);
		callout_reset(&arptimer_ch[mycpuid], hz, arptimer, NULL);
	}
	if (rt->rt_flags & RTF_GATEWAY)
		return;

	switch (req) {
	case RTM_ADD:
		/*
		 * XXX: If this is a manually added route to interface
		 * such as older version of routed or gated might provide,
		 * restore cloning bit.
		 */
		if (!(rt->rt_flags & RTF_HOST) &&
		    SIN(rt_mask(rt))->sin_addr.s_addr != 0xffffffff)
			rt->rt_flags |= RTF_CLONING;
		if (rt->rt_flags & RTF_CLONING) {
			/*
			 * Case 1: This route should come from a route to iface.
			 */
			rt_setgate(rt, rt_key(rt),
				   (struct sockaddr *)&null_sdl,
				   RTL_DONTREPORT);
			gate = rt->rt_gateway;
			SDL(gate)->sdl_type = rt->rt_ifp->if_type;
			SDL(gate)->sdl_index = rt->rt_ifp->if_index;
			rt->rt_expire = time_second;
			break;
		}
		/* Announce a new entry if requested. */
		if (rt->rt_flags & RTF_ANNOUNCE) {
			arprequest_async(rt->rt_ifp,
			    &SIN(rt_key(rt))->sin_addr,
			    &SIN(rt_key(rt))->sin_addr,
			    LLADDR(SDL(gate)));
		}
		/*FALLTHROUGH*/
	case RTM_RESOLVE:
		if (gate->sa_family != AF_LINK ||
		    gate->sa_len < sizeof(struct sockaddr_dl)) {
			log(LOG_DEBUG, "arp_rtrequest: bad gateway value\n");
			break;
		}
		SDL(gate)->sdl_type = rt->rt_ifp->if_type;
		SDL(gate)->sdl_index = rt->rt_ifp->if_index;
		if (la != NULL)
			break; /* This happens on a route change */
		/*
		 * Case 2:  This route may come from cloning, or a manual route
		 * add with a LL address.
		 */
		R_Malloc(la, struct llinfo_arp *, sizeof *la);
		rt->rt_llinfo = la;
		if (la == NULL) {
			log(LOG_DEBUG, "arp_rtrequest: malloc failed\n");
			break;
		}
		bzero(la, sizeof *la);
		la->la_rt = rt;
		rt->rt_flags |= RTF_LLINFO;
		LIST_INSERT_HEAD(&llinfo_arp_list[mycpuid], la, la_le);

#ifdef INET
		/*
		 * This keeps the multicast addresses from showing up
		 * in `arp -a' listings as unresolved.  It's not actually
		 * functional.  Then the same for broadcast.
		 */
		if (IN_MULTICAST(ntohl(SIN(rt_key(rt))->sin_addr.s_addr))) {
			ETHER_MAP_IP_MULTICAST(&SIN(rt_key(rt))->sin_addr,
					       LLADDR(SDL(gate)));
			SDL(gate)->sdl_alen = 6;
			rt->rt_expire = 0;
		}
		if (in_broadcast(SIN(rt_key(rt))->sin_addr, rt->rt_ifp)) {
			memcpy(LLADDR(SDL(gate)), rt->rt_ifp->if_broadcastaddr,
			       rt->rt_ifp->if_addrlen);
			SDL(gate)->sdl_alen = rt->rt_ifp->if_addrlen;
			rt->rt_expire = 0;
		}
#endif

		/*
		 * This fixes up the routing interface for local addresses.
		 * The route is adjusted to point at lo0 and the expiration
		 * timer is disabled.
		 *
		 * NOTE: This prevents locally targetted traffic from going
		 *	 out the hardware interface, which is inefficient
		 *	 and might not work if the hardware cannot listen
		 *	 to its own transmitted packets.   Setting
		 *	 net.link.ether.inet.useloopback to 0 will force
		 *	 packets for local addresses out the hardware (and
		 *	 it is expected to receive its own packet).
		 *
		 * XXX We should just be able to test RTF_LOCAL here instead
		 *     of having to compare IPs.
		 */
		if (SIN(rt_key(rt))->sin_addr.s_addr ==
		    (IA_SIN(rt->rt_ifa))->sin_addr.s_addr) {
			rt->rt_expire = 0;
			bcopy(IF_LLADDR(rt->rt_ifp), LLADDR(SDL(gate)),
			      SDL(gate)->sdl_alen = rt->rt_ifp->if_addrlen);
			if (useloopback)
				rt->rt_ifp = loif;
		}
		break;

	case RTM_DELETE:
		if (la == NULL)
			break;
		LIST_REMOVE(la, la_le);
		rt->rt_llinfo = NULL;
		rt->rt_flags &= ~RTF_LLINFO;
		if (la->la_hold != NULL)
			m_freem(la->la_hold);
		Free(la);
		break;
	}
}

static struct mbuf *
arpreq_alloc(struct ifnet *ifp, const struct in_addr *sip,
	     const struct in_addr *tip, const u_char *enaddr)
{
	struct mbuf *m;
	struct arphdr *ah;
	u_short ar_hrd;

	if ((m = m_gethdr(MB_DONTWAIT, MT_DATA)) == NULL)
		return NULL;
	m->m_pkthdr.rcvif = NULL;

	switch (ifp->if_type) {
	case IFT_ETHER:
		/*
		 * This may not be correct for types not explicitly
		 * listed, but this is our best guess
		 */
	default:
		ar_hrd = htons(ARPHRD_ETHER);

		m->m_len = arphdr_len2(ifp->if_addrlen, sizeof(struct in_addr));
		m->m_pkthdr.len = m->m_len;
		MH_ALIGN(m, m->m_len);

		ah = mtod(m, struct arphdr *);
		break;
	}

	ah->ar_hrd = ar_hrd;
	ah->ar_pro = htons(ETHERTYPE_IP);
	ah->ar_hln = ifp->if_addrlen;		/* hardware address length */
	ah->ar_pln = sizeof(struct in_addr);	/* protocol address length */
	ah->ar_op = htons(ARPOP_REQUEST);
	memcpy(ar_sha(ah), enaddr, ah->ar_hln);
	memset(ar_tha(ah), 0, ah->ar_hln);
	memcpy(ar_spa(ah), sip, ah->ar_pln);
	memcpy(ar_tpa(ah), tip, ah->ar_pln);

	return m;
}

static void
arpreq_send(struct ifnet *ifp, struct mbuf *m)
{
	struct sockaddr sa;
	struct ether_header *eh;

	switch (ifp->if_type) {
	case IFT_ETHER:
		/*
		 * This may not be correct for types not explicitly
		 * listed, but this is our best guess
		 */
	default:
		eh = (struct ether_header *)sa.sa_data;
		/* if_output() will not swap */
		eh->ether_type = htons(ETHERTYPE_ARP);
		memcpy(eh->ether_dhost, ifp->if_broadcastaddr, ifp->if_addrlen);
		break;
	}

	sa.sa_family = AF_UNSPEC;
	sa.sa_len = sizeof(sa);
	ifp->if_output(ifp, m, &sa, NULL);
}

static void
arpreq_send_handler(netmsg_t msg)
{
	struct mbuf *m = msg->packet.nm_packet;
	struct ifnet *ifp = msg->lmsg.u.ms_resultp;

	arpreq_send(ifp, m);
	/* nmsg was embedded in the mbuf, do not reply! */
}

/*
 * Broadcast an ARP request. Caller specifies:
 *	- arp header source ip address
 *	- arp header target ip address
 *	- arp header source ethernet address
 *
 * NOTE: Caller MUST NOT hold ifp's serializer
 */
static void
arprequest(struct ifnet *ifp, const struct in_addr *sip,
	   const struct in_addr *tip, const u_char *enaddr)
{
	struct mbuf *m;

	if (enaddr == NULL) {
		if (ifp->if_bridge) {
			enaddr = IF_LLADDR(ether_bridge_interface(ifp));
		} else {
			enaddr = IF_LLADDR(ifp);
		}
	}

	m = arpreq_alloc(ifp, sip, tip, enaddr);
	if (m == NULL)
		return;
	arpreq_send(ifp, m);
}

/*
 * Same as arprequest(), except:
 * - Caller is allowed to hold ifp's serializer
 * - Network output is done in protocol thead
 */
static void
arprequest_async(struct ifnet *ifp, const struct in_addr *sip,
		 const struct in_addr *tip, const u_char *enaddr)
{
	struct mbuf *m;
	struct netmsg_packet *pmsg;

	if (enaddr == NULL) {
		if (ifp->if_bridge) {
			enaddr = IF_LLADDR(ether_bridge_interface(ifp));
		} else {
			enaddr = IF_LLADDR(ifp);
		}
	}
	m = arpreq_alloc(ifp, sip, tip, enaddr);
	if (m == NULL)
		return;

	pmsg = &m->m_hdr.mh_netmsg;
	netmsg_init(&pmsg->base, NULL, &netisr_apanic_rport,
		    0, arpreq_send_handler);
	pmsg->nm_packet = m;
	pmsg->base.lmsg.u.ms_resultp = ifp;

	lwkt_sendmsg(netisr_cpuport(mycpuid), &pmsg->base.lmsg);
}

/*
 * Resolve an IP address into an ethernet address.  If success,
 * desten is filled in.  If there is no entry in arptab,
 * set one up and broadcast a request for the IP address.
 * Hold onto this mbuf and resend it once the address
 * is finally resolved.  A return value of 1 indicates
 * that desten has been filled in and the packet should be sent
 * normally; a 0 return indicates that the packet has been
 * taken over here, either now or for later transmission.
 */
int
arpresolve(struct ifnet *ifp, struct rtentry *rt0, struct mbuf *m,
	   struct sockaddr *dst, u_char *desten)
{
	struct rtentry *rt = NULL;
	struct llinfo_arp *la = NULL;
	struct sockaddr_dl *sdl;

	if (m->m_flags & M_BCAST) {	/* broadcast */
		memcpy(desten, ifp->if_broadcastaddr, ifp->if_addrlen);
		return (1);
	}
	if (m->m_flags & M_MCAST) {/* multicast */
		ETHER_MAP_IP_MULTICAST(&SIN(dst)->sin_addr, desten);
		return (1);
	}
	if (rt0 != NULL) {
		if (rt_llroute(dst, rt0, &rt) != 0) {
			m_freem(m);
			return 0;
		}
		la = rt->rt_llinfo;
	}
	if (la == NULL) {
		la = arplookup(SIN(dst)->sin_addr.s_addr,
			       TRUE, RTL_REPORTMSG, FALSE);
		if (la != NULL)
			rt = la->la_rt;
	}
	if (la == NULL || rt == NULL) {
		log(LOG_DEBUG, "arpresolve: can't allocate llinfo for %s%s%s\n",
		    inet_ntoa(SIN(dst)->sin_addr), la ? "la" : " ",
		    rt ? "rt" : "");
		m_freem(m);
		return (0);
	}
	sdl = SDL(rt->rt_gateway);
	/*
	 * Check the address family and length is valid, the address
	 * is resolved; otherwise, try to resolve.
	 */
	if ((rt->rt_expire == 0 || rt->rt_expire > time_second) &&
	    sdl->sdl_family == AF_LINK && sdl->sdl_alen != 0) {
		/*
		 * If entry has an expiry time and it is approaching,
		 * see if we need to send an ARP request within this
		 * arpt_down interval.
		 */
		if ((rt->rt_expire != 0) &&
		    (time_second + la->la_preempt > rt->rt_expire)) {
			arprequest(ifp,
				   &SIN(rt->rt_ifa->ifa_addr)->sin_addr,
				   &SIN(dst)->sin_addr,
				   NULL);
			la->la_preempt--;
		}

		bcopy(LLADDR(sdl), desten, sdl->sdl_alen);
		return 1;
	}
	/*
	 * If ARP is disabled or static on this interface, stop.
	 * XXX
	 * Probably should not allocate empty llinfo struct if we are
	 * not going to be sending out an arp request.
	 */
	if (ifp->if_flags & (IFF_NOARP | IFF_STATICARP)) {
		m_freem(m);
		return (0);
	}
	/*
	 * There is an arptab entry, but no ethernet address
	 * response yet.  Replace the held mbuf with this
	 * latest one.
	 */
	if (la->la_hold != NULL)
		m_freem(la->la_hold);
	la->la_hold = m;
	la->la_msgport = netisr_curport();
	if (rt->rt_expire || ((rt->rt_flags & RTF_STATIC) && !sdl->sdl_alen)) {
		rt->rt_flags &= ~RTF_REJECT;
		if (la->la_asked == 0 || rt->rt_expire != time_second) {
			rt->rt_expire = time_second;
			if (la->la_asked++ < arp_maxtries) {
				arprequest(ifp,
					   &SIN(rt->rt_ifa->ifa_addr)->sin_addr,
					   &SIN(dst)->sin_addr,
					   NULL);
			} else {
				rt->rt_flags |= RTF_REJECT;
				rt->rt_expire += arpt_down;
				la->la_asked = 0;
				la->la_preempt = arp_maxtries;
			}
		}
	}
	return (0);
}

/*
 * Common length and type checks are done here,
 * then the protocol-specific routine is called.
 */
static void
arpintr(netmsg_t msg)
{
	struct mbuf *m = msg->packet.nm_packet;
	struct arphdr *ar;
	u_short ar_hrd;
	char hexstr[6];

	if (m->m_len < sizeof(struct arphdr) &&
	    (m = m_pullup(m, sizeof(struct arphdr))) == NULL) {
		log(LOG_ERR, "arp: runt packet -- m_pullup failed\n");
		return;
	}
	ar = mtod(m, struct arphdr *);

	ar_hrd = ntohs(ar->ar_hrd);
	if (ar_hrd != ARPHRD_ETHER && ar_hrd != ARPHRD_IEEE802) {
		hexncpy((unsigned char *)&ar->ar_hrd, 2, hexstr, 5, NULL);
		log(LOG_ERR, "arp: unknown hardware address format (0x%s)\n",
		    hexstr);
		m_freem(m);
		return;
	}

	if (m->m_pkthdr.len < arphdr_len(ar)) {
		if ((m = m_pullup(m, arphdr_len(ar))) == NULL) {
			log(LOG_ERR, "arp: runt packet\n");
			return;
		}
		ar = mtod(m, struct arphdr *);
	}

	switch (ntohs(ar->ar_pro)) {
#ifdef INET
	case ETHERTYPE_IP:
		in_arpinput(m);
		return;
#endif
	}
	m_freem(m);
	/* msg was embedded in the mbuf, do not reply! */
}

#ifdef INET
/*
 * ARP for Internet protocols on 10 Mb/s Ethernet.
 * Algorithm is that given in RFC 826.
 * In addition, a sanity check is performed on the sender
 * protocol address, to catch impersonators.
 * We no longer handle negotiations for use of trailer protocol:
 * Formerly, ARP replied for protocol type ETHERTYPE_TRAIL sent
 * along with IP replies if we wanted trailers sent to us,
 * and also sent them in response to IP replies.
 * This allowed either end to announce the desire to receive
 * trailer packets.
 * We no longer reply to requests for ETHERTYPE_TRAIL protocol either,
 * but formerly didn't normally send requests.
 */

static int	log_arp_wrong_iface = 1;
static int	log_arp_movements = 1;
static int	log_arp_permanent_modify = 1;

SYSCTL_INT(_net_link_ether_inet, OID_AUTO, log_arp_wrong_iface, CTLFLAG_RW,
	   &log_arp_wrong_iface, 0,
	   "Log arp packets arriving on the wrong interface");
SYSCTL_INT(_net_link_ether_inet, OID_AUTO, log_arp_movements, CTLFLAG_RW,
	   &log_arp_movements, 0,
	   "Log arp replies from MACs different than the one in the cache");
SYSCTL_INT(_net_link_ether_inet, OID_AUTO, log_arp_permanent_modify, CTLFLAG_RW,
	   &log_arp_permanent_modify, 0,
	   "Log arp replies from MACs different than the one "
	   "in the permanent arp entry");


static void
arp_hold_output(netmsg_t msg)
{
	struct mbuf *m = msg->packet.nm_packet;
	struct rtentry *rt;
	struct ifnet *ifp;

	rt = msg->lmsg.u.ms_resultp;
	ifp = m->m_pkthdr.rcvif;
	m->m_pkthdr.rcvif = NULL;

	ifp->if_output(ifp, m, rt_key(rt), rt);

	/* Drop the reference count bumped by the sender */
	RTFREE(rt);

	/* nmsg was embedded in the mbuf, do not reply! */
}

static void
arp_update_oncpu(struct mbuf *m, in_addr_t saddr, boolean_t create,
		 boolean_t generate_report, boolean_t dologging)
{
	struct arphdr *ah = mtod(m, struct arphdr *);
	struct ifnet *ifp = m->m_pkthdr.rcvif;
	struct llinfo_arp *la;
	struct sockaddr_dl *sdl;
	struct rtentry *rt;
	char hexstr[2][64];

	la = arplookup(saddr, create, generate_report, FALSE);
	if (la && (rt = la->la_rt) && (sdl = SDL(rt->rt_gateway))) {
		struct in_addr isaddr = { saddr };

		/*
		 * Normally arps coming in on the wrong interface are ignored,
		 * but if we are bridging and the two interfaces belong to
		 * the same bridge, or one is a member of the bridge which
		 * is the other, then it isn't an error.
		 */
		if (rt->rt_ifp != ifp) {
			/*
			 * (1) ifp and rt_ifp both members of same bridge
			 * (2) rt_ifp member of bridge ifp
			 * (3) ifp member of bridge rt_ifp
			 *
			 * Always replace rt_ifp with the bridge ifc.
			 */
			struct ifnet *nifp;

			if (ifp->if_bridge &&
			    rt->rt_ifp->if_bridge == ifp->if_bridge) {
				nifp = ether_bridge_interface(ifp);
			} else if (rt->rt_ifp->if_bridge &&
				   ether_bridge_interface(rt->rt_ifp) == ifp) {
				nifp = ifp;
			} else if (ifp->if_bridge &&
				   ether_bridge_interface(ifp) == rt->rt_ifp) {
				nifp = rt->rt_ifp;
			} else {
				nifp = NULL;
			}

			if ((log_arp_wrong_iface == 1 && nifp == NULL) ||
			    log_arp_wrong_iface == 2) {
				hexncpy((u_char *)ar_sha(ah), ifp->if_addrlen,
				    hexstr[0], HEX_NCPYLEN(ifp->if_addrlen), ":");
				log(LOG_ERR,
				    "arp: %s is on %s "
				    "but got reply from %s on %s\n",
				    inet_ntoa(isaddr),
				    rt->rt_ifp->if_xname, hexstr[0],
				    ifp->if_xname);
			}
			if (nifp == NULL)
				return;

			/*
			 * nifp is our man!  Replace rt_ifp and adjust
			 * the sdl.
			 */
			ifp = rt->rt_ifp = nifp;
			sdl->sdl_type = ifp->if_type;
			sdl->sdl_index = ifp->if_index;
		}
		if (sdl->sdl_alen &&
		    bcmp(ar_sha(ah), LLADDR(sdl), sdl->sdl_alen)) {
			if (rt->rt_expire != 0) {
				if (dologging && log_arp_movements) {
					hexncpy((u_char *)LLADDR(sdl), ifp->if_addrlen,
					    hexstr[0], HEX_NCPYLEN(ifp->if_addrlen), ":");
					hexncpy((u_char *)ar_sha(ah), ifp->if_addrlen,
					    hexstr[1], HEX_NCPYLEN(ifp->if_addrlen), ":");
			    		log(LOG_INFO,
					    "arp: %s moved from %s to %s on %s\n",
					    inet_ntoa(isaddr), hexstr[0], hexstr[1],
					    ifp->if_xname);
				}
			} else {
				if (dologging && log_arp_permanent_modify) {
					hexncpy((u_char *)ar_sha(ah), ifp->if_addrlen,
					    hexstr[0], HEX_NCPYLEN(ifp->if_addrlen), ":");
					log(LOG_ERR,
					"arp: %s attempts to modify "
					"permanent entry for %s on %s\n",
					hexstr[0], inet_ntoa(isaddr), ifp->if_xname);
				}
				return;
			}
		}
		/*
		 * sanity check for the address length.
		 * XXX this does not work for protocols with variable address
		 * length. -is
		 */
		if (dologging && sdl->sdl_alen && sdl->sdl_alen != ah->ar_hln) {
			hexncpy((u_char *)ar_sha(ah), ifp->if_addrlen,
			    hexstr[0], HEX_NCPYLEN(ifp->if_addrlen), ":");
			log(LOG_WARNING,
			    "arp from %s: new addr len %d, was %d",
			    hexstr[0], ah->ar_hln, sdl->sdl_alen);
		}
		if (ifp->if_addrlen != ah->ar_hln) {
			if (dologging) {
				hexncpy((u_char *)ar_sha(ah), ifp->if_addrlen,
				    hexstr[0], HEX_NCPYLEN(ifp->if_addrlen), ":");
				log(LOG_WARNING,
				"arp from %s: addr len: new %d, i/f %d "
				"(ignored)", hexstr[0],
				ah->ar_hln, ifp->if_addrlen);
			}
			return;
		}
		memcpy(LLADDR(sdl), ar_sha(ah), sdl->sdl_alen = ah->ar_hln);
		if (rt->rt_expire != 0) {
			rt->rt_expire = time_second + arpt_keep;
		}
		rt->rt_flags &= ~RTF_REJECT;
		la->la_asked = 0;
		la->la_preempt = arp_maxtries;

		/*
		 * This particular cpu might have been holding an mbuf
		 * pending ARP resolution.  If so, transmit the mbuf now.
		 */
		if (la->la_hold != NULL) {
			struct mbuf *m = la->la_hold;
			struct lwkt_port *port = la->la_msgport;
			struct netmsg_packet *pmsg;

			la->la_hold = NULL;
			la->la_msgport = NULL;

			m_adj(m, sizeof(struct ether_header));

			/*
			 * Make sure that this rtentry will not be freed
			 * before the packet is processed on the target
			 * msgport.  The reference count will be dropped
			 * in the handler associated with this packet.
			 */
			rt->rt_refcnt++;

			pmsg = &m->m_hdr.mh_netmsg;
			netmsg_init(&pmsg->base, NULL,
				    &netisr_apanic_rport,
				    MSGF_PRIORITY, arp_hold_output);
			pmsg->nm_packet = m;

			/* Record necessary information */
			m->m_pkthdr.rcvif = ifp;
			pmsg->base.lmsg.u.ms_resultp = rt;

			lwkt_sendmsg(port, &pmsg->base.lmsg);
		}
	}
}

/*
 * Called from arpintr() - this routine is run from a single cpu.
 */
static void
in_arpinput(struct mbuf *m)
{
	struct arphdr *ah;
	struct ifnet *ifp = m->m_pkthdr.rcvif;
	struct ifaddr_container *ifac;
	struct in_ifaddr_container *iac;
	struct in_ifaddr *ia = NULL;
	struct in_addr isaddr, itaddr, myaddr;
	uint8_t *enaddr = NULL;
	int req_len;
	char hexstr[64];

	req_len = arphdr_len2(ifp->if_addrlen, sizeof(struct in_addr));
	if (m->m_len < req_len && (m = m_pullup(m, req_len)) == NULL) {
		log(LOG_ERR, "in_arp: runt packet -- m_pullup failed\n");
		return;
	}

	ah = mtod(m, struct arphdr *);
	memcpy(&isaddr, ar_spa(ah), sizeof isaddr);
	memcpy(&itaddr, ar_tpa(ah), sizeof itaddr);

	/*
	 * Check both target and sender IP addresses:
	 *
	 * If we receive the packet on the interface owning the address,
	 * then accept the address.
	 *
	 * For a bridge, we accept the address if the receive interface and
	 * the interface owning the address are on the same bridge, and
	 * use the bridge MAC as the is-at response.  The bridge will be
	 * responsible for handling the packet.
	 *
	 * (0) Check target IP against CARP IPs
	 */
#ifdef CARP
	LIST_FOREACH(iac, INADDR_HASH(itaddr.s_addr), ia_hash) {
		int is_match = 0, is_parent = 0;

		ia = iac->ia;

		/* Skip all ia's which don't match */
		if (itaddr.s_addr != ia->ia_addr.sin_addr.s_addr)
			continue;

		if (ia->ia_ifp->if_type != IFT_CARP)
			continue;

		if (carp_parent(ia->ia_ifp) == ifp)
			is_parent = 1;
		if (is_parent || ia->ia_ifp == ifp)
			is_match = carp_iamatch(ia);

		if (is_match) {
			if (is_parent) {
				/*
				 * The parent interface will also receive
				 * the ethernet broadcast packets, e.g. ARP
				 * REQUEST, so if we could find a CARP
				 * interface of the parent that could match
				 * the target IP address, we then drop the
				 * packets, which is delieverd to us through
				 * the parent interface.
				 */
				m_freem(m);
				return;
			}
			goto match;
		}
	}
#endif	/* CARP */

	/*
	 * (1) Check target IP against our local IPs
	 */
	LIST_FOREACH(iac, INADDR_HASH(itaddr.s_addr), ia_hash) {
		ia = iac->ia;

		/* Skip all ia's which don't match */
		if (itaddr.s_addr != ia->ia_addr.sin_addr.s_addr)
			continue;

#ifdef CARP
		/* CARP interfaces are checked in (0) */
		if (ia->ia_ifp->if_type == IFT_CARP)
			continue;
#endif

		if (ifp->if_bridge && ia->ia_ifp &&
		    ifp->if_bridge == ia->ia_ifp->if_bridge) {
			ifp = ether_bridge_interface(ifp);
			goto match;
		}
		if (ia->ia_ifp && ia->ia_ifp->if_bridge &&
		    ether_bridge_interface(ia->ia_ifp) == ifp) {
			goto match;
		}
		if (ifp->if_bridge && ether_bridge_interface(ifp) ==
		    ia->ia_ifp) {
			goto match;
		}
		if (ia->ia_ifp == ifp) {
			goto match;
		}
	}

	/*
	 * (2) Check sender IP against our local IPs
	 */
	LIST_FOREACH(iac, INADDR_HASH(isaddr.s_addr), ia_hash) {
		ia = iac->ia;

		/* Skip all ia's which don't match */
		if (isaddr.s_addr != ia->ia_addr.sin_addr.s_addr)
			continue;

		if (ifp->if_bridge && ia->ia_ifp &&
		    ifp->if_bridge == ia->ia_ifp->if_bridge) {
			ifp = ether_bridge_interface(ifp);
			goto match;
		}
		if (ia->ia_ifp && ia->ia_ifp->if_bridge &&
		    ether_bridge_interface(ia->ia_ifp) == ifp) {
			goto match;
		}
		if (ifp->if_bridge && ether_bridge_interface(ifp) ==
		    ia->ia_ifp) {
			goto match;
		}

		if (ia->ia_ifp == ifp)
			goto match;
	}

	/*
	 * No match, use the first inet address on the receive interface
	 * as a dummy address for the rest of the function.
	 */
	TAILQ_FOREACH(ifac, &ifp->if_addrheads[mycpuid], ifa_link) {
		struct ifaddr *ifa = ifac->ifa;

		if (ifa->ifa_addr && ifa->ifa_addr->sa_family == AF_INET) {
			ia = ifatoia(ifa);
			goto match;
		}
	}

	/*
	 * If we got here, we didn't find any suitable interface,
	 * so drop the packet.
	 */
	m_freem(m);
	return;

match:
	if (!enaddr)
		enaddr = (uint8_t *)IF_LLADDR(ifp);
	myaddr = ia->ia_addr.sin_addr;
	if (!bcmp(ar_sha(ah), enaddr, ifp->if_addrlen)) {
		m_freem(m);	/* it's from me, ignore it. */
		return;
	}
	if (!bcmp(ar_sha(ah), ifp->if_broadcastaddr, ifp->if_addrlen)) {
		log(LOG_ERR,
		    "arp: link address is broadcast for IP address %s!\n",
		    inet_ntoa(isaddr));
		m_freem(m);
		return;
	}
	if (isaddr.s_addr == myaddr.s_addr && myaddr.s_addr != 0) {
		hexncpy((u_char *)ar_sha(ah), ifp->if_addrlen,
		    hexstr, HEX_NCPYLEN(ifp->if_addrlen), ":");
		log(LOG_ERR,
		   "arp: %s is using my IP address %s!\n",
		    hexstr, inet_ntoa(isaddr));
		itaddr = myaddr;
		goto reply;
	}
	if (ifp->if_flags & IFF_STATICARP)
		goto reply;

	/*
	 * When arp_restricted_match is true and the ARP response is not
	 * specifically targetted to me, ignore it.  Otherwise the entry
	 * timeout may be updated for an old MAC.
	 */
	if (arp_restricted_match && itaddr.s_addr != myaddr.s_addr) {
		m_freem(m);
		return;
	}

	/*
	 * Update all CPU's routing tables with this ARP packet.
	 *
	 * However, we only need to generate rtmsg on CPU0.
	 */
	KASSERT(&curthread->td_msgport == netisr_cpuport(0),
	    ("arp input not in netisr0, but on cpu%d", mycpuid));
	arp_update_oncpu(m, isaddr.s_addr, itaddr.s_addr == myaddr.s_addr,
	    RTL_REPORTMSG, TRUE);

	if (ncpus > 1) {
		struct netmsg_inarp *msg = &m->m_hdr.mh_arpmsg;

		netmsg_init(&msg->base, NULL, &netisr_apanic_rport,
		    0, arp_update_msghandler);
		msg->m = m;
		msg->saddr = isaddr.s_addr;
		msg->taddr = itaddr.s_addr;
		msg->myaddr = myaddr.s_addr;
		lwkt_sendmsg(netisr_cpuport(1), &msg->base.lmsg);
	} else {
		goto reply;
	}

	/*
	 * Just return here; after all CPUs's routing tables are
	 * properly updated by this ARP packet, an ARP reply will
	 * be generated if appropriate.
	 */
	return;
reply:
	in_arpreply(m, itaddr.s_addr, myaddr.s_addr);
}

static void
arp_reply_msghandler(netmsg_t msg)
{
	struct netmsg_inarp *rmsg = (struct netmsg_inarp *)msg;

	in_arpreply(rmsg->m, rmsg->taddr, rmsg->myaddr);
	/* Don't reply this netmsg; netmsg_inarp is embedded in mbuf */
}

static void
arp_update_msghandler(netmsg_t msg)
{
	struct netmsg_inarp *rmsg = (struct netmsg_inarp *)msg;
	int nextcpu;

	/*
	 * This message handler will be called on all of the APs;
	 * no need to generate rtmsg on them.
	 */
	KASSERT(mycpuid > 0, ("arp update msg on cpu%d", mycpuid));
	arp_update_oncpu(rmsg->m, rmsg->saddr, rmsg->taddr == rmsg->myaddr,
	    RTL_DONTREPORT, FALSE);

	nextcpu = mycpuid + 1;
	if (nextcpu < ncpus) {
		lwkt_forwardmsg(netisr_cpuport(nextcpu), &rmsg->base.lmsg);
	} else {
		struct mbuf *m = rmsg->m;
		in_addr_t saddr = rmsg->saddr;
		in_addr_t taddr = rmsg->taddr;
		in_addr_t myaddr = rmsg->myaddr;

		/*
		 * Dispatch this mbuf to netisr0 to perform ARP reply,
		 * if appropriate.
		 * NOTE: netmsg_inarp is embedded in this mbuf.
		 */
		netmsg_init(&rmsg->base, NULL, &netisr_apanic_rport,
		    0, arp_reply_msghandler);
		rmsg->m = m;
		rmsg->saddr = saddr;
		rmsg->taddr = taddr;
		rmsg->myaddr = myaddr;
		lwkt_sendmsg(netisr_cpuport(0), &rmsg->base.lmsg);
	}
}

static void
in_arpreply(struct mbuf *m, in_addr_t taddr, in_addr_t myaddr)
{
	struct ifnet *ifp = m->m_pkthdr.rcvif;
	const uint8_t *enaddr;
	struct arphdr *ah;
	struct sockaddr sa;
	struct ether_header *eh;

	ah = mtod(m, struct arphdr *);
	if (ntohs(ah->ar_op) != ARPOP_REQUEST) {
		m_freem(m);
		return;
	}

	enaddr = (const uint8_t *)IF_LLADDR(ifp);
	if (taddr == myaddr) {
		/* I am the target */
		memcpy(ar_tha(ah), ar_sha(ah), ah->ar_hln);
		memcpy(ar_sha(ah), enaddr, ah->ar_hln);
	} else {
		struct llinfo_arp *la;
		struct rtentry *rt;

		la = arplookup(taddr, FALSE, RTL_DONTREPORT, SIN_PROXY);
		if (la == NULL) {
			struct sockaddr_in sin;

			if (!arp_proxyall) {
				m_freem(m);
				return;
			}

			bzero(&sin, sizeof sin);
			sin.sin_family = AF_INET;
			sin.sin_len = sizeof sin;
			sin.sin_addr.s_addr = taddr;

			rt = rtpurelookup((struct sockaddr *)&sin);
			if (rt == NULL) {
				m_freem(m);
				return;
			}
			--rt->rt_refcnt;
			/*
			 * Don't send proxies for nodes on the same interface
			 * as this one came out of, or we'll get into a fight
			 * over who claims what Ether address.
			 */
			if (rt->rt_ifp == ifp) {
				m_freem(m);
				return;
			}
			memcpy(ar_tha(ah), ar_sha(ah), ah->ar_hln);
			memcpy(ar_sha(ah), enaddr, ah->ar_hln);
#ifdef DEBUG_PROXY
			kprintf("arp: proxying for %s\n", inet_ntoa(itaddr));
#endif
		} else {
			struct sockaddr_dl *sdl;

			rt = la->la_rt;
			memcpy(ar_tha(ah), ar_sha(ah), ah->ar_hln);
			sdl = SDL(rt->rt_gateway);
			memcpy(ar_sha(ah), LLADDR(sdl), ah->ar_hln);
		}
	}

	memcpy(ar_tpa(ah), ar_spa(ah), ah->ar_pln);
	memcpy(ar_spa(ah), &taddr, ah->ar_pln);
	ah->ar_op = htons(ARPOP_REPLY);
	ah->ar_pro = htons(ETHERTYPE_IP); /* let's be sure! */
	switch (ifp->if_type) {
	case IFT_ETHER:
		/*
		 * May not be correct for types not explictly
		 * listed, but it is our best guess.
		 */
	default:
		eh = (struct ether_header *)sa.sa_data;
		memcpy(eh->ether_dhost, ar_tha(ah), sizeof eh->ether_dhost);
		eh->ether_type = htons(ETHERTYPE_ARP);
		break;
	}
	sa.sa_family = AF_UNSPEC;
	sa.sa_len = sizeof sa;
	ifp->if_output(ifp, m, &sa, NULL);
}

#endif	/* INET */

/*
 * Free an arp entry.  If the arp entry is actively referenced or represents
 * a static entry we only clear it back to an unresolved state, otherwise
 * we destroy the entry entirely.
 *
 * Note that static entries are created when route add ... -interface is used
 * to create an interface route to a (direct) destination.
 */
static void
arptfree(struct llinfo_arp *la)
{
	struct rtentry *rt = la->la_rt;
	struct sockaddr_dl *sdl;

	if (rt == NULL)
		panic("arptfree");
	sdl = SDL(rt->rt_gateway);
	if (sdl != NULL &&
	    ((rt->rt_refcnt > 0 && sdl->sdl_family == AF_LINK) ||
	     (rt->rt_flags & RTF_STATIC))) {
		sdl->sdl_alen = 0;
		la->la_preempt = la->la_asked = 0;
		rt->rt_flags &= ~RTF_REJECT;
		return;
	}
	rtrequest(RTM_DELETE, rt_key(rt), NULL, rt_mask(rt), 0, NULL);
}

/*
 * Lookup or enter a new address in arptab.
 */
static struct llinfo_arp *
arplookup(in_addr_t addr, boolean_t create, boolean_t generate_report,
	  boolean_t proxy)
{
	struct rtentry *rt;
	struct sockaddr_inarp sin = { sizeof sin, AF_INET };
	const char *why = NULL;

	sin.sin_addr.s_addr = addr;
	sin.sin_other = proxy ? SIN_PROXY : 0;
	if (create) {
		rt = _rtlookup((struct sockaddr *)&sin,
			       generate_report, RTL_DOCLONE);
	} else {
		rt = rtpurelookup((struct sockaddr *)&sin);
	}
	if (rt == NULL)
		return (NULL);
	rt->rt_refcnt--;

	if (rt->rt_flags & RTF_GATEWAY)
		why = "host is not on local network";
	else if (!(rt->rt_flags & RTF_LLINFO))
		why = "could not allocate llinfo";
	else if (rt->rt_gateway->sa_family != AF_LINK)
		why = "gateway route is not ours";

	if (why) {
		if (create) {
			log(LOG_DEBUG, "arplookup %s failed: %s\n",
			    inet_ntoa(sin.sin_addr), why);
		}
		if (rt->rt_refcnt <= 0 && (rt->rt_flags & RTF_WASCLONED)) {
			/* No references to this route.  Purge it. */
			rtrequest(RTM_DELETE, rt_key(rt), rt->rt_gateway,
				  rt_mask(rt), rt->rt_flags, NULL);
		}
		return (NULL);
	}
	return (rt->rt_llinfo);
}

void
arp_ifinit(struct ifnet *ifp, struct ifaddr *ifa)
{
	ifa->ifa_rtrequest = arp_rtrequest;
	ifa->ifa_flags |= RTF_CLONING;
}

void
arp_gratuitous(struct ifnet *ifp, struct ifaddr *ifa)
{
	if (IA_SIN(ifa)->sin_addr.s_addr != INADDR_ANY) {
		arprequest_async(ifp, &IA_SIN(ifa)->sin_addr,
				 &IA_SIN(ifa)->sin_addr, NULL);
	}
}

static void
arp_ifaddr(void *arg __unused, struct ifnet *ifp,
    enum ifaddr_event event, struct ifaddr *ifa)
{
	if (ifa->ifa_rtrequest != arp_rtrequest) /* XXX need a generic way */
		return;
	if (ifa->ifa_addr->sa_family != AF_INET)
		return;
	if (event == IFADDR_EVENT_DELETE)
		return;

	/*
	 * - CARP interfaces will take care of gratuitous ARP themselves.
	 * - If we are the CARP interface's parent, don't send gratuitous
	 *   ARP to avoid unnecessary confusion.
	 */
#ifdef CARP
	if (ifp->if_type != IFT_CARP && ifp->if_carp == NULL)
#endif
	{
		arp_gratuitous(ifp, ifa);
	}
}

static void
arp_init(void)
{
	int cpu;

	for (cpu = 0; cpu < ncpus2; cpu++)
		LIST_INIT(&llinfo_arp_list[cpu]);

	netisr_register(NETISR_ARP, arpintr, NULL);

	EVENTHANDLER_REGISTER(ifaddr_event, arp_ifaddr, NULL,
	    EVENTHANDLER_PRI_LAST);
}

SYSINIT(arp, SI_SUB_PROTO_DOMAIN, SI_ORDER_ANY, arp_init, 0);
