/*	$FreeBSD: src/sys/netinet6/in6_src.c,v 1.1.2.3 2002/02/26 18:02:06 ume Exp $	*/
/*	$KAME: in6_src.c,v 1.37 2001/03/29 05:34:31 itojun Exp $	*/

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

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/jail.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/protosw.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/sockio.h>
#include <sys/sysctl.h>
#include <sys/errno.h>
#include <sys/time.h>
#include <sys/proc.h>
#include <sys/priv.h>

#include <net/if.h>
#include <net/route.h>

#include <netinet/in.h>
#include <netinet/in_var.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#include <netinet/in_pcb.h>
#include <netinet6/in6_var.h>
#include <netinet/ip6.h>
#include <netinet6/in6_pcb.h>
#include <netinet6/ip6_var.h>
#include <netinet6/nd6.h>
#ifdef ENABLE_DEFAULT_SCOPE
#include <netinet6/scope6_var.h>
#endif

#include <net/net_osdep.h>

#define ADDR_LABEL_NOTAPP (-1)
struct in6_addrpolicy defaultaddrpolicy;

static void	init_policy_queue(void);
static int	add_addrsel_policyent(struct in6_addrpolicy *);
static int	delete_addrsel_policyent(struct in6_addrpolicy *);
static int	walk_addrsel_policy(int (*)(struct in6_addrpolicy *, void *),
				    void *);
static int	dump_addrsel_policyent(struct in6_addrpolicy *, void *);


/*
 * Return an IPv6 address, which is the most appropriate for a given
 * destination and user specified options.
 * If necessary, this function lookups the routing table and returns
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
	struct in6_ifaddr *ia6 = NULL;
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
				ia6 = NULL;
		}

		if (ia6 == NULL) {
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
				ia6 = NULL;
		}

		if (ia6 == NULL) {
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
			if (ia6 == NULL) {
				*errorp = EADDRNOTAVAIL;
				return (0);
			}
			return (&satosin6(&ia6->ia_addr)->sin6_addr);
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
				if (ia6 == NULL)
					ia6 = ifatoia6(rt->rt_ifa);
			}
			if (ia6 && jailed) {
				jsin6.sin6_addr = (&ia6->ia_addr)->sin6_addr;
				if (!jailed_ip(cred->cr_prison,
					(struct sockaddr *)&jsin6))
					ia6 = NULL;
			}

			if (ia6 == NULL) {
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
		    (!(ro->ro_rt->rt_flags & RTF_UP) ||
		     satosin6(&ro->ro_dst)->sin6_family != AF_INET6 ||
		     !IN6_ARE_ADDR_EQUAL(&satosin6(&ro->ro_dst)->sin6_addr,
					 dst))) {
			RTFREE(ro->ro_rt);
			ro->ro_rt = NULL;
		}
		if (ro->ro_rt == NULL || ro->ro_rt->rt_ifp == NULL) {
			struct sockaddr_in6 *sa6;

			/* No route yet, so try to acquire one */
			bzero(&ro->ro_dst, sizeof(struct sockaddr_in6));
			sa6 = &ro->ro_dst;
			sa6->sin6_family = AF_INET6;
			sa6->sin6_len = sizeof(struct sockaddr_in6);
			sa6->sin6_addr = *dst;
			sa6->sin6_scope_id = dstsock->sin6_scope_id;
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
					ia6 = NULL;
			}

			if (ia6 == NULL) /* xxx scope error ?*/
				ia6 = ifatoia6(ro->ro_rt->rt_ifa);

			if (ia6 && jailed) {
				jsin6.sin6_addr = (&ia6->ia_addr)->sin6_addr;
				if (!jailed_ip(cred->cr_prison,
					(struct sockaddr *)&jsin6))
					ia6 = NULL;
			}
		}
#if 0
		/*
		 * xxx The followings are necessary? (kazu)
		 * I don't think so.
		 * It's for SO_DONTROUTE option in IPv4.(jinmei)
		 */
		if (ia6 == 0) {
			struct sockaddr_in6 sin6 = {sizeof(sin6), AF_INET6, 0};

			sin6->sin6_addr = *dst;

			ia6 = ifatoia6(ifa_ifwithdstaddr(sin6tosa(&sin6)));
			if (ia6 == 0)
				ia6 = ifatoia6(ifa_ifwithnet(sin6tosa(&sin6)));
			if (ia6 == 0)
				return (0);
			return (&satosin6(&ia6->ia_addr)->sin6_addr);
		}
