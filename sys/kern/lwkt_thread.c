/*
 * Copyright (c) 2003-2010 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Matthew Dillon <dillon@backplane.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
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

/*
 * Each cpu in a system has its own self-contained light weight kernel
 * thread scheduler, which means that generally speaking we only need
 * to use a critical section to avoid problems.  Foreign thread 
 * scheduling is queued via (async) IPIs.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/proc.h>
#include <sys/rtprio.h>
#include <sys/kinfo.h>
#include <sys/queue.h>
#include <sys/sysctl.h>
#include <sys/kthread.h>
#include <machine/cpu.h>
#include <sys/lock.h>
#include <sys/caps.h>
#include <sys/spinlock.h>
#include <sys/ktr.h>

#include <sys/thread2.h>
#include <sys/spinlock2.h>
#include <sys/mplock2.h>

#include <sys/dsched.h>

#include <vm/vm.h>
#include <vm/vm_param.h>
#include <vm/vm_kern.h>
#include <vm/vm_object.h>
#include <vm/vm_page.h>
#include <vm/vm_map.h>
#include <vm/vm_pager.h>
#include <vm/vm_extern.h>

#include <machine/stdarg.h>
#include <machine/smp.h>

#if !defined(KTR_CTXSW)
#define KTR_CTXSW KTR_ALL
#endif
KTR_INFO_MASTER(ctxsw);
KTR_INFO(KTR_CTXSW, ctxsw, sw, 0, "#cpu[%d].td = %p",
	 sizeof(int) + sizeof(struct thread *));
KTR_INFO(KTR_CTXSW, ctxsw, pre, 1, "#cpu[%d].td = %p",
	 sizeof(int) + sizeof(struct thread *));
KTR_INFO(KTR_CTXSW, ctxsw, newtd, 2, "#threads[%p].name = %s",
	 sizeof (struct thread *) + sizeof(char *));
KTR_INFO(KTR_CTXSW, ctxsw, deadtd, 3, "#threads[%p].name = <dead>", sizeof (struct thread *));

static MALLOC_DEFINE(M_THREAD, "thread", "lwkt threads");

#ifdef	INVARIANTS
static int panic_on_cscount = 0;
#endif
static __int64_t switch_count = 0;
static __int64_t preempt_hit = 0;
static __int64_t preempt_miss = 0;
static __int64_t preempt_weird = 0;
static __int64_t token_contention_count __debugvar = 0;
static int lwkt_use_spin_port;
static struct objcache *thread_cache;

#ifdef SMP
static void lwkt_schedule_remote(void *arg, int arg2, struct intrframe *frame);
#endif
static void lwkt_fairq_accumulate(globaldata_t gd, thread_t td);

extern void cpu_heavy_restore(void);
extern void cpu_lwkt_restore(void);
extern void cpu_kthread_restore(void);
extern void cpu_idle_restore(void);

/*
 * We can make all thread ports use the spin backend instead of the thread
 * backend.  This should only be set to debug the spin backend.
 */
TUNABLE_INT("lwkt.use_spin_port", &lwkt_use_spin_port);

#ifdef	INVARIANTS
SYSCTL_INT(_lwkt, OID_AUTO, panic_on_cscount, CTLFLAG_RW, &panic_on_cscount, 0,
    "Panic if attempting to switch lwkt's while mastering cpusync");
#endif
SYSCTL_QUAD(_lwkt, OID_AUTO, switch_count, CTLFLAG_RW, &switch_count, 0,
    "Number of switched threads");
SYSCTL_QUAD(_lwkt, OID_AUTO, preempt_hit, CTLFLAG_RW, &preempt_hit, 0, 
    "Successful preemption events");
SYSCTL_QUAD(_lwkt, OID_AUTO, preempt_miss, CTLFLAG_RW, &preempt_miss, 0, 
    "Failed preemption events");
SYSCTL_QUAD(_lwkt, OID_AUTO, preempt_weird, CTLFLAG_RW, &preempt_weird, 0,
    "Number of preempted threads.");
#ifdef	INVARIANTS
SYSCTL_QUAD(_lwkt, OID_AUTO, token_contention_count, CTLFLAG_RW,
	&token_contention_count, 0, "spinning due to token contention");
#endif
static int fairq_enable = 1;
SYSCTL_INT(_lwkt, OID_AUTO, fairq_enable, CTLFLAG_RW, &fairq_enable, 0,
    "Turn on fairq priority accumulators");
static int user_pri_sched = 0;
SYSCTL_INT(_lwkt, OID_AUTO, user_pri_sched, CTLFLAG_RW, &user_pri_sched, 0,
    "");
static int preempt_enable = 1;
SYSCTL_INT(_lwkt, OID_AUTO, preempt_enable, CTLFLAG_RW, &preempt_enable, 0,
    "Enable preemption");


/*
 * These helper procedures handle the runq, they can only be called from
 * within a critical section.
 *
 * WARNING!  Prior to SMP being brought up it is possible to enqueue and
 * dequeue threads belonging to other cpus, so be sure to use td->td_gd
 * instead of 'mycpu' when referencing the globaldata structure.   Once
 * SMP live enqueuing and dequeueing only occurs on the current cpu.
 */
static __inline
void
_lwkt_dequeue(thread_t td)
{
    if (td->td_flags & TDF_RUNQ) {
	struct globaldata *gd = td->td_gd;

	td->td_flags &= ~TDF_RUNQ;
	TAILQ_REMOVE(&gd->gd_tdrunq, td, td_threadq);
	gd->gd_fairq_total_pri -= td->td_pri;
	if (TAILQ_FIRST(&gd->gd_tdrunq) == NULL)
		atomic_clear_int_nonlocked(&gd->gd_reqflags, RQF_RUNNING);
    }
}

/*
 * Priority enqueue.
 *
 * NOTE: There are a limited number of lwkt threads runnable since user
 *	 processes only schedule one at a time per cpu.
 */
static __inline
void
_lwkt_enqueue(thread_t td)
{
    thread_t xtd;

    if ((td->td_flags & (TDF_RUNQ|TDF_MIGRATING|TDF_BLOCKQ)) == 0) {
	struct globaldata *gd = td->td_gd;

	td->td_flags |= TDF_RUNQ;
	xtd = TAILQ_FIRST(&gd->gd_tdrunq);
	if (xtd == NULL) {
		TAILQ_INSERT_TAIL(&gd->gd_tdrunq, td, td_threadq);
		atomic_set_int_nonlocked(&gd->gd_reqflags, RQF_RUNNING);
	} else {
		while (xtd && xtd->td_pri > td->td_pri)
			xtd = TAILQ_NEXT(xtd, td_threadq);
		if (xtd)
			TAILQ_INSERT_BEFORE(xtd, td, td_threadq);
		else
			TAILQ_INSERT_TAIL(&gd->gd_tdrunq, td, td_threadq);
	}
	gd->gd_fairq_total_pri += td->td_pri;
    }
}

