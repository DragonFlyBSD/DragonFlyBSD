/*
 * Copyright (c) 2003 Matthew Dillon <dillon@backplane.com>
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
 *	Each cpu in a system has its own self-contained light weight kernel
 *	thread scheduler, which means that generally speaking we only need
 *	to use a critical section to avoid problems.  Foreign thread 
 *	scheduling is queued via (async) IPIs.
 *
 * $DragonFly: src/sys/kern/lwkt_thread.c,v 1.25 2003/07/20 01:37:22 dillon Exp $
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/proc.h>
#include <sys/rtprio.h>
#include <sys/queue.h>
#include <sys/thread2.h>
#include <sys/sysctl.h>
#include <sys/kthread.h>
#include <machine/cpu.h>
#include <sys/lock.h>

#include <vm/vm.h>
#include <vm/vm_param.h>
#include <vm/vm_kern.h>
#include <vm/vm_object.h>
#include <vm/vm_page.h>
#include <vm/vm_map.h>
#include <vm/vm_pager.h>
#include <vm/vm_extern.h>
#include <vm/vm_zone.h>

#include <machine/stdarg.h>
#include <machine/ipl.h>
#ifdef SMP
#include <machine/smp.h>
#endif

static int untimely_switch = 0;
SYSCTL_INT(_lwkt, OID_AUTO, untimely_switch, CTLFLAG_RW, &untimely_switch, 0, "");
#ifdef INVARIANTS
static int token_debug = 0;
SYSCTL_INT(_lwkt, OID_AUTO, token_debug, CTLFLAG_RW, &token_debug, 0, "");
#endif
static quad_t switch_count = 0;
SYSCTL_QUAD(_lwkt, OID_AUTO, switch_count, CTLFLAG_RW, &switch_count, 0, "");
static quad_t preempt_hit = 0;
SYSCTL_QUAD(_lwkt, OID_AUTO, preempt_hit, CTLFLAG_RW, &preempt_hit, 0, "");
static quad_t preempt_miss = 0;
SYSCTL_QUAD(_lwkt, OID_AUTO, preempt_miss, CTLFLAG_RW, &preempt_miss, 0, "");
static quad_t preempt_weird = 0;
SYSCTL_QUAD(_lwkt, OID_AUTO, preempt_weird, CTLFLAG_RW, &preempt_weird, 0, "");
static quad_t ipiq_count = 0;
SYSCTL_QUAD(_lwkt, OID_AUTO, ipiq_count, CTLFLAG_RW, &ipiq_count, 0, "");
static quad_t ipiq_fifofull = 0;
SYSCTL_QUAD(_lwkt, OID_AUTO, ipiq_fifofull, CTLFLAG_RW, &ipiq_fifofull, 0, "");

/*
 * These helper procedures handle the runq, they can only be called from
 * within a critical section.
 */
static __inline
void
_lwkt_dequeue(thread_t td)
{
    if (td->td_flags & TDF_RUNQ) {
	int nq = td->td_pri & TDPRI_MASK;
	struct globaldata *gd = mycpu;

	td->td_flags &= ~TDF_RUNQ;
	TAILQ_REMOVE(&gd->gd_tdrunq[nq], td, td_threadq);
	/* runqmask is passively cleaned up by the switcher */
    }
}

static __inline
void
_lwkt_enqueue(thread_t td)
{
    if ((td->td_flags & TDF_RUNQ) == 0) {
	int nq = td->td_pri & TDPRI_MASK;
	struct globaldata *gd = mycpu;

	td->td_flags |= TDF_RUNQ;
	TAILQ_INSERT_TAIL(&gd->gd_tdrunq[nq], td, td_threadq);
	gd->gd_runqmask |= 1 << nq;
    }
}

static __inline
int
_lwkt_wantresched(thread_t ntd, thread_t cur)
{
    return((ntd->td_pri & TDPRI_MASK) > (cur->td_pri & TDPRI_MASK));
}

/*
 * LWKTs operate on a per-cpu basis
 *
 * WARNING!  Called from early boot, 'mycpu' may not work yet.
 */
void
lwkt_gdinit(struct globaldata *gd)
{
    int i;

    for (i = 0; i < sizeof(gd->gd_tdrunq)/sizeof(gd->gd_tdrunq[0]); ++i)
	TAILQ_INIT(&gd->gd_tdrunq[i]);
    gd->gd_runqmask = 0;
    TAILQ_INIT(&gd->gd_tdallq);
}

/*
 * Initialize a thread wait structure prior to first use.
 *
 * NOTE!  called from low level boot code, we cannot do anything fancy!
 */
void
lwkt_init_wait(lwkt_wait_t w)
{
    TAILQ_INIT(&w->wa_waitq);
}

/*
 * Create a new thread.  The thread must be associated with a process context
 * or LWKT start address before it can be scheduled.
 *
 * If you intend to create a thread without a process context this function
 * does everything except load the startup and switcher function.
 */
thread_t
lwkt_alloc_thread(struct thread *td)
{
    void *stack;
    int flags = 0;

    if (td == NULL) {
	crit_enter();
	if (mycpu->gd_tdfreecount > 0) {
	    --mycpu->gd_tdfreecount;
	    td = TAILQ_FIRST(&mycpu->gd_tdfreeq);
	    KASSERT(td != NULL && (td->td_flags & TDF_RUNNING) == 0,
		("lwkt_alloc_thread: unexpected NULL or corrupted td"));
	    TAILQ_REMOVE(&mycpu->gd_tdfreeq, td, td_threadq);
	    crit_exit();
	    stack = td->td_kstack;
	    flags = td->td_flags & (TDF_ALLOCATED_STACK|TDF_ALLOCATED_THREAD);
	} else {
	    crit_exit();
	    td = zalloc(thread_zone);
	    td->td_kstack = NULL;
	    flags |= TDF_ALLOCATED_THREAD;
	}
    }
    if ((stack = td->td_kstack) == NULL) {
	stack = (void *)kmem_alloc(kernel_map, UPAGES * PAGE_SIZE);
	flags |= TDF_ALLOCATED_STACK;
    }
    lwkt_init_thread(td, stack, flags, mycpu);
    return(td);
}

