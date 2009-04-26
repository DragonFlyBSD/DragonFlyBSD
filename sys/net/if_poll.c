/*-
 * Copyright (c) 2001-2002 Luigi Rizzo
 *
 * Supported by: the Xorp Project (www.xorp.org)
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD: src/sys/kern/kern_poll.c,v 1.2.2.4 2002/06/27 23:26:33 luigi Exp $
 */

#include "opt_ifpoll.h"

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/ktr.h>
#include <sys/malloc.h>
#include <sys/serialize.h>
#include <sys/socket.h>
#include <sys/sysctl.h>

#include <sys/thread2.h>
#include <sys/msgport2.h>

#include <machine/atomic.h>
#include <machine/smp.h>

#include <net/if.h>
#include <net/if_poll.h>
#include <net/netmsg2.h>

/*
 * Polling support for network device drivers.
 *
 * Drivers which support this feature try to register one status polling
 * handler and several TX/RX polling handlers with the polling code.
 * If interface's if_qpoll is called with non-NULL second argument, then
 * a register operation is requested, else a deregister operation is
 * requested.  If the requested operation is "register", driver should
 * setup the ifpoll_info passed in accoding its own needs:
 *   ifpoll_info.ifpi_status.status_func == NULL
 *     No status polling handler will be installed on CPU(0)
 *   ifpoll_info.ifpi_rx[n].poll_func == NULL
 *     No RX polling handler will be installed on CPU(n)
 *   ifpoll_info.ifpi_tx[n].poll_func == NULL
 *     No TX polling handler will be installed on CPU(n)
 *
 * All of the registered polling handlers are called only if the interface
 * is marked as 'IFF_RUNNING and IFF_NPOLLING'.  However, the interface's
 * register and deregister function (ifnet.if_qpoll) will be called even
 * if interface is not marked with 'IFF_RUNNING'.
 *
 * If registration is successful, the driver must disable interrupts,
 * and further I/O is performed through the TX/RX polling handler, which
 * are invoked (at least once per clock tick) with 3 arguments: the "arg"
 * passed at register time, a struct ifnet pointer, and a "count" limit.
 * The registered serializer will be held before calling the related
 * polling handler.
 *
 * The count limit specifies how much work the handler can do during the
 * call -- typically this is the number of packets to be received, or
 * transmitted, etc. (drivers are free to interpret this number, as long
 * as the max time spent in the function grows roughly linearly with the
 * count).
 *
 * A second variable controls the sharing of CPU between polling/kernel
 * network processing, and other activities (typically userlevel tasks):
 * net.ifpoll.{rxX,txX}.user_frac (between 0 and 100, default 50) sets the
 * share of CPU allocated to user tasks.  CPU is allocated proportionally
 * to the shares, by dynamically adjusting the "count" (poll_burst).
 *
 * Other parameters can should be left to their default values.
 * The following constraints hold
 *
 *	1 <= poll_burst <= poll_burst_max
 *	1 <= poll_each_burst <= poll_burst_max
 *	MIN_POLL_BURST_MAX <= poll_burst_max <= MAX_POLL_BURST_MAX
 */

#define IFPOLL_LIST_LEN		128
#define IFPOLL_FREQ_MAX		30000

#define MIN_IOPOLL_BURST_MAX	10
#define MAX_IOPOLL_BURST_MAX	1000
#define IOPOLL_BURST_MAX	150	/* good for 100Mbit net and HZ=1000 */

#define IOPOLL_EACH_BURST	5

#define IFPOLL_FREQ_DEFAULT	2000
#define IOPOLL_FREQ_DEFAULT	IFPOLL_FREQ_DEFAULT
#define STPOLL_FREQ_DEFAULT	100

#define IFPOLL_TXFRAC_DEFAULT	1
#define IFPOLL_STFRAC_DEFAULT	20

#define IFPOLL_RX		0x1
#define IFPOLL_TX		0x2

struct iopoll_rec {
	struct lwkt_serialize	*serializer;
	struct ifnet		*ifp;
	void			*arg;
	ifpoll_iofn_t		poll_func;
};

struct iopoll_ctx {
#ifdef IFPOLL_MULTI_SYSTIMER
	struct systimer		pollclock;
#endif

	struct timeval		prev_t;			/* state */
	uint32_t		short_ticks;		/* statistics */
	uint32_t		lost_polls;		/* statistics */
	uint32_t		suspect;		/* statistics */
	uint32_t		stalled;		/* statistics */
	uint32_t		pending_polls;		/* state */

	struct netmsg		poll_netmsg;

	int			poll_cpuid;
#ifdef IFPOLL_MULTI_SYSTIMER
	int			pollhz;			/* tunable */
#else
	int			poll_type;		/* IFPOLL_{RX,TX} */
#endif
	uint32_t		phase;			/* state */
	int			residual_burst;		/* state */
	uint32_t		poll_each_burst;	/* tunable */
	struct timeval		poll_start_t;		/* state */

	uint32_t		poll_handlers; /* next free entry in pr[]. */
	struct iopoll_rec	pr[IFPOLL_LIST_LEN];

	struct netmsg		poll_more_netmsg;

	uint32_t		poll_burst;		/* state */
	uint32_t		poll_burst_max;		/* tunable */
	uint32_t		user_frac;		/* tunable */
	uint32_t		kern_frac;		/* state */

	struct sysctl_ctx_list	poll_sysctl_ctx;
	struct sysctl_oid	*poll_sysctl_tree;
} __cachealign;

struct stpoll_rec {
	struct lwkt_serialize	*serializer;
	struct ifnet		*ifp;
	ifpoll_stfn_t		status_func;
};

struct stpoll_ctx {
#ifdef IFPOLL_MULTI_SYSTIMER
	struct systimer		pollclock;
#endif

	struct netmsg		poll_netmsg;

#ifdef IFPOLL_MULTI_SYSTIMER
	int			pollhz;			/* tunable */
#endif
	uint32_t		poll_handlers; /* next free entry in pr[]. */
	struct stpoll_rec	pr[IFPOLL_LIST_LEN];

	struct sysctl_ctx_list	poll_sysctl_ctx;
	struct sysctl_oid	*poll_sysctl_tree;
};

struct iopoll_sysctl_netmsg {
	struct netmsg		nmsg;
	struct iopoll_ctx	*ctx;
};

#ifndef IFPOLL_MULTI_SYSTIMER

struct ifpoll_data {
	struct systimer	clock;
	int		txfrac_count;
	int		stfrac_count;
	u_int		tx_cpumask;
	u_int		rx_cpumask;
} __cachealign;

#endif

static struct stpoll_ctx	stpoll_context;
static struct iopoll_ctx	*rxpoll_context[IFPOLL_CTX_MAX];
static struct iopoll_ctx	*txpoll_context[IFPOLL_CTX_MAX];

