/*
 * Copyright (c) 1980, 1986, 1993
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
 *	@(#)if.c	8.3 (Berkeley) 1/4/94
 * $FreeBSD: src/sys/net/if.c,v 1.185 2004/03/13 02:35:03 brooks Exp $
 */

#include "opt_inet6.h"
#include "opt_inet.h"
#include "opt_ifpoll.h"

#include <sys/param.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/priv.h>
#include <sys/protosw.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/socketops.h>
#include <sys/kernel.h>
#include <sys/ktr.h>
#include <sys/mutex.h>
#include <sys/lock.h>
#include <sys/sockio.h>
#include <sys/syslog.h>
#include <sys/sysctl.h>
#include <sys/domain.h>
#include <sys/thread.h>
#include <sys/serialize.h>
#include <sys/bus.h>
#include <sys/jail.h>

#include <sys/thread2.h>
#include <sys/msgport2.h>
#include <sys/mutex2.h>

#include <net/if.h>
#include <net/if_arp.h>
#include <net/if_dl.h>
#include <net/if_types.h>
#include <net/if_var.h>
#include <net/if_ringmap.h>
#include <net/ifq_var.h>
#include <net/radix.h>
#include <net/route.h>
#include <net/if_clone.h>
#include <net/netisr2.h>
#include <net/netmsg2.h>

#include <machine/atomic.h>
#include <machine/stdarg.h>
#include <machine/smp.h>

#if defined(INET) || defined(INET6)
#include <netinet/in.h>
#include <netinet/in_var.h>
#include <netinet/if_ether.h>
#ifdef INET6
#include <netinet6/in6_var.h>
#include <netinet6/in6_ifattach.h>
#endif /* INET6 */
#endif /* INET || INET6 */

struct netmsg_ifaddr {
	struct netmsg_base base;
	struct ifaddr	*ifa;
	struct ifnet	*ifp;
	int		tail;
};

struct ifsubq_stage_head {
	TAILQ_HEAD(, ifsubq_stage)	stg_head;
} __cachealign;

struct if_ringmap {
	int		rm_cnt;
	int		rm_grid;
	int		rm_cpumap[];
};

#define RINGMAP_FLAG_NONE		0x0
#define RINGMAP_FLAG_POWEROF2		0x1

/*
 * System initialization
 */
static void	if_attachdomain(void *);
static void	if_attachdomain1(struct ifnet *);
static int	ifconf(u_long, caddr_t, struct ucred *);
static void	ifinit(void *);
static void	ifnetinit(void *);
static void	if_slowtimo(void *);
static void	link_rtrequest(int, struct rtentry *);
static int	if_rtdel(struct radix_node *, void *);
static void	if_slowtimo_dispatch(netmsg_t);

/* Helper functions */
static void	ifsq_watchdog_reset(struct ifsubq_watchdog *);
static int	if_delmulti_serialized(struct ifnet *, struct sockaddr *);
static struct ifnet_array *ifnet_array_alloc(int);
static void	ifnet_array_free(struct ifnet_array *);
static struct ifnet_array *ifnet_array_add(struct ifnet *,
		    const struct ifnet_array *);
static struct ifnet_array *ifnet_array_del(struct ifnet *,
		    const struct ifnet_array *);
static struct ifg_group *if_creategroup(const char *);
static int	if_destroygroup(struct ifg_group *);
static int	if_delgroup_locked(struct ifnet *, const char *);
static int	if_getgroups(struct ifgroupreq *, struct ifnet *);
static int	if_getgroupmembers(struct ifgroupreq *);

#ifdef INET6
/*
 * XXX: declare here to avoid to include many inet6 related files..
 * should be more generalized?
 */
extern void	nd6_setmtu(struct ifnet *);
#endif

SYSCTL_NODE(_net, PF_LINK, link, CTLFLAG_RW, 0, "Link layers");
SYSCTL_NODE(_net_link, 0, generic, CTLFLAG_RW, 0, "Generic link-management");
SYSCTL_NODE(_net_link, OID_AUTO, ringmap, CTLFLAG_RW, 0, "link ringmap");

static int ifsq_stage_cntmax = 16;
TUNABLE_INT("net.link.stage_cntmax", &ifsq_stage_cntmax);
SYSCTL_INT(_net_link, OID_AUTO, stage_cntmax, CTLFLAG_RW,
    &ifsq_stage_cntmax, 0, "ifq staging packet count max");

static int if_stats_compat = 0;
SYSCTL_INT(_net_link, OID_AUTO, stats_compat, CTLFLAG_RW,
    &if_stats_compat, 0, "Compat the old ifnet stats");

static int if_ringmap_dumprdr = 0;
SYSCTL_INT(_net_link_ringmap, OID_AUTO, dump_rdr, CTLFLAG_RW,
    &if_ringmap_dumprdr, 0, "dump redirect table");

SYSINIT(interfaces, SI_SUB_PROTO_IF, SI_ORDER_FIRST, ifinit, NULL);
SYSINIT(ifnet, SI_SUB_PRE_DRIVERS, SI_ORDER_ANY, ifnetinit, NULL);

static if_com_alloc_t *if_com_alloc[256];
static if_com_free_t *if_com_free[256];

MALLOC_DEFINE(M_IFADDR, "ifaddr", "interface address");
MALLOC_DEFINE(M_IFMADDR, "ether_multi", "link-level multicast address");
MALLOC_DEFINE(M_IFNET, "ifnet", "interface structure");

int			ifqmaxlen = IFQ_MAXLEN;
struct ifnethead	ifnet = TAILQ_HEAD_INITIALIZER(ifnet);
struct ifgrouphead	ifg_head = TAILQ_HEAD_INITIALIZER(ifg_head);
static struct lock	ifgroup_lock;

static struct ifnet_array	ifnet_array0;
static struct ifnet_array	*ifnet_array = &ifnet_array0;

static struct callout		if_slowtimo_timer;
static struct netmsg_base	if_slowtimo_netmsg;

int			if_index = 0;
struct ifnet		**ifindex2ifnet = NULL;
static struct mtx	ifnet_mtx = MTX_INITIALIZER("ifnet");

static struct ifsubq_stage_head	ifsubq_stage_heads[MAXCPU];

#ifdef notyet
#define IFQ_KTR_STRING		"ifq=%p"
#define IFQ_KTR_ARGS		struct ifaltq *ifq
#ifndef KTR_IFQ
#define KTR_IFQ			KTR_ALL
#endif
KTR_INFO_MASTER(ifq);
KTR_INFO(KTR_IFQ, ifq, enqueue, 0, IFQ_KTR_STRING, IFQ_KTR_ARGS);
KTR_INFO(KTR_IFQ, ifq, dequeue, 1, IFQ_KTR_STRING, IFQ_KTR_ARGS);
#define logifq(name, arg)	KTR_LOG(ifq_ ## name, arg)

#define IF_START_KTR_STRING	"ifp=%p"
#define IF_START_KTR_ARGS	struct ifnet *ifp
#ifndef KTR_IF_START
#define KTR_IF_START		KTR_ALL
#endif
KTR_INFO_MASTER(if_start);
KTR_INFO(KTR_IF_START, if_start, run, 0,
	 IF_START_KTR_STRING, IF_START_KTR_ARGS);
KTR_INFO(KTR_IF_START, if_start, sched, 1,
	 IF_START_KTR_STRING, IF_START_KTR_ARGS);
KTR_INFO(KTR_IF_START, if_start, avoid, 2,
	 IF_START_KTR_STRING, IF_START_KTR_ARGS);
KTR_INFO(KTR_IF_START, if_start, contend_sched, 3,
	 IF_START_KTR_STRING, IF_START_KTR_ARGS);
KTR_INFO(KTR_IF_START, if_start, chase_sched, 4,
	 IF_START_KTR_STRING, IF_START_KTR_ARGS);
#define logifstart(name, arg)	KTR_LOG(if_start_ ## name, arg)
#endif /* notyet */

/*
 * Network interface utility routines.
 *
 * Routines with ifa_ifwith* names take sockaddr *'s as
 * parameters.
 */
/* ARGSUSED */
static void
ifinit(void *dummy)
{
	lockinit(&ifgroup_lock, "ifgroup", 0, 0);

	callout_init_mp(&if_slowtimo_timer);
	netmsg_init(&if_slowtimo_netmsg, NULL, &netisr_adone_rport,
	    MSGF_PRIORITY, if_slowtimo_dispatch);

	/* Start if_slowtimo */
	lwkt_sendmsg(netisr_cpuport(0), &if_slowtimo_netmsg.lmsg);
}

static void
ifsq_ifstart_ipifunc(void *arg)
{
	struct ifaltq_subque *ifsq = arg;
	struct lwkt_msg *lmsg = ifsq_get_ifstart_lmsg(ifsq, mycpuid);

	crit_enter();
	if (lmsg->ms_flags & MSGF_DONE)
		lwkt_sendmsg_oncpu(netisr_cpuport(mycpuid), lmsg);
	crit_exit();
}

static __inline void
ifsq_stage_remove(struct ifsubq_stage_head *head, struct ifsubq_stage *stage)
{
	KKASSERT(stage->stg_flags & IFSQ_STAGE_FLAG_QUED);
	TAILQ_REMOVE(&head->stg_head, stage, stg_link);
	stage->stg_flags &= ~(IFSQ_STAGE_FLAG_QUED | IFSQ_STAGE_FLAG_SCHED);
	stage->stg_cnt = 0;
	stage->stg_len = 0;
}

static __inline void
ifsq_stage_insert(struct ifsubq_stage_head *head, struct ifsubq_stage *stage)
{
	KKASSERT((stage->stg_flags &
	    (IFSQ_STAGE_FLAG_QUED | IFSQ_STAGE_FLAG_SCHED)) == 0);
	stage->stg_flags |= IFSQ_STAGE_FLAG_QUED;
	TAILQ_INSERT_TAIL(&head->stg_head, stage, stg_link);
}

/*
 * Schedule ifnet.if_start on the subqueue owner CPU
 */
static void
ifsq_ifstart_schedule(struct ifaltq_subque *ifsq, int force)
{
	int cpu;

	if (!force && curthread->td_type == TD_TYPE_NETISR &&
	    ifsq_stage_cntmax > 0) {
		struct ifsubq_stage *stage = ifsq_get_stage(ifsq, mycpuid);

		stage->stg_cnt = 0;
		stage->stg_len = 0;
		if ((stage->stg_flags & IFSQ_STAGE_FLAG_QUED) == 0)
			ifsq_stage_insert(&ifsubq_stage_heads[mycpuid], stage);
		stage->stg_flags |= IFSQ_STAGE_FLAG_SCHED;
		return;
	}

	cpu = ifsq_get_cpuid(ifsq);
	if (cpu != mycpuid)
		lwkt_send_ipiq(globaldata_find(cpu), ifsq_ifstart_ipifunc, ifsq);
	else
		ifsq_ifstart_ipifunc(ifsq);
}

/*
 * NOTE:
 * This function will release ifnet.if_start subqueue interlock,
 * if ifnet.if_start for the subqueue does not need to be scheduled
 */
static __inline int
ifsq_ifstart_need_schedule(struct ifaltq_subque *ifsq, int running)
{
	if (!running || ifsq_is_empty(ifsq)
#ifdef ALTQ
	    || ifsq->ifsq_altq->altq_tbr != NULL
#endif
	) {
		ALTQ_SQ_LOCK(ifsq);
		/*
		 * ifnet.if_start subqueue interlock is released, if:
		 * 1) Hardware can not take any packets, due to
		 *    o  interface is marked down
		 *    o  hardware queue is full (ifsq_is_oactive)
		 *    Under the second situation, hardware interrupt
		 *    or polling(4) will call/schedule ifnet.if_start
		 *    on the subqueue when hardware queue is ready
		 * 2) There is no packet in the subqueue.
		 *    Further ifq_dispatch or ifq_handoff will call/
		 *    schedule ifnet.if_start on the subqueue.
		 * 3) TBR is used and it does not allow further
		 *    dequeueing.
		 *    TBR callout will call ifnet.if_start on the
		 *    subqueue.
		 */
		if (!running || !ifsq_data_ready(ifsq)) {
			ifsq_clr_started(ifsq);
			ALTQ_SQ_UNLOCK(ifsq);
			return 0;
		}
		ALTQ_SQ_UNLOCK(ifsq);
	}
	return 1;
}

static void
ifsq_ifstart_dispatch(netmsg_t msg)
{
	struct lwkt_msg *lmsg = &msg->base.lmsg;
	struct ifaltq_subque *ifsq = lmsg->u.ms_resultp;
	struct ifnet *ifp = ifsq_get_ifp(ifsq);
	struct globaldata *gd = mycpu;
	int running = 0, need_sched;

	crit_enter_gd(gd);

	lwkt_replymsg(lmsg, 0);	/* reply ASAP */

	if (gd->gd_cpuid != ifsq_get_cpuid(ifsq)) {
		/*
		 * We need to chase the subqueue owner CPU change.
		 */
		ifsq_ifstart_schedule(ifsq, 1);
		crit_exit_gd(gd);
		return;
	}

	ifsq_serialize_hw(ifsq);
	if ((ifp->if_flags & IFF_RUNNING) && !ifsq_is_oactive(ifsq)) {
		ifp->if_start(ifp, ifsq);
		if ((ifp->if_flags & IFF_RUNNING) && !ifsq_is_oactive(ifsq))
			running = 1;
	}
	need_sched = ifsq_ifstart_need_schedule(ifsq, running);
	ifsq_deserialize_hw(ifsq);

	if (need_sched) {
		/*
		 * More data need to be transmitted, ifnet.if_start is
		 * scheduled on the subqueue owner CPU, and we keep going.
		 * NOTE: ifnet.if_start subqueue interlock is not released.
		 */
		ifsq_ifstart_schedule(ifsq, 0);
	}

	crit_exit_gd(gd);
}

/* Device driver ifnet.if_start helper function */
void
ifsq_devstart(struct ifaltq_subque *ifsq)
{
	struct ifnet *ifp = ifsq_get_ifp(ifsq);
	int running = 0;

	ASSERT_ALTQ_SQ_SERIALIZED_HW(ifsq);

	ALTQ_SQ_LOCK(ifsq);
	if (ifsq_is_started(ifsq) || !ifsq_data_ready(ifsq)) {
		ALTQ_SQ_UNLOCK(ifsq);
		return;
	}
	ifsq_set_started(ifsq);
	ALTQ_SQ_UNLOCK(ifsq);

	ifp->if_start(ifp, ifsq);

	if ((ifp->if_flags & IFF_RUNNING) && !ifsq_is_oactive(ifsq))
		running = 1;

	if (ifsq_ifstart_need_schedule(ifsq, running)) {
		/*
		 * More data need to be transmitted, ifnet.if_start is
		 * scheduled on ifnet's CPU, and we keep going.
		 * NOTE: ifnet.if_start interlock is not released.
		 */
		ifsq_ifstart_schedule(ifsq, 0);
	}
}

void
if_devstart(struct ifnet *ifp)
{
	ifsq_devstart(ifq_get_subq_default(&ifp->if_snd));
}

/* Device driver ifnet.if_start schedule helper function */
void
ifsq_devstart_sched(struct ifaltq_subque *ifsq)
{
	ifsq_ifstart_schedule(ifsq, 1);
}

void
if_devstart_sched(struct ifnet *ifp)
{
	ifsq_devstart_sched(ifq_get_subq_default(&ifp->if_snd));
}

static void
if_default_serialize(struct ifnet *ifp, enum ifnet_serialize slz __unused)
{
	lwkt_serialize_enter(ifp->if_serializer);
}

static void
if_default_deserialize(struct ifnet *ifp, enum ifnet_serialize slz __unused)
{
	lwkt_serialize_exit(ifp->if_serializer);
}

static int
if_default_tryserialize(struct ifnet *ifp, enum ifnet_serialize slz __unused)
{
	return lwkt_serialize_try(ifp->if_serializer);
}

#ifdef INVARIANTS
static void
if_default_serialize_assert(struct ifnet *ifp,
			    enum ifnet_serialize slz __unused,
			    boolean_t serialized)
{
	if (serialized)
		ASSERT_SERIALIZED(ifp->if_serializer);
	else
		ASSERT_NOT_SERIALIZED(ifp->if_serializer);
}
#endif

/*
 * Attach an interface to the list of "active" interfaces.
 *
 * The serializer is optional.
 */
