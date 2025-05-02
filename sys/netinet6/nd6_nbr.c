/*	$FreeBSD: src/sys/netinet6/nd6_nbr.c,v 1.4.2.6 2003/01/23 21:06:47 sam Exp $	*/
/*	$KAME: nd6_nbr.c,v 1.86 2002/01/21 02:33:04 jinmei Exp $	*/

/*
 * Copyright (C) 1995, 1996, 1997, and 1998 WIDE Project.
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
 * 3. Neither the name of the project nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE PROJECT AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE PROJECT OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include "opt_inet.h"
#include "opt_inet6.h"
#include "opt_carp.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/time.h>
#include <sys/kernel.h>
#include <sys/errno.h>
#include <sys/syslog.h>
#include <sys/queue.h>
#include <sys/callout.h>
#include <sys/mutex.h>
#include <sys/resourcevar.h>

#include <sys/thread2.h>
#include <sys/mutex2.h>

#include <net/if.h>
#include <net/if_types.h>
#include <net/if_dl.h>
#include <net/route.h>
#include <net/netisr2.h>
#include <net/netmsg2.h>

#include <netinet/in.h>
#include <netinet/in_var.h>
#include <netinet6/in6_var.h>
#include <netinet/ip6.h>
#include <netinet6/ip6_var.h>
#include <netinet6/nd6.h>
#include <netinet/icmp6.h>

#include <net/net_osdep.h>

#ifdef CARP
#include <netinet/ip_carp.h>
#endif


#define SDL(s) ((struct sockaddr_dl *)s)

struct dadq;
static struct dadq *nd6_dad_find(struct ifaddr *);
static void nd6_dad_starttimer(struct dadq *, int);
static void nd6_dad_stoptimer(struct dadq *);
static void nd6_dad_timer(void *);
static void nd6_dad_timer_handler(netmsg_t);
static void nd6_dad_ns_output(struct dadq *);
static void nd6_dad_ns_input(struct ifaddr *);
static void nd6_dad_na_input(struct ifaddr *);
static struct dadq *nd6_dad_create(struct ifaddr *);
static void nd6_dad_destroy(struct dadq *);
static void nd6_dad_duplicated(struct ifaddr *);

static int dad_ignore_ns = 0;	/* ignore NS in DAD - specwise incorrect*/
static int dad_maxtry = 15;	/* max # of *tries* to transmit DAD packet */

/*
 * Input an Neighbor Solicitation Message.
 *
 * Based on RFC 2461
 * Based on RFC 2462 (duplicated address detection)
 */
