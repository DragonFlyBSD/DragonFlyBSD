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
 * $DragonFly: src/sys/kern/usched_bsd4.c,v 1.4 2005/11/14 18:50:05 dillon Exp $
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/queue.h>
#include <sys/proc.h>
#include <sys/rtprio.h>
#include <sys/thread2.h>
#include <sys/uio.h>
#include <sys/sysctl.h>
#include <sys/resourcevar.h>
#include <machine/ipl.h>
#include <machine/cpu.h>
#include <machine/smp.h>

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

static void bsd4_acquire_curproc(struct lwp *lp);
static void bsd4_release_curproc(struct lwp *lp);
static void bsd4_select_curproc(globaldata_t gd);
static void bsd4_setrunqueue(struct lwp *lp);
static void bsd4_remrunqueue(struct lwp *lp);
static void bsd4_schedulerclock(struct lwp *lp, sysclock_t period,
				sysclock_t cpstamp);
static void bsd4_resetpriority(struct lwp *lp);
static void bsd4_forking(struct lwp *plp, struct lwp *lp);
static void bsd4_exiting(struct lwp *plp, struct lwp *lp);

static void bsd4_recalculate_estcpu(struct lwp *lp);

struct usched usched_bsd4 = {
	{ NULL },
	"bsd4", "Original DragonFly Scheduler",
	bsd4_acquire_curproc,
	bsd4_release_curproc,
	bsd4_select_curproc,
	bsd4_setrunqueue,
	bsd4_remrunqueue,
	bsd4_schedulerclock,
	bsd4_recalculate_estcpu,
	bsd4_resetpriority,
	bsd4_forking,
	bsd4_exiting
};

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
static struct rq queues[NQS];
static struct rq rtqueues[NQS];
static struct rq idqueues[NQS];
static u_int32_t queuebits;
static u_int32_t rtqueuebits;
static u_int32_t idqueuebits;
static cpumask_t curprocmask = -1;	/* currently running a user process */
static cpumask_t rdyprocmask;		/* ready to accept a user process */
static int	 runqcount;
#ifdef SMP
static int	 scancpu;
#endif

SYSCTL_INT(_debug, OID_AUTO, runqcount, CTLFLAG_RD, &runqcount, 0, "");
#ifdef INVARIANTS
static int usched_nonoptimal;
SYSCTL_INT(_debug, OID_AUTO, usched_nonoptimal, CTLFLAG_RW,
        &usched_nonoptimal, 0, "acquire_curproc() was not optimal");
static int usched_optimal;
SYSCTL_INT(_debug, OID_AUTO, usched_optimal, CTLFLAG_RW,
        &usched_optimal, 0, "acquire_curproc() was optimal");
#endif
static int usched_debug = -1;
SYSCTL_INT(_debug, OID_AUTO, scdebug, CTLFLAG_RW, &usched_debug, 0, "");
#ifdef SMP
static int remote_resched = 1;
static int remote_resched_nonaffinity;
static int remote_resched_affinity;
static int choose_affinity;
SYSCTL_INT(_debug, OID_AUTO, remote_resched, CTLFLAG_RW,
        &remote_resched, 0, "Resched to another cpu");
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

	for (i = 0; i < NQS; i++) {
		TAILQ_INIT(&queues[i]);
		TAILQ_INIT(&rtqueues[i]);
		TAILQ_INIT(&idqueues[i]);
	}
	curprocmask &= ~1;
}
SYSINIT(runqueue, SI_SUB_RUN_QUEUE, SI_ORDER_FIRST, rqinit, NULL)

/*
 * chooseproc() is called when a cpu needs a user process to LWKT schedule,
 * it selects a user process and returns it.  If chkp is non-NULL and chkp
 * has a better or equal then the process that would otherwise be
 * chosen, NULL is returned.
 *
 * Until we fix the RUNQ code the chkp test has to be strict or we may
 * bounce between processes trying to acquire the current process designation.
 */