void
if_attach(struct ifnet *ifp, lwkt_serialize_t serializer)
{
	unsigned socksize;
	int namelen, masklen;
	struct sockaddr_dl *sdl, *sdl_addr;
	struct ifaddr *ifa;
	struct ifaltq *ifq;
	struct ifnet **old_ifindex2ifnet = NULL;
	struct ifnet_array *old_ifnet_array;
	int i, q, qlen;
	char qlenname[64];

	static int if_indexlim = 8;

	if (ifp->if_serialize != NULL) {
		KASSERT(ifp->if_deserialize != NULL &&
			ifp->if_tryserialize != NULL &&
			ifp->if_serialize_assert != NULL,
			("serialize functions are partially setup"));

		/*
		 * If the device supplies serialize functions,
		 * then clear if_serializer to catch any invalid
		 * usage of this field.
		 */
		KASSERT(serializer == NULL,
			("both serialize functions and default serializer "
			 "are supplied"));
		ifp->if_serializer = NULL;
	} else {
		KASSERT(ifp->if_deserialize == NULL &&
			ifp->if_tryserialize == NULL &&
			ifp->if_serialize_assert == NULL,
			("serialize functions are partially setup"));
		ifp->if_serialize = if_default_serialize;
		ifp->if_deserialize = if_default_deserialize;
		ifp->if_tryserialize = if_default_tryserialize;
#ifdef INVARIANTS
		ifp->if_serialize_assert = if_default_serialize_assert;
#endif

		/*
		 * The serializer can be passed in from the device,
		 * allowing the same serializer to be used for both
		 * the interrupt interlock and the device queue.
		 * If not specified, the netif structure will use an
		 * embedded serializer.
		 */
		if (serializer == NULL) {
			serializer = &ifp->if_default_serializer;
			lwkt_serialize_init(serializer);
		}
		ifp->if_serializer = serializer;
	}

	/*
	 * Make if_addrhead available on all CPUs, since they
	 * could be accessed by any threads.
	 */
	ifp->if_addrheads = kmalloc(ncpus * sizeof(struct ifaddrhead),
				    M_IFADDR, M_WAITOK | M_ZERO);
	for (i = 0; i < ncpus; ++i)
		TAILQ_INIT(&ifp->if_addrheads[i]);

	TAILQ_INIT(&ifp->if_multiaddrs);
	TAILQ_INIT(&ifp->if_groups);
	getmicrotime(&ifp->if_lastchange);
	if_addgroup(ifp, IFG_ALL);

	/*
	 * create a Link Level name for this device
	 */
	namelen = strlen(ifp->if_xname);
	masklen = offsetof(struct sockaddr_dl, sdl_data[0]) + namelen;
	socksize = masklen + ifp->if_addrlen;
	if (socksize < sizeof(*sdl))
		socksize = sizeof(*sdl);
	socksize = RT_ROUNDUP(socksize);
	ifa = ifa_create(sizeof(struct ifaddr) + 2 * socksize);
	sdl = sdl_addr = (struct sockaddr_dl *)(ifa + 1);
	sdl->sdl_len = socksize;
	sdl->sdl_family = AF_LINK;
	bcopy(ifp->if_xname, sdl->sdl_data, namelen);
	sdl->sdl_nlen = namelen;
	sdl->sdl_type = ifp->if_type;
	ifp->if_lladdr = ifa;
	ifa->ifa_ifp = ifp;
	ifa->ifa_rtrequest = link_rtrequest;
	ifa->ifa_addr = (struct sockaddr *)sdl;
	sdl = (struct sockaddr_dl *)(socksize + (caddr_t)sdl);
	ifa->ifa_netmask = (struct sockaddr *)sdl;
	sdl->sdl_len = masklen;
	while (namelen != 0)
		sdl->sdl_data[--namelen] = 0xff;
	ifa_iflink(ifa, ifp, 0 /* Insert head */);

	/*
	 * Make if_data available on all CPUs, since they could
	 * be updated by hardware interrupt routing, which could
	 * be bound to any CPU.
	 */
	ifp->if_data_pcpu = kmalloc(ncpus * sizeof(struct ifdata_pcpu),
				    M_DEVBUF,
				    M_WAITOK | M_ZERO | M_CACHEALIGN);

	if (ifp->if_mapsubq == NULL)
		ifp->if_mapsubq = ifq_mapsubq_default;

	ifq = &ifp->if_snd;
	ifq->altq_type = 0;
	ifq->altq_disc = NULL;
	ifq->altq_flags &= ALTQF_CANTCHANGE;
	ifq->altq_tbr = NULL;
	ifq->altq_ifp = ifp;

	if (ifq->altq_subq_cnt <= 0)
		ifq->altq_subq_cnt = 1;
	ifq->altq_subq =
		kmalloc(ifq->altq_subq_cnt * sizeof(struct ifaltq_subque),
			M_DEVBUF,
			M_WAITOK | M_ZERO | M_CACHEALIGN);

	if (ifq->altq_maxlen == 0) {
		if_printf(ifp, "driver didn't set altq_maxlen\n");
		ifq_set_maxlen(ifq, ifqmaxlen);
	}

	/* Allow user to override driver's setting. */
	ksnprintf(qlenname, sizeof(qlenname), "net.%s.qlenmax", ifp->if_xname);
	qlen = -1;
	TUNABLE_INT_FETCH(qlenname, &qlen);
	if (qlen > 0) {
		if_printf(ifp, "qlenmax -> %d\n", qlen);
		ifq_set_maxlen(ifq, qlen);
	}

	for (q = 0; q < ifq->altq_subq_cnt; ++q) {
		struct ifaltq_subque *ifsq = &ifq->altq_subq[q];

		ALTQ_SQ_LOCK_INIT(ifsq);
		ifsq->ifsq_index = q;

		ifsq->ifsq_altq = ifq;
		ifsq->ifsq_ifp = ifp;

		ifsq->ifsq_maxlen = ifq->altq_maxlen;
		ifsq->ifsq_maxbcnt = ifsq->ifsq_maxlen * MCLBYTES;
		ifsq->ifsq_prepended = NULL;
		ifsq->ifsq_started = 0;
		ifsq->ifsq_hw_oactive = 0;
		ifsq_set_cpuid(ifsq, 0);
		if (ifp->if_serializer != NULL)
			ifsq_set_hw_serialize(ifsq, ifp->if_serializer);

		/* XXX: netisr_ncpus */
		ifsq->ifsq_stage =
			kmalloc(ncpus * sizeof(struct ifsubq_stage),
				M_DEVBUF,
				M_WAITOK | M_ZERO | M_CACHEALIGN);
		for (i = 0; i < ncpus; ++i)
			ifsq->ifsq_stage[i].stg_subq = ifsq;

		/*
		 * Allocate one if_start message for each CPU, since
		 * the hardware TX ring could be assigned to any CPU.
		 *
		 * NOTE:
		 * If the hardware TX ring polling CPU and the hardware
		 * TX ring interrupt CPU are same, one if_start message
		 * should be enough.
		 */
		ifsq->ifsq_ifstart_nmsg =
		    kmalloc(ncpus * sizeof(struct netmsg_base),
		    M_LWKTMSG, M_WAITOK);
		for (i = 0; i < ncpus; ++i) {
			netmsg_init(&ifsq->ifsq_ifstart_nmsg[i], NULL,
			    &netisr_adone_rport, 0, ifsq_ifstart_dispatch);
			ifsq->ifsq_ifstart_nmsg[i].lmsg.u.ms_resultp = ifsq;
		}
	}
	ifq_set_classic(ifq);

	/*
	 * Increase mbuf cluster/jcluster limits for the mbufs that
	 * could sit on the device queues for quite some time.
	 */
	if (ifp->if_nmbclusters > 0)
		mcl_inclimit(ifp->if_nmbclusters);
	if (ifp->if_nmbjclusters > 0)
		mjcl_inclimit(ifp->if_nmbjclusters);

	/*
	 * Install this ifp into ifindex2inet, ifnet queue and ifnet
	 * array after it is setup.
	 *
	 * Protect ifindex2ifnet, ifnet queue and ifnet array changes
	 * by ifnet lock, so that non-netisr threads could get a
	 * consistent view.
	 */
	ifnet_lock();

	/* Don't update if_index until ifindex2ifnet is setup */
	ifp->if_index = if_index + 1;
	sdl_addr->sdl_index = ifp->if_index;

	/*
	 * Install this ifp into ifindex2ifnet
	 */
	if (ifindex2ifnet == NULL || ifp->if_index >= if_indexlim) {
		unsigned int n;
		struct ifnet **q;

		/*
		 * Grow ifindex2ifnet
		 */
		if_indexlim <<= 1;
		n = if_indexlim * sizeof(*q);
		q = kmalloc(n, M_IFADDR, M_WAITOK | M_ZERO);
		if (ifindex2ifnet != NULL) {
			bcopy(ifindex2ifnet, q, n/2);
			/* Free old ifindex2ifnet after sync all netisrs */
			old_ifindex2ifnet = ifindex2ifnet;
		}
		ifindex2ifnet = q;
	}
	ifindex2ifnet[ifp->if_index] = ifp;
	/*
	 * Update if_index after this ifp is installed into ifindex2ifnet,
	 * so that netisrs could get a consistent view of ifindex2ifnet.
	 */
	cpu_sfence();
	if_index = ifp->if_index;

	/*
	 * Install this ifp into ifnet array.
	 */
	/* Free old ifnet array after sync all netisrs */
	old_ifnet_array = ifnet_array;
	ifnet_array = ifnet_array_add(ifp, old_ifnet_array);

	/*
	 * Install this ifp into ifnet queue.
	 */
	TAILQ_INSERT_TAIL(&ifnetlist, ifp, if_link);

	ifnet_unlock();

	/*
	 * Sync all netisrs so that the old ifindex2ifnet and ifnet array
	 * are no longer accessed and we can free them safely later on.
	 */
	netmsg_service_sync();
	if (old_ifindex2ifnet != NULL)
		kfree(old_ifindex2ifnet, M_IFADDR);
	ifnet_array_free(old_ifnet_array);

	if (!SLIST_EMPTY(&domains))
		if_attachdomain1(ifp);

	/* Announce the interface. */
	EVENTHANDLER_INVOKE(ifnet_attach_event, ifp);
	devctl_notify("IFNET", ifp->if_xname, "ATTACH", NULL);
	rt_ifannouncemsg(ifp, IFAN_ARRIVAL);
}

static void
if_attachdomain(void *dummy)
{
	struct ifnet *ifp;

	ifnet_lock();
	TAILQ_FOREACH(ifp, &ifnetlist, if_list)
		if_attachdomain1(ifp);
	ifnet_unlock();
}
SYSINIT(domainifattach, SI_SUB_PROTO_IFATTACHDOMAIN, SI_ORDER_FIRST,
	if_attachdomain, NULL);

static void
if_attachdomain1(struct ifnet *ifp)
{
	struct domain *dp;

	crit_enter();

	/* address family dependent data region */
	bzero(ifp->if_afdata, sizeof(ifp->if_afdata));
	SLIST_FOREACH(dp, &domains, dom_next)
		if (dp->dom_ifattach)
			ifp->if_afdata[dp->dom_family] =
				(*dp->dom_ifattach)(ifp);
	crit_exit();
}

/*
 * Purge all addresses whose type is _not_ AF_LINK
 */
static void
if_purgeaddrs_nolink_dispatch(netmsg_t nmsg)
{
	struct ifnet *ifp = nmsg->lmsg.u.ms_resultp;
	struct ifaddr_container *ifac, *next;

	ASSERT_NETISR0;

	/*
	 * The ifaddr processing in the following loop will block,
	 * however, this function is called in netisr0, in which
	 * ifaddr list changes happen, so we don't care about the
	 * blockness of the ifaddr processing here.
	 */
	TAILQ_FOREACH_MUTABLE(ifac, &ifp->if_addrheads[mycpuid],
			      ifa_link, next) {
		struct ifaddr *ifa = ifac->ifa;

		/* Ignore marker */
		if (ifa->ifa_addr->sa_family == AF_UNSPEC)
			continue;

		/* Leave link ifaddr as it is */
		if (ifa->ifa_addr->sa_family == AF_LINK)
			continue;
#ifdef INET
		/* XXX: Ugly!! ad hoc just for INET */
		if (ifa->ifa_addr->sa_family == AF_INET) {
			struct ifaliasreq ifr;
			struct sockaddr_in saved_addr, saved_dst;
#ifdef IFADDR_DEBUG_VERBOSE
			int i;

			kprintf("purge in4 addr %p: ", ifa);
			for (i = 0; i < ncpus; ++i) {
				kprintf("%d ",
				    ifa->ifa_containers[i].ifa_refcnt);
			}
			kprintf("\n");
#endif

			/* Save information for panic. */
			memcpy(&saved_addr, ifa->ifa_addr, sizeof(saved_addr));
			if (ifa->ifa_dstaddr != NULL) {
				memcpy(&saved_dst, ifa->ifa_dstaddr,
				    sizeof(saved_dst));
			} else {
				memset(&saved_dst, 0, sizeof(saved_dst));
			}

			bzero(&ifr, sizeof ifr);
			ifr.ifra_addr = *ifa->ifa_addr;
			if (ifa->ifa_dstaddr)
				ifr.ifra_broadaddr = *ifa->ifa_dstaddr;
			if (in_control(SIOCDIFADDR, (caddr_t)&ifr, ifp,
				       NULL) == 0)
				continue;

			/* MUST NOT HAPPEN */
			panic("%s: in_control failed %x, dst %x", ifp->if_xname,
			    ntohl(saved_addr.sin_addr.s_addr),
			    ntohl(saved_dst.sin_addr.s_addr));
		}
#endif /* INET */
#ifdef INET6
		if (ifa->ifa_addr->sa_family == AF_INET6) {
#ifdef IFADDR_DEBUG_VERBOSE
			int i;

			kprintf("purge in6 addr %p: ", ifa);
			for (i = 0; i < ncpus; ++i) {
				kprintf("%d ",
				    ifa->ifa_containers[i].ifa_refcnt);
			}
			kprintf("\n");
#endif

			in6_purgeaddr(ifa);
			/* ifp_addrhead is already updated */
			continue;
		}
#endif /* INET6 */
		if_printf(ifp, "destroy ifaddr family %d\n",
		    ifa->ifa_addr->sa_family);
		ifa_ifunlink(ifa, ifp);
		ifa_destroy(ifa);
	}

	netisr_replymsg(&nmsg->base, 0);
}

void
if_purgeaddrs_nolink(struct ifnet *ifp)
{
	struct netmsg_base nmsg;

	netmsg_init(&nmsg, NULL, &curthread->td_msgport, 0,
	    if_purgeaddrs_nolink_dispatch);
	nmsg.lmsg.u.ms_resultp = ifp;
	netisr_domsg(&nmsg, 0);
}

static void
ifq_stage_detach_handler(netmsg_t nmsg)
{
	struct ifaltq *ifq = nmsg->lmsg.u.ms_resultp;
	int q;

	for (q = 0; q < ifq->altq_subq_cnt; ++q) {
		struct ifaltq_subque *ifsq = &ifq->altq_subq[q];
		struct ifsubq_stage *stage = ifsq_get_stage(ifsq, mycpuid);

		if (stage->stg_flags & IFSQ_STAGE_FLAG_QUED)
			ifsq_stage_remove(&ifsubq_stage_heads[mycpuid], stage);
	}
	lwkt_replymsg(&nmsg->lmsg, 0);
}

static void
ifq_stage_detach(struct ifaltq *ifq)
{
	struct netmsg_base base;
	int cpu;

	netmsg_init(&base, NULL, &curthread->td_msgport, 0,
	    ifq_stage_detach_handler);
	base.lmsg.u.ms_resultp = ifq;

	/* XXX netisr_ncpus */
	for (cpu = 0; cpu < ncpus; ++cpu)
		lwkt_domsg(netisr_cpuport(cpu), &base.lmsg, 0);
}

struct netmsg_if_rtdel {
	struct netmsg_base	base;
	struct ifnet		*ifp;
};

static void
if_rtdel_dispatch(netmsg_t msg)
{
	struct netmsg_if_rtdel *rmsg = (void *)msg;
	int i, cpu;

	cpu = mycpuid;
	ASSERT_NETISR_NCPUS(cpu);

	for (i = 1; i <= AF_MAX; i++) {
		struct radix_node_head	*rnh;

		if ((rnh = rt_tables[cpu][i]) == NULL)
			continue;
		rnh->rnh_walktree(rnh, if_rtdel, rmsg->ifp);
	}
	netisr_forwardmsg(&msg->base, cpu + 1);
}

/*
 * Detach an interface, removing it from the
 * list of "active" interfaces.
 */
