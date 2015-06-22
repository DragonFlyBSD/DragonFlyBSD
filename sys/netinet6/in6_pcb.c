/*	$FreeBSD: src/sys/netinet6/in6_pcb.c,v 1.10.2.9 2003/01/24 05:11:35 sam Exp $	*/
/*	$KAME: in6_pcb.c,v 1.31 2001/05/21 05:45:10 jinmei Exp $	*/

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
 *
 */

/*
 * Copyright (c) 1982, 1986, 1991, 1993
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
 *	@(#)in_pcb.c	8.2 (Berkeley) 1/4/94
 */

#include "opt_inet.h"
#include "opt_inet6.h"
#include "opt_ipsec.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/domain.h>
#include <sys/protosw.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/sockio.h>
#include <sys/errno.h>
#include <sys/time.h>
#include <sys/proc.h>
#include <sys/priv.h>
#include <sys/jail.h>

#include <sys/thread2.h>
#include <sys/msgport2.h>

#include <vm/vm_zone.h>

#include <net/if.h>
#include <net/if_types.h>
#include <net/route.h>
#include <net/netisr2.h>

#include <netinet/in.h>
#include <netinet/in_var.h>
#include <netinet/in_systm.h>
#include <netinet/ip6.h>
#include <netinet/ip_var.h>
#include <netinet6/ip6_var.h>
#include <netinet6/nd6.h>
#include <netinet/in_pcb.h>
#include <netinet6/in6_pcb.h>

#ifdef IPSEC
#include <netinet6/ipsec.h>
#ifdef INET6
#include <netinet6/ipsec6.h>
#endif
#include <netinet6/ah.h>
#ifdef INET6
#include <netinet6/ah6.h>
#endif
#include <netproto/key/key.h>
#endif /* IPSEC */

#ifdef FAST_IPSEC
#include <netproto/ipsec/ipsec.h>
#include <netproto/ipsec/ipsec6.h>
#include <netproto/ipsec/key.h>
#define	IPSEC
#endif /* FAST_IPSEC */

struct	in6_addr zeroin6_addr;

int
in6_pcbbind(struct inpcb *inp, struct sockaddr *nam, struct thread *td)
{
	struct socket *so = inp->inp_socket;
	struct sockaddr_in6 jsin6;
	int error;

	if (!in6_ifaddr) /* XXX broken! */
		return (EADDRNOTAVAIL);
	if (inp->inp_lport || !IN6_IS_ADDR_UNSPECIFIED(&inp->in6p_laddr))
		return (EINVAL);

	if (nam) {
		struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *)nam;
		struct inpcbinfo *pcbinfo;
		struct inpcbportinfo *portinfo;
		int wild = 0, reuseport = (so->so_options & SO_REUSEPORT);
		struct ucred *cred = NULL;
		struct inpcb *t;
		u_short	lport, lport_ho;

		if ((so->so_options & (SO_REUSEADDR|SO_REUSEPORT)) == 0)
			wild = 1;
		if (td->td_proc != NULL)
			cred = td->td_proc->p_ucred;

		if (nam->sa_len != sizeof(*sin6))
			return (EINVAL);
		/*
		 * family check.
		 */
		if (nam->sa_family != AF_INET6)
			return (EAFNOSUPPORT);

		/* Reject v4-mapped address */
		if (IN6_IS_ADDR_V4MAPPED(&sin6->sin6_addr))
			return (EADDRNOTAVAIL);

		if (!prison_replace_wildcards(td, nam))
			return (EINVAL);

		/* KAME hack: embed scopeid */
		if (in6_embedscope(&sin6->sin6_addr, sin6, inp, NULL) != 0)
			return (EINVAL);
		/* this must be cleared for ifa_ifwithaddr() */
		sin6->sin6_scope_id = 0;

		lport = sin6->sin6_port;
		if (IN6_IS_ADDR_MULTICAST(&sin6->sin6_addr)) {
			/*
			 * Treat SO_REUSEADDR as SO_REUSEPORT for multicast;
			 * allow compepte duplication of binding if
			 * SO_REUSEPORT is set, or if SO_REUSEADDR is set
			 * and a multicast address is bound on both
			 * new and duplicated sockets.
			 */
			if (so->so_options & SO_REUSEADDR)
				reuseport = SO_REUSEADDR|SO_REUSEPORT;
		} else if (!IN6_IS_ADDR_UNSPECIFIED(&sin6->sin6_addr)) {
			struct ifaddr *ia = NULL;

			sin6->sin6_port = 0;		/* yech... */
			if (!prison_replace_wildcards(td, (struct sockaddr *)sin6)) {
				sin6->sin6_addr = kin6addr_any;
				return (EINVAL);
			}
			if ((ia = ifa_ifwithaddr((struct sockaddr *)sin6)) == NULL)
				return (EADDRNOTAVAIL);

			/*
			 * XXX: bind to an anycast address might accidentally
			 * cause sending a packet with anycast source address.
			 * We should allow to bind to a deprecated address, since
			 * the application dare to use it.
			 */
			if (ia &&
			    ((struct in6_ifaddr *)ia)->ia6_flags &
			    (IN6_IFF_ANYCAST|IN6_IFF_NOTREADY|IN6_IFF_DETACHED))
				return (EADDRNOTAVAIL);
		}

		inp->in6p_laddr = sin6->sin6_addr;

		if (lport == 0)
			goto auto_select;
		lport_ho = ntohs(lport);

		/* GROSS */
		if (lport_ho < IPV6PORT_RESERVED && cred &&
		    priv_check_cred(cred, PRIV_NETINET_RESERVEDPORT, 0)) {
			inp->in6p_laddr = kin6addr_any;
			return (EACCES);
		}

		/*
		 * Locate the proper portinfo based on lport
		 */
		pcbinfo = inp->inp_pcbinfo;
		portinfo =
		    &pcbinfo->portinfo[lport_ho & pcbinfo->portinfo_mask];
		KKASSERT((lport_ho & pcbinfo->portinfo_mask) ==
		    portinfo->offset);

		/*
		 * This has to be atomic.  If the porthash is shared across
		 * multiple protocol threads (aka tcp) then the token must
		 * be held.
		 */
		GET_PORT_TOKEN(portinfo);

		if (so->so_cred->cr_uid != 0 &&
		    !IN6_IS_ADDR_MULTICAST(&sin6->sin6_addr)) {
			t = in6_pcblookup_local(portinfo,
			    &sin6->sin6_addr, lport, INPLOOKUP_WILDCARD, cred);
			if (t &&
			    (so->so_cred->cr_uid !=
			     t->inp_socket->so_cred->cr_uid)) {
				inp->in6p_laddr = kin6addr_any;
				error = EADDRINUSE;
				goto done;
			}
		}
		if (cred && cred->cr_prison &&
		    !prison_replace_wildcards(td, nam)) {
			inp->in6p_laddr = kin6addr_any;
			error = EADDRNOTAVAIL;
			goto done;
		}
		t = in6_pcblookup_local(portinfo, &sin6->sin6_addr, lport,
		    wild, cred);
		if (t && (reuseport & t->inp_socket->so_options) == 0) {
			inp->in6p_laddr = kin6addr_any;
			error = EADDRINUSE;
			goto done;
		}

		inp->inp_lport = lport;
		in_pcbinsporthash(portinfo, inp);
		error = 0;
done:
		REL_PORT_TOKEN(portinfo);
		return (error);
	} else {
auto_select:
		jsin6.sin6_addr = inp->in6p_laddr;
		jsin6.sin6_family = AF_INET6;
		if (!prison_replace_wildcards(td, (struct sockaddr*)&jsin6)) {
			inp->in6p_laddr = kin6addr_any;
			inp->inp_lport = 0;
			return (EINVAL);
		}

		return in6_pcbsetlport(&inp->in6p_laddr, inp, td);
	}
}