void
nd6_ns_input(struct mbuf *m, int off, int icmp6len)
{
	struct ifnet *ifp = m->m_pkthdr.rcvif;
	struct ifnet *cmpifp;
	struct ip6_hdr *ip6 = mtod(m, struct ip6_hdr *);
	struct nd_neighbor_solicit *nd_ns;
	struct in6_addr saddr6 = ip6->ip6_src;
	struct in6_addr daddr6 = ip6->ip6_dst;
	struct in6_addr taddr6;
	struct in6_addr myaddr6;
	char *lladdr = NULL;
	struct ifaddr *ifa = NULL;
	int lladdrlen = 0;
	int anycast = 0, proxy = 0, tentative = 0;
	int tlladdr;
	union nd_opts ndopts;
	struct sockaddr_dl *proxydl = NULL;

	/*
	 * Collapse interfaces to the bridge for comparison and
	 * mac (llinfo) purposes.
	 */
	cmpifp = ifp;
	if (ifp->if_bridge)
		cmpifp = ifp->if_bridge;

#ifndef PULLDOWN_TEST
	IP6_EXTHDR_CHECK(m, off, icmp6len,);
	nd_ns = (struct nd_neighbor_solicit *)((caddr_t)ip6 + off);
#else
	IP6_EXTHDR_GET(nd_ns, struct nd_neighbor_solicit *, m, off, icmp6len);
	if (nd_ns == NULL) {
		icmp6stat.icp6s_tooshort++;
		return;
	}
#endif
	ip6 = mtod(m, struct ip6_hdr *); /* adjust pointer for safety */
	taddr6 = nd_ns->nd_ns_target;

	if (ip6->ip6_hlim != 255) {
		nd6log((LOG_ERR,
		    "nd6_ns_input: invalid hlim (%d) from %s to %s on %s\n",
		    ip6->ip6_hlim, ip6_sprintf(&ip6->ip6_src),
		    ip6_sprintf(&ip6->ip6_dst), if_name(ifp)));
		goto bad;
	}

	if (IN6_IS_ADDR_UNSPECIFIED(&saddr6)) {
		/* dst has to be solicited node multicast address. */
		if (daddr6.s6_addr16[0] == IPV6_ADDR_INT16_MLL &&
		    /* don't check ifindex portion */
		    daddr6.s6_addr32[1] == 0 &&
		    daddr6.s6_addr32[2] == IPV6_ADDR_INT32_ONE &&
		    daddr6.s6_addr8[12] == 0xff) {
			; /* good */
		} else {
			nd6log((LOG_INFO, "nd6_ns_input: bad DAD packet "
			    "(wrong ip6 dst)\n"));
			goto bad;
		}
	} else if (!nd6_onlink_ns_rfc4861) {
		struct sockaddr_in6 src_sa6;

		/*
		 * According to recent IETF discussions, it is not a good idea
		 * to accept a NS from an address which would not be deemed
		 * to be a neighbor otherwise.  This point is expected to be
		 * clarified in future revisions of the specification.
		 */
		bzero(&src_sa6, sizeof(src_sa6));
		src_sa6.sin6_family = AF_INET6;
		src_sa6.sin6_len = sizeof(src_sa6);
		src_sa6.sin6_addr = saddr6;
		if (nd6_is_addr_neighbor(&src_sa6, ifp) == 0) {
			nd6log((LOG_INFO, "nd6_ns_input: "
			    "NS packet from non-neighbor\n"));
			goto bad;
		}
	}

	if (IN6_IS_ADDR_MULTICAST(&taddr6)) {
		nd6log((LOG_INFO, "nd6_ns_input: bad NS target (multicast)\n"));
		goto bad;
	}

	if (IN6_IS_SCOPE_LINKLOCAL(&taddr6))
		taddr6.s6_addr16[1] = htons(ifp->if_index);

	icmp6len -= sizeof(*nd_ns);
	nd6_option_init(nd_ns + 1, icmp6len, &ndopts);
	if (nd6_options(&ndopts) < 0) {
		nd6log((LOG_INFO,
		    "nd6_ns_input: invalid ND option, ignored\n"));
		/* nd6_options have incremented stats */
		goto freeit;
	}

	if (ndopts.nd_opts_src_lladdr) {
		lladdr = (char *)(ndopts.nd_opts_src_lladdr + 1);
		lladdrlen = ndopts.nd_opts_src_lladdr->nd_opt_len << 3;
	}

	if (IN6_IS_ADDR_UNSPECIFIED(&ip6->ip6_src) && lladdr) {
		nd6log((LOG_INFO, "nd6_ns_input: bad DAD packet "
		    "(link-layer address option)\n"));
		goto bad;
	}

	/*
	 * Attaching target link-layer address to the NA?
	 * (RFC 2461 7.2.4)
	 *
	 * NS IP dst is unicast/anycast			MUST NOT add
	 * NS IP dst is solicited-node multicast	MUST add
	 *
	 * In implementation, we add target link-layer address by default.
	 * We do not add one in MUST NOT cases.
	 */
#if 0 /* too much! */
	ifa = (struct ifaddr *)in6ifa_ifpwithaddr(ifp, &daddr6);
	if (ifa && (((struct in6_ifaddr *)ifa)->ia6_flags & IN6_IFF_ANYCAST))
		tlladdr = 0;
	else
#endif
	if (!IN6_IS_ADDR_MULTICAST(&daddr6))
		tlladdr = 0;
	else
		tlladdr = 1;

	/*
	 * Target address (taddr6) must be either:
	 * (1) Valid unicast/anycast address for my receiving interface.
	 * (2) Unicast or anycast address for which I'm offering proxy
	 *     service.
	 * (3) "tentative" address on which DAD is being performed.
	 */
	/* (1) and (3) check. */
#ifdef CARP
	if (ifp->if_carp)
		ifa = carp_iamatch6(ifp->if_carp, &taddr6);
	if (!ifa)
		ifa = (struct ifaddr *)in6ifa_ifpwithaddr(ifp, &taddr6);
#else
	ifa = (struct ifaddr *)in6ifa_ifpwithaddr(ifp, &taddr6);
#endif

	/*
	 * (2) Check proxying.  Requires ip6_forwarding to be turned on.
	 *
	 *     If the packet is anycast the target route must be on a
	 *     different interface because the anycast will get anything
	 *     on the current interface.
	 *
	 *     If the packet is unicast the target route may be on the
	 *     same interface.  If the gateway is a (typically manually
	 *     configured) link address we can directly offer it.
	 *     XXX for now we don't do this but instead offer ours and
	 *     presumably relay.
	 *
	 *     WARNING! Since this is a subnet proxy the interface proxying
	 *     the ND6 must be in promiscuous mode or it will not see the
	 *     solicited multicast requests for various hosts being proxied.
	 *
	 *     WARNING! Since this is a subnet proxy we have to treat bridge
	 *     interfaces as being the bridge itself so we do not proxy-nd6
	 *     between bridge interfaces (which are effectively switched).
	 *
	 *     (In the specific-host-proxy case via RTF_ANNOUNCE, which is
	 *     a bitch to configure, a specific multicast route is already
	 *     added for that host <-- NOT RECOMMENDED).
	 */
	if (!ifa && ip6_forwarding) {
		struct rtentry *rt;
		struct sockaddr_in6 tsin6;
		struct ifnet *rtifp;

		bzero(&tsin6, sizeof tsin6);
		tsin6.sin6_len = sizeof(struct sockaddr_in6);
		tsin6.sin6_family = AF_INET6;
		tsin6.sin6_addr = taddr6;

		rt = rtpurelookup((struct sockaddr *)&tsin6);
		rtifp = rt ? rt->rt_ifp : NULL;
		if (rtifp && rtifp->if_bridge)
			rtifp = rtifp->if_bridge;

		if (rt != NULL &&
		    ((rt->rt_flags & RTF_ANNOUNCE) == 0 &&
		     (rtifp->if_flags & IFF_ANNOUNCE) == 0)
		    ) {
			static struct krate krate_nff = { .freq = 1 };
			krateprintf(&krate_nff,
				    "route %p not flagged RTF_ANNOUNCE "
				    "rtifp %p "
				    "target %04x:%04x:%04x:%04x:"
				    "%04x:%04x:%04x:%04x\n",
				    rt,
				    rtifp,
				    taddr6.s6_addr16[0],
				    taddr6.s6_addr16[1],
				    taddr6.s6_addr16[2],
				    taddr6.s6_addr16[3],
				    taddr6.s6_addr16[4],
				    taddr6.s6_addr16[5],
				    taddr6.s6_addr16[6],
				    taddr6.s6_addr16[7]);
		}

		if (rt != NULL &&
		    ((rt->rt_flags & RTF_ANNOUNCE) ||
		     (rtifp->if_flags & IFF_ANNOUNCE)) &&
		    (cmpifp != rtifp || (m->m_flags & M_MCAST) == 0)) {
			ifa = (struct ifaddr *)in6ifa_ifpforlinklocal(cmpifp,
				IN6_IFF_NOTREADY|IN6_IFF_ANYCAST);
			nd6log((LOG_INFO,
			       "nd6_ns_input: nd6 proxy %s(%s)<-%s ifa %p\n",
			       if_name(cmpifp), if_name(ifp),
			       if_name(rtifp), ifa));
			if (ifa) {
				proxy = 1;
				/*
				 * Manual link address on same interface
				 * w/announce flag will proxy-arp using
				 * target mac, else our mac is used.
				 */
				if (cmpifp == rtifp &&
				    rt->rt_gateway->sa_family == AF_LINK) {
					proxydl = SDL(rt->rt_gateway);
				}
			}
		}
		if (rt != NULL)
			--rt->rt_refcnt;
	}
	if (ifa == NULL) {
		/*
		 * We've got an NS packet, and we don't have that adddress
		 * assigned for us.  We MUST silently ignore it.
		 * See RFC2461 7.2.3.
		 */
		goto freeit;
	}
	myaddr6 = *IFA_IN6(ifa);
	anycast = ((struct in6_ifaddr *)ifa)->ia6_flags & IN6_IFF_ANYCAST;
	tentative = ((struct in6_ifaddr *)ifa)->ia6_flags & IN6_IFF_TENTATIVE;
	if (((struct in6_ifaddr *)ifa)->ia6_flags & IN6_IFF_DUPLICATED)
		goto freeit;

	if (lladdr && ((cmpifp->if_addrlen + 2 + 7) & ~7) != lladdrlen) {
		nd6log((LOG_INFO, "nd6_ns_input: lladdrlen mismatch for %s "
		    "(if %d, NS packet %d)\n",
		    ip6_sprintf(&taddr6), cmpifp->if_addrlen, lladdrlen - 2));
		goto bad;
	}

	if (IN6_ARE_ADDR_EQUAL(&myaddr6, &saddr6)) {
		nd6log((LOG_INFO, "nd6_ns_input: duplicate IP6 address %s\n",
		    ip6_sprintf(&saddr6)));
		goto freeit;
	}

	/*
	 * We have neighbor solicitation packet, with target address equals to
	 * one of my tentative address.
	 *
	 * src addr	how to process?
	 * ---		---
	 * multicast	of course, invalid (rejected in ip6_input)
	 * unicast	somebody is doing address resolution -> ignore
	 * unspec	dup address detection
	 *
	 * The processing is defined in RFC 2462.
	 */
	if (tentative) {
		/*
		 * If source address is unspecified address, it is for
		 * duplicated address detection.
		 *
		 * If not, the packet is for addess resolution;
		 * silently ignore it.
		 */
		if (IN6_IS_ADDR_UNSPECIFIED(&saddr6))
			nd6_dad_ns_input(ifa);

		goto freeit;
	}

	/*
	 * If the source address is unspecified address, entries must not
	 * be created or updated.
	 * It looks that sender is performing DAD.  Output NA toward
	 * all-node multicast address, to tell the sender that I'm using
	 * the address.
	 * S bit ("solicited") must be zero.
	 */
	if (IN6_IS_ADDR_UNSPECIFIED(&saddr6)) {
		saddr6 = kin6addr_linklocal_allnodes;
		saddr6.s6_addr16[1] = htons(cmpifp->if_index);
		nd6_na_output(cmpifp, &saddr6, &taddr6,
		    ((anycast || proxy || !tlladdr) ? 0 : ND_NA_FLAG_OVERRIDE) |
		    (ip6_forwarding ? ND_NA_FLAG_ROUTER : 0),
		    tlladdr, (struct sockaddr *)proxydl);
		goto freeit;
	}

	nd6_cache_lladdr(cmpifp, &saddr6, lladdr, lladdrlen,
	    ND_NEIGHBOR_SOLICIT, 0);

	nd6_na_output(ifp, &saddr6, &taddr6,
	    ((anycast || proxy || !tlladdr) ? 0 : ND_NA_FLAG_OVERRIDE) |
	    (ip6_forwarding ? ND_NA_FLAG_ROUTER : 0) | ND_NA_FLAG_SOLICITED,
	    tlladdr, (struct sockaddr *)proxydl);
freeit:
	m_freem(m);
	return;

bad:
	nd6log((LOG_ERR, "nd6_ns_input: src=%s\n", ip6_sprintf(&saddr6)));
	nd6log((LOG_ERR, "nd6_ns_input: dst=%s\n", ip6_sprintf(&daddr6)));
	nd6log((LOG_ERR, "nd6_ns_input: tgt=%s\n", ip6_sprintf(&taddr6)));
	icmp6stat.icp6s_badns++;
	m_freem(m);
}

