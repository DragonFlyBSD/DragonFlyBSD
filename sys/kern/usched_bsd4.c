/*
 * Copyright (c) 1999 Peter Wemm <peter@FreeBSD.org>
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
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $DragonFly: src/sys/kern/usched_bsd4.c,v 1.26 2008/11/01 23:31:19 dillon Exp $
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/queue.h>
#include <sys/proc.h>
#include <sys/rtprio.h>
#include <sys/uio.h>
#include <sys/sysctl.h>
#include <sys/resourcevar.h>
#include <sys/spinlock.h>
#include <machine/cpu.h>
#include <machine/smp.h>

#include <sys/thread2.h>
#include <sys/spinlock2.h>
#include <sys/mplock2.h>

/*
 * Priorities.  Note that with 32 run queues per scheduler each queue
 * represents four priority levels.
 */

#define MAXPRI			128
#define PRIMASK			(MAXPRI - 1)
#define PRIBASE_REALTIME	0
#define PRIBASE_NORMAL		MAXPRI
#define PRIBASE_IDLE		(MAXPRI * 2)
#define PRIBASE_THREAD		(MAXPRI * 3)
#define PRIBASE_NULL		(MAXPRI * 4)

#define NQS	32			/* 32 run queues. */
#define PPQ	(MAXPRI / NQS)		/* priorities per queue */
#define PPQMASK	(PPQ - 1)

/*
 * NICEPPQ	- number of nice units per priority queue
 * ESTCPURAMP	- number of scheduler ticks for estcpu to switch queues
 *
 * ESTCPUPPQ	- number of estcpu units per priority queue
 * ESTCPUMAX	- number of estcpu units
 * ESTCPUINCR	- amount we have to increment p_estcpu per scheduling tick at
 *		  100% cpu.
 */
#define NICEPPQ		2
#define ESTCPURAMP	4
#define ESTCPUPPQ	512
#define ESTCPUMAX	(ESTCPUPPQ * NQS)
#define ESTCPUINCR	(ESTCPUPPQ / ESTCPURAMP)
#define PRIO_RANGE	(PRIO_MAX - PRIO_MIN + 1)

#define ESTCPULIM(v)	min((v), ESTCPUMAX)

TAILQ_HEAD(rq, lwp);

#define lwp_priority	lwp_usdata.bsd4.priority
#define lwp_rqindex	lwp_usdata.bsd4.rqindex
#define lwp_origcpu	lwp_usdata.bsd4.origcpu
#define lwp_estcpu	lwp_usdata.bsd4.estcpu
#define lwp_rqtype	lwp_usdata.bsd4.rqtype

static void bsd4_acquire_curproc(struct lwp *lp);
static void bsd4_release_curproc(struct lwp *lp);
static void bsd4_select_curproc(globaldata_t gd);
static void bsd4_setrunqueue(struct lwp *lp);
static void bsd4_schedulerclock(struct lwp *lp, sysclock_t period,
				sysclock_t cpstamp);
static void bsd4_recalculate_estcpu(struct lwp *lp);
static void bsd4_resetpriority(struct lwp *lp);
static void bsd4_forking(struct lwp *plp, struct lwp *lp);
static void bsd4_exiting(struct lwp *plp, struct lwp *lp);
static void bsd4_yield(struct lwp *lp);

#ifdef SMP
static void need_user_resched_remote(void *dummy);
#endif
static struct lwp *chooseproc_locked(struct lwp *chklp);
static void bsd4_remrunqueue_locked(struct lwp *lp);
static void bsd4_setrunqueue_locked(struct lwp *lp);

struct usched usched_bsd4 = {
	{ NULL },
	"bsd4", "Original DragonFly Scheduler",
	NULL,			/* default registration */
	NULL,			/* default deregistration */
	bsd4_acquire_curproc,
	bsd4_release_curproc,
	bsd4_setrunqueue,
	bsd4_schedulerclock,
	bsd4_recalculate_estcpu,
	bsd4_resetpriority,
	bsd4_forking,
	bsd4_exiting,
	NULL,			/* setcpumask not supported */
	bsd4_yield
};

struct usched_bsd4_pcpu {
	struct thread helper_thread;
	short	rrcount;
	short	upri;
	struct lwp *uschedcp;
};

typedef struct usched_bsd4_pcpu	*bsd4_pcpu_t;

/*
 * We have NQS (32) run queues per scheduling class.  For the normal
 * class, there are 128 priorities scaled onto these 32 queues.  New
 * processes are added to the last entry in each queue, and processes
 * are selected for running by taking them from the head and maintaining
 * a simple FIFO arrangement.  Realtime and Idle priority processes have
 * and explicit 0-31 priority which maps directly onto their class queue
 * index.  When a queue has something in it, the corresponding bit is
 * set in the queuebits variable, allowing a single read to determine
 * the state of all 32 queues and then a ffs() to find the first busy
 * queue.
 */
static struct rq bsd4_queues[NQS];
static struct rq bsd4_rtqueues[NQS];
static struct rq bsd4_idqueues[NQS];
static u_int32_t bsd4_queuebits;
static u_int32_t bsd4_rtqueuebits;
static u_int32_t bsd4_idqueuebits;
static cpumask_t bsd4_curprocmask = -1;	/* currently running a user process */
static cpumask_t bsd4_rdyprocmask;	/* ready to accept a user process */
static int	 bsd4_runqcount;
#ifdef SMP
static volatile int bsd4_scancpu;
#endif
static struct spinlock bsd4_spin;
static struct usched_bsd4_pcpu bsd4_pcpu[MAXCPU];