SYSCTL_NODE(_net, OID_AUTO, ifpoll, CTLFLAG_RW, 0,
	    "Network device polling parameters");

static int	ifpoll_ncpus = IFPOLL_CTX_MAX;

static int	iopoll_burst_max = IOPOLL_BURST_MAX;
static int	iopoll_each_burst = IOPOLL_EACH_BURST;

TUNABLE_INT("net.ifpoll.burst_max", &iopoll_burst_max);
TUNABLE_INT("net.ifpoll.each_burst", &iopoll_each_burst);

#ifdef IFPOLL_MULTI_SYSTIMER

static int	stpoll_hz = STPOLL_FREQ_DEFAULT;
static int	iopoll_hz = IOPOLL_FREQ_DEFAULT;

TUNABLE_INT("net.ifpoll.stpoll_hz", &stpoll_hz);
TUNABLE_INT("net.ifpoll.iopoll_hz", &iopoll_hz);

#else	/* !IFPOLL_MULTI_SYSTIMER */

static struct ifpoll_data ifpoll0;
static int	ifpoll_pollhz = IFPOLL_FREQ_DEFAULT;
static int	ifpoll_stfrac = IFPOLL_STFRAC_DEFAULT;
static int	ifpoll_txfrac = IFPOLL_TXFRAC_DEFAULT;
static int	ifpoll_handlers;

TUNABLE_INT("net.ifpoll.pollhz", &ifpoll_pollhz);
TUNABLE_INT("net.ifpoll.status_frac", &ifpoll_stfrac);
TUNABLE_INT("net.ifpoll.tx_frac", &ifpoll_txfrac);

static void	sysctl_ifpollhz_handler(struct netmsg *);
static int	sysctl_ifpollhz(SYSCTL_HANDLER_ARGS);

SYSCTL_PROC(_net_ifpoll, OID_AUTO, pollhz, CTLTYPE_INT | CTLFLAG_RW,
	    0, 0, sysctl_ifpollhz, "I", "Polling frequency");
SYSCTL_INT(_net_ifpoll, OID_AUTO, tx_frac, CTLFLAG_RW,
	   &ifpoll_txfrac, 0, "Every this many cycles poll transmit");
SYSCTL_INT(_net_ifpoll, OID_AUTO, st_frac, CTLFLAG_RW,
	   &ifpoll_stfrac, 0, "Every this many cycles poll status");

#endif	/* IFPOLL_MULTI_SYSTIMER */

void		ifpoll_init_pcpu(int);

#ifndef IFPOLL_MULTI_SYSTIMER
static void	ifpoll_start_handler(struct netmsg *);
static void	ifpoll_stop_handler(struct netmsg *);
static void	ifpoll_handler_addevent(void);
static void	ifpoll_handler_delevent(void);
static void	ifpoll_ipi_handler(void *, int);
static void	ifpoll_systimer(systimer_t, struct intrframe *);
#endif

static void	ifpoll_register_handler(struct netmsg *);
static void	ifpoll_deregister_handler(struct netmsg *);

/*
 * Status polling
 */
static void	stpoll_init(void);
static void	stpoll_handler(struct netmsg *);
static void	stpoll_clock(struct stpoll_ctx *);
#ifdef IFPOLL_MULTI_SYSTIMER
static void	stpoll_systimer(systimer_t, struct intrframe *);
#endif
static int	stpoll_register(struct ifnet *, const struct ifpoll_status *);
static int	stpoll_deregister(struct ifnet *);

#ifdef IFPOLL_MULTI_SYSTIMER
static void	sysctl_stpollhz_handler(struct netmsg *);
static int	sysctl_stpollhz(SYSCTL_HANDLER_ARGS);
#endif

/*
 * RX/TX polling
 */
static struct iopoll_ctx *iopoll_ctx_create(int, int);
static void	iopoll_init(int);
static void	iopoll_handler(struct netmsg *);
static void	iopollmore_handler(struct netmsg *);
static void	iopoll_clock(struct iopoll_ctx *);
#ifdef IFPOLL_MULTI_SYSTIMER
static void	iopoll_systimer(systimer_t, struct intrframe *);
#endif
static int	iopoll_register(struct ifnet *, struct iopoll_ctx *,
		    const struct ifpoll_io *);
static int	iopoll_deregister(struct ifnet *, struct iopoll_ctx *);

static void	iopoll_add_sysctl(struct sysctl_ctx_list *,
		    struct sysctl_oid_list *, struct iopoll_ctx *);
#ifdef IFPOLL_MULTI_SYSTIMER
static void	sysctl_iopollhz_handler(struct netmsg *);
static int	sysctl_iopollhz(SYSCTL_HANDLER_ARGS);
#endif
static void	sysctl_burstmax_handler(struct netmsg *);
static int	sysctl_burstmax(SYSCTL_HANDLER_ARGS);
static void	sysctl_eachburst_handler(struct netmsg *);
static int	sysctl_eachburst(SYSCTL_HANDLER_ARGS);

static __inline void
ifpoll_sendmsg_oncpu(struct netmsg *msg)
{
	if (msg->nm_lmsg.ms_flags & MSGF_DONE)
		ifnet_sendmsg(&msg->nm_lmsg, mycpuid);
}

static __inline void
sched_stpoll(struct stpoll_ctx *st_ctx)
{
	ifpoll_sendmsg_oncpu(&st_ctx->poll_netmsg);
}

static __inline void
sched_iopoll(struct iopoll_ctx *io_ctx)
{
	ifpoll_sendmsg_oncpu(&io_ctx->poll_netmsg);
}

static __inline void
sched_iopollmore(struct iopoll_ctx *io_ctx)
{
	ifpoll_sendmsg_oncpu(&io_ctx->poll_more_netmsg);
}

/*
 * Initialize per-cpu qpolling(4) context.  Called from kern_clock.c:
 */
void
ifpoll_init_pcpu(int cpuid)
{
	if (cpuid >= IFPOLL_CTX_MAX) {
		return;
	} else if (cpuid == 0) {
		if (ifpoll_ncpus > ncpus)
			ifpoll_ncpus = ncpus;
		kprintf("ifpoll_ncpus %d\n", ifpoll_ncpus);

#ifndef IFPOLL_MULTI_SYSTIMER
		systimer_init_periodic_nq(&ifpoll0.clock,
					  ifpoll_systimer, NULL, 1);
#endif

		stpoll_init();
	}
	iopoll_init(cpuid);
}

#ifndef IFPOLL_MULTI_SYSTIMER