/*
 * Initialize a preexisting thread structure.  This function is used by
 * lwkt_alloc_thread() and also used to initialize the per-cpu idlethread.
 *
 * NOTE!  called from low level boot code, we cannot do anything fancy!
 */
void
lwkt_init_thread(thread_t td, void *stack, int flags, struct globaldata *gd)
{
    bzero(td, sizeof(struct thread));
    td->td_kstack = stack;
    td->td_flags |= flags;
    td->td_gd = gd;
    td->td_pri = TDPRI_CRIT;
    td->td_cpu = gd->gd_cpuid;	/* YYY don't really need this if have td_gd */
    lwkt_init_port(&td->td_msgport, td);
    pmap_init_thread(td);
    crit_enter();
    TAILQ_INSERT_TAIL(&mycpu->gd_tdallq, td, td_allq);
    crit_exit();
}

void
lwkt_set_comm(thread_t td, const char *ctl, ...)
{
    va_list va;

    va_start(va, ctl);
    vsnprintf(td->td_comm, sizeof(td->td_comm), ctl, va);
    va_end(va);
}

void
lwkt_hold(thread_t td)
{
    ++td->td_refs;
}

void
lwkt_rele(thread_t td)
{
    KKASSERT(td->td_refs > 0);
    --td->td_refs;
}

void
lwkt_wait_free(thread_t td)
{
    while (td->td_refs)
	tsleep(td, 0, "tdreap", hz);
}

void
lwkt_free_thread(thread_t td)
{
    struct globaldata *gd = mycpu;

    KASSERT((td->td_flags & TDF_RUNNING) == 0,
	("lwkt_free_thread: did not exit! %p", td));

    crit_enter();
    TAILQ_REMOVE(&gd->gd_tdallq, td, td_allq);
    if (gd->gd_tdfreecount < CACHE_NTHREADS &&
	(td->td_flags & TDF_ALLOCATED_THREAD)
    ) {
	++gd->gd_tdfreecount;
	TAILQ_INSERT_HEAD(&gd->gd_tdfreeq, td, td_threadq);
	crit_exit();
    } else {
	crit_exit();
	if (td->td_kstack && (td->td_flags & TDF_ALLOCATED_STACK)) {
	    kmem_free(kernel_map,
		    (vm_offset_t)td->td_kstack, UPAGES * PAGE_SIZE);
	    /* gd invalid */
	    td->td_kstack = NULL;
	}
	if (td->td_flags & TDF_ALLOCATED_THREAD)
	    zfree(thread_zone, td);
    }
}


/*
 * Switch to the next runnable lwkt.  If no LWKTs are runnable then 
 * switch to the idlethread.  Switching must occur within a critical
 * section to avoid races with the scheduling queue.
 *
 * We always have full control over our cpu's run queue.  Other cpus
 * that wish to manipulate our queue must use the cpu_*msg() calls to
 * talk to our cpu, so a critical section is all that is needed and
 * the result is very, very fast thread switching.
 *
 * The LWKT scheduler uses a fixed priority model and round-robins at
 * each priority level.  User process scheduling is a totally
 * different beast and LWKT priorities should not be confused with
 * user process priorities.
 *
 * The MP lock may be out of sync with the thread's td_mpcount.  lwkt_switch()
 * cleans it up.  Note that the td_switch() function cannot do anything that
 * requires the MP lock since the MP lock will have already been setup for
 * the target thread (not the current thread).
 */

