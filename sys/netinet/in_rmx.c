/*
 * Copyright 1994, 1995 Massachusetts Institute of Technology
 *
 * Permission to use, copy, modify, and distribute this software and
 * its documentation for any purpose and without fee is hereby
 * granted, provided that both the above copyright notice and this
 * permission notice appear in all copies, that both the above
 * copyright notice and this permission notice appear in all
 * supporting documentation, and that the name of M.I.T. not be used
 * in advertising or publicity pertaining to distribution of the
 * software without specific, written prior permission.  M.I.T. makes
 * no representations about the suitability of this software for any
 * purpose.  It is provided "as is" without express or implied
 * warranty.
 *
 * THIS SOFTWARE IS PROVIDED BY M.I.T. ``AS IS''.  M.I.T. DISCLAIMS
 * ALL EXPRESS OR IMPLIED WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE. IN NO EVENT
 * SHALL M.I.T. BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF
 * USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD: src/sys/netinet/in_rmx.c,v 1.37.2.3 2002/08/09 14:49:23 ru Exp $
 * $DragonFly: src/sys/netinet/in_rmx.c,v 1.14 2006/04/11 06:59:34 dillon Exp $
 */

/*
 * This code does two things necessary for the enhanced TCP metrics to
 * function in a useful manner:
 *  1) It marks all non-host routes as `cloning', thus ensuring that
 *     every actual reference to such a route actually gets turned
 *     into a reference to a host route to the specific destination
 *     requested.
 *  2) When such routes lose all their references, it arranges for them
 *     to be deleted in some random collection of circumstances, so that
 *     a large quantity of stale routing data is not kept in kernel memory
 *     indefinitely.  See in_rtqtimo() below for the exact mechanism.
 */

#include "opt_carp.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/sysctl.h>
#include <sys/socket.h>
#include <sys/mbuf.h>
#include <sys/syslog.h>
#include <sys/globaldata.h>
#include <sys/thread2.h>

#include <net/if.h>
#include <net/route.h>
#include <net/if_var.h>
#ifdef CARP
#include <net/if_types.h>
#endif
#include <net/netmsg2.h>
#include <net/netisr2.h>
#include <netinet/in.h>
#include <netinet/in_var.h>
#include <netinet/ip_var.h>
#include <netinet/ip_flow.h>

#define RTPRF_EXPIRING	RTF_PROTO3	/* set on routes we manage */

struct in_rtqtimo_ctx {
	struct callout		timo_ch;
	struct netmsg_base	timo_nmsg;
	struct radix_node_head	*timo_rnh;
} __cachealign;

static void	in_rtqtimo(void *);

static struct in_rtqtimo_ctx in_rtqtimo_context[MAXCPU];

/*
 * Do what we need to do when inserting a route.
 */
static struct radix_node *
in_addroute(char *key, char *mask, struct radix_node_head *head,
	    struct radix_node *treenodes)
{
	struct rtentry *rt = (struct rtentry *)treenodes;
	struct sockaddr_in *sin = (struct sockaddr_in *)rt_key(rt);
	struct radix_node *ret;
	struct in_ifaddr_container *iac;
	struct in_ifaddr *ia;

	/*
	 * For IP, mark routes to multicast addresses as such, because
	 * it's easy to do and might be useful (but this is much more
	 * dubious since it's so easy to inspect the address).
	 *
	 * For IP, all unicast non-host routes are automatically cloning.
	 */
	if (IN_MULTICAST(ntohl(sin->sin_addr.s_addr)))
		rt->rt_flags |= RTF_MULTICAST;

	if (!(rt->rt_flags & (RTF_HOST | RTF_CLONING | RTF_MULTICAST)))
		rt->rt_flags |= RTF_PRCLONING;

	/*
	 *   For host routes, we make sure that RTF_BROADCAST
	 *   is set for anything that looks like a broadcast address.
	 *   This way, we can avoid an expensive call to in_broadcast()
	 *   in ip_output() most of the time (because the route passed
	 *   to ip_output() is almost always a host route).
	 *
	 *   For local routes we set RTF_LOCAL allowing various shortcuts.
	 *
	 *   A cloned network route will point to one of several possible
	 *   addresses if an interface has aliases and must be repointed
	 *   back to the correct address or arp_rtrequest() will not properly
	 *   detect the local ip.
	 */
	if (rt->rt_flags & RTF_HOST) {
		if (in_broadcast(sin->sin_addr, rt->rt_ifp)) {
			rt->rt_flags |= RTF_BROADCAST;
		} else if (satosin(rt->rt_ifa->ifa_addr)->sin_addr.s_addr ==
			   sin->sin_addr.s_addr) {
			rt->rt_flags |= RTF_LOCAL;
		} else {
			LIST_FOREACH(iac, INADDR_HASH(sin->sin_addr.s_addr),
				     ia_hash) {
				ia = iac->ia;
				if (sin->sin_addr.s_addr ==
				    ia->ia_addr.sin_addr.s_addr) {
					rt->rt_flags |= RTF_LOCAL;
					IFAREF(&ia->ia_ifa);
					IFAFREE(rt->rt_ifa);
					rt->rt_ifa = &ia->ia_ifa;
					rt->rt_ifp = rt->rt_ifa->ifa_ifp;
					break;
				}
			}
		}
	}