static void
ifpoll_ipi_handler(void *arg __unused, int poll)
{
	KKASSERT(mycpuid < ifpoll_ncpus);

	if (poll & IFPOLL_TX)
		iopoll_clock(txpoll_context[mycpuid]);
	if (poll & IFPOLL_RX)
		iopoll_clock(rxpoll_context[mycpuid]);
}

static void
ifpoll_systimer(systimer_t info __unused, struct intrframe *frame __unused)
{
	uint32_t cpumask = 0;

	KKASSERT(mycpuid == 0);

	if (ifpoll0.stfrac_count-- == 0) {
		ifpoll0.stfrac_count = ifpoll_stfrac;
		stpoll_clock(&stpoll_context);
	}

	if (ifpoll0.txfrac_count-- == 0) {
		ifpoll0.txfrac_count = ifpoll_txfrac;

		/* TODO: We may try to piggyback TX on RX */
		cpumask = smp_active_mask & ifpoll0.tx_cpumask;
		if (cpumask != 0) {
			lwkt_send_ipiq2_mask(cpumask, ifpoll_ipi_handler,
					     NULL, IFPOLL_TX);
		}
	}

	cpumask = smp_active_mask & ifpoll0.rx_cpumask;
	if (cpumask != 0) {
		lwkt_send_ipiq2_mask(cpumask, ifpoll_ipi_handler,
				     NULL, IFPOLL_RX);
	}
}

static void
ifpoll_start_handler(struct netmsg *nmsg)
{
	KKASSERT(&curthread->td_msgport == ifnet_portfn(0));

	kprintf("ifpoll: start\n");
	systimer_adjust_periodic(&ifpoll0.clock, ifpoll_pollhz);
	lwkt_replymsg(&nmsg->nm_lmsg, 0);
}

static void
ifpoll_stop_handler(struct netmsg *nmsg)
{
	KKASSERT(&curthread->td_msgport == ifnet_portfn(0));

	kprintf("ifpoll: stop\n");
	systimer_adjust_periodic(&ifpoll0.clock, 1);
	lwkt_replymsg(&nmsg->nm_lmsg, 0);
}

static void
ifpoll_handler_addevent(void)
{
	if (atomic_fetchadd_int(&ifpoll_handlers, 1) == 0) {
		struct netmsg *nmsg;

		/* Start systimer */
		nmsg = kmalloc(sizeof(*nmsg), M_LWKTMSG, M_WAITOK);
		netmsg_init(nmsg, &netisr_afree_rport, 0, ifpoll_start_handler);
		ifnet_sendmsg(&nmsg->nm_lmsg, 0);
	}
}

static void
ifpoll_handler_delevent(void)
{
	KKASSERT(ifpoll_handlers > 0);
	if (atomic_fetchadd_int(&ifpoll_handlers, -1) == 1) {
		struct netmsg *nmsg;

		/* Stop systimer */
		nmsg = kmalloc(sizeof(*nmsg), M_LWKTMSG, M_WAITOK);
		netmsg_init(nmsg, &netisr_afree_rport, 0, ifpoll_stop_handler);
		ifnet_sendmsg(&nmsg->nm_lmsg, 0);
	}
}

static void
sysctl_ifpollhz_handler(struct netmsg *nmsg)
{
	KKASSERT(&curthread->td_msgport == ifnet_portfn(0));

	/*
	 * If there is no handler registered, don't adjust polling
	 * systimer frequency; polling systimer frequency will be
	 * adjusted once there is registered handler.
	 */
	ifpoll_pollhz = nmsg->nm_lmsg.u.ms_result;
	if (ifpoll_handlers)
		systimer_adjust_periodic(&ifpoll0.clock, ifpoll_pollhz);

	lwkt_replymsg(&nmsg->nm_lmsg, 0);
}

static int
sysctl_ifpollhz(SYSCTL_HANDLER_ARGS)
{
	struct netmsg nmsg;
	int error, phz;

	phz = ifpoll_pollhz;
	error = sysctl_handle_int(oidp, &phz, 0, req);
	if (error || req->newptr == NULL)
		return error;
	if (phz <= 0)
		return EINVAL;
	else if (phz > IFPOLL_FREQ_MAX)
		phz = IFPOLL_FREQ_MAX;

	netmsg_init(&nmsg, &curthread->td_msgport, MSGF_MPSAFE,
		    sysctl_ifpollhz_handler);
	nmsg.nm_lmsg.u.ms_result = phz;

	return ifnet_domsg(&nmsg.nm_lmsg, 0);
}

#endif	/* !IFPOLL_MULTI_SYSTIMER */

int
ifpoll_register(struct ifnet *ifp)
{
	struct ifpoll_info info;
	struct netmsg nmsg;
	int error;

	if (ifp->if_qpoll == NULL) {
		/* Device does not support polling */
		return EOPNOTSUPP;
	}

	/*
	 * Attempt to register.  Interlock with IFF_NPOLLING.
	 */

	ifnet_serialize_all(ifp);

	if (ifp->if_flags & IFF_NPOLLING) {
		/* Already polling */
		ifnet_deserialize_all(ifp);
		return EBUSY;
	}

	bzero(&info, sizeof(info));
	info.ifpi_ifp = ifp;

	ifp->if_flags |= IFF_NPOLLING;
	ifp->if_qpoll(ifp, &info);

	ifnet_deserialize_all(ifp);

	netmsg_init(&nmsg, &curthread->td_msgport, MSGF_MPSAFE,
		    ifpoll_register_handler);
	nmsg.nm_lmsg.u.ms_resultp = &info;

	error = ifnet_domsg(&nmsg.nm_lmsg, 0);
	if (error) {
		if (!ifpoll_deregister(ifp)) {
			if_printf(ifp, "ifpoll_register: "
				  "ifpoll_deregister failed!\n");
		}
	}
	return error;
}

int
ifpoll_deregister(struct ifnet *ifp)
{
	struct netmsg nmsg;
	int error;

	if (ifp->if_qpoll == NULL)
		return EOPNOTSUPP;

	ifnet_serialize_all(ifp);

	if ((ifp->if_flags & IFF_NPOLLING) == 0) {
		ifnet_deserialize_all(ifp);
		return EINVAL;
	}
	ifp->if_flags &= ~IFF_NPOLLING;

	ifnet_deserialize_all(ifp);

	netmsg_init(&nmsg, &curthread->td_msgport, MSGF_MPSAFE,
		    ifpoll_deregister_handler);
	nmsg.nm_lmsg.u.ms_resultp = ifp;

	error = ifnet_domsg(&nmsg.nm_lmsg, 0);
	if (!error) {
		ifnet_serialize_all(ifp);
		ifp->if_qpoll(ifp, NULL);
		ifnet_deserialize_all(ifp);
	}
	return error;
}