void
lwkt_switch(void)
{
    struct globaldata *gd;
    thread_t td = curthread;
    thread_t ntd;
#ifdef SMP
    int mpheld;
#endif

    if (mycpu->gd_intr_nesting_level && 
	td->td_preempted == NULL && panicstr == NULL
    ) {
	panic("lwkt_switch: cannot switch from within an interrupt, yet\n");
    }

    /*
     * Passive release (used to transition from user to kernel mode
     * when we block or switch rather then when we enter the kernel).
     * This function is NOT called if we are switching into a preemption
     * or returning from a preemption.  Typically this causes us to lose
     * our P_CURPROC designation (if we have one) and become a true LWKT
     * thread, and may also hand P_CURPROC to another process and schedule
     * its thread.
     */
    if (td->td_release)
	    td->td_release(td);

    crit_enter();
    ++switch_count;

#ifdef SMP
    /*
     * td_mpcount cannot be used to determine if we currently hold the
     * MP lock because get_mplock() will increment it prior to attempting
     * to get the lock, and switch out if it can't.  Look at the actual lock.
     */
    mpheld = MP_LOCK_HELD();
#endif
    if ((ntd = td->td_preempted) != NULL) {
	/*
	 * We had preempted another thread on this cpu, resume the preempted
	 * thread.  This occurs transparently, whether the preempted thread
	 * was scheduled or not (it may have been preempted after descheduling
	 * itself). 
	 *
	 * We have to setup the MP lock for the original thread after backing
	 * out the adjustment that was made to curthread when the original
	 * was preempted.
	 */
	KKASSERT(ntd->td_flags & TDF_PREEMPT_LOCK);
#ifdef SMP
	if (ntd->td_mpcount && mpheld == 0) {
	    panic("MPLOCK NOT HELD ON RETURN: %p %p %d %d\n",
	       td, ntd, td->td_mpcount, ntd->td_mpcount);
	}
	if (ntd->td_mpcount) {
	    td->td_mpcount -= ntd->td_mpcount;
	    KKASSERT(td->td_mpcount >= 0);
	}
#endif
	ntd->td_flags |= TDF_PREEMPT_DONE;
	/* YYY release mp lock on switchback if original doesn't need it */
    } else {
	/*
	 * Priority queue / round-robin at each priority.  Note that user
	 * processes run at a fixed, low priority and the user process
	 * scheduler deals with interactions between user processes
	 * by scheduling and descheduling them from the LWKT queue as
	 * necessary.
	 *
	 * We have to adjust the MP lock for the target thread.  If we 
	 * need the MP lock and cannot obtain it we try to locate a
	 * thread that does not need the MP lock.
	 */
	gd = mycpu;
again:
	if (gd->gd_runqmask) {
	    int nq = bsrl(gd->gd_runqmask);
	    if ((ntd = TAILQ_FIRST(&gd->gd_tdrunq[nq])) == NULL) {
		gd->gd_runqmask &= ~(1 << nq);
		goto again;
	    }
#ifdef SMP
	    if (ntd->td_mpcount && mpheld == 0 && !cpu_try_mplock()) {
		/*
		 * Target needs MP lock and we couldn't get it, try
		 * to locate a thread which does not need the MP lock
		 * to run.  If we cannot locate a thread spin in idle.
		 */
		u_int32_t rqmask = gd->gd_runqmask;
		while (rqmask) {
		    TAILQ_FOREACH(ntd, &gd->gd_tdrunq[nq], td_threadq) {
			if (ntd->td_mpcount == 0)
			    break;
		    }
		    if (ntd)
			break;
		    rqmask &= ~(1 << nq);
		    nq = bsrl(rqmask);
		}
		if (ntd == NULL) {
		    ntd = &gd->gd_idlethread;
		    ntd->td_flags |= TDF_IDLE_NOHLT;
		} else {
		    TAILQ_REMOVE(&gd->gd_tdrunq[nq], ntd, td_threadq);
		    TAILQ_INSERT_TAIL(&gd->gd_tdrunq[nq], ntd, td_threadq);
		}
	    } else {
		TAILQ_REMOVE(&gd->gd_tdrunq[nq], ntd, td_threadq);
		TAILQ_INSERT_TAIL(&gd->gd_tdrunq[nq], ntd, td_threadq);
	    }
#else
	    TAILQ_REMOVE(&gd->gd_tdrunq[nq], ntd, td_threadq);
	    TAILQ_INSERT_TAIL(&gd->gd_tdrunq[nq], ntd, td_threadq);
#endif
	} else {
	    /*
	     * Nothing to run but we may still need the BGL to deal with
	     * pending interrupts, spin in idle if so.
	     */
	    ntd = &gd->gd_idlethread;
	    if (gd->gd_reqflags)
		ntd->td_flags |= TDF_IDLE_NOHLT;
	}
    }
    KASSERT(ntd->td_pri >= TDPRI_CRIT,
	("priority problem in lwkt_switch %d %d", td->td_pri, ntd->td_pri));

    /*
     * Do the actual switch.  If the new target does not need the MP lock
     * and we are holding it, release the MP lock.  If the new target requires
     * the MP lock we have already acquired it for the target.
     */
#ifdef SMP
    if (ntd->td_mpcount == 0 ) {
	if (MP_LOCK_HELD())
	    cpu_rel_mplock();
    } else {
	ASSERT_MP_LOCK_HELD();
    }
#endif
    if (td != ntd) {
	td->td_switch(ntd);
    }

    crit_exit();
}

/*
 * Switch if another thread has a higher priority.  Do not switch to other
 * threads at the same priority.
 */
void
lwkt_maybe_switch()
{
    struct globaldata *gd = mycpu;
    struct thread *td = gd->gd_curthread;

    if ((td->td_pri & TDPRI_MASK) < bsrl(gd->gd_runqmask)) {
	lwkt_switch();
    }
}

/*
 * Request that the target thread preempt the current thread.  Preemption
 * only works under a specific set of conditions:
 *
 *	- We are not preempting ourselves
 *	- The target thread is owned by the current cpu
 *	- We are not currently being preempted
 *	- The target is not currently being preempted
 *	- We are able to satisfy the target's MP lock requirements (if any).
 *
 * THE CALLER OF LWKT_PREEMPT() MUST BE IN A CRITICAL SECTION.  Typically
 * this is called via lwkt_schedule() through the td_preemptable callback.
 * critpri is the managed critical priority that we should ignore in order
 * to determine whether preemption is possible (aka usually just the crit
 * priority of lwkt_schedule() itself).
 *
 * XXX at the moment we run the target thread in a critical section during
 * the preemption in order to prevent the target from taking interrupts
 * that *WE* can't.  Preemption is strictly limited to interrupt threads
 * and interrupt-like threads, outside of a critical section, and the
 * preempted source thread will be resumed the instant the target blocks
 * whether or not the source is scheduled (i.e. preemption is supposed to
 * be as transparent as possible).
 *
 * The target thread inherits our MP count (added to its own) for the
 * duration of the preemption in order to preserve the atomicy of the
 * MP lock during the preemption.  Therefore, any preempting targets must be
 * careful in regards to MP assertions.  Note that the MP count may be
 * out of sync with the physical mp_lock.  If we preempt we have to preserve
 * the expected situation.
 */