/*
 * Output an Neighbor Solicitation Message. Caller specifies:
 *	- ICMP6 header source IP6 address
 *	- ND6 header target IP6 address
 *	- ND6 header source datalink address
 *
 * Based on RFC 2461
 * Based on RFC 2462 (duplicated address detection)
 */
void
nd6_ns_output(struct ifnet *ifp, const struct in6_addr *daddr6,
	      const struct in6_addr *taddr6,
	      struct llinfo_nd6 *ln,	/* for source address determination */
	      int dad)			/* duplicated address detection */
{
	struct mbuf *m;
	struct ip6_hdr *ip6;
	struct nd_neighbor_solicit *nd_ns;
	struct in6_ifaddr *ia = NULL;
	struct ip6_moptions im6o;
	int icmp6len;
	int maxlen;
	caddr_t mac;
	struct ifnet *outif = NULL;

	if (IN6_IS_ADDR_MULTICAST(taddr6))
		return;

	/* estimate the size of message */
	maxlen = sizeof(*ip6) + sizeof(*nd_ns);
	maxlen += (sizeof(struct nd_opt_hdr) + ifp->if_addrlen + 7) & ~7;
	if (max_linkhdr + maxlen > MCLBYTES) {
#ifdef DIAGNOSTIC
		kprintf("nd6_ns_output: max_linkhdr + maxlen > MCLBYTES "
		    "(%d + %d > %d)\n", max_linkhdr, maxlen, MCLBYTES);
#endif
		return;
	}

	m = m_getb(max_linkhdr + maxlen, M_NOWAIT, MT_DATA, M_PKTHDR);
	if (m == NULL)
		return;

	if (daddr6 == NULL || IN6_IS_ADDR_MULTICAST(daddr6)) {
		m->m_flags |= M_MCAST;
		im6o.im6o_multicast_ifp = ifp;
		im6o.im6o_multicast_hlim = 255;
		im6o.im6o_multicast_loop = 0;
	}

	icmp6len = sizeof(*nd_ns);
	m->m_pkthdr.len = m->m_len = sizeof(*ip6) + icmp6len;
	m->m_data += max_linkhdr;	/* or MH_ALIGN() equivalent? */

	/* fill neighbor solicitation packet */
	ip6 = mtod(m, struct ip6_hdr *);
	ip6->ip6_flow = 0;
	ip6->ip6_vfc &= ~IPV6_VERSION_MASK;
	ip6->ip6_vfc |= IPV6_VERSION;
	/* ip6->ip6_plen will be set later */
	ip6->ip6_nxt = IPPROTO_ICMPV6;
	ip6->ip6_hlim = 255;
	if (daddr6)
		ip6->ip6_dst = *daddr6;
	else {
		ip6->ip6_dst.s6_addr16[0] = IPV6_ADDR_INT16_MLL;
		ip6->ip6_dst.s6_addr16[1] = htons(ifp->if_index);
		ip6->ip6_dst.s6_addr32[1] = 0;
		ip6->ip6_dst.s6_addr32[2] = IPV6_ADDR_INT32_ONE;
		ip6->ip6_dst.s6_addr32[3] = taddr6->s6_addr32[3];
		ip6->ip6_dst.s6_addr8[12] = 0xff;
	}
	if (!dad) {
		/*
		 * RFC2461 7.2.2:
		 * "If the source address of the packet prompting the
		 * solicitation is the same as one of the addresses assigned
		 * to the outgoing interface, that address SHOULD be placed
		 * in the IP Source Address of the outgoing solicitation.
		 * Otherwise, any one of the addresses assigned to the
		 * interface should be used."
		 *
		 * We use the source address for the prompting packet
		 * (saddr6), if:
		 * - saddr6 is given from the caller (by giving "ln"), and
		 * - saddr6 belongs to the outgoing interface.
		 * Otherwise, we perform a scope-wise match.
		 */
		struct ip6_hdr *hip6;		/* hold ip6 */
		struct in6_addr *saddr6;

		if (ln && ln->ln_hold) {
			hip6 = mtod(ln->ln_hold, struct ip6_hdr *);
			/* XXX pullup? */
			if (sizeof(*hip6) < ln->ln_hold->m_len)
				saddr6 = &hip6->ip6_src;
			else
				saddr6 = NULL;
		} else
			saddr6 = NULL;
		if (saddr6 && in6ifa_ifpwithaddr(ifp, saddr6))
			bcopy(saddr6, &ip6->ip6_src, sizeof(*saddr6));
		else {
			ia = in6_ifawithifp(ifp, &ip6->ip6_dst);
			if (ia == NULL) {
				m_freem(m);
				return;
			}
			ip6->ip6_src = ia->ia_addr.sin6_addr;
		}
	} else {
		/*
		 * Source address for DAD packet must always be IPv6
		 * unspecified address. (0::0)
		 */
		bzero(&ip6->ip6_src, sizeof(ip6->ip6_src));
	}
	nd_ns = (struct nd_neighbor_solicit *)(ip6 + 1);
	nd_ns->nd_ns_type = ND_NEIGHBOR_SOLICIT;
	nd_ns->nd_ns_code = 0;
	nd_ns->nd_ns_reserved = 0;
	nd_ns->nd_ns_target = *taddr6;
	in6_clearscope(&nd_ns->nd_ns_target); /* XXX */

	/*
	 * Add source link-layer address option.
	 *
	 *				spec		implementation
	 *				---		---
	 * DAD packet			MUST NOT	do not add the option
	 * there's no link layer address:
	 *				impossible	do not add the option
	 * there's link layer address:
	 *	Multicast NS		MUST add one	add the option
	 *	Unicast NS		SHOULD add one	add the option
	 */
	if (!dad && (mac = nd6_ifptomac(ifp))) {
		int optlen = sizeof(struct nd_opt_hdr) + ifp->if_addrlen;
		struct nd_opt_hdr *nd_opt = (struct nd_opt_hdr *)(nd_ns + 1);
		/* 8 byte alignments... */
		optlen = (optlen + 7) & ~7;

		m->m_pkthdr.len += optlen;
		m->m_len += optlen;
		icmp6len += optlen;
		bzero((caddr_t)nd_opt, optlen);
		nd_opt->nd_opt_type = ND_OPT_SOURCE_LINKADDR;
		nd_opt->nd_opt_len = optlen >> 3;
		bcopy(mac, (caddr_t)(nd_opt + 1), ifp->if_addrlen);
	}

	ip6->ip6_plen = htons((u_short)icmp6len);
	nd_ns->nd_ns_cksum = 0;
	nd_ns->nd_ns_cksum =
	    in6_cksum(m, IPPROTO_ICMPV6, sizeof(*ip6), icmp6len);

	ip6_output(m, NULL, NULL, dad ? IPV6_DADOUTPUT : 0, &im6o, &outif, NULL);
	if (outif) {
		icmp6_ifstat_inc(outif, ifs6_out_msg);
		icmp6_ifstat_inc(outif, ifs6_out_neighborsolicit);
	}
	icmp6stat.icp6s_outhist[ND_NEIGHBOR_SOLICIT]++;
}