static void
ifpoll_register_handler(struct netmsg *nmsg)
{
	const struct ifpoll_info *info = nmsg->nm_lmsg.u.ms_resultp;
	int cpuid = mycpuid, nextcpu;
	int error;

	KKASSERT(cpuid < ifpoll_ncpus);
	KKASSERT(&curthread->td_msgport == ifnet_portfn(cpuid));

	if (cpuid == 0) {
		error = stpoll_register(info->ifpi_ifp, &info->ifpi_status);
		if (error)
			goto failed;
	}

	error = iopoll_register(info->ifpi_ifp, rxpoll_context[cpuid],
				&info->ifpi_rx[cpuid]);
	if (error)
		goto failed;

	error = iopoll_register(info->ifpi_ifp, txpoll_context[cpuid],
				&info->ifpi_tx[cpuid]);
	if (error)
		goto failed;

	nextcpu = cpuid + 1;
	if (nextcpu < ifpoll_ncpus)
		ifnet_forwardmsg(&nmsg->nm_lmsg, nextcpu);
	else
		lwkt_replymsg(&nmsg->nm_lmsg, 0);
	return;
failed:
	lwkt_replymsg(&nmsg->nm_lmsg, error);
}

static void
ifpoll_deregister_handler(struct netmsg *nmsg)
{
	struct ifnet *ifp = nmsg->nm_lmsg.u.ms_resultp;
	int cpuid = mycpuid, nextcpu;

	KKASSERT(cpuid < ifpoll_ncpus);
	KKASSERT(&curthread->td_msgport == ifnet_portfn(cpuid));

	/* Ignore errors */
	if (cpuid == 0)
		stpoll_deregister(ifp);
	iopoll_deregister(ifp, rxpoll_context[cpuid]);
	iopoll_deregister(ifp, txpoll_context[cpuid]);

	nextcpu = cpuid + 1;
	if (nextcpu < ifpoll_ncpus)
		ifnet_forwardmsg(&nmsg->nm_lmsg, nextcpu);
	else
		lwkt_replymsg(&nmsg->nm_lmsg, 0);
}

static void
stpoll_init(void)
{
	struct stpoll_ctx *st_ctx = &stpoll_context;

#ifdef IFPOLL_MULTI_SYSTIMER
	st_ctx->pollhz = stpoll_hz;
#endif

	sysctl_ctx_init(&st_ctx->poll_sysctl_ctx);
	st_ctx->poll_sysctl_tree = SYSCTL_ADD_NODE(&st_ctx->poll_sysctl_ctx,
				   SYSCTL_STATIC_CHILDREN(_net_ifpoll),
				   OID_AUTO, "status", CTLFLAG_RD, 0, "");

#ifdef IFPOLL_MULTI_SYSTIMER
	SYSCTL_ADD_PROC(&st_ctx->poll_sysctl_ctx,
			SYSCTL_CHILDREN(st_ctx->poll_sysctl_tree),
			OID_AUTO, "pollhz", CTLTYPE_INT | CTLFLAG_RW,
			st_ctx, 0, sysctl_stpollhz, "I",
			"Status polling frequency");
#endif

	SYSCTL_ADD_UINT(&st_ctx->poll_sysctl_ctx,
			SYSCTL_CHILDREN(st_ctx->poll_sysctl_tree),
			OID_AUTO, "handlers", CTLFLAG_RD,
			&st_ctx->poll_handlers, 0,
			"Number of registered status poll handlers");

	netmsg_init(&st_ctx->poll_netmsg, &netisr_adone_rport, MSGF_MPSAFE,
		    stpoll_handler);

#ifdef IFPOLL_MULTI_SYSTIMER
	systimer_init_periodic_nq(&st_ctx->pollclock,
				  stpoll_systimer, st_ctx, 1);
#endif
}

#ifdef IFPOLL_MULTI_SYSTIMER

static void
sysctl_stpollhz_handler(struct netmsg *msg)
{
	struct stpoll_ctx *st_ctx = &stpoll_context;

	KKASSERT(&curthread->td_msgport == ifnet_portfn(0));

	/*
	 * If there is no handler registered, don't adjust polling
	 * systimer frequency; polling systimer frequency will be
	 * adjusted once there is registered handler.
	 */
	st_ctx->pollhz = msg->nm_lmsg.u.ms_result;
	if (st_ctx->poll_handlers)
		systimer_adjust_periodic(&st_ctx->pollclock, st_ctx->pollhz);

	lwkt_replymsg(&msg->nm_lmsg, 0);
}

static int
sysctl_stpollhz(SYSCTL_HANDLER_ARGS)
{
	struct stpoll_ctx *st_ctx = arg1;
	struct netmsg msg;
	int error, phz;

	phz = st_ctx->pollhz;
	error = sysctl_handle_int(oidp, &phz, 0, req);
	if (error || req->newptr == NULL)
		return error;
	if (phz <= 0)
		return EINVAL;
	else if (phz > IFPOLL_FREQ_MAX)
		phz = IFPOLL_FREQ_MAX;

	netmsg_init(&msg, &curthread->td_msgport, MSGF_MPSAFE,
		    sysctl_stpollhz_handler);
	msg.nm_lmsg.u.ms_result = phz;

	return ifnet_domsg(&msg.nm_lmsg, 0);
}

#endif	/* IFPOLL_MULTI_SYSTIMER */

/*
 * stpoll_handler is scheduled by sched_stpoll when appropriate, typically
 * once per polling systimer tick.
 */
static void
stpoll_handler(struct netmsg *msg)
{
	struct stpoll_ctx *st_ctx = &stpoll_context;
	struct thread *td = curthread;
	int i, poll_hz;

	KKASSERT(&td->td_msgport == ifnet_portfn(0));

	crit_enter_quick(td);

	/* Reply ASAP */
	lwkt_replymsg(&msg->nm_lmsg, 0);

	if (st_ctx->poll_handlers == 0) {
		crit_exit_quick(td);
		return;
	}

#ifdef IFPOLL_MULTI_SYSTIMER
	poll_hz = st_ctx->pollhz;
#else
	poll_hz = ifpoll_pollhz / (ifpoll_stfrac + 1);
#endif

	for (i = 0; i < st_ctx->poll_handlers; ++i) {
		const struct stpoll_rec *rec = &st_ctx->pr[i];
		struct ifnet *ifp = rec->ifp;

		if (!lwkt_serialize_try(rec->serializer))
			continue;

		if ((ifp->if_flags & (IFF_RUNNING | IFF_NPOLLING)) ==
		    (IFF_RUNNING | IFF_NPOLLING))
			rec->status_func(ifp, poll_hz);

		lwkt_serialize_exit(rec->serializer);
	}

	crit_exit_quick(td);
}

/*
 * Hook from status poll systimer.  Tries to schedule an status poll.
 */