/*
 *   Transform old in6_pcbconnect() into an inner subroutine for new
 *   in6_pcbconnect(): Do some validity-checking on the remote
 *   address (in mbuf 'nam') and then determine local host address
 *   (i.e., which interface) to use to access that remote host.
 *
 *   This preserves definition of in6_pcbconnect(), while supporting a
 *   slightly different version for T/TCP.  (This is more than
 *   a bit of a kludge, but cleaning up the internal interfaces would
 *   have forced minor changes in every protocol).
 */

int
in6_pcbladdr(struct inpcb *inp, struct sockaddr *nam,
	     struct in6_addr **plocal_addr6, struct thread *td)
{
	struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *)nam;
	struct ifnet *ifp = NULL;
	int error = 0;

	if (nam->sa_len != sizeof (*sin6))
		return (EINVAL);
	if (sin6->sin6_family != AF_INET6)
		return (EAFNOSUPPORT);
	if (sin6->sin6_port == 0)
		return (EADDRNOTAVAIL);

	/* KAME hack: embed scopeid */
	if (in6_embedscope(&sin6->sin6_addr, sin6, inp, &ifp) != 0)
		return EINVAL;

	if (in6_ifaddr) {
		/*
		 * If the destination address is UNSPECIFIED addr,
		 * use the loopback addr, e.g ::1.
		 */
		if (IN6_IS_ADDR_UNSPECIFIED(&sin6->sin6_addr))
			sin6->sin6_addr = kin6addr_loopback;
	}
	{
		/*
		 * XXX: in6_selectsrc might replace the bound local address
		 * with the address specified by setsockopt(IPV6_PKTINFO).
		 * Is it the intended behavior?
		 */
		*plocal_addr6 = in6_selectsrc(sin6, inp->in6p_outputopts,
					      inp->in6p_moptions,
					      &inp->in6p_route,
					      &inp->in6p_laddr, &error, td);
		if (*plocal_addr6 == NULL) {
			if (error == 0)
				error = EADDRNOTAVAIL;
			return (error);
		}
		/*
		 * Don't do pcblookup call here; return interface in
		 * plocal_addr6
		 * and exit to caller, that will do the lookup.
		 */
	}

	if (inp->in6p_route.ro_rt)
		ifp = inp->in6p_route.ro_rt->rt_ifp;

	return (0);
}

/*
 * Outer subroutine:
 * Connect from a socket to a specified address.
 * Both address and port must be specified in argument sin.
 * If don't have a local address for this socket yet,
 * then pick one.
 */