	if (rt->rt_rmx.rmx_mtu != 0 && !(rt->rt_rmx.rmx_locks & RTV_MTU) &&
	    rt->rt_ifp != NULL)
		rt->rt_rmx.rmx_mtu = rt->rt_ifp->if_mtu;

	ret = rn_addroute(key, mask, head, treenodes);
	if (ret == NULL && (rt->rt_flags & RTF_HOST)) {
		struct rtentry *oldrt;

		/*
		 * We are trying to add a host route, but can't.
		 * Find out if it is because of an ARP entry and
		 * delete it if so.
		 */
		oldrt = rtpurelookup((struct sockaddr *)sin);
		if (oldrt != NULL) {
			--oldrt->rt_refcnt;
			if ((oldrt->rt_flags & RTF_LLINFO) &&
			    (oldrt->rt_flags & RTF_HOST) &&
			    oldrt->rt_gateway &&
			    oldrt->rt_gateway->sa_family == AF_LINK) {
				rtrequest(RTM_DELETE, rt_key(oldrt),
					  oldrt->rt_gateway, rt_mask(oldrt),
					  oldrt->rt_flags, NULL);
				ret = rn_addroute(key, mask, head, treenodes);
			}
		}
	}

	/*
	 * If the new route has been created successfully, and it is
	 * not a multicast/broadcast or cloned route, then we will
	 * have to flush the ipflow.  Otherwise, we may end up using
	 * the wrong route.
	 */
	if (ret != NULL &&
	    (rt->rt_flags &
	     (RTF_MULTICAST | RTF_BROADCAST | RTF_WASCLONED)) == 0) {
		ipflow_flush_oncpu();
	}
	return ret;
}

/*
 * This code is the inverse of in_closeroute: on first reference, if we
 * were managing the route, stop doing so and set the expiration timer
 * back off again.
 */
static struct radix_node *
in_matchroute(char *key, struct radix_node_head *head)
{
	struct radix_node *rn = rn_match(key, head);
	struct rtentry *rt = (struct rtentry *)rn;

	if (rt != NULL && rt->rt_refcnt == 0) { /* this is first reference */
		if (rt->rt_flags & RTPRF_EXPIRING) {
			rt->rt_flags &= ~RTPRF_EXPIRING;
			rt->rt_rmx.rmx_expire = 0;
		}
	}
	return rn;
}

static int rtq_reallyold = 60*60;  /* one hour is ``really old'' */
SYSCTL_INT(_net_inet_ip, IPCTL_RTEXPIRE, rtexpire, CTLFLAG_RW,
    &rtq_reallyold , 0,
    "Default expiration time on cloned routes");

static int rtq_minreallyold = 10;  /* never automatically crank down to less */
SYSCTL_INT(_net_inet_ip, IPCTL_RTMINEXPIRE, rtminexpire, CTLFLAG_RW,
    &rtq_minreallyold , 0,
    "Minimum time to attempt to hold onto cloned routes");

static int rtq_toomany = 128;	   /* 128 cached routes is ``too many'' */
SYSCTL_INT(_net_inet_ip, IPCTL_RTMAXCACHE, rtmaxcache, CTLFLAG_RW,
    &rtq_toomany , 0, "Upper limit on cloned routes");

/*
 * On last reference drop, mark the route as belong to us so that it can be
 * timed out.
 */
static void
in_closeroute(struct radix_node *rn, struct radix_node_head *head)
{
	struct rtentry *rt = (struct rtentry *)rn;

	if (!(rt->rt_flags & RTF_UP))
		return;		/* prophylactic measures */

	if ((rt->rt_flags & (RTF_LLINFO | RTF_HOST)) != RTF_HOST)
		return;

	if ((rt->rt_flags & (RTF_WASCLONED | RTPRF_EXPIRING)) != RTF_WASCLONED)
		return;

	/*
	 * As requested by David Greenman:
	 * If rtq_reallyold is 0, just delete the route without
	 * waiting for a timeout cycle to kill it.
	 */
	if (rtq_reallyold != 0) {
		rt->rt_flags |= RTPRF_EXPIRING;
		rt->rt_rmx.rmx_expire = time_second + rtq_reallyold;
	} else {
		/*
		 * Remove route from the radix tree, but defer deallocation
		 * until we return to rtfree().
		 */
		rtrequest(RTM_DELETE, rt_key(rt), rt->rt_gateway, rt_mask(rt),
			  rt->rt_flags, &rt);
	}
}