static
struct lwp *
chooseproc(struct lwp *chklp)
{
	struct lwp *lp;
	struct rq *q;
	u_int32_t *which;
	u_int32_t pri;

	if (rtqueuebits) {
		pri = bsfl(rtqueuebits);
		q = &rtqueues[pri];
		which = &rtqueuebits;
	} else if (queuebits) {
		pri = bsfl(queuebits);
		q = &queues[pri];
		which = &queuebits;
	} else if (idqueuebits) {
		pri = bsfl(idqueuebits);
		q = &idqueues[pri];
		which = &idqueuebits;
	} else {
		return NULL;
	}
	lp = TAILQ_FIRST(q);
	KASSERT(lp, ("chooseproc: no lwp on busy queue"));

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
	--runqcount;
	if (TAILQ_EMPTY(q))
		*which &= ~(1 << pri);
	KASSERT((lp->lwp_proc->p_flag & P_ONRUNQ) != 0, ("not on runq6!"));
	lp->lwp_proc->p_flag &= ~P_ONRUNQ;
	return lp;
}

#ifdef SMP
/*
 * called via an ipi message to reschedule on another cpu.
 */
static
void
need_user_resched_remote(void *dummy)
{
	need_user_resched();
}

#endif

/*
 * setrunqueue() 'wakes up' a 'user' process.  GIANT must be held.  The
 * user process may represent any user process, including the current
 * process.
 *
 * If P_PASSIVE_ACQ is set setrunqueue() will not wakeup potential target
 * cpus in an attempt to keep the process on the current cpu at least for
 * a little while to take advantage of locality of reference (e.g. fork/exec
 * or short fork/exit, and uio_yield()).
 *
 * CPU AFFINITY: cpu affinity is handled by attempting to either schedule
 * or (user level) preempt on the same cpu that a process was previously
 * scheduled to.  If we cannot do this but we are at enough of a higher
 * priority then the processes running on other cpus, we will allow the
 * process to be stolen by another cpu.
 *
 * WARNING! a thread can be acquired by another cpu the moment it is put
 * on the user scheduler's run queue AND we release the MP lock.  Since we
 * release the MP lock before switching out another cpu may begin stealing
 * our current thread before we are completely switched out!  The 
 * lwkt_acquire() function will stall until TDF_RUNNING is cleared on the
 * thread before stealing it.
 *
 * NOTE on need_user_resched() calls: we have to call need_user_resched()
 * if the new process is more important then the current process, or if
 * the new process is the current process and is now less important then
 * other processes.
 *
 * The associated thread must NOT be scheduled.  
 * The process must be runnable.
 * This must be called at splhigh().
 */