void
lwkt_preempt(thread_t ntd, int critpri)
{
    thread_t td = curthread;
#ifdef SMP
    int mpheld;
    int savecnt;
#endif

    /*
     * The caller has put us in a critical section.  We can only preempt
     * if the caller of the caller was not in a critical section (basically
     * a local interrupt), as determined by the 'critpri' parameter.   If
     * we are unable to preempt 
     *
     * YYY The target thread must be in a critical section (else it must
     * inherit our critical section?  I dunno yet).
     */
    KASSERT(ntd->td_pri >= TDPRI_CRIT, ("BADCRIT0 %d", ntd->td_pri));

    need_resched();
    if (!_lwkt_wantresched(ntd, td)) {
	++preempt_miss;
	return;
    }
    if ((td->td_pri & ~TDPRI_MASK) > critpri) {
	++preempt_miss;
	return;
    }
#ifdef SMP
    if (ntd->td_cpu != mycpu->gd_cpuid) {
	++preempt_miss;
	return;
    }
#endif
    if (td == ntd || ((td->td_flags | ntd->td_flags) & TDF_PREEMPT_LOCK)) {
	++preempt_weird;
	return;
    }
    if (ntd->td_preempted) {
	++preempt_hit;
	return;
    }
#ifdef SMP
    /*
     * note: an interrupt might have occured just as we were transitioning
     * to the MP lock.  In this case td_mpcount will be pre-disposed but
     * not actually synchronized with the actual state of the lock.  We
     * can use it to imply an MP lock requirement for the preemption but
     * we cannot use it to test whether we hold the MP lock or not.
     */
    mpheld = MP_LOCK_HELD();
    if (mpheld && td->td_mpcount == 0)
	panic("lwkt_preempt(): held and no count");
    savecnt = td->td_mpcount;
    ntd->td_mpcount += td->td_mpcount;
    if (mpheld == 0 && ntd->td_mpcount && !cpu_try_mplock()) {
	ntd->td_mpcount -= td->td_mpcount;
	++preempt_miss;
	return;
    }
#endif

    ++preempt_hit;
    ntd->td_preempted = td;
    td->td_flags |= TDF_PREEMPT_LOCK;
    td->td_switch(ntd);
    KKASSERT(ntd->td_preempted && (td->td_flags & TDF_PREEMPT_DONE));
#ifdef SMP
    KKASSERT(savecnt == td->td_mpcount);
    if (mpheld == 0 && MP_LOCK_HELD())
	cpu_rel_mplock();
    else if (mpheld && !MP_LOCK_HELD())
	panic("lwkt_preempt(): MP lock was not held through");
#endif
    ntd->td_preempted = NULL;
    td->td_flags &= ~(TDF_PREEMPT_LOCK|TDF_PREEMPT_DONE);
}

/*
 * Yield our thread while higher priority threads are pending.  This is
 * typically called when we leave a critical section but it can be safely
 * called while we are in a critical section.
 *
 * This function will not generally yield to equal priority threads but it
 * can occur as a side effect.  Note that lwkt_switch() is called from
 * inside the critical section to pervent its own crit_exit() from reentering
 * lwkt_yield_quick().
 *
 * gd_reqflags indicates that *something* changed, e.g. an interrupt or softint
 * came along but was blocked and made pending.
 *
 * (self contained on a per cpu basis)
 */
void
lwkt_yield_quick(void)
{
    thread_t td = curthread;

    /*
     * gd_reqflags is cleared in splz if the cpl is 0.  If we were to clear
     * it with a non-zero cpl then we might not wind up calling splz after
     * a task switch when the critical section is exited even though the
     * new task could accept the interrupt.  YYY alternative is to have
     * lwkt_switch() just call splz unconditionally.
     *
     * XXX from crit_exit() only called after last crit section is released.
     * If called directly will run splz() even if in a critical section.
     */
    if (mycpu->gd_reqflags)
	splz();

    /*
     * YYY enabling will cause wakeup() to task-switch, which really
     * confused the old 4.x code.  This is a good way to simulate
     * preemption and MP without actually doing preemption or MP, because a
     * lot of code assumes that wakeup() does not block.
     */
    if (untimely_switch && mycpu->gd_intr_nesting_level == 0) {
	crit_enter();
	/*
	 * YYY temporary hacks until we disassociate the userland scheduler
	 * from the LWKT scheduler.
	 */
	if (td->td_flags & TDF_RUNQ) {
	    lwkt_switch();		/* will not reenter yield function */
	} else {
	    lwkt_schedule_self();	/* make sure we are scheduled */
	    lwkt_switch();		/* will not reenter yield function */
	    lwkt_deschedule_self();	/* make sure we are descheduled */
	}
	crit_exit_noyield();
    }
}

/*
 * This implements a normal yield which, unlike _quick, will yield to equal
 * priority threads as well.  Note that gd_reqflags tests will be handled by
 * the crit_exit() call in lwkt_switch().
 *
 * (self contained on a per cpu basis)
 */
void
lwkt_yield(void)
{
    lwkt_schedule_self();
    lwkt_switch();
}

/*
 * Schedule a thread to run.  As the current thread we can always safely
 * schedule ourselves, and a shortcut procedure is provided for that
 * function.
 *
 * (non-blocking, self contained on a per cpu basis)
 */
void
lwkt_schedule_self(void)
{
    thread_t td = curthread;

    crit_enter();
    KASSERT(td->td_wait == NULL, ("lwkt_schedule_self(): td_wait not NULL!"));
    _lwkt_enqueue(td);
    if (td->td_proc && td->td_proc->p_stat == SSLEEP)
	panic("SCHED SELF PANIC");
    crit_exit();
}

/*
 * Generic schedule.  Possibly schedule threads belonging to other cpus and
 * deal with threads that might be blocked on a wait queue.
 *
 * YYY this is one of the best places to implement load balancing code.
 * Load balancing can be accomplished by requesting other sorts of actions
 * for the thread in question.
 */