int
in6_pcbconnect(struct inpcb *inp, struct sockaddr *nam, struct thread *td)
{
	struct in6_addr *addr6;
	struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *)nam;
	int error;

	/* Reject v4-mapped address */
	if (IN6_IS_ADDR_V4MAPPED(&sin6->sin6_addr))
		return EADDRNOTAVAIL;

	/*
	 * Call inner routine, to assign local interface address.
	 * in6_pcbladdr() may automatically fill in sin6_scope_id.
	 */
	if ((error = in6_pcbladdr(inp, nam, &addr6, td)) != 0)
		return (error);

	if (in6_pcblookup_hash(inp->inp_pcbinfo, &sin6->sin6_addr,
			       sin6->sin6_port,
			      IN6_IS_ADDR_UNSPECIFIED(&inp->in6p_laddr)
			      ? addr6 : &inp->in6p_laddr,
			      inp->inp_lport, 0, NULL) != NULL) {
		return (EADDRINUSE);
	}
	if (IN6_IS_ADDR_UNSPECIFIED(&inp->in6p_laddr)) {
		if (inp->inp_lport == 0) {
			error = in6_pcbbind(inp, NULL, td);
			if (error)
				return (error);
		}
		inp->in6p_laddr = *addr6;
	}
	inp->in6p_faddr = sin6->sin6_addr;
	inp->inp_fport = sin6->sin6_port;
	/* update flowinfo - draft-itojun-ipv6-flowlabel-api-00 */
	inp->in6p_flowinfo &= ~IPV6_FLOWLABEL_MASK;
	if (inp->in6p_flags & IN6P_AUTOFLOWLABEL)
		inp->in6p_flowinfo |=
		    (htonl(ip6_flow_seq++) & IPV6_FLOWLABEL_MASK);

	in_pcbinsconnhash(inp);
	return (0);
}

#if 0
/*
 * Return an IPv6 address, which is the most appropriate for given
 * destination and user specified options.
 * If necessary, this function lookups the routing table and return
 * an entry to the caller for later use.
 */
struct in6_addr *
in6_selectsrc(struct sockaddr_in6 *dstsock, struct ip6_pktopts *opts,
	      struct ip6_moptions *mopts, struct route_in6 *ro,
	      struct in6_addr *laddr, int *errorp, struct thread *td)
{
	struct sockaddr_in6 jsin6;
	struct ucred *cred = NULL;
	struct in6_addr *dst;
	struct in6_ifaddr *ia6 = 0;
	struct in6_pktinfo *pi = NULL;
	int jailed = 0;

	if (td && td->td_proc && td->td_proc->p_ucred)
		cred = td->td_proc->p_ucred;
	if (cred && cred->cr_prison)
		jailed = 1;
	jsin6.sin6_family = AF_INET6;
	dst = &dstsock->sin6_addr;
	*errorp = 0;

	/*
	 * If the source address is explicitly specified by the caller,
	 * use it.
	 */
	if (opts && (pi = opts->ip6po_pktinfo) &&
	    !IN6_IS_ADDR_UNSPECIFIED(&pi->ipi6_addr)) {
		jsin6.sin6_addr = pi->ipi6_addr;
		if (jailed && !jailed_ip(cred->cr_prison,
		    (struct sockaddr *)&jsin6)) {
			return(0);
		} else {
			return (&pi->ipi6_addr);
		}
	}

	/*
	 * If the source address is not specified but the socket(if any)
	 * is already bound, use the bound address.
	 */
	if (laddr && !IN6_IS_ADDR_UNSPECIFIED(laddr)) {
		jsin6.sin6_addr = *laddr;
		if (jailed && !jailed_ip(cred->cr_prison,
		    (struct sockaddr *)&jsin6)) {
			return(0);
		} else {
			return (laddr);
		}
	}

	/*
	 * If the caller doesn't specify the source address but
	 * the outgoing interface, use an address associated with
	 * the interface.
	 */
	if (pi && pi->ipi6_ifindex) {
		/* XXX boundary check is assumed to be already done. */
		ia6 = in6_ifawithscope(ifindex2ifnet[pi->ipi6_ifindex],
				       dst, cred);

		if (ia6 && jailed) {
			jsin6.sin6_addr = (&ia6->ia_addr)->sin6_addr;
			if (!jailed_ip(cred->cr_prison,
				(struct sockaddr *)&jsin6))
				ia6 = 0;
		}

		if (ia6 == 0) {
			*errorp = EADDRNOTAVAIL;
			return (0);
		}
		return (&satosin6(&ia6->ia_addr)->sin6_addr);
	}

	/*
	 * If the destination address is a link-local unicast address or
	 * a multicast address, and if the outgoing interface is specified
	 * by the sin6_scope_id filed, use an address associated with the
	 * interface.
	 * XXX: We're now trying to define more specific semantics of
	 *      sin6_scope_id field, so this part will be rewritten in
	 *      the near future.
	 */
	if ((IN6_IS_ADDR_LINKLOCAL(dst) || IN6_IS_ADDR_MULTICAST(dst)) &&
	    dstsock->sin6_scope_id) {
		/*
		 * I'm not sure if boundary check for scope_id is done
		 * somewhere...
		 */
		if (dstsock->sin6_scope_id < 0 ||
		    if_index < dstsock->sin6_scope_id) {
			*errorp = ENXIO; /* XXX: better error? */
			return (0);
		}
		ia6 = in6_ifawithscope(ifindex2ifnet[dstsock->sin6_scope_id],
				       dst, cred);

		if (ia6 && jailed) {
			jsin6.sin6_addr = (&ia6->ia_addr)->sin6_addr;
			if (!jailed_ip(cred->cr_prison,
				(struct sockaddr *)&jsin6))
				ia6 = 0;
		}

		if (ia6 == 0) {
			*errorp = EADDRNOTAVAIL;
			return (0);
		}
		return (&satosin6(&ia6->ia_addr)->sin6_addr);
	}