/*
 * Neighbor advertisement input handling.
 *
 * Based on RFC 2461
 * Based on RFC 2462 (duplicated address detection)
 *
 * the following items are not implemented yet:
 * - proxy advertisement delay rule (RFC2461 7.2.8, last paragraph, SHOULD)
 * - anycast advertisement delay rule (RFC2461 7.2.7, SHOULD)
 */
void
nd6_na_input(struct mbuf *m, int off, int icmp6len)
{
	struct ifnet *ifp = m->m_pkthdr.rcvif;
	struct ip6_hdr *ip6 = mtod(m, struct ip6_hdr *);
	struct nd_neighbor_advert *nd_na;
	struct in6_addr saddr6 = ip6->ip6_src;
	struct in6_addr daddr6 = ip6->ip6_dst;
	struct in6_addr taddr6;
	int flags;
	int is_router;
	int is_solicited;
	int is_override;
	char *lladdr = NULL;
	int lladdrlen = 0;
	struct ifaddr *ifa;
	struct llinfo_nd6 *ln;
	struct rtentry *rt;
	struct sockaddr_dl *sdl;
	union nd_opts ndopts;

	if (ip6->ip6_hlim != 255) {
		nd6log((LOG_ERR,
		    "nd6_na_input: invalid hlim (%d) from %s to %s on %s\n",
		    ip6->ip6_hlim, ip6_sprintf(&ip6->ip6_src),
		    ip6_sprintf(&ip6->ip6_dst), if_name(ifp)));
		goto bad;
	}

#ifndef PULLDOWN_TEST
	IP6_EXTHDR_CHECK(m, off, icmp6len,);
	nd_na = (struct nd_neighbor_advert *)((caddr_t)ip6 + off);
#else
	IP6_EXTHDR_GET(nd_na, struct nd_neighbor_advert *, m, off, icmp6len);
	if (nd_na == NULL) {
		icmp6stat.icp6s_tooshort++;
		return;
	}
#endif
	taddr6 = nd_na->nd_na_target;
	flags = nd_na->nd_na_flags_reserved;
	is_router = ((flags & ND_NA_FLAG_ROUTER) != 0);
	is_solicited = ((flags & ND_NA_FLAG_SOLICITED) != 0);
	is_override = ((flags & ND_NA_FLAG_OVERRIDE) != 0);

	if (IN6_IS_SCOPE_LINKLOCAL(&taddr6))
		taddr6.s6_addr16[1] = htons(ifp->if_index);

	if (IN6_IS_ADDR_MULTICAST(&taddr6)) {
		nd6log((LOG_ERR,
		    "nd6_na_input: invalid target address %s\n",
		    ip6_sprintf(&taddr6)));
		goto bad;
	}
	if (IN6_IS_ADDR_MULTICAST(&daddr6))
		if (is_solicited) {
			nd6log((LOG_ERR,
			    "nd6_na_input: a solicited adv is multicasted\n"));
			goto bad;
		}

	icmp6len -= sizeof(*nd_na);
	nd6_option_init(nd_na + 1, icmp6len, &ndopts);
	if (nd6_options(&ndopts) < 0) {
		nd6log((LOG_INFO,
		    "nd6_na_input: invalid ND option, ignored\n"));
		/* nd6_options have incremented stats */
		goto freeit;
	}

	if (ndopts.nd_opts_tgt_lladdr) {
		struct ifnet *ifp_ll;

		lladdr = (char *)(ndopts.nd_opts_tgt_lladdr + 1);
		lladdrlen = ndopts.nd_opts_tgt_lladdr->nd_opt_len << 3;

		if (lladdr && ((ifp->if_addrlen + 2 + 7) & ~7) != lladdrlen) {
			nd6log((LOG_INFO, "nd6_na_input: lladdrlen mismatch "
			    "for %s (if %d, NA packet %d)\n", ip6_sprintf(&taddr6),
			    ifp->if_addrlen, lladdrlen - 2));
			goto bad;
		}

		/* If it's from me, ignore it. */
		ifp_ll = if_bylla(lladdr, ifp->if_addrlen);
		if (ifp_ll != NULL)
			goto freeit;
	}

	ifa = (struct ifaddr *)in6ifa_ifpwithaddr(ifp, &taddr6);

	/*
	 * Target address matches one of my interface address.
	 *
	 * If my address is tentative, this means that there's somebody
	 * already using the same address as mine.  This indicates DAD failure.
	 * This is defined in RFC 2462.
	 *
	 * Otherwise, process as defined in RFC 2461.
	 */
	if (ifa
	 && (((struct in6_ifaddr *)ifa)->ia6_flags & IN6_IFF_TENTATIVE)) {
		nd6_dad_na_input(ifa);
		goto freeit;
	}

	/* Just for safety, maybe unnecessary. */
	if (ifa) {
		log(LOG_ERR,
		    "nd6_na_input: duplicate IP6 address %s\n",
		    ip6_sprintf(&taddr6));
		goto freeit;
	}

	if (!nd6_onlink_ns_rfc4861) {
		/*
		 * Make sure the source address is from a neighbor's address.
		 */
		struct sockaddr_in6 src_sa6;

		bzero(&src_sa6, sizeof(src_sa6));
		src_sa6.sin6_family = AF_INET6;
		src_sa6.sin6_len = sizeof(src_sa6);
		src_sa6.sin6_addr = saddr6;
		if (nd6_is_addr_neighbor(&src_sa6, ifp) == 0) {
			nd6log((LOG_INFO, "nd6_na_input: "
			    "NA packet from non-neighbor\n"));
			goto bad;
		}
	}

	/*
	 * If no neighbor cache entry is found, NA SHOULD silently be discarded.
	 */
	rt = nd6_lookup(&taddr6, 0, ifp);
	if ((rt == NULL) ||
	   ((ln = (struct llinfo_nd6 *)rt->rt_llinfo) == NULL) ||
	   ((sdl = SDL(rt->rt_gateway)) == NULL))
		goto freeit;

	if (ln->ln_state <= ND6_LLINFO_INCOMPLETE) {
		/*
		 * If the link-layer has address, and no lladdr option came,
		 * discard the packet.
		 */
		if (ifp->if_addrlen && !lladdr)
			goto freeit;

		/*
		 * Record link-layer address, and update the state.
		 */
		sdl->sdl_alen = ifp->if_addrlen;
		bcopy(lladdr, LLADDR(sdl), ifp->if_addrlen);
		if (is_solicited) {
			ln->ln_state = ND6_LLINFO_REACHABLE;
			ln->ln_byhint = 0;
			if (ln->ln_expire) {
				ln->ln_expire = time_uptime +
				    ND_IFINFO(rt->rt_ifp)->reachable;
			}
		} else {
			ln->ln_state = ND6_LLINFO_STALE;
			ln->ln_expire = time_uptime + nd6_gctimer;
		}
		if ((ln->ln_router = is_router) != 0) {
			/*
			 * This means a router's state has changed from
			 * non-reachable to probably reachable, and might
			 * affect the status of associated prefixes..
			 */
			pfxlist_onlink_check();
		}
	} else {
		int llchange;

		/*
		 * Check if the link-layer address has changed or not.
		 */
		if (!lladdr)
			llchange = 0;
		else {
			if (sdl->sdl_alen) {
				if (bcmp(lladdr, LLADDR(sdl), ifp->if_addrlen))
					llchange = 1;
				else
					llchange = 0;
			} else
				llchange = 1;
		}

		/*
		 * This is VERY complex.  Look at it with care.
		 *
		 * override solicit lladdr llchange	action
		 *					(L: record lladdr)
		 *
		 *	0	0	n	--	(2c)
		 *	0	0	y	n	(2b) L
		 *	0	0	y	y	(1)    REACHABLE->STALE
		 *	0	1	n	--	(2c)   *->REACHABLE
		 *	0	1	y	n	(2b) L *->REACHABLE
		 *	0	1	y	y	(1)    REACHABLE->STALE
		 *	1	0	n	--	(2a)
		 *	1	0	y	n	(2a) L
		 *	1	0	y	y	(2a) L *->STALE
		 *	1	1	n	--	(2a)   *->REACHABLE
		 *	1	1	y	n	(2a) L *->REACHABLE
		 *	1	1	y	y	(2a) L *->REACHABLE
		 */
		if (!is_override && (lladdr && llchange)) {	   /* (1) */
			/*
			 * If state is REACHABLE, make it STALE.
			 * no other updates should be done.
			 */
			if (ln->ln_state == ND6_LLINFO_REACHABLE) {
				ln->ln_state = ND6_LLINFO_STALE;
				ln->ln_expire = time_uptime + nd6_gctimer;
			}
			goto freeit;
		} else if (is_override				   /* (2a) */
			|| (lladdr && !llchange)                   /* (2b) */
			|| !lladdr) {				   /* (2c) */
			/*
			 * Update link-local address, if any.
			 */
			if (lladdr) {
				sdl->sdl_alen = ifp->if_addrlen;
				bcopy(lladdr, LLADDR(sdl), ifp->if_addrlen);
			}

			/*
			 * If solicited, make the state REACHABLE.
			 * If not solicited and the link-layer address was
			 * changed, make it STALE.
			 */
			if (is_solicited) {
				ln->ln_state = ND6_LLINFO_REACHABLE;
				ln->ln_byhint = 0;
				if (ln->ln_expire) {
					ln->ln_expire = time_uptime +
					    ND_IFINFO(ifp)->reachable;
				}
			} else {
				if (lladdr && llchange) {
					ln->ln_state = ND6_LLINFO_STALE;
					ln->ln_expire = time_uptime + nd6_gctimer;
				}
			}
		}

		if (ln->ln_router && !is_router) {
			/*
			 * The peer dropped the router flag.
			 * Remove the sender from the Default Router List and
			 * update the Destination Cache entries.
			 */
			struct nd_defrouter *dr;
			struct in6_addr *in6;

			in6 = &((struct sockaddr_in6 *)rt_key(rt))->sin6_addr;

			/*
			 * Lock to protect the default router list.
			 * XXX: this might be unnecessary, since this function
			 * is only called under the network software interrupt
			 * context.  However, we keep it just for safety.
			 */
			mtx_lock(&nd6_mtx);
			dr = defrouter_lookup(in6, rt->rt_ifp);
			if (dr)
				defrtrlist_del(dr);
			mtx_unlock(&nd6_mtx);

			if (dr == NULL && !ip6_forwarding &&
			    (ND_IFINFO(rt->rt_ifp)->flags &
			     ND6_IFF_ACCEPT_RTADV) != 0) {
				/*
				 * Even if the neighbor is not in the default
				 * router list, the neighbor may be used
				 * as a next hop for some destinations
				 * (e.g. redirect case). So we must
				 * call rt6_flush explicitly.
				 */
				rt6_flush(&ip6->ip6_src, rt->rt_ifp);
			}
		}
		ln->ln_router = is_router;
	}
	rt->rt_flags &= ~RTF_REJECT;
	ln->ln_asked = 0;
	if (ln->ln_hold) {
		/*
		 * we assume ifp is not a loopback here, so just set the 2nd
		 * argument as the 1st one.
		 */
		nd6_output(ifp, ifp, ln->ln_hold,
			   (struct sockaddr_in6 *)rt_key(rt), rt);
		ln->ln_hold = NULL;
	}

freeit:
	m_freem(m);
	return;

bad:
	icmp6stat.icp6s_badna++;
	m_freem(m);
}

