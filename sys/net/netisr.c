/*
 * Copyright (c) 2003, 2004 Matthew Dillon. All rights reserved.
 * Copyright (c) 2003, 2004 Jeffrey M. Hsu.  All rights reserved.
 * Copyright (c) 2003 Jonathan Lemon.  All rights reserved.
 * Copyright (c) 2003, 2004 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Jonathan Lemon, Jeffrey M. Hsu, and Matthew Dillon.
 *
 * Jonathan Lemon gave Jeffrey Hsu permission to combine his copyright
 * into this one around July 8 2004.
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

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/msgport.h>
#include <sys/proc.h>
#include <sys/interrupt.h>
#include <sys/socket.h>
#include <sys/sysctl.h>
#include <sys/socketvar.h>
#include <net/if.h>
#include <net/if_var.h>
#include <net/netisr2.h>
#include <machine/cpufunc.h>
#include <machine/smp.h>

#include <sys/thread2.h>
#include <sys/msgport2.h>
#include <net/netmsg2.h>
#include <sys/mplock2.h>

static void netmsg_service_loop(void *arg);
static void netisr_hashfn0(struct mbuf **mp, int hoff);
static void netisr_nohashck(struct mbuf *, const struct pktinfo *);

struct netmsg_port_registration {
	TAILQ_ENTRY(netmsg_port_registration) npr_entry;
	lwkt_port_t	npr_port;
};

struct netmsg_rollup {
	TAILQ_ENTRY(netmsg_rollup) ru_entry;
	netisr_ru_t	ru_func;
	int		ru_prio;
};

struct netmsg_barrier {
	struct netmsg_base	base;
	volatile cpumask_t	*br_cpumask;
	volatile uint32_t	br_done;
};

#define NETISR_BR_NOTDONE	0x1
#define NETISR_BR_WAITDONE	0x80000000

struct netisr_barrier {
	struct netmsg_barrier	*br_msgs[MAXCPU];
	int			br_isset;
};

static struct netisr netisrs[NETISR_MAX];
static TAILQ_HEAD(,netmsg_port_registration) netreglist;
static TAILQ_HEAD(,netmsg_rollup) netrulist;

/* Per-CPU thread to handle any protocol.  */
struct thread netisr_cpu[MAXCPU];
lwkt_port netisr_afree_rport;
lwkt_port netisr_afree_free_so_rport;
lwkt_port netisr_adone_rport;
lwkt_port netisr_apanic_rport;
lwkt_port netisr_sync_port;

static int (*netmsg_fwd_port_fn)(lwkt_port_t, lwkt_msg_t);

SYSCTL_NODE(_net, OID_AUTO, netisr, CTLFLAG_RW, 0, "netisr");

/*
 * netisr_afree_rport replymsg function, only used to handle async
 * messages which the sender has abandoned to their fate.
 */
static void
netisr_autofree_reply(lwkt_port_t port, lwkt_msg_t msg)
{
	kfree(msg, M_LWKTMSG);
}

static void
netisr_autofree_free_so_reply(lwkt_port_t port, lwkt_msg_t msg)
{
	sofree(((netmsg_t)msg)->base.nm_so);
	kfree(msg, M_LWKTMSG);
}

/*
 * We need a custom putport function to handle the case where the
 * message target is the current thread's message port.  This case
 * can occur when the TCP or UDP stack does a direct callback to NFS and NFS
 * then turns around and executes a network operation synchronously.
 *
 * To prevent deadlocking, we must execute these self-referential messages
 * synchronously, effectively turning the message into a glorified direct
 * procedure call back into the protocol stack.  The operation must be
 * complete on return or we will deadlock, so panic if it isn't.
 *
 * However, the target function is under no obligation to immediately
 * reply the message.  It may forward it elsewhere.
 */
static int
netmsg_put_port(lwkt_port_t port, lwkt_msg_t lmsg)
{
	netmsg_base_t nmsg = (void *)lmsg;

	if ((lmsg->ms_flags & MSGF_SYNC) && port == &curthread->td_msgport) {
		nmsg->nm_dispatch((netmsg_t)nmsg);
		return(EASYNC);
	} else {
		return(netmsg_fwd_port_fn(port, lmsg));
	}
}

/*
 * UNIX DOMAIN sockets still have to run their uipc functions synchronously,
 * because they depend on the user proc context for a number of things 
 * (like creds) which we have not yet incorporated into the message structure.
 *
 * However, we maintain or message/port abstraction.  Having a special 
 * synchronous port which runs the commands synchronously gives us the
 * ability to serialize operations in one place later on when we start
 * removing the BGL.
 */