static void
bsd4_setrunqueue(struct lwp *lp)
{
	struct rq *q;
	struct globaldata *gd;
	int pri;
	int cpuid;
	u_int32_t needresched;
#ifdef SMP
	int count;
	cpumask_t mask;
#endif

	crit_enter();
	KASSERT(lp->lwp_proc->p_stat == SRUN, ("setrunqueue: proc not SRUN"));
	KASSERT((lp->lwp_proc->p_flag & P_ONRUNQ) == 0,
	    ("lwp %d/%d already on runq! flag %08x", lp->lwp_proc->p_pid,
	     lp->lwp_tid, lp->lwp_proc->p_flag));
	KKASSERT((lp->lwp_thread->td_flags & TDF_RUNQ) == 0);

	/*
	 * Note: gd is the gd of the TARGET thread's cpu, not our cpu.
	 */
	gd = lp->lwp_thread->td_gd;

	/*
	 * Because recalculate is only called once or twice for long sleeps,
	 * not every second forever while the process is sleeping, we have 
	 * to manually call it to resynchronize p_cpbase on wakeup or it
	 * will wrap if the process was sleeping long enough (e.g. ~10 min
	 * with the ACPI timer) and really mess up the nticks calculation.
	 */
	if (lp->lwp_slptime) {
	    bsd4_recalculate_estcpu(lp);
	    lp->lwp_slptime = 0;
	}
	/*
	 * We have not been released, make sure that we are not the currently
	 * designated process.
	 */
	KKASSERT(gd->gd_uschedcp != lp);

	/*
	 * Check cpu affinity.  The associated thread is stable at the
	 * moment.  Note that we may be checking another cpu here so we
	 * have to be careful.  We are currently protected by the BGL.
	 *
	 * This allows us to avoid actually queueing the process.  
	 * acquire_curproc() will handle any threads we mistakenly schedule.
	 */
	cpuid = gd->gd_cpuid;

	if ((curprocmask & (1 << cpuid)) == 0) {
		curprocmask |= 1 << cpuid;
		gd->gd_uschedcp = lp;
		gd->gd_upri = lp->lwp_priority;
		lwkt_schedule(lp->lwp_thread);
		/* CANNOT TOUCH PROC OR TD AFTER SCHEDULE CALL TO REMOTE CPU */
		crit_exit();
#ifdef SMP
		if (gd != mycpu)
			++remote_resched_affinity;
#endif
		return;
	}

	/*
	 * gd and cpuid may still 'hint' at another cpu.  Even so we have
	 * to place this process on the userland scheduler's run queue for
	 * action by the target cpu.
	 */
	++runqcount;
	lp->lwp_proc->p_flag |= P_ONRUNQ;
	if (lp->lwp_rtprio.type == RTP_PRIO_NORMAL) {
		pri = (lp->lwp_priority & PRIMASK) / PPQ;
		q = &queues[pri];
		queuebits |= 1 << pri;
		needresched = (queuebits & ((1 << pri) - 1));
	} else if (lp->lwp_rtprio.type == RTP_PRIO_REALTIME ||
		   lp->lwp_rtprio.type == RTP_PRIO_FIFO) {
		pri = (u_int8_t)lp->lwp_rtprio.prio;
		q = &rtqueues[pri];
		rtqueuebits |= 1 << pri;
		needresched = (rtqueuebits & ((1 << pri) - 1));
	} else if (lp->lwp_rtprio.type == RTP_PRIO_IDLE) {
		pri = (u_int8_t)lp->lwp_rtprio.prio;
		q = &idqueues[pri];
		idqueuebits |= 1 << pri;
		needresched = (idqueuebits & ((1 << pri) - 1));
	} else {
		needresched = 0;
		panic("setrunqueue: invalid rtprio type");
	}
	KKASSERT(pri < 32);
	lp->lwp_rqindex = pri;		/* remember the queue index */
	TAILQ_INSERT_TAIL(q, lp, lwp_procq);

#ifdef SMP
	/*
	 * Either wakeup other cpus user thread scheduler or request 
	 * preemption on other cpus (which will also wakeup a HLT).
	 *
	 * NOTE!  gd and cpuid may still be our 'hint', not our current
	 * cpu info.
	 */

	count = runqcount;

	/*
	 * Check cpu affinity for user preemption (when the curprocmask bit
	 * is set).  Note that gd_upri is a speculative field (we modify
	 * another cpu's gd_upri to avoid sending ipiq storms).
	 */
	if (gd == mycpu) {
		if ((lp->lwp_thread->td_flags & TDF_NORESCHED) == 0) {
			if (lp->lwp_priority < gd->gd_upri - PPQ) {
				gd->gd_upri = lp->lwp_priority;
				gd->gd_rrcount = 0;
				need_user_resched();
				--count;
			} else if (gd->gd_uschedcp == lp && needresched) {
				gd->gd_rrcount = 0;
				need_user_resched();
				--count;
			}
		}
	} else if (remote_resched) {
		if (lp->lwp_priority < gd->gd_upri - PPQ) {
			gd->gd_upri = lp->lwp_priority;
			lwkt_send_ipiq(gd, need_user_resched_remote, NULL);
			--count;
			++remote_resched_affinity;
		}
	}

	/*
	 * No affinity, first schedule to any cpus that do not have a current
	 * process.  If there is a free cpu we always schedule to it.
	 */
	if (count &&
	    (mask = ~curprocmask & rdyprocmask & mycpu->gd_other_cpus) != 0 &&
	    (lp->lwp_proc->p_flag & P_PASSIVE_ACQ) == 0) {
		if (!mask)
			printf("lwp %d/%d nocpu to schedule it on\n",
			       lp->lwp_proc->p_pid, lp->lwp_tid);
		while (mask && count) {
			cpuid = bsfl(mask);
			KKASSERT((curprocmask & (1 << cpuid)) == 0);
			rdyprocmask &= ~(1 << cpuid);
			lwkt_schedule(&globaldata_find(cpuid)->gd_schedthread);
			--count;
			mask &= ~(1 << cpuid);
		}
	}

	/*
	 * If there are still runnable processes try to wakeup a random
	 * cpu that is running a much lower priority process in order to
	 * preempt on it.  Note that gd_upri is only a hint, so we can
	 * overwrite it from the wrong cpu.   If we can't find one, we
	 * are SOL.
	 *
	 * We depress the priority check so multiple cpu bound programs
	 * do not bounce between cpus.  Remember that the clock interrupt
	 * will also cause all cpus to reschedule.
	 *
	 * We must mask against rdyprocmask or we will race in the boot
	 * code (before all cpus have working scheduler helpers), plus
	 * some cpus might not be operational and/or not configured to
	 * handle user processes.
	 */
	if (count && remote_resched && ncpus > 1) {
		cpuid = scancpu;
		do {
			if (++cpuid == ncpus)
				cpuid = 0;
		} while (cpuid == mycpu->gd_cpuid);
		scancpu = cpuid;

		if (rdyprocmask & (1 << cpuid)) {
			gd = globaldata_find(cpuid);

			if (lp->lwp_priority < gd->gd_upri - PPQ) {
				gd->gd_upri = lp->lwp_priority;
				lwkt_send_ipiq(gd, need_user_resched_remote, NULL);
				++remote_resched_nonaffinity;
			}
		}
	}
#else
	if ((lp->lwp_thread->td_flags & TDF_NORESCHED) == 0) {
		if (lp->lwp_priority < gd->gd_upri - PPQ) {
			gd->gd_upri = lp->lwp_priority;
			gd->gd_rrcount = 0;
			need_user_resched();
		} else if (gd->gd_uschedcp == lp && needresched) {
			gd->gd_rrcount = 0;
			need_user_resched();
		}
	}
#endif
	crit_exit();
}