/*
 * Neighbor advertisement output handling.
 *
 * Based on RFC 2461
 *
 * the following items are not implemented yet:
 * - proxy advertisement delay rule (RFC2461 7.2.8, last paragraph, SHOULD)
 * - anycast advertisement delay rule (RFC2461 7.2.7, SHOULD)
 */
void
nd6_na_output(struct ifnet *ifp, const struct in6_addr *daddr6,
	      const struct in6_addr *taddr6, u_long flags,
	      int tlladdr,	/* 1 if include target link-layer address */
	      struct sockaddr *sdl0)	/* sockaddr_dl (= proxy NA) or NULL */
{
	struct mbuf *m;
	struct ip6_hdr *ip6;
	struct nd_neighbor_advert *nd_na;
	struct in6_ifaddr *ia = NULL;
	struct ip6_moptions im6o;
	int icmp6len;
	int maxlen;
	caddr_t mac;
	struct ifnet *outif = NULL;

	/* estimate the size of message */
	maxlen = sizeof(*ip6) + sizeof(*nd_na);
	maxlen += (sizeof(struct nd_opt_hdr) + ifp->if_addrlen + 7) & ~7;
	if (max_linkhdr + maxlen > MCLBYTES) {
#ifdef DIAGNOSTIC
		kprintf("nd6_na_output: max_linkhdr + maxlen > MCLBYTES "
		    "(%d + %d > %d)\n", max_linkhdr, maxlen, MCLBYTES);
#endif
		return;
	}