static __boolean_t
_lwkt_thread_ctor(void *obj, void *privdata, int ocflags)
{
	struct thread *td = (struct thread *)obj;

	td->td_kstack = NULL;
	td->td_kstack_size = 0;
	td->td_flags = TDF_ALLOCATED_THREAD;
	return (1);
}

static void
_lwkt_thread_dtor(void *obj, void *privdata)
{
	struct thread *td = (struct thread *)obj;

	KASSERT(td->td_flags & TDF_ALLOCATED_THREAD,
	    ("_lwkt_thread_dtor: not allocated from objcache"));
	KASSERT((td->td_flags & TDF_ALLOCATED_STACK) && td->td_kstack &&
		td->td_kstack_size > 0,
	    ("_lwkt_thread_dtor: corrupted stack"));
	kmem_free(&kernel_map, (vm_offset_t)td->td_kstack, td->td_kstack_size);
}

/*
 * Initialize the lwkt s/system.
 */
void
lwkt_init(void)
{
    /* An objcache has 2 magazines per CPU so divide cache size by 2. */
    thread_cache = objcache_create_mbacked(M_THREAD, sizeof(struct thread),
			NULL, CACHE_NTHREADS/2,
			_lwkt_thread_ctor, _lwkt_thread_dtor, NULL);
}

/*
 * Schedule a thread to run.  As the current thread we can always safely
 * schedule ourselves, and a shortcut procedure is provided for that
 * function.
 *
 * (non-blocking, self contained on a per cpu basis)
 */
void
lwkt_schedule_self(thread_t td)
{
    crit_enter_quick(td);
    KASSERT(td != &td->td_gd->gd_idlethread,
	    ("lwkt_schedule_self(): scheduling gd_idlethread is illegal!"));
    KKASSERT(td->td_lwp == NULL || (td->td_lwp->lwp_flag & LWP_ONRUNQ) == 0);
    _lwkt_enqueue(td);
    crit_exit_quick(td);
}

/*
 * Deschedule a thread.
 *
 * (non-blocking, self contained on a per cpu basis)
 */
void
lwkt_deschedule_self(thread_t td)
{
    crit_enter_quick(td);
    _lwkt_dequeue(td);
    crit_exit_quick(td);
}

/*
 * LWKTs operate on a per-cpu basis
 *
 * WARNING!  Called from early boot, 'mycpu' may not work yet.
 */
void
lwkt_gdinit(struct globaldata *gd)
{
    TAILQ_INIT(&gd->gd_tdrunq);
    TAILQ_INIT(&gd->gd_tdallq);
}

/*
 * Create a new thread.  The thread must be associated with a process context
 * or LWKT start address before it can be scheduled.  If the target cpu is
 * -1 the thread will be created on the current cpu.
 *
 * If you intend to create a thread without a process context this function
 * does everything except load the startup and switcher function.
 */
thread_t
lwkt_alloc_thread(struct thread *td, int stksize, int cpu, int flags)
{
    globaldata_t gd = mycpu;
    void *stack;

    /*
     * If static thread storage is not supplied allocate a thread.  Reuse
     * a cached free thread if possible.  gd_freetd is used to keep an exiting
     * thread intact through the exit.
     */
    if (td == NULL) {
	crit_enter_gd(gd);
	if ((td = gd->gd_freetd) != NULL) {
	    KKASSERT((td->td_flags & (TDF_RUNNING|TDF_PREEMPT_LOCK|
				      TDF_RUNQ)) == 0);
	    gd->gd_freetd = NULL;
	} else {
	    td = objcache_get(thread_cache, M_WAITOK);
	    KKASSERT((td->td_flags & (TDF_RUNNING|TDF_PREEMPT_LOCK|
				      TDF_RUNQ)) == 0);
	}
	crit_exit_gd(gd);
    	KASSERT((td->td_flags &
		 (TDF_ALLOCATED_THREAD|TDF_RUNNING)) == TDF_ALLOCATED_THREAD,
		("lwkt_alloc_thread: corrupted td flags 0x%X", td->td_flags));
    	flags |= td->td_flags & (TDF_ALLOCATED_THREAD|TDF_ALLOCATED_STACK);
    }

    /*
     * Try to reuse cached stack.
     */
    if ((stack = td->td_kstack) != NULL && td->td_kstack_size != stksize) {
	if (flags & TDF_ALLOCATED_STACK) {
	    kmem_free(&kernel_map, (vm_offset_t)stack, td->td_kstack_size);
	    stack = NULL;
	}
    }
    if (stack == NULL) {
	stack = (void *)kmem_alloc_stack(&kernel_map, stksize);
	flags |= TDF_ALLOCATED_STACK;
    }
    if (cpu < 0)
	lwkt_init_thread(td, stack, stksize, flags, gd);
    else
	lwkt_init_thread(td, stack, stksize, flags, globaldata_find(cpu));
    return(td);
}

/*
 * Initialize a preexisting thread structure.  This function is used by
 * lwkt_alloc_thread() and also used to initialize the per-cpu idlethread.
 *
 * All threads start out in a critical section at a priority of
 * TDPRI_KERN_DAEMON.  Higher level code will modify the priority as
 * appropriate.  This function may send an IPI message when the 
 * requested cpu is not the current cpu and consequently gd_tdallq may
 * not be initialized synchronously from the point of view of the originating
 * cpu.
 *
 * NOTE! we have to be careful in regards to creating threads for other cpus
 * if SMP has not yet been activated.
 */
#ifdef SMP

static void
lwkt_init_thread_remote(void *arg)
{
    thread_t td = arg;

    /*
     * Protected by critical section held by IPI dispatch
     */
    TAILQ_INSERT_TAIL(&td->td_gd->gd_tdallq, td, td_allq);
}

#endif

/*
 * lwkt core thread structural initialization.
 *
 * NOTE: All threads are initialized as mpsafe threads.
 */
void
lwkt_init_thread(thread_t td, void *stack, int stksize, int flags,
		struct globaldata *gd)
{
    globaldata_t mygd = mycpu;

    bzero(td, sizeof(struct thread));
    td->td_kstack = stack;
    td->td_kstack_size = stksize;
    td->td_flags = flags;
    td->td_gd = gd;
    td->td_pri = TDPRI_KERN_DAEMON;
    td->td_critcount = 1;
    td->td_toks_stop = &td->td_toks_base;
    if (lwkt_use_spin_port)
	lwkt_initport_spin(&td->td_msgport);
    else
	lwkt_initport_thread(&td->td_msgport, td);
    pmap_init_thread(td);
#ifdef SMP
    /*
     * Normally initializing a thread for a remote cpu requires sending an
     * IPI.  However, the idlethread is setup before the other cpus are
     * activated so we have to treat it as a special case.  XXX manipulation
     * of gd_tdallq requires the BGL.
     */
    if (gd == mygd || td == &gd->gd_idlethread) {
	crit_enter_gd(mygd);
	TAILQ_INSERT_TAIL(&gd->gd_tdallq, td, td_allq);
	crit_exit_gd(mygd);
    } else {
	lwkt_send_ipiq(gd, lwkt_init_thread_remote, td);
    }
#else
    crit_enter_gd(mygd);
    TAILQ_INSERT_TAIL(&gd->gd_tdallq, td, td_allq);
    crit_exit_gd(mygd);
#endif

