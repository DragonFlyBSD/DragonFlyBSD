/*	$FreeBSD: src/sys/netinet6/in6_rmx.c,v 1.1.2.4 2004/10/06 02:35:17 suz Exp $	*/
/*	$DragonFly: src/sys/netinet6/in6_rmx.c,v 1.15 2006/12/22 23:57:53 swildner Exp $	*/
/*	$KAME: in6_rmx.c,v 1.11 2001/07/26 06:53:16 jinmei Exp $	*/

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
 *     indefinitely.  See in6_rtqtimo() below for the exact mechanism.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/sysctl.h>
#include <sys/queue.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/mbuf.h>
#include <sys/syslog.h>
#include <sys/globaldata.h>
#include <sys/thread2.h>

#include <net/if.h>
#include <net/route.h>
#include <net/netisr2.h>
#include <net/netmsg2.h>
#include <netinet/in.h>
#include <netinet/ip_var.h>
#include <netinet/in_var.h>

#include <netinet/ip6.h>
#include <netinet6/ip6_var.h>

#include <netinet/icmp6.h>
#include <netinet6/nd6.h>

#include <netinet/tcp.h>
#include <netinet/tcp_seq.h>
#include <netinet/tcp_timer.h>
#include <netinet/tcp_var.h>

struct in6_rttimo_ctx {
	struct callout		timo_ch;
	struct netmsg_base	timo_nmsg;
	struct radix_node_head	*timo_rnh;
} __cachealign;

static struct in6_rttimo_ctx	in6_rtqtimo_ctx[MAXCPU];
static struct in6_rttimo_ctx	in6_mtutimo_ctx[MAXCPU];

extern int	in6_inithead (void **head, int off);

#define RTPRF_OURS		RTF_PROTO3	/* set on routes we manage */

/*
 * Do what we need to do when inserting a route.
 */
static struct radix_node *
in6_addroute(const void *key, const void *mask, struct radix_node_head *head,
	     struct radix_node *nodes)
{
	struct rtentry *rt = (struct rtentry *)nodes;
	struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *)rt_key(rt);
	struct radix_node *ret;

	/*
	 * For IPv6, all unicast non-host routes are automatically cloning.
	 */
	if (IN6_IS_ADDR_MULTICAST(&sin6->sin6_addr))
		rt->rt_flags |= RTF_MULTICAST;

	if (!(rt->rt_flags & (RTF_HOST | RTF_CLONING | RTF_MULTICAST))) {
		rt->rt_flags |= RTF_PRCLONING;
	}

	/*
	 * A little bit of help for both IPv6 output and input:
	 *   For local addresses, we make sure that RTF_LOCAL is set,
	 *   with the thought that this might one day be used to speed up
	 *   ip_input().
	 *
	 * We also mark routes to multicast addresses as such, because
	 * it's easy to do and might be useful (but this is much more
	 * dubious since it's so easy to inspect the address).  (This
	 * is done above.)
	 *
	 * XXX
	 * should elaborate the code.
	 */
	if (rt->rt_flags & RTF_HOST) {
		if (IN6_ARE_ADDR_EQUAL(&satosin6(rt->rt_ifa->ifa_addr)
					->sin6_addr,
				       &sin6->sin6_addr)) {
			rt->rt_flags |= RTF_LOCAL;
		}
	}

	if (!rt->rt_rmx.rmx_mtu && !(rt->rt_rmx.rmx_locks & RTV_MTU) &&
	    rt->rt_ifp != NULL)
		rt->rt_rmx.rmx_mtu = IN6_LINKMTU(rt->rt_ifp);

	ret = rn_addroute(key, mask, head, nodes);
	if (ret == NULL && rt->rt_flags & RTF_HOST) {
		struct rtentry *rt2;

		/*
		 * We are trying to add a host route, but can't.
		 * Find out if it is because of an
		 * ARP entry and delete it if so.
		 */
		rt2 = rtpurelookup((struct sockaddr *)sin6);
		if (rt2 != NULL) {
			--rt2->rt_refcnt;
			if (rt2->rt_flags & RTF_LLINFO &&
			    rt2->rt_flags & RTF_HOST &&
			    rt2->rt_gateway &&
			    rt2->rt_gateway->sa_family == AF_LINK) {
				rtrequest(RTM_DELETE, rt_key(rt2),
					  rt2->rt_gateway, rt_mask(rt2),
					  rt2->rt_flags, NULL);
				ret = rn_addroute(key, mask, head, nodes);
			}
		}
	} else if (ret == NULL && rt->rt_flags & RTF_CLONING) {
		struct rtentry *rt2;

		/*
		 * We are trying to add a net route, but can't.
		 * The following case should be allowed, so we'll make a
		 * special check for this:
		 *	Two IPv6 addresses with the same prefix is assigned
		 *	to a single interrface.
		 *	# ifconfig if0 inet6 3ffe:0501::1 prefix 64 alias (*1)
		 *	# ifconfig if0 inet6 3ffe:0501::2 prefix 64 alias (*2)
		 *	In this case, (*1) and (*2) want to add the same
		 *	net route entry, 3ffe:0501:: -> if0.
		 *	This case should not raise an error.
		 */
		rt2 = rtpurelookup((struct sockaddr *)sin6);
		if (rt2 != NULL) {
			if ((rt2->rt_flags & (RTF_CLONING|RTF_HOST|RTF_GATEWAY))
					== RTF_CLONING &&
			    rt2->rt_gateway &&
			    rt2->rt_gateway->sa_family == AF_LINK &&
			    rt2->rt_ifp == rt->rt_ifp) {
				ret = rt2->rt_nodes;
			}
			--rt2->rt_refcnt;
		}
	}
	return ret;
}