	m = m_getb(max_linkhdr + maxlen, M_NOWAIT, MT_DATA, M_PKTHDR);
	if (m == NULL)
		return;

	if (IN6_IS_ADDR_MULTICAST(daddr6)) {
		m->m_flags |= M_MCAST;
		im6o.im6o_multicast_ifp = ifp;
		im6o.im6o_multicast_hlim = 255;
		im6o.im6o_multicast_loop = 0;
	}

	icmp6len = sizeof(*nd_na);
	m->m_pkthdr.len = m->m_len = sizeof(struct ip6_hdr) + icmp6len;
	m->m_data += max_linkhdr;	/* or MH_ALIGN() equivalent? */

	/* fill neighbor advertisement packet */
	ip6 = mtod(m, struct ip6_hdr *);
	ip6->ip6_flow = 0;
	ip6->ip6_vfc &= ~IPV6_VERSION_MASK;
	ip6->ip6_vfc |= IPV6_VERSION;
	ip6->ip6_nxt = IPPROTO_ICMPV6;
	ip6->ip6_hlim = 255;
	if (IN6_IS_ADDR_UNSPECIFIED(daddr6)) {
		/* reply to DAD */
		ip6->ip6_dst.s6_addr16[0] = IPV6_ADDR_INT16_MLL;
		ip6->ip6_dst.s6_addr16[1] = htons(ifp->if_index);
		ip6->ip6_dst.s6_addr32[1] = 0;
		ip6->ip6_dst.s6_addr32[2] = 0;
		ip6->ip6_dst.s6_addr32[3] = IPV6_ADDR_INT32_ONE;
		flags &= ~ND_NA_FLAG_SOLICITED;
	} else
		ip6->ip6_dst = *daddr6;

	/*
	 * Select a source whose scope is the same as that of the dest.
	 */
	ia = in6_ifawithifp(ifp, &ip6->ip6_dst);
	if (ia == NULL) {
		m_freem(m);
		return;
	}
	ip6->ip6_src = ia->ia_addr.sin6_addr;
	nd_na = (struct nd_neighbor_advert *)(ip6 + 1);
	nd_na->nd_na_type = ND_NEIGHBOR_ADVERT;
	nd_na->nd_na_code = 0;
	nd_na->nd_na_target = *taddr6;
	in6_clearscope(&nd_na->nd_na_target); /* XXX */

	/*
	 * "tlladdr" indicates NS's condition for adding tlladdr or not.
	 * see nd6_ns_input() for details.
	 * Basically, if NS packet is sent to unicast/anycast addr,
	 * target lladdr option SHOULD NOT be included.
	 */
	mac = NULL;
	if (tlladdr) {
		/*
		 * sdl0 != NULL indicates proxy NA.  If we do proxy, use
		 * lladdr in sdl0.  If we are not proxying (sending NA for
		 * my address) use lladdr configured for the interface.
		 */
		if (sdl0 == NULL) {
#ifdef CARP
			if (ifp->if_carp)
				mac = carp_macmatch6(ifp->if_carp, m, taddr6);
			if (mac == NULL)
				mac = nd6_ifptomac(ifp);
#else
			mac = nd6_ifptomac(ifp);
#endif
		} else if (sdl0->sa_family == AF_LINK) {
			struct sockaddr_dl *sdl;
			sdl = (struct sockaddr_dl *)sdl0;
			if (sdl->sdl_alen == ifp->if_addrlen)
				mac = LLADDR(sdl);
		}
	}
	if (mac != NULL) {
		int optlen = sizeof(struct nd_opt_hdr) + ifp->if_addrlen;
		struct nd_opt_hdr *nd_opt = (struct nd_opt_hdr *)(nd_na + 1);

		/* roundup to 8 bytes alignment! */
		optlen = (optlen + 7) & ~7;

		m->m_pkthdr.len += optlen;
		m->m_len += optlen;
		icmp6len += optlen;
		bzero((caddr_t)nd_opt, optlen);
		nd_opt->nd_opt_type = ND_OPT_TARGET_LINKADDR;
		nd_opt->nd_opt_len = optlen >> 3;
		bcopy(mac, (caddr_t)(nd_opt + 1), ifp->if_addrlen);
	} else
		flags &= ~ND_NA_FLAG_OVERRIDE;

	ip6->ip6_plen = htons((u_short)icmp6len);
	nd_na->nd_na_flags_reserved = flags;
	nd_na->nd_na_cksum = 0;
	nd_na->nd_na_cksum =
	    in6_cksum(m, IPPROTO_ICMPV6, sizeof(struct ip6_hdr), icmp6len);

	ip6_output(m, NULL, NULL, 0, &im6o, &outif, NULL);
	if (outif) {
		icmp6_ifstat_inc(outif, ifs6_out_msg);
		icmp6_ifstat_inc(outif, ifs6_out_neighboradvert);
	}
	icmp6stat.icp6s_outhist[ND_NEIGHBOR_ADVERT]++;
}

caddr_t
nd6_ifptomac(struct ifnet *ifp)
{
	switch (ifp->if_type) {
	case IFT_ETHER:
	case IFT_IEEE1394:
#ifdef IFT_L2VLAN
	case IFT_L2VLAN:
#endif
#ifdef IFT_IEEE80211
	case IFT_IEEE80211:
#endif
#ifdef IFT_CARP
	case IFT_CARP:
#endif
		return ((caddr_t)(ifp + 1));
	default:
		return NULL;
	}
}