static int
netmsg_sync_putport(lwkt_port_t port, lwkt_msg_t lmsg)
{
	netmsg_base_t nmsg = (void *)lmsg;

	KKASSERT((lmsg->ms_flags & MSGF_DONE) == 0);

	lmsg->ms_target_port = port;	/* required for abort */
	nmsg->nm_dispatch((netmsg_t)nmsg);
	return(EASYNC);
}

static void
netisr_init(void)
{
	int i;

	TAILQ_INIT(&netreglist);
	TAILQ_INIT(&netrulist);

	/*
	 * Create default per-cpu threads for generic protocol handling.
	 */
	for (i = 0; i < ncpus; ++i) {
		lwkt_create(netmsg_service_loop, NULL, NULL,
			    &netisr_cpu[i],
			    TDF_NOSTART|TDF_FORCE_SPINPORT|TDF_FIXEDCPU,
			    i, "netisr_cpu %d", i);
		netmsg_service_port_init(&netisr_cpu[i].td_msgport);
		lwkt_schedule(&netisr_cpu[i]);
	}

	/*
	 * The netisr_afree_rport is a special reply port which automatically
	 * frees the replied message.  The netisr_adone_rport simply marks
	 * the message as being done.  The netisr_apanic_rport panics if
	 * the message is replied to.
	 */
	lwkt_initport_replyonly(&netisr_afree_rport, netisr_autofree_reply);
	lwkt_initport_replyonly(&netisr_afree_free_so_rport,
				netisr_autofree_free_so_reply);
	lwkt_initport_replyonly_null(&netisr_adone_rport);
	lwkt_initport_panic(&netisr_apanic_rport);

	/*
	 * The netisr_syncport is a special port which executes the message
	 * synchronously and waits for it if EASYNC is returned.
	 */
	lwkt_initport_putonly(&netisr_sync_port, netmsg_sync_putport);
}

SYSINIT(netisr, SI_SUB_PRE_DRIVERS, SI_ORDER_FIRST, netisr_init, NULL);

/*
 * Finish initializing the message port for a netmsg service.  This also
 * registers the port for synchronous cleanup operations such as when an
 * ifnet is being destroyed.  There is no deregistration API yet.
 */
void
netmsg_service_port_init(lwkt_port_t port)
{
	struct netmsg_port_registration *reg;

	/*
	 * Override the putport function.  Our custom function checks for
	 * self-references and executes such commands synchronously.
	 */
	if (netmsg_fwd_port_fn == NULL)
		netmsg_fwd_port_fn = port->mp_putport;
	KKASSERT(netmsg_fwd_port_fn == port->mp_putport);
	port->mp_putport = netmsg_put_port;

	/*
	 * Keep track of ports using the netmsg API so we can synchronize
	 * certain operations (such as freeing an ifnet structure) across all
	 * consumers.
	 */
	reg = kmalloc(sizeof(*reg), M_TEMP, M_WAITOK|M_ZERO);
	reg->npr_port = port;
	TAILQ_INSERT_TAIL(&netreglist, reg, npr_entry);
}

/*
 * This function synchronizes the caller with all netmsg services.  For
 * example, if an interface is being removed we must make sure that all
 * packets related to that interface complete processing before the structure
 * can actually be freed.  This sort of synchronization is an alternative to
 * ref-counting the netif, removing the ref counting overhead in favor of
 * placing additional overhead in the netif freeing sequence (where it is
 * inconsequential).
 */
void
netmsg_service_sync(void)
{
	struct netmsg_port_registration *reg;
	struct netmsg_base smsg;

	netmsg_init(&smsg, NULL, &curthread->td_msgport, 0, netmsg_sync_handler);

	TAILQ_FOREACH(reg, &netreglist, npr_entry) {
		lwkt_domsg(reg->npr_port, &smsg.lmsg, 0);
	}
}

/*
 * The netmsg function simply replies the message.  API semantics require
 * EASYNC to be returned if the netmsg function disposes of the message.
 */
void
netmsg_sync_handler(netmsg_t msg)
{
	lwkt_replymsg(&msg->lmsg, 0);
}

/*
 * Generic netmsg service loop.  Some protocols may roll their own but all
 * must do the basic command dispatch function call done here.
 */