/*
 * remrunqueue() removes a given process from the run queue that it is on,
 * clearing the queue busy bit if it becomes empty.  This function is called
 * when a userland process is selected for LWKT scheduling.  Note that 
 * LWKT scheduling is an abstraction of 'curproc'.. there could very well be
 * several userland processes whos threads are scheduled or otherwise in
 * a special state, and such processes are NOT on the userland scheduler's
 * run queue.
 *
 * This must be called at splhigh().
 */
static void
bsd4_remrunqueue(struct lwp *lp)
{
	struct rq *q;
	u_int32_t *which;
	u_int8_t pri;

	crit_enter();
	KASSERT((lp->lwp_proc->p_flag & P_ONRUNQ) != 0, ("not on runq4!"));
	lp->lwp_proc->p_flag &= ~P_ONRUNQ;
	--runqcount;
	KKASSERT(runqcount >= 0);
	pri = lp->lwp_rqindex;
	if (lp->lwp_rtprio.type == RTP_PRIO_NORMAL) {
		q = &queues[pri];
		which = &queuebits;
	} else if (lp->lwp_rtprio.type == RTP_PRIO_REALTIME ||
		   lp->lwp_rtprio.type == RTP_PRIO_FIFO) {
		q = &rtqueues[pri];
		which = &rtqueuebits;
	} else if (lp->lwp_rtprio.type == RTP_PRIO_IDLE) {
		q = &idqueues[pri];
		which = &idqueuebits;
	} else {
		panic("remrunqueue: invalid rtprio type");
	}
	TAILQ_REMOVE(q, lp, lwp_procq);
	if (TAILQ_EMPTY(q)) {
		KASSERT((*which & (1 << pri)) != 0,
			("remrunqueue: remove from empty queue"));
		*which &= ~(1 << pri);
	}
	crit_exit();
}

/*
 * This routine is called from a systimer IPI.  It MUST be MP-safe and
 * the BGL IS NOT HELD ON ENTRY.  This routine is called at ESTCPUFREQ.
 */