void
lwkt_schedule(thread_t td)
{
#ifdef	INVARIANTS
    if ((td->td_flags & TDF_PREEMPT_LOCK) == 0 && td->td_proc 
	&& td->td_proc->p_stat == SSLEEP
    ) {
	printf("PANIC schedule curtd = %p (%d %d) target %p (%d %d)\n",
	    curthread,
	    curthread->td_proc ? curthread->td_proc->p_pid : -1,
	    curthread->td_proc ? curthread->td_proc->p_stat : -1,
	    td,
	    td->td_proc ? curthread->td_proc->p_pid : -1,
	    td->td_proc ? curthread->td_proc->p_stat : -1
	);
	panic("SCHED PANIC");
    }
#endif
    crit_enter();
    if (td == curthread) {
	_lwkt_enqueue(td);
    } else {
	lwkt_wait_t w;

	/*
	 * If the thread is on a wait list we have to send our scheduling
	 * request to the owner of the wait structure.  Otherwise we send
	 * the scheduling request to the cpu owning the thread.  Races
	 * are ok, the target will forward the message as necessary (the
	 * message may chase the thread around before it finally gets
	 * acted upon).
	 *
	 * (remember, wait structures use stable storage)
	 */
	if ((w = td->td_wait) != NULL) {
	    if (lwkt_trytoken(&w->wa_token)) {
		TAILQ_REMOVE(&w->wa_waitq, td, td_threadq);
		--w->wa_count;
		td->td_wait = NULL;
		if (td->td_cpu == mycpu->gd_cpuid) {
		    _lwkt_enqueue(td);
		    if (td->td_preemptable) {
			td->td_preemptable(td, TDPRI_CRIT*2); /* YYY +token */
		    } else if (_lwkt_wantresched(td, curthread)) {
			need_resched();
		    }
		} else {
		    lwkt_send_ipiq(td->td_cpu, (ipifunc_t)lwkt_schedule, td);
		}
		lwkt_reltoken(&w->wa_token);
	    } else {
		lwkt_send_ipiq(w->wa_token.t_cpu, (ipifunc_t)lwkt_schedule, td);
	    }
	} else {
	    /*
	     * If the wait structure is NULL and we own the thread, there
	     * is no race (since we are in a critical section).  If we
	     * do not own the thread there might be a race but the
	     * target cpu will deal with it.
	     */
	    if (td->td_cpu == mycpu->gd_cpuid) {
		_lwkt_enqueue(td);
		if (td->td_preemptable) {
		    td->td_preemptable(td, TDPRI_CRIT);
		} else if (_lwkt_wantresched(td, curthread)) {
		    need_resched();
		}
	    } else {
		lwkt_send_ipiq(td->td_cpu, (ipifunc_t)lwkt_schedule, td);
	    }
	}
    }
    crit_exit();
}

/*
 * Managed acquisition.  This code assumes that the MP lock is held for
 * the tdallq operation and that the thread has been descheduled from its
 * original cpu.  We also have to wait for the thread to be entirely switched
 * out on its original cpu (this is usually fast enough that we never loop)
 * since the LWKT system does not have to hold the MP lock while switching
 * and the target may have released it before switching.
 */
void
lwkt_acquire(thread_t td)
{
    struct globaldata *gd;
    int ocpu;

    gd = td->td_gd;
    KKASSERT((td->td_flags & TDF_RUNQ) == 0);
    while (td->td_flags & TDF_RUNNING)	/* XXX spin */
	;
    if (gd != mycpu) {
	ocpu = td->td_cpu;
	crit_enter();
	TAILQ_REMOVE(&gd->gd_tdallq, td, td_allq);	/* protected by BGL */
	gd = mycpu;
	td->td_gd = gd;
	td->td_cpu = gd->gd_cpuid;
	TAILQ_INSERT_TAIL(&gd->gd_tdallq, td, td_allq); /* protected by BGL */
	crit_exit();
    }
}

/*
 * Deschedule a thread.
 *
 * (non-blocking, self contained on a per cpu basis)
 */
void
lwkt_deschedule_self(void)
{
    thread_t td = curthread;

    crit_enter();
    KASSERT(td->td_wait == NULL, ("lwkt_schedule_self(): td_wait not NULL!"));
    _lwkt_dequeue(td);
    crit_exit();
}

/*
 * Generic deschedule.  Descheduling threads other then your own should be
 * done only in carefully controlled circumstances.  Descheduling is 
 * asynchronous.  
 *
 * This function may block if the cpu has run out of messages.
 */
void
lwkt_deschedule(thread_t td)
{
    crit_enter();
    if (td == curthread) {
	_lwkt_dequeue(td);
    } else {
	if (td->td_cpu == mycpu->gd_cpuid) {
	    _lwkt_dequeue(td);
	} else {
	    lwkt_send_ipiq(td->td_cpu, (ipifunc_t)lwkt_deschedule, td);
	}
    }
    crit_exit();
}

/*
 * Set the target thread's priority.  This routine does not automatically
 * switch to a higher priority thread, LWKT threads are not designed for
 * continuous priority changes.  Yield if you want to switch.
 *
 * We have to retain the critical section count which uses the high bits
 * of the td_pri field.  The specified priority may also indicate zero or
 * more critical sections by adding TDPRI_CRIT*N.
 */
void
lwkt_setpri(thread_t td, int pri)
{
    KKASSERT(pri >= 0);
    KKASSERT(td->td_cpu == mycpu->gd_cpuid);
    crit_enter();
    if (td->td_flags & TDF_RUNQ) {
	_lwkt_dequeue(td);
	td->td_pri = (td->td_pri & ~TDPRI_MASK) + pri;
	_lwkt_enqueue(td);
    } else {
	td->td_pri = (td->td_pri & ~TDPRI_MASK) + pri;
    }
    crit_exit();
}

void
lwkt_setpri_self(int pri)
{
    thread_t td = curthread;

    KKASSERT(pri >= 0 && pri <= TDPRI_MAX);
    crit_enter();
    if (td->td_flags & TDF_RUNQ) {
	_lwkt_dequeue(td);
	td->td_pri = (td->td_pri & ~TDPRI_MASK) + pri;
	_lwkt_enqueue(td);
    } else {
	td->td_pri = (td->td_pri & ~TDPRI_MASK) + pri;
    }
    crit_exit();
}