SYSCTL_INT(_debug, OID_AUTO, bsd4_runqcount, CTLFLAG_RD, &bsd4_runqcount, 0,
    "Number of run queues");
#ifdef INVARIANTS
static int usched_nonoptimal;
SYSCTL_INT(_debug, OID_AUTO, usched_nonoptimal, CTLFLAG_RW,
        &usched_nonoptimal, 0, "acquire_curproc() was not optimal");
static int usched_optimal;
SYSCTL_INT(_debug, OID_AUTO, usched_optimal, CTLFLAG_RW,
        &usched_optimal, 0, "acquire_curproc() was optimal");
#endif
static int usched_debug = -1;
SYSCTL_INT(_debug, OID_AUTO, scdebug, CTLFLAG_RW, &usched_debug, 0,
    "Print debug information for this pid");
#ifdef SMP
static int remote_resched_nonaffinity;
static int remote_resched_affinity;
static int choose_affinity;
SYSCTL_INT(_debug, OID_AUTO, remote_resched_nonaffinity, CTLFLAG_RD,
        &remote_resched_nonaffinity, 0, "Number of remote rescheds");
SYSCTL_INT(_debug, OID_AUTO, remote_resched_affinity, CTLFLAG_RD,
        &remote_resched_affinity, 0, "Number of remote rescheds");
SYSCTL_INT(_debug, OID_AUTO, choose_affinity, CTLFLAG_RD,
        &choose_affinity, 0, "chooseproc() was smart");
#endif

static int usched_bsd4_rrinterval = (ESTCPUFREQ + 9) / 10;
SYSCTL_INT(_kern, OID_AUTO, usched_bsd4_rrinterval, CTLFLAG_RW,
        &usched_bsd4_rrinterval, 0, "");
static int usched_bsd4_decay = ESTCPUINCR / 2;
SYSCTL_INT(_kern, OID_AUTO, usched_bsd4_decay, CTLFLAG_RW,
        &usched_bsd4_decay, 0, "");

/*
 * Initialize the run queues at boot time.
 */
static void
rqinit(void *dummy)
{
	int i;

	spin_init(&bsd4_spin);
	for (i = 0; i < NQS; i++) {
		TAILQ_INIT(&bsd4_queues[i]);
		TAILQ_INIT(&bsd4_rtqueues[i]);
		TAILQ_INIT(&bsd4_idqueues[i]);
	}
	atomic_clear_cpumask(&bsd4_curprocmask, 1);
}
SYSINIT(runqueue, SI_BOOT2_USCHED, SI_ORDER_FIRST, rqinit, NULL)

/*
 * BSD4_ACQUIRE_CURPROC
 *
 * This function is called when the kernel intends to return to userland.
 * It is responsible for making the thread the current designated userland
 * thread for this cpu, blocking if necessary.
 *
 * The kernel has already depressed our LWKT priority so we must not switch
 * until we have either assigned or disposed of the thread.
 *
 * WARNING! THIS FUNCTION IS ALLOWED TO CAUSE THE CURRENT THREAD TO MIGRATE
 * TO ANOTHER CPU!  Because most of the kernel assumes that no migration will
 * occur, this function is called only under very controlled circumstances.
 *
 * MPSAFE
 */
static void
bsd4_acquire_curproc(struct lwp *lp)
{
	globaldata_t gd;
	bsd4_pcpu_t dd;
	struct lwp *olp;

	crit_enter();
	bsd4_recalculate_estcpu(lp);

	/*
	 * If a reschedule was requested give another thread the
	 * driver's seat.
	 */
	if (user_resched_wanted()) {
		clear_user_resched();
		bsd4_release_curproc(lp);
	}

	/*
	 * Loop until we are the current user thread
	 */
	do {
		/*
		 * Reload after a switch or setrunqueue/switch possibly
		 * moved us to another cpu.
		 */
		/*clear_lwkt_resched();*/
		gd = mycpu;
		dd = &bsd4_pcpu[gd->gd_cpuid];

		/*
		 * Become the currently scheduled user thread for this cpu
		 * if we can do so trivially.
		 *
		 * We can steal another thread's current thread designation
		 * on this cpu since if we are running that other thread
		 * must not be, so we can safely deschedule it.
		 */
		if (dd->uschedcp == lp) {
			/*
			 * We are already the current lwp (hot path).
			 */
			dd->upri = lp->lwp_priority;
		} else if (dd->uschedcp == NULL) {
			/*
			 * We can trivially become the current lwp.
			 */
			atomic_set_cpumask(&bsd4_curprocmask, gd->gd_cpumask);
			dd->uschedcp = lp;
			dd->upri = lp->lwp_priority;
		} else if (dd->upri > lp->lwp_priority) {
			/*
			 * We can steal the current lwp designation from the
			 * olp that was previously assigned to this cpu.
			 */
			olp = dd->uschedcp;
			dd->uschedcp = lp;
			dd->upri = lp->lwp_priority;
			lwkt_deschedule(olp->lwp_thread);
			bsd4_setrunqueue(olp);
		} else {
			/*
			 * We cannot become the current lwp, place the lp
			 * on the bsd4 run-queue and deschedule ourselves.
			 */
			lwkt_deschedule(lp->lwp_thread);
			bsd4_setrunqueue(lp);
			lwkt_switch();
		}

		/*
		 * Other threads at our current user priority have already
		 * put in their bids, but we must run any kernel threads
		 * at higher priorities, and we could lose our bid to
		 * another thread trying to return to user mode in the
		 * process.
		 *
		 * If we lose our bid we will be descheduled and put on
		 * the run queue.  When we are reactivated we will have
		 * another chance.
		 */
		if (lwkt_resched_wanted() ||
		    lp->lwp_thread->td_fairq_accum < 0) {
			lwkt_switch();
		}
	} while (dd->uschedcp != lp);

	crit_exit();
	KKASSERT((lp->lwp_flag & LWP_ONRUNQ) == 0);
}