static
void
bsd4_schedulerclock(struct lwp *lp, sysclock_t period, sysclock_t cpstamp)
{
	globaldata_t gd = mycpu;

	/*
	 * Do we need to round-robin?  We round-robin 10 times a second.
	 * This should only occur for cpu-bound batch processes.
	 */
	if (++gd->gd_rrcount >= usched_bsd4_rrinterval) {
		gd->gd_rrcount = 0;
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

	/* XXX optimize, avoid lock if no reset is required */
	if (try_mplock()) {
		bsd4_resetpriority(lp);
		rel_mplock();
	}
}

/*
 * Release the current process designation on p.  P MUST BE CURPROC.
 * Attempt to assign a new current process from the run queue.
 *
 * This function is called from exit1(), tsleep(), and the passive
 * release code setup in <arch>/<arch>/trap.c
 *
 * If we do not have or cannot get the MP lock we just wakeup the userland
 * helper scheduler thread for this cpu to do the work for us.
 *
 * WARNING!  The MP lock may be in an unsynchronized state due to the
 * way get_mplock() works and the fact that this function may be called
 * from a passive release during a lwkt_switch().   try_mplock() will deal 
 * with this for us but you should be aware that td_mpcount may not be
 * useable.
 */
static void
bsd4_release_curproc(struct lwp *lp)
{
	int cpuid;
	globaldata_t gd = mycpu;

	KKASSERT(lp->lwp_thread->td_gd == gd);
	crit_enter();
	cpuid = gd->gd_cpuid;

	if (gd->gd_uschedcp == lp) {
		if (try_mplock()) {
			/* 
			 * YYY when the MP lock is not assumed (see else) we
			 * will have to check that gd_uschedcp is still == lp
			 * after acquisition of the MP lock
			 */
			gd->gd_uschedcp = NULL;
			gd->gd_upri = PRIBASE_NULL;
			bsd4_select_curproc(gd);
			rel_mplock();
		} else {
			KKASSERT(0);	/* MP LOCK ALWAYS HELD AT THE MOMENT */
			gd->gd_uschedcp = NULL;
			gd->gd_upri = PRIBASE_NULL;
			/* YYY uschedcp and curprocmask */
			if (runqcount && (rdyprocmask & (1 << cpuid))) {
				rdyprocmask &= ~(1 << cpuid);
				lwkt_schedule(&mycpu->gd_schedthread);
			}
		}
	}
	crit_exit();
}

/*
 * Select a new current process, potentially retaining gd_uschedcp.  However,
 * be sure to round-robin.  This routine is generally only called if a
 * reschedule is requested and that typically only occurs if a new process
 * has a better priority or when we are round-robining.
 *
 * NOTE: Must be called with giant held and the current cpu's gd. 
 * NOTE: The caller must handle the situation where it loses a
 *	uschedcp designation that it previously held, typically by
 *	calling acquire_curproc() again. 
 * NOTE: May not block
 */
static
void
bsd4_select_curproc(globaldata_t gd)
{
	struct lwp *nlp;
	int cpuid = gd->gd_cpuid;
	void *old;

	clear_user_resched();

	/*
	 * Choose the next designated current user process.
	 * Note that we cannot schedule gd_schedthread
	 * if runqcount is 0 without creating a scheduling
	 * loop. 
	 *
	 * We do not clear the user resched request here,
	 * we need to test it later when we re-acquire.
	 *
	 * NOTE: chooseproc returns NULL if the chosen lwp
	 * is gd_uschedcp. XXX needs cleanup.
	 */
	old = gd->gd_uschedcp;
	if ((nlp = chooseproc(gd->gd_uschedcp)) != NULL) {
		curprocmask |= 1 << cpuid;
		gd->gd_upri = nlp->lwp_priority;
		gd->gd_uschedcp = nlp;
		lwkt_acquire(nlp->lwp_thread);
		lwkt_schedule(nlp->lwp_thread);
	} else if (gd->gd_uschedcp) {
		gd->gd_upri = gd->gd_uschedcp->lwp_priority;
		KKASSERT(curprocmask & (1 << cpuid));
	} else if (runqcount && (rdyprocmask & (1 << cpuid))) {
		/*gd->gd_uschedcp = NULL;*/
		curprocmask &= ~(1 << cpuid);
		rdyprocmask &= ~(1 << cpuid);
		lwkt_schedule(&gd->gd_schedthread);
	} else {
		/*gd->gd_uschedcp = NULL;*/
		curprocmask &= ~(1 << cpuid);
	}
}

/*
 * Acquire the current process designation on the CURRENT process only.
 * This function is called at kernel-user priority (not userland priority)
 * when curlwp does not match gd_uschedcp.
 *
 * This function is only called just prior to returning to user mode.
 *
 * Basically we recalculate our estcpu to hopefully give us a more
 * favorable disposition, setrunqueue, then wait for the curlwp
 * designation to be handed to us (if the setrunqueue didn't do it).
 *
 * WARNING! THIS FUNCTION MAY CAUSE THE CURRENT THREAD TO MIGRATE TO
 * ANOTHER CPU!  Because most of the kernel assumes that no migration will
 * occur, this function is called only under very controlled circumstances.
 */
static void
bsd4_acquire_curproc(struct lwp *lp)
{
	globaldata_t gd = mycpu;

	crit_enter();

	/*
	 * Recalculate our priority and put us back on the userland
	 * scheduler's runq.
	 *
	 * Only increment the involuntary context switch count if the
	 * setrunqueue call did not immediately schedule us.
	 */
	KKASSERT(lp == gd->gd_curthread->td_lwp);
	bsd4_recalculate_estcpu(lp);
	lwkt_deschedule_self(gd->gd_curthread);
	bsd4_setrunqueue(lp);
	if ((gd->gd_curthread->td_flags & TDF_RUNQ) == 0)
		++lp->lwp_stats->p_ru.ru_nivcsw;
	lwkt_switch();

	/*
	 * Because we put ourselves back on the userland scheduler's run
	 * queue, WE MAY HAVE BEEN MIGRATED TO ANOTHER CPU
	 */
	gd = mycpu;

	/*
	 * We better be the current process when we wake up, and we had
	 * better not be on the run queue.
	 */
	KKASSERT(gd->gd_uschedcp == lp);
	KKASSERT((lp->lwp_proc->p_flag & P_ONRUNQ) == 0);

	crit_exit();
}

/*
 * Compute the priority of a process when running in user mode.
 * Arrange to reschedule if the resulting priority is better
 * than that of the current process.
 */
static void
bsd4_resetpriority(struct lwp *lp)
{
	int newpriority;
	int opq;
	int npq;

	/*
	 * Set p_priority for general process comparisons
	 */
	switch(lp->lwp_rtprio.type) {
	case RTP_PRIO_REALTIME:
		lp->lwp_priority = PRIBASE_REALTIME + lp->lwp_rtprio.prio;
		return;
	case RTP_PRIO_NORMAL:
		break;
	case RTP_PRIO_IDLE:
		lp->lwp_priority = PRIBASE_IDLE + lp->lwp_rtprio.prio;
		return;
	case RTP_PRIO_THREAD:
		lp->lwp_priority = PRIBASE_THREAD + lp->lwp_rtprio.prio;
		return;
	}

	/*
	 * NORMAL priorities fall through.  These are based on niceness
	 * and cpu use.  Lower numbers == higher priorities.
	 *
	 * Calculate our priority based on our niceness and estimated cpu.
	 * Note that the nice value adjusts the baseline, which effects
	 * cpu bursts but does not effect overall cpu use between cpu-bound
	 * processes.  The use of the nice field in the decay calculation
	 * controls the overall cpu use.
	 *
	 * This isn't an exact calculation.  We fit the full nice and
	 * estcpu range into the priority range so the actual PPQ value
	 * is incorrect, but it's still a reasonable way to think about it.
	 */
	newpriority = (lp->lwp_proc->p_nice - PRIO_MIN) * PPQ / NICEPPQ;
	newpriority += lp->lwp_estcpu * PPQ / ESTCPUPPQ;
	newpriority = newpriority * MAXPRI /
		    (PRIO_RANGE * PPQ / NICEPPQ + ESTCPUMAX * PPQ / ESTCPUPPQ);
	newpriority = MIN(newpriority, MAXPRI - 1);	/* sanity */
	newpriority = MAX(newpriority, 0);		/* sanity */
	npq = newpriority / PPQ;
	crit_enter();
	opq = (lp->lwp_priority & PRIMASK) / PPQ;
	if (lp->lwp_proc->p_stat == SRUN && (lp->lwp_proc->p_flag & P_ONRUNQ) && opq != npq) {
		/*
		 * We have to move the process to another queue
		 */
		bsd4_remrunqueue(lp);
		lp->lwp_priority = PRIBASE_NORMAL + newpriority;
		bsd4_setrunqueue(lp);
	} else {
		/*
		 * We can just adjust the priority and it will be picked
		 * up later.
		 */
		KKASSERT(opq == npq || (lp->lwp_proc->p_flag & P_ONRUNQ) == 0);
		lp->lwp_priority = PRIBASE_NORMAL + newpriority;
	}
	crit_exit();
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
 * Called from acquire and from kern_synch's one-second timer with a 
 * critical section held.
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
		 * interval.
		 */
		nticks = (cpbase - lp->lwp_cpbase) / gd->gd_schedclock.periodic;
		updatepcpu(lp, lp->lwp_cpticks, nticks);

		if ((nleft = nticks - lp->lwp_cpticks) < 0)
			nleft = 0;
		if (usched_debug == lp->lwp_proc->p_pid) {
			printf("pid %d tid %d estcpu %d cpticks %d nticks %d nleft %d",
				lp->lwp_proc->p_pid, lp->lwp_tid, lp->lwp_estcpu,
				lp->lwp_cpticks, nticks, nleft);
		}

		/*
		 * Calculate a decay value based on ticks remaining scaled
		 * down by the instantanious load and p_nice.
		 */
		if ((loadfac = runqcount) < 2)
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
			printf(" ndecay %d estcpu %d\n", ndecay, lp->lwp_estcpu);

		bsd4_resetpriority(lp);
		lp->lwp_cpbase = cpbase;
		lp->lwp_cpticks = 0;
	}
}