    dsched_new_thread(td);
}

void
lwkt_set_comm(thread_t td, const char *ctl, ...)
{
    __va_list va;

    __va_start(va, ctl);
    kvsnprintf(td->td_comm, sizeof(td->td_comm), ctl, va);
    __va_end(va);
    KTR_LOG(ctxsw_newtd, td, &td->td_comm[0]);
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
    KKASSERT((td->td_flags & (TDF_RUNNING|TDF_PREEMPT_LOCK|TDF_RUNQ)) == 0);
    if (td->td_flags & TDF_ALLOCATED_THREAD) {
    	objcache_put(thread_cache, td);
    } else if (td->td_flags & TDF_ALLOCATED_STACK) {
	/* client-allocated struct with internally allocated stack */
	KASSERT(td->td_kstack && td->td_kstack_size > 0,
	    ("lwkt_free_thread: corrupted stack"));
	kmem_free(&kernel_map, (vm_offset_t)td->td_kstack, td->td_kstack_size);
	td->td_kstack = NULL;
	td->td_kstack_size = 0;
    }
    KTR_LOG(ctxsw_deadtd, td);
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
 * The MP lock may be out of sync with the thread's td_mpcount + td_xpcount.
 * lwkt_switch() cleans it up.
 *
 * Note that the td_switch() function cannot do anything that requires
 * the MP lock since the MP lock will have already been setup for
 * the target thread (not the current thread).  It's nice to have a scheduler
 * that does not need the MP lock to work because it allows us to do some
 * really cool high-performance MP lock optimizations.
 *
 * PREEMPTION NOTE: Preemption occurs via lwkt_preempt().  lwkt_switch()
 * is not called by the current thread in the preemption case, only when
 * the preempting thread blocks (in order to return to the original thread).
 */
void
lwkt_switch(void)
{
    globaldata_t gd = mycpu;
    thread_t td = gd->gd_curthread;
    thread_t ntd;
    thread_t xtd;
    thread_t nlast;
    int nquserok;
#ifdef SMP
    int mpheld;
#endif
    int didaccumulate;
    const char *lmsg;	/* diagnostic - 'systat -pv 1' */
    const void *laddr;

    /*
     * Switching from within a 'fast' (non thread switched) interrupt or IPI
     * is illegal.  However, we may have to do it anyway if we hit a fatal
     * kernel trap or we have paniced.
     *
     * If this case occurs save and restore the interrupt nesting level.
     */
    if (gd->gd_intr_nesting_level) {
	int savegdnest;
	int savegdtrap;

	if (gd->gd_trap_nesting_level == 0 && panic_cpu_gd != mycpu) {
	    panic("lwkt_switch: Attempt to switch from a "
		  "a fast interrupt, ipi, or hard code section, "
		  "td %p\n",
		  td);
	} else {
	    savegdnest = gd->gd_intr_nesting_level;
	    savegdtrap = gd->gd_trap_nesting_level;
	    gd->gd_intr_nesting_level = 0;
	    gd->gd_trap_nesting_level = 0;
	    if ((td->td_flags & TDF_PANICWARN) == 0) {
		td->td_flags |= TDF_PANICWARN;
		kprintf("Warning: thread switch from interrupt, IPI, "
			"or hard code section.\n"
			"thread %p (%s)\n", td, td->td_comm);
		print_backtrace(-1);
	    }
	    lwkt_switch();
	    gd->gd_intr_nesting_level = savegdnest;
	    gd->gd_trap_nesting_level = savegdtrap;
	    return;
	}
    }

    /*
     * Passive release (used to transition from user to kernel mode
     * when we block or switch rather then when we enter the kernel).
     * This function is NOT called if we are switching into a preemption
     * or returning from a preemption.  Typically this causes us to lose
     * our current process designation (if we have one) and become a true
     * LWKT thread, and may also hand the current process designation to
     * another process and schedule thread.
     */
    if (td->td_release)
	    td->td_release(td);

    crit_enter_gd(gd);
    if (TD_TOKS_HELD(td))
	    lwkt_relalltokens(td);

    /*
     * We had better not be holding any spin locks, but don't get into an
     * endless panic loop.
     */
    KASSERT(gd->gd_spinlocks_wr == 0 || panicstr != NULL, 
	    ("lwkt_switch: still holding %d exclusive spinlocks!",
	     gd->gd_spinlocks_wr));


#ifdef SMP
    /*
     * td_mpcount + td_xpcount cannot be used to determine if we currently
     * hold the MP lock because get_mplock() will increment it prior to
     * attempting to get the lock, and switch out if it can't.  Our
     * ownership of the actual lock will remain stable while we are
     * in a critical section, and once we actually acquire the underlying
     * lock as long as the count is greater than 0.
     */
    mpheld = MP_LOCK_HELD(gd);
#ifdef	INVARIANTS
    if (td->td_cscount) {
	kprintf("Diagnostic: attempt to switch while mastering cpusync: %p\n",
		td);
	if (panic_on_cscount)
	    panic("switching while mastering cpusync");
    }
#endif
#endif

    /*
     * If we had preempted another thread on this cpu, resume the preempted
     * thread.  This occurs transparently, whether the preempted thread
     * was scheduled or not (it may have been preempted after descheduling
     * itself).
     *
     * We have to setup the MP lock for the original thread after backing
     * out the adjustment that was made to curthread when the original
     * was preempted.
     */
    if ((ntd = td->td_preempted) != NULL) {
	KKASSERT(ntd->td_flags & TDF_PREEMPT_LOCK);
#ifdef SMP
	if (ntd->td_mpcount + ntd->td_xpcount && mpheld == 0) {
	    panic("MPLOCK NOT HELD ON RETURN: %p %p %d %d",
	       td, ntd, td->td_mpcount, ntd->td_mpcount + ntd->td_xpcount);
	}
	td->td_xpcount = 0;
#endif
	ntd->td_flags |= TDF_PREEMPT_DONE;

	/*
	 * The interrupt may have woken a thread up, we need to properly
	 * set the reschedule flag if the originally interrupted thread is
	 * at a lower priority.
	 */
	if (TAILQ_FIRST(&gd->gd_tdrunq) &&
	    TAILQ_FIRST(&gd->gd_tdrunq)->td_pri > ntd->td_pri) {
	    need_lwkt_resched();
	}
	/* YYY release mp lock on switchback if original doesn't need it */
	goto havethread_preempted;
    }

    /*
     * Implement round-robin fairq with priority insertion.  The priority
     * insertion is handled by _lwkt_enqueue()
     *
     * We have to adjust the MP lock for the target thread.  If we
     * need the MP lock and cannot obtain it we try to locate a
     * thread that does not need the MP lock.  If we cannot, we spin
     * instead of HLT.
     *
     * A similar issue exists for the tokens held by the target thread.
     * If we cannot obtain ownership of the tokens we cannot immediately
     * schedule the thread.
     */
    for (;;) {
	clear_lwkt_resched();
	didaccumulate = 0;
	ntd = TAILQ_FIRST(&gd->gd_tdrunq);

	/*
	 * Hotpath if we can get all necessary resources.
	 *
	 * If nothing is runnable switch to the idle thread
	 */
	if (ntd == NULL) {
	    ntd = &gd->gd_idlethread;
	    if (gd->gd_reqflags & RQF_IDLECHECK_MASK)
		    ntd->td_flags |= TDF_IDLE_NOHLT;
#ifdef SMP
	    KKASSERT(ntd->td_xpcount == 0);
	    if (ntd->td_mpcount) {
		if (gd->gd_trap_nesting_level == 0 && panicstr == NULL)
		    panic("Idle thread %p was holding the BGL!", ntd);
		if (mpheld == 0) {
		    set_cpu_contention_mask(gd);
		    handle_cpu_contention_mask();
		    cpu_try_mplock();
		    mpheld = MP_LOCK_HELD(gd);
		    cpu_pause();
		    continue;
		}
	    }
	    clr_cpu_contention_mask(gd);
#endif
	    cpu_time.cp_msg[0] = 0;
	    cpu_time.cp_stallpc = 0;
	    goto haveidle;
	}

	/*
	 * Hotpath schedule
	 *
	 * NOTE: For UP there is no mplock and lwkt_getalltokens()
	 *	     always succeeds.
	 */
	if (ntd->td_fairq_accum >= 0 &&
#ifdef SMP
	    (ntd->td_mpcount + ntd->td_xpcount == 0 ||
	     mpheld || cpu_try_mplock()) &&
#endif
	    (!TD_TOKS_HELD(ntd) || lwkt_getalltokens(ntd, &lmsg, &laddr))
	) {
#ifdef SMP
	    clr_cpu_contention_mask(gd);
#endif
	    goto havethread;
	}

	lmsg = NULL;
	laddr = NULL;

#ifdef SMP
	if (ntd->td_fairq_accum >= 0)
		set_cpu_contention_mask(gd);
	/* Reload mpheld (it become stale after mplock/token ops) */
	mpheld = MP_LOCK_HELD(gd);
	if (ntd->td_mpcount + ntd->td_xpcount && mpheld == 0) {
	    lmsg = "mplock";
	    laddr = ntd->td_mplock_stallpc;
	}
#endif

	/*
	 * Coldpath - unable to schedule ntd, continue looking for threads
	 * to schedule.  This is only allowed of the (presumably) kernel
	 * thread exhausted its fair share.  A kernel thread stuck on
	 * resources does not currently allow a user thread to get in
	 * front of it.
	 */
#ifdef SMP
	nquserok = ((ntd->td_pri < TDPRI_KERN_LPSCHED) ||
		    (ntd->td_fairq_accum < 0));
#else
	nquserok = 1;
#endif
	nlast = NULL;

	for (;;) {
	    /*
	     * If the fair-share scheduler ran out ntd gets moved to the
	     * end and its accumulator will be bumped, if it didn't we
	     * maintain the same queue position.
	     *
	     * nlast keeps track of the last element prior to any moves.
	     */
	    if (ntd->td_fairq_accum < 0) {
		lwkt_fairq_accumulate(gd, ntd);
		didaccumulate = 1;

		/*
		 * Move to end
		 */
		xtd = TAILQ_NEXT(ntd, td_threadq);
		TAILQ_REMOVE(&gd->gd_tdrunq, ntd, td_threadq);
		TAILQ_INSERT_TAIL(&gd->gd_tdrunq, ntd, td_threadq);

		/*
		 * Set terminal element (nlast)
		 */
		if (nlast == NULL) {
		    nlast = ntd;
		    if (xtd == NULL)
			xtd = ntd;
		}
		ntd = xtd;
	    } else {
		ntd = TAILQ_NEXT(ntd, td_threadq);
	    }

	    /*
	     * If we exhausted the run list switch to the idle thread.
	     * Since one or more threads had resource acquisition issues
	     * we do not allow the idle thread to halt.
	     *
	     * NOTE: nlast can be NULL.
	     */
	    if (ntd == nlast) {
		cpu_pause();
		ntd = &gd->gd_idlethread;
		ntd->td_flags |= TDF_IDLE_NOHLT;
#ifdef SMP
		KKASSERT(ntd->td_xpcount == 0);
		if (ntd->td_mpcount) {
		    mpheld = MP_LOCK_HELD(gd);
		    if (gd->gd_trap_nesting_level == 0 && panicstr == NULL)
			panic("Idle thread %p was holding the BGL!", ntd);
		    if (mpheld == 0) {
			set_cpu_contention_mask(gd);
			handle_cpu_contention_mask();
			cpu_try_mplock();
			mpheld = MP_LOCK_HELD(gd);
			cpu_pause();
			break;		/* try again from the top, almost */
		    }
		}
#endif

		/*
		 * If fairq accumulations occured we do not schedule the
		 * idle thread.  This will cause us to try again from
		 * the (almost) top.
		 */
		if (didaccumulate)
			break;		/* try again from the top, almost */
		if (lmsg)
		    strlcpy(cpu_time.cp_msg, lmsg, sizeof(cpu_time.cp_msg));
		cpu_time.cp_stallpc = (uintptr_t)laddr;
		goto haveidle;
	    }

	    /*
	     * Try to switch to this thread.
	     *
	     * NOTE: For UP there is no mplock and lwkt_getalltokens()
	     *	     always succeeds.
	     */
	    if ((ntd->td_pri >= TDPRI_KERN_LPSCHED || nquserok ||
		user_pri_sched) && ntd->td_fairq_accum >= 0 &&
#ifdef SMP
		(ntd->td_mpcount + ntd->td_xpcount == 0 ||
		 mpheld || cpu_try_mplock()) &&
#endif
		(!TD_TOKS_HELD(ntd) || lwkt_getalltokens(ntd, &lmsg, &laddr))
	    ) {
#ifdef SMP
		    clr_cpu_contention_mask(gd);
#endif
		    goto havethread;
	    }
#ifdef SMP
	    if (ntd->td_fairq_accum >= 0)
		    set_cpu_contention_mask(gd);
	    /*
	     * Reload mpheld (it become stale after mplock/token ops).
	     */
	    mpheld = MP_LOCK_HELD(gd);
	    if (ntd->td_mpcount + ntd->td_xpcount && mpheld == 0) {
		lmsg = "mplock";
		laddr = ntd->td_mplock_stallpc;
	    }
	    if (ntd->td_pri >= TDPRI_KERN_LPSCHED && ntd->td_fairq_accum >= 0)
		nquserok = 0;
#endif
	}

	/*
	 * All threads exhausted but we can loop due to a negative
	 * accumulator.
	 *
	 * While we are looping in the scheduler be sure to service
	 * any interrupts which were made pending due to our critical
	 * section, otherwise we could livelock (e.g.) IPIs.
	 *
	 * NOTE: splz can enter and exit the mplock so mpheld is
	 * stale after this call.
	 */
	splz_check();

#ifdef SMP
	/*
	 * Our mplock can be cached and cause other cpus to livelock
	 * if we loop due to e.g. not being able to acquire tokens.
	 */
	if (MP_LOCK_HELD(gd))
	    cpu_rel_mplock(gd->gd_cpuid);
	mpheld = 0;
#endif
    }

    /*
     * Do the actual switch.  WARNING: mpheld is stale here.
     *
     * We must always decrement td_fairq_accum on non-idle threads just
     * in case a thread never gets a tick due to being in a continuous
     * critical section.  The page-zeroing code does that.
     *
     * If the thread we came up with is a higher or equal priority verses
     * the thread at the head of the queue we move our thread to the
     * front.  This way we can always check the front of the queue.
     */
havethread:
    ++gd->gd_cnt.v_swtch;
    --ntd->td_fairq_accum;
    xtd = TAILQ_FIRST(&gd->gd_tdrunq);
    if (ntd != xtd && ntd->td_pri >= xtd->td_pri) {
	TAILQ_REMOVE(&gd->gd_tdrunq, ntd, td_threadq);
	TAILQ_INSERT_HEAD(&gd->gd_tdrunq, ntd, td_threadq);
    }
havethread_preempted:
    ;
    /*
     * If the new target does not need the MP lock and we are holding it,
     * release the MP lock.  If the new target requires the MP lock we have
     * already acquired it for the target.
     *
     * WARNING: mpheld is stale here.
     */
haveidle:
    KASSERT(ntd->td_critcount,
	    ("priority problem in lwkt_switch %d %d", td->td_pri, ntd->td_pri));
#ifdef SMP
    if (ntd->td_mpcount + ntd->td_xpcount == 0 ) {
	if (MP_LOCK_HELD(gd))
	    cpu_rel_mplock(gd->gd_cpuid);
    } else {
	ASSERT_MP_LOCK_HELD(ntd);
    }
#endif
    if (td != ntd) {
	++switch_count;
	KTR_LOG(ctxsw_sw, gd->gd_cpuid, ntd);
	td->td_switch(ntd);
    }
    /* NOTE: current cpu may have changed after switch */
    crit_exit_quick(td);
}

/*
 * Request that the target thread preempt the current thread.  Preemption
 * only works under a specific set of conditions:
 *
 *	- We are not preempting ourselves
 *	- The target thread is owned by the current cpu
 *	- We are not currently being preempted
 *	- The target is not currently being preempted
 *	- We are not holding any spin locks
 *	- The target thread is not holding any tokens
 *	- We are able to satisfy the target's MP lock requirements (if any).
 *
 * THE CALLER OF LWKT_PREEMPT() MUST BE IN A CRITICAL SECTION.  Typically
 * this is called via lwkt_schedule() through the td_preemptable callback.
 * critcount is the managed critical priority that we should ignore in order
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
 * out of sync with the physical mp_lock, but we do not have to preserve
 * the original ownership of the lock if it was out of synch (that is, we
 * can leave it synchronized on return).
 */
void
lwkt_preempt(thread_t ntd, int critcount)
{
    struct globaldata *gd = mycpu;
    thread_t td;
#ifdef SMP
    int mpheld;
    int savecnt;
#endif
    int save_gd_intr_nesting_level;

    /*
     * The caller has put us in a critical section.  We can only preempt
     * if the caller of the caller was not in a critical section (basically
     * a local interrupt), as determined by the 'critcount' parameter.  We
     * also can't preempt if the caller is holding any spinlocks (even if
     * he isn't in a critical section).  This also handles the tokens test.
     *
     * YYY The target thread must be in a critical section (else it must
     * inherit our critical section?  I dunno yet).
     *
     * Set need_lwkt_resched() unconditionally for now YYY.
     */
    KASSERT(ntd->td_critcount, ("BADCRIT0 %d", ntd->td_pri));

    if (preempt_enable == 0) {
	++preempt_miss;
	return;
    }

    td = gd->gd_curthread;
    if (ntd->td_pri <= td->td_pri) {
	++preempt_miss;
	return;
    }
    if (td->td_critcount > critcount) {
	++preempt_miss;
	need_lwkt_resched();
	return;
    }
#ifdef SMP
    if (ntd->td_gd != gd) {
	++preempt_miss;
	need_lwkt_resched();
	return;
    }
#endif
    /*
     * We don't have to check spinlocks here as they will also bump
     * td_critcount.
     *
     * Do not try to preempt if the target thread is holding any tokens.
     * We could try to acquire the tokens but this case is so rare there
     * is no need to support it.
     */
    KKASSERT(gd->gd_spinlocks_wr == 0);

    if (TD_TOKS_HELD(ntd)) {
	++preempt_miss;
	need_lwkt_resched();
	return;
    }
    if (td == ntd || ((td->td_flags | ntd->td_flags) & TDF_PREEMPT_LOCK)) {
	++preempt_weird;
	need_lwkt_resched();
	return;
    }
    if (ntd->td_preempted) {
	++preempt_hit;
	need_lwkt_resched();
	return;
    }
#ifdef SMP
    /*
     * NOTE: An interrupt might have occured just as we were transitioning
     * to or from the MP lock.  In this case td_mpcount will be pre-disposed
     * (non-zero) but not actually synchronized with the mp_lock itself.
     * We can use it to imply an MP lock requirement for the preemption but
     * we cannot use it to test whether we hold the MP lock or not.
     */
    savecnt = td->td_mpcount;
    mpheld = MP_LOCK_HELD(gd);
    ntd->td_xpcount = td->td_mpcount + td->td_xpcount;
    if (mpheld == 0 && ntd->td_mpcount + ntd->td_xpcount && !cpu_try_mplock()) {
	ntd->td_xpcount = 0;
	++preempt_miss;
	need_lwkt_resched();
	return;
    }
#endif

    /*
     * Since we are able to preempt the current thread, there is no need to
     * call need_lwkt_resched().
     *
     * We must temporarily clear gd_intr_nesting_level around the switch
     * since switchouts from the target thread are allowed (they will just
     * return to our thread), and since the target thread has its own stack.
     */
    ++preempt_hit;
    ntd->td_preempted = td;
    td->td_flags |= TDF_PREEMPT_LOCK;
    KTR_LOG(ctxsw_pre, gd->gd_cpuid, ntd);
    save_gd_intr_nesting_level = gd->gd_intr_nesting_level;
    gd->gd_intr_nesting_level = 0;
    td->td_switch(ntd);
    gd->gd_intr_nesting_level = save_gd_intr_nesting_level;

    KKASSERT(ntd->td_preempted && (td->td_flags & TDF_PREEMPT_DONE));
#ifdef SMP
    KKASSERT(savecnt == td->td_mpcount);
    mpheld = MP_LOCK_HELD(gd);
    if (mpheld && td->td_mpcount == 0)
	cpu_rel_mplock(gd->gd_cpuid);
    else if (mpheld == 0 && td->td_mpcount + td->td_xpcount)
	panic("lwkt_preempt(): MP lock was not held through");
#endif
    ntd->td_preempted = NULL;
    td->td_flags &= ~(TDF_PREEMPT_LOCK|TDF_PREEMPT_DONE);
}

/*
 * Conditionally call splz() if gd_reqflags indicates work is pending.
 * This will work inside a critical section but not inside a hard code
 * section.
 *
 * (self contained on a per cpu basis)
 */
void
splz_check(void)
{
    globaldata_t gd = mycpu;
    thread_t td = gd->gd_curthread;

    if ((gd->gd_reqflags & RQF_IDLECHECK_MASK) &&
	gd->gd_intr_nesting_level == 0 &&
	td->td_nest_count < 2)
    {
	splz();
    }
}

/*
 * This version is integrated into crit_exit, reqflags has already
 * been tested but td_critcount has not.
 *
 * We only want to execute the splz() on the 1->0 transition of
 * critcount and not in a hard code section or if too deeply nested.
 */
void
lwkt_maybe_splz(thread_t td)
{
    globaldata_t gd = td->td_gd;

    if (td->td_critcount == 0 &&
	gd->gd_intr_nesting_level == 0 &&
	td->td_nest_count < 2)
    {
	splz();
    }
}

/*
 * This function is used to negotiate a passive release of the current
 * process/lwp designation with the user scheduler, allowing the user
 * scheduler to schedule another user thread.  The related kernel thread
 * (curthread) continues running in the released state.
 */
void
lwkt_passive_release(struct thread *td)
{
    struct lwp *lp = td->td_lwp;

    td->td_release = NULL;
    lwkt_setpri_self(TDPRI_KERN_USER);
    lp->lwp_proc->p_usched->release_curproc(lp);
}


/*
 * This implements a normal yield.  This routine is virtually a nop if
 * there is nothing to yield to but it will always run any pending interrupts
 * if called from a critical section.
 *
 * This yield is designed for kernel threads without a user context.
 *
 * (self contained on a per cpu basis)
 */
void
lwkt_yield(void)
{
    globaldata_t gd = mycpu;
    thread_t td = gd->gd_curthread;
    thread_t xtd;

    if ((gd->gd_reqflags & RQF_IDLECHECK_MASK) && td->td_nest_count < 2)
	splz();
    if (td->td_fairq_accum < 0) {
	lwkt_schedule_self(curthread);
	lwkt_switch();
    } else {
	xtd = TAILQ_FIRST(&gd->gd_tdrunq);
	if (xtd && xtd->td_pri > td->td_pri) {
	    lwkt_schedule_self(curthread);
	    lwkt_switch();
	}
    }
}

/*
 * This yield is designed for kernel threads with a user context.
 *
 * The kernel acting on behalf of the user is potentially cpu-bound,
 * this function will efficiently allow other threads to run and also
 * switch to other processes by releasing.
 *
 * The lwkt_user_yield() function is designed to have very low overhead
 * if no yield is determined to be needed.
 */
void
lwkt_user_yield(void)
{
    globaldata_t gd = mycpu;
    thread_t td = gd->gd_curthread;

    /*
     * Always run any pending interrupts in case we are in a critical
     * section.
     */
    if ((gd->gd_reqflags & RQF_IDLECHECK_MASK) && td->td_nest_count < 2)
	splz();

#ifdef SMP
    /*
     * XXX SEVERE TEMPORARY HACK.  A cpu-bound operation running in the
     * kernel can prevent other cpus from servicing interrupt threads
     * which still require the MP lock (which is a lot of them).  This
     * has a chaining effect since if the interrupt is blocked, so is
     * the event, so normal scheduling will not pick up on the problem.
     */
    if (cpu_contention_mask && td->td_mpcount + td->td_xpcount) {
	yield_mplock(td);
    }
#endif

    /*
     * Switch (which forces a release) if another kernel thread needs
     * the cpu, if userland wants us to resched, or if our kernel
     * quantum has run out.
     */
    if (lwkt_resched_wanted() ||
	user_resched_wanted() ||
	td->td_fairq_accum < 0)
    {
	lwkt_switch();
    }

#if 0
    /*
     * Reacquire the current process if we are released.
     *
     * XXX not implemented atm.  The kernel may be holding locks and such,
     *     so we want the thread to continue to receive cpu.
     */
    if (td->td_release == NULL && lp) {
	lp->lwp_proc->p_usched->acquire_curproc(lp);
	td->td_release = lwkt_passive_release;
	lwkt_setpri_self(TDPRI_USER_NORM);
    }
#endif
}

/*
 * Generic schedule.  Possibly schedule threads belonging to other cpus and
 * deal with threads that might be blocked on a wait queue.
 *
 * We have a little helper inline function which does additional work after
 * the thread has been enqueued, including dealing with preemption and
 * setting need_lwkt_resched() (which prevents the kernel from returning
 * to userland until it has processed higher priority threads).
 *
 * It is possible for this routine to be called after a failed _enqueue
 * (due to the target thread migrating, sleeping, or otherwise blocked).
 * We have to check that the thread is actually on the run queue!
 *
 * reschedok is an optimized constant propagated from lwkt_schedule() or
 * lwkt_schedule_noresched().  By default it is non-zero, causing a
 * reschedule to be requested if the target thread has a higher priority.
 * The port messaging code will set MSG_NORESCHED and cause reschedok to
 * be 0, prevented undesired reschedules.
 */
static __inline
void
_lwkt_schedule_post(globaldata_t gd, thread_t ntd, int ccount, int reschedok)
{
    thread_t otd;

    if (ntd->td_flags & TDF_RUNQ) {
	if (ntd->td_preemptable && reschedok) {
	    ntd->td_preemptable(ntd, ccount);	/* YYY +token */
	} else if (reschedok) {
	    otd = curthread;
	    if (ntd->td_pri > otd->td_pri)
		need_lwkt_resched();
	}

	/*
	 * Give the thread a little fair share scheduler bump if it
	 * has been asleep for a while.  This is primarily to avoid
	 * a degenerate case for interrupt threads where accumulator
	 * crosses into negative territory unnecessarily.
	 */
	if (ntd->td_fairq_lticks != ticks) {
	    ntd->td_fairq_lticks = ticks;
	    ntd->td_fairq_accum += gd->gd_fairq_total_pri;
	    if (ntd->td_fairq_accum > TDFAIRQ_MAX(gd))
		    ntd->td_fairq_accum = TDFAIRQ_MAX(gd);
	}
    }
}

static __inline
void
_lwkt_schedule(thread_t td, int reschedok)
{
    globaldata_t mygd = mycpu;

    KASSERT(td != &td->td_gd->gd_idlethread,
	    ("lwkt_schedule(): scheduling gd_idlethread is illegal!"));
    crit_enter_gd(mygd);
    KKASSERT(td->td_lwp == NULL || (td->td_lwp->lwp_flag & LWP_ONRUNQ) == 0);
    if (td == mygd->gd_curthread) {
	_lwkt_enqueue(td);
    } else {
	/*
	 * If we own the thread, there is no race (since we are in a
	 * critical section).  If we do not own the thread there might
	 * be a race but the target cpu will deal with it.
	 */
#ifdef SMP
	if (td->td_gd == mygd) {
	    _lwkt_enqueue(td);
	    _lwkt_schedule_post(mygd, td, 1, reschedok);
	} else {
	    lwkt_send_ipiq3(td->td_gd, lwkt_schedule_remote, td, 0);
	}
#else
	_lwkt_enqueue(td);
	_lwkt_schedule_post(mygd, td, 1, reschedok);
#endif
    }
    crit_exit_gd(mygd);
}

void
lwkt_schedule(thread_t td)
{
    _lwkt_schedule(td, 1);
}

void
lwkt_schedule_noresched(thread_t td)
{
    _lwkt_schedule(td, 0);
}

#ifdef SMP

/*
 * When scheduled remotely if frame != NULL the IPIQ is being
 * run via doreti or an interrupt then preemption can be allowed.
 *
 * To allow preemption we have to drop the critical section so only
 * one is present in _lwkt_schedule_post.
 */
static void
lwkt_schedule_remote(void *arg, int arg2, struct intrframe *frame)
{
    thread_t td = curthread;
    thread_t ntd = arg;

    if (frame && ntd->td_preemptable) {
	crit_exit_noyield(td);
	_lwkt_schedule(ntd, 1);
	crit_enter_quick(td);
    } else {
	_lwkt_schedule(ntd, 1);
    }
}

/*
 * Thread migration using a 'Pull' method.  The thread may or may not be
 * the current thread.  It MUST be descheduled and in a stable state.
 * lwkt_giveaway() must be called on the cpu owning the thread.
 *
 * At any point after lwkt_giveaway() is called, the target cpu may
 * 'pull' the thread by calling lwkt_acquire().
 *
 * We have to make sure the thread is not sitting on a per-cpu tsleep
 * queue or it will blow up when it moves to another cpu.
 *
 * MPSAFE - must be called under very specific conditions.
 */
void
lwkt_giveaway(thread_t td)
{
    globaldata_t gd = mycpu;

    crit_enter_gd(gd);
    if (td->td_flags & TDF_TSLEEPQ)
	tsleep_remove(td);
    KKASSERT(td->td_gd == gd);
    TAILQ_REMOVE(&gd->gd_tdallq, td, td_allq);
    td->td_flags |= TDF_MIGRATING;
    crit_exit_gd(gd);
}

void
lwkt_acquire(thread_t td)
{
    globaldata_t gd;
    globaldata_t mygd;

    KKASSERT(td->td_flags & TDF_MIGRATING);
    gd = td->td_gd;
    mygd = mycpu;
    if (gd != mycpu) {
	cpu_lfence();
	KKASSERT((td->td_flags & TDF_RUNQ) == 0);
	crit_enter_gd(mygd);
	while (td->td_flags & (TDF_RUNNING|TDF_PREEMPT_LOCK)) {
#ifdef SMP
	    lwkt_process_ipiq();
#endif
	    cpu_lfence();
	}
	cpu_mfence();
	td->td_gd = mygd;
	TAILQ_INSERT_TAIL(&mygd->gd_tdallq, td, td_allq);
	td->td_flags &= ~TDF_MIGRATING;
	crit_exit_gd(mygd);
    } else {
	crit_enter_gd(mygd);
	TAILQ_INSERT_TAIL(&mygd->gd_tdallq, td, td_allq);
	td->td_flags &= ~TDF_MIGRATING;
	crit_exit_gd(mygd);
    }
}

#endif

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
#ifdef SMP
    if (td == curthread) {
	_lwkt_dequeue(td);
    } else {
	if (td->td_gd == mycpu) {
	    _lwkt_dequeue(td);
	} else {
	    lwkt_send_ipiq(td->td_gd, (ipifunc1_t)lwkt_deschedule, td);
	}
    }
#else
    _lwkt_dequeue(td);
#endif
    crit_exit();
}