static void
netmsg_service_loop(void *arg)
{
	struct netmsg_rollup *ru;
	netmsg_base_t msg;
	thread_t td = curthread;
	int limit;

	td->td_type = TD_TYPE_NETISR;

	while ((msg = lwkt_waitport(&td->td_msgport, 0))) {
		/*
		 * Run up to 512 pending netmsgs.
		 */
		limit = 512;
		do {
			KASSERT(msg->nm_dispatch != NULL,
				("netmsg_service isr %d badmsg",
				msg->lmsg.u.ms_result));
			if (msg->nm_so &&
			    msg->nm_so->so_port != &td->td_msgport) {
				/*
				 * Sockets undergoing connect or disconnect
				 * ops can change ports on us.  Chase the
				 * port.
				 */
#ifdef foo
				/*
				 * This could be quite common for protocols
				 * which support asynchronous pru_connect,
				 * e.g. TCP, so kprintf socket port chasing
				 * could be too verbose for the console.
				 */
				kprintf("netmsg_service_loop: Warning, "
					"port changed so=%p\n", msg->nm_so);
#endif
				lwkt_forwardmsg(msg->nm_so->so_port,
						&msg->lmsg);
			} else {
				/*
				 * We are on the correct port, dispatch it.
				 */
				msg->nm_dispatch((netmsg_t)msg);
			}
			if (--limit == 0)
				break;
		} while ((msg = lwkt_getport(&td->td_msgport)) != NULL);

		/*
		 * Run all registered rollup functions for this cpu
		 * (e.g. tcp_willblock()).
		 */
		TAILQ_FOREACH(ru, &netrulist, ru_entry)
			ru->ru_func();
	}
}

/*
 * Forward a packet to a netisr service function.
 *
 * If the packet has not been assigned to a protocol thread we call
 * the port characterization function to assign it.  The caller must
 * clear M_HASH (or not have set it in the first place) if the caller
 * wishes the packet to be recharacterized.
 */
int
netisr_queue(int num, struct mbuf *m)
{
	struct netisr *ni;
	struct netmsg_packet *pmsg;
	lwkt_port_t port;

	KASSERT((num > 0 && num <= NELEM(netisrs)),
		("Bad isr %d", num));

	ni = &netisrs[num];
	if (ni->ni_handler == NULL) {
		kprintf("Unregistered isr %d\n", num);
		m_freem(m);
		return (EIO);
	}

	/*
	 * Figure out which protocol thread to send to.  This does not
	 * have to be perfect but performance will be really good if it
	 * is correct.  Major protocol inputs such as ip_input() will
	 * re-characterize the packet as necessary.
	 */
	if ((m->m_flags & M_HASH) == 0) {
		ni->ni_hashfn(&m, 0);
		if (m == NULL)
			return (EIO);
		if ((m->m_flags & M_HASH) == 0) {
			kprintf("netisr_queue(%d): packet hash failed\n", num);
			m_freem(m);
			return (EIO);
		}
	}

	/*
	 * Get the protocol port based on the packet hash, initialize
	 * the netmsg, and send it off.
	 */
	port = netisr_hashport(m->m_pkthdr.hash);
	pmsg = &m->m_hdr.mh_netmsg;
	netmsg_init(&pmsg->base, NULL, &netisr_apanic_rport,
		    0, ni->ni_handler);
	pmsg->nm_packet = m;
	pmsg->base.lmsg.u.ms_result = num;
	lwkt_sendmsg(port, &pmsg->base.lmsg);

	return (0);
}

/*
 * Run a netisr service function on the packet.
 *
 * The packet must have been correctly characterized!
 */
int
netisr_handle(int num, struct mbuf *m)
{
	struct netisr *ni;
	struct netmsg_packet *pmsg;
	lwkt_port_t port;

	/*
	 * Get the protocol port based on the packet hash
	 */
	KASSERT((m->m_flags & M_HASH), ("packet not characterized"));
	port = netisr_hashport(m->m_pkthdr.hash);
	KASSERT(&curthread->td_msgport == port, ("wrong msgport"));

	KASSERT((num > 0 && num <= NELEM(netisrs)), ("bad isr %d", num));
	ni = &netisrs[num];
	if (ni->ni_handler == NULL) {
		kprintf("unregistered isr %d\n", num);
		m_freem(m);
		return EIO;
	}

	/*
	 * Initialize the netmsg, and run the handler directly.
	 */
	pmsg = &m->m_hdr.mh_netmsg;
	netmsg_init(&pmsg->base, NULL, &netisr_apanic_rport,
		    0, ni->ni_handler);
	pmsg->nm_packet = m;
	pmsg->base.lmsg.u.ms_result = num;
	ni->ni_handler((netmsg_t)&pmsg->base);

	return 0;
}

