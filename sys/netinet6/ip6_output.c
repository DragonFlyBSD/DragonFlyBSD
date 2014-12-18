/*	$FreeBSD: src/sys/netinet6/ip6_output.c,v 1.13.2.18 2003/01/24 05:11:35 sam Exp $	*/
/*	$KAME: ip6_output.c,v 1.279 2002/01/26 06:12:30 jinmei Exp $	*/

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

/*
 * Copyright (c) 1982, 1986, 1988, 1990, 1993
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
 *	@(#)ip_output.c	8.3 (Berkeley) 1/21/94
 */

#include "opt_ip6fw.h"
#include "opt_inet.h"
#include "opt_inet6.h"
#include "opt_ipsec.h"

#include <sys/param.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/errno.h>
#include <sys/protosw.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/proc.h>
#include <sys/priv.h>

#include <sys/thread2.h>
#include <sys/msgport2.h>

#include <net/if.h>
#include <net/route.h>
#include <net/pfil.h>

#include <netinet/in.h>
#include <netinet/in_var.h>
#include <netinet6/in6_var.h>
#include <netinet/ip6.h>
#include <netinet/icmp6.h>
#include <netinet6/ip6_var.h>
#include <netinet/in_pcb.h>
#include <netinet6/nd6.h>
#include <netinet6/ip6protosw.h>

#ifdef IPSEC
#include <netinet6/ipsec.h>
#ifdef INET6
#include <netinet6/ipsec6.h>
#endif
#include <netproto/key/key.h>
#endif /* IPSEC */

#ifdef FAST_IPSEC
#include <netproto/ipsec/ipsec.h>
#include <netproto/ipsec/ipsec6.h>
#include <netproto/ipsec/key.h>
#endif

#include <net/ip6fw/ip6_fw.h>

#include <net/net_osdep.h>

static MALLOC_DEFINE(M_IPMOPTS, "ip6_moptions", "internet multicast options");

struct ip6_exthdrs {
	struct mbuf *ip6e_ip6;
	struct mbuf *ip6e_hbh;
	struct mbuf *ip6e_dest1;
	struct mbuf *ip6e_rthdr;
	struct mbuf *ip6e_dest2;
};

static int ip6_pcbopt (int, u_char *, int, struct ip6_pktopts **, int);
static int ip6_setpktoption (int, u_char *, int, struct ip6_pktopts *,
	 int, int, int, int);
static int ip6_pcbopts (struct ip6_pktopts **, struct mbuf *,
			    struct socket *, struct sockopt *);
static int ip6_getpcbopt(struct ip6_pktopts *, int, struct sockopt *);
static int ip6_setmoptions (int, struct ip6_moptions **, struct mbuf *);
static int ip6_getmoptions (int, struct ip6_moptions *, struct mbuf **);
static int ip6_getpmtu(struct route_in6 *, struct route_in6 *,
	struct ifnet *, struct in6_addr *, u_long *, int *);
static int copyexthdr (void *, struct mbuf **);
static int ip6_insertfraghdr (struct mbuf *, struct mbuf *, int,
				  struct ip6_frag **);
static int ip6_insert_jumboopt (struct ip6_exthdrs *, u_int32_t);
static struct mbuf *ip6_splithdr (struct mbuf *);
static int copypktopts(struct ip6_pktopts *, struct ip6_pktopts *, int);

/*
 * IP6 output. The packet in mbuf chain m contains a skeletal IP6
 * header (with pri, len, nxt, hlim, src, dst).
 * This function may modify ver and hlim only.
 * The mbuf chain containing the packet will be freed.
 * The mbuf opt, if present, will not be freed.
 *
 * type of "mtu": rt_rmx.rmx_mtu is u_long, ifnet.ifr_mtu is int, and
 * nd_ifinfo.linkmtu is u_int32_t.  so we use u_long to hold largest one,
 * which is rt_rmx.rmx_mtu.
 */
int
ip6_output(struct mbuf *m0, struct ip6_pktopts *opt, struct route_in6 *ro,
	   int flags, struct ip6_moptions *im6o,
	   struct ifnet **ifpp,		/* XXX: just for statistics */
	   struct inpcb *inp)
{
	struct ip6_hdr *ip6, *mhip6;
	struct ifnet *ifp, *origifp;
	struct mbuf *m = m0;
	struct mbuf *mprev;
	u_char *nexthdrp;
	int hlen, tlen, len, off;
	struct route_in6 ip6route;
	struct sockaddr_in6 *dst;
	int error = 0;
	struct in6_ifaddr *ia = NULL;
	u_long mtu;
	int alwaysfrag, dontfrag;
	u_int32_t optlen, plen = 0, unfragpartlen;
	struct ip6_exthdrs exthdrs;
	struct in6_addr finaldst;
	struct route_in6 *ro_pmtu = NULL;
	boolean_t hdrsplit = FALSE;
	boolean_t needipsec = FALSE;
#ifdef IPSEC
	boolean_t needipsectun = FALSE;
	struct secpolicy *sp = NULL;
	struct socket *so = inp ? inp->inp_socket : NULL;

	ip6 = mtod(m, struct ip6_hdr *);
#endif
#ifdef FAST_IPSEC
	boolean_t needipsectun = FALSE;
	struct secpolicy *sp = NULL;

	ip6 = mtod(m, struct ip6_hdr *);
#endif

	bzero(&exthdrs, sizeof exthdrs);

	if (opt) {
		if ((error = copyexthdr(opt->ip6po_hbh, &exthdrs.ip6e_hbh)))
			goto freehdrs;
		if ((error = copyexthdr(opt->ip6po_dest1, &exthdrs.ip6e_dest1)))
			goto freehdrs;
		if ((error = copyexthdr(opt->ip6po_rthdr, &exthdrs.ip6e_rthdr)))
			goto freehdrs;
		if ((error = copyexthdr(opt->ip6po_dest2, &exthdrs.ip6e_dest2)))
			goto freehdrs;
	}

#ifdef IPSEC
	/* get a security policy for this packet */
	if (so == NULL)
		sp = ipsec6_getpolicybyaddr(m, IPSEC_DIR_OUTBOUND, 0, &error);
	else
		sp = ipsec6_getpolicybysock(m, IPSEC_DIR_OUTBOUND, so, &error);

	if (sp == NULL) {
		ipsec6stat.out_inval++;
		goto freehdrs;
	}

	error = 0;

	/* check policy */
	switch (sp->policy) {
	case IPSEC_POLICY_DISCARD:
		/*
		 * This packet is just discarded.
		 */
		ipsec6stat.out_polvio++;
		goto freehdrs;

	case IPSEC_POLICY_BYPASS:
	case IPSEC_POLICY_NONE:
		/* no need to do IPsec. */
		needipsec = FALSE;
		break;

	case IPSEC_POLICY_IPSEC:
		if (sp->req == NULL) {
			error = key_spdacquire(sp);	/* acquire a policy */
			goto freehdrs;
		}
		needipsec = TRUE;
		break;

	case IPSEC_POLICY_ENTRUST:
	default:
		kprintf("ip6_output: Invalid policy found. %d\n", sp->policy);
	}
#endif /* IPSEC */
#ifdef FAST_IPSEC
	/* get a security policy for this packet */
	if (inp == NULL)
		sp = ipsec_getpolicybyaddr(m, IPSEC_DIR_OUTBOUND, 0, &error);
	else
		sp = ipsec_getpolicybysock(m, IPSEC_DIR_OUTBOUND, inp, &error);

	if (sp == NULL) {
		newipsecstat.ips_out_inval++;
		goto freehdrs;
	}

	error = 0;

	/* check policy */
	switch (sp->policy) {
	case IPSEC_POLICY_DISCARD:
		/*
		 * This packet is just discarded.
		 */
		newipsecstat.ips_out_polvio++;
		goto freehdrs;

	case IPSEC_POLICY_BYPASS:
	case IPSEC_POLICY_NONE:
		/* no need to do IPsec. */
		needipsec = FALSE;
		break;

	case IPSEC_POLICY_IPSEC:
		if (sp->req == NULL) {
			error = key_spdacquire(sp);	/* acquire a policy */
			goto freehdrs;
		}
		needipsec = TRUE;
		break;

	case IPSEC_POLICY_ENTRUST:
	default:
		kprintf("ip6_output: Invalid policy found. %d\n", sp->policy);
	}
#endif /* FAST_IPSEC */

	/*
	 * Calculate the total length of the extension header chain.
	 * Keep the length of the unfragmentable part for fragmentation.
	 */
	optlen = m_lengthm(exthdrs.ip6e_hbh, NULL) +
	    m_lengthm(exthdrs.ip6e_dest1, NULL) +
	    m_lengthm(exthdrs.ip6e_rthdr, NULL);

	unfragpartlen = optlen + sizeof(struct ip6_hdr);

	/* NOTE: we don't add AH/ESP length here. do that later. */
	optlen += m_lengthm(exthdrs.ip6e_dest2, NULL);

	/*
	 * If we need IPsec, or there is at least one extension header,
	 * separate IP6 header from the payload.
	 */
	if ((needipsec || optlen) && !hdrsplit) {
		exthdrs.ip6e_ip6 = ip6_splithdr(m);
		if (exthdrs.ip6e_ip6 == NULL) {
			error = ENOBUFS;
			goto freehdrs;
		}
		m = exthdrs.ip6e_ip6;
		hdrsplit = TRUE;
	}

	/* adjust pointer */
	ip6 = mtod(m, struct ip6_hdr *);

	/* adjust mbuf packet header length */
	m->m_pkthdr.len += optlen;
	plen = m->m_pkthdr.len - sizeof(*ip6);

	/* If this is a jumbo payload, insert a jumbo payload option. */
	if (plen > IPV6_MAXPACKET) {
		if (!hdrsplit) {
			exthdrs.ip6e_ip6 = ip6_splithdr(m);
			if (exthdrs.ip6e_ip6 == NULL) {
				error = ENOBUFS;
				goto freehdrs;
			}
			m = exthdrs.ip6e_ip6;
			hdrsplit = TRUE;
		}
		/* adjust pointer */
		ip6 = mtod(m, struct ip6_hdr *);
		if ((error = ip6_insert_jumboopt(&exthdrs, plen)) != 0)
			goto freehdrs;
		ip6->ip6_plen = 0;
	} else
		ip6->ip6_plen = htons(plen);

	/*
	 * Concatenate headers and fill in next header fields.
	 * Here we have, on "m"
	 *	IPv6 payload
	 * and we insert headers accordingly.  Finally, we should be getting:
	 *	IPv6 hbh dest1 rthdr ah* [esp* dest2 payload]
	 *
	 * during the header composing process, "m" points to IPv6 header.
	 * "mprev" points to an extension header prior to esp.
	 */

	nexthdrp = &ip6->ip6_nxt;
	mprev = m;

	/*
	 * we treat dest2 specially.  this makes IPsec processing
	 * much easier.  the goal here is to make mprev point the
	 * mbuf prior to dest2.
	 *
	 * result: IPv6 dest2 payload
	 * m and mprev will point to IPv6 header.
	 */
	if (exthdrs.ip6e_dest2) {
		if (!hdrsplit)
			panic("assumption failed: hdr not split");
		exthdrs.ip6e_dest2->m_next = m->m_next;
		m->m_next = exthdrs.ip6e_dest2;
		*mtod(exthdrs.ip6e_dest2, u_char *) = ip6->ip6_nxt;
		ip6->ip6_nxt = IPPROTO_DSTOPTS;
	}

/*
 * Place m1 after mprev.
 */
#define MAKE_CHAIN(m1, mprev, nexthdrp, i)\
    do {\
	if (m1) {\
		if (!hdrsplit)\
			panic("assumption failed: hdr not split");\
		*mtod(m1, u_char *) = *nexthdrp;\
		*nexthdrp = (i);\
		nexthdrp = mtod(m1, u_char *);\
		m1->m_next = mprev->m_next;\
		mprev->m_next = m1;\
		mprev = m1;\
	}\
    } while (0)

	/*
	 * result: IPv6 hbh dest1 rthdr dest2 payload
	 * m will point to IPv6 header.  mprev will point to the
	 * extension header prior to dest2 (rthdr in the above case).
	 */
	MAKE_CHAIN(exthdrs.ip6e_hbh, mprev, nexthdrp, IPPROTO_HOPOPTS);
	MAKE_CHAIN(exthdrs.ip6e_dest1, mprev, nexthdrp, IPPROTO_DSTOPTS);
	MAKE_CHAIN(exthdrs.ip6e_rthdr, mprev, nexthdrp, IPPROTO_ROUTING);

#if defined(IPSEC) || defined(FAST_IPSEC)
	if (needipsec) {
		struct ipsec_output_state state;
		int segleft_org = 0;
		struct ip6_rthdr *rh = NULL;

		/*
		 * pointers after IPsec headers are not valid any more.
		 * other pointers need a great care too.
		 * (IPsec routines should not mangle mbufs prior to AH/ESP)
		 */
		exthdrs.ip6e_dest2 = NULL;

		if (exthdrs.ip6e_rthdr) {
			rh = mtod(exthdrs.ip6e_rthdr, struct ip6_rthdr *);
			segleft_org = rh->ip6r_segleft;
			rh->ip6r_segleft = 0;
		}

		bzero(&state, sizeof state);
		state.m = m;
		error = ipsec6_output_trans(&state, nexthdrp, mprev, sp, flags,
					    &needipsectun);
		m = state.m;
		if (error) {
			/* mbuf is already reclaimed in ipsec6_output_trans. */
			m = NULL;
			switch (error) {
			case EHOSTUNREACH:
			case ENETUNREACH:
			case EMSGSIZE:
			case ENOBUFS:
			case ENOMEM:
				break;
			default:
				kprintf("ip6_output (ipsec): error code %d\n",
				       error);
				/* fall through */
			case ENOENT:
				/* don't show these error codes to the user */
				error = 0;
				break;
			}
			goto bad;
		}
		if (exthdrs.ip6e_rthdr) {
			/* ah6_output doesn't modify mbuf chain */
			rh->ip6r_segleft = segleft_org;
		}
	}
#endif