static void
stpoll_clock(struct stpoll_ctx *st_ctx)
{
	globaldata_t gd = mycpu;

	KKASSERT(gd->gd_cpuid == 0);

	if (st_ctx->poll_handlers == 0)
		return;

	crit_enter_gd(gd);
	sched_stpoll(st_ctx);
	crit_exit_gd(gd);
}

#ifdef IFPOLL_MULTI_SYSTIMER
static void
stpoll_systimer(systimer_t info, struct intrframe *frame __unused)
{
	stpoll_clock(info->data);
}
#endif

static int
stpoll_register(struct ifnet *ifp, const struct ifpoll_status *st_rec)
{
	struct stpoll_ctx *st_ctx = &stpoll_context;
	int error;

	KKASSERT(&curthread->td_msgport == ifnet_portfn(0));

	if (st_rec->status_func == NULL)
		return 0;

	/*
	 * Check if there is room.
	 */
	if (st_ctx->poll_handlers >= IFPOLL_LIST_LEN) {
		/*
		 * List full, cannot register more entries.
		 * This should never happen; if it does, it is probably a
		 * broken driver trying to register multiple times. Checking
		 * this at runtime is expensive, and won't solve the problem
		 * anyways, so just report a few times and then give up.
		 */
		static int verbose = 10; /* XXX */

		if (verbose > 0) {
			kprintf("status poll handlers list full, "
				"maybe a broken driver ?\n");
			verbose--;
		}
		error = ENOENT;
	} else {
		struct stpoll_rec *rec = &st_ctx->pr[st_ctx->poll_handlers];

		rec->ifp = ifp;
		rec->serializer = st_rec->serializer;
		rec->status_func = st_rec->status_func;

		st_ctx->poll_handlers++;

#ifdef IFPOLL_MULTI_SYSTIMER
		if (st_ctx->poll_handlers == 1) {
			systimer_adjust_periodic(&st_ctx->pollclock,
						 st_ctx->pollhz);
		}
#else
		ifpoll_handler_addevent();
#endif
		error = 0;
	}
	return error;
}

static int
stpoll_deregister(struct ifnet *ifp)
{
	struct stpoll_ctx *st_ctx = &stpoll_context;
	int i, error;

	KKASSERT(&curthread->td_msgport == ifnet_portfn(0));

	for (i = 0; i < st_ctx->poll_handlers; ++i) {
		if (st_ctx->pr[i].ifp == ifp) /* Found it */
			break;
	}
	if (i == st_ctx->poll_handlers) {
		kprintf("stpoll_deregister: ifp not found!!!\n");
		error = ENOENT;
	} else {
		st_ctx->poll_handlers--;
		if (i < st_ctx->poll_handlers) {
			/* Last entry replaces this one. */
			st_ctx->pr[i] = st_ctx->pr[st_ctx->poll_handlers];
		}

#ifdef IFPOLL_MULTI_SYSTIMER
		if (st_ctx->poll_handlers == 0)
			systimer_adjust_periodic(&st_ctx->pollclock, 1);
#else
		ifpoll_handler_delevent();
#endif
		error = 0;
	}
	return error;
}

#ifndef IFPOLL_MULTI_SYSTIMER
static __inline int
iopoll_hz(struct iopoll_ctx *io_ctx)
{
	int poll_hz;

	poll_hz = ifpoll_pollhz;
	if (io_ctx->poll_type == IFPOLL_TX)
		poll_hz /= ifpoll_txfrac + 1;
	return poll_hz;
}
#endif

static __inline void
iopoll_reset_state(struct iopoll_ctx *io_ctx)
{
	crit_enter();
	io_ctx->poll_burst = 5;
	io_ctx->pending_polls = 0;
	io_ctx->residual_burst = 0;
	io_ctx->phase = 0;
	io_ctx->kern_frac = 0;
	bzero(&io_ctx->poll_start_t, sizeof(io_ctx->poll_start_t));
	bzero(&io_ctx->prev_t, sizeof(io_ctx->prev_t));
	crit_exit();
}

static void
iopoll_init(int cpuid)
{
	KKASSERT(cpuid < IFPOLL_CTX_MAX);

	rxpoll_context[cpuid] = iopoll_ctx_create(cpuid, IFPOLL_RX);
	txpoll_context[cpuid] = iopoll_ctx_create(cpuid, IFPOLL_TX);
}

static struct iopoll_ctx *
iopoll_ctx_create(int cpuid, int poll_type)
{
	struct iopoll_ctx *io_ctx;
	const char *poll_type_str;
	char cpuid_str[16];

	KKASSERT(poll_type == IFPOLL_RX || poll_type == IFPOLL_TX);

	/*
	 * Make sure that tunables are in sane state
	 */
	if (iopoll_burst_max < MIN_IOPOLL_BURST_MAX)
		iopoll_burst_max = MIN_IOPOLL_BURST_MAX;
	else if (iopoll_burst_max > MAX_IOPOLL_BURST_MAX)
		iopoll_burst_max = MAX_IOPOLL_BURST_MAX;

	if (iopoll_each_burst > iopoll_burst_max)
		iopoll_each_burst = iopoll_burst_max;

	/*
	 * Create the per-cpu polling context
	 */
	io_ctx = kmalloc(sizeof(*io_ctx), M_DEVBUF, M_WAITOK | M_ZERO);

	io_ctx->poll_each_burst = iopoll_each_burst;
	io_ctx->poll_burst_max = iopoll_burst_max;
	io_ctx->user_frac = 50;
#ifdef IFPOLL_MULTI_SYSTIMER
	io_ctx->pollhz = iopoll_hz;
#else
	io_ctx->poll_type = poll_type;
#endif
	io_ctx->poll_cpuid = cpuid;
	iopoll_reset_state(io_ctx);

	netmsg_init(&io_ctx->poll_netmsg, &netisr_adone_rport, MSGF_MPSAFE,
		    iopoll_handler);
	io_ctx->poll_netmsg.nm_lmsg.u.ms_resultp = io_ctx;

	netmsg_init(&io_ctx->poll_more_netmsg, &netisr_adone_rport, MSGF_MPSAFE,
		    iopollmore_handler);
	io_ctx->poll_more_netmsg.nm_lmsg.u.ms_resultp = io_ctx;

	/*
	 * Initialize per-cpu sysctl nodes
	 */
	if (poll_type == IFPOLL_RX)
		poll_type_str = "rx";
	else
		poll_type_str = "tx";
	ksnprintf(cpuid_str, sizeof(cpuid_str), "%s%d",
		  poll_type_str, io_ctx->poll_cpuid);

	sysctl_ctx_init(&io_ctx->poll_sysctl_ctx);
	io_ctx->poll_sysctl_tree = SYSCTL_ADD_NODE(&io_ctx->poll_sysctl_ctx,
				   SYSCTL_STATIC_CHILDREN(_net_ifpoll),
				   OID_AUTO, cpuid_str, CTLFLAG_RD, 0, "");
	iopoll_add_sysctl(&io_ctx->poll_sysctl_ctx,
			  SYSCTL_CHILDREN(io_ctx->poll_sysctl_tree), io_ctx);

#ifdef IFPOLL_MULTI_SYSTIMER
	/*
	 * Initialize systimer
	 */
	systimer_init_periodic_nq(&io_ctx->pollclock,
				  iopoll_systimer, io_ctx, 1);
#endif

	return io_ctx;
}