struct proc *
lwkt_preempted_proc(void)
{
    thread_t td = curthread;
    while (td->td_preempted)
	td = td->td_preempted;
    return(td->td_proc);
}

typedef struct lwkt_gettoken_req {
    lwkt_token_t tok;
    int	cpu;
} lwkt_gettoken_req;

#if 0

/*
 * This function deschedules the current thread and blocks on the specified
 * wait queue.  We obtain ownership of the wait queue in order to block
 * on it.  A generation number is used to interlock the wait queue in case
 * it gets signalled while we are blocked waiting on the token.
 *
 * Note: alternatively we could dequeue our thread and then message the
 * target cpu owning the wait queue.  YYY implement as sysctl.
 *
 * Note: wait queue signals normally ping-pong the cpu as an optimization.
 */

void
lwkt_block(lwkt_wait_t w, const char *wmesg, int *gen)
{
    thread_t td = curthread;

    lwkt_gettoken(&w->wa_token);
    if (w->wa_gen == *gen) {
	_lwkt_dequeue(td);
	TAILQ_INSERT_TAIL(&w->wa_waitq, td, td_threadq);
	++w->wa_count;
	td->td_wait = w;
	td->td_wmesg = wmesg;
again:
	lwkt_switch();
	lwkt_regettoken(&w->wa_token);
	if (td->td_wmesg != NULL) {
	    _lwkt_dequeue(td);
	    goto again;
	}
    }
    /* token might be lost, doesn't matter for gen update */
    *gen = w->wa_gen;
    lwkt_reltoken(&w->wa_token);
}

/*
 * Signal a wait queue.  We gain ownership of the wait queue in order to
 * signal it.  Once a thread is removed from the wait queue we have to
 * deal with the cpu owning the thread.
 *
 * Note: alternatively we could message the target cpu owning the wait
 * queue.  YYY implement as sysctl.
 */
void
lwkt_signal(lwkt_wait_t w, int count)
{
    thread_t td;
    int count;

    lwkt_gettoken(&w->wa_token);
    ++w->wa_gen;
    if (count < 0)
	count = w->wa_count;
    while ((td = TAILQ_FIRST(&w->wa_waitq)) != NULL && count) {
	--count;
	--w->wa_count;
	TAILQ_REMOVE(&w->wa_waitq, td, td_threadq);
	td->td_wait = NULL;
	td->td_wmesg = NULL;
	if (td->td_cpu == mycpu->gd_cpuid) {
	    _lwkt_enqueue(td);
	} else {
	    lwkt_send_ipiq(td->td_cpu, (ipifunc_t)lwkt_schedule, td);
	}
	lwkt_regettoken(&w->wa_token);
    }
    lwkt_reltoken(&w->wa_token);
}

#endif

/*
 * Acquire ownership of a token
 *
 * Acquire ownership of a token.  The token may have spl and/or critical
 * section side effects, depending on its purpose.  These side effects
 * guarentee that you will maintain ownership of the token as long as you
 * do not block.  If you block you may lose access to the token (but you
 * must still release it even if you lose your access to it).
 *
 * YYY for now we use a critical section to prevent IPIs from taking away
 * a token, but do we really only need to disable IPIs ?
 *
 * YYY certain tokens could be made to act like mutexes when performance
 * would be better (e.g. t_cpu == -1).  This is not yet implemented.
 *
 * YYY the tokens replace 4.x's simplelocks for the most part, but this
 * means that 4.x does not expect a switch so for now we cannot switch
 * when waiting for an IPI to be returned.  
 *
 * YYY If the token is owned by another cpu we may have to send an IPI to
 * it and then block.   The IPI causes the token to be given away to the
 * requesting cpu, unless it has already changed hands.  Since only the
 * current cpu can give away a token it owns we do not need a memory barrier.
 * This needs serious optimization.
 */

#ifdef SMP

static
void
lwkt_gettoken_remote(void *arg)
{
    lwkt_gettoken_req *req = arg;
    if (req->tok->t_cpu == mycpu->gd_cpuid) {
	if (token_debug)
	    printf("GT(%d,%d) ", req->tok->t_cpu, req->cpu);
	req->tok->t_cpu = req->cpu;
	req->tok->t_reqcpu = req->cpu;	/* YYY leave owned by target cpu */
	/* else set reqcpu to point to current cpu for release */
    }
}

#endif

int
lwkt_gettoken(lwkt_token_t tok)
{
    /*
     * Prevent preemption so the token can't be taken away from us once
     * we gain ownership of it.  Use a synchronous request which might
     * block.  The request will be forwarded as necessary playing catchup
     * to the token.
     */

    crit_enter();
#ifdef INVARIANTS
    if (curthread->td_pri > 2000) {
	curthread->td_pri = 1000;
	panic("too HIGH!");
    }
#endif
#ifdef SMP
    while (tok->t_cpu != mycpu->gd_cpuid) {
	struct lwkt_gettoken_req req;
	int seq;
	int dcpu;

	req.cpu = mycpu->gd_cpuid;
	req.tok = tok;
	dcpu = (volatile int)tok->t_cpu;
	KKASSERT(dcpu >= 0 && dcpu < ncpus);
	if (token_debug)
	    printf("REQT%d ", dcpu);
	seq = lwkt_send_ipiq(dcpu, lwkt_gettoken_remote, &req);
	lwkt_wait_ipiq(dcpu, seq);
	if (token_debug)
	    printf("REQR%d ", tok->t_cpu);
    }
#endif
    /*
     * leave us in a critical section on return.  This will be undone
     * by lwkt_reltoken().  Bump the generation number.
     */
    return(++tok->t_gen);
}

/*
 * Attempt to acquire ownership of a token.  Returns 1 on success, 0 on
 * failure.
 */