	/*
	 * If there is a routing header, replace destination address field
	 * with the first hop of the routing header.
	 */
	if (exthdrs.ip6e_rthdr) {
		struct ip6_rthdr *rh;

		finaldst = ip6->ip6_dst;
		rh = mtod(exthdrs.ip6e_rthdr, struct ip6_rthdr *);
		switch (rh->ip6r_type) {
		default:	/* is it possible? */
			 error = EINVAL;
			 goto bad;
		}
	}

	/* Source address validation */
	if (IN6_IS_ADDR_UNSPECIFIED(&ip6->ip6_src) &&
	    !(flags & IPV6_DADOUTPUT)) {
		error = EOPNOTSUPP;
		ip6stat.ip6s_badscope++;
		goto bad;
	}
	if (IN6_IS_ADDR_MULTICAST(&ip6->ip6_src)) {
		error = EOPNOTSUPP;
		ip6stat.ip6s_badscope++;
		goto bad;
	}

	ip6stat.ip6s_localout++;

	/*
	 * Route packet.
	 */
	if (ro == NULL) {
		ro = &ip6route;
		bzero(ro, sizeof(*ro));
	}
	ro_pmtu = ro;
	if (opt && opt->ip6po_rthdr)
		ro = &opt->ip6po_route;
	dst = (struct sockaddr_in6 *)&ro->ro_dst;
	/*
	 * If there is a cached route,
	 * check that it is to the same destination
	 * and is still up. If not, free it and try again.
	 */
	if (ro->ro_rt != NULL &&
	    (!(ro->ro_rt->rt_flags & RTF_UP) || dst->sin6_family != AF_INET6 ||
	     !IN6_ARE_ADDR_EQUAL(&dst->sin6_addr, &ip6->ip6_dst))) {
		RTFREE(ro->ro_rt);
		ro->ro_rt = NULL;
	}
	if (ro->ro_rt == NULL) {
		bzero(dst, sizeof(*dst));
		dst->sin6_family = AF_INET6;
		dst->sin6_len = sizeof(struct sockaddr_in6);
		dst->sin6_addr = ip6->ip6_dst;
	}
#if defined(IPSEC) || defined(FAST_IPSEC)
	if (needipsec && needipsectun) {
		struct ipsec_output_state state;

		/*
		 * All the extension headers will become inaccessible
		 * (since they can be encrypted).
		 * Don't panic, we need no more updates to extension headers
		 * on inner IPv6 packet (since they are now encapsulated).
		 *
		 * IPv6 [ESP|AH] IPv6 [extension headers] payload
		 */
		bzero(&exthdrs, sizeof(exthdrs));
		exthdrs.ip6e_ip6 = m;

		bzero(&state, sizeof(state));
		state.m = m;
		state.ro = (struct route *)ro;
		state.dst = (struct sockaddr *)dst;

		error = ipsec6_output_tunnel(&state, sp, flags);

		m = state.m;
		ro = (struct route_in6 *)state.ro;
		dst = (struct sockaddr_in6 *)state.dst;
		if (error) {
			/* mbuf is already reclaimed in ipsec6_output_tunnel. */
			m0 = m = NULL;
			m = NULL;
			switch (error) {
			case EHOSTUNREACH:
			case ENETUNREACH:
			case EMSGSIZE:
			case ENOBUFS:
			case ENOMEM:
				break;
			default:
				kprintf("ip6_output (ipsec): error code %d\n", error);
				/* fall through */
			case ENOENT:
				/* don't show these error codes to the user */
				error = 0;
				break;
			}
			goto bad;
		}

		exthdrs.ip6e_ip6 = m;
	}
#endif

	if (!IN6_IS_ADDR_MULTICAST(&ip6->ip6_dst)) {
		/* Unicast */

#define ifatoia6(ifa)	((struct in6_ifaddr *)(ifa))
#define sin6tosa(sin6)	((struct sockaddr *)(sin6))
		/* xxx
		 * interface selection comes here
		 * if an interface is specified from an upper layer,
		 * ifp must point it.
		 */
		if (ro->ro_rt == NULL) {
			/*
			 * non-bsdi always clone routes, if parent is
			 * PRF_CLONING.
			 */
			rtalloc((struct route *)ro);
		}
		if (ro->ro_rt == NULL) {
			ip6stat.ip6s_noroute++;
			error = EHOSTUNREACH;
			/* XXX in6_ifstat_inc(ifp, ifs6_out_discard); */
			goto bad;
		}
		ia = ifatoia6(ro->ro_rt->rt_ifa);
		ifp = ro->ro_rt->rt_ifp;
		ro->ro_rt->rt_use++;
		if (ro->ro_rt->rt_flags & RTF_GATEWAY)
			dst = (struct sockaddr_in6 *)ro->ro_rt->rt_gateway;
		m->m_flags &= ~(M_BCAST | M_MCAST);	/* just in case */

		in6_ifstat_inc(ifp, ifs6_out_request);

		/*
		 * Check if the outgoing interface conflicts with
		 * the interface specified by ifi6_ifindex (if specified).
		 * Note that loopback interface is always okay.
		 * (this may happen when we are sending a packet to one of
		 *  our own addresses.)
		 */
		if (opt && opt->ip6po_pktinfo
		 && opt->ip6po_pktinfo->ipi6_ifindex) {
			if (!(ifp->if_flags & IFF_LOOPBACK)
			 && ifp->if_index != opt->ip6po_pktinfo->ipi6_ifindex) {
				ip6stat.ip6s_noroute++;
				in6_ifstat_inc(ifp, ifs6_out_discard);
				error = EHOSTUNREACH;
				goto bad;
			}
		}

		if (opt && opt->ip6po_hlim != -1)
			ip6->ip6_hlim = opt->ip6po_hlim & 0xff;
	} else {
		/* Multicast */
		struct	in6_multi *in6m;

		m->m_flags = (m->m_flags & ~M_BCAST) | M_MCAST;

		/*
		 * See if the caller provided any multicast options
		 */
		ifp = NULL;
		if (im6o != NULL) {
			ip6->ip6_hlim = im6o->im6o_multicast_hlim;
			if (im6o->im6o_multicast_ifp != NULL)
				ifp = im6o->im6o_multicast_ifp;
		} else
			ip6->ip6_hlim = ip6_defmcasthlim;

		/*
		 * See if the caller provided the outgoing interface
		 * as an ancillary data.
		 * Boundary check for ifindex is assumed to be already done.
		 */
		if (opt && opt->ip6po_pktinfo && opt->ip6po_pktinfo->ipi6_ifindex)
			ifp = ifindex2ifnet[opt->ip6po_pktinfo->ipi6_ifindex];

		/*
		 * If the destination is a node-local scope multicast,
		 * the packet should be loop-backed only.
		 */
		if (IN6_IS_ADDR_MC_NODELOCAL(&ip6->ip6_dst)) {
			/*
			 * If the outgoing interface is already specified,
			 * it should be a loopback interface.
			 */
			if (ifp && !(ifp->if_flags & IFF_LOOPBACK)) {
				ip6stat.ip6s_badscope++;
				error = ENETUNREACH; /* XXX: better error? */
				/* XXX correct ifp? */
				in6_ifstat_inc(ifp, ifs6_out_discard);
				goto bad;
			} else {
				ifp = &loif[0];
			}
		}

		if (opt && opt->ip6po_hlim != -1)
			ip6->ip6_hlim = opt->ip6po_hlim & 0xff;

		/*
		 * If caller did not provide an interface lookup a
		 * default in the routing table.  This is either a
		 * default for the speicfied group (i.e. a host
		 * route), or a multicast default (a route for the
		 * ``net'' ff00::/8).
		 */
		if (ifp == NULL) {
			if (ro->ro_rt == NULL) {
				ro->ro_rt =
				  rtpurelookup((struct sockaddr *)&ro->ro_dst);
			}
			if (ro->ro_rt == NULL) {
				ip6stat.ip6s_noroute++;
				error = EHOSTUNREACH;
				/* XXX in6_ifstat_inc(ifp, ifs6_out_discard) */
				goto bad;
			}
			ia = ifatoia6(ro->ro_rt->rt_ifa);
			ifp = ro->ro_rt->rt_ifp;
			ro->ro_rt->rt_use++;
		}

		if (!(flags & IPV6_FORWARDING))
			in6_ifstat_inc(ifp, ifs6_out_request);
		in6_ifstat_inc(ifp, ifs6_out_mcast);

		/*
		 * Confirm that the outgoing interface supports multicast.
		 */
		if (!(ifp->if_flags & IFF_MULTICAST)) {
			ip6stat.ip6s_noroute++;
			in6_ifstat_inc(ifp, ifs6_out_discard);
			error = ENETUNREACH;
			goto bad;
		}
		in6m = IN6_LOOKUP_MULTI(&ip6->ip6_dst, ifp);
		if (in6m != NULL &&
		   (im6o == NULL || im6o->im6o_multicast_loop)) {
			/*
			 * If we belong to the destination multicast group
			 * on the outgoing interface, and the caller did not
			 * forbid loopback, loop back a copy.
			 */
			ip6_mloopback(ifp, m, dst);
		} else {
			/*
			 * If we are acting as a multicast router, perform
			 * multicast forwarding as if the packet had just
			 * arrived on the interface to which we are about
			 * to send.  The multicast forwarding function
			 * recursively calls this function, using the
			 * IPV6_FORWARDING flag to prevent infinite recursion.
			 *
			 * Multicasts that are looped back by ip6_mloopback(),
			 * above, will be forwarded by the ip6_input() routine,
			 * if necessary.
			 */
			if (ip6_mrouter && !(flags & IPV6_FORWARDING)) {
				if (ip6_mforward(ip6, ifp, m) != 0) {
					m_freem(m);
					goto done;
				}
			}
		}
		/*
		 * Multicasts with a hoplimit of zero may be looped back,
		 * above, but must not be transmitted on a network.
		 * Also, multicasts addressed to the loopback interface
		 * are not sent -- the above call to ip6_mloopback() will
		 * loop back a copy if this host actually belongs to the
		 * destination group on the loopback interface.
		 */
		if (ip6->ip6_hlim == 0 || (ifp->if_flags & IFF_LOOPBACK)) {
			m_freem(m);
			goto done;
		}
	}

	/*
	 * Fill the outgoing inteface to tell the upper layer
	 * to increment per-interface statistics.
	 */
	if (ifpp)
		*ifpp = ifp;

	/* Determine path MTU. */
	if ((error = ip6_getpmtu(ro_pmtu, ro, ifp, &finaldst, &mtu,
	    &alwaysfrag)) != 0)
		goto bad;

	/*
	 * The caller of this function may specify to use the minimum MTU
	 * in some cases.
	 * An advanced API option (IPV6_USE_MIN_MTU) can also override MTU
	 * setting.  The logic is a bit complicated; by default, unicast
	 * packets will follow path MTU while multicast packets will be sent at
	 * the minimum MTU.  If IP6PO_MINMTU_ALL is specified, all packets
	 * including unicast ones will be sent at the minimum MTU.  Multicast
	 * packets will always be sent at the minimum MTU unless
	 * IP6PO_MINMTU_DISABLE is explicitly specified.
	 * See RFC 3542 for more details.
	 */
	if (mtu > IPV6_MMTU) {
		if ((flags & IPV6_MINMTU))
			mtu = IPV6_MMTU;
		else if (opt && opt->ip6po_minmtu == IP6PO_MINMTU_ALL)
			mtu = IPV6_MMTU;
		else if (IN6_IS_ADDR_MULTICAST(&ip6->ip6_dst) &&
			 (opt == NULL ||
			  opt->ip6po_minmtu != IP6PO_MINMTU_DISABLE)) {
			mtu = IPV6_MMTU;
		}
	}

	/* Fake scoped addresses */
	if ((ifp->if_flags & IFF_LOOPBACK) != 0) {
		/*
		 * If source or destination address is a scoped address, and
		 * the packet is going to be sent to a loopback interface,
		 * we should keep the original interface.
		 */

		/*
		 * XXX: this is a very experimental and temporary solution.
		 * We eventually have sockaddr_in6 and use the sin6_scope_id
		 * field of the structure here.
		 * We rely on the consistency between two scope zone ids
		 * of source and destination, which should already be assured.
		 * Larger scopes than link will be supported in the future.
		 */
		origifp = NULL;
		if (IN6_IS_SCOPE_LINKLOCAL(&ip6->ip6_src))
			origifp = ifindex2ifnet[ntohs(ip6->ip6_src.s6_addr16[1])];
		else if (IN6_IS_SCOPE_LINKLOCAL(&ip6->ip6_dst))
			origifp = ifindex2ifnet[ntohs(ip6->ip6_dst.s6_addr16[1])];
		/*
		 * XXX: origifp can be NULL even in those two cases above.
		 * For example, if we remove the (only) link-local address
		 * from the loopback interface, and try to send a link-local
		 * address without link-id information.  Then the source
		 * address is ::1, and the destination address is the
		 * link-local address with its s6_addr16[1] being zero.
		 * What is worse, if the packet goes to the loopback interface
		 * by a default rejected route, the null pointer would be
		 * passed to looutput, and the kernel would hang.
		 * The following last resort would prevent such disaster.
		 */
		if (origifp == NULL)
			origifp = ifp;
	}
	else
		origifp = ifp;
	/*
	 * clear embedded scope identifiers if necessary.
	 * in6_clearscope will touch the addresses only when necessary.
	 */
	in6_clearscope(&ip6->ip6_src);
	in6_clearscope(&ip6->ip6_dst);

	/*
	 * Check with the firewall...
	 */
	if (ip6_fw_enable && ip6_fw_chk_ptr) {
		u_short port = 0;

		m->m_pkthdr.rcvif = NULL;	/* XXX */
		/* If ipfw says divert, we have to just drop packet */
		if ((*ip6_fw_chk_ptr)(&ip6, ifp, &port, &m)) {
			m_freem(m);
			goto done;
		}
		if (!m) {
			error = EACCES;
			goto done;
		}
	}