/*
 * Hook from iopoll systimer.  Tries to schedule an iopoll, but keeps
 * track of lost ticks due to the previous handler taking too long.
 * Normally, this should not happen, because polling handler should
 * run for a short time.  However, in some cases (e.g. when there are
 * changes in link status etc.) the drivers take a very long time
 * (even in the order of milliseconds) to reset and reconfigure the
 * device, causing apparent lost polls.
 *
 * The first part of the code is just for debugging purposes, and tries
 * to count how often hardclock ticks are shorter than they should,
 * meaning either stray interrupts or delayed events.
 *
 * WARNING! called from fastint or IPI, the MP lock might not be held.
 */
static void
iopoll_clock(struct iopoll_ctx *io_ctx)
{
	globaldata_t gd = mycpu;
	struct timeval t;
	int delta, poll_hz;

	KKASSERT(gd->gd_cpuid == io_ctx->poll_cpuid);

	if (io_ctx->poll_handlers == 0)
		return;

#ifdef IFPOLL_MULTI_SYSTIMER
	poll_hz = io_ctx->pollhz;
#else
	poll_hz = iopoll_hz(io_ctx);
#endif

	microuptime(&t);
	delta = (t.tv_usec - io_ctx->prev_t.tv_usec) +
		(t.tv_sec - io_ctx->prev_t.tv_sec) * 1000000;
	if (delta * poll_hz < 500000)
		io_ctx->short_ticks++;
	else
		io_ctx->prev_t = t;

	if (io_ctx->pending_polls > 100) {
		/*
		 * Too much, assume it has stalled (not always true
		 * see comment above).
		 */
		io_ctx->stalled++;
		io_ctx->pending_polls = 0;
		io_ctx->phase = 0;
	}

	if (io_ctx->phase <= 2) {
		if (io_ctx->phase != 0)
			io_ctx->suspect++;
		io_ctx->phase = 1;
		crit_enter_gd(gd);
		sched_iopoll(io_ctx);
		crit_exit_gd(gd);
		io_ctx->phase = 2;
	}
	if (io_ctx->pending_polls++ > 0)
		io_ctx->lost_polls++;
}

#ifdef IFPOLL_MULTI_SYSTIMER
static void
iopoll_systimer(systimer_t info, struct intrframe *frame __unused)
{
	iopoll_clock(info->data);
}
#endif

/*
 * iopoll_handler is scheduled by sched_iopoll when appropriate, typically
 * once per polling systimer tick.
 *
 * Note that the message is replied immediately in order to allow a new
 * ISR to be scheduled in the handler.
 */
static void
iopoll_handler(struct netmsg *msg)
{
	struct iopoll_ctx *io_ctx;
	struct thread *td = curthread;
	int i, cycles;

	io_ctx = msg->nm_lmsg.u.ms_resultp;
	KKASSERT(&td->td_msgport == ifnet_portfn(io_ctx->poll_cpuid));

	crit_enter_quick(td);

	/* Reply ASAP */
	lwkt_replymsg(&msg->nm_lmsg, 0);

	if (io_ctx->poll_handlers == 0) {
		crit_exit_quick(td);
		return;
	}

	io_ctx->phase = 3;
	if (io_ctx->residual_burst == 0) {
		/* First call in this tick */
		microuptime(&io_ctx->poll_start_t);
		io_ctx->residual_burst = io_ctx->poll_burst;
	}
	cycles = (io_ctx->residual_burst < io_ctx->poll_each_burst) ?
		 io_ctx->residual_burst : io_ctx->poll_each_burst;
	io_ctx->residual_burst -= cycles;

	for (i = 0; i < io_ctx->poll_handlers; i++) {
		const struct iopoll_rec *rec = &io_ctx->pr[i];
		struct ifnet *ifp = rec->ifp;

		if (!lwkt_serialize_try(rec->serializer))
			continue;

		if ((ifp->if_flags & (IFF_RUNNING | IFF_NPOLLING)) ==
		    (IFF_RUNNING | IFF_NPOLLING))
			rec->poll_func(ifp, rec->arg, cycles);

		lwkt_serialize_exit(rec->serializer);
	}

	/*
	 * Do a quick exit/enter to catch any higher-priority
	 * interrupt sources.
	 */
	crit_exit_quick(td);
	crit_enter_quick(td);

	sched_iopollmore(io_ctx);
	io_ctx->phase = 4;

	crit_exit_quick(td);
}

/*
 * iopollmore_handler is called after other netisr's, possibly scheduling
 * another iopoll_handler call, or adapting the burst size for the next cycle.
 *
 * It is very bad to fetch large bursts of packets from a single card at once,
 * because the burst could take a long time to be completely processed leading
 * to unfairness.  To reduce the problem, and also to account better for time
 * spent in network-related processing, we split the burst in smaller chunks
 * of fixed size, giving control to the other netisr's between chunks.  This
 * helps in improving the fairness, reducing livelock and accounting for the
 * work performed in low level handling.
 */