void
if_detach(struct ifnet *ifp)
{
	struct ifnet_array *old_ifnet_array;
	struct ifg_list *ifgl;
	struct netmsg_if_rtdel msg;
	struct domain *dp;
	int q;

	/* Announce that the interface is gone. */
	EVENTHANDLER_INVOKE(ifnet_detach_event, ifp);
	rt_ifannouncemsg(ifp, IFAN_DEPARTURE);
	devctl_notify("IFNET", ifp->if_xname, "DETACH", NULL);

	/*
	 * Remove this ifp from ifindex2inet, ifnet queue and ifnet
	 * array before it is whacked.
	 *
	 * Protect ifindex2ifnet, ifnet queue and ifnet array changes
	 * by ifnet lock, so that non-netisr threads could get a
	 * consistent view.
	 */
	ifnet_lock();

	/*
	 * Remove this ifp from ifindex2ifnet and maybe decrement if_index.
	 */
	ifindex2ifnet[ifp->if_index] = NULL;
	while (if_index > 0 && ifindex2ifnet[if_index] == NULL)
		if_index--;

	/*
	 * Remove this ifp from ifnet queue.
	 */
	TAILQ_REMOVE(&ifnetlist, ifp, if_link);

	/*
	 * Remove this ifp from ifnet array.
	 */
	/* Free old ifnet array after sync all netisrs */
	old_ifnet_array = ifnet_array;
	ifnet_array = ifnet_array_del(ifp, old_ifnet_array);

	ifnet_unlock();

	ifgroup_lockmgr(LK_EXCLUSIVE);
	while ((ifgl = TAILQ_FIRST(&ifp->if_groups)) != NULL)
		if_delgroup_locked(ifp, ifgl->ifgl_group->ifg_group);
	ifgroup_lockmgr(LK_RELEASE);

	/*
	 * Sync all netisrs so that the old ifnet array is no longer
	 * accessed and we can free it safely later on.
	 */
	netmsg_service_sync();
	ifnet_array_free(old_ifnet_array);

	/*
	 * Remove routes and flush queues.
	 */
	crit_enter();
#ifdef IFPOLL_ENABLE
	if (ifp->if_flags & IFF_NPOLLING)
		ifpoll_deregister(ifp);
#endif
	if_down(ifp);

	/* Decrease the mbuf clusters/jclusters limits increased by us */
	if (ifp->if_nmbclusters > 0)
		mcl_inclimit(-ifp->if_nmbclusters);
	if (ifp->if_nmbjclusters > 0)
		mjcl_inclimit(-ifp->if_nmbjclusters);

#ifdef ALTQ
	if (ifq_is_enabled(&ifp->if_snd))
		altq_disable(&ifp->if_snd);
	if (ifq_is_attached(&ifp->if_snd))
		altq_detach(&ifp->if_snd);
#endif

	/*
	 * Clean up all addresses.
	 */
	ifp->if_lladdr = NULL;

	if_purgeaddrs_nolink(ifp);
	if (!TAILQ_EMPTY(&ifp->if_addrheads[mycpuid])) {
		struct ifaddr *ifa;

		ifa = TAILQ_FIRST(&ifp->if_addrheads[mycpuid])->ifa;
		KASSERT(ifa->ifa_addr->sa_family == AF_LINK,
			("non-link ifaddr is left on if_addrheads"));

		ifa_ifunlink(ifa, ifp);
		ifa_destroy(ifa);
		KASSERT(TAILQ_EMPTY(&ifp->if_addrheads[mycpuid]),
			("there are still ifaddrs left on if_addrheads"));
	}

#ifdef INET
	/*
	 * Remove all IPv4 kernel structures related to ifp.
	 */
	in_ifdetach(ifp);
#endif

#ifdef INET6
	/*
	 * Remove all IPv6 kernel structs related to ifp.  This should be done
	 * before removing routing entries below, since IPv6 interface direct
	 * routes are expected to be removed by the IPv6-specific kernel API.
	 * Otherwise, the kernel will detect some inconsistency and bark it.
	 */
	in6_ifdetach(ifp);
#endif

	/*
	 * Delete all remaining routes using this interface
	 */
	netmsg_init(&msg.base, NULL, &curthread->td_msgport, MSGF_PRIORITY,
	    if_rtdel_dispatch);
	msg.ifp = ifp;
	netisr_domsg_global(&msg.base);

	SLIST_FOREACH(dp, &domains, dom_next) {
		if (dp->dom_ifdetach && ifp->if_afdata[dp->dom_family])
			(*dp->dom_ifdetach)(ifp,
				ifp->if_afdata[dp->dom_family]);
	}

	kfree(ifp->if_addrheads, M_IFADDR);

	lwkt_synchronize_ipiqs("if_detach");
	ifq_stage_detach(&ifp->if_snd);

	for (q = 0; q < ifp->if_snd.altq_subq_cnt; ++q) {
		struct ifaltq_subque *ifsq = &ifp->if_snd.altq_subq[q];

		kfree(ifsq->ifsq_ifstart_nmsg, M_LWKTMSG);
		kfree(ifsq->ifsq_stage, M_DEVBUF);
	}
	kfree(ifp->if_snd.altq_subq, M_DEVBUF);

	kfree(ifp->if_data_pcpu, M_DEVBUF);

	crit_exit();
}

int
ifgroup_lockmgr(u_int flags)
{
	return lockmgr(&ifgroup_lock, flags);
}

/*
 * Create an empty interface group.
 */
static struct ifg_group *
if_creategroup(const char *groupname)
{
	struct ifg_group *ifg;

	ifg = kmalloc(sizeof(*ifg), M_IFNET, M_WAITOK);
	strlcpy(ifg->ifg_group, groupname, sizeof(ifg->ifg_group));
	ifg->ifg_refcnt = 0;
	ifg->ifg_carp_demoted = 0;
	TAILQ_INIT(&ifg->ifg_members);

	ifgroup_lockmgr(LK_EXCLUSIVE);
	TAILQ_INSERT_TAIL(&ifg_head, ifg, ifg_next);
	ifgroup_lockmgr(LK_RELEASE);

	EVENTHANDLER_INVOKE(group_attach_event, ifg);

	return (ifg);
}

/*
 * Destroy an empty interface group.
 */
static int
if_destroygroup(struct ifg_group *ifg)
{
	KASSERT(ifg->ifg_refcnt == 0,
		("trying to delete a non-empty interface group"));

	ifgroup_lockmgr(LK_EXCLUSIVE);
	TAILQ_REMOVE(&ifg_head, ifg, ifg_next);
	ifgroup_lockmgr(LK_RELEASE);

	EVENTHANDLER_INVOKE(group_detach_event, ifg);
	kfree(ifg, M_IFNET);

	return (0);
}

/*
 * Add the interface to a group.
 * The target group will be created if it doesn't exist.
 */
int
if_addgroup(struct ifnet *ifp, const char *groupname)
{
	struct ifg_list *ifgl;
	struct ifg_group *ifg;
	struct ifg_member *ifgm;

	if (groupname[0] &&
	    groupname[strlen(groupname) - 1] >= '0' &&
	    groupname[strlen(groupname) - 1] <= '9')
		return (EINVAL);

	ifgroup_lockmgr(LK_SHARED);

	TAILQ_FOREACH(ifgl, &ifp->if_groups, ifgl_next) {
		if (strcmp(ifgl->ifgl_group->ifg_group, groupname) == 0) {
			ifgroup_lockmgr(LK_RELEASE);
			return (EEXIST);
		}
	}

	TAILQ_FOREACH(ifg, &ifg_head, ifg_next) {
		if (strcmp(ifg->ifg_group, groupname) == 0)
			break;
	}

	ifgroup_lockmgr(LK_RELEASE);

	if (ifg == NULL)
		ifg = if_creategroup(groupname);

	ifgl = kmalloc(sizeof(*ifgl), M_IFNET, M_WAITOK);
	ifgm = kmalloc(sizeof(*ifgm), M_IFNET, M_WAITOK);
	ifgl->ifgl_group = ifg;
	ifgm->ifgm_ifp = ifp;
	ifg->ifg_refcnt++;

	ifgroup_lockmgr(LK_EXCLUSIVE);
	TAILQ_INSERT_TAIL(&ifg->ifg_members, ifgm, ifgm_next);
	TAILQ_INSERT_TAIL(&ifp->if_groups, ifgl, ifgl_next);
	ifgroup_lockmgr(LK_RELEASE);

	EVENTHANDLER_INVOKE(group_change_event, groupname);

	return (0);
}

/*
 * Remove the interface from a group.
 * The group will be destroyed if it becomes empty.
 *
 * The 'ifgroup_lock' must be hold exclusively when calling this.
 */
static int
if_delgroup_locked(struct ifnet *ifp, const char *groupname)
{
	struct ifg_list *ifgl;
	struct ifg_member *ifgm;

	KKASSERT(lockstatus(&ifgroup_lock, curthread) == LK_EXCLUSIVE);

	TAILQ_FOREACH(ifgl, &ifp->if_groups, ifgl_next) {
		if (strcmp(ifgl->ifgl_group->ifg_group, groupname) == 0)
			break;
	}
	if (ifgl == NULL)
		return (ENOENT);

	TAILQ_REMOVE(&ifp->if_groups, ifgl, ifgl_next);

	TAILQ_FOREACH(ifgm, &ifgl->ifgl_group->ifg_members, ifgm_next) {
		if (ifgm->ifgm_ifp == ifp)
			break;
	}

	if (ifgm != NULL) {
		TAILQ_REMOVE(&ifgl->ifgl_group->ifg_members, ifgm, ifgm_next);

		ifgroup_lockmgr(LK_RELEASE);
		EVENTHANDLER_INVOKE(group_change_event, groupname);
		ifgroup_lockmgr(LK_EXCLUSIVE);

		kfree(ifgm, M_IFNET);
		ifgl->ifgl_group->ifg_refcnt--;
	}

	if (ifgl->ifgl_group->ifg_refcnt == 0) {
		ifgroup_lockmgr(LK_RELEASE);
		if_destroygroup(ifgl->ifgl_group);
		ifgroup_lockmgr(LK_EXCLUSIVE);
	}

	kfree(ifgl, M_IFNET);

	return (0);
}

int
if_delgroup(struct ifnet *ifp, const char *groupname)
{
	int error;

	ifgroup_lockmgr(LK_EXCLUSIVE);
	error = if_delgroup_locked(ifp, groupname);
	ifgroup_lockmgr(LK_RELEASE);

	return (error);
}

/*
 * Store all the groups that the interface belongs to in memory
 * pointed to by data.
 */
static int
if_getgroups(struct ifgroupreq *ifgr, struct ifnet *ifp)
{
	struct ifg_list *ifgl;
	struct ifg_req *ifgrq, *p;
	int len, error;

	len = 0;
	ifgroup_lockmgr(LK_SHARED);
	TAILQ_FOREACH(ifgl, &ifp->if_groups, ifgl_next)
		len += sizeof(struct ifg_req);
	ifgroup_lockmgr(LK_RELEASE);

	if (ifgr->ifgr_len == 0) {
		/*
		 * Caller is asking how much memory should be allocated in
		 * the next request in order to hold all the groups.
		 */
		ifgr->ifgr_len = len;
		return (0);
	} else if (ifgr->ifgr_len != len) {
		return (EINVAL);
	}

	ifgrq = kmalloc(len, M_TEMP, M_INTWAIT | M_NULLOK | M_ZERO);
	if (ifgrq == NULL)
		return (ENOMEM);

	ifgroup_lockmgr(LK_SHARED);
	p = ifgrq;
	TAILQ_FOREACH(ifgl, &ifp->if_groups, ifgl_next) {
		if (len < sizeof(struct ifg_req)) {
			ifgroup_lockmgr(LK_RELEASE);
			error = EINVAL;
			goto failed;
		}

		strlcpy(p->ifgrq_group, ifgl->ifgl_group->ifg_group,
			sizeof(ifgrq->ifgrq_group));
		len -= sizeof(struct ifg_req);
		p++;
	}
	ifgroup_lockmgr(LK_RELEASE);

	error = copyout(ifgrq, ifgr->ifgr_groups, ifgr->ifgr_len);
failed:
	kfree(ifgrq, M_TEMP);
	return error;
}

/*
 * Store all the members of a group in memory pointed to by data.
 */
static int
if_getgroupmembers(struct ifgroupreq *ifgr)
{
	struct ifg_group *ifg;
	struct ifg_member *ifgm;
	struct ifg_req *ifgrq, *p;
	int len, error;

	ifgroup_lockmgr(LK_SHARED);

	TAILQ_FOREACH(ifg, &ifg_head, ifg_next) {
		if (strcmp(ifg->ifg_group, ifgr->ifgr_name) == 0)
			break;
	}
	if (ifg == NULL) {
		ifgroup_lockmgr(LK_RELEASE);
		return (ENOENT);
	}

	len = 0;
	TAILQ_FOREACH(ifgm, &ifg->ifg_members, ifgm_next)
		len += sizeof(struct ifg_req);

	ifgroup_lockmgr(LK_RELEASE);

	if (ifgr->ifgr_len == 0) {
		ifgr->ifgr_len = len;
		return (0);
	} else if (ifgr->ifgr_len != len) {
		return (EINVAL);
	}

	ifgrq = kmalloc(len, M_TEMP, M_INTWAIT | M_NULLOK | M_ZERO);
	if (ifgrq == NULL)
		return (ENOMEM);

	ifgroup_lockmgr(LK_SHARED);
	p = ifgrq;
	TAILQ_FOREACH(ifgm, &ifg->ifg_members, ifgm_next) {
		if (len < sizeof(struct ifg_req)) {
			ifgroup_lockmgr(LK_RELEASE);
			error = EINVAL;
			goto failed;
		}

		strlcpy(p->ifgrq_member, ifgm->ifgm_ifp->if_xname,
			sizeof(p->ifgrq_member));
		len -= sizeof(struct ifg_req);
		p++;
	}
	ifgroup_lockmgr(LK_RELEASE);

	error = copyout(ifgrq, ifgr->ifgr_groups, ifgr->ifgr_len);
failed:
	kfree(ifgrq, M_TEMP);
	return error;
}

/*
 * Delete Routes for a Network Interface
 *
 * Called for each routing entry via the rnh->rnh_walktree() call above
 * to delete all route entries referencing a detaching network interface.
 *
 * Arguments:
 *	rn	pointer to node in the routing table
 *	arg	argument passed to rnh->rnh_walktree() - detaching interface
 *
 * Returns:
 *	0	successful
 *	errno	failed - reason indicated
 *
 */
static int
if_rtdel(struct radix_node *rn, void *arg)
{
	struct rtentry	*rt = (struct rtentry *)rn;
	struct ifnet	*ifp = arg;
	int		err;

	if (rt->rt_ifp == ifp) {

		/*
		 * Protect (sorta) against walktree recursion problems
		 * with cloned routes
		 */
		if (!(rt->rt_flags & RTF_UP))
			return (0);

		err = rtrequest(RTM_DELETE, rt_key(rt), rt->rt_gateway,
				rt_mask(rt), rt->rt_flags,
				NULL);
		if (err) {
			log(LOG_WARNING, "if_rtdel: error %d\n", err);
		}
	}

	return (0);
}

static __inline boolean_t
ifa_prefer(const struct ifaddr *cur_ifa, const struct ifaddr *old_ifa)
{
	if (old_ifa == NULL)
		return TRUE;

	if ((old_ifa->ifa_ifp->if_flags & IFF_UP) == 0 &&
	    (cur_ifa->ifa_ifp->if_flags & IFF_UP))
		return TRUE;
	if ((old_ifa->ifa_flags & IFA_ROUTE) == 0 &&
	    (cur_ifa->ifa_flags & IFA_ROUTE))
		return TRUE;
	return FALSE;
}

/*
 * Locate an interface based on a complete address.
 */
struct ifaddr *
ifa_ifwithaddr(struct sockaddr *addr)
{
	const struct ifnet_array *arr;
	int i;

	arr = ifnet_array_get();
	for (i = 0; i < arr->ifnet_count; ++i) {
		struct ifnet *ifp = arr->ifnet_arr[i];
		struct ifaddr_container *ifac;

		TAILQ_FOREACH(ifac, &ifp->if_addrheads[mycpuid], ifa_link) {
			struct ifaddr *ifa = ifac->ifa;

			if (ifa->ifa_addr->sa_family != addr->sa_family)
				continue;
			if (sa_equal(addr, ifa->ifa_addr))
				return (ifa);
			if ((ifp->if_flags & IFF_BROADCAST) &&
			    ifa->ifa_broadaddr &&
			    /* IPv6 doesn't have broadcast */
			    ifa->ifa_broadaddr->sa_len != 0 &&
			    sa_equal(ifa->ifa_broadaddr, addr))
				return (ifa);
		}
	}
	return (NULL);
}

/*
 * Locate the point to point interface with a given destination address.
 */
struct ifaddr *
ifa_ifwithdstaddr(struct sockaddr *addr)
{
	const struct ifnet_array *arr;
	int i;

	arr = ifnet_array_get();
	for (i = 0; i < arr->ifnet_count; ++i) {
		struct ifnet *ifp = arr->ifnet_arr[i];
		struct ifaddr_container *ifac;

		if (!(ifp->if_flags & IFF_POINTOPOINT))
			continue;

		TAILQ_FOREACH(ifac, &ifp->if_addrheads[mycpuid], ifa_link) {
			struct ifaddr *ifa = ifac->ifa;

			if (ifa->ifa_addr->sa_family != addr->sa_family)
				continue;
			if (ifa->ifa_dstaddr &&
			    sa_equal(addr, ifa->ifa_dstaddr))
				return (ifa);
		}
	}
	return (NULL);
}