	/*
	 * If the outgoing packet contains a hop-by-hop options header,
	 * it must be examined and processed even by the source node.
	 * (RFC 2460, section 4.)
	 */
	if (exthdrs.ip6e_hbh) {
		struct ip6_hbh *hbh = mtod(exthdrs.ip6e_hbh, struct ip6_hbh *);
		u_int32_t dummy1; /* XXX unused */
		u_int32_t dummy2; /* XXX unused */

#ifdef DIAGNOSTIC
		if ((hbh->ip6h_len + 1) << 3 > exthdrs.ip6e_hbh->m_len)
			panic("ip6e_hbh is not continuous");
#endif
		/*
		 *  XXX: if we have to send an ICMPv6 error to the sender,
		 *       we need the M_LOOP flag since icmp6_error() expects
		 *       the IPv6 and the hop-by-hop options header are
		 *       continuous unless the flag is set.
		 */
		m->m_flags |= M_LOOP;
		m->m_pkthdr.rcvif = ifp;
		if (ip6_process_hopopts(m,
					(u_int8_t *)(hbh + 1),
					((hbh->ip6h_len + 1) << 3) -
					sizeof(struct ip6_hbh),
					&dummy1, &dummy2) < 0) {
			/* m was already freed at this point */
			error = EINVAL;/* better error? */
			goto done;
		}
		m->m_flags &= ~M_LOOP; /* XXX */
		m->m_pkthdr.rcvif = NULL;
	}

	/*
	 * Run through list of hooks for output packets.
	 */
	if (pfil_has_hooks(&inet6_pfil_hook)) {
		error = pfil_run_hooks(&inet6_pfil_hook, &m, ifp, PFIL_OUT);
		if (error != 0 || m == NULL)
			goto done;
		ip6 = mtod(m, struct ip6_hdr *);
	}

	/*
	 * Send the packet to the outgoing interface.
	 * If necessary, do IPv6 fragmentation before sending.
	 *
	 * the logic here is rather complex:
	 * 1: normal case (dontfrag == 0, alwaysfrag == 0)
	 * 1-a:	send as is if tlen <= path mtu
	 * 1-b:	fragment if tlen > path mtu
	 *
	 * 2: if user asks us not to fragment (dontfrag == 1)
	 * 2-a:	send as is if tlen <= interface mtu
	 * 2-b:	error if tlen > interface mtu
	 *
	 * 3: if we always need to attach fragment header (alwaysfrag == 1)
	 *	always fragment
	 *
	 * 4: if dontfrag == 1 && alwaysfrag == 1
	 *	error, as we cannot handle this conflicting request
	 */
	tlen = m->m_pkthdr.len;

	if (opt && (opt->ip6po_flags & IP6PO_DONTFRAG))
		dontfrag = 1;
	else
		dontfrag = 0;
	if (dontfrag && alwaysfrag) {	/* case 4 */
		/* conflicting request - can't transmit */
		error = EMSGSIZE;
		goto bad;
	}
	if (dontfrag && tlen > IN6_LINKMTU(ifp)) {	/* case 2-b */
		/*
		 * Even if the DONTFRAG option is specified, we cannot send the
		 * packet when the data length is larger than the MTU of the
		 * outgoing interface.
		 * Notify the error by sending IPV6_PATHMTU ancillary data as
		 * well as returning an error code (the latter is not described
		 * in the API spec.)
		 */
		u_int32_t mtu32;
		struct ip6ctlparam ip6cp;

		mtu32 = (u_int32_t)mtu;
		bzero(&ip6cp, sizeof(ip6cp));
		ip6cp.ip6c_cmdarg = (void *)&mtu32;
		kpfctlinput2(PRC_MSGSIZE, (struct sockaddr *)&ro_pmtu->ro_dst,
		    (void *)&ip6cp);

		error = EMSGSIZE;
		goto bad;
	}

	/*
	 * transmit packet without fragmentation
	 */
	if (dontfrag || (!alwaysfrag && tlen <= mtu)) {	/* case 1-a and 2-a */
		struct in6_ifaddr *ia6;

		ip6 = mtod(m, struct ip6_hdr *);
		ia6 = in6_ifawithifp(ifp, &ip6->ip6_src);
		if (ia6) {
			/* Record statistics for this interface address. */
			IFA_STAT_INC(&ia6->ia_ifa, opackets, 1);
			IFA_STAT_INC(&ia6->ia_ifa, obytes, m->m_pkthdr.len);
		}
#ifdef IPSEC
		/* clean ipsec history once it goes out of the node */
		ipsec_delaux(m);
#endif
		error = nd6_output(ifp, origifp, m, dst, ro->ro_rt);
		goto done;
	} 

	/*
	 * try to fragment the packet.  case 1-b and 3
	 */
	if (mtu < IPV6_MMTU) {
		/*
		 * note that path MTU is never less than IPV6_MMTU
		 * (see icmp6_input).
		 */
		error = EMSGSIZE;
		in6_ifstat_inc(ifp, ifs6_out_fragfail);
		goto bad;
	} else if (ip6->ip6_plen == 0) { /* jumbo payload cannot be fragmented */
		error = EMSGSIZE;
		in6_ifstat_inc(ifp, ifs6_out_fragfail);
		goto bad;
	} else {
		struct mbuf **mnext, *m_frgpart;
		struct ip6_frag *ip6f;
		u_int32_t id = htonl(ip6_id++);
		u_char nextproto;

		/*
		 * Too large for the destination or interface;
		 * fragment if possible.
		 * Must be able to put at least 8 bytes per fragment.
		 */
		hlen = unfragpartlen;
		if (mtu > IPV6_MAXPACKET)
			mtu = IPV6_MAXPACKET;

		len = (mtu - hlen - sizeof(struct ip6_frag)) & ~7;
		if (len < 8) {
			error = EMSGSIZE;
			in6_ifstat_inc(ifp, ifs6_out_fragfail);
			goto bad;
		}

		mnext = &m->m_nextpkt;

		/*
		 * Change the next header field of the last header in the
		 * unfragmentable part.
		 */
		if (exthdrs.ip6e_rthdr) {
			nextproto = *mtod(exthdrs.ip6e_rthdr, u_char *);
			*mtod(exthdrs.ip6e_rthdr, u_char *) = IPPROTO_FRAGMENT;
		} else if (exthdrs.ip6e_dest1) {
			nextproto = *mtod(exthdrs.ip6e_dest1, u_char *);
			*mtod(exthdrs.ip6e_dest1, u_char *) = IPPROTO_FRAGMENT;
		} else if (exthdrs.ip6e_hbh) {
			nextproto = *mtod(exthdrs.ip6e_hbh, u_char *);
			*mtod(exthdrs.ip6e_hbh, u_char *) = IPPROTO_FRAGMENT;
		} else {
			nextproto = ip6->ip6_nxt;
			ip6->ip6_nxt = IPPROTO_FRAGMENT;
		}

		/*
		 * Loop through length of segment after first fragment,
		 * make new header and copy data of each part and link onto
		 * chain.
		 */
		m0 = m;
		for (off = hlen; off < tlen; off += len) {
			MGETHDR(m, MB_DONTWAIT, MT_HEADER);
			if (!m) {
				error = ENOBUFS;
				ip6stat.ip6s_odropped++;
				goto sendorfree;
			}
			m->m_pkthdr.rcvif = NULL;
			m->m_flags = m0->m_flags & M_COPYFLAGS;
			*mnext = m;
			mnext = &m->m_nextpkt;
			m->m_data += max_linkhdr;
			mhip6 = mtod(m, struct ip6_hdr *);
			*mhip6 = *ip6;
			m->m_len = sizeof(*mhip6);
 			error = ip6_insertfraghdr(m0, m, hlen, &ip6f);
 			if (error) {
				ip6stat.ip6s_odropped++;
				goto sendorfree;
			}
			ip6f->ip6f_offlg = htons((u_short)((off - hlen) & ~7));
			if (off + len >= tlen)
				len = tlen - off;
			else
				ip6f->ip6f_offlg |= IP6F_MORE_FRAG;
			mhip6->ip6_plen = htons((u_short)(len + hlen +
							  sizeof(*ip6f) -
							  sizeof(struct ip6_hdr)));
			if ((m_frgpart = m_copy(m0, off, len)) == NULL) {
				error = ENOBUFS;
				ip6stat.ip6s_odropped++;
				goto sendorfree;
			}
			m_cat(m, m_frgpart);
			m->m_pkthdr.len = len + hlen + sizeof(*ip6f);
			m->m_pkthdr.rcvif = NULL;
			ip6f->ip6f_reserved = 0;
			ip6f->ip6f_ident = id;
			ip6f->ip6f_nxt = nextproto;
			ip6stat.ip6s_ofragments++;
			in6_ifstat_inc(ifp, ifs6_out_fragcreat);
		}

		in6_ifstat_inc(ifp, ifs6_out_fragok);
	}

	/*
	 * Remove leading garbages.
	 */
sendorfree:
	m = m0->m_nextpkt;
	m0->m_nextpkt = NULL;
	m_freem(m0);
	for (m0 = m; m; m = m0) {
		m0 = m->m_nextpkt;
		m->m_nextpkt = NULL;
		if (error == 0) {
 			/* Record statistics for this interface address. */
 			if (ia) {
 				IFA_STAT_INC(&ia->ia_ifa, opackets, 1);
 				IFA_STAT_INC(&ia->ia_ifa, obytes,
				    m->m_pkthdr.len);
 			}
#ifdef IPSEC
			/* clean ipsec history once it goes out of the node */
			ipsec_delaux(m);
#endif
			error = nd6_output(ifp, origifp, m, dst, ro->ro_rt);
		} else
			m_freem(m);
	}

	if (error == 0)
		ip6stat.ip6s_fragmented++;

done:
	if (ro == &ip6route && ro->ro_rt) { /* brace necessary for RTFREE */
		RTFREE(ro->ro_rt);
	} else if (ro_pmtu == &ip6route && ro_pmtu->ro_rt) {
		RTFREE(ro_pmtu->ro_rt);
	}

#ifdef IPSEC
	if (sp != NULL)
		key_freesp(sp);
#endif
#ifdef FAST_IPSEC
	if (sp != NULL)
		KEY_FREESP(&sp);
#endif

	return (error);

freehdrs:
	m_freem(exthdrs.ip6e_hbh);	/* m_freem will check if mbuf is 0 */
	m_freem(exthdrs.ip6e_dest1);
	m_freem(exthdrs.ip6e_rthdr);
	m_freem(exthdrs.ip6e_dest2);
	/* fall through */
bad:
	m_freem(m);
	goto done;
}

static int
copyexthdr(void *h, struct mbuf **mp)
{
	struct ip6_ext *hdr = h;
	int hlen;
	struct mbuf *m;

	if (hdr == NULL)
		return 0;

	hlen = (hdr->ip6e_len + 1) * 8;
	if (hlen > MCLBYTES)
		return ENOBUFS;	/* XXX */

	m = m_getb(hlen, MB_DONTWAIT, MT_DATA, 0);
	if (!m)
		return ENOBUFS;
	m->m_len = hlen;

	bcopy(hdr, mtod(m, caddr_t), hlen);

	*mp = m;
	return 0;
}

/*
 * Insert jumbo payload option.
 */
static int
ip6_insert_jumboopt(struct ip6_exthdrs *exthdrs, u_int32_t plen)
{
	struct mbuf *mopt;
	u_char *optbuf;
	u_int32_t v;

#define JUMBOOPTLEN	8	/* length of jumbo payload option and padding */

	/*
	 * If there is no hop-by-hop options header, allocate new one.
	 * If there is one but it doesn't have enough space to store the
	 * jumbo payload option, allocate a cluster to store the whole options.
	 * Otherwise, use it to store the options.
	 */
	if (exthdrs->ip6e_hbh == NULL) {
		MGET(mopt, MB_DONTWAIT, MT_DATA);
		if (mopt == NULL)
			return (ENOBUFS);
		mopt->m_len = JUMBOOPTLEN;
		optbuf = mtod(mopt, u_char *);
		optbuf[1] = 0;	/* = ((JUMBOOPTLEN) >> 3) - 1 */
		exthdrs->ip6e_hbh = mopt;
	} else {
		struct ip6_hbh *hbh;

		mopt = exthdrs->ip6e_hbh;
		if (M_TRAILINGSPACE(mopt) < JUMBOOPTLEN) {
			/*
			 * XXX assumption:
			 * - exthdrs->ip6e_hbh is not referenced from places
			 *   other than exthdrs.
			 * - exthdrs->ip6e_hbh is not an mbuf chain.
			 */
			int oldoptlen = mopt->m_len;
			struct mbuf *n;

			/*
			 * XXX: give up if the whole (new) hbh header does
			 * not fit even in an mbuf cluster.
			 */
			if (oldoptlen + JUMBOOPTLEN > MCLBYTES)
				return (ENOBUFS);

			/*
			 * As a consequence, we must always prepare a cluster
			 * at this point.
			 */
			n = m_getcl(MB_DONTWAIT, MT_DATA, 0);
			if (!n)
				return (ENOBUFS);
			n->m_len = oldoptlen + JUMBOOPTLEN;
			bcopy(mtod(mopt, caddr_t), mtod(n, caddr_t), oldoptlen);
			optbuf = mtod(n, caddr_t) + oldoptlen;
			m_freem(mopt);
			mopt = exthdrs->ip6e_hbh = n;
		} else {
			optbuf = mtod(mopt, u_char *) + mopt->m_len;
			mopt->m_len += JUMBOOPTLEN;
		}
		optbuf[0] = IP6OPT_PADN;
		optbuf[1] = 1;

		/*
		 * Adjust the header length according to the pad and
		 * the jumbo payload option.
		 */
		hbh = mtod(mopt, struct ip6_hbh *);
		hbh->ip6h_len += (JUMBOOPTLEN >> 3);
	}

	/* fill in the option. */
	optbuf[2] = IP6OPT_JUMBO;
	optbuf[3] = 4;
	v = (u_int32_t)htonl(plen + JUMBOOPTLEN);
	bcopy(&v, &optbuf[4], sizeof(u_int32_t));

	/* finally, adjust the packet header length */
	exthdrs->ip6e_ip6->m_pkthdr.len += JUMBOOPTLEN;

	return (0);
#undef JUMBOOPTLEN
}