/*
 * BSD4_RELEASE_CURPROC
 *
 * This routine detaches the current thread from the userland scheduler,
 * usually because the thread needs to run or block in the kernel (at
 * kernel priority) for a while.
 *
 * This routine is also responsible for selecting a new thread to
 * make the current thread.
 *
 * NOTE: This implementation differs from the dummy example in that
 * bsd4_select_curproc() is able to select the current process, whereas
 * dummy_select_curproc() is not able to select the current process.
 * This means we have to NULL out uschedcp.
 *
 * Additionally, note that we may already be on a run queue if releasing
 * via the lwkt_switch() in bsd4_setrunqueue().
 *
 * MPSAFE
 */
static void
bsd4_release_curproc(struct lwp *lp)
{
	globaldata_t gd = mycpu;
	bsd4_pcpu_t dd = &bsd4_pcpu[gd->gd_cpuid];

	if (dd->uschedcp == lp) {
		crit_enter();
		KKASSERT((lp->lwp_flag & LWP_ONRUNQ) == 0);
		dd->uschedcp = NULL;	/* don't let lp be selected */
		dd->upri = PRIBASE_NULL;
		atomic_clear_cpumask(&bsd4_curprocmask, gd->gd_cpumask);
		bsd4_select_curproc(gd);
		crit_exit();
	}
}

/*
 * BSD4_SELECT_CURPROC
 *
 * Select a new current process for this cpu and clear any pending user
 * reschedule request.  The cpu currently has no current process.
 *
 * This routine is also responsible for equal-priority round-robining,
 * typically triggered from bsd4_schedulerclock().  In our dummy example
 * all the 'user' threads are LWKT scheduled all at once and we just
 * call lwkt_switch().
 *
 * The calling process is not on the queue and cannot be selected.
 *
 * MPSAFE
 */
static
void
bsd4_select_curproc(globaldata_t gd)
{
	bsd4_pcpu_t dd = &bsd4_pcpu[gd->gd_cpuid];
	struct lwp *nlp;
	int cpuid = gd->gd_cpuid;

	crit_enter_gd(gd);

	spin_lock(&bsd4_spin);
	if ((nlp = chooseproc_locked(dd->uschedcp)) != NULL) {
		atomic_set_cpumask(&bsd4_curprocmask, CPUMASK(cpuid));
		dd->upri = nlp->lwp_priority;
		dd->uschedcp = nlp;
		spin_unlock(&bsd4_spin);
#ifdef SMP
		lwkt_acquire(nlp->lwp_thread);
#endif
		lwkt_schedule(nlp->lwp_thread);
	} else {
		spin_unlock(&bsd4_spin);
	}
#if 0
	} else if (bsd4_runqcount && (bsd4_rdyprocmask & CPUMASK(cpuid))) {
		atomic_clear_cpumask(&bsd4_rdyprocmask, CPUMASK(cpuid));
		spin_unlock(&bsd4_spin);
		lwkt_schedule(&dd->helper_thread);
	} else {
		spin_unlock(&bsd4_spin);
	}
#endif
	crit_exit_gd(gd);
}

/*
 * BSD4_SETRUNQUEUE
 *
 * Place the specified lwp on the user scheduler's run queue.  This routine
 * must be called with the thread descheduled.  The lwp must be runnable.
 *
 * The thread may be the current thread as a special case.
 *
 * MPSAFE
 */