/*
 * Find an interface on a specific network.  If many, choice
 * is most specific found.
 */
struct ifaddr *
ifa_ifwithnet(struct sockaddr *addr)
{
	struct ifaddr *ifa_maybe = NULL;
	u_int af = addr->sa_family;
	char *addr_data = addr->sa_data, *cplim;
	const struct ifnet_array *arr;
	int i;

	/*
	 * AF_LINK addresses can be looked up directly by their index number,
	 * so do that if we can.
	 */
	if (af == AF_LINK) {
		struct sockaddr_dl *sdl = (struct sockaddr_dl *)addr;

		if (sdl->sdl_index && sdl->sdl_index <= if_index)
			return (ifindex2ifnet[sdl->sdl_index]->if_lladdr);
	}

	/*
	 * Scan though each interface, looking for ones that have
	 * addresses in this address family.
	 */
	arr = ifnet_array_get();
	for (i = 0; i < arr->ifnet_count; ++i) {
		struct ifnet *ifp = arr->ifnet_arr[i];
		struct ifaddr_container *ifac;

		TAILQ_FOREACH(ifac, &ifp->if_addrheads[mycpuid], ifa_link) {
			struct ifaddr *ifa = ifac->ifa;
			char *cp, *cp2, *cp3;

			if (ifa->ifa_addr->sa_family != af)
next:				continue;
			if (af == AF_INET && ifp->if_flags & IFF_POINTOPOINT) {
				/*
				 * This is a bit broken as it doesn't
				 * take into account that the remote end may
				 * be a single node in the network we are
				 * looking for.
				 * The trouble is that we don't know the
				 * netmask for the remote end.
				 */
				if (ifa->ifa_dstaddr != NULL &&
				    sa_equal(addr, ifa->ifa_dstaddr))
					return (ifa);
			} else {
				/*
				 * if we have a special address handler,
				 * then use it instead of the generic one.
				 */
				if (ifa->ifa_claim_addr) {
					if ((*ifa->ifa_claim_addr)(ifa, addr)) {
						return (ifa);
					} else {
						continue;
					}
				}

				/*
				 * Scan all the bits in the ifa's address.
				 * If a bit dissagrees with what we are
				 * looking for, mask it with the netmask
				 * to see if it really matters.
				 * (A byte at a time)
				 */
				if (ifa->ifa_netmask == 0)
					continue;
				cp = addr_data;
				cp2 = ifa->ifa_addr->sa_data;
				cp3 = ifa->ifa_netmask->sa_data;
				cplim = ifa->ifa_netmask->sa_len +
					(char *)ifa->ifa_netmask;
				while (cp3 < cplim)
					if ((*cp++ ^ *cp2++) & *cp3++)
						goto next; /* next address! */
				/*
				 * If the netmask of what we just found
				 * is more specific than what we had before
				 * (if we had one) then remember the new one
				 * before continuing to search for an even
				 * better one.  If the netmasks are equal,
				 * we prefer the this ifa based on the result
				 * of ifa_prefer().
				 */
				if (ifa_maybe == NULL ||
				    rn_refines((char *)ifa->ifa_netmask,
				        (char *)ifa_maybe->ifa_netmask) ||
				    (sa_equal(ifa_maybe->ifa_netmask,
				        ifa->ifa_netmask) &&
				     ifa_prefer(ifa, ifa_maybe)))
					ifa_maybe = ifa;
			}
		}
	}
	return (ifa_maybe);
}

/*
 * Find an interface address specific to an interface best matching
 * a given address.
 */
struct ifaddr *
ifaof_ifpforaddr(struct sockaddr *addr, struct ifnet *ifp)
{
	struct ifaddr_container *ifac;
	char *cp, *cp2, *cp3;
	char *cplim;
	struct ifaddr *ifa_maybe = NULL;
	u_int af = addr->sa_family;

	if (af >= AF_MAX)
		return (0);
	TAILQ_FOREACH(ifac, &ifp->if_addrheads[mycpuid], ifa_link) {
		struct ifaddr *ifa = ifac->ifa;

		if (ifa->ifa_addr->sa_family != af)
			continue;
		if (ifa_maybe == NULL)
			ifa_maybe = ifa;
		if (ifa->ifa_netmask == NULL) {
			if (sa_equal(addr, ifa->ifa_addr) ||
			    (ifa->ifa_dstaddr != NULL &&
			     sa_equal(addr, ifa->ifa_dstaddr)))
				return (ifa);
			continue;
		}
		if (ifp->if_flags & IFF_POINTOPOINT) {
			if (sa_equal(addr, ifa->ifa_dstaddr))
				return (ifa);
		} else {
			cp = addr->sa_data;
			cp2 = ifa->ifa_addr->sa_data;
			cp3 = ifa->ifa_netmask->sa_data;
			cplim = ifa->ifa_netmask->sa_len + (char *)ifa->ifa_netmask;
			for (; cp3 < cplim; cp3++)
				if ((*cp++ ^ *cp2++) & *cp3)
					break;
			if (cp3 == cplim)
				return (ifa);
		}
	}
	return (ifa_maybe);
}

/*
 * Default action when installing a route with a Link Level gateway.
 * Lookup an appropriate real ifa to point to.
 * This should be moved to /sys/net/link.c eventually.
 */
static void
link_rtrequest(int cmd, struct rtentry *rt)
{
	struct ifaddr *ifa;
	struct sockaddr *dst;
	struct ifnet *ifp;

	if (cmd != RTM_ADD || (ifa = rt->rt_ifa) == NULL ||
	    (ifp = ifa->ifa_ifp) == NULL || (dst = rt_key(rt)) == NULL)
		return;
	ifa = ifaof_ifpforaddr(dst, ifp);
	if (ifa != NULL) {
		IFAFREE(rt->rt_ifa);
		IFAREF(ifa);
		rt->rt_ifa = ifa;
		if (ifa->ifa_rtrequest && ifa->ifa_rtrequest != link_rtrequest)
			ifa->ifa_rtrequest(cmd, rt);
	}
}

struct netmsg_if {
	struct netmsg_base	base;
	struct ifnet		*ifp;
};

/*
 * Mark an interface down and notify protocols of the transition.
 */
static void
if_down_dispatch(netmsg_t nmsg)
{
	struct netmsg_if *msg = (struct netmsg_if *)nmsg;
	struct ifnet *ifp = msg->ifp;
	struct ifaddr_container *ifac;
	struct domain *dp;

	ASSERT_NETISR0;

	ifp->if_flags &= ~IFF_UP;
	getmicrotime(&ifp->if_lastchange);
	rt_ifmsg(ifp);

	/*
	 * The ifaddr processing in the following loop will block,
	 * however, this function is called in netisr0, in which
	 * ifaddr list changes happen, so we don't care about the
	 * blockness of the ifaddr processing here.
	 */
	TAILQ_FOREACH(ifac, &ifp->if_addrheads[mycpuid], ifa_link) {
		struct ifaddr *ifa = ifac->ifa;

		/* Ignore marker */
		if (ifa->ifa_addr->sa_family == AF_UNSPEC)
			continue;

		kpfctlinput(PRC_IFDOWN, ifa->ifa_addr);
	}

	SLIST_FOREACH(dp, &domains, dom_next)
		if (dp->dom_if_down != NULL)
			dp->dom_if_down(ifp);

	ifq_purge_all(&ifp->if_snd);
	netisr_replymsg(&nmsg->base, 0);
}

/*
 * Mark an interface up and notify protocols of the transition.
 */
static void
if_up_dispatch(netmsg_t nmsg)
{
	struct netmsg_if *msg = (struct netmsg_if *)nmsg;
	struct ifnet *ifp = msg->ifp;
	struct ifaddr_container *ifac;
	struct domain *dp;

	ASSERT_NETISR0;

	ifq_purge_all(&ifp->if_snd);
	ifp->if_flags |= IFF_UP;
	getmicrotime(&ifp->if_lastchange);
	rt_ifmsg(ifp);

	/*
	 * The ifaddr processing in the following loop will block,
	 * however, this function is called in netisr0, in which
	 * ifaddr list changes happen, so we don't care about the
	 * blockness of the ifaddr processing here.
	 */
	TAILQ_FOREACH(ifac, &ifp->if_addrheads[mycpuid], ifa_link) {
		struct ifaddr *ifa = ifac->ifa;

		/* Ignore marker */
		if (ifa->ifa_addr->sa_family == AF_UNSPEC)
			continue;

		kpfctlinput(PRC_IFUP, ifa->ifa_addr);
	}

	SLIST_FOREACH(dp, &domains, dom_next)
		if (dp->dom_if_up != NULL)
			dp->dom_if_up(ifp);

	netisr_replymsg(&nmsg->base, 0);
}

/*
 * Mark an interface down and notify protocols of the transition.  An
 * interface going down is also considered to be a synchronizing event.
 * We must ensure that all packet processing related to the interface
 * has completed before we return so e.g. the caller can free the ifnet
 * structure that the mbufs may be referencing.
 *
 * NOTE: must be called at splnet or eqivalent.
 */
void
if_down(struct ifnet *ifp)
{
	struct netmsg_if msg;

	EVENTHANDLER_INVOKE(ifnet_event, ifp, IFNET_EVENT_DOWN);
	netmsg_init(&msg.base, NULL, &curthread->td_msgport, 0,
	    if_down_dispatch);
	msg.ifp = ifp;
	netisr_domsg(&msg.base, 0);
	netmsg_service_sync();
}

/*
 * Mark an interface up and notify protocols of
 * the transition.
 * NOTE: must be called at splnet or eqivalent.
 */
void
if_up(struct ifnet *ifp)
{
	struct netmsg_if msg;

	netmsg_init(&msg.base, NULL, &curthread->td_msgport, 0,
	    if_up_dispatch);
	msg.ifp = ifp;
	netisr_domsg(&msg.base, 0);
	EVENTHANDLER_INVOKE(ifnet_event, ifp, IFNET_EVENT_UP);
}

/*
 * Process a link state change.
 * NOTE: must be called at splsoftnet or equivalent.
 */
void
if_link_state_change(struct ifnet *ifp)
{
	int link_state = ifp->if_link_state;

	rt_ifmsg(ifp);
	devctl_notify("IFNET", ifp->if_xname,
	    (link_state == LINK_STATE_UP) ? "LINK_UP" : "LINK_DOWN", NULL);

	EVENTHANDLER_INVOKE(ifnet_link_event, ifp, link_state);
}

/*
 * Handle interface watchdog timer routines.  Called
 * from softclock, we decrement timers (if set) and
 * call the appropriate interface routine on expiration.
 */
static void
if_slowtimo_dispatch(netmsg_t nmsg)
{
	struct globaldata *gd = mycpu;
	const struct ifnet_array *arr;
	int i;

	ASSERT_NETISR0;

	crit_enter_gd(gd);
	lwkt_replymsg(&nmsg->lmsg, 0);  /* reply ASAP */
	crit_exit_gd(gd);

	arr = ifnet_array_get();
	for (i = 0; i < arr->ifnet_count; ++i) {
		struct ifnet *ifp = arr->ifnet_arr[i];

		crit_enter_gd(gd);

		if (if_stats_compat) {
			IFNET_STAT_GET(ifp, ipackets, ifp->if_ipackets);
			IFNET_STAT_GET(ifp, ierrors, ifp->if_ierrors);
			IFNET_STAT_GET(ifp, opackets, ifp->if_opackets);
			IFNET_STAT_GET(ifp, oerrors, ifp->if_oerrors);
			IFNET_STAT_GET(ifp, collisions, ifp->if_collisions);
			IFNET_STAT_GET(ifp, ibytes, ifp->if_ibytes);
			IFNET_STAT_GET(ifp, obytes, ifp->if_obytes);
			IFNET_STAT_GET(ifp, imcasts, ifp->if_imcasts);
			IFNET_STAT_GET(ifp, omcasts, ifp->if_omcasts);
			IFNET_STAT_GET(ifp, iqdrops, ifp->if_iqdrops);
			IFNET_STAT_GET(ifp, noproto, ifp->if_noproto);
			IFNET_STAT_GET(ifp, oqdrops, ifp->if_oqdrops);
		}

		if (ifp->if_timer == 0 || --ifp->if_timer) {
			crit_exit_gd(gd);
			continue;
		}
		if (ifp->if_watchdog) {
			if (ifnet_tryserialize_all(ifp)) {
				(*ifp->if_watchdog)(ifp);
				ifnet_deserialize_all(ifp);
			} else {
				/* try again next timeout */
				++ifp->if_timer;
			}
		}

		crit_exit_gd(gd);
	}

	callout_reset(&if_slowtimo_timer, hz / IFNET_SLOWHZ, if_slowtimo, NULL);
}

static void
if_slowtimo(void *arg __unused)
{
	struct lwkt_msg *lmsg = &if_slowtimo_netmsg.lmsg;

	KASSERT(mycpuid == 0, ("not on cpu0"));
	crit_enter();
	if (lmsg->ms_flags & MSGF_DONE)
		lwkt_sendmsg_oncpu(netisr_cpuport(0), lmsg);
	crit_exit();
}

/*
 * Map interface name to
 * interface structure pointer.
 */
struct ifnet *
ifunit(const char *name)
{
	struct ifnet *ifp;

	/*
	 * Search all the interfaces for this name/number
	 */
	KASSERT(mtx_owned(&ifnet_mtx), ("ifnet is not locked"));

	TAILQ_FOREACH(ifp, &ifnetlist, if_link) {
		if (strncmp(ifp->if_xname, name, IFNAMSIZ) == 0)
			break;
	}
	return (ifp);
}

struct ifnet *
ifunit_netisr(const char *name)
{
	const struct ifnet_array *arr;
	int i;

	/*
	 * Search all the interfaces for this name/number
	 */

	arr = ifnet_array_get();
	for (i = 0; i < arr->ifnet_count; ++i) {
		struct ifnet *ifp = arr->ifnet_arr[i];

		if (strncmp(ifp->if_xname, name, IFNAMSIZ) == 0)
			return ifp;
	}
	return NULL;
}

/*
 * Interface ioctls.
 */