	/*
	 * If the destination address is a multicast address and
	 * the outgoing interface for the address is specified
	 * by the caller, use an address associated with the interface.
	 * There is a sanity check here; if the destination has node-local
	 * scope, the outgoing interfacde should be a loopback address.
	 * Even if the outgoing interface is not specified, we also
	 * choose a loopback interface as the outgoing interface.
	 */
	if (!jailed && IN6_IS_ADDR_MULTICAST(dst)) {
		struct ifnet *ifp = mopts ? mopts->im6o_multicast_ifp : NULL;

		if (ifp == NULL && IN6_IS_ADDR_MC_NODELOCAL(dst)) {
			ifp = &loif[0];
		}

		if (ifp) {
			ia6 = in6_ifawithscope(ifp, dst, cred);
			if (ia6 == 0) {
				*errorp = EADDRNOTAVAIL;
				return (0);
			}
			return (&ia6->ia_addr.sin6_addr);
		}
	}

	/*
	 * If the next hop address for the packet is specified
	 * by caller, use an address associated with the route
	 * to the next hop.
	 */
	{
		struct sockaddr_in6 *sin6_next;
		struct rtentry *rt;

		if (opts && opts->ip6po_nexthop) {
			sin6_next = satosin6(opts->ip6po_nexthop);
			rt = nd6_lookup(&sin6_next->sin6_addr, 1, NULL);
			if (rt) {
				ia6 = in6_ifawithscope(rt->rt_ifp, dst, cred);
				if (ia6 == 0)
					ia6 = ifatoia6(rt->rt_ifa);
			}
			if (ia6 && jailed) {
				jsin6.sin6_addr = (&ia6->ia_addr)->sin6_addr;
				if (!jailed_ip(cred->cr_prison,
					(struct sockaddr *)&jsin6))
					ia6 = 0;
			}

			if (ia6 == 0) {
				*errorp = EADDRNOTAVAIL;
				return (0);
			}
			return (&satosin6(&ia6->ia_addr)->sin6_addr);
		}
	}

	/*
	 * If route is known or can be allocated now,
	 * our src addr is taken from the i/f, else punt.
	 */
	if (ro) {
		if (ro->ro_rt &&
		    !IN6_ARE_ADDR_EQUAL(&satosin6(&ro->ro_dst)->sin6_addr, dst)) {
			RTFREE(ro->ro_rt);
			ro->ro_rt = NULL;
		}
		if (ro->ro_rt == NULL || ro->ro_rt->rt_ifp == NULL) {
			struct sockaddr_in6 *dst6;

			/* No route yet, so try to acquire one */
			bzero(&ro->ro_dst, sizeof(struct sockaddr_in6));
			dst6 = &ro->ro_dst;
			dst6->sin6_family = AF_INET6;
			dst6->sin6_len = sizeof(struct sockaddr_in6);
			dst6->sin6_addr = *dst;
			if (!jailed && IN6_IS_ADDR_MULTICAST(dst)) {
				ro->ro_rt =
				  rtpurelookup((struct sockaddr *)&ro->ro_dst);
			} else {
				rtalloc((struct route *)ro);
			}
		}

		/*
		 * in_pcbconnect() checks out IFF_LOOPBACK to skip using
		 * the address. But we don't know why it does so.
		 * It is necessary to ensure the scope even for lo0
		 * so doesn't check out IFF_LOOPBACK.
		 */

		if (ro->ro_rt) {
			ia6 = in6_ifawithscope(ro->ro_rt->rt_ifa->ifa_ifp, dst, cred);
			if (ia6 && jailed) {
				jsin6.sin6_addr = (&ia6->ia_addr)->sin6_addr;
				if (!jailed_ip(cred->cr_prison,
					(struct sockaddr *)&jsin6))
					ia6 = 0;
			}

			if (ia6 == 0) /* xxx scope error ?*/
				ia6 = ifatoia6(ro->ro_rt->rt_ifa);

			if (ia6 && jailed) {
				jsin6.sin6_addr = (&ia6->ia_addr)->sin6_addr;
				if (!jailed_ip(cred->cr_prison,
					(struct sockaddr *)&jsin6))
					ia6 = 0;
			}
		}
		if (ia6 == 0) {
			*errorp = EHOSTUNREACH;	/* no route */
			return (0);
		}
		return (&satosin6(&ia6->ia_addr)->sin6_addr);
	}

	*errorp = EADDRNOTAVAIL;
	return (0);
}

/*
 * Default hop limit selection. The precedence is as follows:
 * 1. Hoplimit valued specified via ioctl.
 * 2. (If the outgoing interface is detected) the current
 *     hop limit of the interface specified by router advertisement.
 * 3. The system default hoplimit.
*/
int
in6_selecthlim(struct in6pcb *in6p, struct ifnet *ifp)
{
	int hlim;

	if (in6p && in6p->in6p_hops >= 0) {
		return (in6p->in6p_hops);
	} else if (ifp) {
		hlim = ND_IFINFO(ifp)->chlim;
		if (hlim < ip6_minhlim)
			hlim = ip6_minhlim;
	} else {
		hlim = ip6_defhlim;
	}
	return (hlim);

}
#endif

void
in6_pcbdisconnect(struct inpcb *inp)
{
	bzero((caddr_t)&inp->in6p_faddr, sizeof(inp->in6p_faddr));
	inp->inp_fport = 0;
	/* clear flowinfo - draft-itojun-ipv6-flowlabel-api-00 */
	inp->in6p_flowinfo &= ~IPV6_FLOWLABEL_MASK;
	in_pcbremconnhash(inp);
	if (inp->inp_socket->so_state & SS_NOFDREF)
		in6_pcbdetach(inp);
}