static void
bsd4_setrunqueue(struct lwp *lp)
{
	globaldata_t gd;
	bsd4_pcpu_t dd;
#ifdef SMP
	int cpuid;
	cpumask_t mask;
	cpumask_t tmpmask;
#endif

	/*
	 * First validate the process state relative to the current cpu.
	 * We don't need the spinlock for this, just a critical section.
	 * We are in control of the process.
	 */
	crit_enter();
	KASSERT(lp->lwp_stat == LSRUN, ("setrunqueue: lwp not LSRUN"));
	KASSERT((lp->lwp_flag & LWP_ONRUNQ) == 0,
	    ("lwp %d/%d already on runq! flag %08x/%08x", lp->lwp_proc->p_pid,
	     lp->lwp_tid, lp->lwp_proc->p_flag, lp->lwp_flag));
	KKASSERT((lp->lwp_thread->td_flags & TDF_RUNQ) == 0);

	/*
	 * Note: gd and dd are relative to the target thread's last cpu,
	 * NOT our current cpu.
	 */
	gd = lp->lwp_thread->td_gd;
	dd = &bsd4_pcpu[gd->gd_cpuid];

	/*
	 * This process is not supposed to be scheduled anywhere or assigned
	 * as the current process anywhere.  Assert the condition.
	 */
	KKASSERT(dd->uschedcp != lp);

#ifndef SMP
	/*
	 * If we are not SMP we do not have a scheduler helper to kick
	 * and must directly activate the process if none are scheduled.
	 *
	 * This is really only an issue when bootstrapping init since
	 * the caller in all other cases will be a user process, and
	 * even if released (dd->uschedcp == NULL), that process will
	 * kickstart the scheduler when it returns to user mode from
	 * the kernel.
	 */
	if (dd->uschedcp == NULL) {
		atomic_set_cpumask(&bsd4_curprocmask, gd->gd_cpumask);
		dd->uschedcp = lp;
		dd->upri = lp->lwp_priority;
		lwkt_schedule(lp->lwp_thread);
		crit_exit();
		return;
	}
#endif

#ifdef SMP
	/*
	 * XXX fixme.  Could be part of a remrunqueue/setrunqueue
	 * operation when the priority is recalculated, so TDF_MIGRATING
	 * may already be set.
	 */
	if ((lp->lwp_thread->td_flags & TDF_MIGRATING) == 0)
		lwkt_giveaway(lp->lwp_thread);
#endif

	/*
	 * We lose control of lp the moment we release the spinlock after
	 * having placed lp on the queue.  i.e. another cpu could pick it
	 * up and it could exit, or its priority could be further adjusted,
	 * or something like that.
	 */
	spin_lock(&bsd4_spin);
	bsd4_setrunqueue_locked(lp);

#ifdef SMP
	/*
	 * Kick the scheduler helper on one of the other cpu's
	 * and request a reschedule if appropriate.
	 *
	 * NOTE: We check all cpus whos rdyprocmask is set.  First we
	 *	 look for cpus without designated lps, then we look for
	 *	 cpus with designated lps with a worse priority than our
	 *	 process.
	 */
	++bsd4_scancpu;
	cpuid = (bsd4_scancpu & 0xFFFF) % ncpus;
	mask = ~bsd4_curprocmask & bsd4_rdyprocmask & lp->lwp_cpumask &
	       smp_active_mask;

	while (mask) {
		tmpmask = ~(CPUMASK(cpuid) - 1);
		if (mask & tmpmask)
			cpuid = BSFCPUMASK(mask & tmpmask);
		else
			cpuid = BSFCPUMASK(mask);
		gd = globaldata_find(cpuid);
		dd = &bsd4_pcpu[cpuid];

		if ((dd->upri & ~PPQMASK) >= (lp->lwp_priority & ~PPQMASK))
			goto found;
		mask &= ~CPUMASK(cpuid);
	}

	/*
	 * Then cpus which might have a currently running lp
	 */
	mask = bsd4_curprocmask & bsd4_rdyprocmask &
	       lp->lwp_cpumask & smp_active_mask;

	while (mask) {
		tmpmask = ~(CPUMASK(cpuid) - 1);
		if (mask & tmpmask)
			cpuid = BSFCPUMASK(mask & tmpmask);
		else
			cpuid = BSFCPUMASK(mask);
		gd = globaldata_find(cpuid);
		dd = &bsd4_pcpu[cpuid];

		if ((dd->upri & ~PPQMASK) > (lp->lwp_priority & ~PPQMASK))
			goto found;
		mask &= ~CPUMASK(cpuid);
	}

	/*
	 * If we cannot find a suitable cpu we reload from bsd4_scancpu
	 * and round-robin.  Other cpus will pickup as they release their
	 * current lwps or become ready.
	 *
	 * We only kick the target helper thread in this case, we do not
	 * set the user resched flag because
	 */
	cpuid = (bsd4_scancpu & 0xFFFF) % ncpus;
	gd = globaldata_find(cpuid);
	dd = &bsd4_pcpu[cpuid];
found:
	if (gd == mycpu) {
		spin_unlock(&bsd4_spin);
		if ((dd->upri & ~PPQMASK) > (lp->lwp_priority & ~PPQMASK)) {
			if (dd->uschedcp == NULL) {
				lwkt_schedule(&dd->helper_thread);
			} else {
				need_user_resched();
			}
		}
	} else {
		atomic_clear_cpumask(&bsd4_rdyprocmask, CPUMASK(cpuid));
		spin_unlock(&bsd4_spin);
		if ((dd->upri & ~PPQMASK) > (lp->lwp_priority & ~PPQMASK))
			lwkt_send_ipiq(gd, need_user_resched_remote, NULL);
		else
			lwkt_schedule(&dd->helper_thread);
	}
#else
	/*
	 * Request a reschedule if appropriate.
	 */
	spin_unlock(&bsd4_spin);
	if ((dd->upri & ~PPQMASK) > (lp->lwp_priority & ~PPQMASK)) {
		need_user_resched();
	}
#endif
	crit_exit();
}

/*
 * This routine is called from a systimer IPI.  It MUST be MP-safe and
 * the BGL IS NOT HELD ON ENTRY.  This routine is called at ESTCPUFREQ on
 * each cpu.
 *
 * MPSAFE
 */
static
void
bsd4_schedulerclock(struct lwp *lp, sysclock_t period, sysclock_t cpstamp)
{
	globaldata_t gd = mycpu;
	bsd4_pcpu_t dd = &bsd4_pcpu[gd->gd_cpuid];

	/*
	 * Do we need to round-robin?  We round-robin 10 times a second.
	 * This should only occur for cpu-bound batch processes.
	 */
	if (++dd->rrcount >= usched_bsd4_rrinterval) {
		dd->rrcount = 0;
		need_user_resched();
	}

	/*
	 * As the process accumulates cpu time p_estcpu is bumped and may
	 * push the process into another scheduling queue.  It typically
	 * takes 4 ticks to bump the queue.
	 */
	lp->lwp_estcpu = ESTCPULIM(lp->lwp_estcpu + ESTCPUINCR);

	/*
	 * Reducing p_origcpu over time causes more of our estcpu to be
	 * returned to the parent when we exit.  This is a small tweak
	 * for the batch detection heuristic.
	 */
	if (lp->lwp_origcpu)
		--lp->lwp_origcpu;

	/*
	 * Spinlocks also hold a critical section so there should not be
	 * any active.
	 */
	KKASSERT(gd->gd_spinlocks_wr == 0);

	bsd4_resetpriority(lp);
#if 0
	/*
	* if we can't call bsd4_resetpriority for some reason we must call
	 * need user_resched().
	 */
	need_user_resched();
#endif
}