/*
 * This code is the inverse of in6_clsroute: on first reference, if we
 * were managing the route, stop doing so and set the expiration timer
 * back off again.
 */
static struct radix_node *
in6_matchroute(const void *key, struct radix_node_head *head)
{
	struct radix_node *rn = rn_match(key, head);
	struct rtentry *rt = (struct rtentry *)rn;

	if (rt != NULL && rt->rt_refcnt == 0) { /* this is first reference */
		if (rt->rt_flags & RTPRF_OURS) {
			rt->rt_flags &= ~RTPRF_OURS;
			rt->rt_rmx.rmx_expire = 0;
		}
	}
	return rn;
}

SYSCTL_DECL(_net_inet6_ip6);

static int rtq_reallyold = 60*60;
	/* one hour is ``really old'' */
SYSCTL_INT(_net_inet6_ip6, IPV6CTL_RTEXPIRE, rtexpire,
    CTLFLAG_RW, &rtq_reallyold , 0, "Default expiration time on cloned routes");

static int rtq_minreallyold = 10;
	/* never automatically crank down to less */
SYSCTL_INT(_net_inet6_ip6, IPV6CTL_RTMINEXPIRE, rtminexpire, CTLFLAG_RW,
    &rtq_minreallyold , 0, "Minimum time to attempt to hold onto cloned routes");

static int rtq_toomany = 128;
	/* 128 cached routes is ``too many'' */
SYSCTL_INT(_net_inet6_ip6, IPV6CTL_RTMAXCACHE, rtmaxcache,
    CTLFLAG_RW, &rtq_toomany , 0, "Upper limit on cloned routes");


/*
 * On last reference drop, mark the route as belong to us so that it can be
 * timed out.
 */
