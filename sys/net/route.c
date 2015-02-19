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
 * Copyright (c) 1980, 1986, 1991, 1993
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
 *	@(#)route.c	8.3 (Berkeley) 1/9/95
 * $FreeBSD: src/sys/net/route.c,v 1.59.2.10 2003/01/17 08:04:00 ru Exp $
 */

#include "opt_inet.h"
#include "opt_mpls.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/socket.h>
#include <sys/domain.h>
#include <sys/kernel.h>
#include <sys/sysctl.h>
#include <sys/globaldata.h>
#include <sys/thread.h>

#include <net/if.h>
#include <net/route.h>
#include <net/netisr.h>

#include <netinet/in.h>
#include <net/ip_mroute/ip_mroute.h>

#include <sys/thread2.h>
#include <sys/msgport2.h>
#include <net/netmsg2.h>
#include <net/netisr2.h>

#ifdef MPLS
#include <netproto/mpls/mpls.h>
#endif

static struct rtstatistics rtstatistics_percpu[MAXCPU];
#define rtstat	rtstatistics_percpu[mycpuid]

struct radix_node_head *rt_tables[MAXCPU][AF_MAX+1];

static void	rt_maskedcopy (struct sockaddr *, struct sockaddr *,
			       struct sockaddr *);
static void rtable_init(void);
static void rtinit_rtrequest_callback(int, int, struct rt_addrinfo *,
				      struct rtentry *, void *);

static void rtredirect_msghandler(netmsg_t msg);
static void rtrequest1_msghandler(netmsg_t msg);
static void rtsearch_msghandler(netmsg_t msg);
static void rtmask_add_msghandler(netmsg_t msg);

static int rt_setshims(struct rtentry *, struct sockaddr **);

SYSCTL_NODE(_net, OID_AUTO, route, CTLFLAG_RW, 0, "Routing");

#ifdef ROUTE_DEBUG
static int route_debug = 1;
SYSCTL_INT(_net_route, OID_AUTO, route_debug, CTLFLAG_RW,
           &route_debug, 0, "");
#endif

int route_assert_owner_access = 1;
SYSCTL_INT(_net_route, OID_AUTO, assert_owner_access, CTLFLAG_RW,
           &route_assert_owner_access, 0, "");

u_long route_kmalloc_limit = 0;
TUNABLE_ULONG("net.route.kmalloc_limit", &route_kmalloc_limit);

/*
 * Initialize the route table(s) for protocol domains and
 * create a helper thread which will be responsible for updating
 * route table entries on each cpu.
 */
void
route_init(void)
{
	int cpu;

	for (cpu = 0; cpu < ncpus; ++cpu)
		bzero(&rtstatistics_percpu[cpu], sizeof(struct rtstatistics));
	rn_init();      /* initialize all zeroes, all ones, mask table */
	rtable_init();	/* call dom_rtattach() on each cpu */

	if (route_kmalloc_limit)
		kmalloc_raise_limit(M_RTABLE, route_kmalloc_limit);
}

static void
rtable_init_oncpu(netmsg_t msg)
{
	struct domain *dom;
	int cpu = mycpuid;

	SLIST_FOREACH(dom, &domains, dom_next) {
		if (dom->dom_rtattach) {
			dom->dom_rtattach(
				(void **)&rt_tables[cpu][dom->dom_family],
			        dom->dom_rtoffset);
		}
	}
	ifnet_forwardmsg(&msg->lmsg, cpu + 1);
}

static void
rtable_init(void)
{
	struct netmsg_base msg;

	netmsg_init(&msg, NULL, &curthread->td_msgport, 0, rtable_init_oncpu);
	ifnet_domsg(&msg.lmsg, 0);
}

/*
 * Routing statistics.
 */
static int
sysctl_rtstatistics(SYSCTL_HANDLER_ARGS)
{
	int cpu, error = 0;

	for (cpu = 0; cpu < ncpus; ++cpu) {
		if ((error = SYSCTL_OUT(req, &rtstatistics_percpu[cpu],
					sizeof(struct rtstatistics))))
				break;
		if ((error = SYSCTL_IN(req, &rtstatistics_percpu[cpu],
					sizeof(struct rtstatistics))))
				break;
	}

	return (error);
}
SYSCTL_PROC(_net_route, OID_AUTO, stats, (CTLTYPE_OPAQUE|CTLFLAG_RW),
	0, 0, sysctl_rtstatistics, "S,rtstatistics", "Routing statistics");

/*
 * Packet routing routines.
 */

/*
 * Look up and fill in the "ro_rt" rtentry field in a route structure given
 * an address in the "ro_dst" field.  Always send a report on a miss and
 * always clone routes.
 */
void
rtalloc(struct route *ro)
{
	rtalloc_ign(ro, 0UL);
}

/*
 * Look up and fill in the "ro_rt" rtentry field in a route structure given
 * an address in the "ro_dst" field.  Always send a report on a miss and
 * optionally clone routes when RTF_CLONING or RTF_PRCLONING are not being
 * ignored.
 */
void
rtalloc_ign(struct route *ro, u_long ignoreflags)
{
	if (ro->ro_rt != NULL) {
		if (ro->ro_rt->rt_ifp != NULL && ro->ro_rt->rt_flags & RTF_UP)
			return;
		rtfree(ro->ro_rt);
		ro->ro_rt = NULL;
	}
	ro->ro_rt = _rtlookup(&ro->ro_dst, RTL_REPORTMSG, ignoreflags);
}

/*
 * Look up the route that matches the given "dst" address.
 *
 * Route lookup can have the side-effect of creating and returning
 * a cloned route instead when "dst" matches a cloning route and the
 * RTF_CLONING and RTF_PRCLONING flags are not being ignored.
 *
 * Any route returned has its reference count incremented.
 */
struct rtentry *
_rtlookup(struct sockaddr *dst, boolean_t generate_report, u_long ignore)
{
	struct radix_node_head *rnh = rt_tables[mycpuid][dst->sa_family];
	struct rtentry *rt;

	if (rnh == NULL)
		goto unreach;

	/*
	 * Look up route in the radix tree.
	 */
	rt = (struct rtentry *) rnh->rnh_matchaddr((char *)dst, rnh);
	if (rt == NULL)
		goto unreach;

	/*
	 * Handle cloning routes.
	 */
	if ((rt->rt_flags & ~ignore & (RTF_CLONING | RTF_PRCLONING)) != 0) {
		struct rtentry *clonedroute;
		int error;

		clonedroute = rt;	/* copy in/copy out parameter */
		error = rtrequest(RTM_RESOLVE, dst, NULL, NULL, 0,
				  &clonedroute);	/* clone the route */
		if (error != 0) {	/* cloning failed */
			if (generate_report)
				rt_dstmsg(RTM_MISS, dst, error);
			rt->rt_refcnt++;
			return (rt);	/* return the uncloned route */
		}
		if (generate_report) {
			if (clonedroute->rt_flags & RTF_XRESOLVE)
				rt_dstmsg(RTM_RESOLVE, dst, 0);
			else
				rt_rtmsg(RTM_ADD, clonedroute,
					 clonedroute->rt_ifp, 0);
		}
		return (clonedroute);	/* return cloned route */
	}

	/*
	 * Increment the reference count of the matched route and return.
	 */
	rt->rt_refcnt++;
	return (rt);

unreach:
	rtstat.rts_unreach++;
	if (generate_report)
		rt_dstmsg(RTM_MISS, dst, 0);
	return (NULL);
}

void
rtfree(struct rtentry *rt)
{
	if (rt->rt_cpuid == mycpuid)
		rtfree_oncpu(rt);
	else
		rtfree_remote(rt);
}

void
rtfree_oncpu(struct rtentry *rt)
{
	KKASSERT(rt->rt_cpuid == mycpuid);
	KASSERT(rt->rt_refcnt > 0, ("rtfree: rt_refcnt %ld", rt->rt_refcnt));

	--rt->rt_refcnt;
	if (rt->rt_refcnt == 0) {
		struct radix_node_head *rnh =
		    rt_tables[mycpuid][rt_key(rt)->sa_family];

		if (rnh->rnh_close)
			rnh->rnh_close((struct radix_node *)rt, rnh);
		if (!(rt->rt_flags & RTF_UP)) {
			/* deallocate route */
			if (rt->rt_ifa != NULL)
				IFAFREE(rt->rt_ifa);
			if (rt->rt_parent != NULL)
				RTFREE(rt->rt_parent);	/* recursive call! */
			Free(rt_key(rt));
			Free(rt);
		}
	}
}

static void
rtfree_remote_dispatch(netmsg_t msg)
{
	struct lwkt_msg *lmsg = &msg->lmsg;
	struct rtentry *rt = lmsg->u.ms_resultp;

	rtfree_oncpu(rt);
	lwkt_replymsg(lmsg, 0);
}

void
rtfree_remote(struct rtentry *rt)
{
	struct netmsg_base *msg;
	struct lwkt_msg *lmsg;

	KKASSERT(rt->rt_cpuid != mycpuid);

	if (route_assert_owner_access) {
		panic("rt remote free rt_cpuid %d, mycpuid %d",
		      rt->rt_cpuid, mycpuid);
	} else {
		kprintf("rt remote free rt_cpuid %d, mycpuid %d\n",
			rt->rt_cpuid, mycpuid);
		print_backtrace(-1);
	}

	msg = kmalloc(sizeof(*msg), M_LWKTMSG, M_INTWAIT);
	netmsg_init(msg, NULL, &netisr_afree_rport, 0, rtfree_remote_dispatch);
	lmsg = &msg->lmsg;
	lmsg->u.ms_resultp = rt;

	lwkt_sendmsg(netisr_cpuport(rt->rt_cpuid), lmsg);
}

int
rtredirect_oncpu(struct sockaddr *dst, struct sockaddr *gateway,
		 struct sockaddr *netmask, int flags, struct sockaddr *src)
{
	struct rtentry *rt = NULL;
	struct rt_addrinfo rtinfo;
	struct ifaddr *ifa;
	u_long *stat = NULL;
	int error;

	/* verify the gateway is directly reachable */
	if ((ifa = ifa_ifwithnet(gateway)) == NULL) {
		error = ENETUNREACH;
		goto out;
	}

	/*
	 * If the redirect isn't from our current router for this destination,
	 * it's either old or wrong.
	 */
	if (!(flags & RTF_DONE) &&		/* XXX JH */
	    (rt = rtpurelookup(dst)) != NULL &&
	    (!sa_equal(src, rt->rt_gateway) || rt->rt_ifa != ifa)) {
		error = EINVAL;
		goto done;
	}

	/*
	 * If it redirects us to ourselves, we have a routing loop,
	 * perhaps as a result of an interface going down recently.
	 */
	if (ifa_ifwithaddr(gateway)) {
		error = EHOSTUNREACH;
		goto done;
	}

	/*
	 * Create a new entry if the lookup failed or if we got back
	 * a wildcard entry for the default route.  This is necessary
	 * for hosts which use routing redirects generated by smart
	 * gateways to dynamically build the routing tables.
	 */
	if (rt == NULL)
		goto create;
	if ((rt_mask(rt) != NULL && rt_mask(rt)->sa_len < 2)) {
		rtfree(rt);
		goto create;
	}

	/* Ignore redirects for directly connected hosts. */
	if (!(rt->rt_flags & RTF_GATEWAY)) {
		error = EHOSTUNREACH;
		goto done;
	}

	if (!(rt->rt_flags & RTF_HOST) && (flags & RTF_HOST)) {
		/*
		 * Changing from a network route to a host route.
		 * Create a new host route rather than smashing the
		 * network route.
		 */
create:
		flags |=  RTF_GATEWAY | RTF_DYNAMIC;
		bzero(&rtinfo, sizeof(struct rt_addrinfo));
		rtinfo.rti_info[RTAX_DST] = dst;
		rtinfo.rti_info[RTAX_GATEWAY] = gateway;
		rtinfo.rti_info[RTAX_NETMASK] = netmask;
		rtinfo.rti_flags = flags;
		rtinfo.rti_ifa = ifa;
		rt = NULL;	/* copy-in/copy-out parameter */
		error = rtrequest1(RTM_ADD, &rtinfo, &rt);
		if (rt != NULL)
			flags = rt->rt_flags;
		stat = &rtstat.rts_dynamic;
	} else {
		/*
		 * Smash the current notion of the gateway to this destination.
		 * Should check about netmask!!!
		 */
		rt->rt_flags |= RTF_MODIFIED;
		flags |= RTF_MODIFIED;

		/* We only need to report rtmsg on CPU0 */
		rt_setgate(rt, rt_key(rt), gateway,
			   mycpuid == 0 ? RTL_REPORTMSG : RTL_DONTREPORT);
		error = 0;
		stat = &rtstat.rts_newgateway;
	}

done:
	if (rt != NULL)
		rtfree(rt);
out:
	if (error != 0)
		rtstat.rts_badredirect++;
	else if (stat != NULL)
		(*stat)++;

	return error;
}

struct netmsg_rtredirect {
	struct netmsg_base base;
	struct sockaddr *dst;
	struct sockaddr *gateway;
	struct sockaddr *netmask;
	int		flags;
	struct sockaddr *src;
};

/*
 * Force a routing table entry to the specified
 * destination to go through the given gateway.
 * Normally called as a result of a routing redirect
 * message from the network layer.
 *
 * N.B.: must be called at splnet
 */
void
rtredirect(struct sockaddr *dst, struct sockaddr *gateway,
	   struct sockaddr *netmask, int flags, struct sockaddr *src)
{
	struct rt_addrinfo rtinfo;
	int error;
	struct netmsg_rtredirect msg;

	netmsg_init(&msg.base, NULL, &curthread->td_msgport,
		    0, rtredirect_msghandler);
	msg.dst = dst;
	msg.gateway = gateway;
	msg.netmask = netmask;
	msg.flags = flags;
	msg.src = src;
	error = rt_domsg_global(&msg.base);
	bzero(&rtinfo, sizeof(struct rt_addrinfo));
	rtinfo.rti_info[RTAX_DST] = dst;
	rtinfo.rti_info[RTAX_GATEWAY] = gateway;
	rtinfo.rti_info[RTAX_NETMASK] = netmask;
	rtinfo.rti_info[RTAX_AUTHOR] = src;
	rt_missmsg(RTM_REDIRECT, &rtinfo, flags, error);
}

static void
rtredirect_msghandler(netmsg_t msg)
{
	struct netmsg_rtredirect *rmsg = (void *)msg;
	int nextcpu;

	rtredirect_oncpu(rmsg->dst, rmsg->gateway, rmsg->netmask,
			 rmsg->flags, rmsg->src);
	nextcpu = mycpuid + 1;
	if (nextcpu < ncpus)
		lwkt_forwardmsg(netisr_cpuport(nextcpu), &msg->lmsg);
	else
		lwkt_replymsg(&msg->lmsg, 0);
}

/*
* Routing table ioctl interface.
*/
int
rtioctl(u_long req, caddr_t data, struct ucred *cred)
{
#ifdef INET
	/* Multicast goop, grrr... */
	return mrt_ioctl ? mrt_ioctl(req, data) : EOPNOTSUPP;
#else
	return ENXIO;
#endif
}

struct ifaddr *
ifa_ifwithroute(int flags, struct sockaddr *dst, struct sockaddr *gateway)
{
	struct ifaddr *ifa;

	if (!(flags & RTF_GATEWAY)) {
		/*
		 * If we are adding a route to an interface,
		 * and the interface is a point-to-point link,
		 * we should search for the destination
		 * as our clue to the interface.  Otherwise
		 * we can use the local address.
		 */
		ifa = NULL;
		if (flags & RTF_HOST) {
			ifa = ifa_ifwithdstaddr(dst);
		}
		if (ifa == NULL)
			ifa = ifa_ifwithaddr(gateway);
	} else {
		/*
		 * If we are adding a route to a remote net
		 * or host, the gateway may still be on the
		 * other end of a pt to pt link.
		 */
		ifa = ifa_ifwithdstaddr(gateway);
	}
	if (ifa == NULL)
		ifa = ifa_ifwithnet(gateway);
	if (ifa == NULL) {
		struct rtentry *rt;

		rt = rtpurelookup(gateway);
		if (rt == NULL)
			return (NULL);
		rt->rt_refcnt--;
		if ((ifa = rt->rt_ifa) == NULL)
			return (NULL);
	}
	if (ifa->ifa_addr->sa_family != dst->sa_family) {
		struct ifaddr *oldifa = ifa;

		ifa = ifaof_ifpforaddr(dst, ifa->ifa_ifp);
		if (ifa == NULL)
			ifa = oldifa;
	}
	return (ifa);
}

static int rt_fixdelete (struct radix_node *, void *);
static int rt_fixchange (struct radix_node *, void *);

struct rtfc_arg {
	struct rtentry *rt0;
	struct radix_node_head *rnh;
};

/*
 * Set rtinfo->rti_ifa and rtinfo->rti_ifp.
 */
int
rt_getifa(struct rt_addrinfo *rtinfo)
{
	struct sockaddr *gateway = rtinfo->rti_info[RTAX_GATEWAY];
	struct sockaddr *dst = rtinfo->rti_info[RTAX_DST];
	struct sockaddr *ifaaddr = rtinfo->rti_info[RTAX_IFA];
	int flags = rtinfo->rti_flags;

	/*
	 * ifp may be specified by sockaddr_dl
	 * when protocol address is ambiguous.
	 */
	if (rtinfo->rti_ifp == NULL) {
		struct sockaddr *ifpaddr;

		ifpaddr = rtinfo->rti_info[RTAX_IFP];
		if (ifpaddr != NULL && ifpaddr->sa_family == AF_LINK) {
			struct ifaddr *ifa;

			ifa = ifa_ifwithnet(ifpaddr);
			if (ifa != NULL)
				rtinfo->rti_ifp = ifa->ifa_ifp;
		}
	}

	if (rtinfo->rti_ifa == NULL && ifaaddr != NULL)
		rtinfo->rti_ifa = ifa_ifwithaddr(ifaaddr);
	if (rtinfo->rti_ifa == NULL) {
		struct sockaddr *sa;

		sa = ifaaddr != NULL ? ifaaddr :
		    (gateway != NULL ? gateway : dst);
		if (sa != NULL && rtinfo->rti_ifp != NULL)
			rtinfo->rti_ifa = ifaof_ifpforaddr(sa, rtinfo->rti_ifp);
		else if (dst != NULL && gateway != NULL)
			rtinfo->rti_ifa = ifa_ifwithroute(flags, dst, gateway);
		else if (sa != NULL)
			rtinfo->rti_ifa = ifa_ifwithroute(flags, sa, sa);
	}
	if (rtinfo->rti_ifa == NULL)
		return (ENETUNREACH);

	if (rtinfo->rti_ifp == NULL)
		rtinfo->rti_ifp = rtinfo->rti_ifa->ifa_ifp;
	return (0);
}

/*
 * Do appropriate manipulations of a routing tree given
 * all the bits of info needed
 */
int
rtrequest(
	int req,
	struct sockaddr *dst,
	struct sockaddr *gateway,
	struct sockaddr *netmask,
	int flags,
	struct rtentry **ret_nrt)
{
	struct rt_addrinfo rtinfo;

	bzero(&rtinfo, sizeof(struct rt_addrinfo));
	rtinfo.rti_info[RTAX_DST] = dst;
	rtinfo.rti_info[RTAX_GATEWAY] = gateway;
	rtinfo.rti_info[RTAX_NETMASK] = netmask;
	rtinfo.rti_flags = flags;
	return rtrequest1(req, &rtinfo, ret_nrt);
}

int
rtrequest_global(
	int req,
	struct sockaddr *dst,
	struct sockaddr *gateway,
	struct sockaddr *netmask,
	int flags)
{
	struct rt_addrinfo rtinfo;

	bzero(&rtinfo, sizeof(struct rt_addrinfo));
	rtinfo.rti_info[RTAX_DST] = dst;
	rtinfo.rti_info[RTAX_GATEWAY] = gateway;
	rtinfo.rti_info[RTAX_NETMASK] = netmask;
	rtinfo.rti_flags = flags;
	return rtrequest1_global(req, &rtinfo, NULL, NULL, RTREQ_PRIO_NORM);
}

struct netmsg_rtq {
	struct netmsg_base	base;
	int			req;
	struct rt_addrinfo	*rtinfo;
	rtrequest1_callback_func_t callback;
	void			*arg;
};

int
rtrequest1_global(int req, struct rt_addrinfo *rtinfo,
    rtrequest1_callback_func_t callback, void *arg, boolean_t req_prio)
{
	int error, flags = 0;
	struct netmsg_rtq msg;

	if (req_prio)
		flags = MSGF_PRIORITY;
	netmsg_init(&msg.base, NULL, &curthread->td_msgport, flags,
	    rtrequest1_msghandler);
	msg.base.lmsg.ms_error = -1;
	msg.req = req;
	msg.rtinfo = rtinfo;
	msg.callback = callback;
	msg.arg = arg;
	error = rt_domsg_global(&msg.base);
	return (error);
}

/*
 * Handle a route table request on the current cpu.  Since the route table's
 * are supposed to be identical on each cpu, an error occuring later in the
 * message chain is considered system-fatal.
 */
static void
rtrequest1_msghandler(netmsg_t msg)
{
	struct netmsg_rtq *rmsg = (void *)msg;
	struct rt_addrinfo rtinfo;
	struct rtentry *rt = NULL;
	int nextcpu;
	int error;

	/*
	 * Copy the rtinfo.  We need to make sure that the original
	 * rtinfo, which is setup by the caller, in the netmsg will
	 * _not_ be changed; else the next CPU on the netmsg forwarding
	 * path will see a different rtinfo than what this CPU has seen.
	 */
	rtinfo = *rmsg->rtinfo;

	error = rtrequest1(rmsg->req, &rtinfo, &rt);
	if (rt)
		--rt->rt_refcnt;
	if (rmsg->callback)
		rmsg->callback(rmsg->req, error, &rtinfo, rt, rmsg->arg);

	/*
	 * RTM_DELETE's are propogated even if an error occurs, since a
	 * cloned route might be undergoing deletion and cloned routes
	 * are not necessarily replicated.  An overall error is returned
	 * only if no cpus have the route in question.
	 */
	if (rmsg->base.lmsg.ms_error < 0 || error == 0)
		rmsg->base.lmsg.ms_error = error;

	nextcpu = mycpuid + 1;
	if (error && rmsg->req != RTM_DELETE) {
		if (mycpuid != 0) {
			panic("rtrequest1_msghandler: rtrequest table "
			      "error was cpu%d, err %d\n", mycpuid, error);
		}
		lwkt_replymsg(&rmsg->base.lmsg, error);
	} else if (nextcpu < ncpus) {
		lwkt_forwardmsg(netisr_cpuport(nextcpu), &rmsg->base.lmsg);
	} else {
		lwkt_replymsg(&rmsg->base.lmsg, rmsg->base.lmsg.ms_error);
	}
}

int
rtrequest1(int req, struct rt_addrinfo *rtinfo, struct rtentry **ret_nrt)
{
	struct sockaddr *dst = rtinfo->rti_info[RTAX_DST];
	struct rtentry *rt;
	struct radix_node *rn;
	struct radix_node_head *rnh;
	struct ifaddr *ifa;
	struct sockaddr *ndst;
	boolean_t reportmsg;
	int error = 0;

#define gotoerr(x) { error = x ; goto bad; }

#ifdef ROUTE_DEBUG
	if (route_debug)
		rt_addrinfo_print(req, rtinfo);
#endif

	crit_enter();
	/*
	 * Find the correct routing tree to use for this Address Family
	 */
	if ((rnh = rt_tables[mycpuid][dst->sa_family]) == NULL)
		gotoerr(EAFNOSUPPORT);

	/*
	 * If we are adding a host route then we don't want to put
	 * a netmask in the tree, nor do we want to clone it.
	 */
	if (rtinfo->rti_flags & RTF_HOST) {
		rtinfo->rti_info[RTAX_NETMASK] = NULL;
		rtinfo->rti_flags &= ~(RTF_CLONING | RTF_PRCLONING);
	}

	switch (req) {
	case RTM_DELETE:
		/* Remove the item from the tree. */
		rn = rnh->rnh_deladdr((char *)rtinfo->rti_info[RTAX_DST],
				      (char *)rtinfo->rti_info[RTAX_NETMASK],
				      rnh);
		if (rn == NULL)
			gotoerr(ESRCH);
		KASSERT(!(rn->rn_flags & (RNF_ACTIVE | RNF_ROOT)),
			("rnh_deladdr returned flags 0x%x", rn->rn_flags));
		rt = (struct rtentry *)rn;

		/* ref to prevent a deletion race */
		++rt->rt_refcnt;

		/* Free any routes cloned from this one. */
		if ((rt->rt_flags & (RTF_CLONING | RTF_PRCLONING)) &&
		    rt_mask(rt) != NULL) {
			rnh->rnh_walktree_from(rnh, (char *)rt_key(rt),
					       (char *)rt_mask(rt),
					       rt_fixdelete, rt);
		}

		if (rt->rt_gwroute != NULL) {
			RTFREE(rt->rt_gwroute);
			rt->rt_gwroute = NULL;
		}

		/*
		 * NB: RTF_UP must be set during the search above,
		 * because we might delete the last ref, causing
		 * rt to get freed prematurely.
		 */
		rt->rt_flags &= ~RTF_UP;

#ifdef ROUTE_DEBUG
		if (route_debug)
			rt_print(rtinfo, rt);
#endif

		/* Give the protocol a chance to keep things in sync. */
		if ((ifa = rt->rt_ifa) && ifa->ifa_rtrequest)
			ifa->ifa_rtrequest(RTM_DELETE, rt);

		/*
		 * If the caller wants it, then it can have it,
		 * but it's up to it to free the rtentry as we won't be
		 * doing it.
		 */
		KASSERT(rt->rt_refcnt >= 0,
			("rtrequest1(DELETE): refcnt %ld", rt->rt_refcnt));
		if (ret_nrt != NULL) {
			/* leave ref intact for return */
			*ret_nrt = rt;
		} else {
			/* deref / attempt to destroy */
			rtfree(rt);
		}
		break;

	case RTM_RESOLVE:
		if (ret_nrt == NULL || (rt = *ret_nrt) == NULL)
			gotoerr(EINVAL);

		KASSERT(rt->rt_cpuid == mycpuid,
		    ("rt resolve rt_cpuid %d, mycpuid %d",
		     rt->rt_cpuid, mycpuid));

		ifa = rt->rt_ifa;
		rtinfo->rti_flags =
		    rt->rt_flags & ~(RTF_CLONING | RTF_PRCLONING | RTF_STATIC);
		rtinfo->rti_flags |= RTF_WASCLONED;
		rtinfo->rti_info[RTAX_GATEWAY] = rt->rt_gateway;
		if ((rtinfo->rti_info[RTAX_NETMASK] = rt->rt_genmask) == NULL)
			rtinfo->rti_flags |= RTF_HOST;
		rtinfo->rti_info[RTAX_MPLS1] = rt->rt_shim[0];
		rtinfo->rti_info[RTAX_MPLS2] = rt->rt_shim[1];
		rtinfo->rti_info[RTAX_MPLS3] = rt->rt_shim[2];
		goto makeroute;

	case RTM_ADD:
		KASSERT(!(rtinfo->rti_flags & RTF_GATEWAY) ||
			rtinfo->rti_info[RTAX_GATEWAY] != NULL,
		    ("rtrequest: GATEWAY but no gateway"));

		if (rtinfo->rti_ifa == NULL && (error = rt_getifa(rtinfo)))
			gotoerr(error);
		ifa = rtinfo->rti_ifa;
makeroute:
		R_Malloc(rt, struct rtentry *, sizeof(struct rtentry));
		if (rt == NULL) {
			if (req == RTM_ADD) {
				kprintf("rtrequest1: alloc rtentry failed on "
				    "cpu%d\n", mycpuid);
			}
			gotoerr(ENOBUFS);
		}
		bzero(rt, sizeof(struct rtentry));
		rt->rt_flags = RTF_UP | rtinfo->rti_flags;
		rt->rt_cpuid = mycpuid;

		if (mycpuid != 0 && req == RTM_ADD) {
			/* For RTM_ADD, we have already sent rtmsg on CPU0. */
			reportmsg = RTL_DONTREPORT;
		} else {
			/*
			 * For RTM_ADD, we only send rtmsg on CPU0.
			 * For RTM_RESOLVE, we always send rtmsg. XXX
			 */
			reportmsg = RTL_REPORTMSG;
		}
		error = rt_setgate(rt, dst, rtinfo->rti_info[RTAX_GATEWAY],
				   reportmsg);
		if (error != 0) {
			Free(rt);
			gotoerr(error);
		}

		ndst = rt_key(rt);
		if (rtinfo->rti_info[RTAX_NETMASK] != NULL)
			rt_maskedcopy(dst, ndst,
				      rtinfo->rti_info[RTAX_NETMASK]);
		else
			bcopy(dst, ndst, dst->sa_len);

		if (rtinfo->rti_info[RTAX_MPLS1] != NULL)
			rt_setshims(rt, rtinfo->rti_info);

		/*
		 * Note that we now have a reference to the ifa.
		 * This moved from below so that rnh->rnh_addaddr() can
		 * examine the ifa and  ifa->ifa_ifp if it so desires.
		 */
		IFAREF(ifa);
		rt->rt_ifa = ifa;
		rt->rt_ifp = ifa->ifa_ifp;
		/* XXX mtu manipulation will be done in rnh_addaddr -- itojun */

		rn = rnh->rnh_addaddr((char *)ndst,
				      (char *)rtinfo->rti_info[RTAX_NETMASK],
				      rnh, rt->rt_nodes);
		if (rn == NULL) {
			struct rtentry *oldrt;

			/*
			 * We already have one of these in the tree.
			 * We do a special hack: if the old route was
			 * cloned, then we blow it away and try
			 * re-inserting the new one.
			 */
			oldrt = rtpurelookup(ndst);
			if (oldrt != NULL) {
				--oldrt->rt_refcnt;
				if (oldrt->rt_flags & RTF_WASCLONED) {
					rtrequest(RTM_DELETE, rt_key(oldrt),
						  oldrt->rt_gateway,
						  rt_mask(oldrt),
						  oldrt->rt_flags, NULL);
					rn = rnh->rnh_addaddr((char *)ndst,
					    (char *)
						rtinfo->rti_info[RTAX_NETMASK],
					    rnh, rt->rt_nodes);
				}
			}
		}

		/*
		 * If it still failed to go into the tree,
		 * then un-make it (this should be a function).
		 */
		if (rn == NULL) {
			if (rt->rt_gwroute != NULL)
				rtfree(rt->rt_gwroute);
			IFAFREE(ifa);
			Free(rt_key(rt));
			Free(rt);
			gotoerr(EEXIST);
		}

		/*
		 * If we got here from RESOLVE, then we are cloning
		 * so clone the rest, and note that we
		 * are a clone (and increment the parent's references)
		 */
		if (req == RTM_RESOLVE) {
			rt->rt_rmx = (*ret_nrt)->rt_rmx;    /* copy metrics */
			rt->rt_rmx.rmx_pksent = 0;  /* reset packet counter */
			if ((*ret_nrt)->rt_flags &
				       (RTF_CLONING | RTF_PRCLONING)) {
				rt->rt_parent = *ret_nrt;
				(*ret_nrt)->rt_refcnt++;
			}
		}

		/*
		 * if this protocol has something to add to this then
		 * allow it to do that as well.
		 */
		if (ifa->ifa_rtrequest != NULL)
			ifa->ifa_rtrequest(req, rt);

		/*
		 * We repeat the same procedure from rt_setgate() here because
		 * it doesn't fire when we call it there because the node
		 * hasn't been added to the tree yet.
		 */
		if (req == RTM_ADD && !(rt->rt_flags & RTF_HOST) &&
		    rt_mask(rt) != NULL) {
			struct rtfc_arg arg = { rt, rnh };

			rnh->rnh_walktree_from(rnh, (char *)rt_key(rt),
					       (char *)rt_mask(rt),
					       rt_fixchange, &arg);
		}

#ifdef ROUTE_DEBUG
		if (route_debug)
			rt_print(rtinfo, rt);
#endif
		/*
		 * Return the resulting rtentry,
		 * increasing the number of references by one.
		 */
		if (ret_nrt != NULL) {
			rt->rt_refcnt++;
			*ret_nrt = rt;
		}
		break;
	default:
		error = EOPNOTSUPP;
	}
bad:
#ifdef ROUTE_DEBUG
	if (route_debug) {
		if (error)
			kprintf("rti %p failed error %d\n", rtinfo, error);
		else
			kprintf("rti %p succeeded\n", rtinfo);
	}
#endif
	crit_exit();
	return (error);
}

/*
 * Called from rtrequest(RTM_DELETE, ...) to fix up the route's ``family''
 * (i.e., the routes related to it by the operation of cloning).  This
 * routine is iterated over all potential former-child-routes by way of
 * rnh->rnh_walktree_from() above, and those that actually are children of
 * the late parent (passed in as VP here) are themselves deleted.
 */
static int
rt_fixdelete(struct radix_node *rn, void *vp)
{
	struct rtentry *rt = (struct rtentry *)rn;
	struct rtentry *rt0 = vp;

	if (rt->rt_parent == rt0 &&
	    !(rt->rt_flags & (RTF_PINNED | RTF_CLONING | RTF_PRCLONING))) {
		return rtrequest(RTM_DELETE, rt_key(rt), NULL, rt_mask(rt),
				 rt->rt_flags, NULL);
	}
	return 0;
}

/*
 * This routine is called from rt_setgate() to do the analogous thing for
 * adds and changes.  There is the added complication in this case of a
 * middle insert; i.e., insertion of a new network route between an older
 * network route and (cloned) host routes.  For this reason, a simple check
 * of rt->rt_parent is insufficient; each candidate route must be tested
 * against the (mask, value) of the new route (passed as before in vp)
 * to see if the new route matches it.
 *
 * XXX - it may be possible to do fixdelete() for changes and reserve this
 * routine just for adds.  I'm not sure why I thought it was necessary to do
 * changes this way.
 */
#ifdef DEBUG
static int rtfcdebug = 0;
#endif

static int
rt_fixchange(struct radix_node *rn, void *vp)
{
	struct rtentry *rt = (struct rtentry *)rn;
	struct rtfc_arg *ap = vp;
	struct rtentry *rt0 = ap->rt0;
	struct radix_node_head *rnh = ap->rnh;
	u_char *xk1, *xm1, *xk2, *xmp;
	int i, len, mlen;

#ifdef DEBUG
	if (rtfcdebug)
		kprintf("rt_fixchange: rt %p, rt0 %p\n", rt, rt0);
#endif

	if (rt->rt_parent == NULL ||
	    (rt->rt_flags & (RTF_PINNED | RTF_CLONING | RTF_PRCLONING))) {
#ifdef DEBUG
		if (rtfcdebug) kprintf("no parent, pinned or cloning\n");
#endif
		return 0;
	}

	if (rt->rt_parent == rt0) {
#ifdef DEBUG
		if (rtfcdebug) kprintf("parent match\n");
#endif
		return rtrequest(RTM_DELETE, rt_key(rt), NULL, rt_mask(rt),
				 rt->rt_flags, NULL);
	}

	/*
	 * There probably is a function somewhere which does this...
	 * if not, there should be.
	 */
	len = imin(rt_key(rt0)->sa_len, rt_key(rt)->sa_len);

	xk1 = (u_char *)rt_key(rt0);
	xm1 = (u_char *)rt_mask(rt0);
	xk2 = (u_char *)rt_key(rt);

	/* avoid applying a less specific route */
	xmp = (u_char *)rt_mask(rt->rt_parent);
	mlen = rt_key(rt->rt_parent)->sa_len;
	if (mlen > rt_key(rt0)->sa_len) {
#ifdef DEBUG
		if (rtfcdebug)
			kprintf("rt_fixchange: inserting a less "
			       "specific route\n");
#endif
		return 0;
	}
	for (i = rnh->rnh_treetop->rn_offset; i < mlen; i++) {
		if ((xmp[i] & ~(xmp[i] ^ xm1[i])) != xmp[i]) {
#ifdef DEBUG
			if (rtfcdebug)
				kprintf("rt_fixchange: inserting a less "
				       "specific route\n");
#endif
			return 0;
		}
	}

	for (i = rnh->rnh_treetop->rn_offset; i < len; i++) {
		if ((xk2[i] & xm1[i]) != xk1[i]) {
#ifdef DEBUG
			if (rtfcdebug) kprintf("no match\n");
#endif
			return 0;
		}
	}

	/*
	 * OK, this node is a clone, and matches the node currently being
	 * changed/added under the node's mask.  So, get rid of it.
	 */
#ifdef DEBUG
	if (rtfcdebug) kprintf("deleting\n");
#endif
	return rtrequest(RTM_DELETE, rt_key(rt), NULL, rt_mask(rt),
			 rt->rt_flags, NULL);
}

int
rt_setgate(struct rtentry *rt0, struct sockaddr *dst, struct sockaddr *gate,
	   boolean_t generate_report)
{
	char *space, *oldspace;
	int dlen = RT_ROUNDUP(dst->sa_len), glen = RT_ROUNDUP(gate->sa_len);
	struct rtentry *rt = rt0;
	struct radix_node_head *rnh = rt_tables[mycpuid][dst->sa_family];

	/*
	 * A host route with the destination equal to the gateway
	 * will interfere with keeping LLINFO in the routing
	 * table, so disallow it.
	 */
	if (((rt0->rt_flags & (RTF_HOST | RTF_GATEWAY | RTF_LLINFO)) ==
			      (RTF_HOST | RTF_GATEWAY)) &&
	    dst->sa_len == gate->sa_len &&
	    sa_equal(dst, gate)) {
		/*
		 * The route might already exist if this is an RTM_CHANGE
		 * or a routing redirect, so try to delete it.
		 */
		if (rt_key(rt0) != NULL)
			rtrequest(RTM_DELETE, rt_key(rt0), rt0->rt_gateway,
				  rt_mask(rt0), rt0->rt_flags, NULL);
		return EADDRNOTAVAIL;
	}

	/*
	 * Both dst and gateway are stored in the same malloc'ed chunk
	 * (If I ever get my hands on....)
	 * if we need to malloc a new chunk, then keep the old one around
	 * till we don't need it any more.
	 */
	if (rt->rt_gateway == NULL ||
	    glen > RT_ROUNDUP(rt->rt_gateway->sa_len)) {
		oldspace = (char *)rt_key(rt);
		R_Malloc(space, char *, dlen + glen);
		if (space == NULL)
			return ENOBUFS;
		rt->rt_nodes->rn_key = space;
	} else {
		space = (char *)rt_key(rt);	/* Just use the old space. */
		oldspace = NULL;
	}

	/* Set the gateway value. */
	rt->rt_gateway = (struct sockaddr *)(space + dlen);
	bcopy(gate, rt->rt_gateway, glen);

	if (oldspace != NULL) {
		/*
		 * If we allocated a new chunk, preserve the original dst.
		 * This way, rt_setgate() really just sets the gate
		 * and leaves the dst field alone.
		 */
		bcopy(dst, space, dlen);
		Free(oldspace);
	}

	/*
	 * If there is already a gwroute, it's now almost definitely wrong
	 * so drop it.
	 */
	if (rt->rt_gwroute != NULL) {
		RTFREE(rt->rt_gwroute);
		rt->rt_gwroute = NULL;
	}
	if (rt->rt_flags & RTF_GATEWAY) {
		/*
		 * Cloning loop avoidance: In the presence of
		 * protocol-cloning and bad configuration, it is
		 * possible to get stuck in bottomless mutual recursion
		 * (rtrequest rt_setgate rtlookup).  We avoid this
		 * by not allowing protocol-cloning to operate for
		 * gateways (which is probably the correct choice
		 * anyway), and avoid the resulting reference loops
		 * by disallowing any route to run through itself as
		 * a gateway.  This is obviously mandatory when we
		 * get rt->rt_output().
		 *
		 * This breaks TTCP for hosts outside the gateway!  XXX JH
		 */
		rt->rt_gwroute = _rtlookup(gate, generate_report,
					   RTF_PRCLONING);
		if (rt->rt_gwroute == rt) {
			rt->rt_gwroute = NULL;
			--rt->rt_refcnt;
			return EDQUOT; /* failure */
		}
	}

	/*
	 * This isn't going to do anything useful for host routes, so
	 * don't bother.  Also make sure we have a reasonable mask
	 * (we don't yet have one during adds).
	 */
	if (!(rt->rt_flags & RTF_HOST) && rt_mask(rt) != NULL) {
		struct rtfc_arg arg = { rt, rnh };

		rnh->rnh_walktree_from(rnh, (char *)rt_key(rt),
				       (char *)rt_mask(rt),
				       rt_fixchange, &arg);
	}

	return 0;
}

static void
rt_maskedcopy(
	struct sockaddr *src,
	struct sockaddr *dst,
	struct sockaddr *netmask)
{
	u_char *cp1 = (u_char *)src;
	u_char *cp2 = (u_char *)dst;
	u_char *cp3 = (u_char *)netmask;
	u_char *cplim = cp2 + *cp3;
	u_char *cplim2 = cp2 + *cp1;

	*cp2++ = *cp1++; *cp2++ = *cp1++; /* copies sa_len & sa_family */
	cp3 += 2;
	if (cplim > cplim2)
		cplim = cplim2;
	while (cp2 < cplim)
		*cp2++ = *cp1++ & *cp3++;
	if (cp2 < cplim2)
		bzero(cp2, cplim2 - cp2);
}

int
rt_llroute(struct sockaddr *dst, struct rtentry *rt0, struct rtentry **drt)
{
	struct rtentry *up_rt, *rt;

	if (!(rt0->rt_flags & RTF_UP)) {
		up_rt = rtlookup(dst);
		if (up_rt == NULL)
			return (EHOSTUNREACH);
		up_rt->rt_refcnt--;
	} else
		up_rt = rt0;
	if (up_rt->rt_flags & RTF_GATEWAY) {
		if (up_rt->rt_gwroute == NULL) {
			up_rt->rt_gwroute = rtlookup(up_rt->rt_gateway);
			if (up_rt->rt_gwroute == NULL)
				return (EHOSTUNREACH);
		} else if (!(up_rt->rt_gwroute->rt_flags & RTF_UP)) {
			rtfree(up_rt->rt_gwroute);
			up_rt->rt_gwroute = rtlookup(up_rt->rt_gateway);
			if (up_rt->rt_gwroute == NULL)
				return (EHOSTUNREACH);
		}
		rt = up_rt->rt_gwroute;
	} else
		rt = up_rt;
	if (rt->rt_flags & RTF_REJECT &&
	    (rt->rt_rmx.rmx_expire == 0 ||		/* rt doesn't expire */
	     time_uptime < rt->rt_rmx.rmx_expire))	/* rt not expired */
		return (rt->rt_flags & RTF_HOST ?  EHOSTDOWN : EHOSTUNREACH);
	*drt = rt;
	return 0;
}

static int
rt_setshims(struct rtentry *rt, struct sockaddr **rt_shim){
	int i;
	
	for (i=0; i<3; i++) {
		struct sockaddr *shim = rt_shim[RTAX_MPLS1 + i];
		int shimlen;

		if (shim == NULL)
			break;

		shimlen = RT_ROUNDUP(shim->sa_len);
		R_Malloc(rt->rt_shim[i], struct sockaddr *, shimlen);
		bcopy(shim, rt->rt_shim[i], shimlen);
	}

	return 0;
}

#ifdef ROUTE_DEBUG

/*
 * Print out a route table entry
 */
void
rt_print(struct rt_addrinfo *rtinfo, struct rtentry *rn)
{
	kprintf("rti %p cpu %d route %p flags %08lx: ", 
		rtinfo, mycpuid, rn, rn->rt_flags);
	sockaddr_print(rt_key(rn));
	kprintf(" mask ");
	sockaddr_print(rt_mask(rn));
	kprintf(" gw ");
	sockaddr_print(rn->rt_gateway);
	kprintf(" ifc \"%s\"", rn->rt_ifp ? rn->rt_ifp->if_dname : "?");
	kprintf(" ifa %p\n", rn->rt_ifa);
}

void
rt_addrinfo_print(int cmd, struct rt_addrinfo *rti)
{
	int didit = 0;
	int i;

#ifdef ROUTE_DEBUG
	if (cmd == RTM_DELETE && route_debug > 1)
		print_backtrace(-1);
#endif

	switch(cmd) {
	case RTM_ADD:
		kprintf("ADD ");
		break;
	case RTM_RESOLVE:
		kprintf("RES ");
		break;
	case RTM_DELETE:
		kprintf("DEL ");
		break;
	default:
		kprintf("C%02d ", cmd);
		break;
	}
	kprintf("rti %p cpu %d ", rti, mycpuid);
	for (i = 0; i < rti->rti_addrs; ++i) {
		if (rti->rti_info[i] == NULL)
			continue;
		if (didit)
			kprintf(" ,");
		switch(i) {
		case RTAX_DST:
			kprintf("(DST ");
			break;
		case RTAX_GATEWAY:
			kprintf("(GWY ");
			break;
		case RTAX_NETMASK:
			kprintf("(MSK ");
			break;
		case RTAX_GENMASK:
			kprintf("(GEN ");
			break;
		case RTAX_IFP:
			kprintf("(IFP ");
			break;
		case RTAX_IFA:
			kprintf("(IFA ");
			break;
		case RTAX_AUTHOR:
			kprintf("(AUT ");
			break;
		case RTAX_BRD:
			kprintf("(BRD ");
			break;
		default:
			kprintf("(?%02d ", i);
			break;
		}
		sockaddr_print(rti->rti_info[i]);
		kprintf(")");
		didit = 1;
	}
	kprintf("\n");
}

void
sockaddr_print(struct sockaddr *sa)
{
	struct sockaddr_in *sa4;
	struct sockaddr_in6 *sa6;
	int len;
	int i;

	if (sa == NULL) {
		kprintf("NULL");
		return;
	}

	len = sa->sa_len - offsetof(struct sockaddr, sa_data[0]);

	switch(sa->sa_family) {
	case AF_INET:
	case AF_INET6:
	default:
		switch(sa->sa_family) {
		case AF_INET:
			sa4 = (struct sockaddr_in *)sa;
			kprintf("INET %d %d.%d.%d.%d",
				ntohs(sa4->sin_port),
				(ntohl(sa4->sin_addr.s_addr) >> 24) & 255,
				(ntohl(sa4->sin_addr.s_addr) >> 16) & 255,
				(ntohl(sa4->sin_addr.s_addr) >> 8) & 255,
				(ntohl(sa4->sin_addr.s_addr) >> 0) & 255
			);
			break;
		case AF_INET6:
			sa6 = (struct sockaddr_in6 *)sa;
			kprintf("INET6 %d %04x:%04x%04x:%04x:%04x:%04x:%04x:%04x",
				ntohs(sa6->sin6_port),
				sa6->sin6_addr.s6_addr16[0],
				sa6->sin6_addr.s6_addr16[1],
				sa6->sin6_addr.s6_addr16[2],
				sa6->sin6_addr.s6_addr16[3],
				sa6->sin6_addr.s6_addr16[4],
				sa6->sin6_addr.s6_addr16[5],
				sa6->sin6_addr.s6_addr16[6],
				sa6->sin6_addr.s6_addr16[7]
			);
			break;
		default:
			kprintf("AF%d ", sa->sa_family);
			while (len > 0 && sa->sa_data[len-1] == 0)
				--len;

			for (i = 0; i < len; ++i) {
				if (i)
					kprintf(".");
				kprintf("%d", (unsigned char)sa->sa_data[i]);
			}
			break;
		}
	}
}

#endif

/*
 * Set up a routing table entry, normally for an interface.
 */
int
rtinit(struct ifaddr *ifa, int cmd, int flags)
{
	struct sockaddr *dst, *deldst, *netmask;
	struct mbuf *m = NULL;
	struct radix_node_head *rnh;
	struct radix_node *rn;
	struct rt_addrinfo rtinfo;
	int error;

	if (flags & RTF_HOST) {
		dst = ifa->ifa_dstaddr;
		netmask = NULL;
	} else {
		dst = ifa->ifa_addr;
		netmask = ifa->ifa_netmask;
	}
	/*
	 * If it's a delete, check that if it exists, it's on the correct
	 * interface or we might scrub a route to another ifa which would
	 * be confusing at best and possibly worse.
	 */
	if (cmd == RTM_DELETE) {
		/*
		 * It's a delete, so it should already exist..
		 * If it's a net, mask off the host bits
		 * (Assuming we have a mask)
		 */
		if (netmask != NULL) {
			m = m_get(M_NOWAIT, MT_SONAME);
			if (m == NULL)
				return (ENOBUFS);
			mbuftrackid(m, 34);
			deldst = mtod(m, struct sockaddr *);
			rt_maskedcopy(dst, deldst, netmask);
			dst = deldst;
		}
		/*
		 * Look up an rtentry that is in the routing tree and
		 * contains the correct info.
		 */
		if ((rnh = rt_tables[mycpuid][dst->sa_family]) == NULL ||
		    (rn = rnh->rnh_lookup((char *)dst,
					  (char *)netmask, rnh)) == NULL ||
		    ((struct rtentry *)rn)->rt_ifa != ifa ||
		    !sa_equal((struct sockaddr *)rn->rn_key, dst)) {
			if (m != NULL)
				m_free(m);
			return (flags & RTF_HOST ? EHOSTUNREACH : ENETUNREACH);
		}
		/* XXX */
#if 0
		else {
			/*
			 * One would think that as we are deleting, and we know
			 * it doesn't exist, we could just return at this point
			 * with an "ELSE" clause, but apparently not..
			 */
			return (flags & RTF_HOST ? EHOSTUNREACH : ENETUNREACH);
		}
#endif
	}
	/*
	 * Do the actual request
	 */
	bzero(&rtinfo, sizeof(struct rt_addrinfo));
	rtinfo.rti_info[RTAX_DST] = dst;
	rtinfo.rti_info[RTAX_GATEWAY] = ifa->ifa_addr;
	rtinfo.rti_info[RTAX_NETMASK] = netmask;
	rtinfo.rti_flags = flags | ifa->ifa_flags;
	rtinfo.rti_ifa = ifa;
	error = rtrequest1_global(cmd, &rtinfo, rtinit_rtrequest_callback, ifa,
	    RTREQ_PRIO_HIGH);
	if (m != NULL)
		m_free(m);
	return (error);
}

static void
rtinit_rtrequest_callback(int cmd, int error,
			  struct rt_addrinfo *rtinfo, struct rtentry *rt,
			  void *arg)
{
	struct ifaddr *ifa = arg;

	if (error == 0 && rt) {
		if (mycpuid == 0) {
			++rt->rt_refcnt;
			rt_newaddrmsg(cmd, ifa, error, rt);
			--rt->rt_refcnt;
		}
		if (cmd == RTM_DELETE) {
			if (rt->rt_refcnt == 0) {
				++rt->rt_refcnt;
				rtfree(rt);
			}
		}
	}
}

struct netmsg_rts {
	struct netmsg_base	base;
	int			req;
	struct rt_addrinfo	*rtinfo;
	rtsearch_callback_func_t callback;
	void			*arg;
	boolean_t		exact_match;
	int			found_cnt;
};

int
rtsearch_global(int req, struct rt_addrinfo *rtinfo,
    rtsearch_callback_func_t callback, void *arg, boolean_t exact_match,
    boolean_t req_prio)
{
	struct netmsg_rts msg;
	int flags = 0;

	if (req_prio)
		flags = MSGF_PRIORITY;
	netmsg_init(&msg.base, NULL, &curthread->td_msgport, flags,
	    rtsearch_msghandler);
	msg.req = req;
	msg.rtinfo = rtinfo;
	msg.callback = callback;
	msg.arg = arg;
	msg.exact_match = exact_match;
	msg.found_cnt = 0;
	return rt_domsg_global(&msg.base);
}

static void
rtsearch_msghandler(netmsg_t msg)
{
	struct netmsg_rts *rmsg = (void *)msg;
	struct rt_addrinfo rtinfo;
	struct radix_node_head *rnh;
	struct rtentry *rt;
	int nextcpu, error;

	/*
	 * Copy the rtinfo.  We need to make sure that the original
	 * rtinfo, which is setup by the caller, in the netmsg will
	 * _not_ be changed; else the next CPU on the netmsg forwarding
	 * path will see a different rtinfo than what this CPU has seen.
	 */
	rtinfo = *rmsg->rtinfo;

	/*
	 * Find the correct routing tree to use for this Address Family
	 */
	if ((rnh = rt_tables[mycpuid][rtinfo.rti_dst->sa_family]) == NULL) {
		if (mycpuid != 0)
			panic("partially initialized routing tables");
		lwkt_replymsg(&rmsg->base.lmsg, EAFNOSUPPORT);
		return;
	}

	/*
	 * Correct rtinfo for the host route searching.
	 */
	if (rtinfo.rti_flags & RTF_HOST) {
		rtinfo.rti_netmask = NULL;
		rtinfo.rti_flags &= ~(RTF_CLONING | RTF_PRCLONING);
	}

	rt = (struct rtentry *)
	     rnh->rnh_lookup((char *)rtinfo.rti_dst,
			     (char *)rtinfo.rti_netmask, rnh);

	/*
	 * If we are asked to do the "exact match", we need to make sure
	 * that host route searching got a host route while a network
	 * route searching got a network route.
	 */
	if (rt != NULL && rmsg->exact_match &&
	    ((rt->rt_flags ^ rtinfo.rti_flags) & RTF_HOST))
		rt = NULL;

	if (rt == NULL) {
		/*
		 * No matching routes have been found, don't count this
		 * as a critical error (here, we set 'error' to 0), just
		 * keep moving on, since at least prcloned routes are not
		 * duplicated onto each CPU.
		 */
		error = 0;
	} else {
		rmsg->found_cnt++;

		rt->rt_refcnt++;
		error = rmsg->callback(rmsg->req, &rtinfo, rt, rmsg->arg,
				      rmsg->found_cnt);
		rt->rt_refcnt--;

		if (error == EJUSTRETURN) {
			lwkt_replymsg(&rmsg->base.lmsg, 0);
			return;
		}
	}

	nextcpu = mycpuid + 1;
	if (error) {
		KKASSERT(rmsg->found_cnt > 0);

		/*
		 * Under following cases, unrecoverable error has
		 * not occured:
		 * o  Request is RTM_GET
		 * o  The first time that we find the route, but the
		 *    modification fails.
		 */
		if (rmsg->req != RTM_GET && rmsg->found_cnt > 1) {
			panic("rtsearch_msghandler: unrecoverable error "
			      "cpu %d", mycpuid);
		}
		lwkt_replymsg(&rmsg->base.lmsg, error);
	} else if (nextcpu < ncpus) {
		lwkt_forwardmsg(netisr_cpuport(nextcpu), &rmsg->base.lmsg);
	} else {
		if (rmsg->found_cnt == 0) {
			/* The requested route was never seen ... */
			error = ESRCH;
		}
		lwkt_replymsg(&rmsg->base.lmsg, error);
	}
}

int
rtmask_add_global(struct sockaddr *mask, boolean_t req_prio)
{
	struct netmsg_base msg;
	int flags = 0;

	if (req_prio)
		flags = MSGF_PRIORITY;
	netmsg_init(&msg, NULL, &curthread->td_msgport, flags,
	    rtmask_add_msghandler);
	msg.lmsg.u.ms_resultp = mask;

	return rt_domsg_global(&msg);
}

struct sockaddr *
_rtmask_lookup(struct sockaddr *mask, boolean_t search)
{
	struct radix_node *n;

#define	clen(s)	(*(u_char *)(s))
	n = rn_addmask((char *)mask, search, 1, rn_cpumaskhead(mycpuid));
	if (n != NULL &&
	    mask->sa_len >= clen(n->rn_key) &&
	    bcmp((char *)mask + 1,
		 (char *)n->rn_key + 1, clen(n->rn_key) - 1) == 0) {
		return (struct sockaddr *)n->rn_key;
	} else {
		return NULL;
	}
#undef clen
}

static void
rtmask_add_msghandler(netmsg_t msg)
{
	struct lwkt_msg *lmsg = &msg->lmsg;
	struct sockaddr *mask = lmsg->u.ms_resultp;
	int error = 0, nextcpu;

	if (rtmask_lookup(mask) == NULL)
		error = ENOBUFS;

	nextcpu = mycpuid + 1;
	if (!error && nextcpu < ncpus)
		lwkt_forwardmsg(netisr_cpuport(nextcpu), lmsg);
	else
		lwkt_replymsg(lmsg, error);
}

/* This must be before ip6_init2(), which is now SI_ORDER_MIDDLE */
SYSINIT(route, SI_SUB_PROTO_DOMAIN, SI_ORDER_THIRD, route_init, 0);

struct rtchange_arg {
	struct ifaddr	*old_ifa;
	struct ifaddr	*new_ifa;
	struct rtentry	*rt;
	int		changed;
};

static void
rtchange_ifa(struct rtentry *rt, struct rtchange_arg *ap)
{
	if (rt->rt_ifa->ifa_rtrequest != NULL)
		rt->rt_ifa->ifa_rtrequest(RTM_DELETE, rt);
	IFAFREE(rt->rt_ifa);

	IFAREF(ap->new_ifa);
	rt->rt_ifa = ap->new_ifa;
	rt->rt_ifp = ap->new_ifa->ifa_ifp;
	if (rt->rt_ifa->ifa_rtrequest != NULL)
		rt->rt_ifa->ifa_rtrequest(RTM_ADD, rt);

	ap->changed = 1;
}

static int
rtchange_callback(struct radix_node *rn, void *xap)
{
	struct rtchange_arg *ap = xap;
	struct rtentry *rt = (struct rtentry *)rn;

	if (rt->rt_ifa == ap->old_ifa) {
		if (rt->rt_flags & (RTF_CLONING | RTF_PRCLONING)) {
			/*
			 * We could saw the branch off when we are
			 * still sitting on it, if the ifa_rtrequest
			 * DEL/ADD are called directly from here.
			 */
			ap->rt = rt;
			return EJUSTRETURN;
		}
		rtchange_ifa(rt, ap);
	}
	return 0;
}

struct netmsg_rtchange {
	struct netmsg_base	base;
	struct ifaddr		*old_ifa;
	struct ifaddr		*new_ifa;
	int			changed;
};

static void
rtchange_dispatch(netmsg_t msg)
{
	struct netmsg_rtchange *rmsg = (void *)msg;
	struct radix_node_head *rnh;
	struct rtchange_arg arg;
	int nextcpu, cpu;

	cpu = mycpuid;

	memset(&arg, 0, sizeof(arg));
	arg.old_ifa = rmsg->old_ifa;
	arg.new_ifa = rmsg->new_ifa;

	rnh = rt_tables[cpu][AF_INET];
	for (;;) {
		int error;

		KKASSERT(arg.rt == NULL);
		error = rnh->rnh_walktree(rnh, rtchange_callback, &arg);
		if (arg.rt != NULL) {
			struct rtentry *rt;

			rt = arg.rt;
			arg.rt = NULL;
			rtchange_ifa(rt, &arg);
		} else {
			break;
		}
	}
	if (arg.changed)
		rmsg->changed = 1;

	nextcpu = cpu + 1;
	if (nextcpu < ncpus)
		lwkt_forwardmsg(netisr_cpuport(nextcpu), &rmsg->base.lmsg);
	else
		lwkt_replymsg(&rmsg->base.lmsg, 0);
}

int
rtchange(struct ifaddr *old_ifa, struct ifaddr *new_ifa)
{
	struct netmsg_rtchange msg;

	/*
	 * XXX individual requests are not independantly chained,
	 * which means that the per-cpu route tables will not be
	 * consistent in the middle of the operation.  If routes
	 * related to the interface are manipulated while we are
	 * doing this the inconsistancy could trigger a panic.
	 */
	netmsg_init(&msg.base, NULL, &curthread->td_msgport, MSGF_PRIORITY,
	    rtchange_dispatch);
	msg.old_ifa = old_ifa;
	msg.new_ifa = new_ifa;
	msg.changed = 0;
	rt_domsg_global(&msg.base);

	if (msg.changed) {
		old_ifa->ifa_flags &= ~IFA_ROUTE;
		new_ifa->ifa_flags |= IFA_ROUTE;
		return 0;
	} else {
		return ENOENT;
	}
}

int
rt_domsg_global(struct netmsg_base *nmsg)
{
	ASSERT_CANDOMSG_NETISR0(curthread);
	return lwkt_domsg(netisr_cpuport(0), &nmsg->lmsg, 0);
}