/*
 * Insert fragment header and copy unfragmentable header portions.
 */
static int
ip6_insertfraghdr(struct mbuf *m0, struct mbuf *m, int hlen,
		  struct ip6_frag **frghdrp)
{
	struct mbuf *n, *mlast;

	if (hlen > sizeof(struct ip6_hdr)) {
		n = m_copym(m0, sizeof(struct ip6_hdr),
			    hlen - sizeof(struct ip6_hdr), MB_DONTWAIT);
		if (n == NULL)
			return (ENOBUFS);
		m->m_next = n;
	} else
		n = m;

	/* Search for the last mbuf of unfragmentable part. */
	for (mlast = n; mlast->m_next; mlast = mlast->m_next)
		;

	if (!(mlast->m_flags & M_EXT) &&
	    M_TRAILINGSPACE(mlast) >= sizeof(struct ip6_frag)) {
		/* use the trailing space of the last mbuf for the fragment hdr */
		*frghdrp =
			(struct ip6_frag *)(mtod(mlast, caddr_t) + mlast->m_len);
		mlast->m_len += sizeof(struct ip6_frag);
		m->m_pkthdr.len += sizeof(struct ip6_frag);
	} else {
		/* allocate a new mbuf for the fragment header */
		struct mbuf *mfrg;

		MGET(mfrg, MB_DONTWAIT, MT_DATA);
		if (mfrg == NULL)
			return (ENOBUFS);
		mfrg->m_len = sizeof(struct ip6_frag);
		*frghdrp = mtod(mfrg, struct ip6_frag *);
		mlast->m_next = mfrg;
	}

	return (0);
}

static int
ip6_getpmtu(struct route_in6 *ro_pmtu, struct route_in6 *ro,
    struct ifnet *ifp, struct in6_addr *dst, u_long *mtup,
    int *alwaysfragp)
{
	u_int32_t mtu = 0;
	int alwaysfrag = 0;
	int error = 0;

	if (ro_pmtu != ro) {
		/* The first hop and the final destination may differ. */
		struct sockaddr_in6 *sa6_dst =
		    (struct sockaddr_in6 *)&ro_pmtu->ro_dst;
		if (ro_pmtu->ro_rt &&
		    ((ro_pmtu->ro_rt->rt_flags & RTF_UP) == 0 ||
		     !IN6_ARE_ADDR_EQUAL(&sa6_dst->sin6_addr, dst))) {
			RTFREE(ro_pmtu->ro_rt);
			ro_pmtu->ro_rt = NULL;
		}
		if (ro_pmtu->ro_rt == NULL) {
			bzero(sa6_dst, sizeof(*sa6_dst));
			sa6_dst->sin6_family = AF_INET6;
			sa6_dst->sin6_len = sizeof(struct sockaddr_in6);
			sa6_dst->sin6_addr = *dst;

			rtalloc((struct route *)ro_pmtu);
		}
	}
	if (ro_pmtu->ro_rt) {
		u_int32_t ifmtu;
		struct in_conninfo inc;

		bzero(&inc, sizeof(inc));
		inc.inc_flags = 1; /* IPv6 */
		inc.inc6_faddr = *dst;

		if (ifp == NULL)
			ifp = ro_pmtu->ro_rt->rt_ifp;
		ifmtu = IN6_LINKMTU(ifp);
		mtu = ro_pmtu->ro_rt->rt_rmx.rmx_mtu;
		if (mtu == 0)
			mtu = ifmtu;
		else if (mtu < IPV6_MMTU) {
			/*
			 * RFC2460 section 5, last paragraph:
			 * if we record ICMPv6 too big message with
			 * mtu < IPV6_MMTU, transmit packets sized IPV6_MMTU
			 * or smaller, with framgent header attached.
			 * (fragment header is needed regardless from the
			 * packet size, for translators to identify packets)
			 */
			alwaysfrag = 1;
			mtu = IPV6_MMTU;
		} else if (mtu > ifmtu) {
			/*
			 * The MTU on the route is larger than the MTU on
			 * the interface!  This shouldn't happen, unless the
			 * MTU of the interface has been changed after the
			 * interface was brought up.  Change the MTU in the
			 * route to match the interface MTU (as long as the
			 * field isn't locked).
			 */
			mtu = ifmtu;
			ro_pmtu->ro_rt->rt_rmx.rmx_mtu = mtu;
		}
	} else if (ifp) {
		mtu = IN6_LINKMTU(ifp);
	} else
		error = EHOSTUNREACH; /* XXX */

	*mtup = mtu;
	if (alwaysfragp)
		*alwaysfragp = alwaysfrag;
	return (error);
}

/*
 * IP6 socket option processing.
 */
void
ip6_ctloutput_dispatch(netmsg_t msg)
{
	int error;

	error = ip6_ctloutput(msg->ctloutput.base.nm_so,
			      msg->ctloutput.nm_sopt);
	lwkt_replymsg(&msg->ctloutput.base.lmsg, error);
}