static void
in6_clsroute(struct radix_node *rn, struct radix_node_head *head)
{
	struct rtentry *rt = (struct rtentry *)rn;

	if (!(rt->rt_flags & RTF_UP))
		return;		/* prophylactic measures */

	if ((rt->rt_flags & (RTF_LLINFO | RTF_HOST)) != RTF_HOST)
		return;

	if ((rt->rt_flags & (RTF_WASCLONED | RTPRF_OURS)) != RTF_WASCLONED)
		return;

	/*
	 * As requested by David Greenman:
	 * If rtq_reallyold is 0, just delete the route without
	 * waiting for a timeout cycle to kill it.
	 */
	if (rtq_reallyold != 0) {
		rt->rt_flags |= RTPRF_OURS;
		rt->rt_rmx.rmx_expire = time_uptime + rtq_reallyold;
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
	int mode;
	int updating;
	int draining;
	int killed;
	int found;
	time_t nextstop;
};

/*
 * Get rid of old routes.  When draining, this deletes everything, even when
 * the timeout is not expired yet.  When updating, this makes sure that
 * nothing has a timeout longer than the current value of rtq_reallyold.
 */
static int
in6_rtqkill(struct radix_node *rn, void *rock)
{
	struct rtqk_arg *ap = rock;
	struct rtentry *rt = (struct rtentry *)rn;
	int err;

	if (rt->rt_flags & RTPRF_OURS) {
		ap->found++;

		if (ap->draining || rt->rt_rmx.rmx_expire <= time_uptime) {
			if (rt->rt_refcnt > 0)
				panic("rtqkill route really not free");

			err = rtrequest(RTM_DELETE, rt_key(rt), rt->rt_gateway,
					rt_mask(rt), rt->rt_flags, NULL);
			if (err)
				log(LOG_WARNING, "in6_rtqkill: error %d", err);
			else
				ap->killed++;
		} else {
			if (ap->updating &&
			    (rt->rt_rmx.rmx_expire - time_uptime >
			     rtq_reallyold)) {
				rt->rt_rmx.rmx_expire =
				    time_uptime + rtq_reallyold;
			}
			ap->nextstop = lmin(ap->nextstop,
					    rt->rt_rmx.rmx_expire);
		}
	}

	return 0;
}

#define RTQ_TIMEOUT	60*10	/* run no less than once every ten minutes */
static int rtq_timeout = RTQ_TIMEOUT;

static void
in6_rtqtimo(void *arg __unused)
{
	int cpuid = mycpuid;
	struct lwkt_msg *lmsg = &in6_rtqtimo_ctx[cpuid].timo_nmsg.lmsg;

	crit_enter();
	if (lmsg->ms_flags & MSGF_DONE)
		lwkt_sendmsg_oncpu(netisr_cpuport(cpuid), lmsg);
	crit_exit();
}

static void
in6_rtqtimo_dispatch(netmsg_t nmsg)
{
	struct in6_rttimo_ctx *ctx = &in6_rtqtimo_ctx[mycpuid];
	struct radix_node_head *rnh = ctx->timo_rnh;
	struct rtqk_arg arg;
	struct timeval atv;
	static time_t last_adjusted_timeout = 0;

	ASSERT_NETISR_NCPUS(mycpuid);

	/* Reply ASAP */
	crit_enter();
	lwkt_replymsg(&nmsg->lmsg, 0);
	crit_exit();

	arg.found = arg.killed = 0;
	arg.rnh = rnh;
	arg.nextstop = time_uptime + rtq_timeout;
	arg.draining = arg.updating = 0;
	rnh->rnh_walktree(rnh, in6_rtqkill, &arg);

	/*
	 * Attempt to be somewhat dynamic about this:
	 * If there are ``too many'' routes sitting around taking up space,
	 * then crank down the timeout, and see if we can't make some more
	 * go away.  However, we make sure that we will never adjust more
	 * than once in rtq_timeout seconds, to keep from cranking down too
	 * hard.
	 */
	if ((arg.found - arg.killed > rtq_toomany)
	   && (int)(time_uptime - last_adjusted_timeout) >= rtq_timeout
	   && rtq_reallyold > rtq_minreallyold) {
		rtq_reallyold = 2*rtq_reallyold / 3;
		if (rtq_reallyold < rtq_minreallyold) {
			rtq_reallyold = rtq_minreallyold;
		}

		last_adjusted_timeout = time_uptime;
#ifdef DIAGNOSTIC
		log(LOG_DEBUG, "in6_rtqtimo: adjusted rtq_reallyold to %d",
		    rtq_reallyold);
#endif
		arg.found = arg.killed = 0;
		arg.updating = 1;
		rnh->rnh_walktree(rnh, in6_rtqkill, &arg);
	}

	atv.tv_usec = 0;
	atv.tv_sec = arg.nextstop - time_uptime;
	if ((int)atv.tv_sec < 1) {		/* time shift safety */
		atv.tv_sec = 1;
		arg.nextstop = time_uptime + atv.tv_sec;
	}
	if ((int)atv.tv_sec > rtq_timeout) {	/* time shift safety */
		atv.tv_sec = rtq_timeout;
		arg.nextstop = time_uptime + atv.tv_sec;
	}
	callout_reset(&ctx->timo_ch, tvtohz_high(&atv), in6_rtqtimo, NULL);
}

/*
 * Age old PMTUs.
 */
struct mtuex_arg {
	struct radix_node_head *rnh;
	time_t nextstop;
};

static int
in6_mtuexpire(struct radix_node *rn, void *rock)
{
	struct rtentry *rt = (struct rtentry *)rn;
	struct mtuex_arg *ap = rock;

	/* sanity */
	if (!rt)
		panic("rt == NULL in in6_mtuexpire");

	if (rt->rt_rmx.rmx_expire && !(rt->rt_flags & RTF_PROBEMTU)) {
		if (rt->rt_rmx.rmx_expire <= time_uptime) {
			rt->rt_flags |= RTF_PROBEMTU;
		} else {
			ap->nextstop = lmin(ap->nextstop,
					rt->rt_rmx.rmx_expire);
		}
	}

	return 0;
}

#define	MTUTIMO_DEFAULT	(60*1)

static void
in6_mtutimo(void *arg __unused)
{
	int cpuid = mycpuid;
	struct lwkt_msg *lmsg = &in6_mtutimo_ctx[cpuid].timo_nmsg.lmsg;

	crit_enter();
	if (lmsg->ms_flags & MSGF_DONE)
		lwkt_sendmsg_oncpu(netisr_cpuport(cpuid), lmsg);
	crit_exit();
}

static void
in6_mtutimo_dispatch(netmsg_t nmsg)
{
	struct in6_rttimo_ctx *ctx = &in6_mtutimo_ctx[mycpuid];
	struct radix_node_head *rnh = ctx->timo_rnh;
	struct mtuex_arg arg;
	struct timeval atv;

	ASSERT_NETISR_NCPUS(mycpuid);

	/* Reply ASAP */
	crit_enter();
	lwkt_replymsg(&nmsg->lmsg, 0);
	crit_exit();

	arg.rnh = rnh;
	arg.nextstop = time_uptime + MTUTIMO_DEFAULT;
	rnh->rnh_walktree(rnh, in6_mtuexpire, &arg);

	atv.tv_usec = 0;
	atv.tv_sec = arg.nextstop - time_uptime;
	if ((int)atv.tv_sec < 1) {		/* time shift safety */
		atv.tv_sec = 1;
		arg.nextstop = time_uptime + atv.tv_sec;
	}
	if ((int)atv.tv_sec > rtq_timeout) {	/* time shift safety */
		atv.tv_sec = rtq_timeout;
		arg.nextstop = time_uptime + atv.tv_sec;
	}
	callout_reset(&ctx->timo_ch, tvtohz_high(&atv), in6_mtutimo, NULL);
}

#if 0
void
in6_rtqdrain(void)
{
	struct radix_node_head *rnh = rt_tables[mycpuid][AF_INET6];
	struct rtqk_arg arg;

	arg.found = arg.killed = 0;
	arg.rnh = rnh;
	arg.nextstop = 0;
	arg.draining = 1;
	arg.updating = 0;
	crit_enter();
	rnh->rnh_walktree(rnh, in6_rtqkill, &arg);
	crit_exit();
}
#endif

/*
 * Initialize our routing tree.
 */
int
in6_inithead(void **head, int off)
{
	struct radix_node_head *rnh;
	struct in6_rttimo_ctx *ctx;
	int cpuid = mycpuid;

	rnh = *head;
	KKASSERT(rnh == rt_tables[cpuid][AF_INET6]);

	if (!rn_inithead(&rnh, rn_cpumaskhead(cpuid), off))
		return 0;

	*head = rnh;
	rnh->rnh_addaddr = in6_addroute;
	rnh->rnh_matchaddr = in6_matchroute;
	rnh->rnh_close = in6_clsroute;

	ctx = &in6_rtqtimo_ctx[cpuid];
	ctx->timo_rnh = rnh;
	callout_init_mp(&ctx->timo_ch);
	netmsg_init(&ctx->timo_nmsg, NULL, &netisr_adone_rport, MSGF_PRIORITY,
	    in6_rtqtimo_dispatch);

	ctx = &in6_mtutimo_ctx[cpuid];
	ctx->timo_rnh = rnh;
	callout_init_mp(&ctx->timo_ch);
	netmsg_init(&ctx->timo_nmsg, NULL, &netisr_adone_rport, MSGF_PRIORITY,
	    in6_mtutimo_dispatch);

	in6_rtqtimo(NULL);	/* kick off timeout first time */
	in6_mtutimo(NULL);	/* kick off timeout first time */

	return 1;
}