#ifdef SMP

/*
 * For SMP systems a user scheduler helper thread is created for each
 * cpu and is used to allow one cpu to wakeup another for the purposes of
 * scheduling userland threads from setrunqueue().  UP systems do not
 * need the helper since there is only one cpu.  We can't use the idle
 * thread for this because we need to hold the MP lock.  Additionally,
 * doing things this way allows us to HLT idle cpus on MP systems.
 */
static void
sched_thread(void *dummy)
{
    globaldata_t gd = mycpu;
    int cpuid = gd->gd_cpuid;		/* doesn't change */
    u_int32_t cpumask = 1 << cpuid;	/* doesn't change */

    get_mplock();			/* hold the MP lock */
    for (;;) {
	struct lwp *nlp;

	lwkt_deschedule_self(gd->gd_curthread);	/* interlock */
	rdyprocmask |= cpumask;
	crit_enter_quick(gd->gd_curthread);
	if ((curprocmask & cpumask) == 0 && (nlp = chooseproc(NULL)) != NULL) {
	    curprocmask |= cpumask;
	    gd->gd_upri = nlp->lwp_priority;
	    gd->gd_uschedcp = nlp;
	    lwkt_acquire(nlp->lwp_thread);
	    lwkt_schedule(nlp->lwp_thread);
	}
	crit_exit_quick(gd->gd_curthread);
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
	printf("start scheduler helpers on cpus:");

    for (i = 0; i < ncpus; ++i) {
	globaldata_t dgd = globaldata_find(i);
	cpumask_t mask = 1 << i;

	if ((mask & smp_active_mask) == 0)
	    continue;

	if (bootverbose)
	    printf(" %d", i);

	lwkt_create(sched_thread, NULL, NULL, &dgd->gd_schedthread, 
		    TDF_STOPREQ, i, "usched %d", i);

	/*
	 * Allow user scheduling on the target cpu.  cpu #0 has already
	 * been enabled in rqinit().
	 */
	if (i)
	    curprocmask &= ~mask;
	rdyprocmask |= mask;
    }
    if (bootverbose)
	printf("\n");
}
SYSINIT(uschedtd, SI_SUB_FINISH_SMP, SI_ORDER_ANY, sched_thread_cpu_init, NULL)

#endif