/*
 * Pre-characterization of a deeper portion of the packet for the
 * requested isr.
 *
 * The base of the ISR type (e.g. IP) that we want to characterize is
 * at (hoff) relative to the beginning of the mbuf.  This allows
 * e.g. ether_characterize() to not have to adjust the m_data/m_len.
 */
void
netisr_characterize(int num, struct mbuf **mp, int hoff)
{
	struct netisr *ni;
	struct mbuf *m;

	/*
	 * Validation
	 */
	m = *mp;
	KKASSERT(m != NULL);

	if (num < 0 || num >= NETISR_MAX) {
		if (num == NETISR_MAX) {
			m->m_flags |= M_HASH;
			m->m_pkthdr.hash = 0;
			return;
		}
		panic("Bad isr %d", num);
	}

	/*
	 * Valid netisr?
	 */
	ni = &netisrs[num];
	if (ni->ni_handler == NULL) {
		kprintf("Unregistered isr %d\n", num);
		m_freem(m);
		*mp = NULL;
	}

	/*
	 * Characterize the packet
	 */
	if ((m->m_flags & M_HASH) == 0) {
		ni->ni_hashfn(mp, hoff);
		m = *mp;
		if (m && (m->m_flags & M_HASH) == 0)
			kprintf("netisr_queue(%d): packet hash failed\n", num);
	}
}

void
netisr_register(int num, netisr_fn_t handler, netisr_hashfn_t hashfn)
{
	struct netisr *ni;

	KASSERT((num > 0 && num <= NELEM(netisrs)),
		("netisr_register: bad isr %d", num));
	KKASSERT(handler != NULL);

	if (hashfn == NULL)
		hashfn = netisr_hashfn0;

	ni = &netisrs[num];

	ni->ni_handler = handler;
	ni->ni_hashck = netisr_nohashck;
	ni->ni_hashfn = hashfn;
	netmsg_init(&ni->ni_netmsg, NULL, &netisr_adone_rport, 0, NULL);
}

void
netisr_register_hashcheck(int num, netisr_hashck_t hashck)
{
	struct netisr *ni;

	KASSERT((num > 0 && num <= NELEM(netisrs)),
		("netisr_register: bad isr %d", num));

	ni = &netisrs[num];
	ni->ni_hashck = hashck;
}

void
netisr_register_rollup(netisr_ru_t ru_func, int prio)
{
	struct netmsg_rollup *new_ru, *ru;

	new_ru = kmalloc(sizeof(*new_ru), M_TEMP, M_WAITOK|M_ZERO);
	new_ru->ru_func = ru_func;
	new_ru->ru_prio = prio;

	/*
	 * Higher priority "rollup" appears first
	 */
	TAILQ_FOREACH(ru, &netrulist, ru_entry) {
		if (ru->ru_prio < new_ru->ru_prio) {
			TAILQ_INSERT_BEFORE(ru, new_ru, ru_entry);
			return;
		}
	}
	TAILQ_INSERT_TAIL(&netrulist, new_ru, ru_entry);
}

/*
 * Return a default protocol control message processing thread port
 */
lwkt_port_t
cpu0_ctlport(int cmd __unused, struct sockaddr *sa __unused,
	     void *extra __unused)
{
	return (&netisr_cpu[0].td_msgport);
}

/*
 * This is a default netisr packet characterization function which
 * sets M_HASH.  If a netisr is registered with a NULL hashfn function
 * this one is assigned.
 *
 * This function makes no attempt to validate the packet.
 */
static void
netisr_hashfn0(struct mbuf **mp, int hoff __unused)
{
	struct mbuf *m = *mp;

	m->m_flags |= M_HASH;
	m->m_pkthdr.hash = 0;
}

/*
 * schednetisr() is used to call the netisr handler from the appropriate
 * netisr thread for polling and other purposes.
 *
 * This function may be called from a hard interrupt or IPI and must be
 * MP SAFE and non-blocking.  We use a fixed per-cpu message instead of
 * trying to allocate one.  We must get ourselves onto the target cpu
 * to safely check the MSGF_DONE bit on the message but since the message
 * will be sent to that cpu anyway this does not add any extra work beyond
 * what lwkt_sendmsg() would have already had to do to schedule the target
 * thread.
 */
static void
schednetisr_remote(void *data)
{
	int num = (int)(intptr_t)data;
	struct netisr *ni = &netisrs[num];
	lwkt_port_t port = &netisr_cpu[0].td_msgport;
	netmsg_base_t pmsg;

	pmsg = &netisrs[num].ni_netmsg;
	if (pmsg->lmsg.ms_flags & MSGF_DONE) {
		netmsg_init(pmsg, NULL, &netisr_adone_rport, 0, ni->ni_handler);
		pmsg->lmsg.u.ms_result = num;
		lwkt_sendmsg(port, &pmsg->lmsg);
	}
}

