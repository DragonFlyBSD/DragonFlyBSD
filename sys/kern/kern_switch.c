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
 * $DragonFly: src/sys/kern/Attic/kern_switch.c,v 1.5 2003/07/10 04:47:54 dillon Exp $
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/queue.h>
#include <sys/proc.h>
#include <sys/rtprio.h>
#include <sys/thread2.h>
#include <sys/uio.h>
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
static u_int32_t curprocmask = -1;
static u_int32_t rdyprocmask = 0;
static int	 runqcount;

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

static int
test_resched(struct proc *curp, struct proc *newp)
{
	if (newp->p_rtprio.type < curp->p_rtprio.type)
		return(1);
	if (newp->p_rtprio.type == curp->p_rtprio.type) {
		if (newp->p_rtprio.type == RTP_PRIO_NORMAL) {
			if (newp->p_priority / PPQ <= curp->p_priority / PPQ)
				return(1);
		} else if (newp->p_rtprio.prio < curp->p_rtprio.prio) {
			return(1);
		}
	}
	return(0);
}

/*
 * chooseproc() is called when a cpu needs a user process to LWKT schedule.
 * chooseproc() will select a user process and return it.
 */
static
struct proc *
chooseproc(void)
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
	TAILQ_REMOVE(q, p, p_procq);
	if (TAILQ_EMPTY(q))
		*which &= ~(1 << pri);
	KASSERT((p->p_flag & P_ONRUNQ) != 0, ("not on runq6!"));
	p->p_flag &= ~P_ONRUNQ;
	return p;
}


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
 * The associated thread must NOT be scheduled.  
 * The process must be runnable.
 * This must be called at splhigh().
 */
void
setrunqueue(struct proc *p)
{
	struct rq *q;
	int pri;
	int cpuid;
	u_int32_t mask;

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
	 * Otherwise place this process on the userland scheduler's run
	 * queue for action.
	 */
	++runqcount;
	p->p_flag |= P_ONRUNQ;
	if (p->p_rtprio.type == RTP_PRIO_NORMAL) {
		pri = p->p_priority >> 2;
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

	/*
	 * Wakeup other cpus to schedule the newly available thread.
	 * XXX doesn't really have to be in a critical section.
	 * We own giant after all.
	 */
	if ((mask = ~curprocmask & rdyprocmask & mycpu->gd_other_cpus) != 0) {
		int count = runqcount;
		while (mask && count) {
			cpuid = bsfl(mask);
			KKASSERT((curprocmask & (1 << cpuid)) == 0);
			rdyprocmask &= ~(1 << cpuid);
			lwkt_schedule(&globaldata_find(cpuid)->gd_schedthread);
			--count;
			mask &= ~(1 << cpuid);
		}
	}
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
 * Release the P_CURPROC designation on the CURRENT process only.  This
 * will allow another userland process to be scheduled and places our
 * process back on the userland scheduling queue.
 */
void
release_curproc(struct proc *p)
{
	int cpuid;
	struct proc *np;

#ifdef ONLY_ONE_USER_CPU
	KKASSERT(mycpu->gd_cpuid == 0 && p->p_thread->td_cpu == 0);
#endif
	crit_enter();
	clear_resched();
	cpuid = p->p_thread->td_cpu;
	p->p_flag |= P_CP_RELEASED;
	if (p->p_flag & P_CURPROC) {
		p->p_flag &= ~P_CURPROC;
		KKASSERT(curprocmask & (1 << cpuid));
		if ((np = chooseproc()) != NULL) {
			np->p_flag |= P_CURPROC;
			lwkt_acquire(np->p_thread);
			lwkt_schedule(np->p_thread);
		} else {
			curprocmask &= ~(1 << cpuid);
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
	 * lost it in the first place.
	 */
	if ((p->p_flag & P_CURPROC) != 0)
		return;

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
	KKASSERT(mycpu->gd_cpuid == 0 && p->p_thread->td_cpu == 0);
#endif
	crit_enter();
	p->p_flag &= ~P_CP_RELEASED;
	while ((p->p_flag & P_CURPROC) == 0) {
		cpuid = p->p_thread->td_cpu;	/* load/reload cpuid */
		if ((curprocmask & (1 << cpuid)) == 0) {
			curprocmask |= 1 << cpuid;
			if ((np = chooseproc()) != NULL) {
				KKASSERT((np->p_flag & P_CP_RELEASED) == 0);
				if (test_resched(p, np)) {
					np->p_flag |= P_CURPROC;
					lwkt_acquire(np->p_thread);
					lwkt_schedule(np->p_thread);
				} else {
					p->p_flag |= P_CURPROC;
					setrunqueue(np);
				}
			} else {
				p->p_flag |= P_CURPROC;
			}
		}
		if ((p->p_flag & P_CURPROC) == 0) {
			lwkt_deschedule_self();
			setrunqueue(p);
			lwkt_switch();
			KKASSERT((p->p_flag & (P_ONRUNQ|P_CURPROC|P_CP_RELEASED)) == P_CURPROC);
		}
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
		acquire_curproc(p);
		release_curproc(p);
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

	rdyprocmask |= cpumask;
	lwkt_deschedule_self();		/* interlock */
	crit_enter();
	if ((curprocmask & cpumask) == 0 && (np = chooseproc()) != NULL) {
	    curprocmask |= cpumask;
	    np->p_flag |= P_CURPROC;
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