/*
 * Set the target thread's priority.  This routine does not automatically
 * switch to a higher priority thread, LWKT threads are not designed for
 * continuous priority changes.  Yield if you want to switch.
 */
void
lwkt_setpri(thread_t td, int pri)
{
    KKASSERT(td->td_gd == mycpu);
    if (td->td_pri != pri) {
	KKASSERT(pri >= 0);
	crit_enter();
	if (td->td_flags & TDF_RUNQ) {
	    _lwkt_dequeue(td);
	    td->td_pri = pri;
	    _lwkt_enqueue(td);
	} else {
	    td->td_pri = pri;
	}
	crit_exit();
    }
}

/*
 * Set the initial priority for a thread prior to it being scheduled for
 * the first time.  The thread MUST NOT be scheduled before or during
 * this call.  The thread may be assigned to a cpu other then the current
 * cpu.
 *
 * Typically used after a thread has been created with TDF_STOPPREQ,
 * and before the thread is initially scheduled.
 */
void
lwkt_setpri_initial(thread_t td, int pri)
{
    KKASSERT(pri >= 0);
    KKASSERT((td->td_flags & TDF_RUNQ) == 0);
    td->td_pri = pri;
}

void
lwkt_setpri_self(int pri)
{
    thread_t td = curthread;

    KKASSERT(pri >= 0 && pri <= TDPRI_MAX);
    crit_enter();
    if (td->td_flags & TDF_RUNQ) {
	_lwkt_dequeue(td);
	td->td_pri = pri;
	_lwkt_enqueue(td);
    } else {
	td->td_pri = pri;
    }
    crit_exit();
}