void
in6_pcbdetach(struct inpcb *inp)
{
	struct socket *so = inp->inp_socket;
	struct inpcbinfo *ipi = inp->inp_pcbinfo;

#ifdef IPSEC
	if (inp->in6p_sp != NULL)
		ipsec6_delete_pcbpolicy(inp);
#endif /* IPSEC */
	inp->inp_gencnt = ++ipi->ipi_gencnt;
	in_pcbremlists(inp);
	so->so_pcb = NULL;
	KKASSERT((so->so_state & SS_ASSERTINPROG) == 0);
	sofree(so);		/* remove pcb ref */

	if (inp->in6p_options)
		m_freem(inp->in6p_options);
	ip6_freepcbopts(inp->in6p_outputopts);
	ip6_freemoptions(inp->in6p_moptions);
	if (inp->in6p_route.ro_rt)
		rtfree(inp->in6p_route.ro_rt);
	/* Check and free IPv4 related resources in case of mapped addr */
	if (inp->inp_options)
		m_free(inp->inp_options);
	ip_freemoptions(inp->inp_moptions);

	kfree(inp, M_PCB);
}

/*
 * The calling convention of in6_setsockaddr() and in6_setpeeraddr() was
 * modified to match the pru_sockaddr() and pru_peeraddr() entry points
 * in struct pr_usrreqs, so that protocols can just reference then directly
 * without the need for a wrapper function.  The socket must have a valid
 * (i.e., non-nil) PCB, but it should be impossible to get an invalid one
 * except through a kernel programming error, so it is acceptable to panic
 * (or in this case trap) if the PCB is invalid.  (Actually, we don't trap
 * because there actually /is/ a programming error somewhere... XXX)
 */
void
in6_setsockaddr_dispatch(netmsg_t msg)
{
	int error;

	error = in6_setsockaddr(msg->sockaddr.base.nm_so, msg->sockaddr.nm_nam);
	lwkt_replymsg(&msg->sockaddr.base.lmsg, error);
}

int
in6_setsockaddr(struct socket *so, struct sockaddr **nam)
{
	struct inpcb *inp;
	struct sockaddr_in6 *sin6;

	/*
	 * Do the malloc first in case it blocks.
	 */
	sin6 = kmalloc(sizeof *sin6, M_SONAME, M_WAITOK | M_ZERO);
	sin6->sin6_family = AF_INET6;
	sin6->sin6_len = sizeof(*sin6);

	crit_enter();
	inp = so->so_pcb;
	if (!inp) {
		crit_exit();
		kfree(sin6, M_SONAME);
		return EINVAL;
	}
	sin6->sin6_port = inp->inp_lport;
	sin6->sin6_addr = inp->in6p_laddr;
	crit_exit();
	if (IN6_IS_SCOPE_LINKLOCAL(&sin6->sin6_addr))
		sin6->sin6_scope_id = ntohs(sin6->sin6_addr.s6_addr16[1]);
	else
		sin6->sin6_scope_id = 0;	/*XXX*/
	if (IN6_IS_SCOPE_LINKLOCAL(&sin6->sin6_addr))
		sin6->sin6_addr.s6_addr16[1] = 0;

	*nam = (struct sockaddr *)sin6;
	return 0;
}

void
in6_setpeeraddr_dispatch(netmsg_t msg)
{
	int error;

	error = in6_setpeeraddr(msg->peeraddr.base.nm_so, msg->peeraddr.nm_nam);
	lwkt_replymsg(&msg->peeraddr.base.lmsg, error);
}

int
in6_setpeeraddr(struct socket *so, struct sockaddr **nam)
{
	struct inpcb *inp;
	struct sockaddr_in6 *sin6;

	/*
	 * Do the malloc first in case it blocks.
	 */
	sin6 = kmalloc(sizeof(*sin6), M_SONAME, M_WAITOK | M_ZERO);
	sin6->sin6_family = AF_INET6;
	sin6->sin6_len = sizeof(struct sockaddr_in6);

	crit_enter();
	inp = so->so_pcb;
	if (!inp) {
		crit_exit();
		kfree(sin6, M_SONAME);
		return EINVAL;
	}
	sin6->sin6_port = inp->inp_fport;
	sin6->sin6_addr = inp->in6p_faddr;
	crit_exit();
	if (IN6_IS_SCOPE_LINKLOCAL(&sin6->sin6_addr))
		sin6->sin6_scope_id = ntohs(sin6->sin6_addr.s6_addr16[1]);
	else
		sin6->sin6_scope_id = 0;	/*XXX*/
	if (IN6_IS_SCOPE_LINKLOCAL(&sin6->sin6_addr))
		sin6->sin6_addr.s6_addr16[1] = 0;

	*nam = (struct sockaddr *)sin6;
	return 0;
}

/*
 * Pass some notification to all connections of a protocol
 * associated with address dst.  The local address and/or port numbers
 * may be specified to limit the search.  The "usual action" will be
 * taken, depending on the ctlinput cmd.  The caller must filter any
 * cmds that are uninteresting (e.g., no error in the map).
 * Call the protocol specific routine (if any) to report
 * any errors for each matching socket.
 */