int
ifioctl(struct socket *so, u_long cmd, caddr_t data, struct ucred *cred)
{
	struct ifnet *ifp;
	struct ifgroupreq *ifgr;
	struct ifreq *ifr;
	struct ifstat *ifs;
	int error, do_ifup = 0;
	short oif_flags;
	int new_flags;
	size_t namelen, onamelen;
	char new_name[IFNAMSIZ];
	struct ifaddr *ifa;
	struct sockaddr_dl *sdl;

	switch (cmd) {
	case SIOCGIFCONF:
		return (ifconf(cmd, data, cred));
	default:
		break;
	}

	ifr = (struct ifreq *)data;

	switch (cmd) {
	case SIOCIFCREATE:
	case SIOCIFCREATE2:
		if ((error = priv_check_cred(cred, PRIV_ROOT, 0)) != 0)
			return (error);
		return (if_clone_create(ifr->ifr_name, sizeof(ifr->ifr_name),
			(cmd == SIOCIFCREATE2 ? ifr->ifr_data : NULL), NULL));
	case SIOCIFDESTROY:
		if ((error = priv_check_cred(cred, PRIV_ROOT, 0)) != 0)
			return (error);
		return (if_clone_destroy(ifr->ifr_name));
	case SIOCIFGCLONERS:
		return (if_clone_list((struct if_clonereq *)data));
	case SIOCGIFGMEMB:
		return (if_getgroupmembers((struct ifgroupreq *)data));
	default:
		break;
	}

	/*
	 * Nominal ioctl through interface, lookup the ifp and obtain a
	 * lock to serialize the ifconfig ioctl operation.
	 */
	ifnet_lock();

	ifp = ifunit(ifr->ifr_name);
	if (ifp == NULL) {
		ifnet_unlock();
		return (ENXIO);
	}
	error = 0;

	switch (cmd) {
	case SIOCGIFINDEX:
		ifr->ifr_index = ifp->if_index;
		break;

	case SIOCGIFFLAGS:
		ifr->ifr_flags = ifp->if_flags;
		ifr->ifr_flagshigh = ifp->if_flags >> 16;
		break;

	case SIOCGIFCAP:
		ifr->ifr_reqcap = ifp->if_capabilities;
		ifr->ifr_curcap = ifp->if_capenable;
		break;

	case SIOCGIFMETRIC:
		ifr->ifr_metric = ifp->if_metric;
		break;

	case SIOCGIFMTU:
		ifr->ifr_mtu = ifp->if_mtu;
		break;

	case SIOCGIFTSOLEN:
		ifr->ifr_tsolen = ifp->if_tsolen;
		break;

	case SIOCGIFDATA:
		error = copyout((caddr_t)&ifp->if_data, ifr->ifr_data,
				sizeof(ifp->if_data));
		break;

	case SIOCGIFPHYS:
		ifr->ifr_phys = ifp->if_physical;
		break;

	case SIOCGIFPOLLCPU:
		ifr->ifr_pollcpu = -1;
		break;

	case SIOCSIFPOLLCPU:
		break;

	case SIOCSIFFLAGS:
		error = priv_check_cred(cred, PRIV_ROOT, 0);
		if (error)
			break;
		new_flags = (ifr->ifr_flags & 0xffff) |
		    (ifr->ifr_flagshigh << 16);
		if (ifp->if_flags & IFF_SMART) {
			/* Smart drivers twiddle their own routes */
		} else if (ifp->if_flags & IFF_UP &&
		    (new_flags & IFF_UP) == 0) {
			if_down(ifp);
		} else if (new_flags & IFF_UP &&
		    (ifp->if_flags & IFF_UP) == 0) {
			do_ifup = 1;
		}

#ifdef IFPOLL_ENABLE
		if ((new_flags ^ ifp->if_flags) & IFF_NPOLLING) {
			if (new_flags & IFF_NPOLLING)
				ifpoll_register(ifp);
			else
				ifpoll_deregister(ifp);
		}
#endif

		ifp->if_flags = (ifp->if_flags & IFF_CANTCHANGE) |
			(new_flags &~ IFF_CANTCHANGE);
		if (new_flags & IFF_PPROMISC) {
			/* Permanently promiscuous mode requested */
			ifp->if_flags |= IFF_PROMISC;
		} else if (ifp->if_pcount == 0) {
			ifp->if_flags &= ~IFF_PROMISC;
		}
		if (ifp->if_ioctl) {
			ifnet_serialize_all(ifp);
			ifp->if_ioctl(ifp, cmd, data, cred);
			ifnet_deserialize_all(ifp);
		}
		if (do_ifup)
			if_up(ifp);
		getmicrotime(&ifp->if_lastchange);
		break;

	case SIOCSIFCAP:
		error = priv_check_cred(cred, PRIV_ROOT, 0);
		if (error)
			break;
		if (ifr->ifr_reqcap & ~ifp->if_capabilities) {
			error = EINVAL;
			break;
		}
		ifnet_serialize_all(ifp);
		ifp->if_ioctl(ifp, cmd, data, cred);
		ifnet_deserialize_all(ifp);
		break;

	case SIOCSIFNAME:
		error = priv_check_cred(cred, PRIV_ROOT, 0);
		if (error)
			break;
		error = copyinstr(ifr->ifr_data, new_name, IFNAMSIZ, NULL);
		if (error)
			break;
		if (new_name[0] == '\0') {
			error = EINVAL;
			break;
		}
		if (ifunit(new_name) != NULL) {
			error = EEXIST;
			break;
		}

		EVENTHANDLER_INVOKE(ifnet_detach_event, ifp);

		/* Announce the departure of the interface. */
		rt_ifannouncemsg(ifp, IFAN_DEPARTURE);

		strlcpy(ifp->if_xname, new_name, sizeof(ifp->if_xname));
		ifa = TAILQ_FIRST(&ifp->if_addrheads[mycpuid])->ifa;
		sdl = (struct sockaddr_dl *)ifa->ifa_addr;
		namelen = strlen(new_name);
		onamelen = sdl->sdl_nlen;
		/*
		 * Move the address if needed.  This is safe because we
		 * allocate space for a name of length IFNAMSIZ when we
		 * create this in if_attach().
		 */
		if (namelen != onamelen) {
			bcopy(sdl->sdl_data + onamelen,
			    sdl->sdl_data + namelen, sdl->sdl_alen);
		}
		bcopy(new_name, sdl->sdl_data, namelen);
		sdl->sdl_nlen = namelen;
		sdl = (struct sockaddr_dl *)ifa->ifa_netmask;
		bzero(sdl->sdl_data, onamelen);
		while (namelen != 0)
			sdl->sdl_data[--namelen] = 0xff;

		EVENTHANDLER_INVOKE(ifnet_attach_event, ifp);

		/* Announce the return of the interface. */
		rt_ifannouncemsg(ifp, IFAN_ARRIVAL);
		break;

	case SIOCSIFMETRIC:
		error = priv_check_cred(cred, PRIV_ROOT, 0);
		if (error)
			break;
		ifp->if_metric = ifr->ifr_metric;
		getmicrotime(&ifp->if_lastchange);
		break;

	case SIOCSIFPHYS:
		error = priv_check_cred(cred, PRIV_ROOT, 0);
		if (error)
			break;
		if (ifp->if_ioctl == NULL) {
		        error = EOPNOTSUPP;
			break;
		}
		ifnet_serialize_all(ifp);
		error = ifp->if_ioctl(ifp, cmd, data, cred);
		ifnet_deserialize_all(ifp);
		if (error == 0)
			getmicrotime(&ifp->if_lastchange);
		break;

	case SIOCSIFMTU:
	{
		u_long oldmtu = ifp->if_mtu;

		error = priv_check_cred(cred, PRIV_ROOT, 0);
		if (error)
			break;
		if (ifp->if_ioctl == NULL) {
			error = EOPNOTSUPP;
			break;
		}
		if (ifr->ifr_mtu < IF_MINMTU || ifr->ifr_mtu > IF_MAXMTU) {
			error = EINVAL;
			break;
		}
		ifnet_serialize_all(ifp);
		error = ifp->if_ioctl(ifp, cmd, data, cred);
		ifnet_deserialize_all(ifp);
		if (error == 0) {
			getmicrotime(&ifp->if_lastchange);
			rt_ifmsg(ifp);
		}
		/*
		 * If the link MTU changed, do network layer specific procedure.
		 */
		if (ifp->if_mtu != oldmtu) {
#ifdef INET6
			nd6_setmtu(ifp);
#endif
		}
		break;
	}

	case SIOCSIFTSOLEN:
		error = priv_check_cred(cred, PRIV_ROOT, 0);
		if (error)
			break;

		/* XXX need driver supplied upper limit */
		if (ifr->ifr_tsolen <= 0) {
			error = EINVAL;
			break;
		}
		ifp->if_tsolen = ifr->ifr_tsolen;
		break;

	case SIOCADDMULTI:
	case SIOCDELMULTI:
		error = priv_check_cred(cred, PRIV_ROOT, 0);
		if (error)
			break;

		/* Don't allow group membership on non-multicast interfaces. */
		if ((ifp->if_flags & IFF_MULTICAST) == 0) {
			error = EOPNOTSUPP;
			break;
		}

		/* Don't let users screw up protocols' entries. */
		if (ifr->ifr_addr.sa_family != AF_LINK) {
			error = EINVAL;
			break;
		}

		if (cmd == SIOCADDMULTI) {
			struct ifmultiaddr *ifma;
			error = if_addmulti(ifp, &ifr->ifr_addr, &ifma);
		} else {
			error = if_delmulti(ifp, &ifr->ifr_addr);
		}
		if (error == 0)
			getmicrotime(&ifp->if_lastchange);
		break;

	case SIOCSIFPHYADDR:
	case SIOCDIFPHYADDR:
#ifdef INET6
	case SIOCSIFPHYADDR_IN6:
#endif
	case SIOCSLIFPHYADDR:
	case SIOCSIFMEDIA:
	case SIOCSIFGENERIC:
		error = priv_check_cred(cred, PRIV_ROOT, 0);
		if (error)
			break;
		if (ifp->if_ioctl == NULL) {
			error = EOPNOTSUPP;
			break;
		}
		ifnet_serialize_all(ifp);
		error = ifp->if_ioctl(ifp, cmd, data, cred);
		ifnet_deserialize_all(ifp);
		if (error == 0)
			getmicrotime(&ifp->if_lastchange);
		break;

	case SIOCGIFSTATUS:
		ifs = (struct ifstat *)data;
		ifs->ascii[0] = '\0';
		/* fall through */
	case SIOCGIFPSRCADDR:
	case SIOCGIFPDSTADDR:
	case SIOCGLIFPHYADDR:
	case SIOCGIFMEDIA:
	case SIOCGIFGENERIC:
		if (ifp->if_ioctl == NULL) {
			error = EOPNOTSUPP;
			break;
		}
		ifnet_serialize_all(ifp);
		error = ifp->if_ioctl(ifp, cmd, data, cred);
		ifnet_deserialize_all(ifp);
		break;

	case SIOCSIFLLADDR:
		error = priv_check_cred(cred, PRIV_ROOT, 0);
		if (error)
			break;
		error = if_setlladdr(ifp, ifr->ifr_addr.sa_data,
				     ifr->ifr_addr.sa_len);
		EVENTHANDLER_INVOKE(iflladdr_event, ifp);
		break;

	case SIOCAIFGROUP:
		ifgr = (struct ifgroupreq *)ifr;
		if ((error = priv_check_cred(cred, PRIV_NET_ADDIFGROUP, 0)))
			return (error);
		if ((error = if_addgroup(ifp, ifgr->ifgr_group)))
			return (error);
		break;

	case SIOCDIFGROUP:
		ifgr = (struct ifgroupreq *)ifr;
		if ((error = priv_check_cred(cred, PRIV_NET_DELIFGROUP, 0)))
			return (error);
		if ((error = if_delgroup(ifp, ifgr->ifgr_group)))
			return (error);
		break;

	case SIOCGIFGROUP:
		ifgr = (struct ifgroupreq *)ifr;
		if ((error = if_getgroups(ifgr, ifp)))
			return (error);
		break;

	default:
		oif_flags = ifp->if_flags;
		if (so->so_proto == 0) {
			error = EOPNOTSUPP;
			break;
		}
		error = so_pru_control_direct(so, cmd, data, ifp);

		/*
		 * If the socket control method returns EOPNOTSUPP, pass the
		 * request directly to the interface.
		 *
		 * Exclude the SIOCSIF{ADDR,BRDADDR,DSTADDR,NETMASK} ioctls,
		 * because drivers may trust these ioctls to come from an
		 * already privileged layer and thus do not perform credentials
		 * checks or input validation.
		 */
		if (error == EOPNOTSUPP &&
		    ifp->if_ioctl != NULL &&
		    cmd != SIOCSIFADDR &&
		    cmd != SIOCSIFBRDADDR &&
		    cmd != SIOCSIFDSTADDR &&
		    cmd != SIOCSIFNETMASK) {
			ifnet_serialize_all(ifp);
			error = ifp->if_ioctl(ifp, cmd, data, cred);
			ifnet_deserialize_all(ifp);
		}

		if ((oif_flags ^ ifp->if_flags) & IFF_UP) {
#ifdef INET6
			DELAY(100);/* XXX: temporary workaround for fxp issue*/
			if (ifp->if_flags & IFF_UP) {
				crit_enter();
				in6_if_up(ifp);
				crit_exit();
			}
#endif
		}
		break;
	}

	ifnet_unlock();
	return (error);
}

/*
 * Set/clear promiscuous mode on interface ifp based on the truth value
 * of pswitch.  The calls are reference counted so that only the first
 * "on" request actually has an effect, as does the final "off" request.
 * Results are undefined if the "off" and "on" requests are not matched.
 */
int
ifpromisc(struct ifnet *ifp, int pswitch)
{
	struct ifreq ifr;
	int error;
	int oldflags;

	oldflags = ifp->if_flags;
	if (ifp->if_flags & IFF_PPROMISC) {
		/* Do nothing if device is in permanently promiscuous mode */
		ifp->if_pcount += pswitch ? 1 : -1;
		return (0);
	}
	if (pswitch) {
		/*
		 * If the device is not configured up, we cannot put it in
		 * promiscuous mode.
		 */
		if ((ifp->if_flags & IFF_UP) == 0)
			return (ENETDOWN);
		if (ifp->if_pcount++ != 0)
			return (0);
		ifp->if_flags |= IFF_PROMISC;
		log(LOG_INFO, "%s: promiscuous mode enabled\n",
		    ifp->if_xname);
	} else {
		if (--ifp->if_pcount > 0)
			return (0);
		ifp->if_flags &= ~IFF_PROMISC;
		log(LOG_INFO, "%s: promiscuous mode disabled\n",
		    ifp->if_xname);
	}
	ifr.ifr_flags = ifp->if_flags;
	ifr.ifr_flagshigh = ifp->if_flags >> 16;
	ifnet_serialize_all(ifp);
	error = ifp->if_ioctl(ifp, SIOCSIFFLAGS, (caddr_t)&ifr, NULL);
	ifnet_deserialize_all(ifp);
	if (error == 0)
		rt_ifmsg(ifp);
	else
		ifp->if_flags = oldflags;
	return error;
}

/*
 * Return interface configuration
 * of system.  List may be used
 * in later ioctl's (above) to get
 * other information.
 */
static int
ifconf(u_long cmd, caddr_t data, struct ucred *cred)
{
	struct ifconf *ifc = (struct ifconf *)data;
	struct ifnet *ifp;
	struct sockaddr *sa;
	struct ifreq ifr, *ifrp;
	int space = ifc->ifc_len, error = 0;

	ifrp = ifc->ifc_req;

	ifnet_lock();
	TAILQ_FOREACH(ifp, &ifnetlist, if_link) {
		struct ifaddr_container *ifac, *ifac_mark;
		struct ifaddr_marker mark;
		struct ifaddrhead *head;
		int addrs;

		if (space <= sizeof ifr)
			break;

		/*
		 * Zero the stack declared structure first to prevent
		 * memory disclosure.
		 */
		bzero(&ifr, sizeof(ifr));
		if (strlcpy(ifr.ifr_name, ifp->if_xname, sizeof(ifr.ifr_name))
		    >= sizeof(ifr.ifr_name)) {
			error = ENAMETOOLONG;
			break;
		}

		/*
		 * Add a marker, since copyout() could block and during that
		 * period the list could be changed.  Inserting the marker to
		 * the header of the list will not cause trouble for the code
		 * assuming that the first element of the list is AF_LINK; the
		 * marker will be moved to the next position w/o blocking.
		 */
		ifa_marker_init(&mark, ifp);
		ifac_mark = &mark.ifac;
		head = &ifp->if_addrheads[mycpuid];

		addrs = 0;
		TAILQ_INSERT_HEAD(head, ifac_mark, ifa_link);
		while ((ifac = TAILQ_NEXT(ifac_mark, ifa_link)) != NULL) {
			struct ifaddr *ifa = ifac->ifa;

			TAILQ_REMOVE(head, ifac_mark, ifa_link);
			TAILQ_INSERT_AFTER(head, ifac, ifac_mark, ifa_link);

			/* Ignore marker */
			if (ifa->ifa_addr->sa_family == AF_UNSPEC)
				continue;

			if (space <= sizeof ifr)
				break;
			sa = ifa->ifa_addr;
			if (cred->cr_prison && prison_if(cred, sa))
				continue;
			addrs++;
			/*
			 * Keep a reference on this ifaddr, so that it will
			 * not be destroyed when its address is copied to
			 * the userland, which could block.
			 */
			IFAREF(ifa);
			if (sa->sa_len <= sizeof(*sa)) {
				ifr.ifr_addr = *sa;
				error = copyout(&ifr, ifrp, sizeof ifr);
				ifrp++;
			} else {
				if (space < (sizeof ifr) + sa->sa_len -
					    sizeof(*sa)) {
					IFAFREE(ifa);
					break;
				}
				space -= sa->sa_len - sizeof(*sa);
				error = copyout(&ifr, ifrp,
						sizeof ifr.ifr_name);
				if (error == 0)
					error = copyout(sa, &ifrp->ifr_addr,
							sa->sa_len);
				ifrp = (struct ifreq *)
					(sa->sa_len + (caddr_t)&ifrp->ifr_addr);
			}
			IFAFREE(ifa);
			if (error)
				break;
			space -= sizeof ifr;
		}
		TAILQ_REMOVE(head, ifac_mark, ifa_link);
		if (error)
			break;
		if (!addrs) {
			bzero(&ifr.ifr_addr, sizeof ifr.ifr_addr);
			error = copyout(&ifr, ifrp, sizeof ifr);
			if (error)
				break;
			space -= sizeof ifr;
			ifrp++;
		}
	}
	ifnet_unlock();

	ifc->ifc_len -= space;
	return (error);
}

/*
 * Just like if_promisc(), but for all-multicast-reception mode.
 */
int
if_allmulti(struct ifnet *ifp, int onswitch)
{
	int error = 0;
	struct ifreq ifr;

	crit_enter();

	if (onswitch) {
		if (ifp->if_amcount++ == 0) {
			ifp->if_flags |= IFF_ALLMULTI;
			ifr.ifr_flags = ifp->if_flags;
			ifr.ifr_flagshigh = ifp->if_flags >> 16;
			ifnet_serialize_all(ifp);
			error = ifp->if_ioctl(ifp, SIOCSIFFLAGS, (caddr_t)&ifr,
					      NULL);
			ifnet_deserialize_all(ifp);
		}
	} else {
		if (ifp->if_amcount > 1) {
			ifp->if_amcount--;
		} else {
			ifp->if_amcount = 0;
			ifp->if_flags &= ~IFF_ALLMULTI;
			ifr.ifr_flags = ifp->if_flags;
			ifr.ifr_flagshigh = ifp->if_flags >> 16;
			ifnet_serialize_all(ifp);
			error = ifp->if_ioctl(ifp, SIOCSIFFLAGS, (caddr_t)&ifr,
					      NULL);
			ifnet_deserialize_all(ifp);
		}
	}

	crit_exit();

	if (error == 0)
		rt_ifmsg(ifp);
	return error;
}