/*
 * Called from acquire and from kern_synch's one-second timer (one of the
 * callout helper threads) with a critical section held. 
 *
 * Decay p_estcpu based on the number of ticks we haven't been running
 * and our p_nice.  As the load increases each process observes a larger
 * number of idle ticks (because other processes are running in them).
 * This observation leads to a larger correction which tends to make the
 * system more 'batchy'.
 *
 * Note that no recalculation occurs for a process which sleeps and wakes
 * up in the same tick.  That is, a system doing thousands of context
 * switches per second will still only do serious estcpu calculations
 * ESTCPUFREQ times per second.
 *
 * MPSAFE
 */
static
void 
bsd4_recalculate_estcpu(struct lwp *lp)
{
	globaldata_t gd = mycpu;
	sysclock_t cpbase;
	int loadfac;
	int ndecay;
	int nticks;
	int nleft;

	/*
	 * We have to subtract periodic to get the last schedclock
	 * timeout time, otherwise we would get the upcoming timeout.
	 * Keep in mind that a process can migrate between cpus and
	 * while the scheduler clock should be very close, boundary
	 * conditions could lead to a small negative delta.
	 */
	cpbase = gd->gd_schedclock.time - gd->gd_schedclock.periodic;

	if (lp->lwp_slptime > 1) {
		/*
		 * Too much time has passed, do a coarse correction.
		 */
		lp->lwp_estcpu = lp->lwp_estcpu >> 1;
		bsd4_resetpriority(lp);
		lp->lwp_cpbase = cpbase;
		lp->lwp_cpticks = 0;
	} else if (lp->lwp_cpbase != cpbase) {
		/*
		 * Adjust estcpu if we are in a different tick.  Don't waste
		 * time if we are in the same tick. 
		 * 
		 * First calculate the number of ticks in the measurement
		 * interval.  The nticks calculation can wind up 0 due to
		 * a bug in the handling of lwp_slptime  (as yet not found),
		 * so make sure we do not get a divide by 0 panic.
		 */
		nticks = (cpbase - lp->lwp_cpbase) / gd->gd_schedclock.periodic;
		if (nticks <= 0)
			nticks = 1;
		updatepcpu(lp, lp->lwp_cpticks, nticks);

		if ((nleft = nticks - lp->lwp_cpticks) < 0)
			nleft = 0;
		if (usched_debug == lp->lwp_proc->p_pid) {
			kprintf("pid %d tid %d estcpu %d cpticks %d nticks %d nleft %d",
				lp->lwp_proc->p_pid, lp->lwp_tid, lp->lwp_estcpu,
				lp->lwp_cpticks, nticks, nleft);
		}

		/*
		 * Calculate a decay value based on ticks remaining scaled
		 * down by the instantanious load and p_nice.
		 */
		if ((loadfac = bsd4_runqcount) < 2)
			loadfac = 2;
		ndecay = nleft * usched_bsd4_decay * 2 * 
			(PRIO_MAX * 2 - lp->lwp_proc->p_nice) / (loadfac * PRIO_MAX * 2);

		/*
		 * Adjust p_estcpu.  Handle a border case where batch jobs
		 * can get stalled long enough to decay to zero when they
		 * shouldn't.
		 */
		if (lp->lwp_estcpu > ndecay * 2)
			lp->lwp_estcpu -= ndecay;
		else
			lp->lwp_estcpu >>= 1;

		if (usched_debug == lp->lwp_proc->p_pid)
			kprintf(" ndecay %d estcpu %d\n", ndecay, lp->lwp_estcpu);
		bsd4_resetpriority(lp);
		lp->lwp_cpbase = cpbase;
		lp->lwp_cpticks = 0;
	}
}

/*
 * Compute the priority of a process when running in user mode.
 * Arrange to reschedule if the resulting priority is better
 * than that of the current process.
 *
 * This routine may be called with any process.
 *
 * This routine is called by fork1() for initial setup with the process
 * of the run queue, and also may be called normally with the process on or
 * off the run queue.
 *
 * MPSAFE
 */