void
in6_pcbnotify(struct inpcbinfo *pcbinfo, struct sockaddr *dst, in_port_t fport,
    const struct sockaddr *src, in_port_t lport, int cmd, int arg,
    inp_notify_t notify)
{
	struct inpcb *inp, *marker;
	struct sockaddr_in6 sa6_src, *sa6_dst;
	u_int32_t flowinfo;

	if ((unsigned)cmd >= PRC_NCMDS || dst->sa_family != AF_INET6)
		return;

	sa6_dst = (struct sockaddr_in6 *)dst;
	if (IN6_IS_ADDR_UNSPECIFIED(&sa6_dst->sin6_addr))
		return;

	/*
	 * note that src can be NULL when we get notify by local fragmentation.
	 */
	sa6_src = (src == NULL) ? sa6_any : *(const struct sockaddr_in6 *)src;
	flowinfo = sa6_src.sin6_flowinfo;

	/*
	 * Redirects go to all references to the destination,
	 * and use in6_rtchange to invalidate the route cache.
	 * Dead host indications: also use in6_rtchange to invalidate
	 * the cache, and deliver the error to all the sockets.
	 * Otherwise, if we have knowledge of the local port and address,
	 * deliver only to that socket.
	 */
	if (PRC_IS_REDIRECT(cmd) || cmd == PRC_HOSTDEAD) {
		fport = 0;
		lport = 0;
		bzero((caddr_t)&sa6_src.sin6_addr, sizeof(sa6_src.sin6_addr));

		if (cmd != PRC_HOSTDEAD)
			notify = in6_rtchange;
	}
	if (cmd != PRC_MSGSIZE)
		arg = inet6ctlerrmap[cmd];

	marker = in_pcbmarker(mycpuid);

	GET_PCBINFO_TOKEN(pcbinfo);

	LIST_INSERT_HEAD(&pcbinfo->pcblisthead, marker, inp_list);
	while ((inp = LIST_NEXT(marker, inp_list)) != NULL) {
		LIST_REMOVE(marker, inp_list);
		LIST_INSERT_AFTER(inp, marker, inp_list);

		if (inp->inp_flags & INP_PLACEMARKER)
			continue;

		if (!INP_ISIPV6(inp))
			continue;
		/*
		 * If the error designates a new path MTU for a destination
		 * and the application (associated with this socket) wanted to
		 * know the value, notify. Note that we notify for all
		 * disconnected sockets if the corresponding application
		 * wanted. This is because some UDP applications keep sending
		 * sockets disconnected.
		 * XXX: should we avoid to notify the value to TCP sockets?
		 */
		if (cmd == PRC_MSGSIZE && (inp->inp_flags & IN6P_MTU) != 0 &&
		    (IN6_IS_ADDR_UNSPECIFIED(&inp->in6p_faddr) ||
		     IN6_ARE_ADDR_EQUAL(&inp->in6p_faddr, &sa6_dst->sin6_addr))) {
			ip6_notify_pmtu(inp, (struct sockaddr_in6 *)dst, &arg);
		}

		/*
		 * Detect if we should notify the error. If no source and
		 * destination ports are specifed, but non-zero flowinfo and
		 * local address match, notify the error. This is the case
		 * when the error is delivered with an encrypted buffer
		 * by ESP. Otherwise, just compare addresses and ports
		 * as usual.
		 */
		if (lport == 0 && fport == 0 && flowinfo &&
		    inp->inp_socket != NULL &&
		    flowinfo == (inp->in6p_flowinfo & IPV6_FLOWLABEL_MASK) &&
		    IN6_ARE_ADDR_EQUAL(&inp->in6p_laddr, &sa6_src.sin6_addr))
			goto do_notify;
		else if (!IN6_ARE_ADDR_EQUAL(&inp->in6p_faddr,
					     &sa6_dst->sin6_addr) ||
			 inp->inp_socket == 0 ||
			 (lport && inp->inp_lport != lport) ||
			 (!IN6_IS_ADDR_UNSPECIFIED(&sa6_src.sin6_addr) &&
			  !IN6_ARE_ADDR_EQUAL(&inp->in6p_laddr,
					      &sa6_src.sin6_addr)) ||
			 (fport && inp->inp_fport != fport))
			continue;

do_notify:
		if (notify)
			(*notify)(inp, arg);
	}
	LIST_REMOVE(marker, inp_list);

	REL_PCBINFO_TOKEN(pcbinfo);
}

/*
 * Lookup a PCB based on the local address and port.
 */
struct inpcb *
in6_pcblookup_local(struct inpcbportinfo *portinfo,
    const struct in6_addr *laddr, u_int lport_arg, int wild_okay,
    struct ucred *cred)
{
	struct inpcb *inp;
	int matchwild = 3, wildcard;
	u_short lport = lport_arg;
	struct inpcbporthead *porthash;
	struct inpcbport *phd;
	struct inpcb *match = NULL;

	/*
	 * If the porthashbase is shared across several cpus, it must
	 * have been locked.
	 */
	ASSERT_PORT_TOKEN_HELD(portinfo);

	/*
	 * Best fit PCB lookup.
	 *
	 * First see if this local port is in use by looking on the
	 * port hash list.
	 */
	porthash = &portinfo->porthashbase[
				INP_PCBPORTHASH(lport, portinfo->porthashmask)];
	LIST_FOREACH(phd, porthash, phd_hash) {
		if (phd->phd_port == lport)
			break;
	}