/*
 * Add a multicast listenership to the interface in question.
 * The link layer provides a routine which converts
 */
int
if_addmulti_serialized(struct ifnet *ifp, struct sockaddr *sa,
    struct ifmultiaddr **retifma)
{
	struct sockaddr *llsa, *dupsa;
	int error;
	struct ifmultiaddr *ifma;

	ASSERT_IFNET_SERIALIZED_ALL(ifp);

	/*
	 * If the matching multicast address already exists
	 * then don't add a new one, just add a reference
	 */
	TAILQ_FOREACH(ifma, &ifp->if_multiaddrs, ifma_link) {
		if (sa_equal(sa, ifma->ifma_addr)) {
			ifma->ifma_refcount++;
			if (retifma)
				*retifma = ifma;
			return 0;
		}
	}

	/*
	 * Give the link layer a chance to accept/reject it, and also
	 * find out which AF_LINK address this maps to, if it isn't one
	 * already.
	 */
	if (ifp->if_resolvemulti) {
		error = ifp->if_resolvemulti(ifp, &llsa, sa);
		if (error)
			return error;
	} else {
		llsa = NULL;
	}

	ifma = kmalloc(sizeof *ifma, M_IFMADDR, M_INTWAIT);
	dupsa = kmalloc(sa->sa_len, M_IFMADDR, M_INTWAIT);
	bcopy(sa, dupsa, sa->sa_len);

	ifma->ifma_addr = dupsa;
	ifma->ifma_lladdr = llsa;
	ifma->ifma_ifp = ifp;
	ifma->ifma_refcount = 1;
	ifma->ifma_protospec = NULL;
	rt_newmaddrmsg(RTM_NEWMADDR, ifma);

	TAILQ_INSERT_HEAD(&ifp->if_multiaddrs, ifma, ifma_link);
	if (retifma)
		*retifma = ifma;

	if (llsa != NULL) {
		TAILQ_FOREACH(ifma, &ifp->if_multiaddrs, ifma_link) {
			if (sa_equal(ifma->ifma_addr, llsa))
				break;
		}
		if (ifma) {
			ifma->ifma_refcount++;
		} else {
			ifma = kmalloc(sizeof *ifma, M_IFMADDR, M_INTWAIT);
			dupsa = kmalloc(llsa->sa_len, M_IFMADDR, M_INTWAIT);
			bcopy(llsa, dupsa, llsa->sa_len);
			ifma->ifma_addr = dupsa;
			ifma->ifma_ifp = ifp;
			ifma->ifma_refcount = 1;
			TAILQ_INSERT_HEAD(&ifp->if_multiaddrs, ifma, ifma_link);
		}
	}
	/*
	 * We are certain we have added something, so call down to the
	 * interface to let them know about it.
	 */
	if (ifp->if_ioctl)
		ifp->if_ioctl(ifp, SIOCADDMULTI, 0, NULL);

	return 0;
}

int
if_addmulti(struct ifnet *ifp, struct sockaddr *sa,
    struct ifmultiaddr **retifma)
{
	int error;

	ifnet_serialize_all(ifp);
	error = if_addmulti_serialized(ifp, sa, retifma);
	ifnet_deserialize_all(ifp);

	return error;
}

/*
 * Remove a reference to a multicast address on this interface.  Yell
 * if the request does not match an existing membership.
 */
static int
if_delmulti_serialized(struct ifnet *ifp, struct sockaddr *sa)
{
	struct ifmultiaddr *ifma;

	ASSERT_IFNET_SERIALIZED_ALL(ifp);

	TAILQ_FOREACH(ifma, &ifp->if_multiaddrs, ifma_link)
		if (sa_equal(sa, ifma->ifma_addr))
			break;
	if (ifma == NULL)
		return ENOENT;

	if (ifma->ifma_refcount > 1) {
		ifma->ifma_refcount--;
		return 0;
	}

	rt_newmaddrmsg(RTM_DELMADDR, ifma);
	sa = ifma->ifma_lladdr;
	TAILQ_REMOVE(&ifp->if_multiaddrs, ifma, ifma_link);
	/*
	 * Make sure the interface driver is notified
	 * in the case of a link layer mcast group being left.
	 */
	if (ifma->ifma_addr->sa_family == AF_LINK && sa == NULL)
		ifp->if_ioctl(ifp, SIOCDELMULTI, 0, NULL);
	kfree(ifma->ifma_addr, M_IFMADDR);
	kfree(ifma, M_IFMADDR);
	if (sa == NULL)
		return 0;

	/*
	 * Now look for the link-layer address which corresponds to
	 * this network address.  It had been squirreled away in
	 * ifma->ifma_lladdr for this purpose (so we don't have
	 * to call ifp->if_resolvemulti() again), and we saved that
	 * value in sa above.  If some nasty deleted the
	 * link-layer address out from underneath us, we can deal because
	 * the address we stored was is not the same as the one which was
	 * in the record for the link-layer address.  (So we don't complain
	 * in that case.)
	 */
	TAILQ_FOREACH(ifma, &ifp->if_multiaddrs, ifma_link)
		if (sa_equal(sa, ifma->ifma_addr))
			break;
	if (ifma == NULL)
		return 0;

	if (ifma->ifma_refcount > 1) {
		ifma->ifma_refcount--;
		return 0;
	}

	TAILQ_REMOVE(&ifp->if_multiaddrs, ifma, ifma_link);
	ifp->if_ioctl(ifp, SIOCDELMULTI, 0, NULL);
	kfree(ifma->ifma_addr, M_IFMADDR);
	kfree(sa, M_IFMADDR);
	kfree(ifma, M_IFMADDR);

	return 0;
}

int
if_delmulti(struct ifnet *ifp, struct sockaddr *sa)
{
	int error;

	ifnet_serialize_all(ifp);
	error = if_delmulti_serialized(ifp, sa);
	ifnet_deserialize_all(ifp);

	return error;
}

/*
 * Delete all multicast group membership for an interface.
 * Should be used to quickly flush all multicast filters.
 */
void
if_delallmulti_serialized(struct ifnet *ifp)
{
	struct ifmultiaddr *ifma, mark;
	struct sockaddr sa;

	ASSERT_IFNET_SERIALIZED_ALL(ifp);

	bzero(&sa, sizeof(sa));
	sa.sa_family = AF_UNSPEC;
	sa.sa_len = sizeof(sa);

	bzero(&mark, sizeof(mark));
	mark.ifma_addr = &sa;

	TAILQ_INSERT_HEAD(&ifp->if_multiaddrs, &mark, ifma_link);
	while ((ifma = TAILQ_NEXT(&mark, ifma_link)) != NULL) {
		TAILQ_REMOVE(&ifp->if_multiaddrs, &mark, ifma_link);
		TAILQ_INSERT_AFTER(&ifp->if_multiaddrs, ifma, &mark,
		    ifma_link);

		if (ifma->ifma_addr->sa_family == AF_UNSPEC)
			continue;

		if_delmulti_serialized(ifp, ifma->ifma_addr);
	}
	TAILQ_REMOVE(&ifp->if_multiaddrs, &mark, ifma_link);
}


/*
 * Set the link layer address on an interface.
 *
 * At this time we only support certain types of interfaces,
 * and we don't allow the length of the address to change.
 */
int
if_setlladdr(struct ifnet *ifp, const u_char *lladdr, int len)
{
	struct sockaddr_dl *sdl;
	struct ifreq ifr;

	sdl = IF_LLSOCKADDR(ifp);
	if (sdl == NULL)
		return (EINVAL);
	if (len != sdl->sdl_alen)	/* don't allow length to change */
		return (EINVAL);
	switch (ifp->if_type) {
	case IFT_ETHER:			/* these types use struct arpcom */
	case IFT_XETHER:
	case IFT_L2VLAN:
	case IFT_IEEE8023ADLAG:
		bcopy(lladdr, ((struct arpcom *)ifp->if_softc)->ac_enaddr, len);
		bcopy(lladdr, LLADDR(sdl), len);
		break;
	default:
		return (ENODEV);
	}
	/*
	 * If the interface is already up, we need
	 * to re-init it in order to reprogram its
	 * address filter.
	 */
	ifnet_serialize_all(ifp);
	if ((ifp->if_flags & IFF_UP) != 0) {
#ifdef INET
		struct ifaddr_container *ifac;
#endif

		ifp->if_flags &= ~IFF_UP;
		ifr.ifr_flags = ifp->if_flags;
		ifr.ifr_flagshigh = ifp->if_flags >> 16;
		ifp->if_ioctl(ifp, SIOCSIFFLAGS, (caddr_t)&ifr,
			      NULL);
		ifp->if_flags |= IFF_UP;
		ifr.ifr_flags = ifp->if_flags;
		ifr.ifr_flagshigh = ifp->if_flags >> 16;
		ifp->if_ioctl(ifp, SIOCSIFFLAGS, (caddr_t)&ifr,
				 NULL);
#ifdef INET
		/*
		 * Also send gratuitous ARPs to notify other nodes about
		 * the address change.
		 */
		TAILQ_FOREACH(ifac, &ifp->if_addrheads[mycpuid], ifa_link) {
			struct ifaddr *ifa = ifac->ifa;

			if (ifa->ifa_addr != NULL &&
			    ifa->ifa_addr->sa_family == AF_INET)
				arp_gratuitous(ifp, ifa);
		}
#endif
	}
	ifnet_deserialize_all(ifp);
	return (0);
}


/*
 * Locate an interface based on a complete address.
 */
struct ifnet *
if_bylla(const void *lla, unsigned char lla_len)
{
	const struct ifnet_array *arr;
	struct ifnet *ifp;
	struct sockaddr_dl *sdl;
	int i;

	arr = ifnet_array_get();
	for (i = 0; i < arr->ifnet_count; ++i) {
		ifp = arr->ifnet_arr[i];
		if (ifp->if_addrlen != lla_len)
			continue;

		sdl = IF_LLSOCKADDR(ifp);
		if (memcmp(lla, LLADDR(sdl), lla_len) == 0)
			return (ifp);
	}
	return (NULL);
}

struct ifmultiaddr *
ifmaof_ifpforaddr(struct sockaddr *sa, struct ifnet *ifp)
{
	struct ifmultiaddr *ifma;

	/* TODO: need ifnet_serialize_main */
	ifnet_serialize_all(ifp);
	TAILQ_FOREACH(ifma, &ifp->if_multiaddrs, ifma_link)
		if (sa_equal(ifma->ifma_addr, sa))
			break;
	ifnet_deserialize_all(ifp);

	return ifma;
}

/*
 * This function locates the first real ethernet MAC from a network
 * card and loads it into node, returning 0 on success or ENOENT if
 * no suitable interfaces were found.  It is used by the uuid code to
 * generate a unique 6-byte number.
 */
int
if_getanyethermac(uint16_t *node, int minlen)
{
	struct ifnet *ifp;
	struct sockaddr_dl *sdl;

	ifnet_lock();
	TAILQ_FOREACH(ifp, &ifnetlist, if_link) {
		if (ifp->if_type != IFT_ETHER)
			continue;
		sdl = IF_LLSOCKADDR(ifp);
		if (sdl->sdl_alen < minlen)
			continue;
		bcopy(((struct arpcom *)ifp->if_softc)->ac_enaddr, node,
		      minlen);
		ifnet_unlock();
		return(0);
	}
	ifnet_unlock();
	return (ENOENT);
}

/*
 * The name argument must be a pointer to storage which will last as
 * long as the interface does.  For physical devices, the result of
 * device_get_name(dev) is a good choice and for pseudo-devices a
 * static string works well.
 */
void
if_initname(struct ifnet *ifp, const char *name, int unit)
{
	ifp->if_dname = name;
	ifp->if_dunit = unit;
	if (unit != IF_DUNIT_NONE)
		ksnprintf(ifp->if_xname, IFNAMSIZ, "%s%d", name, unit);
	else
		strlcpy(ifp->if_xname, name, IFNAMSIZ);
}

int
if_printf(struct ifnet *ifp, const char *fmt, ...)
{
	__va_list ap;
	int retval;

	retval = kprintf("%s: ", ifp->if_xname);
	__va_start(ap, fmt);
	retval += kvprintf(fmt, ap);
	__va_end(ap);
	return (retval);
}

struct ifnet *
if_alloc(uint8_t type)
{
	struct ifnet *ifp;
	size_t size;

	/*
	 * XXX temporary hack until arpcom is setup in if_l2com
	 */
	if (type == IFT_ETHER)
		size = sizeof(struct arpcom);
	else
		size = sizeof(struct ifnet);

	ifp = kmalloc(size, M_IFNET, M_WAITOK|M_ZERO);

	ifp->if_type = type;

	if (if_com_alloc[type] != NULL) {
		ifp->if_l2com = if_com_alloc[type](type, ifp);
		if (ifp->if_l2com == NULL) {
			kfree(ifp, M_IFNET);
			return (NULL);
		}
	}
	return (ifp);
}

void
if_free(struct ifnet *ifp)
{
	kfree(ifp, M_IFNET);
}

void
ifq_set_classic(struct ifaltq *ifq)
{
	ifq_set_methods(ifq, ifq->altq_ifp->if_mapsubq,
	    ifsq_classic_enqueue, ifsq_classic_dequeue, ifsq_classic_request);
}

void
ifq_set_methods(struct ifaltq *ifq, altq_mapsubq_t mapsubq,
    ifsq_enqueue_t enqueue, ifsq_dequeue_t dequeue, ifsq_request_t request)
{
	int q;

	KASSERT(mapsubq != NULL, ("mapsubq is not specified"));
	KASSERT(enqueue != NULL, ("enqueue is not specified"));
	KASSERT(dequeue != NULL, ("dequeue is not specified"));
	KASSERT(request != NULL, ("request is not specified"));

	ifq->altq_mapsubq = mapsubq;
	for (q = 0; q < ifq->altq_subq_cnt; ++q) {
		struct ifaltq_subque *ifsq = &ifq->altq_subq[q];

		ifsq->ifsq_enqueue = enqueue;
		ifsq->ifsq_dequeue = dequeue;
		ifsq->ifsq_request = request;
	}
}

static void
ifsq_norm_enqueue(struct ifaltq_subque *ifsq, struct mbuf *m)
{

	classq_add(&ifsq->ifsq_norm, m);
	ALTQ_SQ_CNTR_INC(ifsq, m->m_pkthdr.len);
}

static void
ifsq_prio_enqueue(struct ifaltq_subque *ifsq, struct mbuf *m)
{

	classq_add(&ifsq->ifsq_prio, m);
	ALTQ_SQ_CNTR_INC(ifsq, m->m_pkthdr.len);
	ALTQ_SQ_PRIO_CNTR_INC(ifsq, m->m_pkthdr.len);
}

static struct mbuf *
ifsq_norm_dequeue(struct ifaltq_subque *ifsq)
{
	struct mbuf *m;

	m = classq_get(&ifsq->ifsq_norm);
	if (m != NULL)
		ALTQ_SQ_CNTR_DEC(ifsq, m->m_pkthdr.len);
	return (m);
}

static struct mbuf *
ifsq_prio_dequeue(struct ifaltq_subque *ifsq)
{
	struct mbuf *m;

	m = classq_get(&ifsq->ifsq_prio);
	if (m != NULL) {
		ALTQ_SQ_CNTR_DEC(ifsq, m->m_pkthdr.len);
		ALTQ_SQ_PRIO_CNTR_DEC(ifsq, m->m_pkthdr.len);
	}
	return (m);
}

int
ifsq_classic_enqueue(struct ifaltq_subque *ifsq, struct mbuf *m,
    struct altq_pktattr *pa __unused)
{

	M_ASSERTPKTHDR(m);
again:
	if (ifsq->ifsq_len >= ifsq->ifsq_maxlen ||
	    ifsq->ifsq_bcnt >= ifsq->ifsq_maxbcnt) {
		struct mbuf *m_drop;

		if (m->m_flags & M_PRIO) {
			m_drop = NULL;
			if (ifsq->ifsq_prio_len < (ifsq->ifsq_maxlen >> 1) &&
			    ifsq->ifsq_prio_bcnt < (ifsq->ifsq_maxbcnt >> 1)) {
				/* Try dropping some from normal queue. */
				m_drop = ifsq_norm_dequeue(ifsq);
			}
			if (m_drop == NULL)
				m_drop = ifsq_prio_dequeue(ifsq);
		} else {
			m_drop = ifsq_norm_dequeue(ifsq);
		}
		if (m_drop != NULL) {
			IFNET_STAT_INC(ifsq->ifsq_ifp, oqdrops, 1);
			m_freem(m_drop);
			goto again;
		}
		/*
		 * No old packets could be dropped!
		 * NOTE: Caller increases oqdrops.
		 */
		m_freem(m);
		return (ENOBUFS);
	} else {
		if (m->m_flags & M_PRIO)
			ifsq_prio_enqueue(ifsq, m);
		else
			ifsq_norm_enqueue(ifsq, m);
		return (0);
	}
}