struct netmsg_dad {
	struct netmsg_base	base;
	struct dadq		*dadq;
};

struct dadq {
	TAILQ_ENTRY(dadq) dad_list;
	struct ifaddr *dad_ifa;
	int dad_count;		/* max NS to send */
	int dad_ns_tcount;	/* # of trials to send NS */
	int dad_ns_ocount;	/* NS sent so far */
	int dad_ns_icount;
	int dad_na_icount;
	struct callout dad_timer_ch;
	struct netmsg_dad dad_nmsg;
};
TAILQ_HEAD(dadq_head, dadq);

static struct dadq_head dadq = TAILQ_HEAD_INITIALIZER(dadq);

static struct dadq *
nd6_dad_find(struct ifaddr *ifa)
{
	struct dadq *dp;

	ASSERT_NETISR0;

	TAILQ_FOREACH(dp, &dadq, dad_list) {
		if (dp->dad_ifa == ifa)
			return dp;
	}
	return NULL;
}

static void
nd6_dad_starttimer(struct dadq *dp, int ticks)
{
	ASSERT_NETISR0;
	callout_reset(&dp->dad_timer_ch, ticks, nd6_dad_timer, dp);
}

static void
nd6_dad_stoptimer(struct dadq *dp)
{
	ASSERT_NETISR0;
	callout_stop(&dp->dad_timer_ch);
}

/*
 * Start Duplicated Address Detection (DAD) for specified interface address.
 */
void
nd6_dad_start(struct ifaddr *ifa,
	      int *tick)	/* minimum delay ticks for IFF_UP event */
{
	struct in6_ifaddr *ia = (struct in6_ifaddr *)ifa;
	struct dadq *dp;

	ASSERT_NETISR0;

	/*
	 * If we don't need DAD, don't do it.
	 * There are several cases:
	 * - DAD is disabled globally (ip6_dad_count == 0) or on the interface
	 * - the interface address is anycast
	 */
	if (!(ia->ia6_flags & IN6_IFF_TENTATIVE)) {
		log(LOG_DEBUG,
			"nd6_dad_start: called with non-tentative address "
			"%s(%s)\n",
			ip6_sprintf(&ia->ia_addr.sin6_addr),
			ifa->ifa_ifp ? if_name(ifa->ifa_ifp) : "???");
		return;
	}
	if ((ia->ia6_flags & IN6_IFF_ANYCAST) ||
	    ip6_dad_count == 0 ||
	    (ND_IFINFO(ifa->ifa_ifp)->flags & ND6_IFF_NO_DAD)) {
		ia->ia6_flags &= ~IN6_IFF_TENTATIVE;
		in6_newaddrmsg(ifa);
		return;
	}
	if (!(ifa->ifa_ifp->if_flags & IFF_UP))
		return;
	if (nd6_dad_find(ifa) != NULL) {
		/* DAD already in progress */
		return;
	}

	dp = nd6_dad_create(ifa);
	nd6log((LOG_DEBUG, "%s: starting DAD for %s\n", if_name(ifa->ifa_ifp),
	    ip6_sprintf(&ia->ia_addr.sin6_addr)));

	/*
	 * Send NS packet for DAD, dp->dad_count times.
	 * Note that we must delay the first transmission, if this is the
	 * first packet to be sent from the interface after interface
	 * (re)initialization.
	 */
	if (tick == NULL) {
		nd6_dad_ns_output(dp);
		nd6_dad_starttimer(dp,
		    ND_IFINFO(ifa->ifa_ifp)->retrans * hz / 1000);
	} else {
		int ntick;

		if (*tick == 0)
			ntick = krandom() % (MAX_RTR_SOLICITATION_DELAY * hz);
		else
			ntick = *tick + krandom() % (hz / 2);
		*tick = ntick;
		nd6_dad_starttimer(dp, ntick);
	}
}

/*
 * Terminate DAD unconditionally.  Used for address removals.
 */
void
nd6_dad_stop(struct ifaddr *ifa)
{
	struct dadq *dp;

	ASSERT_NETISR0;

	dp = nd6_dad_find(ifa);
	if (!dp) {
		/* DAD wasn't started yet */
		return;
	}
	nd6_dad_destroy(dp);
}

static struct dadq *
nd6_dad_create(struct ifaddr *ifa)
{
	struct netmsg_dad *dm;
	struct dadq *dp;

	ASSERT_NETISR0;

	dp = kmalloc(sizeof(*dp), M_IP6NDP, M_INTWAIT | M_ZERO);
	callout_init_mp(&dp->dad_timer_ch);

	dm = &dp->dad_nmsg;
	netmsg_init(&dm->base, NULL, &netisr_adone_rport,
	    MSGF_DROPABLE | MSGF_PRIORITY, nd6_dad_timer_handler);
	dm->dadq = dp;

	dp->dad_ifa = ifa;
	IFAREF(ifa);	/* just for safety */

	/* Send NS packet for DAD, ip6_dad_count times. */
	dp->dad_count = ip6_dad_count;

	TAILQ_INSERT_TAIL(&dadq, dp, dad_list);

	return dp;
}

static void
nd6_dad_destroy(struct dadq *dp)
{
	struct lwkt_msg *lmsg = &dp->dad_nmsg.base.lmsg;

	ASSERT_NETISR0;

	TAILQ_REMOVE(&dadq, dp, dad_list);

	nd6_dad_stoptimer(dp);

	crit_enter();
	if ((lmsg->ms_flags & MSGF_DONE) == 0)
		lwkt_dropmsg(lmsg);
	crit_exit();

	IFAFREE(dp->dad_ifa);
	kfree(dp, M_IP6NDP);
}

static void
nd6_dad_timer(void *xdp)
{
	struct dadq *dp = xdp;
	struct lwkt_msg *lmsg = &dp->dad_nmsg.base.lmsg;

	KASSERT(mycpuid == 0, ("dad timer not on cpu0"));

	crit_enter();
	if (lmsg->ms_flags & MSGF_DONE)
		lwkt_sendmsg_oncpu(netisr_cpuport(0), lmsg);
	crit_exit();
}