#endif /* 0 */
		if (ia6 == NULL) {
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
 * 1. Hoplimit value specified via ioctl.
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

/*
 * XXX: this is borrowed from in6_pcbbind(). If possible, we should
 * share this function by all *bsd*...
 */
int
in6_pcbsetlport(struct in6_addr *laddr, struct inpcb *inp, struct thread *td)
{
	struct socket *so = inp->inp_socket;
	u_int16_t lport = 0, first, last, *lastport, step;
	int count, error = 0, wild = 0;
	struct inpcbinfo *pcbinfo = inp->inp_pcbinfo;
	struct inpcbportinfo *portinfo;
	struct ucred *cred = NULL;
	int portinfo_first, portinfo_idx;

	/* XXX: this is redundant when called from in6_pcbbind */
	if ((so->so_options & (SO_REUSEADDR|SO_REUSEPORT)) == 0)
		wild = INPLOOKUP_WILDCARD;
	if (td->td_proc && td->td_proc->p_ucred)
		cred = td->td_proc->p_ucred;

	inp->inp_flags |= INP_ANONPORT;

	step = pcbinfo->portinfo_mask + 1;
	portinfo_first = mycpuid & pcbinfo->portinfo_mask;
	portinfo_idx = portinfo_first;
loop:
	portinfo = &pcbinfo->portinfo[portinfo_idx];

	if (inp->inp_flags & INP_HIGHPORT) {
		first = ipport_hifirstauto;	/* sysctl */
		last  = ipport_hilastauto;
		lastport = &portinfo->lasthi;
	} else if (inp->inp_flags & INP_LOWPORT) {
		if ((error = priv_check(td, PRIV_ROOT)) != 0)
			return error;
		first = ipport_lowfirstauto;	/* 1023 */
		last  = ipport_lowlastauto;	/* 600 */
		lastport = &portinfo->lastlow;
	} else {
		first = ipport_firstauto;	/* sysctl */
		last  = ipport_lastauto;
		lastport = &portinfo->lastport;
	}

	/*
	 * This has to be atomic.  If the porthash is shared across multiple
	 * protocol threads (aka tcp) then the token must be held.
	 */
	GET_PORT_TOKEN(portinfo);

	/*
	 * Simple check to ensure all ports are not used up causing
	 * a deadlock here.
	 *
	 * We split the two cases (up and down) so that the direction
	 * is not being tested on each round of the loop.
	 */
	if (first > last) {
		/*
		 * counting down
		 */
		in_pcbportrange(&first, &last, portinfo->offset, step);
		count = (first - last) / step;

		do {
			if (count-- < 0) {	/* completely used? */
				error = EAGAIN;
				goto done;
			}
			*lastport -= step;
			if (*lastport > first || *lastport < last)
				*lastport = first;
			KKASSERT((*lastport & pcbinfo->portinfo_mask) ==
			    portinfo->offset);
			lport = htons(*lastport);
		} while (in6_pcblookup_local(portinfo, &inp->in6p_laddr,
			 lport, wild, cred));
	} else {
		/*
		 * counting up
		 */
		in_pcbportrange(&last, &first, portinfo->offset, step);
		count = (last - first) / step;

		do {
			if (count-- < 0) {	/* completely used? */
				error = EAGAIN;
				goto done;
			}
			*lastport += step;
			if (*lastport < first || *lastport > last)
				*lastport = first;
			KKASSERT((*lastport & pcbinfo->portinfo_mask) ==
			    portinfo->offset);
			lport = htons(*lastport);
		} while (in6_pcblookup_local(portinfo, &inp->in6p_laddr,
			 lport, wild, cred));
	}

	inp->inp_lport = lport;
	in_pcbinsporthash(portinfo, inp);
	error = 0;
done:
	REL_PORT_TOKEN(portinfo);

	if (error) {
		/* Try next portinfo */
		portinfo_idx++;
		portinfo_idx &= pcbinfo->portinfo_mask;
		if (portinfo_idx != portinfo_first)
			goto loop;

		/* Undo any address bind that may have occurred above. */
		inp->in6p_laddr = kin6addr_any;
	}
	return error;
}

/*
 * generate kernel-internal form (scopeid embedded into s6_addr16[1]).
 * If the address scope of is link-local, embed the interface index in the
 * address.  The routine determines our precedence
 * between advanced API scope/interface specification and basic API
 * specification.
 *
 * this function should be nuked in the future, when we get rid of
 * embedded scopeid thing.
 *
 * XXX actually, it is over-specification to return ifp against sin6_scope_id.
 * there can be multiple interfaces that belong to a particular scope zone
 * (in specification, we have 1:N mapping between a scope zone and interfaces).
 * we may want to change the function to return something other than ifp.
 */
int
in6_embedscope(struct in6_addr *in6,
	       const struct sockaddr_in6 *sin6,
#ifdef HAVE_NRL_INPCB
	       struct inpcb *in6p,
#define in6p_outputopts inp_outputopts6
#define in6p_moptions   inp_moptions6
#else
	       struct in6pcb *in6p,
#endif
	       struct ifnet **ifpp)
{
	struct ifnet *ifp = NULL;
	u_int32_t scopeid;

	*in6 = sin6->sin6_addr;
	scopeid = sin6->sin6_scope_id;
	if (ifpp)
		*ifpp = NULL;

	/*
	 * don't try to read sin6->sin6_addr beyond here, since the caller may
	 * ask us to overwrite existing sockaddr_in6
	 */

#ifdef ENABLE_DEFAULT_SCOPE
	if (scopeid == 0)
		scopeid = scope6_addr2default(in6);
#endif

	if (IN6_IS_SCOPE_LINKLOCAL(in6)) {
		struct in6_pktinfo *pi;

		/*
		 * KAME assumption: link id == interface id
		 */

		if (in6p && in6p->in6p_outputopts &&
		    (pi = in6p->in6p_outputopts->ip6po_pktinfo) &&
		    pi->ipi6_ifindex) {
			ifp = ifindex2ifnet[pi->ipi6_ifindex];
			in6->s6_addr16[1] = htons(pi->ipi6_ifindex);
		} else if (in6p && IN6_IS_ADDR_MULTICAST(in6) &&
			   in6p->in6p_moptions &&
			   in6p->in6p_moptions->im6o_multicast_ifp) {
			ifp = in6p->in6p_moptions->im6o_multicast_ifp;
			in6->s6_addr16[1] = htons(ifp->if_index);
		} else if (scopeid) {
			/* boundary check */
			if (scopeid < 0 || if_index < scopeid)
				return ENXIO;  /* XXX EINVAL? */
			ifp = ifindex2ifnet[scopeid];
			/*XXX assignment to 16bit from 32bit variable */
			in6->s6_addr16[1] = htons(scopeid & 0xffff);
		}

		if (ifpp)
			*ifpp = ifp;
	}

	return 0;
}
#ifdef HAVE_NRL_INPCB
#undef in6p_outputopts
#undef in6p_moptions
#endif

/*
 * generate standard sockaddr_in6 from embedded form.
 * touches sin6_addr and sin6_scope_id only.
 *
 * this function should be nuked in the future, when we get rid of
 * embedded scopeid thing.
 */
int
in6_recoverscope(struct sockaddr_in6 *sin6, const struct in6_addr *in6,
		 struct ifnet *ifp)
{
	u_int32_t scopeid;

	sin6->sin6_addr = *in6;

	/*
	 * don't try to read *in6 beyond here, since the caller may
	 * ask us to overwrite existing sockaddr_in6
	 */

	sin6->sin6_scope_id = 0;
	if (IN6_IS_SCOPE_LINKLOCAL(in6)) {
		/*
		 * KAME assumption: link id == interface id
		 */
		scopeid = ntohs(sin6->sin6_addr.s6_addr16[1]);
		if (scopeid) {
			/* sanity check */
			if (scopeid < 0 || if_index < scopeid)
				return ENXIO;
			if (ifp && ifp->if_index != scopeid)
				return ENXIO;
			sin6->sin6_addr.s6_addr16[1] = 0;
			sin6->sin6_scope_id = scopeid;
		}
	}

	return 0;
}

/*
 * just clear the embedded scope identifer.
 * XXX: currently used for bsdi4 only as a supplement function.
 */
void
in6_clearscope(struct in6_addr *addr)
{
	if (IN6_IS_SCOPE_LINKLOCAL(addr))
		addr->s6_addr16[1] = 0;
}

void
addrsel_policy_init(void)
{

	init_policy_queue();

	/* initialize the "last resort" policy */
	bzero(&defaultaddrpolicy, sizeof(defaultaddrpolicy));
	defaultaddrpolicy.label = ADDR_LABEL_NOTAPP;
}

/*
 * Subroutines to manage the address selection policy table via sysctl.
 */
struct walkarg {
	struct sysctl_req *w_req;
};

static int in6_src_sysctl(SYSCTL_HANDLER_ARGS);
SYSCTL_DECL(_net_inet6_ip6);
SYSCTL_NODE(_net_inet6_ip6, IPV6CTL_ADDRCTLPOLICY, addrctlpolicy,
	CTLFLAG_RD, in6_src_sysctl, "Address selection policy");

static int
in6_src_sysctl(SYSCTL_HANDLER_ARGS)
{
	struct walkarg w;

	if (req->newptr)
		return EPERM;

	bzero(&w, sizeof(w));
	w.w_req = req;

	return (walk_addrsel_policy(dump_addrsel_policyent, &w));
}

int
in6_src_ioctl(u_long cmd, caddr_t data)
{
	int i;
	struct in6_addrpolicy ent0;

	if (cmd != SIOCAADDRCTL_POLICY && cmd != SIOCDADDRCTL_POLICY)
		return (EOPNOTSUPP); /* check for safety */

	ent0 = *(struct in6_addrpolicy *)data;

	if (ent0.label == ADDR_LABEL_NOTAPP)
		return (EINVAL);
	/* check if the prefix mask is consecutive. */
	if (in6_mask2len(&ent0.addrmask.sin6_addr, NULL) < 0)
		return (EINVAL);
	/* clear trailing garbages (if any) of the prefix address. */
	for (i = 0; i < 4; i++) {
		ent0.addr.sin6_addr.s6_addr32[i] &=
			ent0.addrmask.sin6_addr.s6_addr32[i];
	}
	ent0.use = 0;

	switch (cmd) {
	case SIOCAADDRCTL_POLICY:
		return (add_addrsel_policyent(&ent0));
	case SIOCDADDRCTL_POLICY:
		return (delete_addrsel_policyent(&ent0));
	}

	return (0);		/* XXX: compromise compilers */
}

/*
 * The followings are implementation of the policy table using a
 * simple tail queue.
 * XXX such details should be hidden.
 * XXX implementation using binary tree should be more efficient.
 */
struct addrsel_policyent {
	TAILQ_ENTRY(addrsel_policyent) ape_entry;
	struct in6_addrpolicy ape_policy;
};

TAILQ_HEAD(addrsel_policyhead, addrsel_policyent);

struct addrsel_policyhead addrsel_policytab;

static void
init_policy_queue(void)
{
	TAILQ_INIT(&addrsel_policytab);
}

static int
add_addrsel_policyent(struct in6_addrpolicy *newpolicy)
{
	struct addrsel_policyent *new, *pol;

	/* duplication check */
	for (pol = TAILQ_FIRST(&addrsel_policytab); pol;
	     pol = TAILQ_NEXT(pol, ape_entry)) {
		if (SA6_ARE_ADDR_EQUAL(&newpolicy->addr,
				       &pol->ape_policy.addr) &&
		    SA6_ARE_ADDR_EQUAL(&newpolicy->addrmask,
				       &pol->ape_policy.addrmask)) {
			return (EEXIST);	/* or override it? */
		}
	}

	new = kmalloc(sizeof(*new), M_IFADDR, M_WAITOK | M_ZERO);

	/* XXX: should validate entry */
	new->ape_policy = *newpolicy;

	TAILQ_INSERT_TAIL(&addrsel_policytab, new, ape_entry);

	return (0);
}

static int
delete_addrsel_policyent(struct in6_addrpolicy *key)
{
	struct addrsel_policyent *pol;

	/* search for the entry in the table */
	for (pol = TAILQ_FIRST(&addrsel_policytab); pol;
	     pol = TAILQ_NEXT(pol, ape_entry)) {
		if (SA6_ARE_ADDR_EQUAL(&key->addr, &pol->ape_policy.addr) &&
		    SA6_ARE_ADDR_EQUAL(&key->addrmask,
				       &pol->ape_policy.addrmask)) {
			break;
		}
	}
	if (pol == NULL)
		return (ESRCH);

	TAILQ_REMOVE(&addrsel_policytab, pol, ape_entry);
	kfree(pol, M_IFADDR);

	return (0);
}

static int
walk_addrsel_policy(int(*callback)(struct in6_addrpolicy *, void *), void *w)
{
	struct addrsel_policyent *pol;
	int error = 0;

	for (pol = TAILQ_FIRST(&addrsel_policytab); pol;
	     pol = TAILQ_NEXT(pol, ape_entry)) {
		if ((error = (*callback)(&pol->ape_policy, w)) != 0)
			return (error);
	}

	return (error);
}

static int
dump_addrsel_policyent(struct in6_addrpolicy *pol, void *arg)
{
	int error = 0;
	struct walkarg *w = arg;

	error = SYSCTL_OUT(w->w_req, pol, sizeof(*pol));

	return (error);
}
