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
 * $DragonFly: src/sys/kern/Attic/kern_switch.c,v 1.11 2003/10/17 07:30:42 dillon Exp $
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
#include <machine/ipl.h>
#include <machine/cpu.h>

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
static u_int32_t curprocmask = -1;	/* currently running a user process */
static u_int32_t rdyprocmask;		/* ready to accept a user process */
static int	 runqcount;
#ifdef SMP
static int	 scancpu;
#endif

SYSCTL_INT(_debug, OID_AUTO, runqcount, CTLFLAG_RD, &runqcount, 0, "");
static int usched_steal;
SYSCTL_INT(_debug, OID_AUTO, usched_steal, CTLFLAG_RW,
        &usched_steal, 0, "Passive Release was nonoptimal");
static int usched_optimal;
SYSCTL_INT(_debug, OID_AUTO, usched_optimal, CTLFLAG_RW,
        &usched_optimal, 0, "Passive Release was nonoptimal");
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

#define USCHED_COUNTER(td)	((td->td_gd == mycpu) ? ++usched_optimal : ++usched_steal)

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
#ifdef SMP
	sched_thread_init();
#else
	curprocmask &= ~1;
#endif
}
SYSINIT(runqueue, SI_SUB_RUN_QUEUE, SI_ORDER_FIRST, rqinit, NULL)

static __inline
int
test_resched(struct proc *curp, struct proc *newp)
{
	if (newp->p_priority / PPQ <= curp->p_priority / PPQ)
		return(1);
	return(0);
}

/*
 * chooseproc() is called when a cpu needs a user process to LWKT schedule.
 * chooseproc() will select a user process and return it.
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
	 * If the chosen process is not at a higher priority then chkp
	 * then return NULL without dequeueing a new process.
	 */
	if (chkp && !test_resched(chkp, p))
		return(NULL);

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
need_resched_remote(void *dummy)
{
	need_resched();
}

#endif