static void
nd6_dad_timer_handler(netmsg_t msg)
{
	struct netmsg_dad *dm = (struct netmsg_dad *)msg;
	struct dadq *dp = dm->dadq;
	struct ifaddr *ifa = dp->dad_ifa;
	struct in6_ifaddr *ia = (struct in6_ifaddr *)ifa;

	ASSERT_NETISR0;

	/* Reply ASAP */
	crit_enter();
	lwkt_replymsg(&dm->base.lmsg, 0);
	crit_exit();

	if (ia->ia6_flags & IN6_IFF_DUPLICATED) {
		log(LOG_ERR, "nd6_dad_timer: called with duplicated address "
			"%s(%s)\n",
			ip6_sprintf(&ia->ia_addr.sin6_addr),
			ifa->ifa_ifp ? if_name(ifa->ifa_ifp) : "???");
		goto destroy;
	}
	if (!(ia->ia6_flags & IN6_IFF_TENTATIVE)) {
		log(LOG_ERR, "nd6_dad_timer: called with non-tentative address "
			"%s(%s)\n",
			ip6_sprintf(&ia->ia_addr.sin6_addr),
			ifa->ifa_ifp ? if_name(ifa->ifa_ifp) : "???");
		goto destroy;
	}

	/* Timed out with IFF_{RUNNING,UP} check */
	if (dp->dad_ns_tcount > dad_maxtry) {
		nd6log((LOG_INFO, "%s: could not run DAD, driver problem?\n",
		    if_name(ifa->ifa_ifp)));
		goto destroy;
	}

	/* Need more checks? */
	if (dp->dad_ns_ocount < dp->dad_count) {
		/*
		 * We have more NS to go.  Send NS packet for DAD.
		 */
		nd6_dad_ns_output(dp);
		nd6_dad_starttimer(dp,
		    ND_IFINFO(ifa->ifa_ifp)->retrans * hz / 1000);
	} else {
		/*
		 * We have transmitted sufficient number of DAD packets.
		 * See what we've got.
		 */
		int duplicate;

		duplicate = 0;

		if (dp->dad_na_icount) {
			/*
			 * the check is in nd6_dad_na_input(),
			 * but just in case
			 */
			duplicate++;
		}

		if (dp->dad_ns_icount) {
#if 0 /* heuristics */
			/*
			 * if
			 * - we have sent many(?) DAD NS, and
			 * - the number of NS we sent equals to the
			 *   number of NS we've got, and
			 * - we've got no NA
			 * we may have a faulty network card/driver which
			 * loops back multicasts to myself.
			 */
			if (3 < dp->dad_count
			 && dp->dad_ns_icount == dp->dad_count
			 && dp->dad_na_icount == 0) {
				log(LOG_INFO, "DAD questionable for %s(%s): "
				    "network card loops back multicast?\n",
				    ip6_sprintf(&ia->ia_addr.sin6_addr),
				    if_name(ifa->ifa_ifp));
				/* XXX consider it a duplicate or not? */
				/* duplicate++; */
			} else {
				/* We've seen NS, means DAD has failed. */
				duplicate++;
			}
#else
			/* We've seen NS, means DAD has failed. */
			duplicate++;
#endif
		}

		if (duplicate) {
			/* dp will be freed in nd6_dad_duplicated() */
			dp = NULL;
			nd6_dad_duplicated(ifa);
		} else {
			/*
			 * We are done with DAD.  No NA came, no NS came.
			 * duplicated address found.
			 */
			ia->ia6_flags &= ~IN6_IFF_TENTATIVE;
			in6_newaddrmsg(ifa);
			nd6log((LOG_DEBUG,
			    "%s: DAD complete for %s - no duplicates found\n",
			    if_name(ifa->ifa_ifp),
			    ip6_sprintf(&ia->ia_addr.sin6_addr)));
			goto destroy;
		}
	}
	return;
destroy:
	nd6_dad_destroy(dp);
}

static void
nd6_dad_duplicated(struct ifaddr *ifa)
{
	struct in6_ifaddr *ia = (struct in6_ifaddr *)ifa;
	struct dadq *dp;

	ASSERT_NETISR0;

	dp = nd6_dad_find(ifa);
	if (dp == NULL) {
		log(LOG_ERR, "nd6_dad_duplicated: DAD structure not found\n");
		return;
	}

	/*
	 * We are done with DAD, with duplicated address found. (failure)
	 */
	log(LOG_ERR, "%s: DAD detected duplicate IPv6 address %s: "
	    "NS in/out=%d/%d, NA in=%d\n",
	    if_name(ifa->ifa_ifp), ip6_sprintf(&ia->ia_addr.sin6_addr),
	    dp->dad_ns_icount, dp->dad_ns_ocount, dp->dad_na_icount);

	ia->ia6_flags &= ~IN6_IFF_TENTATIVE;
	ia->ia6_flags |= IN6_IFF_DUPLICATED;
	in6_newaddrmsg(ifa);

	log(LOG_ERR, "%s: DAD complete for %s - duplicate found\n",
	    if_name(ifa->ifa_ifp), ip6_sprintf(&ia->ia_addr.sin6_addr));
	log(LOG_ERR, "%s: manual intervention required\n",
	    if_name(ifa->ifa_ifp));

	nd6_dad_destroy(dp);
}

static void
nd6_dad_ns_output(struct dadq *dp)
{
	struct in6_ifaddr *ia = (struct in6_ifaddr *)dp->dad_ifa;
	struct ifnet *ifp = dp->dad_ifa->ifa_ifp;

	ASSERT_NETISR0;

	dp->dad_ns_tcount++;
	if (!(ifp->if_flags & IFF_UP)) {
#if 0
		kprintf("%s: interface down?\n", if_name(ifp));
#endif
		return;
	}
	if (!(ifp->if_flags & IFF_RUNNING)) {
#if 0
		kprintf("%s: interface not running?\n", if_name(ifp));
#endif
		return;
	}

	dp->dad_ns_ocount++;
	nd6_ns_output(ifp, NULL, &ia->ia_addr.sin6_addr, NULL, 1);
}

static void
nd6_dad_ns_input(struct ifaddr *ifa)
{
	struct in6_ifaddr *ia;
	const struct in6_addr *taddr6;
	struct dadq *dp;
	int duplicate;

	ASSERT_NETISR0;

	if (!ifa)
		panic("ifa == NULL in nd6_dad_ns_input");

	ia = (struct in6_ifaddr *)ifa;
	taddr6 = &ia->ia_addr.sin6_addr;
	duplicate = 0;
	dp = nd6_dad_find(ifa);

	/* Quickhack - completely ignore DAD NS packets */
	if (dad_ignore_ns) {
		nd6log((LOG_INFO,
		    "nd6_dad_ns_input: ignoring DAD NS packet for "
		    "address %s(%s)\n", ip6_sprintf(taddr6),
		    if_name(ifa->ifa_ifp)));
		return;
	}

	/*
	 * if I'm yet to start DAD, someone else started using this address
	 * first.  I have a duplicate and you win.
	 */
	if (!dp || dp->dad_ns_ocount == 0)
		duplicate++;

	/* XXX more checks for loopback situation - see nd6_dad_timer too */

	if (duplicate) {
		dp = NULL;	/* will be freed in nd6_dad_duplicated() */
		nd6_dad_duplicated(ifa);
	} else {
		/*
		 * not sure if I got a duplicate.
		 * increment ns count and see what happens.
		 */
		if (dp)
			dp->dad_ns_icount++;
	}
}

static void
nd6_dad_na_input(struct ifaddr *ifa)
{
	struct dadq *dp;

	ASSERT_NETISR0;

	if (!ifa)
		panic("ifa == NULL in nd6_dad_na_input");

	dp = nd6_dad_find(ifa);
	if (dp)
		dp->dad_na_icount++;

	/* remove the address. */
	nd6_dad_duplicated(ifa);
}