int
ip6_ctloutput(struct socket *so, struct sockopt *sopt)
{
	int optdatalen,uproto;
	int privileged;
	struct inpcb *in6p = so->so_pcb;
	void *optdata;
	int error, optval;
	int level, op, optname;
	int optlen;
	struct thread *td;

	if (sopt) {
		level = sopt->sopt_level;
		op = sopt->sopt_dir;
		optname = sopt->sopt_name;
		optlen = sopt->sopt_valsize;
		td = sopt->sopt_td;
	} else {
		panic("ip6_ctloutput: arg soopt is NULL");
		/* NOT REACHED */
		td = NULL;
	}
	error = optval = 0;

	uproto = (int)so->so_proto->pr_protocol;
	privileged = (td == NULL || priv_check(td, PRIV_ROOT)) ? 0 : 1;

	if (level == IPPROTO_IPV6) {
		switch (op) {

		case SOPT_SET:
			switch (optname) {
			case IPV6_2292PKTOPTIONS:
#ifdef IPV6_PKTOPTIONS
			case IPV6_PKTOPTIONS:
#endif
			{
				struct mbuf *m;

				error = soopt_getm(sopt, &m); /* XXX */
				if (error != 0)
					break;
				soopt_to_mbuf(sopt, m); /* XXX */
				error = ip6_pcbopts(&in6p->in6p_outputopts,
						    m, so, sopt);
				m_freem(m); /* XXX */
				break;
			}

			/*
			 * Use of some Hop-by-Hop options or some
			 * Destination options, might require special
			 * privilege.  That is, normal applications
			 * (without special privilege) might be forbidden
			 * from setting certain options in outgoing packets,
			 * and might never see certain options in received
			 * packets. [RFC 2292 Section 6]
			 * KAME specific note:
			 *  KAME prevents non-privileged users from sending or
			 *  receiving ANY hbh/dst options in order to avoid
			 *  overhead of parsing options in the kernel.
			 */
			case IPV6_RECVHOPOPTS:
			case IPV6_RECVDSTOPTS:
			case IPV6_RECVRTHDRDSTOPTS:
				if (!privileged)
					return (EPERM);
			case IPV6_RECVPKTINFO:
			case IPV6_RECVHOPLIMIT:
			case IPV6_RECVRTHDR:
			case IPV6_RECVPATHMTU:
			case IPV6_RECVTCLASS:
			case IPV6_AUTOFLOWLABEL:
			case IPV6_HOPLIMIT:
			/* FALLTHROUGH */
			case IPV6_UNICAST_HOPS:
			case IPV6_FAITH:

			case IPV6_V6ONLY:
				if (optlen != sizeof(int)) {
					error = EINVAL;
					break;
				}
				error = soopt_to_kbuf(sopt, &optval,
					sizeof optval, sizeof optval);
				if (error)
					break;
				switch (optname) {

				case IPV6_UNICAST_HOPS:
					if (optval < -1 || optval >= 256)
						error = EINVAL;
					else {
						/* -1 = kernel default */
						in6p->in6p_hops = optval;
					}
					break;
#define OPTSET(bit) \
do { \
	if (optval) \
		in6p->in6p_flags |= (bit); \
	else \
		in6p->in6p_flags &= ~(bit); \
} while (0)
#define OPTBIT(bit) (in6p->in6p_flags & (bit) ? 1 : 0)
/* 
 * Although changed to RFC3542, It's better to also support RFC2292 API 
 */
#define OPTSET2292(bit) \
do { \
	in6p->in6p_flags |= IN6P_RFC2292; \
	if (optval) \
		in6p->in6p_flags |= (bit); \
	else \
		in6p->in6p_flags &= ~(bit); \
} while (/*CONSTCOND*/ 0)

				case IPV6_RECVPKTINFO:
					/* cannot mix with RFC2292 */
					if (OPTBIT(IN6P_RFC2292)) {
						error = EINVAL;
						break;
					}
					OPTSET(IN6P_PKTINFO);
					break;

				case IPV6_HOPLIMIT:
				{
					struct ip6_pktopts **optp;

					/* cannot mix with RFC2292 */
					if (OPTBIT(IN6P_RFC2292)) {
						error = EINVAL;
						break;
					}
					optp = &in6p->in6p_outputopts;
					error = ip6_pcbopt(IPV6_HOPLIMIT,
					    (u_char *)&optval, sizeof(optval),
					    optp, uproto);
					break;
				}

				case IPV6_RECVHOPLIMIT:
					/* cannot mix with RFC2292 */
					if (OPTBIT(IN6P_RFC2292)) {
						error = EINVAL;
						break;
					}
					OPTSET(IN6P_HOPLIMIT);
					break;

				case IPV6_RECVHOPOPTS:
					/* cannot mix with RFC2292 */
					if (OPTBIT(IN6P_RFC2292)) {
						error = EINVAL;
						break;
					}
					OPTSET(IN6P_HOPOPTS);
					break;

				case IPV6_RECVDSTOPTS:
					/* cannot mix with RFC2292 */
					if (OPTBIT(IN6P_RFC2292)) {
						error = EINVAL;
						break;
					}
					OPTSET(IN6P_DSTOPTS);
					break;

				case IPV6_RECVRTHDRDSTOPTS:
					/* cannot mix with RFC2292 */
					if (OPTBIT(IN6P_RFC2292)) {
						error = EINVAL;
						break;
					}
					OPTSET(IN6P_RTHDRDSTOPTS);
					break;

				case IPV6_RECVRTHDR:
					/* cannot mix with RFC2292 */
					if (OPTBIT(IN6P_RFC2292)) {
						error = EINVAL;
						break;
					}
					OPTSET(IN6P_RTHDR);
					break;

				case IPV6_RECVPATHMTU:
					/*
					 * We ignore this option for TCP
					 * sockets.
					 * (RFC3542 leaves this case
					 * unspecified.)
					 */
					if (uproto != IPPROTO_TCP)
						OPTSET(IN6P_MTU);
					break;

				case IPV6_RECVTCLASS:
					/* cannot mix with RFC2292 XXX */
					if (OPTBIT(IN6P_RFC2292)) {
						error = EINVAL;
						break;
					}
					OPTSET(IN6P_TCLASS);
					break;

				case IPV6_AUTOFLOWLABEL:
					OPTSET(IN6P_AUTOFLOWLABEL);
					break;

				case IPV6_FAITH:
					OPTSET(IN6P_FAITH);
					break;

				case IPV6_V6ONLY:
					/*
					 * make setsockopt(IPV6_V6ONLY)
					 * available only prior to bind(2).
					 */
					if (in6p->in6p_lport ||
					    !IN6_IS_ADDR_UNSPECIFIED(&in6p->in6p_laddr))
					{
						error = EINVAL;
						break;
					}
					if (!optval) {
						/* Don't allow v4-mapped */
						error = EOPNOTSUPP;
					}
					break;
				}
				break;

			case IPV6_TCLASS:
			case IPV6_DONTFRAG:
			case IPV6_USE_MIN_MTU:
			case IPV6_PREFER_TEMPADDR:
				if (optlen != sizeof(optval)) {
					error = EINVAL;
					break;
				}
				error = soopt_to_kbuf(sopt, &optval,
					sizeof optval, sizeof optval);
				if (error)
					break;
				{
					struct ip6_pktopts **optp;
					optp = &in6p->in6p_outputopts;
					error = ip6_pcbopt(optname,
					    (u_char *)&optval, sizeof(optval),
					    optp, uproto);
					break;
				}

			case IPV6_2292PKTINFO:
			case IPV6_2292HOPLIMIT:
			case IPV6_2292HOPOPTS:
			case IPV6_2292DSTOPTS:
			case IPV6_2292RTHDR:
				/* RFC 2292 */
				if (optlen != sizeof(int)) {
					error = EINVAL;
					break;
				}
				error = soopt_to_kbuf(sopt, &optval,
					sizeof optval, sizeof optval);
				if (error)
					break;
				switch (optname) {
				case IPV6_2292PKTINFO:
					OPTSET2292(IN6P_PKTINFO);
					break;
				case IPV6_2292HOPLIMIT:
					OPTSET2292(IN6P_HOPLIMIT);
					break;
				case IPV6_2292HOPOPTS:
					/*
					 * Check super-user privilege.
					 * See comments for IPV6_RECVHOPOPTS.
					 */
					if (!privileged)
						return (EPERM);
					OPTSET2292(IN6P_HOPOPTS);
					break;
				case IPV6_2292DSTOPTS:
					if (!privileged)
						return (EPERM);
					OPTSET2292(IN6P_DSTOPTS|IN6P_RTHDRDSTOPTS); /* XXX */
					break;
				case IPV6_2292RTHDR:
					OPTSET2292(IN6P_RTHDR);
					break;
				}
				break;

			case IPV6_PKTINFO:
			case IPV6_HOPOPTS:
			case IPV6_RTHDR:
			case IPV6_DSTOPTS:
			case IPV6_RTHDRDSTOPTS:
			case IPV6_NEXTHOP:
			{
				/* 
				 * New advanced API (RFC3542) 
				 */
				u_char *optbuf;
				u_char optbuf_storage[MCLBYTES];
				int optlen;
				struct ip6_pktopts **optp;

				/* cannot mix with RFC2292 */
				if (OPTBIT(IN6P_RFC2292)) {
					error = EINVAL;
					break;
				}

				/*
				 * We only ensure valsize is not too large
				 * here.  Further validation will be done
				 * later.
				 */
				error = soopt_to_kbuf(sopt, optbuf_storage,
				    sizeof(optbuf_storage), 0);
				if (error)
					break;
				optlen = sopt->sopt_valsize;
				optbuf = optbuf_storage;
				optp = &in6p->in6p_outputopts;
				error = ip6_pcbopt(optname, optbuf, optlen,
				    optp, uproto);
				break;
			}	
#undef OPTSET

			case IPV6_MULTICAST_IF:
			case IPV6_MULTICAST_HOPS:
			case IPV6_MULTICAST_LOOP:
			case IPV6_JOIN_GROUP:
			case IPV6_LEAVE_GROUP:
			    {
				struct mbuf *m;
				if (sopt->sopt_valsize > MLEN) {
					error = EMSGSIZE;
					break;
				}
				/* XXX */
				MGET(m, sopt->sopt_td ? MB_WAIT : MB_DONTWAIT, MT_HEADER);
				if (m == NULL) {
					error = ENOBUFS;
					break;
				}
				m->m_len = sopt->sopt_valsize;
				error = soopt_to_kbuf(sopt, mtod(m, char *),
						    m->m_len, m->m_len);
				error =	ip6_setmoptions(sopt->sopt_name,
							&in6p->in6p_moptions,
							m);
				m_free(m);
			    }
				break;

			case IPV6_PORTRANGE:
				error = soopt_to_kbuf(sopt, &optval,
				    sizeof optval, sizeof optval);
				if (error)
					break;

				switch (optval) {
				case IPV6_PORTRANGE_DEFAULT:
					in6p->in6p_flags &= ~(IN6P_LOWPORT);
					in6p->in6p_flags &= ~(IN6P_HIGHPORT);
					break;

				case IPV6_PORTRANGE_HIGH:
					in6p->in6p_flags &= ~(IN6P_LOWPORT);
					in6p->in6p_flags |= IN6P_HIGHPORT;
					break;

				case IPV6_PORTRANGE_LOW:
					in6p->in6p_flags &= ~(IN6P_HIGHPORT);
					in6p->in6p_flags |= IN6P_LOWPORT;
					break;

				default:
					error = EINVAL;
					break;
				}
				break;

#if defined(IPSEC) || defined(FAST_IPSEC)
			case IPV6_IPSEC_POLICY:
			    {
				caddr_t req = NULL;
				size_t len = 0;
				struct mbuf *m;

				if ((error = soopt_getm(sopt, &m)) != 0) /* XXX */
					break;
				soopt_to_mbuf(sopt, m);		/* XXX */
				if (m) {
					req = mtod(m, caddr_t);
					len = m->m_len;
				}
				error = ipsec6_set_policy(in6p, optname, req,
							  len, privileged);
				m_freem(m);
			    }
				break;
#endif /* KAME IPSEC */

			case IPV6_FW_ADD:
			case IPV6_FW_DEL:
			case IPV6_FW_FLUSH:
			case IPV6_FW_ZERO:
			    {
				struct mbuf *m;
				struct mbuf **mp = &m;

				if (ip6_fw_ctl_ptr == NULL)
					return EINVAL;
				/* XXX */
				if ((error = soopt_getm(sopt, &m)) != 0)
					break;
				/* XXX */
				soopt_to_mbuf(sopt, m);
				error = (*ip6_fw_ctl_ptr)(optname, mp);
				m = *mp;
			    }
				break;

			default:
				error = ENOPROTOOPT;
				break;
			}
			break;

		case SOPT_GET:
			switch (optname) {
			case IPV6_2292PKTOPTIONS:
#ifdef IPV6_PKTOPTIONS
			case IPV6_PKTOPTIONS:
#endif
				/*
				 * RFC3542 (effectively) deprecated the
				 * semantics of the 2292-style pktoptions.
				 * Since it was not reliable in nature (i.e.,
				 * applications had to expect the lack of some
				 * information after all), it would make sense
				 * to simplify this part by always returning
				 * empty data.
				 */
				if (in6p->in6p_options) {
					struct mbuf *m;
					m = m_copym(in6p->in6p_options,
					    0, M_COPYALL, MB_WAIT);
					error = soopt_from_mbuf(sopt, m);
					if (error == 0)
						m_freem(m);
				} else
					sopt->sopt_valsize = 0;
				break;
		
			case IPV6_RECVHOPOPTS:
			case IPV6_RECVDSTOPTS:
			case IPV6_RECVRTHDRDSTOPTS:
			case IPV6_UNICAST_HOPS:
			case IPV6_RECVPKTINFO:
			case IPV6_RECVHOPLIMIT:
			case IPV6_RECVRTHDR:
			case IPV6_RECVPATHMTU:
			case IPV6_RECVTCLASS:
			case IPV6_AUTOFLOWLABEL:
			case IPV6_FAITH:
			case IPV6_V6ONLY:
			case IPV6_PORTRANGE:
				switch (optname) {

				case IPV6_RECVHOPOPTS:
					optval = OPTBIT(IN6P_HOPOPTS);
					break;

				case IPV6_RECVDSTOPTS:
					optval = OPTBIT(IN6P_DSTOPTS);
					break;

				case IPV6_RECVRTHDRDSTOPTS:
					optval = OPTBIT(IN6P_RTHDRDSTOPTS);
					break;

				case IPV6_RECVPKTINFO:
					optval = OPTBIT(IN6P_PKTINFO);
					break;

				case IPV6_RECVHOPLIMIT:
					optval = OPTBIT(IN6P_HOPLIMIT);
					break;

				case IPV6_RECVRTHDR:
					optval = OPTBIT(IN6P_RTHDR);
					break;

				case IPV6_RECVPATHMTU:
					optval = OPTBIT(IN6P_MTU);
					break;

				case IPV6_RECVTCLASS:
					optval = OPTBIT(IN6P_TCLASS);
					break;

				case IPV6_AUTOFLOWLABEL:
					optval = OPTBIT(IN6P_AUTOFLOWLABEL);
					break;


				case IPV6_UNICAST_HOPS:
					optval = in6p->in6p_hops;
					break;

				case IPV6_FAITH:
					optval = OPTBIT(IN6P_FAITH);
					break;

				case IPV6_V6ONLY:
					optval = 1;
					break;

				case IPV6_PORTRANGE:
				    {
					int flags;
					flags = in6p->in6p_flags;
					if (flags & IN6P_HIGHPORT)
						optval = IPV6_PORTRANGE_HIGH;
					else if (flags & IN6P_LOWPORT)
						optval = IPV6_PORTRANGE_LOW;
					else
						optval = 0;
					break;
				    }
				}
				soopt_from_kbuf(sopt, &optval,
 					sizeof optval);
				break;

			case IPV6_PATHMTU:
			{
				u_long pmtu = 0;
				struct ip6_mtuinfo mtuinfo;
				struct route_in6 sro;

				bzero(&sro, sizeof(sro));

				if (!(so->so_state & SS_ISCONNECTED))
					return (ENOTCONN);
				/*
				 * XXX: we dot not consider the case of source
				 * routing, or optional information to specify
				 * the outgoing interface.
				 */
				error = ip6_getpmtu(&sro, NULL, NULL,
				    &in6p->in6p_faddr, &pmtu, NULL);
				if (sro.ro_rt)
					RTFREE(sro.ro_rt);
				if (error)
					break;
				if (pmtu > IPV6_MAXPACKET)
					pmtu = IPV6_MAXPACKET;

				bzero(&mtuinfo, sizeof(mtuinfo));
				mtuinfo.ip6m_mtu = (u_int32_t)pmtu;
				optdata = (void *)&mtuinfo;
				optdatalen = sizeof(mtuinfo);
				soopt_from_kbuf(sopt, optdata,
				    optdatalen);
				break;
			}

			case IPV6_2292PKTINFO:
			case IPV6_2292HOPLIMIT:
			case IPV6_2292HOPOPTS:
			case IPV6_2292RTHDR:
			case IPV6_2292DSTOPTS:
				if (optname == IPV6_2292HOPOPTS ||
				    optname == IPV6_2292DSTOPTS ||
				    !privileged)
					return (EPERM);
				switch (optname) {
				case IPV6_2292PKTINFO:
					optval = OPTBIT(IN6P_PKTINFO);
					break;
				case IPV6_2292HOPLIMIT:
					optval = OPTBIT(IN6P_HOPLIMIT);
					break;
				case IPV6_2292HOPOPTS:
					if (!privileged)
						return (EPERM);
					optval = OPTBIT(IN6P_HOPOPTS);
					break;
				case IPV6_2292RTHDR:
					optval = OPTBIT(IN6P_RTHDR);
					break;
				case IPV6_2292DSTOPTS:
					if (!privileged)
						return (EPERM);
					optval = OPTBIT(IN6P_DSTOPTS|IN6P_RTHDRDSTOPTS);
					break;
				}
				soopt_from_kbuf(sopt, &optval,
 					sizeof optval);
				break;

			case IPV6_PKTINFO:
			case IPV6_HOPOPTS:
			case IPV6_RTHDR:
			case IPV6_DSTOPTS:
			case IPV6_RTHDRDSTOPTS:
			case IPV6_NEXTHOP:
			case IPV6_TCLASS:
			case IPV6_DONTFRAG:
			case IPV6_USE_MIN_MTU:
			case IPV6_PREFER_TEMPADDR:
				error = ip6_getpcbopt(in6p->in6p_outputopts,
				    optname, sopt);
				break;

			case IPV6_MULTICAST_IF:
			case IPV6_MULTICAST_HOPS:
			case IPV6_MULTICAST_LOOP:
			case IPV6_JOIN_GROUP:
			case IPV6_LEAVE_GROUP:
			    {
				struct mbuf *m;
				error = ip6_getmoptions(sopt->sopt_name,
						in6p->in6p_moptions, &m);
				if (error == 0)
					soopt_from_kbuf(sopt,
 						mtod(m, char *), m->m_len);
				m_freem(m);
			    }
				break;

#if defined(IPSEC) || defined(FAST_IPSEC)
			case IPV6_IPSEC_POLICY:
			  {
				caddr_t req = NULL;
				size_t len = 0;
				struct mbuf *m = NULL;
				struct mbuf **mp = &m;

				error = soopt_getm(sopt, &m); /* XXX */
				if (error != 0)
					break;
				soopt_to_mbuf(sopt, m); /* XXX */
				if (m) {
					req = mtod(m, caddr_t);
					len = m->m_len;
				}
				error = ipsec6_get_policy(in6p, req, len, mp);
				if (error == 0)
					error = soopt_from_mbuf(sopt, m); /*XXX*/
				if (error == 0 && m != NULL)
					m_freem(m);
				break;
			  }
#endif /* KAME IPSEC */

			case IPV6_FW_GET:
			  {
				struct mbuf *m;
				struct mbuf **mp = &m;

				if (ip6_fw_ctl_ptr == NULL)
				{
					return EINVAL;
				}
				error = (*ip6_fw_ctl_ptr)(optname, mp);
				if (error == 0)
					error = soopt_from_mbuf(sopt, m); /* XXX */
				if (error == 0 && m != NULL)
					m_freem(m);
			  }
				break;

			default:
				error = ENOPROTOOPT;
				break;
			}
			break;
		}
	} else {
		error = EINVAL;
	}
	return (error);
}

int
ip6_raw_ctloutput(struct socket *so, struct sockopt *sopt)
{
	int error = 0, optval, optlen;
	const int icmp6off = offsetof(struct icmp6_hdr, icmp6_cksum);
	struct in6pcb *in6p = sotoin6pcb(so);
	int level, op, optname;

	if (sopt) {
		level = sopt->sopt_level;
		op = sopt->sopt_dir;
		optname = sopt->sopt_name;
		optlen = sopt->sopt_valsize;
	} else
		panic("ip6_raw_ctloutput: arg soopt is NULL");

	if (level != IPPROTO_IPV6) {
		return (EINVAL);
	}

	switch (optname) {
	case IPV6_CHECKSUM:
		/*
		 * For ICMPv6 sockets, no modification allowed for checksum
		 * offset, permit "no change" values to help existing apps.
		 *
		 * RFC3542 says: "An attempt to set IPV6_CHECKSUM
		 * for an ICMPv6 socket will fail."
		 * The current behavior does not meet RFC3542.
		 */
		switch (op) {
		case SOPT_SET:
			if (optlen != sizeof(int)) {
				error = EINVAL;
				break;
			}
			error = soopt_to_kbuf(sopt, &optval,
				    sizeof optval, sizeof optval);
			if (error)
				break;
			if ((optval % 2) != 0) {
				/* the API assumes even offset values */
				error = EINVAL;
			} else if (so->so_proto->pr_protocol ==
			    IPPROTO_ICMPV6) {
				if (optval != icmp6off)
					error = EINVAL;
			} else
				in6p->in6p_cksum = optval;
			break;

		case SOPT_GET:
			if (so->so_proto->pr_protocol == IPPROTO_ICMPV6)
				optval = icmp6off;
			else
				optval = in6p->in6p_cksum;

			soopt_from_kbuf(sopt, &optval, sizeof(optval));
			break;

		default:
			error = EINVAL;
			break;
		}
		break;

	default:
		error = ENOPROTOOPT;
		break;
	}

	return (error);
}

/*
 * Set up IP6 options in pcb for insertion in output packets or
 * specifying behavior of outgoing packets.
 */
static int
ip6_pcbopts(struct ip6_pktopts **pktopt, struct mbuf *m,
    struct socket *so, struct sockopt *sopt)
{
	int priv = 0;
	struct ip6_pktopts *opt = *pktopt;
	int error = 0;