/*
 * setrunqueue() 'wakes up' a 'user' process, which can mean several things.
 *
 * If P_CP_RELEASED is set the user process is under the control of the
 * LWKT subsystem and we simply wake the thread up.  This is ALWAYS the
 * case when setrunqueue() is called from wakeup() and, in fact wakeup()
 * asserts that P_CP_RELEASED is set.
 *
 * Note that acquire_curproc() already optimizes making the current process
 * P_CURPROC, so setrunqueue() does not need to.
 *
 * If P_CP_RELEASED is not set we place the process on the run queue and we
 * signal other cpus in the system that may need to be woken up to service
 * the new 'user' process.
 *
 * If P_PASSIVE_ACQ is set setrunqueue() will not wakeup potential target
 * cpus in an attempt to keep the process on the current cpu at least for
 * a little while to take advantage of locality of reference (e.g. fork/exec
 * or short fork/exit).
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
	u_int32_t mask;
#endif

	crit_enter();
	KASSERT(p->p_stat == SRUN, ("setrunqueue: proc not SRUN"));
	KASSERT((p->p_flag & (P_ONRUNQ|P_CURPROC)) == 0,
	    ("process %d already on runq! flag %08x", p->p_pid, p->p_flag));
	KKASSERT((p->p_thread->td_flags & TDF_RUNQ) == 0);

	/*
	 * If we have been released from the userland scheduler we
	 * directly schedule its thread.
	 */
	if (p->p_flag & P_CP_RELEASED) {
		lwkt_schedule(p->p_thread);
		crit_exit();
		return;
	}

	/*
	 * Check cpu affinity.  The associated thread is stable at the
	 * moment.  Note that we may be checking another cpu here so we
	 * have to be careful.  Note that gd_upri only counts when the
	 * curprocmask bit is set for the cpu in question, and since it is
	 * only a hint we can modify it on another cpu's globaldata structure.
	 * We use it to prevent unnecessary IPIs (hence the - PPQ).
	 */
	gd = p->p_thread->td_gd;
	cpuid = gd->gd_cpuid;

	if ((curprocmask & (1 << cpuid)) == 0) {
		curprocmask |= 1 << cpuid;
		p->p_flag |= P_CURPROC;
		gd->gd_upri = p->p_priority;
		USCHED_COUNTER(p->p_thread);
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
	 * is set)
	 */
	if (gd == mycpu) {
		if (p->p_priority / PPQ < gd->gd_upri / PPQ) {
			need_resched();
			--count;
		}
	} else if (remote_resched) {
		if (p->p_priority / PPQ < gd->gd_upri / PPQ) {
			gd->gd_upri = p->p_priority;
			lwkt_send_ipiq(cpuid, need_resched_remote, NULL);
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
	 */
	if (count && remote_resched && ncpus > 1) {
		cpuid = scancpu;
		do {
			if (++cpuid == ncpus)
				cpuid = 0;
		} while (cpuid == mycpu->gd_cpuid);
		scancpu = cpuid;

		gd = globaldata_find(cpuid);

		if (p->p_priority / PPQ < gd->gd_upri / PPQ - 2) {
			gd->gd_upri = p->p_priority;
			lwkt_send_ipiq(cpuid, need_resched_remote, NULL);
			++remote_resched_nonaffinity;
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
 * Release the P_CURPROC designation on the current process for this cpu
 * and attempt to assign a new current process from the run queue.
 *
 * If we do not have or cannot get the MP lock we just wakeup the userland
 * helper scheduler thread for this cpu.
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
	struct proc *np;

#ifdef ONLY_ONE_USER_CPU
	KKASSERT(mycpu->gd_cpuid == 0 && p->p_thread->td_gd == mycpu);
#endif
	crit_enter();
	clear_resched();
	cpuid = p->p_thread->td_gd->gd_cpuid;
	if ((p->p_flag & P_CP_RELEASED) == 0) {
		p->p_flag |= P_CP_RELEASED;
		lwkt_setpri_self(TDPRI_KERN_USER);
	}
	if (p->p_flag & P_CURPROC) {
		p->p_flag &= ~P_CURPROC;
		curprocmask &= ~(1 << cpuid);
		if (try_mplock()) {
			/*
			 * Choose the next process to assign P_CURPROC to.
			 * Note that we cannot schedule gd_schedthread
			 * if runqcount is 0 without creating a scheduling
			 * loop.
			 */
			if ((np = chooseproc(NULL)) != NULL) {
				curprocmask |= 1 << cpuid;
				np->p_flag |= P_CURPROC;
				mycpu->gd_upri = np->p_priority;
				USCHED_COUNTER(np->p_thread);
				lwkt_acquire(np->p_thread);
				lwkt_schedule(np->p_thread);
			} else if (runqcount && (rdyprocmask & (1 << cpuid))) {
				rdyprocmask &= ~(1 << cpuid);
				lwkt_schedule(&mycpu->gd_schedthread);
			}
			rel_mplock();
		} else {
			KKASSERT(0);	/* MP LOCK ALWAYS HELD AT THE MOMENT */
			if (runqcount && (rdyprocmask & (1 << cpuid))) {
				rdyprocmask &= ~(1 << cpuid);
				lwkt_schedule(&mycpu->gd_schedthread);
			}
		}
	}
	crit_exit();
}

/*
 * Acquire the P_CURPROC designation on the CURRENT process only.  This
 * function is called prior to returning to userland.  If the system
 * call or trap did not block and if no reschedule was requested it is
 * highly likely that the P_CURPROC flag is still set in the proc, and
 * we do almost nothing here.
 */
void
acquire_curproc(struct proc *p)
{
	int cpuid;
	struct proc *np;

	/*
	 * Short cut, we've already acquired the designation or we never
	 * lost it in the first place.  P_CP_RELEASED is cleared, meaning
	 * that the process is again under the control of the userland 
	 * scheduler.  We do not have to fiddle with the LWKT priority,
	 * the trap code (userret/userexit) will do that for us.
	 */
	if ((p->p_flag & P_CURPROC) != 0) {
		p->p_flag &= ~P_CP_RELEASED;
		return;
	}

	/*
	 * Long cut.  This pulls in a bit of the userland scheduler as 
	 * an optimization.  If our cpu has not scheduled a userland
	 * process we gladly fill the slot, otherwise we choose the best
	 * candidate from the run queue and compare it against ourselves,
	 * scheduling either us or him depending.
	 *
	 * If our cpu's slot isn't free we put ourselves on the userland
	 * run queue and switch away.  We should have P_CURPROC when we
	 * come back.  Note that a cpu change can occur when we come back.
	 *
	 * YYY don't need critical section, we hold giant and no interrupt
	 * will mess w/ this proc?  Or will it?  What about curprocmask?
	 */
#ifdef ONLY_ONE_USER_CPU
	KKASSERT(mycpu->gd_cpuid == 0 && p->p_thread->td_gd == mycpu);
#endif
	crit_enter();

	while ((p->p_flag & P_CURPROC) == 0) {
		/*
		 * reload the cpuid
		 */
		cpuid = p->p_thread->td_gd->gd_cpuid;

		/*
		 * (broken out from setrunqueue() as an optimization that
		 * allows us to avoid descheduling and rescheduling ourself)
		 *
		 * Interlock against the helper scheduler thread by setting
		 * curprocmask while we choose a new process.  Check our
		 * process against the new process to shortcut setrunqueue()
		 * and remrunqueue() operations.
		 */
		if ((curprocmask & (1 << cpuid)) == 0) {
			curprocmask |= 1 << cpuid;

			if ((np = chooseproc(p)) != NULL) {
				KKASSERT((np->p_flag & P_CP_RELEASED) == 0);
				np->p_flag |= P_CURPROC;
				mycpu->gd_upri = np->p_priority;
				USCHED_COUNTER(np->p_thread);
				lwkt_acquire(np->p_thread);
				lwkt_schedule(np->p_thread);
			} else {
				p->p_flag |= P_CURPROC;
			}
			break;
		}
		lwkt_deschedule_self();
		p->p_flag &= ~P_CP_RELEASED;
		setrunqueue(p);
		lwkt_switch();	/* CPU CAN CHANGE DUE TO SETRUNQUEUE() */
		KASSERT((p->p_flag & (P_ONRUNQ|P_CURPROC|P_CP_RELEASED)) == P_CURPROC, ("unexpected p_flag %08x acquiring P_CURPROC\n", p->p_flag));
	}
	crit_exit();
}

/*
 * Yield / synchronous reschedule.  This is a bit tricky because the trap
 * code might have set a lazy release on the switch function.  The first
 * thing we do is call lwkt_switch() to resolve the lazy release (if any).
 * Then, if we are a process, we want to allow another process to run.
 *
 * The only way to do that is to acquire and then release P_CURPROC.  We
 * have to release it because the kernel expects it to be released as a
 * sanity check when it goes to sleep.
 *
 * XXX we need a way to ensure that we wake up eventually from a yield,
 * even if we are an idprio process.
 */
void
uio_yield(void)
{
	struct thread *td = curthread;
	struct proc *p = td->td_proc;

	lwkt_switch();
	if (p) {
		p->p_flag |= P_PASSIVE_ACQ;
		acquire_curproc(p);
		release_curproc(p);
		p->p_flag &= ~P_PASSIVE_ACQ;
	}
}


/*
 * For SMP systems a user scheduler helper thread is created for each
 * cpu and is used to allow one cpu to wakeup another for the purposes of
 * scheduling userland threads from setrunqueue().  UP systems do not
 * need the helper since there is only one cpu.  We can't use the idle
 * thread for this because we need to hold the MP lock.  Additionally,
 * doing things this way allows us to HLT idle cpus on MP systems.
 */

#ifdef SMP

static void
sched_thread(void *dummy)
{
    int cpuid = mycpu->gd_cpuid;	/* doesn't change */
    u_int32_t cpumask = 1 << cpuid;	/* doesn't change */

#ifdef ONLY_ONE_USER_CPU
    KKASSERT(cpuid == 0);
#endif

    get_mplock();			/* hold the MP lock */
    for (;;) {
	struct proc *np;

	lwkt_deschedule_self();		/* interlock */
	rdyprocmask |= cpumask;
	crit_enter();
	if ((curprocmask & cpumask) == 0 && (np = chooseproc(NULL)) != NULL) {
	    curprocmask |= cpumask;
	    np->p_flag |= P_CURPROC;
	    mycpu->gd_upri = np->p_priority;
	    USCHED_COUNTER(np->p_thread);
	    lwkt_acquire(np->p_thread);
	    lwkt_schedule(np->p_thread);
	}
	crit_exit();
	lwkt_switch();
    }
}

void
sched_thread_init(void)
{
    int cpuid = mycpu->gd_cpuid;

    lwkt_create(sched_thread, NULL, NULL, &mycpu->gd_schedthread, 
	TDF_STOPREQ, "usched %d", cpuid);
    curprocmask &= ~(1 << cpuid);	/* schedule user proc on cpu */
#ifdef ONLY_ONE_USER_CPU
    if (cpuid)
	curprocmask |= 1 << cpuid;	/* DISABLE USER PROCS */
#endif
    rdyprocmask |= 1 << cpuid;
}

#endif