static void
bsd4_resetpriority(struct lwp *lp)
{
	bsd4_pcpu_t dd;
	int newpriority;
	u_short newrqtype;
	int reschedcpu;

	/*
	 * Calculate the new priority and queue type
	 */
	crit_enter();
	spin_lock(&bsd4_spin);

	newrqtype = lp->lwp_rtprio.type;

	switch(newrqtype) {
	case RTP_PRIO_REALTIME:
	case RTP_PRIO_FIFO:
		newpriority = PRIBASE_REALTIME +
			     (lp->lwp_rtprio.prio & PRIMASK);
		break;
	case RTP_PRIO_NORMAL:
		newpriority = (lp->lwp_proc->p_nice - PRIO_MIN) * PPQ / NICEPPQ;
		newpriority += lp->lwp_estcpu * PPQ / ESTCPUPPQ;
		newpriority = newpriority * MAXPRI / (PRIO_RANGE * PPQ /
			      NICEPPQ + ESTCPUMAX * PPQ / ESTCPUPPQ);
		newpriority = PRIBASE_NORMAL + (newpriority & PRIMASK);
		break;
	case RTP_PRIO_IDLE:
		newpriority = PRIBASE_IDLE + (lp->lwp_rtprio.prio & PRIMASK);
		break;
	case RTP_PRIO_THREAD:
		newpriority = PRIBASE_THREAD + (lp->lwp_rtprio.prio & PRIMASK);
		break;
	default:
		panic("Bad RTP_PRIO %d", newrqtype);
		/* NOT REACHED */
	}

	/*
	 * The newpriority incorporates the queue type so do a simple masked
	 * check to determine if the process has moved to another queue.  If
	 * it has, and it is currently on a run queue, then move it.
	 */
	if ((lp->lwp_priority ^ newpriority) & ~PPQMASK) {
		lp->lwp_priority = newpriority;
		if (lp->lwp_flag & LWP_ONRUNQ) {
			bsd4_remrunqueue_locked(lp);
			lp->lwp_rqtype = newrqtype;
			lp->lwp_rqindex = (newpriority & PRIMASK) / PPQ;
			bsd4_setrunqueue_locked(lp);
			reschedcpu = lp->lwp_thread->td_gd->gd_cpuid;
		} else {
			lp->lwp_rqtype = newrqtype;
			lp->lwp_rqindex = (newpriority & PRIMASK) / PPQ;
			reschedcpu = -1;
		}
	} else {
		lp->lwp_priority = newpriority;
		reschedcpu = -1;
	}

	/*
	 * Determine if we need to reschedule the target cpu.  This only
	 * occurs if the LWP is already on a scheduler queue, which means
	 * that idle cpu notification has already occured.  At most we
	 * need only issue a need_user_resched() on the appropriate cpu.
	 *
	 * The LWP may be owned by a CPU different from the current one,
	 * in which case dd->uschedcp may be modified without an MP lock
	 * or a spinlock held.  The worst that happens is that the code
	 * below causes a spurious need_user_resched() on the target CPU
	 * and dd->pri to be wrong for a short period of time, both of
	 * which are harmless.
	 */
	if (reschedcpu >= 0) {
		dd = &bsd4_pcpu[reschedcpu];
		if ((bsd4_rdyprocmask & CPUMASK(reschedcpu)) &&
		    (dd->upri & ~PRIMASK) > (lp->lwp_priority & ~PRIMASK)) {
#ifdef SMP
			if (reschedcpu == mycpu->gd_cpuid) {
				spin_unlock(&bsd4_spin);
				need_user_resched();
			} else {
				spin_unlock(&bsd4_spin);
				atomic_clear_cpumask(&bsd4_rdyprocmask,
						     CPUMASK(reschedcpu));
				lwkt_send_ipiq(lp->lwp_thread->td_gd,
					       need_user_resched_remote, NULL);
			}
#else
			spin_unlock(&bsd4_spin);
			need_user_resched();
#endif
		} else {
			spin_unlock(&bsd4_spin);
		}
	} else {
		spin_unlock(&bsd4_spin);
	}
	crit_exit();
}

/*
 * MPSAFE
 */
static
void
bsd4_yield(struct lwp *lp) 
{
#if 0
	/* FUTURE (or something similar) */
	switch(lp->lwp_rqtype) {
	case RTP_PRIO_NORMAL:
		lp->lwp_estcpu = ESTCPULIM(lp->lwp_estcpu + ESTCPUINCR);
		break;
	default:
		break;
	}
#endif
        need_user_resched();
}

/*
 * Called from fork1() when a new child process is being created.
 *
 * Give the child process an initial estcpu that is more batch then
 * its parent and dock the parent for the fork (but do not
 * reschedule the parent).   This comprises the main part of our batch
 * detection heuristic for both parallel forking and sequential execs.
 *
 * Interactive processes will decay the boosted estcpu quickly while batch
 * processes will tend to compound it.
 * XXX lwp should be "spawning" instead of "forking"
 *
 * MPSAFE
 */
static void
bsd4_forking(struct lwp *plp, struct lwp *lp)
{
	lp->lwp_estcpu = ESTCPULIM(plp->lwp_estcpu + ESTCPUPPQ);
	lp->lwp_origcpu = lp->lwp_estcpu;
	plp->lwp_estcpu = ESTCPULIM(plp->lwp_estcpu + ESTCPUPPQ);
}

/*
 * Called when the parent reaps a child.   Propogate cpu use by the child
 * back to the parent.
 *
 * MPSAFE
 */
static void
bsd4_exiting(struct lwp *plp, struct lwp *lp)
{
	int delta;

	if (plp->lwp_proc->p_pid != 1) {
		delta = lp->lwp_estcpu - lp->lwp_origcpu;
		if (delta > 0)
			plp->lwp_estcpu = ESTCPULIM(plp->lwp_estcpu + delta);
	}
}


/*
 * chooseproc() is called when a cpu needs a user process to LWKT schedule,
 * it selects a user process and returns it.  If chklp is non-NULL and chklp
 * has a better or equal priority then the process that would otherwise be
 * chosen, NULL is returned.
 *
 * Until we fix the RUNQ code the chklp test has to be strict or we may
 * bounce between processes trying to acquire the current process designation.
 *
 * MPSAFE - must be called with bsd4_spin exclusive held.  The spinlock is
 *	    left intact through the entire routine.
 */