	if (phd != NULL) {
		/*
		 * Port is in use by one or more PCBs. Look for best
		 * fit.
		 */
		LIST_FOREACH(inp, &phd->phd_pcblist, inp_portlist) {
			wildcard = 0;
			if (!INP_ISIPV6(inp))
				continue;
			if (!IN6_IS_ADDR_UNSPECIFIED(&inp->in6p_faddr))
				wildcard++;
			if (!IN6_IS_ADDR_UNSPECIFIED(&inp->in6p_laddr)) {
				if (IN6_IS_ADDR_UNSPECIFIED(laddr))
					wildcard++;
				else if (!IN6_ARE_ADDR_EQUAL(
					&inp->in6p_laddr, laddr))
					continue;
			} else {
				if (!IN6_IS_ADDR_UNSPECIFIED(laddr))
					wildcard++;
			}
			if (wildcard && !wild_okay)
				continue;
			if (wildcard < matchwild &&
			    (cred == NULL ||
			     cred->cr_prison == 
					inp->inp_socket->so_cred->cr_prison)) {
				match = inp;
				matchwild = wildcard;
				if (wildcard == 0)
					break;
				else
					matchwild = wildcard;
			}
		}
	}
	return (match);
}

void
in6_pcbpurgeif0(struct inpcbinfo *pcbinfo, struct ifnet *ifp)
{
	struct in6pcb *in6p, *marker;
	struct ip6_moptions *im6o;
	struct in6_multi_mship *imm, *nimm;

	/*
	 * We only need to make sure that we are in netisr0, where all
	 * multicast operation happen.  We could check inpcbinfo which
	 * does not belong to netisr0 by holding the inpcbinfo's token.
	 * In this case, the pcbinfo must be able to be shared, i.e.
	 * pcbinfo->infotoken is not NULL.
	 */
	KASSERT(&curthread->td_msgport == netisr_cpuport(0),
	    ("not in netisr0"));
	KASSERT(pcbinfo->cpu == 0 || pcbinfo->infotoken != NULL,
	    ("pcbinfo could not be shared"));

	/*
	 * Get a marker for the current netisr (netisr0).
	 *
	 * It is possible that the multicast address deletion blocks,
	 * which could cause temporary token releasing.  So we use
	 * inpcb marker here to get a coherent view of the inpcb list.
	 *
	 * While, on the other hand, moptions are only added and deleted
	 * in netisr0, so we would not see staled moption or miss moption
	 * even if the token was released due to the blocking multicast
	 * address deletion.
	 */
	marker = in_pcbmarker(mycpuid);

	GET_PCBINFO_TOKEN(pcbinfo);

	LIST_INSERT_HEAD(&pcbinfo->pcblisthead, marker, inp_list);
	while ((in6p = LIST_NEXT(marker, inp_list)) != NULL) {
		LIST_REMOVE(marker, inp_list);
		LIST_INSERT_AFTER(in6p, marker, inp_list);

		if (in6p->in6p_flags & INP_PLACEMARKER)
			continue;
		im6o = in6p->in6p_moptions;
		if (INP_ISIPV6(in6p) && im6o) {
			/*
			 * Unselect the outgoing interface if it is being
			 * detached.
			 */
			if (im6o->im6o_multicast_ifp == ifp)
				im6o->im6o_multicast_ifp = NULL;

			/*
			 * Drop multicast group membership if we joined
			 * through the interface being detached.
			 * XXX controversial - is it really legal for kernel
			 * to force this?
			 */
			for (imm = im6o->im6o_memberships.lh_first;
			     imm != NULL; imm = nimm) {
				nimm = imm->i6mm_chain.le_next;
				if (imm->i6mm_maddr->in6m_ifp == ifp) {
					LIST_REMOVE(imm, i6mm_chain);
					in6_delmulti(imm->i6mm_maddr);
					kfree(imm, M_IPMADDR);
				}
			}
		}
	}
	LIST_REMOVE(marker, inp_list);

	REL_PCBINFO_TOKEN(pcbinfo);
}

/*
 * Check for alternatives when higher level complains
 * about service problems.  For now, invalidate cached
 * routing information.  If the route was created dynamically
 * (by a redirect), time to try a default gateway again.
 */
void
in6_losing(struct inpcb *in6p)
{
	struct rtentry *rt;
	struct rt_addrinfo info;

	if ((rt = in6p->in6p_route.ro_rt) != NULL) {
		bzero((caddr_t)&info, sizeof(info));
		info.rti_flags = rt->rt_flags;
		info.rti_info[RTAX_DST] = rt_key(rt);
		info.rti_info[RTAX_GATEWAY] = rt->rt_gateway;
		info.rti_info[RTAX_NETMASK] = rt_mask(rt);
		rt_missmsg(RTM_LOSING, &info, rt->rt_flags, 0);
		if (rt->rt_flags & RTF_DYNAMIC) {
			rtrequest(RTM_DELETE, rt_key(rt), rt->rt_gateway,
			    rt_mask(rt), rt->rt_flags, NULL);
		}
		in6p->in6p_route.ro_rt = NULL;
		rtfree(rt);
		/*
		 * A new route can be allocated
		 * the next time output is attempted.
		 */
	}
}

/*
 * After a routing change, flush old routing
 * and allocate a (hopefully) better one.
 */
void
in6_rtchange(struct inpcb *inp, int error)
{
	if (inp->in6p_route.ro_rt) {
		rtfree(inp->in6p_route.ro_rt);
		inp->in6p_route.ro_rt = 0;
		/*
		 * A new route can be allocated the next time
		 * output is attempted.
		 */
	}
}

/*
 * Lookup PCB in hash list.
 */