struct rtqk_arg {
	struct radix_node_head *rnh;
	int draining;
	int killed;
	int found;
	int updating;
	time_t nextstop;
};

/*
 * Get rid of old routes.  When draining, this deletes everything, even when
 * the timeout is not expired yet.  When updating, this makes sure that
 * nothing has a timeout longer than the current value of rtq_reallyold.
 */
static int
in_rtqkill(struct radix_node *rn, void *rock)
{
	struct rtqk_arg *ap = rock;
	struct rtentry *rt = (struct rtentry *)rn;
	int err;

	if (rt->rt_flags & RTPRF_EXPIRING) {
		ap->found++;
		if (ap->draining || rt->rt_rmx.rmx_expire <= time_second) {
			if (rt->rt_refcnt > 0)
				panic("rtqkill route really not free");

			err = rtrequest(RTM_DELETE, rt_key(rt), rt->rt_gateway,
					rt_mask(rt), rt->rt_flags, NULL);
			if (err)
				log(LOG_WARNING, "in_rtqkill: error %d\n", err);
			else
				ap->killed++;
		} else {
			if (ap->updating &&
			    (rt->rt_rmx.rmx_expire - time_second >
			     rtq_reallyold)) {
				rt->rt_rmx.rmx_expire = time_second +
				    rtq_reallyold;
			}
			ap->nextstop = lmin(ap->nextstop,
					    rt->rt_rmx.rmx_expire);
		}
	}

	return 0;
}

#define RTQ_TIMEOUT	60*10	/* run no less than once every ten minutes */
static int rtq_timeout = RTQ_TIMEOUT;

/*
 * NOTE:
 * 'last_adjusted_timeout' and 'rtq_reallyold' are _not_ read-only, and
 * could be changed by all CPUs.  However, they are changed at so low
 * frequency that we could ignore the cache trashing issue and take them
 * as read-mostly.
 */
static void
in_rtqtimo_dispatch(netmsg_t nmsg)
{
	struct rtqk_arg arg;
	struct timeval atv;
	static time_t last_adjusted_timeout = 0;
	struct in_rtqtimo_ctx *ctx = &in_rtqtimo_context[mycpuid];
	struct radix_node_head *rnh = ctx->timo_rnh;

	/* Reply ASAP */
	crit_enter();
	lwkt_replymsg(&nmsg->lmsg, 0);
	crit_exit();

	arg.found = arg.killed = 0;
	arg.rnh = rnh;
	arg.nextstop = time_second + rtq_timeout;
	arg.draining = arg.updating = 0;
	rnh->rnh_walktree(rnh, in_rtqkill, &arg);

	/*
	 * Attempt to be somewhat dynamic about this:
	 * If there are ``too many'' routes sitting around taking up space,
	 * then crank down the timeout, and see if we can't make some more
	 * go away.  However, we make sure that we will never adjust more
	 * than once in rtq_timeout seconds, to keep from cranking down too
	 * hard.
	 */
	if ((arg.found - arg.killed > rtq_toomany) &&
	    (time_second - last_adjusted_timeout >= rtq_timeout) &&
	    rtq_reallyold > rtq_minreallyold) {
		rtq_reallyold = 2*rtq_reallyold / 3;
		if (rtq_reallyold < rtq_minreallyold) {
			rtq_reallyold = rtq_minreallyold;
		}

		last_adjusted_timeout = time_second;
#ifdef DIAGNOSTIC
		log(LOG_DEBUG, "in_rtqtimo: adjusted rtq_reallyold to %d\n",
		    rtq_reallyold);
#endif
		arg.found = arg.killed = 0;
		arg.updating = 1;
		rnh->rnh_walktree(rnh, in_rtqkill, &arg);
	}

	atv.tv_usec = 0;
	atv.tv_sec = arg.nextstop - time_second;
	callout_reset(&ctx->timo_ch, tvtohz_high(&atv), in_rtqtimo, NULL);
}

static void
in_rtqtimo(void *arg __unused)
{
	int cpuid = mycpuid;
	struct lwkt_msg *lmsg = &in_rtqtimo_context[cpuid].timo_nmsg.lmsg;

	crit_enter();
	if (lmsg->ms_flags & MSGF_DONE)
		lwkt_sendmsg(netisr_cpuport(cpuid), lmsg);
	crit_exit();
}

void
in_rtqdrain(void)
{
	struct radix_node_head *rnh = rt_tables[mycpuid][AF_INET];
	struct rtqk_arg arg;

	arg.found = arg.killed = 0;
	arg.rnh = rnh;
	arg.nextstop = 0;
	arg.draining = 1;
	arg.updating = 0;
	crit_enter();
	rnh->rnh_walktree(rnh, in_rtqkill, &arg);
	crit_exit();
}

