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
 * $FreeBSD: src/sys/kern/kern_switch.c,v 1.3.2.1 2000/05/16 06:58:12 dillon Exp $
 * $DragonFly: src/sys/kern/Attic/kern_switch.c,v 1.22 2004/07/24 20:21:35 dillon Exp $
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
 * debugging only YYY Remove me!   define to schedule user processes only
 * on the BSP.  Interrupts can still be taken on the APs.
 */
#undef ONLY_ONE_USER_CPU	

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
static int usched_debug;
static int usched_count;
SYSCTL_INT(_debug, OID_AUTO, scdebug, CTLFLAG_RW, &usched_debug, 0, "");
SYSCTL_INT(_debug, OID_AUTO, sccount, CTLFLAG_RW, &usched_count, 0, "");
#endif
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
struct proc *
chooseproc(struct proc *chkp)
{
	struct proc *p;
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
	p = TAILQ_FIRST(q);
	KASSERT(p, ("chooseproc: no proc on busy queue"));

	/*
	 * If the passed process <chkp> is reasonably close to the selected
	 * processed <p>, return NULL (indicating that <chkp> should be kept).
	 * 
	 * Note that we must error on the side of <chkp> to avoid bouncing
	 * between threads in the acquire code.
	 */
	if (chkp) {
		if (chkp->p_priority < p->p_priority + PPQ)
			return(NULL);
	}

#ifdef SMP
	/*
	 * If the chosen process does not reside on this cpu spend a few
	 * cycles looking for a better candidate at the same priority level.
	 * This is a fallback check, setrunqueue() tries to wakeup the
	 * correct cpu and is our front-line affinity.
	 */
	if (p->p_thread->td_gd != mycpu &&
	    (chkp = TAILQ_NEXT(p, p_procq)) != NULL
	) {
		if (chkp->p_thread->td_gd == mycpu) {
			++choose_affinity;
			p = chkp;
		}
	}
#endif

	TAILQ_REMOVE(q, p, p_procq);
	--runqcount;
	if (TAILQ_EMPTY(q))
		*which &= ~(1 << pri);
	KASSERT((p->p_flag & P_ONRUNQ) != 0, ("not on runq6!"));
	p->p_flag &= ~P_ONRUNQ;
	return p;
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
 * The associated thread must NOT be scheduled.  
 * The process must be runnable.
 * This must be called at splhigh().
 */
void
setrunqueue(struct proc *p)
{
	struct rq *q;
	struct globaldata *gd;
	int pri;
	int cpuid;
#ifdef SMP
	int count;
	cpumask_t mask;
#endif

	crit_enter();
	KASSERT(p->p_stat == SRUN, ("setrunqueue: proc not SRUN"));
	KASSERT((p->p_flag & P_ONRUNQ) == 0,
	    ("process %d already on runq! flag %08x", p->p_pid, p->p_flag));
	KKASSERT((p->p_thread->td_flags & TDF_RUNQ) == 0);

	/*
	 * Note: gd is the gd of the TARGET thread's cpu, not our cpu.
	 */
	gd = p->p_thread->td_gd;

	/*
	 * We have not been released, make sure that we are not the currently
	 * designated process.
	 */
	KKASSERT(gd->gd_uschedcp != p);

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
		gd->gd_uschedcp = p;
		if (usched_debug) 
			printf("F%-7d", gd->gd_uschedcp->p_pid);
		gd->gd_upri = p->p_priority;
		lwkt_schedule(p->p_thread);
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
	p->p_flag |= P_ONRUNQ;
	if (p->p_rtprio.type == RTP_PRIO_NORMAL) {
		pri = (p->p_priority & PRIMASK) >> 2;
		q = &queues[pri];
		queuebits |= 1 << pri;
	} else if (p->p_rtprio.type == RTP_PRIO_REALTIME ||
		   p->p_rtprio.type == RTP_PRIO_FIFO) {
		pri = (u_int8_t)p->p_rtprio.prio;
		q = &rtqueues[pri];
		rtqueuebits |= 1 << pri;
	} else if (p->p_rtprio.type == RTP_PRIO_IDLE) {
		pri = (u_int8_t)p->p_rtprio.prio;
		q = &idqueues[pri];
		idqueuebits |= 1 << pri;
	} else {
		panic("setrunqueue: invalid rtprio type");
	}
	KKASSERT(pri < 32);
	p->p_rqindex = pri;		/* remember the queue index */
	TAILQ_INSERT_TAIL(q, p, p_procq);

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
		if ((p->p_thread->td_flags & TDF_NORESCHED) == 0 &&
		    p->p_priority < gd->gd_upri - PPQ) {
			gd->gd_upri = p->p_priority;
			need_user_resched();
			--count;
		}
	} else if (remote_resched) {
		if (p->p_priority < gd->gd_upri - PPQ) {
			gd->gd_upri = p->p_priority;
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
	    (p->p_flag & P_PASSIVE_ACQ) == 0) {
		if (!mask)
			printf("PROC %d nocpu to schedule it on\n", p->p_pid);
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

			if (p->p_priority < gd->gd_upri - PPQ) {
				gd->gd_upri = p->p_priority;
				lwkt_send_ipiq(gd, need_user_resched_remote, NULL);
				++remote_resched_nonaffinity;
			}
		}
	}
#else
	if ((p->p_thread->td_flags & TDF_NORESCHED) == 0 &&
	    p->p_priority < gd->gd_upri - PPQ) {
		gd->gd_upri = p->p_priority;
		need_user_resched();
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
void
remrunqueue(struct proc *p)
{
	struct rq *q;
	u_int32_t *which;
	u_int8_t pri;

	crit_enter();
	KASSERT((p->p_flag & P_ONRUNQ) != 0, ("not on runq4!"));
	p->p_flag &= ~P_ONRUNQ;
	--runqcount;
	KKASSERT(runqcount >= 0);
	pri = p->p_rqindex;
	if (p->p_rtprio.type == RTP_PRIO_NORMAL) {
		q = &queues[pri];
		which = &queuebits;
	} else if (p->p_rtprio.type == RTP_PRIO_REALTIME ||
		   p->p_rtprio.type == RTP_PRIO_FIFO) {
		q = &rtqueues[pri];
		which = &rtqueuebits;
	} else if (p->p_rtprio.type == RTP_PRIO_IDLE) {
		q = &idqueues[pri];
		which = &idqueuebits;
	} else {
		panic("remrunqueue: invalid rtprio type");
	}
	TAILQ_REMOVE(q, p, p_procq);
	if (TAILQ_EMPTY(q)) {
		KASSERT((*which & (1 << pri)) != 0,
			("remrunqueue: remove from empty queue"));
		*which &= ~(1 << pri);
	}
	crit_exit();
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
void
release_curproc(struct proc *p)
{
	int cpuid;
	globaldata_t gd = mycpu;

#ifdef ONLY_ONE_USER_CPU
	KKASSERT(gd->gd_cpuid == 0 && p->p_thread->td_gd == gd);
#else
	KKASSERT(p->p_thread->td_gd == gd);
#endif
	crit_enter();
	if (usched_debug) {
	    printf("c%-7d", p->p_pid);
	    if (usched_count && --usched_count == 0)
		panic("x");
	}
	cpuid = gd->gd_cpuid;

	if (gd->gd_uschedcp == p) {
		if (try_mplock()) {
			/* 
			 * YYY when the MP lock is not assumed (see else) we
			 * will have to check that gd_uschedcp is still == p
			 * after acquisition of the MP lock
			 */
			gd->gd_uschedcp = NULL;
			gd->gd_upri = PRIBASE_NULL;
			select_curproc(gd);
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
void
select_curproc(globaldata_t gd)
{
	struct proc *np;
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
	 * NOTE: chooseproc returns NULL if the chosen proc
	 * is gd_uschedcp. XXX needs cleanup.
	 */
	old = gd->gd_uschedcp;
	if ((np = chooseproc(gd->gd_uschedcp)) != NULL) {
		curprocmask |= 1 << cpuid;
		gd->gd_upri = np->p_priority;
		gd->gd_uschedcp = np;
		if (usched_debug) {
		    printf("A%-7d[%p,%p]", gd->gd_uschedcp->p_pid, old, np);
		}
		lwkt_acquire(np->p_thread);
		lwkt_schedule(np->p_thread);
	} else if (gd->gd_uschedcp) {
		gd->gd_upri = gd->gd_uschedcp->p_priority;
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
 * when curproc does not match gd_uschedcp.
 */
void
acquire_curproc(struct proc *p)
{
	globaldata_t gd = mycpu;

#ifdef ONLY_ONE_USER_CPU
	KKASSERT(gd->gd_cpuid == 0);
#endif

	/*
	 * Loop until we become the current process.  
	 */
	crit_enter();
	++p->p_stats->p_ru.ru_nivcsw;
	do {
		KKASSERT(p == gd->gd_curthread->td_proc);

		lwkt_deschedule_self(gd->gd_curthread);
		setrunqueue(p);
		lwkt_switch();
		if (usched_debug)
		    printf("a");

		/*
		 * WE MAY HAVE BEEN MIGRATED TO ANOTHER CPU, RELOAD GD.
		 */
		gd = mycpu;
	} while (gd->gd_uschedcp != p);
	crit_exit();

	/*
	 * That's it.  Cleanup, we are done.  The caller can return to
	 * user mode now.
	 */
	KKASSERT((p->p_flag & P_ONRUNQ) == 0);
}

/*
 * Yield / synchronous reschedule.  This is a bit tricky because the trap
 * code might have set a lazy release on the switch function.   Setting
 * P_PASSIVE_ACQ will ensure that the lazy release executes when we call
 * switch, and that we are given a greater chance of affinity with our
 * current cpu.
 *
 * We call lwkt_setpri_self() to rotate our thread to the end of the lwkt
 * run queue.  lwkt_switch() will also execute any assigned passive release
 * (which usually calls release_curproc()), allowing a same/higher priority
 * process to be designated as the current process.  
 *
 * While it is possible for a lower priority process to be designated,
 * it's call to lwkt_maybe_switch() in acquire_curproc() will likely
 * round-robin back to us and we will be able to re-acquire the current
 * process designation.
 */
void
uio_yield(void)
{
	struct thread *td = curthread;
	struct proc *p = td->td_proc;

	lwkt_setpri_self(td->td_pri & TDPRI_MASK);
	if (p) {
		p->p_flag |= P_PASSIVE_ACQ;
		lwkt_switch();
		p->p_flag &= ~P_PASSIVE_ACQ;
	} else {
		lwkt_switch();
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

#ifdef ONLY_ONE_USER_CPU
    KKASSERT(cpuid == 0);
#endif

    get_mplock();			/* hold the MP lock */
    for (;;) {
	struct proc *np;

	lwkt_deschedule_self(gd->gd_curthread);	/* interlock */
	rdyprocmask |= cpumask;
	crit_enter_quick(gd->gd_curthread);
	if ((curprocmask & cpumask) == 0 && (np = chooseproc(NULL)) != NULL) {
	    curprocmask |= cpumask;
	    gd->gd_upri = np->p_priority;
	    gd->gd_uschedcp = np;
	    if (usched_debug)
		printf("E%-7d", gd->gd_uschedcp->p_pid);
	    lwkt_acquire(np->p_thread);
	    lwkt_schedule(np->p_thread);
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
#ifdef ONLY_ONE_USER_CPU
	if (i)
	    curprocmask |= mask;	/* DISABLE USER PROCS */
#else
	if (i)
	    curprocmask &= ~mask;	/* schedule user proc on cpu */
#endif
	rdyprocmask |= mask;
    }
    if (bootverbose)
	printf("\n");
}
SYSINIT(uschedtd, SI_SUB_FINISH_SMP, SI_ORDER_ANY, sched_thread_cpu_init, NULL)

#endif