static void
iopollmore_handler(struct netmsg *msg)
{
	struct thread *td = curthread;
	struct iopoll_ctx *io_ctx;
	struct timeval t;
	int kern_load, poll_hz;
	uint32_t pending_polls;

	io_ctx = msg->nm_lmsg.u.ms_resultp;
	KKASSERT(&td->td_msgport == ifnet_portfn(io_ctx->poll_cpuid));

	crit_enter_quick(td);

	/* Replay ASAP */
	lwkt_replymsg(&msg->nm_lmsg, 0);

	if (io_ctx->poll_handlers == 0) {
		crit_exit_quick(td);
		return;
	}

#ifdef IFPOLL_MULTI_SYSTIMER
	poll_hz = io_ctx->pollhz;
#else
	poll_hz = iopoll_hz(io_ctx);
#endif

	io_ctx->phase = 5;
	if (io_ctx->residual_burst > 0) {
		sched_iopoll(io_ctx);
		crit_exit_quick(td);
		/* Will run immediately on return, followed by netisrs */
		return;
	}

	/* Here we can account time spent in iopoll's in this tick */
	microuptime(&t);
	kern_load = (t.tv_usec - io_ctx->poll_start_t.tv_usec) +
		    (t.tv_sec - io_ctx->poll_start_t.tv_sec) * 1000000; /* us */
	kern_load = (kern_load * poll_hz) / 10000; /* 0..100 */
	io_ctx->kern_frac = kern_load;

	if (kern_load > (100 - io_ctx->user_frac)) {
		/* Try decrease ticks */
		if (io_ctx->poll_burst > 1)
			io_ctx->poll_burst--;
	} else {
		if (io_ctx->poll_burst < io_ctx->poll_burst_max)
			io_ctx->poll_burst++;
	}

	io_ctx->pending_polls--;
	pending_polls = io_ctx->pending_polls;

	if (pending_polls == 0) {
		/* We are done */
		io_ctx->phase = 0;
	} else {
		/*
		 * Last cycle was long and caused us to miss one or more
		 * hardclock ticks.  Restart processing again, but slightly
		 * reduce the burst size to prevent that this happens again.
		 */
		io_ctx->poll_burst -= (io_ctx->poll_burst / 8);
		if (io_ctx->poll_burst < 1)
			io_ctx->poll_burst = 1;
		sched_iopoll(io_ctx);
		io_ctx->phase = 6;
	}

	crit_exit_quick(td);
}

static void
iopoll_add_sysctl(struct sysctl_ctx_list *ctx, struct sysctl_oid_list *parent,
		  struct iopoll_ctx *io_ctx)
{
#ifdef IFPOLL_MULTI_SYSTIMER
	SYSCTL_ADD_PROC(ctx, parent, OID_AUTO, "pollhz",
			CTLTYPE_INT | CTLFLAG_RW, io_ctx, 0, sysctl_iopollhz,
			"I", "Device polling frequency");
#endif

	SYSCTL_ADD_PROC(ctx, parent, OID_AUTO, "burst_max",
			CTLTYPE_UINT | CTLFLAG_RW, io_ctx, 0, sysctl_burstmax,
			"IU", "Max Polling burst size");

	SYSCTL_ADD_PROC(ctx, parent, OID_AUTO, "each_burst",
			CTLTYPE_UINT | CTLFLAG_RW, io_ctx, 0, sysctl_eachburst,
			"IU", "Max size of each burst");

	SYSCTL_ADD_UINT(ctx, parent, OID_AUTO, "phase", CTLFLAG_RD,
			&io_ctx->phase, 0, "Polling phase");

	SYSCTL_ADD_UINT(ctx, parent, OID_AUTO, "suspect", CTLFLAG_RW,
			&io_ctx->suspect, 0, "suspect event");

	SYSCTL_ADD_UINT(ctx, parent, OID_AUTO, "stalled", CTLFLAG_RW,
			&io_ctx->stalled, 0, "potential stalls");

	SYSCTL_ADD_UINT(ctx, parent, OID_AUTO, "burst", CTLFLAG_RD,
			&io_ctx->poll_burst, 0, "Current polling burst size");

	SYSCTL_ADD_UINT(ctx, parent, OID_AUTO, "user_frac", CTLFLAG_RW,
			&io_ctx->user_frac, 0,
			"Desired user fraction of cpu time");

	SYSCTL_ADD_UINT(ctx, parent, OID_AUTO, "kern_frac", CTLFLAG_RD,
			&io_ctx->kern_frac, 0,
			"Kernel fraction of cpu time");

	SYSCTL_ADD_UINT(ctx, parent, OID_AUTO, "short_ticks", CTLFLAG_RW,
			&io_ctx->short_ticks, 0,
			"Hardclock ticks shorter than they should be");

	SYSCTL_ADD_UINT(ctx, parent, OID_AUTO, "lost_polls", CTLFLAG_RW,
			&io_ctx->lost_polls, 0,
			"How many times we would have lost a poll tick");

	SYSCTL_ADD_UINT(ctx, parent, OID_AUTO, "pending_polls", CTLFLAG_RD,
			&io_ctx->pending_polls, 0, "Do we need to poll again");

	SYSCTL_ADD_INT(ctx, parent, OID_AUTO, "residual_burst", CTLFLAG_RD,
		       &io_ctx->residual_burst, 0,
		       "# of residual cycles in burst");

	SYSCTL_ADD_UINT(ctx, parent, OID_AUTO, "handlers", CTLFLAG_RD,
			&io_ctx->poll_handlers, 0,
			"Number of registered poll handlers");
}

#ifdef IFPOLL_MULTI_SYSTIMER

static int
sysctl_iopollhz(SYSCTL_HANDLER_ARGS)
{
	struct iopoll_ctx *io_ctx = arg1;
	struct iopoll_sysctl_netmsg msg;
	struct netmsg *nmsg;
	int error, phz;

	phz = io_ctx->pollhz;
	error = sysctl_handle_int(oidp, &phz, 0, req);
	if (error || req->newptr == NULL)
		return error;
	if (phz <= 0)
		return EINVAL;
	else if (phz > IFPOLL_FREQ_MAX)
		phz = IFPOLL_FREQ_MAX;

	nmsg = &msg.nmsg;
	netmsg_init(nmsg, &curthread->td_msgport, MSGF_MPSAFE,
		    sysctl_iopollhz_handler);
	nmsg->nm_lmsg.u.ms_result = phz;
	msg.ctx = io_ctx;

	return ifnet_domsg(&nmsg->nm_lmsg, io_ctx->poll_cpuid);
}

static void
sysctl_iopollhz_handler(struct netmsg *nmsg)
{
	struct iopoll_sysctl_netmsg *msg = (struct iopoll_sysctl_netmsg *)nmsg;
	struct iopoll_ctx *io_ctx;

	io_ctx = msg->ctx;
	KKASSERT(&curthread->td_msgport == ifnet_portfn(io_ctx->poll_cpuid));

	/*
	 * If polling is disabled or there is no polling handler
	 * registered, don't adjust polling systimer frequency.
	 * Polling systimer frequency will be adjusted once there
	 * are registered handlers.
	 */
	io_ctx->pollhz = nmsg->nm_lmsg.u.ms_result;
	if (io_ctx->poll_handlers)
		systimer_adjust_periodic(&io_ctx->pollclock, io_ctx->pollhz);

	lwkt_replymsg(&nmsg->nm_lmsg, 0);
}

#endif	/* IFPOLL_MULTI_SYSTIMER */