void
schednetisr(int num)
{
	KASSERT((num > 0 && num <= NELEM(netisrs)),
		("schednetisr: bad isr %d", num));
	KKASSERT(netisrs[num].ni_handler != NULL);
	if (mycpu->gd_cpuid != 0) {
		lwkt_send_ipiq(globaldata_find(0),
			       schednetisr_remote, (void *)(intptr_t)num);
	} else {
		crit_enter();
		schednetisr_remote((void *)(intptr_t)num);
		crit_exit();
	}
}

static void
netisr_barrier_dispatch(netmsg_t nmsg)
{
	struct netmsg_barrier *msg = (struct netmsg_barrier *)nmsg;

	atomic_clear_cpumask(msg->br_cpumask, mycpu->gd_cpumask);
	if (*msg->br_cpumask == 0)
		wakeup(msg->br_cpumask);

	for (;;) {
		uint32_t done = msg->br_done;

		cpu_ccfence();
		if ((done & NETISR_BR_NOTDONE) == 0)
			break;

		tsleep_interlock(&msg->br_done, 0);
		if (atomic_cmpset_int(&msg->br_done,
		    done, done | NETISR_BR_WAITDONE))
			tsleep(&msg->br_done, PINTERLOCKED, "nbrdsp", 0);
	}

	lwkt_replymsg(&nmsg->lmsg, 0);
}

struct netisr_barrier *
netisr_barrier_create(void)
{
	struct netisr_barrier *br;

	br = kmalloc(sizeof(*br), M_LWKTMSG, M_WAITOK | M_ZERO);
	return br;
}

void
netisr_barrier_set(struct netisr_barrier *br)
{
	volatile cpumask_t other_cpumask;
	int i, cur_cpuid;

	KKASSERT(&curthread->td_msgport == netisr_cpuport(0));
	KKASSERT(!br->br_isset);

	other_cpumask = mycpu->gd_other_cpus & smp_active_mask;
	cur_cpuid = mycpuid;

	for (i = 0; i < ncpus; ++i) {
		struct netmsg_barrier *msg;

		if (i == cur_cpuid)
			continue;

		msg = kmalloc(sizeof(struct netmsg_barrier),
			      M_LWKTMSG, M_WAITOK);
		netmsg_init(&msg->base, NULL, &netisr_afree_rport,
			    MSGF_PRIORITY, netisr_barrier_dispatch);
		msg->br_cpumask = &other_cpumask;
		msg->br_done = NETISR_BR_NOTDONE;

		KKASSERT(br->br_msgs[i] == NULL);
		br->br_msgs[i] = msg;
	}

	for (i = 0; i < ncpus; ++i) {
		if (i == cur_cpuid)
			continue;
		lwkt_sendmsg(netisr_cpuport(i), &br->br_msgs[i]->base.lmsg);
	}

	while (other_cpumask != 0) {
		tsleep_interlock(&other_cpumask, 0);
		if (other_cpumask != 0)
			tsleep(&other_cpumask, PINTERLOCKED, "nbrset", 0);
	}
	br->br_isset = 1;
}

void
netisr_barrier_rem(struct netisr_barrier *br)
{
	int i, cur_cpuid;

	KKASSERT(&curthread->td_msgport == netisr_cpuport(0));
	KKASSERT(br->br_isset);

	cur_cpuid = mycpuid;
	for (i = 0; i < ncpus; ++i) {
		struct netmsg_barrier *msg = br->br_msgs[i];
		uint32_t done;

		msg = br->br_msgs[i];
		br->br_msgs[i] = NULL;

		if (i == cur_cpuid)
			continue;

		done = atomic_swap_int(&msg->br_done, 0);
		if (done & NETISR_BR_WAITDONE)
			wakeup(&msg->br_done);
	}
	br->br_isset = 0;
}

static void
netisr_nohashck(struct mbuf *m, const struct pktinfo *pi __unused)
{
	m->m_flags &= ~M_HASH;
}

void
netisr_hashcheck(int num, struct mbuf *m, const struct pktinfo *pi)
{
	struct netisr *ni;

	if (num < 0 || num >= NETISR_MAX)
		panic("Bad isr %d", num);

	/*
	 * Valid netisr?
	 */
	ni = &netisrs[num];
	if (ni->ni_handler == NULL)
		panic("Unregistered isr %d", num);

	ni->ni_hashck(m, pi);
}