/*
 * 1/hz tick (typically 10ms) x TDFAIRQ_SCALE (typ 8) = 80ms full cycle.
 *
 * Example: two competing threads, same priority N.  decrement by (2*N)
 * increment by N*8, each thread will get 4 ticks.
 */
void
lwkt_fairq_schedulerclock(thread_t td)
{
    if (fairq_enable) {
	while (td) {
	    if (td != &td->td_gd->gd_idlethread) {
		td->td_fairq_accum -= td->td_gd->gd_fairq_total_pri;
		if (td->td_fairq_accum < -TDFAIRQ_MAX(td->td_gd))
			td->td_fairq_accum = -TDFAIRQ_MAX(td->td_gd);
		if (td->td_fairq_accum < 0)
			need_lwkt_resched();
		td->td_fairq_lticks = ticks;
	    }
	    td = td->td_preempted;
	}
    }
}

static void
lwkt_fairq_accumulate(globaldata_t gd, thread_t td)
{
	td->td_fairq_accum += td->td_pri * TDFAIRQ_SCALE;
	if (td->td_fairq_accum > TDFAIRQ_MAX(td->td_gd))
		td->td_fairq_accum = TDFAIRQ_MAX(td->td_gd);
}

/*
 * Migrate the current thread to the specified cpu. 
 *
 * This is accomplished by descheduling ourselves from the current cpu,
 * moving our thread to the tdallq of the target cpu, IPI messaging the
 * target cpu, and switching out.  TDF_MIGRATING prevents scheduling
 * races while the thread is being migrated.
 *
 * We must be sure to remove ourselves from the current cpu's tsleepq
 * before potentially moving to another queue.  The thread can be on
 * a tsleepq due to a left-over tsleep_interlock().
 */