int
lwkt_trytoken(lwkt_token_t tok)
{
    crit_enter();
#ifdef SMP
    if (tok->t_cpu != mycpu->gd_cpuid) {
	return(0);
    } 
#endif
    /* leave us in the critical section */
    ++tok->t_gen;
    return(1);
}

/*
 * Release your ownership of a token.  Releases must occur in reverse
 * order to aquisitions, eventually so priorities can be unwound properly
 * like SPLs.  At the moment the actual implemention doesn't care.
 *
 * We can safely hand a token that we own to another cpu without notifying
 * it, but once we do we can't get it back without requesting it (unless
 * the other cpu hands it back to us before we check).
 *
 * We might have lost the token, so check that.
 */
void
lwkt_reltoken(lwkt_token_t tok)
{
    if (tok->t_cpu == mycpu->gd_cpuid) {
	tok->t_cpu = tok->t_reqcpu;
    }
    crit_exit();
}

/*
 * Reacquire a token that might have been lost and compare and update the
 * generation number.  0 is returned if the generation has not changed
 * (nobody else obtained the token while we were blocked, on this cpu or
 * any other cpu).
 *
 * This function returns with the token re-held whether the generation
 * number changed or not.
 */
int
lwkt_gentoken(lwkt_token_t tok, int *gen)
{
    if (lwkt_regettoken(tok) == *gen) {
	return(0);
    } else {
	*gen = tok->t_gen;
	return(-1);
    }
}


/*
 * Re-acquire a token that might have been lost.  Returns the generation 
 * number of the token.
 */
int
lwkt_regettoken(lwkt_token_t tok)
{
    /* assert we are in a critical section */
    if (tok->t_cpu != mycpu->gd_cpuid) {
#ifdef SMP
	while (tok->t_cpu != mycpu->gd_cpuid) {
	    struct lwkt_gettoken_req req;
	    int seq;
	    int dcpu;

	    req.cpu = mycpu->gd_cpuid;
	    req.tok = tok;
	    dcpu = (volatile int)tok->t_cpu;
	    KKASSERT(dcpu >= 0 && dcpu < ncpus);
	    if (token_debug)
		printf("REQT%d ", dcpu);
	    seq = lwkt_send_ipiq(dcpu, lwkt_gettoken_remote, &req);
	    lwkt_wait_ipiq(dcpu, seq);
	    if (token_debug)
		printf("REQR%d ", tok->t_cpu);
	}
#endif
	++tok->t_gen;
    }
    return(tok->t_gen);
}

void
lwkt_inittoken(lwkt_token_t tok)
{
    /*
     * Zero structure and set cpu owner and reqcpu to cpu 0.
     */
    bzero(tok, sizeof(*tok));
}

/*
 * Create a kernel process/thread/whatever.  It shares it's address space
 * with proc0 - ie: kernel only.
 *
 * XXX should be renamed to lwkt_create()
 *
 * The thread will be entered with the MP lock held.
 */
int
lwkt_create(void (*func)(void *), void *arg,
    struct thread **tdp, thread_t template, int tdflags,
    const char *fmt, ...)
{
    thread_t td;
    va_list ap;

    td = lwkt_alloc_thread(template);
    if (tdp)
	*tdp = td;
    cpu_set_thread_handler(td, kthread_exit, func, arg);
    td->td_flags |= TDF_VERBOSE | tdflags;
#ifdef SMP
    td->td_mpcount = 1;
#endif

    /*
     * Set up arg0 for 'ps' etc
     */
    va_start(ap, fmt);
    vsnprintf(td->td_comm, sizeof(td->td_comm), fmt, ap);
    va_end(ap);

    /*
     * Schedule the thread to run
     */
    if ((td->td_flags & TDF_STOPREQ) == 0)
	lwkt_schedule(td);
    else
	td->td_flags &= ~TDF_STOPREQ;
    return 0;
}

/*
 * Destroy an LWKT thread.   Warning!  This function is not called when
 * a process exits, cpu_proc_exit() directly calls cpu_thread_exit() and
 * uses a different reaping mechanism.
 */
void
lwkt_exit(void)
{
    thread_t td = curthread;

    if (td->td_flags & TDF_VERBOSE)
	printf("kthread %p %s has exited\n", td, td->td_comm);
    crit_enter();
    lwkt_deschedule_self();
    ++mycpu->gd_tdfreecount;
    TAILQ_INSERT_TAIL(&mycpu->gd_tdfreeq, td, td_threadq);
    cpu_thread_exit();
}

/*
 * Create a kernel process/thread/whatever.  It shares it's address space
 * with proc0 - ie: kernel only.  5.x compatible.
 */
int
kthread_create(void (*func)(void *), void *arg,
    struct thread **tdp, const char *fmt, ...)
{
    thread_t td;
    va_list ap;

    td = lwkt_alloc_thread(NULL);
    if (tdp)
	*tdp = td;
    cpu_set_thread_handler(td, kthread_exit, func, arg);
    td->td_flags |= TDF_VERBOSE;
#ifdef SMP
    td->td_mpcount = 1;
#endif

    /*
     * Set up arg0 for 'ps' etc
     */
    va_start(ap, fmt);
    vsnprintf(td->td_comm, sizeof(td->td_comm), fmt, ap);
    va_end(ap);

    /*
     * Schedule the thread to run
     */
    lwkt_schedule(td);
    return 0;
}

void
crit_panic(void)
{
    thread_t td = curthread;
    int lpri = td->td_pri;

    td->td_pri = 0;
    panic("td_pri is/would-go negative! %p %d", td, lpri);
}

/*
 * Destroy an LWKT thread.   Warning!  This function is not called when
 * a process exits, cpu_proc_exit() directly calls cpu_thread_exit() and
 * uses a different reaping mechanism.
 *
 * XXX duplicates lwkt_exit()
 */