static
struct lwp *
chooseproc_locked(struct lwp *chklp)
{
	struct lwp *lp;
	struct rq *q;
	u_int32_t *which, *which2;
	u_int32_t pri;
	u_int32_t rtqbits;
	u_int32_t tsqbits;
	u_int32_t idqbits;
	cpumask_t cpumask;

	rtqbits = bsd4_rtqueuebits;
	tsqbits = bsd4_queuebits;
	idqbits = bsd4_idqueuebits;
	cpumask = mycpu->gd_cpumask;

#ifdef SMP
again:
#endif
	if (rtqbits) {
		pri = bsfl(rtqbits);
		q = &bsd4_rtqueues[pri];
		which = &bsd4_rtqueuebits;
		which2 = &rtqbits;
	} else if (tsqbits) {
		pri = bsfl(tsqbits);
		q = &bsd4_queues[pri];
		which = &bsd4_queuebits;
		which2 = &tsqbits;
	} else if (idqbits) {
		pri = bsfl(idqbits);
		q = &bsd4_idqueues[pri];
		which = &bsd4_idqueuebits;
		which2 = &idqbits;
	} else {
		return NULL;
	}
	lp = TAILQ_FIRST(q);
	KASSERT(lp, ("chooseproc: no lwp on busy queue"));

#ifdef SMP
	while ((lp->lwp_cpumask & cpumask) == 0) {
		lp = TAILQ_NEXT(lp, lwp_procq);
		if (lp == NULL) {
			*which2 &= ~(1 << pri);
			goto again;
		}
	}
#endif

	/*
	 * If the passed lwp <chklp> is reasonably close to the selected
	 * lwp <lp>, return NULL (indicating that <chklp> should be kept).
	 * 
	 * Note that we must error on the side of <chklp> to avoid bouncing
	 * between threads in the acquire code.
	 */
	if (chklp) {
		if (chklp->lwp_priority < lp->lwp_priority + PPQ)
			return(NULL);
	}

#ifdef SMP
	/*
	 * If the chosen lwp does not reside on this cpu spend a few
	 * cycles looking for a better candidate at the same priority level.
	 * This is a fallback check, setrunqueue() tries to wakeup the
	 * correct cpu and is our front-line affinity.
	 */
	if (lp->lwp_thread->td_gd != mycpu &&
	    (chklp = TAILQ_NEXT(lp, lwp_procq)) != NULL
	) {
		if (chklp->lwp_thread->td_gd == mycpu) {
			++choose_affinity;
			lp = chklp;
		}
	}
#endif

	TAILQ_REMOVE(q, lp, lwp_procq);
	--bsd4_runqcount;
	if (TAILQ_EMPTY(q))
		*which &= ~(1 << pri);
	KASSERT((lp->lwp_flag & LWP_ONRUNQ) != 0, ("not on runq6!"));
	lp->lwp_flag &= ~LWP_ONRUNQ;
	return lp;
}

#ifdef SMP

static
void
need_user_resched_remote(void *dummy)
{
	globaldata_t gd = mycpu;
	bsd4_pcpu_t  dd = &bsd4_pcpu[gd->gd_cpuid];

	need_user_resched();
	lwkt_schedule(&dd->helper_thread);
}

#endif

/*
 * bsd4_remrunqueue_locked() removes a given process from the run queue
 * that it is on, clearing the queue busy bit if it becomes empty.
 *
 * Note that user process scheduler is different from the LWKT schedule.
 * The user process scheduler only manages user processes but it uses LWKT
 * underneath, and a user process operating in the kernel will often be
 * 'released' from our management.
 *
 * MPSAFE - bsd4_spin must be held exclusively on call
 */
static void
bsd4_remrunqueue_locked(struct lwp *lp)
{
	struct rq *q;
	u_int32_t *which;
	u_int8_t pri;

	KKASSERT(lp->lwp_flag & LWP_ONRUNQ);
	lp->lwp_flag &= ~LWP_ONRUNQ;
	--bsd4_runqcount;
	KKASSERT(bsd4_runqcount >= 0);

	pri = lp->lwp_rqindex;
	switch(lp->lwp_rqtype) {
	case RTP_PRIO_NORMAL:
		q = &bsd4_queues[pri];
		which = &bsd4_queuebits;
		break;
	case RTP_PRIO_REALTIME:
	case RTP_PRIO_FIFO:
		q = &bsd4_rtqueues[pri];
		which = &bsd4_rtqueuebits;
		break;
	case RTP_PRIO_IDLE:
		q = &bsd4_idqueues[pri];
		which = &bsd4_idqueuebits;
		break;
	default:
		panic("remrunqueue: invalid rtprio type");
		/* NOT REACHED */
	}
	TAILQ_REMOVE(q, lp, lwp_procq);
	if (TAILQ_EMPTY(q)) {
		KASSERT((*which & (1 << pri)) != 0,
			("remrunqueue: remove from empty queue"));
		*which &= ~(1 << pri);
	}
}

/*
 * bsd4_setrunqueue_locked()
 *
 * Add a process whos rqtype and rqindex had previously been calculated
 * onto the appropriate run queue.   Determine if the addition requires
 * a reschedule on a cpu and return the cpuid or -1.
 *
 * NOTE: Lower priorities are better priorities.
 *
 * MPSAFE - bsd4_spin must be held exclusively on call
 */
static void
bsd4_setrunqueue_locked(struct lwp *lp)
{
	struct rq *q;
	u_int32_t *which;
	int pri;

	KKASSERT((lp->lwp_flag & LWP_ONRUNQ) == 0);
	lp->lwp_flag |= LWP_ONRUNQ;
	++bsd4_runqcount;

	pri = lp->lwp_rqindex;

	switch(lp->lwp_rqtype) {
	case RTP_PRIO_NORMAL:
		q = &bsd4_queues[pri];
		which = &bsd4_queuebits;
		break;
	case RTP_PRIO_REALTIME:
	case RTP_PRIO_FIFO:
		q = &bsd4_rtqueues[pri];
		which = &bsd4_rtqueuebits;
		break;
	case RTP_PRIO_IDLE:
		q = &bsd4_idqueues[pri];
		which = &bsd4_idqueuebits;
		break;
	default:
		panic("remrunqueue: invalid rtprio type");
		/* NOT REACHED */
	}

	/*
	 * Add to the correct queue and set the appropriate bit.  If no
	 * lower priority (i.e. better) processes are in the queue then
	 * we want a reschedule, calculate the best cpu for the job.
	 *
	 * Always run reschedules on the LWPs original cpu.
	 */
	TAILQ_INSERT_TAIL(q, lp, lwp_procq);
	*which |= 1 << pri;
}