#ifdef SMP
static void lwkt_setcpu_remote(void *arg);
#endif

void
lwkt_setcpu_self(globaldata_t rgd)
{
#ifdef SMP
    thread_t td = curthread;

    if (td->td_gd != rgd) {
	crit_enter_quick(td);
	if (td->td_flags & TDF_TSLEEPQ)
	    tsleep_remove(td);
	td->td_flags |= TDF_MIGRATING;
	lwkt_deschedule_self(td);
	TAILQ_REMOVE(&td->td_gd->gd_tdallq, td, td_allq);
	lwkt_send_ipiq(rgd, (ipifunc1_t)lwkt_setcpu_remote, td);
	lwkt_switch();
	/* we are now on the target cpu */
	TAILQ_INSERT_TAIL(&rgd->gd_tdallq, td, td_allq);
	crit_exit_quick(td);
    }
#endif
}

void
lwkt_migratecpu(int cpuid)
{
#ifdef SMP
	globaldata_t rgd;

	rgd = globaldata_find(cpuid);
	lwkt_setcpu_self(rgd);
#endif
}

/*
 * Remote IPI for cpu migration (called while in a critical section so we
 * do not have to enter another one).  The thread has already been moved to
 * our cpu's allq, but we must wait for the thread to be completely switched
 * out on the originating cpu before we schedule it on ours or the stack
 * state may be corrupt.  We clear TDF_MIGRATING after flushing the GD
 * change to main memory.
 *
 * XXX The use of TDF_MIGRATING might not be sufficient to avoid races
 * against wakeups.  It is best if this interface is used only when there
 * are no pending events that might try to schedule the thread.
 */