	/* turn off any old options. */
	if (opt) {
#ifdef DIAGNOSTIC
		if (opt->ip6po_pktinfo || opt->ip6po_nexthop ||
		    opt->ip6po_hbh || opt->ip6po_dest1 || opt->ip6po_dest2 ||
		    opt->ip6po_rhinfo.ip6po_rhi_rthdr)
			kprintf("ip6_pcbopts: all specified options are cleared.\n");
#endif
		ip6_clearpktopts(opt, -1);
	} else
		opt = kmalloc(sizeof(*opt), M_IP6OPT, M_WAITOK);
	*pktopt = NULL;

	if (!m || m->m_len == 0) {
		/*
		 * Only turning off any previous options, regardless of
		 * whether the opt is just created or given.
		 */
		kfree(opt, M_IP6OPT);
		return (0);
	}

	/*  set options specified by user. */
	if ((error = ip6_setpktoptions(m, opt, NULL, so->so_proto->pr_protocol, priv)) != 0) {
		ip6_clearpktopts(opt, -1); /* XXX: discard all options */
		kfree(opt, M_IP6OPT);
		return (error);
	}
	*pktopt = opt;
	return (0);
}


/*
 * Below three functions are introduced by merge to RFC3542
 */

static int
ip6_getpcbopt(struct ip6_pktopts *pktopt, int optname, struct sockopt *sopt)
{
	void *optdata = NULL;
	int optdatalen = 0;
	struct ip6_ext *ip6e;
	int error = 0;
	struct in6_pktinfo null_pktinfo;
	int deftclass = 0, on;
	int defminmtu = IP6PO_MINMTU_MCASTONLY;
	int defpreftemp = IP6PO_TEMPADDR_SYSTEM;

	switch (optname) {
	case IPV6_PKTINFO:
		if (pktopt && pktopt->ip6po_pktinfo)
			optdata = (void *)pktopt->ip6po_pktinfo;
		else {
			/* XXX: we don't have to do this every time... */
			bzero(&null_pktinfo, sizeof(null_pktinfo));
			optdata = (void *)&null_pktinfo;
		}
		optdatalen = sizeof(struct in6_pktinfo);
		break;
	case IPV6_TCLASS:
		if (pktopt && pktopt->ip6po_tclass >= 0)
			optdata = (void *)&pktopt->ip6po_tclass;
		else
			optdata = (void *)&deftclass;
		optdatalen = sizeof(int);
		break;
	case IPV6_HOPOPTS:
		if (pktopt && pktopt->ip6po_hbh) {
			optdata = (void *)pktopt->ip6po_hbh;
			ip6e = (struct ip6_ext *)pktopt->ip6po_hbh;
			optdatalen = (ip6e->ip6e_len + 1) << 3;
		}
		break;
	case IPV6_RTHDR:
		if (pktopt && pktopt->ip6po_rthdr) {
			optdata = (void *)pktopt->ip6po_rthdr;
			ip6e = (struct ip6_ext *)pktopt->ip6po_rthdr;
			optdatalen = (ip6e->ip6e_len + 1) << 3;
		}
		break;
	case IPV6_RTHDRDSTOPTS:
		if (pktopt && pktopt->ip6po_dest1) {
			optdata = (void *)pktopt->ip6po_dest1;
			ip6e = (struct ip6_ext *)pktopt->ip6po_dest1;
			optdatalen = (ip6e->ip6e_len + 1) << 3;
		}
		break;
	case IPV6_DSTOPTS:
		if (pktopt && pktopt->ip6po_dest2) {
			optdata = (void *)pktopt->ip6po_dest2;
			ip6e = (struct ip6_ext *)pktopt->ip6po_dest2;
			optdatalen = (ip6e->ip6e_len + 1) << 3;
		}
		break;
	case IPV6_NEXTHOP:
		if (pktopt && pktopt->ip6po_nexthop) {
			optdata = (void *)pktopt->ip6po_nexthop;
			optdatalen = pktopt->ip6po_nexthop->sa_len;
		}
		break;
	case IPV6_USE_MIN_MTU:
		if (pktopt)
			optdata = (void *)&pktopt->ip6po_minmtu;
		else
			optdata = (void *)&defminmtu;
		optdatalen = sizeof(int);
		break;
	case IPV6_DONTFRAG:
		if (pktopt && ((pktopt->ip6po_flags) & IP6PO_DONTFRAG))
			on = 1;
		else
			on = 0;
		optdata = (void *)&on;
		optdatalen = sizeof(on);
		break;
	case IPV6_PREFER_TEMPADDR:
		if (pktopt)
			optdata = (void *)&pktopt->ip6po_prefer_tempaddr;
		else
			optdata = (void *)&defpreftemp;
		optdatalen = sizeof(int);
		break;
	default:		/* should not happen */
#ifdef DIAGNOSTIC
		panic("ip6_getpcbopt: unexpected option");
#endif
		return (ENOPROTOOPT);
	}

	soopt_from_kbuf(sopt, optdata, optdatalen);

	return (error);
}

/*
 * initialize ip6_pktopts.  beware that there are non-zero default values in
 * the struct.
 */

static int
ip6_pcbopt(int optname, u_char *buf, int len, struct ip6_pktopts **pktopt, int uproto)
{
	struct ip6_pktopts *opt;
	int priv =0;
	if (*pktopt == NULL) {
		*pktopt = kmalloc(sizeof(*opt), M_IP6OPT, M_WAITOK);
		init_ip6pktopts(*pktopt);
	}
	opt = *pktopt;

	return (ip6_setpktoption(optname, buf, len, opt, 1, 0, uproto, priv));
}

/*
 * initialize ip6_pktopts.  beware that there are non-zero default values in
 * the struct.
 */
void
init_ip6pktopts(struct ip6_pktopts *opt)
{

	bzero(opt, sizeof(*opt));
	opt->ip6po_hlim = -1;	/* -1 means default hop limit */
	opt->ip6po_tclass = -1;	/* -1 means default traffic class */
	opt->ip6po_minmtu = IP6PO_MINMTU_MCASTONLY;
	opt->ip6po_prefer_tempaddr = IP6PO_TEMPADDR_SYSTEM;
}

void
ip6_clearpktopts(struct ip6_pktopts *pktopt, int optname)
{
	if (pktopt == NULL)
		return;

	if (optname == -1 || optname == IPV6_PKTINFO) {
		if (pktopt->ip6po_pktinfo)
			kfree(pktopt->ip6po_pktinfo, M_IP6OPT);
		pktopt->ip6po_pktinfo = NULL;
	}
	if (optname == -1 || optname == IPV6_HOPLIMIT)
		pktopt->ip6po_hlim = -1;
	if (optname == -1 || optname == IPV6_TCLASS)
		pktopt->ip6po_tclass = -1;
	if (optname == -1 || optname == IPV6_NEXTHOP) {
		if (pktopt->ip6po_nextroute.ro_rt) {
			RTFREE(pktopt->ip6po_nextroute.ro_rt);
			pktopt->ip6po_nextroute.ro_rt = NULL;
		}
		if (pktopt->ip6po_nexthop)
			kfree(pktopt->ip6po_nexthop, M_IP6OPT);
		pktopt->ip6po_nexthop = NULL;
	}
	if (optname == -1 || optname == IPV6_HOPOPTS) {
		if (pktopt->ip6po_hbh)
			kfree(pktopt->ip6po_hbh, M_IP6OPT);
		pktopt->ip6po_hbh = NULL;
	}
	if (optname == -1 || optname == IPV6_RTHDRDSTOPTS) {
		if (pktopt->ip6po_dest1)
			kfree(pktopt->ip6po_dest1, M_IP6OPT);
		pktopt->ip6po_dest1 = NULL;
	}
	if (optname == -1 || optname == IPV6_RTHDR) {
		if (pktopt->ip6po_rhinfo.ip6po_rhi_rthdr)
			kfree(pktopt->ip6po_rhinfo.ip6po_rhi_rthdr, M_IP6OPT);
		pktopt->ip6po_rhinfo.ip6po_rhi_rthdr = NULL;
		if (pktopt->ip6po_route.ro_rt) {
			RTFREE(pktopt->ip6po_route.ro_rt);
			pktopt->ip6po_route.ro_rt = NULL;
		}
	}
	if (optname == -1 || optname == IPV6_DSTOPTS) {
		if (pktopt->ip6po_dest2)
			kfree(pktopt->ip6po_dest2, M_IP6OPT);
		pktopt->ip6po_dest2 = NULL;
	}
}

#define PKTOPT_EXTHDRCPY(type) \
do {\
	if (src->type) {\
		int hlen =\
			(((struct ip6_ext *)src->type)->ip6e_len + 1) << 3;\
		dst->type = kmalloc(hlen, M_IP6OPT, canwait);\
		if (dst->type == NULL)\
			goto bad;\
		bcopy(src->type, dst->type, hlen);\
	}\
} while (0)

struct ip6_pktopts *
ip6_copypktopts(struct ip6_pktopts *src, int canwait)
{
	struct ip6_pktopts *dst;

	if (src == NULL) {
		kprintf("ip6_clearpktopts: invalid argument\n");
		return (NULL);
	}

	dst = kmalloc(sizeof(*dst), M_IP6OPT, canwait | M_ZERO);
	if (dst == NULL)
		return (NULL);

	dst->ip6po_hlim = src->ip6po_hlim;
	if (src->ip6po_pktinfo) {
		dst->ip6po_pktinfo = kmalloc(sizeof(*dst->ip6po_pktinfo),
					    M_IP6OPT, canwait);
		if (dst->ip6po_pktinfo == NULL)
			goto bad;
		*dst->ip6po_pktinfo = *src->ip6po_pktinfo;
	}
	if (src->ip6po_nexthop) {
		dst->ip6po_nexthop = kmalloc(src->ip6po_nexthop->sa_len,
					    M_IP6OPT, canwait);
		if (dst->ip6po_nexthop == NULL)
			goto bad;
		bcopy(src->ip6po_nexthop, dst->ip6po_nexthop,
		      src->ip6po_nexthop->sa_len);
	}
	PKTOPT_EXTHDRCPY(ip6po_hbh);
	PKTOPT_EXTHDRCPY(ip6po_dest1);
	PKTOPT_EXTHDRCPY(ip6po_dest2);
	PKTOPT_EXTHDRCPY(ip6po_rthdr); /* not copy the cached route */
	return (dst);

bad:
	if (dst->ip6po_pktinfo) kfree(dst->ip6po_pktinfo, M_IP6OPT);
	if (dst->ip6po_nexthop) kfree(dst->ip6po_nexthop, M_IP6OPT);
	if (dst->ip6po_hbh) kfree(dst->ip6po_hbh, M_IP6OPT);
	if (dst->ip6po_dest1) kfree(dst->ip6po_dest1, M_IP6OPT);
	if (dst->ip6po_dest2) kfree(dst->ip6po_dest2, M_IP6OPT);
	if (dst->ip6po_rthdr) kfree(dst->ip6po_rthdr, M_IP6OPT);
	kfree(dst, M_IP6OPT);
	return (NULL);
}

static int
copypktopts(struct ip6_pktopts *dst, struct ip6_pktopts *src, int canwait)
{
	if (dst == NULL || src == NULL)  {
#ifdef DIAGNOSTIC
		kprintf("ip6_clearpktopts: invalid argument\n");
#endif
		return (EINVAL);
	}

	dst->ip6po_hlim = src->ip6po_hlim;
	dst->ip6po_tclass = src->ip6po_tclass;
	dst->ip6po_flags = src->ip6po_flags;
	if (src->ip6po_pktinfo) {
		dst->ip6po_pktinfo = kmalloc(sizeof(*dst->ip6po_pktinfo),
		    M_IP6OPT, canwait);
		if (dst->ip6po_pktinfo == NULL)
			goto bad;
		*dst->ip6po_pktinfo = *src->ip6po_pktinfo;
	}
	if (src->ip6po_nexthop) {
		dst->ip6po_nexthop = kmalloc(src->ip6po_nexthop->sa_len,
		    M_IP6OPT, canwait);
		if (dst->ip6po_nexthop == NULL)
			goto bad;
		bcopy(src->ip6po_nexthop, dst->ip6po_nexthop,
		    src->ip6po_nexthop->sa_len);
	}
	PKTOPT_EXTHDRCPY(ip6po_hbh);
	PKTOPT_EXTHDRCPY(ip6po_dest1);
	PKTOPT_EXTHDRCPY(ip6po_dest2);
	PKTOPT_EXTHDRCPY(ip6po_rthdr); /* not copy the cached route */
	return (0);

  bad:
	ip6_clearpktopts(dst, -1);
	return (ENOBUFS);
}
#undef PKTOPT_EXTHDRCPY

void
ip6_freepcbopts(struct ip6_pktopts *pktopt)
{
	if (pktopt == NULL)
		return;

	ip6_clearpktopts(pktopt, -1);

	kfree(pktopt, M_IP6OPT);
}

/*
 * Set the IP6 multicast options in response to user setsockopt().
 */