static void
sysctl_burstmax_handler(struct netmsg *nmsg)
{
	struct iopoll_sysctl_netmsg *msg = (struct iopoll_sysctl_netmsg *)nmsg;
	struct iopoll_ctx *io_ctx;

	io_ctx = msg->ctx;
	KKASSERT(&curthread->td_msgport == ifnet_portfn(io_ctx->poll_cpuid));

	io_ctx->poll_burst_max = nmsg->nm_lmsg.u.ms_result;
	if (io_ctx->poll_each_burst > io_ctx->poll_burst_max)
		io_ctx->poll_each_burst = io_ctx->poll_burst_max;
	if (io_ctx->poll_burst > io_ctx->poll_burst_max)
		io_ctx->poll_burst = io_ctx->poll_burst_max;
	if (io_ctx->residual_burst > io_ctx->poll_burst_max)
		io_ctx->residual_burst = io_ctx->poll_burst_max;

	lwkt_replymsg(&nmsg->nm_lmsg, 0);
}

static int
sysctl_burstmax(SYSCTL_HANDLER_ARGS)
{
	struct iopoll_ctx *io_ctx = arg1;
	struct iopoll_sysctl_netmsg msg;
	struct netmsg *nmsg;
	uint32_t burst_max;
	int error;

	burst_max = io_ctx->poll_burst_max;
	error = sysctl_handle_int(oidp, &burst_max, 0, req);
	if (error || req->newptr == NULL)
		return error;
	if (burst_max < MIN_IOPOLL_BURST_MAX)
		burst_max = MIN_IOPOLL_BURST_MAX;
	else if (burst_max > MAX_IOPOLL_BURST_MAX)
		burst_max = MAX_IOPOLL_BURST_MAX;

	nmsg = &msg.nmsg;
	netmsg_init(nmsg, &curthread->td_msgport, MSGF_MPSAFE,
		    sysctl_burstmax_handler);
	nmsg->nm_lmsg.u.ms_result = burst_max;
	msg.ctx = io_ctx;

	return ifnet_domsg(&nmsg->nm_lmsg, io_ctx->poll_cpuid);
}

static void
sysctl_eachburst_handler(struct netmsg *nmsg)
{
	struct iopoll_sysctl_netmsg *msg = (struct iopoll_sysctl_netmsg *)nmsg;
	struct iopoll_ctx *io_ctx;
	uint32_t each_burst;

	io_ctx = msg->ctx;
	KKASSERT(&curthread->td_msgport == ifnet_portfn(io_ctx->poll_cpuid));

	each_burst = nmsg->nm_lmsg.u.ms_result;
	if (each_burst > io_ctx->poll_burst_max)
		each_burst = io_ctx->poll_burst_max;
	else if (each_burst < 1)
		each_burst = 1;
	io_ctx->poll_each_burst = each_burst;

	lwkt_replymsg(&nmsg->nm_lmsg, 0);
}

static int
sysctl_eachburst(SYSCTL_HANDLER_ARGS)
{
	struct iopoll_ctx *io_ctx = arg1;
	struct iopoll_sysctl_netmsg msg;
	struct netmsg *nmsg;
	uint32_t each_burst;
	int error;

	each_burst = io_ctx->poll_each_burst;
	error = sysctl_handle_int(oidp, &each_burst, 0, req);
	if (error || req->newptr == NULL)
		return error;

	nmsg = &msg.nmsg;
	netmsg_init(nmsg, &curthread->td_msgport, MSGF_MPSAFE,
		    sysctl_eachburst_handler);
	nmsg->nm_lmsg.u.ms_result = each_burst;
	msg.ctx = io_ctx;

	return ifnet_domsg(&nmsg->nm_lmsg, io_ctx->poll_cpuid);
}

static int
iopoll_register(struct ifnet *ifp, struct iopoll_ctx *io_ctx,
		const struct ifpoll_io *io_rec)
{
	int error;

	KKASSERT(&curthread->td_msgport == ifnet_portfn(io_ctx->poll_cpuid));

	if (io_rec->poll_func == NULL)
		return 0;

	/*
	 * Check if there is room.
	 */
	if (io_ctx->poll_handlers >= IFPOLL_LIST_LEN) {
		/*
		 * List full, cannot register more entries.
		 * This should never happen; if it does, it is probably a
		 * broken driver trying to register multiple times. Checking
		 * this at runtime is expensive, and won't solve the problem
		 * anyways, so just report a few times and then give up.
		 */
		static int verbose = 10; /* XXX */
		if (verbose > 0) {
			kprintf("io poll handlers list full, "
				"maybe a broken driver ?\n");
			verbose--;
		}
		error = ENOENT;
	} else {
		struct iopoll_rec *rec = &io_ctx->pr[io_ctx->poll_handlers];

		rec->ifp = ifp;
		rec->serializer = io_rec->serializer;
		rec->arg = io_rec->arg;
		rec->poll_func = io_rec->poll_func;

		io_ctx->poll_handlers++;
		if (io_ctx->poll_handlers == 1) {
#ifdef IFPOLL_MULTI_SYSTIMER
			systimer_adjust_periodic(&io_ctx->pollclock,
						 io_ctx->pollhz);
#else
			u_int *mask;

			if (io_ctx->poll_type == IFPOLL_RX)
				mask = &ifpoll0.rx_cpumask;
			else
				mask = &ifpoll0.tx_cpumask;
			KKASSERT((*mask & mycpu->gd_cpumask) == 0);
			atomic_set_int(mask, mycpu->gd_cpumask);
#endif
		}
#ifndef IFPOLL_MULTI_SYSTIMER
		ifpoll_handler_addevent();
#endif
		error = 0;
	}
	return error;
}

static int
iopoll_deregister(struct ifnet *ifp, struct iopoll_ctx *io_ctx)
{
	int i, error;

	KKASSERT(&curthread->td_msgport == ifnet_portfn(io_ctx->poll_cpuid));

	for (i = 0; i < io_ctx->poll_handlers; ++i) {
		if (io_ctx->pr[i].ifp == ifp) /* Found it */
			break;
	}
	if (i == io_ctx->poll_handlers) {
		error = ENOENT;
	} else {
		io_ctx->poll_handlers--;
		if (i < io_ctx->poll_handlers) {
			/* Last entry replaces this one. */
			io_ctx->pr[i] = io_ctx->pr[io_ctx->poll_handlers];
		}

		if (io_ctx->poll_handlers == 0) {
#ifdef IFPOLL_MULTI_SYSTIMER
			systimer_adjust_periodic(&io_ctx->pollclock, 1);
#else
			u_int *mask;

			if (io_ctx->poll_type == IFPOLL_RX)
				mask = &ifpoll0.rx_cpumask;
			else
				mask = &ifpoll0.tx_cpumask;
			KKASSERT(*mask & mycpu->gd_cpumask);
			atomic_clear_int(mask, mycpu->gd_cpumask);
#endif
			iopoll_reset_state(io_ctx);
		}
#ifndef IFPOLL_MULTI_SYSTIMER
		ifpoll_handler_delevent();
#endif
		error = 0;
	}
	return error;
}