#ifdef SMP
static void
lwkt_setcpu_remote(void *arg)
{
    thread_t td = arg;
    globaldata_t gd = mycpu;

    while (td->td_flags & (TDF_RUNNING|TDF_PREEMPT_LOCK)) {
#ifdef SMP
	lwkt_process_ipiq();
#endif
	cpu_lfence();
	cpu_pause();
    }
    td->td_gd = gd;
    cpu_mfence();
    td->td_flags &= ~TDF_MIGRATING;
    KKASSERT(td->td_lwp == NULL || (td->td_lwp->lwp_flag & LWP_ONRUNQ) == 0);
    _lwkt_enqueue(td);
}
#endif

struct lwp *
lwkt_preempted_proc(void)
{
    thread_t td = curthread;
    while (td->td_preempted)
	td = td->td_preempted;
    return(td->td_lwp);
}

/*
 * Create a kernel process/thread/whatever.  It shares it's address space
 * with proc0 - ie: kernel only.
 *
 * NOTE!  By default new threads are created with the MP lock held.  A 
 * thread which does not require the MP lock should release it by calling
 * rel_mplock() at the start of the new thread.
 */
int
lwkt_create(void (*func)(void *), void *arg, struct thread **tdp,
	    thread_t template, int tdflags, int cpu, const char *fmt, ...)
{
    thread_t td;
    __va_list ap;

    td = lwkt_alloc_thread(template, LWKT_THREAD_STACK, cpu,
			   tdflags);
    if (tdp)
	*tdp = td;
    cpu_set_thread_handler(td, lwkt_exit, func, arg);

    /*
     * Set up arg0 for 'ps' etc
     */
    __va_start(ap, fmt);
    kvsnprintf(td->td_comm, sizeof(td->td_comm), fmt, ap);
    __va_end(ap);

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
    thread_t std;
    globaldata_t gd;

    /*
     * Do any cleanup that might block here
     */
    if (td->td_flags & TDF_VERBOSE)
	kprintf("kthread %p %s has exited\n", td, td->td_comm);
    caps_exit(td);
    biosched_done(td);
    dsched_exit_thread(td);

    /*
     * Get us into a critical section to interlock gd_freetd and loop
     * until we can get it freed.
     *
     * We have to cache the current td in gd_freetd because objcache_put()ing
     * it would rip it out from under us while our thread is still active.
     */
    gd = mycpu;
    crit_enter_quick(td);
    while ((std = gd->gd_freetd) != NULL) {
	KKASSERT((std->td_flags & (TDF_RUNNING|TDF_PREEMPT_LOCK)) == 0);
	gd->gd_freetd = NULL;
	objcache_put(thread_cache, std);
    }

    /*
     * Remove thread resources from kernel lists and deschedule us for
     * the last time.  We cannot block after this point or we may end
     * up with a stale td on the tsleepq.
     */
    if (td->td_flags & TDF_TSLEEPQ)
	tsleep_remove(td);
    lwkt_deschedule_self(td);
    lwkt_remove_tdallq(td);

    /*
     * Final cleanup
     */
    KKASSERT(gd->gd_freetd == NULL);
    if (td->td_flags & TDF_ALLOCATED_THREAD)
	gd->gd_freetd = td;
    cpu_thread_exit();
}