static int
ip6_setmoptions(int optname, struct ip6_moptions **im6op, struct mbuf *m)
{
	int error = 0;
	u_int loop, ifindex;
	struct ipv6_mreq *mreq;
	struct ifnet *ifp;
	struct ip6_moptions *im6o = *im6op;
	struct route_in6 ro;
	struct sockaddr_in6 *dst;
	struct in6_multi_mship *imm;
	struct thread *td = curthread;	/* XXX */

	if (im6o == NULL) {
		/*
		 * No multicast option buffer attached to the pcb;
		 * allocate one and initialize to default values.
		 */
		im6o = (struct ip6_moptions *)
			kmalloc(sizeof(*im6o), M_IPMOPTS, M_WAITOK);

		*im6op = im6o;
		im6o->im6o_multicast_ifp = NULL;
		im6o->im6o_multicast_hlim = ip6_defmcasthlim;
		im6o->im6o_multicast_loop = IPV6_DEFAULT_MULTICAST_LOOP;
		LIST_INIT(&im6o->im6o_memberships);
	}

	switch (optname) {

	case IPV6_MULTICAST_IF:
		/*
		 * Select the interface for outgoing multicast packets.
		 */
		if (m == NULL || m->m_len != sizeof(u_int)) {
			error = EINVAL;
			break;
		}
		bcopy(mtod(m, u_int *), &ifindex, sizeof(ifindex));
		if (ifindex < 0 || if_index < ifindex) {
			error = ENXIO;	/* XXX EINVAL? */
			break;
		}
		ifp = ifindex2ifnet[ifindex];
		if (ifp == NULL || !(ifp->if_flags & IFF_MULTICAST)) {
			error = EADDRNOTAVAIL;
			break;
		}
		im6o->im6o_multicast_ifp = ifp;
		break;

	case IPV6_MULTICAST_HOPS:
	    {
		/*
		 * Set the IP6 hoplimit for outgoing multicast packets.
		 */
		int optval;
		if (m == NULL || m->m_len != sizeof(int)) {
			error = EINVAL;
			break;
		}
		bcopy(mtod(m, u_int *), &optval, sizeof(optval));
		if (optval < -1 || optval >= 256)
			error = EINVAL;
		else if (optval == -1)
			im6o->im6o_multicast_hlim = ip6_defmcasthlim;
		else
			im6o->im6o_multicast_hlim = optval;
		break;
	    }

	case IPV6_MULTICAST_LOOP:
		/*
		 * Set the loopback flag for outgoing multicast packets.
		 * Must be zero or one.
		 */
		if (m == NULL || m->m_len != sizeof(u_int)) {
			error = EINVAL;
			break;
		}
		bcopy(mtod(m, u_int *), &loop, sizeof(loop));
		if (loop > 1) {
			error = EINVAL;
			break;
		}
		im6o->im6o_multicast_loop = loop;
		break;

	case IPV6_JOIN_GROUP:
		/*
		 * Add a multicast group membership.
		 * Group must be a valid IP6 multicast address.
		 */
		if (m == NULL || m->m_len != sizeof(struct ipv6_mreq)) {
			error = EINVAL;
			break;
		}
		mreq = mtod(m, struct ipv6_mreq *);
		if (IN6_IS_ADDR_UNSPECIFIED(&mreq->ipv6mr_multiaddr)) {
			/*
			 * We use the unspecified address to specify to accept
			 * all multicast addresses. Only super user is allowed
			 * to do this.
			 */
			if (priv_check(td, PRIV_ROOT))
			{
				error = EACCES;
				break;
			}
		} else if (!IN6_IS_ADDR_MULTICAST(&mreq->ipv6mr_multiaddr)) {
			error = EINVAL;
			break;
		}

		/*
		 * If the interface is specified, validate it.
		 */
		if (mreq->ipv6mr_interface < 0
		 || if_index < mreq->ipv6mr_interface) {
			error = ENXIO;	/* XXX EINVAL? */
			break;
		}
		/*
		 * If no interface was explicitly specified, choose an
		 * appropriate one according to the given multicast address.
		 */
		if (mreq->ipv6mr_interface == 0) {
			/*
			 * If the multicast address is in node-local scope,
			 * the interface should be a loopback interface.
			 * Otherwise, look up the routing table for the
			 * address, and choose the outgoing interface.
			 *   XXX: is it a good approach?
			 */
			if (IN6_IS_ADDR_MC_NODELOCAL(&mreq->ipv6mr_multiaddr)) {
				ifp = &loif[0];
			} else {
				ro.ro_rt = NULL;
				dst = (struct sockaddr_in6 *)&ro.ro_dst;
				bzero(dst, sizeof(*dst));
				dst->sin6_len = sizeof(struct sockaddr_in6);
				dst->sin6_family = AF_INET6;
				dst->sin6_addr = mreq->ipv6mr_multiaddr;
				rtalloc((struct route *)&ro);
				if (ro.ro_rt == NULL) {
					error = EADDRNOTAVAIL;
					break;
				}
				ifp = ro.ro_rt->rt_ifp;
				rtfree(ro.ro_rt);
			}
		} else
			ifp = ifindex2ifnet[mreq->ipv6mr_interface];

		/*
		 * See if we found an interface, and confirm that it
		 * supports multicast
		 */
		if (ifp == NULL || !(ifp->if_flags & IFF_MULTICAST)) {
			error = EADDRNOTAVAIL;
			break;
		}
		/*
		 * Put interface index into the multicast address,
		 * if the address has link-local scope.
		 */
		if (IN6_IS_ADDR_MC_LINKLOCAL(&mreq->ipv6mr_multiaddr)) {
			mreq->ipv6mr_multiaddr.s6_addr16[1]
				= htons(mreq->ipv6mr_interface);
		}
		/*
		 * See if the membership already exists.
		 */
		for (imm = im6o->im6o_memberships.lh_first;
		     imm != NULL; imm = imm->i6mm_chain.le_next)
			if (imm->i6mm_maddr->in6m_ifp == ifp &&
			    IN6_ARE_ADDR_EQUAL(&imm->i6mm_maddr->in6m_addr,
					       &mreq->ipv6mr_multiaddr))
				break;
		if (imm != NULL) {
			error = EADDRINUSE;
			break;
		}
		/*
		 * Everything looks good; add a new record to the multicast
		 * address list for the given interface.
		 */
		imm = kmalloc(sizeof(*imm), M_IPMADDR, M_WAITOK);
		if ((imm->i6mm_maddr =
		     in6_addmulti(&mreq->ipv6mr_multiaddr, ifp, &error)) == NULL) {
			kfree(imm, M_IPMADDR);
			break;
		}
		LIST_INSERT_HEAD(&im6o->im6o_memberships, imm, i6mm_chain);
		break;

	case IPV6_LEAVE_GROUP:
		/*
		 * Drop a multicast group membership.
		 * Group must be a valid IP6 multicast address.
		 */
		if (m == NULL || m->m_len != sizeof(struct ipv6_mreq)) {
			error = EINVAL;
			break;
		}
		mreq = mtod(m, struct ipv6_mreq *);
		if (IN6_IS_ADDR_UNSPECIFIED(&mreq->ipv6mr_multiaddr)) {
			if (priv_check(td, PRIV_ROOT)) {
				error = EACCES;
				break;
			}
		} else if (!IN6_IS_ADDR_MULTICAST(&mreq->ipv6mr_multiaddr)) {
			error = EINVAL;
			break;
		}
		/*
		 * If an interface address was specified, get a pointer
		 * to its ifnet structure.
		 */
		if (mreq->ipv6mr_interface < 0
		 || if_index < mreq->ipv6mr_interface) {
			error = ENXIO;	/* XXX EINVAL? */
			break;
		}
		ifp = ifindex2ifnet[mreq->ipv6mr_interface];
		/*
		 * Put interface index into the multicast address,
		 * if the address has link-local scope.
		 */
		if (IN6_IS_ADDR_MC_LINKLOCAL(&mreq->ipv6mr_multiaddr)) {
			mreq->ipv6mr_multiaddr.s6_addr16[1]
				= htons(mreq->ipv6mr_interface);
		}
		/*
		 * Find the membership in the membership list.
		 */
		for (imm = im6o->im6o_memberships.lh_first;
		     imm != NULL; imm = imm->i6mm_chain.le_next) {
			if ((ifp == NULL ||
			     imm->i6mm_maddr->in6m_ifp == ifp) &&
			    IN6_ARE_ADDR_EQUAL(&imm->i6mm_maddr->in6m_addr,
					       &mreq->ipv6mr_multiaddr))
				break;
		}
		if (imm == NULL) {
			/* Unable to resolve interface */
			error = EADDRNOTAVAIL;
			break;
		}
		/*
		 * Give up the multicast address record to which the
		 * membership points.
		 */
		LIST_REMOVE(imm, i6mm_chain);
		in6_delmulti(imm->i6mm_maddr);
		kfree(imm, M_IPMADDR);
		break;

	default:
		error = EOPNOTSUPP;
		break;
	}

	/*
	 * If all options have default values, no need to keep the mbuf.
	 */
	if (im6o->im6o_multicast_ifp == NULL &&
	    im6o->im6o_multicast_hlim == ip6_defmcasthlim &&
	    im6o->im6o_multicast_loop == IPV6_DEFAULT_MULTICAST_LOOP &&
	    im6o->im6o_memberships.lh_first == NULL) {
		kfree(*im6op, M_IPMOPTS);
		*im6op = NULL;
	}

	return (error);
}

/*
 * Return the IP6 multicast options in response to user getsockopt().
 */
static int
ip6_getmoptions(int optname, struct ip6_moptions *im6o, struct mbuf **mp)
{
	u_int *hlim, *loop, *ifindex;

	*mp = m_get(MB_WAIT, MT_HEADER);		/* XXX */

	switch (optname) {

	case IPV6_MULTICAST_IF:
		ifindex = mtod(*mp, u_int *);
		(*mp)->m_len = sizeof(u_int);
		if (im6o == NULL || im6o->im6o_multicast_ifp == NULL)
			*ifindex = 0;
		else
			*ifindex = im6o->im6o_multicast_ifp->if_index;
		return (0);

	case IPV6_MULTICAST_HOPS:
		hlim = mtod(*mp, u_int *);
		(*mp)->m_len = sizeof(u_int);
		if (im6o == NULL)
			*hlim = ip6_defmcasthlim;
		else
			*hlim = im6o->im6o_multicast_hlim;
		return (0);

	case IPV6_MULTICAST_LOOP:
		loop = mtod(*mp, u_int *);
		(*mp)->m_len = sizeof(u_int);
		if (im6o == NULL)
			*loop = ip6_defmcasthlim;
		else
			*loop = im6o->im6o_multicast_loop;
		return (0);

	default:
		return (EOPNOTSUPP);
	}
}

/*
 * Discard the IP6 multicast options.
 */
void
ip6_freemoptions(struct ip6_moptions *im6o)
{
	struct in6_multi_mship *imm;

	if (im6o == NULL)
		return;

	while ((imm = im6o->im6o_memberships.lh_first) != NULL) {
		LIST_REMOVE(imm, i6mm_chain);
		if (imm->i6mm_maddr)
			in6_delmulti(imm->i6mm_maddr);
		kfree(imm, M_IPMADDR);
	}
	kfree(im6o, M_IPMOPTS);
}

/*
 * Set a particular packet option, as a sticky option or an ancillary data
 * item.  "len" can be 0 only when it's a sticky option.
 * We have 4 cases of combination of "sticky" and "cmsg":
 * "sticky=0, cmsg=0": impossible
 * "sticky=0, cmsg=1": RFC2292 or RFC3542 ancillary data
 * "sticky=1, cmsg=0": RFC3542 socket option
 * "sticky=1, cmsg=1": RFC2292 socket option
 */