struct inpcb *
in6_pcblookup_hash(struct inpcbinfo *pcbinfo, struct in6_addr *faddr,
		   u_int fport_arg, struct in6_addr *laddr, u_int lport_arg,
		   int wildcard, struct ifnet *ifp)
{
	struct inpcbhead *head;
	struct inpcb *inp;
	struct inpcb *jinp = NULL;
	u_short fport = fport_arg, lport = lport_arg;
	int faith;

	if (faithprefix_p != NULL)
		faith = (*faithprefix_p)(laddr);
	else
		faith = 0;

	/*
	 * First look for an exact match.
	 */
	head = &pcbinfo->hashbase[INP_PCBCONNHASH(faddr->s6_addr32[3] /* XXX */,
					      fport,
					      laddr->s6_addr32[3], /* XXX JH */
					      lport,
					      pcbinfo->hashmask)];
	LIST_FOREACH(inp, head, inp_hash) {
		if (!INP_ISIPV6(inp))
			continue;
		if (IN6_ARE_ADDR_EQUAL(&inp->in6p_faddr, faddr) &&
		    IN6_ARE_ADDR_EQUAL(&inp->in6p_laddr, laddr) &&
		    inp->inp_fport == fport &&
		    inp->inp_lport == lport) {
			/*
			 * Found.
			 */
			if (inp->inp_socket == NULL ||
			inp->inp_socket->so_cred->cr_prison == NULL) {
				return (inp);
			} else {
				if  (jinp == NULL)
					jinp = inp;
			}
		}
	}
	if (jinp != NULL)
		return(jinp);

	if (wildcard) {
		struct inpcontainerhead *chead;
		struct inpcontainer *ic;
		struct inpcb *local_wild = NULL;
		struct inpcb *jinp_wild = NULL;
		struct sockaddr_in6 jsin6;
		struct ucred *cred;

		/*
		 * Order of socket selection:
		 * 1. non-jailed, non-wild.
		 * 2. non-jailed, wild.
		 * 3. jailed, non-wild.
		 * 4. jailed, wild.
		 */
		jsin6.sin6_family = AF_INET6;
		chead = &pcbinfo->wildcardhashbase[INP_PCBWILDCARDHASH(lport,
		    pcbinfo->wildcardhashmask)];

		GET_PCBINFO_TOKEN(pcbinfo);
		LIST_FOREACH(ic, chead, ic_list) {
			inp = ic->ic_inp;
			if (inp->inp_flags & INP_PLACEMARKER)
				continue;

			if (!INP_ISIPV6(inp))
				continue;
			if (inp->inp_socket != NULL)
				cred = inp->inp_socket->so_cred;
			else
				cred = NULL;

			if (cred != NULL && jailed(cred)) {
				if (jinp != NULL) {
					continue;
				} else {
		                        jsin6.sin6_addr = *laddr;
					if (!jailed_ip(cred->cr_prison,
					    (struct sockaddr *)&jsin6))
						continue;
				}
			}
			if (IN6_IS_ADDR_UNSPECIFIED(&inp->in6p_faddr) &&
			    inp->inp_lport == lport) {
				if (faith && (inp->inp_flags & INP_FAITH) == 0)
					continue;
				if (IN6_ARE_ADDR_EQUAL(&inp->in6p_laddr,
						       laddr)) {
					if (cred != NULL && jailed(cred)) {
						jinp = inp;
					} else {
						REL_PCBINFO_TOKEN(pcbinfo);
						return (inp);
					}
				} else if (IN6_IS_ADDR_UNSPECIFIED(&inp->in6p_laddr)) {
					if (cred != NULL && jailed(cred))
						jinp_wild = inp;
					else
						local_wild = inp;
				}
			}
		}
		REL_PCBINFO_TOKEN(pcbinfo);

		if (local_wild != NULL)
			return (local_wild);
		if (jinp != NULL)
			return (jinp);
		return (jinp_wild);
	}

	/*
	 * Not found.
	 */
	return (NULL);
}

void
init_sin6(struct sockaddr_in6 *sin6, struct mbuf *m)
{
	struct ip6_hdr *ip;

	ip = mtod(m, struct ip6_hdr *);
	bzero(sin6, sizeof(*sin6));
	sin6->sin6_len = sizeof(*sin6);
	sin6->sin6_family = AF_INET6;
	sin6->sin6_addr = ip->ip6_src;
	if (IN6_IS_SCOPE_LINKLOCAL(&sin6->sin6_addr))
		sin6->sin6_addr.s6_addr16[1] = 0;
	sin6->sin6_scope_id =
		(m->m_pkthdr.rcvif && IN6_IS_SCOPE_LINKLOCAL(&sin6->sin6_addr))
		? m->m_pkthdr.rcvif->if_index : 0;

	return;
}

void
in6_savefaddr(struct socket *so, const struct sockaddr *faddr)
{
	struct sockaddr_in6 *sin6;

	KASSERT(faddr->sa_family == AF_INET6,
	    ("not AF_INET6 faddr %d", faddr->sa_family));

	sin6 = kmalloc(sizeof(*sin6), M_SONAME, M_WAITOK | M_ZERO);
	sin6->sin6_family = AF_INET6;
	sin6->sin6_len = sizeof(*sin6);

	sin6->sin6_port = ((const struct sockaddr_in6 *)faddr)->sin6_port;
	sin6->sin6_addr = ((const struct sockaddr_in6 *)faddr)->sin6_addr;

	if (IN6_IS_SCOPE_LINKLOCAL(&sin6->sin6_addr))
		sin6->sin6_scope_id = ntohs(sin6->sin6_addr.s6_addr16[1]);
	else
		sin6->sin6_scope_id = 0;	/*XXX*/
	if (IN6_IS_SCOPE_LINKLOCAL(&sin6->sin6_addr))
		sin6->sin6_addr.s6_addr16[1] = 0;

	so->so_faddr = (struct sockaddr *)sin6;
}