void
lwkt_remove_tdallq(thread_t td)
{
    KKASSERT(td->td_gd == mycpu);
    TAILQ_REMOVE(&td->td_gd->gd_tdallq, td, td_allq);
}

/*
 * Code reduction and branch prediction improvements.  Call/return
 * overhead on modern cpus often degenerates into 0 cycles due to
 * the cpu's branch prediction hardware and return pc cache.  We
 * can take advantage of this by not inlining medium-complexity
 * functions and we can also reduce the branch prediction impact
 * by collapsing perfectly predictable branches into a single
 * procedure instead of duplicating it.
 *
 * Is any of this noticeable?  Probably not, so I'll take the
 * smaller code size.
 */
void
crit_exit_wrapper(__DEBUG_CRIT_ARG__)
{
    _crit_exit(mycpu __DEBUG_CRIT_PASS_ARG__);
}

void
crit_panic(void)
{
    thread_t td = curthread;
    int lcrit = td->td_critcount;

    td->td_critcount = 0;
    panic("td_critcount is/would-go negative! %p %d", td, lcrit);
    /* NOT REACHED */
}

#ifdef SMP

/*
 * Called from debugger/panic on cpus which have been stopped.  We must still
 * process the IPIQ while stopped, even if we were stopped while in a critical
 * section (XXX).
 *
 * If we are dumping also try to process any pending interrupts.  This may
 * or may not work depending on the state of the cpu at the point it was
 * stopped.
 */
void
lwkt_smp_stopped(void)
{
    globaldata_t gd = mycpu;

    crit_enter_gd(gd);
    if (dumping) {
	lwkt_process_ipiq();
	splz();
    } else {
	lwkt_process_ipiq();
    }
    crit_exit_gd(gd);
}

#endif