/*
 * Initialize our routing tree.
 */
int
in_inithead(void **head, int off)
{
	struct radix_node_head *rnh;
	struct in_rtqtimo_ctx *ctx;
	int cpuid = mycpuid;

	if (!rn_inithead(head, rn_cpumaskhead(cpuid), off))
		return 0;

	if (head != (void **)&rt_tables[cpuid][AF_INET]) /* BOGUS! */
		return 1;	/* only do this for the real routing table */

	rnh = *head;
	rnh->rnh_addaddr = in_addroute;
	rnh->rnh_matchaddr = in_matchroute;
	rnh->rnh_close = in_closeroute;

	ctx = &in_rtqtimo_context[cpuid];
	ctx->timo_rnh = rnh;
	callout_init_mp(&ctx->timo_ch);
	netmsg_init(&ctx->timo_nmsg, NULL, &netisr_adone_rport, 0,
	    in_rtqtimo_dispatch);

	in_rtqtimo(NULL);	/* kick off timeout first time */
	return 1;
}

/*
 * This zaps old routes when the interface goes down or interface
 * address is deleted.  In the latter case, it deletes static routes
 * that point to this address.  If we don't do this, we may end up
 * using the old address in the future.  The ones we always want to
 * get rid of are things like ARP entries, since the user might down
 * the interface, walk over to a completely different network, and
 * plug back in.
 *
 * in_ifadown() is typically called when an interface is being brought
 * down.  We must iterate through all per-cpu route tables and clean
 * them up.
 */
struct in_ifadown_arg {
	struct radix_node_head *rnh;
	struct ifaddr *ifa;
	int del;
};

static int
in_ifadownkill(struct radix_node *rn, void *xap)
{
	struct in_ifadown_arg *ap = xap;
	struct rtentry *rt = (struct rtentry *)rn;
	int err;

	if (rt->rt_ifa == ap->ifa &&
	    (ap->del || !(rt->rt_flags & RTF_STATIC))) {
		/*
		 * We need to disable the automatic prune that happens
		 * in this case in rtrequest() because it will blow
		 * away the pointers that rn_walktree() needs in order
		 * continue our descent.  We will end up deleting all
		 * the routes that rtrequest() would have in any case,
		 * so that behavior is not needed there.
		 */
		rt->rt_flags &= ~(RTF_CLONING | RTF_PRCLONING);
		err = rtrequest(RTM_DELETE, rt_key(rt), rt->rt_gateway,
				rt_mask(rt), rt->rt_flags, NULL);
		if (err)
			log(LOG_WARNING, "in_ifadownkill: error %d\n", err);
	}
	return 0;
}

struct netmsg_ifadown {
	struct netmsg_base	base;
	struct ifaddr		*ifa;
	int			del;
};

static void
in_ifadown_dispatch(netmsg_t msg)
{
	struct netmsg_ifadown *rmsg = (void *)msg;
	struct radix_node_head *rnh;
	struct ifaddr *ifa = rmsg->ifa;
	struct in_ifadown_arg arg;
	int nextcpu, cpu;

	cpu = mycpuid;

	arg.rnh = rnh = rt_tables[cpu][AF_INET];
	arg.ifa = ifa;
	arg.del = rmsg->del;
	rnh->rnh_walktree(rnh, in_ifadownkill, &arg);
	ifa->ifa_flags &= ~IFA_ROUTE;

	nextcpu = cpu + 1;
	if (nextcpu < ncpus)
		lwkt_forwardmsg(netisr_cpuport(nextcpu), &rmsg->base.lmsg);
	else
		lwkt_replymsg(&rmsg->base.lmsg, 0);
}

int
in_ifadown_force(struct ifaddr *ifa, int delete)
{
	struct netmsg_ifadown msg;

	if (ifa->ifa_addr->sa_family != AF_INET)
		return 1;

	/*
	 * XXX individual requests are not independantly chained,
	 * which means that the per-cpu route tables will not be
	 * consistent in the middle of the operation.  If routes
	 * related to the interface are manipulated while we are
	 * doing this the inconsistancy could trigger a panic.
	 */
	netmsg_init(&msg.base, NULL, &curthread->td_msgport, MSGF_PRIORITY,
	    in_ifadown_dispatch);
	msg.ifa = ifa;
	msg.del = delete;
	rt_domsg_global(&msg.base);

	return 0;
}

int
in_ifadown(struct ifaddr *ifa, int delete)
{
#ifdef CARP
	if (ifa->ifa_ifp->if_type == IFT_CARP)
		return 0;
#endif
	return in_ifadown_force(ifa, delete);
}