void
kthread_exit(void)
{
    lwkt_exit();
}

#ifdef SMP

/*
 * Send a function execution request to another cpu.  The request is queued
 * on the cpu<->cpu ipiq matrix.  Each cpu owns a unique ipiq FIFO for every
 * possible target cpu.  The FIFO can be written.
 *
 * YYY If the FIFO fills up we have to enable interrupts and process the
 * IPIQ while waiting for it to empty or we may deadlock with another cpu.
 * Create a CPU_*() function to do this!
 *
 * Must be called from a critical section.
 */
int
lwkt_send_ipiq(int dcpu, ipifunc_t func, void *arg)
{
    lwkt_ipiq_t ip;
    int windex;
    struct globaldata *gd = mycpu;

    if (dcpu == gd->gd_cpuid) {
	func(arg);
	return(0);
    } 
    crit_enter();
    ++gd->gd_intr_nesting_level;
#ifdef INVARIANTS
    if (gd->gd_intr_nesting_level > 20)
	panic("lwkt_send_ipiq: TOO HEAVILY NESTED!");
#endif
    KKASSERT(curthread->td_pri >= TDPRI_CRIT);
    KKASSERT(dcpu >= 0 && dcpu < ncpus);
    ++ipiq_count;
    ip = &gd->gd_ipiq[dcpu];

    /*
     * We always drain before the FIFO becomes full so it should never
     * become full.  We need to leave enough entries to deal with 
     * reentrancy.
     */
    KKASSERT(ip->ip_windex - ip->ip_rindex != MAXCPUFIFO);
    windex = ip->ip_windex & MAXCPUFIFO_MASK;
    ip->ip_func[windex] = func;
    ip->ip_arg[windex] = arg;
    /* YYY memory barrier */
    ++ip->ip_windex;
    if (ip->ip_windex - ip->ip_rindex > MAXCPUFIFO / 2) {
	unsigned int eflags = read_eflags();
	cpu_enable_intr();
	++ipiq_fifofull;
	while (ip->ip_windex - ip->ip_rindex > MAXCPUFIFO / 4) {
	    KKASSERT(ip->ip_windex - ip->ip_rindex != MAXCPUFIFO - 1);
	    lwkt_process_ipiq();
	}
	write_eflags(eflags);
    }
    --gd->gd_intr_nesting_level;
    cpu_send_ipiq(dcpu);	/* issues memory barrier if appropriate */
    crit_exit();
    return(ip->ip_windex);
}

/*
 * Send a message to several target cpus.  Typically used for scheduling.
 */
void
lwkt_send_ipiq_mask(u_int32_t mask, ipifunc_t func, void *arg)
{
    int cpuid;

    while (mask) {
	    cpuid = bsfl(mask);
	    lwkt_send_ipiq(cpuid, func, arg);
	    mask &= ~(1 << cpuid);
    }
}

/*
 * Wait for the remote cpu to finish processing a function.
 *
 * YYY we have to enable interrupts and process the IPIQ while waiting
 * for it to empty or we may deadlock with another cpu.  Create a CPU_*()
 * function to do this!  YYY we really should 'block' here.
 *
 * Must be called from a critical section.  Thsi routine may be called
 * from an interrupt (for example, if an interrupt wakes a foreign thread
 * up).
 */
void
lwkt_wait_ipiq(int dcpu, int seq)
{
    lwkt_ipiq_t ip;
    int maxc = 100000000;

    if (dcpu != mycpu->gd_cpuid) {
	KKASSERT(dcpu >= 0 && dcpu < ncpus);
	ip = &mycpu->gd_ipiq[dcpu];
	if ((int)(ip->ip_xindex - seq) < 0) {
	    unsigned int eflags = read_eflags();
	    cpu_enable_intr();
	    while ((int)(ip->ip_xindex - seq) < 0) {
		lwkt_process_ipiq();
		if (--maxc == 0)
			printf("LWKT_WAIT_IPIQ WARNING! %d wait %d (%d)\n", mycpu->gd_cpuid, dcpu, ip->ip_xindex - seq);
		if (maxc < -1000000)
			panic("LWKT_WAIT_IPIQ");
	    }
	    write_eflags(eflags);
	}
    }
}

/*
 * Called from IPI interrupt (like a fast interrupt), which has placed
 * us in a critical section.  The MP lock may or may not be held.
 * May also be called from doreti or splz, or be reentrantly called
 * indirectly through the ip_func[] we run.
 */
void
lwkt_process_ipiq(void)
{
    int n;
    int cpuid = mycpu->gd_cpuid;

    for (n = 0; n < ncpus; ++n) {
	lwkt_ipiq_t ip;
	int ri;

	if (n == cpuid)
	    continue;
	ip = globaldata_find(n)->gd_ipiq;
	if (ip == NULL)
	    continue;
	ip = &ip[cpuid];

	/*
	 * Note: xindex is only updated after we are sure the function has
	 * finished execution.  Beware lwkt_process_ipiq() reentrancy!  The
	 * function may send an IPI which may block/drain.
	 */
	while (ip->ip_rindex != ip->ip_windex) {
	    ri = ip->ip_rindex & MAXCPUFIFO_MASK;
	    ++ip->ip_rindex;
	    ip->ip_func[ri](ip->ip_arg[ri]);
	    /* YYY memory barrier */
	    ip->ip_xindex = ip->ip_rindex;
	}
    }
}

#else

int
lwkt_send_ipiq(int dcpu, ipifunc_t func, void *arg)
{
    panic("lwkt_send_ipiq: UP box! (%d,%p,%p)", dcpu, func, arg);
    return(0); /* NOT REACHED */
}

void
lwkt_wait_ipiq(int dcpu, int seq)
{
    panic("lwkt_wait_ipiq: UP box! (%d,%d)", dcpu, seq);
}

#endif