struct mbuf *
ifsq_classic_dequeue(struct ifaltq_subque *ifsq, int op)
{
	struct mbuf *m;

	switch (op) {
	case ALTDQ_POLL:
		m = classq_head(&ifsq->ifsq_prio);
		if (m == NULL)
			m = classq_head(&ifsq->ifsq_norm);
		break;

	case ALTDQ_REMOVE:
		m = ifsq_prio_dequeue(ifsq);
		if (m == NULL)
			m = ifsq_norm_dequeue(ifsq);
		break;

	default:
		panic("unsupported ALTQ dequeue op: %d", op);
	}
	return m;
}

int
ifsq_classic_request(struct ifaltq_subque *ifsq, int req, void *arg)
{
	switch (req) {
	case ALTRQ_PURGE:
		for (;;) {
			struct mbuf *m;

			m = ifsq_classic_dequeue(ifsq, ALTDQ_REMOVE);
			if (m == NULL)
				break;
			m_freem(m);
		}
		break;

	default:
		panic("unsupported ALTQ request: %d", req);
	}
	return 0;
}

static void
ifsq_ifstart_try(struct ifaltq_subque *ifsq, int force_sched)
{
	struct ifnet *ifp = ifsq_get_ifp(ifsq);
	int running = 0, need_sched;

	/*
	 * Try to do direct ifnet.if_start on the subqueue first, if there is
	 * contention on the subqueue hardware serializer, ifnet.if_start on
	 * the subqueue will be scheduled on the subqueue owner CPU.
	 */
	if (!ifsq_tryserialize_hw(ifsq)) {
		/*
		 * Subqueue hardware serializer contention happened,
		 * ifnet.if_start on the subqueue is scheduled on
		 * the subqueue owner CPU, and we keep going.
		 */
		ifsq_ifstart_schedule(ifsq, 1);
		return;
	}

	if ((ifp->if_flags & IFF_RUNNING) && !ifsq_is_oactive(ifsq)) {
		ifp->if_start(ifp, ifsq);
		if ((ifp->if_flags & IFF_RUNNING) && !ifsq_is_oactive(ifsq))
			running = 1;
	}
	need_sched = ifsq_ifstart_need_schedule(ifsq, running);

	ifsq_deserialize_hw(ifsq);

	if (need_sched) {
		/*
		 * More data need to be transmitted, ifnet.if_start on the
		 * subqueue is scheduled on the subqueue owner CPU, and we
		 * keep going.
		 * NOTE: ifnet.if_start subqueue interlock is not released.
		 */
		ifsq_ifstart_schedule(ifsq, force_sched);
	}
}

/*
 * Subqeue packets staging mechanism:
 *
 * The packets enqueued into the subqueue are staged to a certain amount
 * before the ifnet.if_start on the subqueue is called.  In this way, the
 * driver could avoid writing to hardware registers upon every packet,
 * instead, hardware registers could be written when certain amount of
 * packets are put onto hardware TX ring.  The measurement on several modern
 * NICs (emx(4), igb(4), bnx(4), bge(4), jme(4)) shows that the hardware
 * registers writing aggregation could save ~20% CPU time when 18bytes UDP
 * datagrams are transmitted at 1.48Mpps.  The performance improvement by
 * hardware registers writing aggeregation is also mentioned by Luigi Rizzo's
 * netmap paper (http://info.iet.unipi.it/~luigi/netmap/).
 *
 * Subqueue packets staging is performed for two entry points into drivers'
 * transmission function:
 * - Direct ifnet.if_start calling on the subqueue, i.e. ifsq_ifstart_try()
 * - ifnet.if_start scheduling on the subqueue, i.e. ifsq_ifstart_schedule()
 *
 * Subqueue packets staging will be stopped upon any of the following
 * conditions:
 * - If the count of packets enqueued on the current CPU is great than or
 *   equal to ifsq_stage_cntmax. (XXX this should be per-interface)
 * - If the total length of packets enqueued on the current CPU is great
 *   than or equal to the hardware's MTU - max_protohdr.  max_protohdr is
 *   cut from the hardware's MTU mainly bacause a full TCP segment's size
 *   is usually less than hardware's MTU.
 * - ifsq_ifstart_schedule() is not pending on the current CPU and
 *   ifnet.if_start subqueue interlock (ifaltq_subq.ifsq_started) is not
 *   released.
 * - The if_start_rollup(), which is registered as low priority netisr
 *   rollup function, is called; probably because no more work is pending
 *   for netisr.
 *
 * NOTE:
 * Currently subqueue packet staging is only performed in netisr threads.
 */
int
ifq_dispatch(struct ifnet *ifp, struct mbuf *m, struct altq_pktattr *pa)
{
	struct ifaltq *ifq = &ifp->if_snd;
	struct ifaltq_subque *ifsq;
	int error, start = 0, len, mcast = 0, avoid_start = 0;
	struct ifsubq_stage_head *head = NULL;
	struct ifsubq_stage *stage = NULL;
	struct globaldata *gd = mycpu;
	struct thread *td = gd->gd_curthread;

	crit_enter_quick(td);

	ifsq = ifq_map_subq(ifq, gd->gd_cpuid);
	ASSERT_ALTQ_SQ_NOT_SERIALIZED_HW(ifsq);

	len = m->m_pkthdr.len;
	if (m->m_flags & M_MCAST)
		mcast = 1;

	if (td->td_type == TD_TYPE_NETISR) {
		head = &ifsubq_stage_heads[mycpuid];
		stage = ifsq_get_stage(ifsq, mycpuid);

		stage->stg_cnt++;
		stage->stg_len += len;
		if (stage->stg_cnt < ifsq_stage_cntmax &&
		    stage->stg_len < (ifp->if_mtu - max_protohdr))
			avoid_start = 1;
	}

	ALTQ_SQ_LOCK(ifsq);
	error = ifsq_enqueue_locked(ifsq, m, pa);
	if (error) {
		IFNET_STAT_INC(ifp, oqdrops, 1);
		if (!ifsq_data_ready(ifsq)) {
			ALTQ_SQ_UNLOCK(ifsq);
			crit_exit_quick(td);
			return error;
		}
		avoid_start = 0;
	}
	if (!ifsq_is_started(ifsq)) {
		if (avoid_start) {
			ALTQ_SQ_UNLOCK(ifsq);

			KKASSERT(!error);
			if ((stage->stg_flags & IFSQ_STAGE_FLAG_QUED) == 0)
				ifsq_stage_insert(head, stage);

			IFNET_STAT_INC(ifp, obytes, len);
			if (mcast)
				IFNET_STAT_INC(ifp, omcasts, 1);
			crit_exit_quick(td);
			return error;
		}

		/*
		 * Hold the subqueue interlock of ifnet.if_start
		 */
		ifsq_set_started(ifsq);
		start = 1;
	}
	ALTQ_SQ_UNLOCK(ifsq);

	if (!error) {
		IFNET_STAT_INC(ifp, obytes, len);
		if (mcast)
			IFNET_STAT_INC(ifp, omcasts, 1);
	}

	if (stage != NULL) {
		if (!start && (stage->stg_flags & IFSQ_STAGE_FLAG_SCHED)) {
			KKASSERT(stage->stg_flags & IFSQ_STAGE_FLAG_QUED);
			if (!avoid_start) {
				ifsq_stage_remove(head, stage);
				ifsq_ifstart_schedule(ifsq, 1);
			}
			crit_exit_quick(td);
			return error;
		}

		if (stage->stg_flags & IFSQ_STAGE_FLAG_QUED) {
			ifsq_stage_remove(head, stage);
		} else {
			stage->stg_cnt = 0;
			stage->stg_len = 0;
		}
	}

	if (!start) {
		crit_exit_quick(td);
		return error;
	}

	ifsq_ifstart_try(ifsq, 0);

	crit_exit_quick(td);
	return error;
}

void *
ifa_create(int size)
{
	struct ifaddr *ifa;
	int i;

	KASSERT(size >= sizeof(*ifa), ("ifaddr size too small"));

	ifa = kmalloc(size, M_IFADDR, M_INTWAIT | M_ZERO);

	/*
	 * Make ifa_container availabel on all CPUs, since they
	 * could be accessed by any threads.
	 */
	ifa->ifa_containers =
		kmalloc(ncpus * sizeof(struct ifaddr_container),
			M_IFADDR,
			M_INTWAIT | M_ZERO | M_CACHEALIGN);

	ifa->ifa_ncnt = ncpus;
	for (i = 0; i < ncpus; ++i) {
		struct ifaddr_container *ifac = &ifa->ifa_containers[i];

		ifac->ifa_magic = IFA_CONTAINER_MAGIC;
		ifac->ifa = ifa;
		ifac->ifa_refcnt = 1;
	}
#ifdef IFADDR_DEBUG
	kprintf("alloc ifa %p %d\n", ifa, size);
#endif
	return ifa;
}

void
ifac_free(struct ifaddr_container *ifac, int cpu_id)
{
	struct ifaddr *ifa = ifac->ifa;

	KKASSERT(ifac->ifa_magic == IFA_CONTAINER_MAGIC);
	KKASSERT(ifac->ifa_refcnt == 0);
	KASSERT(ifac->ifa_listmask == 0,
		("ifa is still on %#x lists", ifac->ifa_listmask));

	ifac->ifa_magic = IFA_CONTAINER_DEAD;

#ifdef IFADDR_DEBUG_VERBOSE
	kprintf("try free ifa %p cpu_id %d\n", ifac->ifa, cpu_id);
#endif

	KASSERT(ifa->ifa_ncnt > 0 && ifa->ifa_ncnt <= ncpus,
		("invalid # of ifac, %d", ifa->ifa_ncnt));
	if (atomic_fetchadd_int(&ifa->ifa_ncnt, -1) == 1) {
#ifdef IFADDR_DEBUG
		kprintf("free ifa %p\n", ifa);
#endif
		kfree(ifa->ifa_containers, M_IFADDR);
		kfree(ifa, M_IFADDR);
	}
}

static void
ifa_iflink_dispatch(netmsg_t nmsg)
{
	struct netmsg_ifaddr *msg = (struct netmsg_ifaddr *)nmsg;
	struct ifaddr *ifa = msg->ifa;
	struct ifnet *ifp = msg->ifp;
	int cpu = mycpuid;
	struct ifaddr_container *ifac;

	crit_enter();

	ifac = &ifa->ifa_containers[cpu];
	ASSERT_IFAC_VALID(ifac);
	KASSERT((ifac->ifa_listmask & IFA_LIST_IFADDRHEAD) == 0,
		("ifaddr is on if_addrheads"));

	ifac->ifa_listmask |= IFA_LIST_IFADDRHEAD;
	if (msg->tail)
		TAILQ_INSERT_TAIL(&ifp->if_addrheads[cpu], ifac, ifa_link);
	else
		TAILQ_INSERT_HEAD(&ifp->if_addrheads[cpu], ifac, ifa_link);

	crit_exit();

	netisr_forwardmsg_all(&nmsg->base, cpu + 1);
}

void
ifa_iflink(struct ifaddr *ifa, struct ifnet *ifp, int tail)
{
	struct netmsg_ifaddr msg;

	netmsg_init(&msg.base, NULL, &curthread->td_msgport,
		    0, ifa_iflink_dispatch);
	msg.ifa = ifa;
	msg.ifp = ifp;
	msg.tail = tail;

	netisr_domsg(&msg.base, 0);
}

static void
ifa_ifunlink_dispatch(netmsg_t nmsg)
{
	struct netmsg_ifaddr *msg = (struct netmsg_ifaddr *)nmsg;
	struct ifaddr *ifa = msg->ifa;
	struct ifnet *ifp = msg->ifp;
	int cpu = mycpuid;
	struct ifaddr_container *ifac;

	crit_enter();

	ifac = &ifa->ifa_containers[cpu];
	ASSERT_IFAC_VALID(ifac);
	KASSERT(ifac->ifa_listmask & IFA_LIST_IFADDRHEAD,
		("ifaddr is not on if_addrhead"));

	TAILQ_REMOVE(&ifp->if_addrheads[cpu], ifac, ifa_link);
	ifac->ifa_listmask &= ~IFA_LIST_IFADDRHEAD;

	crit_exit();

	netisr_forwardmsg_all(&nmsg->base, cpu + 1);
}

void
ifa_ifunlink(struct ifaddr *ifa, struct ifnet *ifp)
{
	struct netmsg_ifaddr msg;

	netmsg_init(&msg.base, NULL, &curthread->td_msgport,
		    0, ifa_ifunlink_dispatch);
	msg.ifa = ifa;
	msg.ifp = ifp;

	netisr_domsg(&msg.base, 0);
}

static void
ifa_destroy_dispatch(netmsg_t nmsg)
{
	struct netmsg_ifaddr *msg = (struct netmsg_ifaddr *)nmsg;

	IFAFREE(msg->ifa);
	netisr_forwardmsg_all(&nmsg->base, mycpuid + 1);
}

void
ifa_destroy(struct ifaddr *ifa)
{
	struct netmsg_ifaddr msg;

	netmsg_init(&msg.base, NULL, &curthread->td_msgport,
		    0, ifa_destroy_dispatch);
	msg.ifa = ifa;

	netisr_domsg(&msg.base, 0);
}

static void
if_start_rollup(void)
{
	struct ifsubq_stage_head *head = &ifsubq_stage_heads[mycpuid];
	struct ifsubq_stage *stage;

	crit_enter();

	while ((stage = TAILQ_FIRST(&head->stg_head)) != NULL) {
		struct ifaltq_subque *ifsq = stage->stg_subq;
		int is_sched = 0;

		if (stage->stg_flags & IFSQ_STAGE_FLAG_SCHED)
			is_sched = 1;
		ifsq_stage_remove(head, stage);

		if (is_sched) {
			ifsq_ifstart_schedule(ifsq, 1);
		} else {
			int start = 0;

			ALTQ_SQ_LOCK(ifsq);
			if (!ifsq_is_started(ifsq)) {
				/*
				 * Hold the subqueue interlock of
				 * ifnet.if_start
				 */
				ifsq_set_started(ifsq);
				start = 1;
			}
			ALTQ_SQ_UNLOCK(ifsq);

			if (start)
				ifsq_ifstart_try(ifsq, 1);
		}
		KKASSERT((stage->stg_flags &
		    (IFSQ_STAGE_FLAG_QUED | IFSQ_STAGE_FLAG_SCHED)) == 0);
	}

	crit_exit();
}

static void
ifnetinit(void *dummy __unused)
{
	int i;

	/* XXX netisr_ncpus */
	for (i = 0; i < ncpus; ++i)
		TAILQ_INIT(&ifsubq_stage_heads[i].stg_head);
	netisr_register_rollup(if_start_rollup, NETISR_ROLLUP_PRIO_IFSTART);
}

void
if_register_com_alloc(u_char type,
    if_com_alloc_t *a, if_com_free_t *f)
{

        KASSERT(if_com_alloc[type] == NULL,
            ("if_register_com_alloc: %d already registered", type));
        KASSERT(if_com_free[type] == NULL,
            ("if_register_com_alloc: %d free already registered", type));

        if_com_alloc[type] = a;
        if_com_free[type] = f;
}

void
if_deregister_com_alloc(u_char type)
{

        KASSERT(if_com_alloc[type] != NULL,
            ("if_deregister_com_alloc: %d not registered", type));
        KASSERT(if_com_free[type] != NULL,
            ("if_deregister_com_alloc: %d free not registered", type));
        if_com_alloc[type] = NULL;
        if_com_free[type] = NULL;
}

void
ifq_set_maxlen(struct ifaltq *ifq, int len)
{
	ifq->altq_maxlen = len + (ncpus * ifsq_stage_cntmax);
}

int
ifq_mapsubq_default(struct ifaltq *ifq __unused, int cpuid __unused)
{
	return ALTQ_SUBQ_INDEX_DEFAULT;
}

int
ifq_mapsubq_modulo(struct ifaltq *ifq, int cpuid)
{

	return (cpuid % ifq->altq_subq_mappriv);
}

/*
 * Watchdog timeout.  Process callback as appropriate.  If we cannot
 * serialize the ifnet just try again on the next timeout.
 *
 * NOTE: The ifnet can adjust wd_timer while holding the serializer.  We
 *	 can only safely adjust it under the same circumstances.
 */