#ifdef SMP

/*
 * For SMP systems a user scheduler helper thread is created for each
 * cpu and is used to allow one cpu to wakeup another for the purposes of
 * scheduling userland threads from setrunqueue().
 *
 * UP systems do not need the helper since there is only one cpu.
 *
 * We can't use the idle thread for this because we might block.
 * Additionally, doing things this way allows us to HLT idle cpus
 * on MP systems.
 *
 * MPSAFE
 */
static void
sched_thread(void *dummy)
{
    globaldata_t gd;
    bsd4_pcpu_t  dd;
    struct lwp *nlp;
    cpumask_t mask;
    int cpuid;
#ifdef SMP
    cpumask_t tmpmask;
    int tmpid;
#endif

    gd = mycpu;
    cpuid = gd->gd_cpuid;	/* doesn't change */
    mask = gd->gd_cpumask;	/* doesn't change */
    dd = &bsd4_pcpu[cpuid];

    /*
     * Since we are woken up only when no user processes are scheduled
     * on a cpu, we can run at an ultra low priority.
     */
    lwkt_setpri_self(TDPRI_USER_SCHEDULER);

    for (;;) {
	/*
	 * We use the LWKT deschedule-interlock trick to avoid racing
	 * bsd4_rdyprocmask.  This means we cannot block through to the
	 * manual lwkt_switch() call we make below.
	 */
	crit_enter_gd(gd);
	lwkt_deschedule_self(gd->gd_curthread);
	spin_lock(&bsd4_spin);
	atomic_set_cpumask(&bsd4_rdyprocmask, mask);

	clear_user_resched();	/* This satisfied the reschedule request */
	dd->rrcount = 0;	/* Reset the round-robin counter */

	if ((bsd4_curprocmask & mask) == 0) {
		/*
		 * No thread is currently scheduled.
		 */
		KKASSERT(dd->uschedcp == NULL);
		if ((nlp = chooseproc_locked(NULL)) != NULL) {
			atomic_set_cpumask(&bsd4_curprocmask, mask);
			dd->upri = nlp->lwp_priority;
			dd->uschedcp = nlp;
			spin_unlock(&bsd4_spin);
			lwkt_acquire(nlp->lwp_thread);
			lwkt_schedule(nlp->lwp_thread);
		} else {
			spin_unlock(&bsd4_spin);
		}
	} else if (bsd4_runqcount) {
		if ((nlp = chooseproc_locked(dd->uschedcp)) != NULL) {
			dd->upri = nlp->lwp_priority;
			dd->uschedcp = nlp;
			spin_unlock(&bsd4_spin);
			lwkt_acquire(nlp->lwp_thread);
			lwkt_schedule(nlp->lwp_thread);
		} else {
			/*
			 * CHAINING CONDITION TRAIN
			 *
			 * We could not deal with the scheduler wakeup
			 * request on this cpu, locate a ready scheduler
			 * with no current lp assignment and chain to it.
			 *
			 * This ensures that a wakeup race which fails due
			 * to priority test does not leave other unscheduled
			 * cpus idle when the runqueue is not empty.
			 */
			tmpmask = ~bsd4_curprocmask & bsd4_rdyprocmask &
				  smp_active_mask;
			if (tmpmask) {
				tmpid = BSFCPUMASK(tmpmask);
				gd = globaldata_find(cpuid);
				dd = &bsd4_pcpu[cpuid];
				atomic_clear_cpumask(&bsd4_rdyprocmask,
						     CPUMASK(tmpid));
				spin_unlock(&bsd4_spin);
				lwkt_schedule(&dd->helper_thread);
			} else {
				spin_unlock(&bsd4_spin);
			}
		}
	} else {
		/*
		 * The runq is empty.
		 */
		spin_unlock(&bsd4_spin);
	}
	crit_exit_gd(gd);
	lwkt_switch();
    }
}

/*
 * Setup our scheduler helpers.  Note that curprocmask bit 0 has already
 * been cleared by rqinit() and we should not mess with it further.
 */
static void
sched_thread_cpu_init(void)
{
    int i;

    if (bootverbose)
	kprintf("start scheduler helpers on cpus:");

    for (i = 0; i < ncpus; ++i) {
	bsd4_pcpu_t dd = &bsd4_pcpu[i];
	cpumask_t mask = CPUMASK(i);

	if ((mask & smp_active_mask) == 0)
	    continue;

	if (bootverbose)
	    kprintf(" %d", i);

	lwkt_create(sched_thread, NULL, NULL, &dd->helper_thread, 
		    TDF_STOPREQ, i, "usched %d", i);

	/*
	 * Allow user scheduling on the target cpu.  cpu #0 has already
	 * been enabled in rqinit().
	 */
	if (i)
	    atomic_clear_cpumask(&bsd4_curprocmask, mask);
	atomic_set_cpumask(&bsd4_rdyprocmask, mask);
	dd->upri = PRIBASE_NULL;
    }
    if (bootverbose)
	kprintf("\n");
}
SYSINIT(uschedtd, SI_BOOT2_USCHED, SI_ORDER_SECOND,
	sched_thread_cpu_init, NULL)

#endif