static int
ip6_setpktoption(int optname, u_char *buf, int len, struct ip6_pktopts *opt,
     int sticky, int cmsg, int uproto, int priv)
{
	int minmtupolicy, preftemp;
	//int error;

	if (!sticky && !cmsg) {
		kprintf("ip6_setpktoption: impossible case\n");
		return (EINVAL);
	}

	/*
	 * IPV6_2292xxx is for backward compatibility to RFC2292, and should
	 * not be specified in the context of RFC3542.  Conversely,
	 * RFC3542 types should not be specified in the context of RFC2292.
	 */
	if (!cmsg) {
		switch (optname) {
		case IPV6_2292PKTINFO:
		case IPV6_2292HOPLIMIT:
		case IPV6_2292NEXTHOP:
		case IPV6_2292HOPOPTS:
		case IPV6_2292DSTOPTS:
		case IPV6_2292RTHDR:
		case IPV6_2292PKTOPTIONS:
			return (ENOPROTOOPT);
		}
	}
	if (sticky && cmsg) {
		switch (optname) {
		case IPV6_PKTINFO:
		case IPV6_HOPLIMIT:
		case IPV6_NEXTHOP:
		case IPV6_HOPOPTS:
		case IPV6_DSTOPTS:
		case IPV6_RTHDRDSTOPTS:
		case IPV6_RTHDR:
		case IPV6_USE_MIN_MTU:
		case IPV6_DONTFRAG:
		case IPV6_TCLASS:
		case IPV6_PREFER_TEMPADDR: /* XXX: not an RFC3542 option */
			return (ENOPROTOOPT);
		}
	}

	switch (optname) {
	case IPV6_2292PKTINFO:
	case IPV6_PKTINFO:
	{
		struct in6_pktinfo *pktinfo;
		if (len != sizeof(struct in6_pktinfo))
			return (EINVAL);
		pktinfo = (struct in6_pktinfo *)buf;

		/*
		 * An application can clear any sticky IPV6_PKTINFO option by
		 * doing a "regular" setsockopt with ipi6_addr being
		 * in6addr_any and ipi6_ifindex being zero.
		 * [RFC 3542, Section 6]
		 */
		if (optname == IPV6_PKTINFO && opt->ip6po_pktinfo &&
		    pktinfo->ipi6_ifindex == 0 &&
		    IN6_IS_ADDR_UNSPECIFIED(&pktinfo->ipi6_addr)) {
			ip6_clearpktopts(opt, optname);
			break;
		}

		if (uproto == IPPROTO_TCP && optname == IPV6_PKTINFO &&
		    sticky && !IN6_IS_ADDR_UNSPECIFIED(&pktinfo->ipi6_addr)) {
			return (EINVAL);
		}

		/* validate the interface index if specified. */
		if (pktinfo->ipi6_ifindex > if_index ||
		    pktinfo->ipi6_ifindex < 0) {
			 return (ENXIO);
		}
		/*
		 * Check if the requested source address is indeed a
		 * unicast address assigned to the node, and can be
		 * used as the packet's source address.
		 */
		if (opt->ip6po_pktinfo != NULL &&
		    !IN6_IS_ADDR_UNSPECIFIED(&opt->ip6po_pktinfo->ipi6_addr)) {
			struct in6_ifaddr *ia6;
			struct sockaddr_in6 sin6;

			bzero(&sin6, sizeof(sin6));
			sin6.sin6_len = sizeof(sin6);
			sin6.sin6_family = AF_INET6;
			sin6.sin6_addr =
			opt->ip6po_pktinfo->ipi6_addr;
			ia6 = (struct in6_ifaddr *)ifa_ifwithaddr(sin6tosa(&sin6));
			if (ia6 == NULL ||
				(ia6->ia6_flags & (IN6_IFF_ANYCAST |
					IN6_IFF_NOTREADY)) != 0)
			return (EADDRNOTAVAIL);
		}

		/*
		 * We store the address anyway, and let in6_selectsrc()
		 * validate the specified address.  This is because ipi6_addr
		 * may not have enough information about its scope zone, and
		 * we may need additional information (such as outgoing
		 * interface or the scope zone of a destination address) to
		 * disambiguate the scope.
		 * XXX: the delay of the validation may confuse the
		 * application when it is used as a sticky option.
		 */
		if (opt->ip6po_pktinfo == NULL) {
			opt->ip6po_pktinfo = kmalloc(sizeof(*pktinfo),
			    M_IP6OPT, M_NOWAIT);
			if (opt->ip6po_pktinfo == NULL)
				return (ENOBUFS);
		}
		bcopy(pktinfo, opt->ip6po_pktinfo, sizeof(*pktinfo));
		break;
	}

	case IPV6_2292HOPLIMIT:
	case IPV6_HOPLIMIT:
	{
		int *hlimp;

		/*
		 * RFC 3542 deprecated the usage of sticky IPV6_HOPLIMIT
		 * to simplify the ordering among hoplimit options.
		 */
		if (optname == IPV6_HOPLIMIT && sticky)
			return (ENOPROTOOPT);

		if (len != sizeof(int))
			return (EINVAL);
		hlimp = (int *)buf;
		if (*hlimp < -1 || *hlimp > 255)
			return (EINVAL);

		opt->ip6po_hlim = *hlimp;
		break;
	}

	case IPV6_TCLASS:
	{
		int tclass;

		if (len != sizeof(int))
			return (EINVAL);
		tclass = *(int *)buf;
		if (tclass < -1 || tclass > 255)
			return (EINVAL);

		opt->ip6po_tclass = tclass;
		break;
	}

	case IPV6_2292NEXTHOP:
	case IPV6_NEXTHOP:
		if (!priv)
			return (EPERM);

		if (len == 0) {	/* just remove the option */
			ip6_clearpktopts(opt, IPV6_NEXTHOP);
			break;
		}

		/* check if cmsg_len is large enough for sa_len */
		if (len < sizeof(struct sockaddr) || len < *buf)
			return (EINVAL);

		switch (((struct sockaddr *)buf)->sa_family) {
		case AF_INET6:
		{
			struct sockaddr_in6 *sa6 = (struct sockaddr_in6 *)buf;
			//int error;

			if (sa6->sin6_len != sizeof(struct sockaddr_in6))
				return (EINVAL);

			if (IN6_IS_ADDR_UNSPECIFIED(&sa6->sin6_addr) ||
			    IN6_IS_ADDR_MULTICAST(&sa6->sin6_addr)) {
				return (EINVAL);
			}
			break;
		}
		case AF_LINK:	/* should eventually be supported */
		default:
			return (EAFNOSUPPORT);
		}

		/* turn off the previous option, then set the new option. */
		ip6_clearpktopts(opt, IPV6_NEXTHOP);
		opt->ip6po_nexthop = kmalloc(*buf, M_IP6OPT, M_NOWAIT);
		if (opt->ip6po_nexthop == NULL)
			return (ENOBUFS);
		bcopy(buf, opt->ip6po_nexthop, *buf);
		break;

	case IPV6_2292HOPOPTS:
	case IPV6_HOPOPTS:
	{
		struct ip6_hbh *hbh;
		int hbhlen;

		/*
		 * XXX: We don't allow a non-privileged user to set ANY HbH
		 * options, since per-option restriction has too much
		 * overhead.
		 */
		if (!priv)
			return (EPERM);
		if (len == 0) {
			ip6_clearpktopts(opt, IPV6_HOPOPTS);
			break;	/* just remove the option */
		}

		/* message length validation */
		if (len < sizeof(struct ip6_hbh))
			return (EINVAL);
		hbh = (struct ip6_hbh *)buf;
		hbhlen = (hbh->ip6h_len + 1) << 3;
		if (len != hbhlen)
			return (EINVAL);

		/* turn off the previous option, then set the new option. */
		ip6_clearpktopts(opt, IPV6_HOPOPTS);
		opt->ip6po_hbh = kmalloc(hbhlen, M_IP6OPT, M_NOWAIT);
		if (opt->ip6po_hbh == NULL)
			return (ENOBUFS);
		bcopy(hbh, opt->ip6po_hbh, hbhlen);

		break;
	}

	case IPV6_2292DSTOPTS:
	case IPV6_DSTOPTS:
	case IPV6_RTHDRDSTOPTS:
	{
		struct ip6_dest *dest, **newdest = NULL;
		int destlen;
		if (!priv)
			return (EPERM);

		if (len == 0) {
			ip6_clearpktopts(opt, optname);
			break;	/* just remove the option */
		}

		/* message length validation */
		if (len < sizeof(struct ip6_dest))
			return (EINVAL);
		dest = (struct ip6_dest *)buf;
		destlen = (dest->ip6d_len + 1) << 3;
		if (len != destlen)
			return (EINVAL);

		/*
		 * Determine the position that the destination options header
		 * should be inserted; before or after the routing header.
		 */
		switch (optname) {
		case IPV6_2292DSTOPTS:
			/*
			 * The old advacned API is ambiguous on this point.
			 * Our approach is to determine the position based
			 * according to the existence of a routing header.
			 * Note, however, that this depends on the order of the
			 * extension headers in the ancillary data; the 1st
			 * part of the destination options header must appear
			 * before the routing header in the ancillary data,
			 * too.
			 * RFC3542 solved the ambiguity by introducing
			 * separate ancillary data or option types.
			 */
			if (opt->ip6po_rthdr == NULL)
				newdest = &opt->ip6po_dest1;
			else
				newdest = &opt->ip6po_dest2;
			break;
		case IPV6_RTHDRDSTOPTS:
			newdest = &opt->ip6po_dest1;
			break;
		case IPV6_DSTOPTS:
			newdest = &opt->ip6po_dest2;
			break;
		}

		/* turn off the previous option, then set the new option. */
		ip6_clearpktopts(opt, optname);
		*newdest = kmalloc(destlen, M_IP6OPT, M_NOWAIT);
		if (*newdest == NULL)
			return (ENOBUFS);
		bcopy(dest, *newdest, destlen);

		break;
	}

	case IPV6_2292RTHDR:
	case IPV6_RTHDR:
	{
		struct ip6_rthdr *rth;
		int rthlen;

		if (len == 0) {
			ip6_clearpktopts(opt, IPV6_RTHDR);
			break;	/* just remove the option */
		}

		/* message length validation */
		if (len < sizeof(struct ip6_rthdr))
			return (EINVAL);
		rth = (struct ip6_rthdr *)buf;
		rthlen = (rth->ip6r_len + 1) << 3;
		if (len != rthlen)
			return (EINVAL);

		switch (rth->ip6r_type) {
		default:
			return (EINVAL);	/* not supported */
		}

		/* turn off the previous option */
		ip6_clearpktopts(opt, IPV6_RTHDR);
		opt->ip6po_rthdr = kmalloc(rthlen, M_IP6OPT, M_NOWAIT);
		if (opt->ip6po_rthdr == NULL)
			return (ENOBUFS);
		bcopy(rth, opt->ip6po_rthdr, rthlen);

		break;
	}

	case IPV6_USE_MIN_MTU:
		if (len != sizeof(int))
			return (EINVAL);
		minmtupolicy = *(int *)buf;
		if (minmtupolicy != IP6PO_MINMTU_MCASTONLY &&
		    minmtupolicy != IP6PO_MINMTU_DISABLE &&
		    minmtupolicy != IP6PO_MINMTU_ALL) {
			return (EINVAL);
		}
		opt->ip6po_minmtu = minmtupolicy;
		break;

	case IPV6_DONTFRAG:
		if (len != sizeof(int))
			return (EINVAL);

		if (uproto == IPPROTO_TCP || *(int *)buf == 0) {
			/*
			 * we ignore this option for TCP sockets.
			 * (RFC3542 leaves this case unspecified.)
			 */
			opt->ip6po_flags &= ~IP6PO_DONTFRAG;
		} else
			opt->ip6po_flags |= IP6PO_DONTFRAG;
		break;

	case IPV6_PREFER_TEMPADDR:
		if (len != sizeof(int))
			return (EINVAL);
		preftemp = *(int *)buf;
		if (preftemp != IP6PO_TEMPADDR_SYSTEM &&
		    preftemp != IP6PO_TEMPADDR_NOTPREFER &&
		    preftemp != IP6PO_TEMPADDR_PREFER) {
			return (EINVAL);
		}
		opt->ip6po_prefer_tempaddr = preftemp;
		break;

	default:
		return (ENOPROTOOPT);
	} /* end of switch */

	return (0);
}


/*
 * Set IPv6 outgoing packet options based on advanced API.
 */
int
ip6_setpktoptions(struct mbuf *control, struct ip6_pktopts *opt,
    struct ip6_pktopts *stickyopt, int uproto, int priv)
{
	struct cmsghdr *cm = NULL;

	if (control == NULL || opt == NULL)
		return (EINVAL);

	init_ip6pktopts(opt);

	/*
	 * XXX: Currently, we assume all the optional information is stored
	 * in a single mbuf.
	 */
	if (stickyopt) {
		int error;

		/*
		 * If stickyopt is provided, make a local copy of the options
		 * for this particular packet, then override them by ancillary
		 * objects.
		 * XXX: copypktopts() does not copy the cached route to a next
		 * hop (if any).  This is not very good in terms of efficiency,
		 * but we can allow this since this option should be rarely
		 * used.
		 */
		if ((error = copypktopts(opt, stickyopt, M_NOWAIT)) != 0)
			return (error);
	}

	/*
	 * XXX: Currently, we assume all the optional information is stored
	 * in a single mbuf.
	 */
	if (control->m_next)
		return (EINVAL);

	for (; control->m_len; control->m_data += CMSG_ALIGN(cm->cmsg_len),
	    control->m_len -= CMSG_ALIGN(cm->cmsg_len)) {
		int error;

		if (control->m_len < CMSG_LEN(0))
			return (EINVAL);

		cm = mtod(control, struct cmsghdr *);
		if (cm->cmsg_len == 0 || cm->cmsg_len > control->m_len)
			return (EINVAL);
		if (cm->cmsg_level != IPPROTO_IPV6)
			continue;

		error = ip6_setpktoption(cm->cmsg_type, CMSG_DATA(cm),
		    cm->cmsg_len - CMSG_LEN(0), opt, 0, 1, uproto, priv);
		if (error)
			return (error);
	}

	return (0);
}

/*
 * Routine called from ip6_output() to loop back a copy of an IP6 multicast
 * packet to the input queue of a specified interface.  Note that this
 * calls the output routine of the loopback "driver", but with an interface
 * pointer that might NOT be &loif -- easier than replicating that code here.
 */
void
ip6_mloopback(struct ifnet *ifp, struct mbuf *m, struct sockaddr_in6 *dst)
{
	struct mbuf *copym;
	struct ip6_hdr *ip6;

	copym = m_copy(m, 0, M_COPYALL);
	if (copym == NULL)
		return;

	/*
	 * Make sure to deep-copy IPv6 header portion in case the data
	 * is in an mbuf cluster, so that we can safely override the IPv6
	 * header portion later.
	 */
	if ((copym->m_flags & M_EXT) != 0 ||
	    copym->m_len < sizeof(struct ip6_hdr)) {
		copym = m_pullup(copym, sizeof(struct ip6_hdr));
		if (copym == NULL)
			return;
	}

#ifdef DIAGNOSTIC
	if (copym->m_len < sizeof(*ip6)) {
		m_freem(copym);
		return;
	}
#endif

	ip6 = mtod(copym, struct ip6_hdr *);
	/*
	 * clear embedded scope identifiers if necessary.
	 * in6_clearscope will touch the addresses only when necessary.
	 */
	in6_clearscope(&ip6->ip6_src);
	in6_clearscope(&ip6->ip6_dst);

	if_simloop(ifp, copym, dst->sin6_family, 0);
}

/*
 * Separate the IPv6 header from the payload into its own mbuf.
 *
 * Returns the new mbuf chain or the original mbuf if no payload.
 * Returns NULL if can't allocate new mbuf for header.
 */
static struct mbuf *
ip6_splithdr(struct mbuf *m)
{
	struct mbuf *mh;

	if (m->m_len <= sizeof(struct ip6_hdr))		/* no payload */
		return (m);

	MGETHDR(mh, MB_DONTWAIT, MT_HEADER);
	if (mh == NULL)
		return (NULL);
	mh->m_len = sizeof(struct ip6_hdr);
	M_MOVE_PKTHDR(mh, m);
	MH_ALIGN(mh, sizeof(struct ip6_hdr));
	bcopy(mtod(m, caddr_t), mtod(mh, caddr_t), sizeof(struct ip6_hdr));
	m->m_data += sizeof(struct ip6_hdr);
	m->m_len -= sizeof(struct ip6_hdr);
	mh->m_next = m;
	return (mh);
}

/*
 * Compute IPv6 extension header length.
 */
int
ip6_optlen(struct in6pcb *in6p)
{
	int len;

	if (!in6p->in6p_outputopts)
		return 0;

	len = 0;
#define elen(x) \
    (((struct ip6_ext *)(x)) ? (((struct ip6_ext *)(x))->ip6e_len + 1) << 3 : 0)

	len += elen(in6p->in6p_outputopts->ip6po_hbh);
	if (in6p->in6p_outputopts->ip6po_rthdr)
		/* dest1 is valid with rthdr only */
		len += elen(in6p->in6p_outputopts->ip6po_dest1);
	len += elen(in6p->in6p_outputopts->ip6po_rthdr);
	len += elen(in6p->in6p_outputopts->ip6po_dest2);
	return len;
#undef elen
}