static void
ifsq_watchdog(void *arg)
{
	struct ifsubq_watchdog *wd = arg;
	struct ifnet *ifp;
	int count;

	/*
	 * Fast track.  Try to avoid acquiring the serializer when not
	 * near the terminal count, unless asked to.  If the atomic op
	 * to decrement the count fails just retry on the next callout.
	 */
	count = wd->wd_timer;
	cpu_ccfence();
	if (count == 0)
		goto done;
	if (count > 2 && (wd->wd_flags & IF_WDOG_ALLTICKS) == 0) {
		(void)atomic_cmpset_int(&wd->wd_timer, count, count - 1);
		goto done;
	}

	/*
	 * Obtain the serializer and then re-test all wd_timer conditions
	 * as it may have changed.  NICs do not mess with wd_timer without
	 * holding the serializer.
	 *
	 * If we are unable to obtain the serializer just retry the same
	 * count on the next callout.
	 *
	 * - call watchdog in terminal count (0)
	 * - call watchdog on last tick (1) if requested
	 * - call watchdog on all ticks if requested
	 */
	ifp = ifsq_get_ifp(wd->wd_subq);
	if (ifnet_tryserialize_all(ifp) == 0)
		goto done;
	if (atomic_cmpset_int(&wd->wd_timer, count, count - 1)) {
		--count;
		if (count == 0 ||
		    (wd->wd_flags & IF_WDOG_ALLTICKS) ||
		    ((wd->wd_flags & IF_WDOG_LASTTICK) && count == 1)) {
			wd->wd_watchdog(wd->wd_subq);
		}
	}
	ifnet_deserialize_all(ifp);
done:
	ifsq_watchdog_reset(wd);
}

static void
ifsq_watchdog_reset(struct ifsubq_watchdog *wd)
{
	callout_reset_bycpu(&wd->wd_callout, hz, ifsq_watchdog, wd,
	    ifsq_get_cpuid(wd->wd_subq));
}

void
ifsq_watchdog_init(struct ifsubq_watchdog *wd, struct ifaltq_subque *ifsq,
		   ifsq_watchdog_t watchdog, int flags)
{
	callout_init_mp(&wd->wd_callout);
	wd->wd_timer = 0;
	wd->wd_flags = flags;
	wd->wd_subq = ifsq;
	wd->wd_watchdog = watchdog;
}

void
ifsq_watchdog_start(struct ifsubq_watchdog *wd)
{
	atomic_swap_int(&wd->wd_timer, 0);
	ifsq_watchdog_reset(wd);
}

void
ifsq_watchdog_stop(struct ifsubq_watchdog *wd)
{
	atomic_swap_int(&wd->wd_timer, 0);
	callout_stop(&wd->wd_callout);
}

void
ifsq_watchdog_set_count(struct ifsubq_watchdog *wd, int count)
{
	atomic_swap_int(&wd->wd_timer, count);
}

void
ifnet_lock(void)
{
	KASSERT(curthread->td_type != TD_TYPE_NETISR,
	    ("try holding ifnet lock in netisr"));
	mtx_lock(&ifnet_mtx);
}

void
ifnet_unlock(void)
{
	KASSERT(curthread->td_type != TD_TYPE_NETISR,
	    ("try holding ifnet lock in netisr"));
	mtx_unlock(&ifnet_mtx);
}

static struct ifnet_array *
ifnet_array_alloc(int count)
{
	struct ifnet_array *arr;

	arr = kmalloc(__offsetof(struct ifnet_array, ifnet_arr[count]),
	    M_IFNET, M_WAITOK);
	arr->ifnet_count = count;

	return arr;
}

static void
ifnet_array_free(struct ifnet_array *arr)
{
	if (arr == &ifnet_array0)
		return;
	kfree(arr, M_IFNET);
}

static struct ifnet_array *
ifnet_array_add(struct ifnet *ifp, const struct ifnet_array *old_arr)
{
	struct ifnet_array *arr;
	int count, i;

	KASSERT(old_arr->ifnet_count >= 0,
	    ("invalid ifnet array count %d", old_arr->ifnet_count));
	count = old_arr->ifnet_count + 1;
	arr = ifnet_array_alloc(count);

	/*
	 * Save the old ifnet array and append this ifp to the end of
	 * the new ifnet array.
	 */
	for (i = 0; i < old_arr->ifnet_count; ++i) {
		KASSERT(old_arr->ifnet_arr[i] != ifp,
		    ("%s is already in ifnet array", ifp->if_xname));
		arr->ifnet_arr[i] = old_arr->ifnet_arr[i];
	}
	KASSERT(i == count - 1,
	    ("add %s, ifnet array index mismatch, should be %d, but got %d",
	     ifp->if_xname, count - 1, i));
	arr->ifnet_arr[i] = ifp;

	return arr;
}

static struct ifnet_array *
ifnet_array_del(struct ifnet *ifp, const struct ifnet_array *old_arr)
{
	struct ifnet_array *arr;
	int count, i, idx, found = 0;

	KASSERT(old_arr->ifnet_count > 0,
	    ("invalid ifnet array count %d", old_arr->ifnet_count));
	count = old_arr->ifnet_count - 1;
	arr = ifnet_array_alloc(count);

	/*
	 * Save the old ifnet array, but skip this ifp.
	 */
	idx = 0;
	for (i = 0; i < old_arr->ifnet_count; ++i) {
		if (old_arr->ifnet_arr[i] == ifp) {
			KASSERT(!found,
			    ("dup %s is in ifnet array", ifp->if_xname));
			found = 1;
			continue;
		}
		KASSERT(idx < count,
		    ("invalid ifnet array index %d, count %d", idx, count));
		arr->ifnet_arr[idx] = old_arr->ifnet_arr[i];
		++idx;
	}
	KASSERT(found, ("%s is not in ifnet array", ifp->if_xname));
	KASSERT(idx == count,
	    ("del %s, ifnet array count mismatch, should be %d, but got %d ",
	     ifp->if_xname, count, idx));

	return arr;
}

const struct ifnet_array *
ifnet_array_get(void)
{
	const struct ifnet_array *ret;

	KASSERT(curthread->td_type == TD_TYPE_NETISR, ("not in netisr"));
	ret = ifnet_array;
	/* Make sure 'ret' is really used. */
	cpu_ccfence();
	return (ret);
}

int
ifnet_array_isempty(void)
{
	KASSERT(curthread->td_type == TD_TYPE_NETISR, ("not in netisr"));
	if (ifnet_array->ifnet_count == 0)
		return 1;
	else
		return 0;
}

void
ifa_marker_init(struct ifaddr_marker *mark, struct ifnet *ifp)
{
	struct ifaddr *ifa;

	memset(mark, 0, sizeof(*mark));
	ifa = &mark->ifa;

	mark->ifac.ifa = ifa;

	ifa->ifa_addr = &mark->addr;
	ifa->ifa_dstaddr = &mark->dstaddr;
	ifa->ifa_netmask = &mark->netmask;
	ifa->ifa_ifp = ifp;
}

static int
if_ringcnt_fixup(int ring_cnt, int ring_cntmax)
{

	KASSERT(ring_cntmax > 0, ("invalid ring count max %d", ring_cntmax));

	if (ring_cnt <= 0 || ring_cnt > ring_cntmax)
		ring_cnt = ring_cntmax;
	if (ring_cnt > netisr_ncpus)
		ring_cnt = netisr_ncpus;
	return (ring_cnt);
}

static void
if_ringmap_set_grid(device_t dev, struct if_ringmap *rm, int grid)
{
	int i, offset;

	KASSERT(grid > 0, ("invalid if_ringmap grid %d", grid));
	KASSERT(grid >= rm->rm_cnt, ("invalid if_ringmap grid %d, count %d",
	    grid, rm->rm_cnt));
	rm->rm_grid = grid;

	offset = (rm->rm_grid * device_get_unit(dev)) % netisr_ncpus;
	for (i = 0; i < rm->rm_cnt; ++i) {
		rm->rm_cpumap[i] = offset + i;
		KASSERT(rm->rm_cpumap[i] < netisr_ncpus,
		    ("invalid cpumap[%d] = %d, offset %d", i,
		     rm->rm_cpumap[i], offset));
	}
}

static struct if_ringmap *
if_ringmap_alloc_flags(device_t dev, int ring_cnt, int ring_cntmax,
    uint32_t flags)
{
	struct if_ringmap *rm;
	int i, grid = 0, prev_grid;

	ring_cnt = if_ringcnt_fixup(ring_cnt, ring_cntmax);
	rm = kmalloc(__offsetof(struct if_ringmap, rm_cpumap[ring_cnt]),
	    M_DEVBUF, M_WAITOK | M_ZERO);

	rm->rm_cnt = ring_cnt;
	if (flags & RINGMAP_FLAG_POWEROF2)
		rm->rm_cnt = 1 << (fls(rm->rm_cnt) - 1);

	prev_grid = netisr_ncpus;
	for (i = 0; i < netisr_ncpus; ++i) {
		if (netisr_ncpus % (i + 1) != 0)
			continue;

		grid = netisr_ncpus / (i + 1);
		if (rm->rm_cnt > grid) {
			grid = prev_grid;
			break;
		}

		if (rm->rm_cnt > netisr_ncpus / (i + 2))
			break;
		prev_grid = grid;
	}
	if_ringmap_set_grid(dev, rm, grid);

	return (rm);
}

struct if_ringmap *
if_ringmap_alloc(device_t dev, int ring_cnt, int ring_cntmax)
{

	return (if_ringmap_alloc_flags(dev, ring_cnt, ring_cntmax,
	    RINGMAP_FLAG_NONE));
}

struct if_ringmap *
if_ringmap_alloc2(device_t dev, int ring_cnt, int ring_cntmax)
{

	return (if_ringmap_alloc_flags(dev, ring_cnt, ring_cntmax,
	    RINGMAP_FLAG_POWEROF2));
}

void
if_ringmap_free(struct if_ringmap *rm)
{

	kfree(rm, M_DEVBUF);
}

/*
 * Align the two ringmaps.
 *
 * e.g. 8 netisrs, rm0 contains 4 rings, rm1 contains 2 rings.
 *
 * Before:
 *
 * CPU      0  1  2  3   4  5  6  7
 * NIC_RX               n0 n1 n2 n3
 * NIC_TX        N0 N1
 *
 * After:
 *
 * CPU      0  1  2  3   4  5  6  7
 * NIC_RX               n0 n1 n2 n3
 * NIC_TX               N0 N1
 */
void
if_ringmap_align(device_t dev, struct if_ringmap *rm0, struct if_ringmap *rm1)
{

	if (rm0->rm_grid > rm1->rm_grid)
		if_ringmap_set_grid(dev, rm1, rm0->rm_grid);
	else if (rm0->rm_grid < rm1->rm_grid)
		if_ringmap_set_grid(dev, rm0, rm1->rm_grid);
}

void
if_ringmap_match(device_t dev, struct if_ringmap *rm0, struct if_ringmap *rm1)
{
	int subset_grid, cnt, divisor, mod, offset, i;
	struct if_ringmap *subset_rm, *rm;
	int old_rm0_grid, old_rm1_grid;

	if (rm0->rm_grid == rm1->rm_grid)
		return;

	/* Save grid for later use */
	old_rm0_grid = rm0->rm_grid;
	old_rm1_grid = rm1->rm_grid;

	if_ringmap_align(dev, rm0, rm1);

	/*
	 * Re-shuffle rings to get more even distribution.
	 *
	 * e.g. 12 netisrs, rm0 contains 4 rings, rm1 contains 2 rings.
	 *
	 * CPU       0  1  2  3   4  5  6  7   8  9 10 11
	 *
	 * NIC_RX   a0 a1 a2 a3  b0 b1 b2 b3  c0 c1 c2 c3
	 * NIC_TX   A0 A1        B0 B1        C0 C1
	 *
	 * NIC_RX   d0 d1 d2 d3  e0 e1 e2 e3  f0 f1 f2 f3
	 * NIC_TX         D0 D1        E0 E1        F0 F1
	 */

	if (rm0->rm_cnt >= (2 * old_rm1_grid)) {
		cnt = rm0->rm_cnt;
		subset_grid = old_rm1_grid;
		subset_rm = rm1;
		rm = rm0;
	} else if (rm1->rm_cnt > (2 * old_rm0_grid)) {
		cnt = rm1->rm_cnt;
		subset_grid = old_rm0_grid;
		subset_rm = rm0;
		rm = rm1;
	} else {
		/* No space to shuffle. */
		return;
	}

	mod = cnt / subset_grid;
	KKASSERT(mod >= 2);
	divisor = netisr_ncpus / rm->rm_grid;
	offset = ((device_get_unit(dev) / divisor) % mod) * subset_grid;

	for (i = 0; i < subset_rm->rm_cnt; ++i) {
		subset_rm->rm_cpumap[i] += offset;
		KASSERT(subset_rm->rm_cpumap[i] < netisr_ncpus,
		    ("match: invalid cpumap[%d] = %d, offset %d",
		     i, subset_rm->rm_cpumap[i], offset));
	}
#ifdef INVARIANTS
	for (i = 0; i < subset_rm->rm_cnt; ++i) {
		int j;

		for (j = 0; j < rm->rm_cnt; ++j) {
			if (rm->rm_cpumap[j] == subset_rm->rm_cpumap[i])
				break;
		}
		KASSERT(j < rm->rm_cnt,
		    ("subset cpumap[%d] = %d not found in superset",
		     i, subset_rm->rm_cpumap[i]));
	}
#endif
}

int
if_ringmap_count(const struct if_ringmap *rm)
{

	return (rm->rm_cnt);
}

int
if_ringmap_cpumap(const struct if_ringmap *rm, int ring)
{

	KASSERT(ring >= 0 && ring < rm->rm_cnt, ("invalid ring %d", ring));
	return (rm->rm_cpumap[ring]);
}

void
if_ringmap_rdrtable(const struct if_ringmap *rm, int table[], int table_nent)
{
	int i, grid_idx, grid_cnt, patch_off, patch_cnt, ncopy;

	KASSERT(table_nent > 0 && (table_nent & NETISR_CPUMASK) == 0,
	    ("invalid redirect table entries %d", table_nent));

	grid_idx = 0;
	for (i = 0; i < NETISR_CPUMAX; ++i) {
		table[i] = grid_idx++ % rm->rm_cnt;

		if (grid_idx == rm->rm_grid)
			grid_idx = 0;
	}

	/*
	 * Make the ring distributed more evenly for the remainder
	 * of each grid.
	 *
	 * e.g. 12 netisrs, rm contains 8 rings.
	 *
	 * Redirect table before:
	 *
	 *  0  1  2  3  4  5  6  7  0  1  2  3  0  1  2  3
	 *  4  5  6  7  0  1  2  3  0  1  2  3  4  5  6  7
	 *  0  1  2  3  0  1  2  3  4  5  6  7  0  1  2  3
	 *  ....
	 *
	 * Redirect table after being patched (pX, patched entries):
	 *
	 *  0  1  2  3  4  5  6  7 p0 p1 p2 p3  0  1  2  3
	 *  4  5  6  7 p4 p5 p6 p7  0  1  2  3  4  5  6  7
	 * p0 p1 p2 p3  0  1  2  3  4  5  6  7 p4 p5 p6 p7
	 *  ....
	 */
	patch_cnt = rm->rm_grid % rm->rm_cnt;
	if (patch_cnt == 0)
		goto done;
	patch_off = rm->rm_grid - (rm->rm_grid % rm->rm_cnt);

	grid_cnt = roundup(NETISR_CPUMAX, rm->rm_grid) / rm->rm_grid;
	grid_idx = 0;
	for (i = 0; i < grid_cnt; ++i) {
		int j;

		for (j = 0; j < patch_cnt; ++j) {
			int fix_idx;

			fix_idx = (i * rm->rm_grid) + patch_off + j;
			if (fix_idx >= NETISR_CPUMAX)
				goto done;
			table[fix_idx] = grid_idx++ % rm->rm_cnt;
		}
	}
done:
	/*
	 * If the device supports larger redirect table, duplicate
	 * the first NETISR_CPUMAX entries to the rest of the table,
	 * so that it matches upper layer's expectation:
	 * (hash & NETISR_CPUMASK) % netisr_ncpus
	 */
	ncopy = table_nent / NETISR_CPUMAX;
	for (i = 1; i < ncopy; ++i) {
		memcpy(&table[i * NETISR_CPUMAX], table,
		    NETISR_CPUMAX * sizeof(table[0]));
	}
	if (if_ringmap_dumprdr) {
		for (i = 0; i < table_nent; ++i) {
			if (i != 0 && i % 16 == 0)
				kprintf("\n");
			kprintf("%03d ", table[i]);
		}
		kprintf("\n");
	}
}

int
if_ringmap_cpumap_sysctl(SYSCTL_HANDLER_ARGS)
{
	struct if_ringmap *rm = arg1;
	int i, error = 0;

	for (i = 0; i < rm->rm_cnt; ++i) {
		int cpu = rm->rm_cpumap[i];

		error = SYSCTL_OUT(req, &cpu, sizeof(cpu));
		if (error)
			break;
	}
	return (error);
}
