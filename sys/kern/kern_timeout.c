/*
 * Copyright (c) 2004,2014 The DragonFly Project.  All rights reserved.
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
 * Copyright (c) 1982, 1986, 1991, 1993
 *	The Regents of the University of California.  All rights reserved.
 * (c) UNIX System Laboratories, Inc.
 * All or some portions of this file are derived from material licensed
 * to the University of California by American Telephone and Telegraph
 * Co. or Unix System Laboratories, Inc. and are reproduced herein with
 * the permission of UNIX System Laboratories, Inc.
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
 */
/*
 * The original callout mechanism was based on the work of Adam M. Costello
 * and George Varghese, published in a technical report entitled "Redesigning
 * the BSD Callout and Timer Facilities" and modified slightly for inclusion
 * in FreeBSD by Justin T. Gibbs.  The original work on the data structures
 * used in this implementation was published by G. Varghese and T. Lauck in
 * the paper "Hashed and Hierarchical Timing Wheels: Data Structures for
 * the Efficient Implementation of a Timer Facility" in the Proceedings of
 * the 11th ACM Annual Symposium on Operating Systems Principles,
 * Austin, Texas Nov 1987.
 *
 * The per-cpu augmentation was done by Matthew Dillon.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/callout.h>
#include <sys/kernel.h>
#include <sys/interrupt.h>
#include <sys/thread.h>

#include <sys/thread2.h>
#include <sys/mplock2.h>

struct softclock_pcpu {
	struct callout_tailq *callwheel;
	struct callout * volatile next;
	intptr_t running;	/* NOTE! Bit 0 used to flag wakeup */
	int softticks;		/* softticks index */
	int curticks;		/* per-cpu ticks counter */
	int isrunning;
	struct thread thread;
};

typedef struct softclock_pcpu *softclock_pcpu_t;

static MALLOC_DEFINE(M_CALLOUT, "callout", "callout structures");
static int cwheelsize;
static int cwheelmask;
static struct softclock_pcpu softclock_pcpu_ary[MAXCPU];

static void softclock_handler(void *arg);
static void slotimer_callback(void *arg);
static void callout_reset_ipi(void *arg);
static void callout_stop_ipi(void *arg, int issync, struct intrframe *frame);


static void
swi_softclock_setup(void *arg)
{
	int cpu;
	int i;
	int target;

	/*
	 * Figure out how large a callwheel we need.  It must be a power of 2.
	 *
	 * ncallout is primarily based on available memory, don't explode
	 * the allocations if the system has a lot of cpus.
	 */
	target = ncallout / ncpus + 16;

	cwheelsize = 1;
	while (cwheelsize < target)
		cwheelsize <<= 1;
	cwheelmask = cwheelsize - 1;

	/*
	 * Initialize per-cpu data structures.
	 */
	for (cpu = 0; cpu < ncpus; ++cpu) {
		softclock_pcpu_t sc;

		sc = &softclock_pcpu_ary[cpu];

		sc->callwheel = kmalloc(sizeof(*sc->callwheel) * cwheelsize,
					M_CALLOUT, M_WAITOK|M_ZERO);
		for (i = 0; i < cwheelsize; ++i)
			TAILQ_INIT(&sc->callwheel[i]);

		/*
		 * Mark the softclock handler as being an interrupt thread
		 * even though it really isn't, but do not allow it to
		 * preempt other threads (do not assign td_preemptable).
		 *
		 * Kernel code now assumes that callouts do not preempt
		 * the cpu they were scheduled on.
		 */
		lwkt_create(softclock_handler, sc, NULL,
			    &sc->thread, TDF_NOSTART | TDF_INTTHREAD,
			    cpu, "softclock %d", cpu);
	}
}

/*
 * Must occur after ncpus has been initialized.
 */
SYSINIT(softclock_setup, SI_BOOT2_SOFTCLOCK, SI_ORDER_SECOND,
	swi_softclock_setup, NULL);

/*
 * Clear PENDING and, if possible, also clear ARMED and WAITING.  Returns
 * the flags prior to the clear, atomically (used to check for WAITING).
 *
 * Clearing the cpu association (ARMED) can significantly improve the
 * performance of the next callout_reset*() call.
 */
static __inline
int
callout_unpend_disarm(struct callout *c)
{
	int flags;
	int nflags;

	for (;;) {
		flags = c->c_flags;
		cpu_ccfence();
		nflags = flags & ~(CALLOUT_PENDING | CALLOUT_WAITING);
		if ((flags & CALLOUT_IPI_MASK) == 0)
			nflags &= ~CALLOUT_ARMED;
		if (atomic_cmpset_int(&c->c_flags, flags, nflags)) {
			break;
		}
	}
	return flags;
}

/*
 * This routine is called from the hardclock() (basically a FASTint/IPI) on
 * each cpu in the system.  sc->curticks is this cpu's notion of the timebase.
 * It IS NOT NECESSARILY SYNCHRONIZED WITH 'ticks'!  sc->softticks is where
 * the callwheel is currently indexed.
 *
 * WARNING!  The MP lock is not necessarily held on call, nor can it be
 * safely obtained.
 *
 * sc->softticks is adjusted by either this routine or our helper thread
 * depending on whether the helper thread is running or not.
 */
void
hardclock_softtick(globaldata_t gd)
{
	softclock_pcpu_t sc;

	sc = &softclock_pcpu_ary[gd->gd_cpuid];
	++sc->curticks;
	if (sc->isrunning)
		return;
	if (sc->softticks == sc->curticks) {
		/*
		 * In sync, only wakeup the thread if there is something to
		 * do.
		 */
		if (TAILQ_FIRST(&sc->callwheel[sc->softticks & cwheelmask])) {
			sc->isrunning = 1;
			lwkt_schedule(&sc->thread);
		} else {
			++sc->softticks;
		}
	} else {
		/*
		 * out of sync, wakeup the thread unconditionally so it can
		 * catch up.
		 */
		sc->isrunning = 1;
		lwkt_schedule(&sc->thread);
	}
}

/*
 * This procedure is the main loop of our per-cpu helper thread.  The
 * sc->isrunning flag prevents us from racing hardclock_softtick() and
 * a critical section is sufficient to interlock sc->curticks and protect
 * us from remote IPI's / list removal.
 *
 * The thread starts with the MP lock released and not in a critical
 * section.  The loop itself is MP safe while individual callbacks
 * may or may not be, so we obtain or release the MP lock as appropriate.
 */
static void
softclock_handler(void *arg)
{
	softclock_pcpu_t sc;
	struct callout *c;
	struct callout_tailq *bucket;
	struct callout slotimer;
	int mpsafe = 1;
	int flags;

	/*
	 * Setup pcpu slow clocks which we want to run from the callout
	 * thread.
	 */
	callout_init_mp(&slotimer);
	callout_reset(&slotimer, hz * 10, slotimer_callback, &slotimer);

	/*
	 * Run the callout thread at the same priority as other kernel
	 * threads so it can be round-robined.
	 */
	/*lwkt_setpri_self(TDPRI_SOFT_NORM);*/

	/*
	 * Loop critical section against ipi operations to this cpu.
	 */
	sc = arg;
	crit_enter();
loop:
	while (sc->softticks != (int)(sc->curticks + 1)) {
		bucket = &sc->callwheel[sc->softticks & cwheelmask];

		for (c = TAILQ_FIRST(bucket); c; c = sc->next) {
			if (c->c_time != sc->softticks) {
				sc->next = TAILQ_NEXT(c, c_links.tqe);
				continue;
			}

			flags = c->c_flags;
			if (flags & CALLOUT_MPSAFE) {
				if (mpsafe == 0) {
					mpsafe = 1;
					rel_mplock();
				}
			} else {
				/*
				 * The request might be removed while we 
				 * are waiting to get the MP lock.  If it
				 * was removed sc->next will point to the
				 * next valid request or NULL, loop up.
				 */
				if (mpsafe) {
					mpsafe = 0;
					sc->next = c;
					get_mplock();
					if (c != sc->next)
						continue;
				}
			}

			/*
			 * Queue protection only exists while we hold the
			 * critical section uninterrupted.
			 *
			 * Adjust sc->next when removing (c) from the queue,
			 * note that an IPI on this cpu may make further
			 * adjustments to sc->next.
			 */
			sc->next = TAILQ_NEXT(c, c_links.tqe);
			TAILQ_REMOVE(bucket, c, c_links.tqe);

			/*
			 * Once CALLOUT_PENDING is cleared, sc->running
			 * protects the callout structure's existance but
			 * only until we call c_func().  A callout_stop()
			 * or callout_reset() issued from within c_func()
			 * will not block.  The callout can also be kfree()d
			 * by c_func().
			 *
			 * We set EXECUTED before calling c_func() so a
			 * callout_stop() issued from within c_func() returns
			 * the correct status.
			 */

			if ((flags & (CALLOUT_AUTOLOCK | CALLOUT_ACTIVE)) ==
			    (CALLOUT_AUTOLOCK | CALLOUT_ACTIVE)) {
				void (*c_func)(void *);
				void *c_arg;
				struct lock *c_lk;
				int error;

				/*
				 * NOTE: sc->running must be set prior to
				 *	 CALLOUT_PENDING being cleared to
				 *	 avoid missed CANCELs and *_stop()
				 *	 races.
				 */
				sc->running = (intptr_t)c;
				c_func = c->c_func;
				c_arg = c->c_arg;
				c_lk = c->c_lk;
				c->c_func = NULL;
				KKASSERT(c->c_flags & CALLOUT_DID_INIT);
				flags = callout_unpend_disarm(c);
				error = lockmgr(c_lk, LK_EXCLUSIVE |
						      LK_CANCELABLE);
				if (error == 0) {
					atomic_set_int(&c->c_flags,
						       CALLOUT_EXECUTED);
					crit_exit();
					c_func(c_arg);
					crit_enter();
					lockmgr(c_lk, LK_RELEASE);
				}
			} else if (flags & CALLOUT_ACTIVE) {
				void (*c_func)(void *);
				void *c_arg;

				sc->running = (intptr_t)c;
				c_func = c->c_func;
				c_arg = c->c_arg;
				c->c_func = NULL;
				KKASSERT(c->c_flags & CALLOUT_DID_INIT);
				flags = callout_unpend_disarm(c);
				atomic_set_int(&c->c_flags, CALLOUT_EXECUTED);
				crit_exit();
				c_func(c_arg);
				crit_enter();
			} else {
				flags = callout_unpend_disarm(c);
			}

			/*
			 * Read and clear sc->running.  If bit 0 was set,
			 * a callout_stop() is likely blocked waiting for
			 * the callback to complete.
			 *
			 * The sigclear above also cleared CALLOUT_WAITING
			 * and returns the contents of flags prior to clearing
			 * any bits.
			 *
			 * Interlock wakeup any _stop's waiting on us.  Note
			 * that once c_func() was called, the callout
			 * structure (c) pointer may no longer be valid.  It
			 * can only be used for the wakeup.
			 */
			if ((atomic_readandclear_ptr(&sc->running) & 1) ||
			    (flags & CALLOUT_WAITING)) {
				wakeup(c);
			}
			/* NOTE: list may have changed */
		}
		++sc->softticks;
	}

	/*
	 * Don't leave us holding the MP lock when we deschedule ourselves.
	 */
	if (mpsafe == 0) {
		mpsafe = 1;
		rel_mplock();
	}
	sc->isrunning = 0;
	lwkt_deschedule_self(&sc->thread);	/* == curthread */
	lwkt_switch();
	goto loop;
	/* NOT REACHED */
}

/*
 * A very slow system cleanup timer (10 second interval),
 * per-cpu.
 */
void
slotimer_callback(void *arg)
{
	struct callout *c = arg;

	slab_cleanup();
	callout_reset(c, hz * 10, slotimer_callback, c);
}

/*
 * Start or restart a timeout.  Installs the callout structure on the
 * callwheel.  Callers may legally pass any value, even if 0 or negative,
 * but since the sc->curticks index may have already been processed a
 * minimum timeout of 1 tick will be enforced.
 *
 * This function will block if the callout is currently queued to a different
 * cpu or the callback is currently running in another thread.
 */
void
callout_reset(struct callout *c, int to_ticks, void (*ftn)(void *), void *arg)
{
	softclock_pcpu_t sc;
	globaldata_t gd;

#ifdef INVARIANTS
        if ((c->c_flags & CALLOUT_DID_INIT) == 0) {
		callout_init(c);
		kprintf(
		    "callout_reset(%p) from %p: callout was not initialized\n",
		    c, ((int **)&c)[-1]);
		print_backtrace(-1);
	}
#endif
	gd = mycpu;
	sc = &softclock_pcpu_ary[gd->gd_cpuid];
	crit_enter_gd(gd);

	/*
	 * Our cpu must gain ownership of the callout and cancel anything
	 * still running, which is complex.  The easiest way to do it is to
	 * issue a callout_stop().
	 *
	 * Clearing bits on flags is a way to guarantee they are not set,
	 * as the cmpset atomic op will fail otherwise.
	 *
	 */
	for (;;) {
		int flags;
		int nflags;

		callout_stop_sync(c);
		flags = c->c_flags & ~CALLOUT_PENDING;
		nflags = (flags & ~(CALLOUT_CPU_MASK |
				    CALLOUT_EXECUTED)) |
			 CALLOUT_CPU_TO_FLAGS(gd->gd_cpuid) |
			 CALLOUT_ARMED |
			 CALLOUT_PENDING |
			 CALLOUT_ACTIVE;
		if (atomic_cmpset_int(&c->c_flags, flags, nflags))
			break;
	}


	if (to_ticks <= 0)
		to_ticks = 1;

	c->c_arg = arg;
	c->c_func = ftn;
	c->c_time = sc->curticks + to_ticks;

	TAILQ_INSERT_TAIL(&sc->callwheel[c->c_time & cwheelmask],
			  c, c_links.tqe);
	crit_exit_gd(gd);
}

/*
 * Setup a callout to run on the specified cpu.  Should generally be used
 * to run a callout on a specific cpu which does not nominally change.
 */
void
callout_reset_bycpu(struct callout *c, int to_ticks, void (*ftn)(void *),
		    void *arg, int cpuid)
{
	globaldata_t gd;
	globaldata_t tgd;

#ifdef INVARIANTS
        if ((c->c_flags & CALLOUT_DID_INIT) == 0) {
		callout_init(c);
		kprintf(
		    "callout_reset(%p) from %p: callout was not initialized\n",
		    c, ((int **)&c)[-1]);
		print_backtrace(-1);
	}
#endif
	gd = mycpu;
	crit_enter_gd(gd);

	tgd = globaldata_find(cpuid);

	/*
	 * Our cpu must temporarily gain ownership of the callout and cancel
	 * anything still running, which is complex.  The easiest way to do
	 * it is to issue a callout_stop().
	 *
	 * Clearing bits on flags (vs nflags) is a way to guarantee they were
	 * not previously set, by forcing the atomic op to fail.
	 */
	for (;;) {
		int flags;
		int nflags;

		callout_stop_sync(c);
		flags = c->c_flags & ~CALLOUT_PENDING;
		nflags = (flags & ~(CALLOUT_CPU_MASK |
				    CALLOUT_EXECUTED)) |
			 CALLOUT_CPU_TO_FLAGS(tgd->gd_cpuid) |
			 CALLOUT_ARMED |
			 CALLOUT_ACTIVE;
		nflags = nflags + 1;		/* bump IPI count */
		if (atomic_cmpset_int(&c->c_flags, flags, nflags))
			break;
		cpu_pause();
	}

	/*
	 * Even though we are not the cpu that now owns the callout, our
	 * bumping of the IPI count (and in a situation where the callout is
	 * not queued to the callwheel) will prevent anyone else from
	 * depending on or acting on the contents of the callout structure.
	 */
	if (to_ticks <= 0)
		to_ticks = 1;

	c->c_arg = arg;
	c->c_func = ftn;
	c->c_load = to_ticks;	/* IPI will add curticks */

	lwkt_send_ipiq(tgd, callout_reset_ipi, c);
	crit_exit_gd(gd);
}

/*
 * Remote IPI for callout_reset_bycpu().  The operation is performed only
 * on the 1->0 transition of the counter, otherwise there are callout_stop()s
 * pending after us.
 *
 * The IPI counter and PENDING flags must be set atomically with the
 * 1->0 transition.  The ACTIVE flag was set prior to the ipi being
 * sent and we do not want to race a caller on the original cpu trying
 * to deactivate() the flag concurrent with our installation of the
 * callout.
 */
static void
callout_reset_ipi(void *arg)
{
	struct callout *c = arg;
	globaldata_t gd = mycpu;
	globaldata_t tgd;
	int flags;
	int nflags;

	for (;;) {
		flags = c->c_flags;
		cpu_ccfence();
		KKASSERT((flags & CALLOUT_IPI_MASK) > 0);

		/*
		 * We should already be armed for our cpu, if armed to another
		 * cpu, chain the IPI.  If for some reason we are not armed,
		 * we can arm ourselves.
		 */
		if (flags & CALLOUT_ARMED) {
			if (CALLOUT_FLAGS_TO_CPU(flags) != gd->gd_cpuid) {
				tgd = globaldata_find(
						CALLOUT_FLAGS_TO_CPU(flags));
				lwkt_send_ipiq(tgd, callout_reset_ipi, c);
				return;
			}
			nflags = (flags & ~CALLOUT_EXECUTED);
		} else {
			nflags = (flags & ~(CALLOUT_CPU_MASK |
					    CALLOUT_EXECUTED)) |
				 CALLOUT_ARMED |
				 CALLOUT_CPU_TO_FLAGS(gd->gd_cpuid);
		}

		/*
		 * Decrement the IPI count, retain and clear the WAITING
		 * status, clear EXECUTED.
		 *
		 * NOTE: It is possible for the callout to already have been
		 *	 marked pending due to SMP races.
		 */
		nflags = nflags - 1;
		if ((flags & CALLOUT_IPI_MASK) == 1) {
			nflags &= ~(CALLOUT_WAITING | CALLOUT_EXECUTED);
			nflags |= CALLOUT_PENDING;
		}

		if (atomic_cmpset_int(&c->c_flags, flags, nflags)) {
			/*
			 * Only install the callout on the 1->0 transition
			 * of the IPI count, and only if PENDING was not
			 * already set.  The latter situation should never
			 * occur but we check anyway.
			 */
			if ((flags & (CALLOUT_PENDING|CALLOUT_IPI_MASK)) == 1) {
				softclock_pcpu_t sc;

				sc = &softclock_pcpu_ary[gd->gd_cpuid];
				c->c_time = sc->curticks + c->c_load;
				TAILQ_INSERT_TAIL(
					&sc->callwheel[c->c_time & cwheelmask],
					c, c_links.tqe);
			}
			break;
		}
		/* retry */
		cpu_pause();
	}

	/*
	 * Issue wakeup if requested.
	 */
	if (flags & CALLOUT_WAITING)
		wakeup(c);
}

/*
 * Stop a running timer and ensure that any running callout completes before
 * returning.  If the timer is running on another cpu this function may block
 * to interlock against the callout.  If the callout is currently executing
 * or blocked in another thread this function may also block to interlock
 * against the callout.
 *
 * The caller must be careful to avoid deadlocks, either by using
 * callout_init_lk() (which uses the lockmgr lock cancelation feature),
 * by using tokens and dealing with breaks in the serialization, or using
 * the lockmgr lock cancelation feature yourself in the callout callback
 * function.
 *
 * callout_stop() returns non-zero if the callout was pending.
 */
static int
_callout_stop(struct callout *c, int issync)
{
	globaldata_t gd = mycpu;
	globaldata_t tgd;
	softclock_pcpu_t sc;
	int flags;
	int nflags;
	int rc;
	int cpuid;

#ifdef INVARIANTS
        if ((c->c_flags & CALLOUT_DID_INIT) == 0) {
		callout_init(c);
		kprintf(
		    "callout_stop(%p) from %p: callout was not initialized\n",
		    c, ((int **)&c)[-1]);
		print_backtrace(-1);
	}
#endif
	crit_enter_gd(gd);

	/*
	 * Fast path operations:
	 *
	 * If ARMED and owned by our cpu, or not ARMED, and other simple
	 * conditions are met, we can just clear ACTIVE and EXECUTED
	 * and we are done.
	 */
	for (;;) {
		flags = c->c_flags;
		cpu_ccfence();

		cpuid = CALLOUT_FLAGS_TO_CPU(flags);

		/*
		 * Can't handle an armed callout in the fast path if it is
		 * not on the current cpu.  We must atomically increment the
		 * IPI count for the IPI we intend to send and break out of
		 * the fast path to enter the slow path.
		 */
		if (flags & CALLOUT_ARMED) {
			if (gd->gd_cpuid != cpuid) {
				nflags = flags + 1;
				if (atomic_cmpset_int(&c->c_flags,
						      flags, nflags)) {
					/* break to slow path */
					break;
				}
				continue;	/* retry */
			}
		} else {
			cpuid = gd->gd_cpuid;
			KKASSERT((flags & CALLOUT_IPI_MASK) == 0);
		}

		/*
		 * Process pending IPIs and retry (only if not called from
		 * an IPI).
		 */
		if (flags & CALLOUT_IPI_MASK) {
			lwkt_process_ipiq();
			continue;	/* retry */
		}

		/*
		 * Transition to the stopped state, recover the EXECUTED
		 * status.  If pending we cannot clear ARMED until after
		 * we have removed (c) from the callwheel.
		 *
		 * NOTE: The callout might already not be armed but in this
		 *	 case it should also not be pending.
		 */
		nflags = flags & ~(CALLOUT_ACTIVE |
				   CALLOUT_EXECUTED |
				   CALLOUT_WAITING |
				   CALLOUT_PENDING);
		if ((flags & CALLOUT_PENDING) == 0)
			nflags &= ~CALLOUT_ARMED;
		if (atomic_cmpset_int(&c->c_flags, flags, nflags)) {
			if (flags & CALLOUT_PENDING) {
				sc = &softclock_pcpu_ary[gd->gd_cpuid];
				if (sc->next == c)
					sc->next = TAILQ_NEXT(c, c_links.tqe);
				TAILQ_REMOVE(
					&sc->callwheel[c->c_time & cwheelmask],
					c,
					c_links.tqe);
				c->c_func = NULL;

				/*
				 * NOTE: Can't clear ARMED until we have
				 *	 physically removed (c) from the
				 *	 callwheel.
				 *
				 * NOTE: WAITING bit race exists when doing
				 *	 unconditional bit clears.
				 */
				atomic_clear_int(&c->c_flags, CALLOUT_ARMED);
				if (c->c_flags & CALLOUT_WAITING)
					flags |= CALLOUT_WAITING;
			}

			/*
			 * ARMED has been cleared at this point and (c)
			 * might now be stale.  Only good for wakeup()s.
			 */
			if (flags & CALLOUT_WAITING)
				wakeup(c);

			goto skip_slow;
		}
		/* retry */
	}

	/*
	 * Slow path (and not called via an IPI).
	 *
	 * When ARMED to a different cpu the stop must be processed on that
	 * cpu.  Issue the IPI and wait for completion.  We have already
	 * incremented the IPI count.
	 */
	tgd = globaldata_find(cpuid);
	lwkt_send_ipiq3(tgd, callout_stop_ipi, c, issync);

	for (;;) {
		int flags;
		int nflags;

		flags = c->c_flags;
		cpu_ccfence();
		if ((flags & CALLOUT_IPI_MASK) == 0)	/* fast path */
			break;
		nflags = flags | CALLOUT_WAITING;
		tsleep_interlock(c, 0);
		if (atomic_cmpset_int(&c->c_flags, flags, nflags)) {
			tsleep(c, PINTERLOCKED, "cstp1", 0);
		}
	}

skip_slow:
	/*
	 * If (issync) we must also wait for any in-progress callbacks to
	 * complete, unless the stop is being executed from the callback
	 * itself.  The EXECUTED flag is set prior to the callback
	 * being made so our existing flags status already has it.
	 *
	 * If auto-lock mode is being used, this is where we cancel any
	 * blocked lock that is potentially preventing the target cpu
	 * from completing the callback.
	 */
	while (issync) {
		intptr_t *runp;
		intptr_t runco;

		sc = &softclock_pcpu_ary[cpuid];
		if (gd->gd_curthread == &sc->thread)	/* stop from cb */
			break;
		runp = &sc->running;
		runco = *runp;
		cpu_ccfence();
		if ((runco & ~(intptr_t)1) != (intptr_t)c)
			break;
		if (c->c_flags & CALLOUT_AUTOLOCK)
			lockmgr(c->c_lk, LK_CANCEL_BEG);
		tsleep_interlock(c, 0);
		if (atomic_cmpset_long(runp, runco, runco | 1))
			tsleep(c, PINTERLOCKED, "cstp3", 0);
		if (c->c_flags & CALLOUT_AUTOLOCK)
			lockmgr(c->c_lk, LK_CANCEL_END);
	}

	crit_exit_gd(gd);
	rc = (flags & CALLOUT_EXECUTED) != 0;

	return rc;
}

static
void
callout_stop_ipi(void *arg, int issync, struct intrframe *frame)
{
	globaldata_t gd = mycpu;
	struct callout *c = arg;
	softclock_pcpu_t sc;

	sc = &softclock_pcpu_ary[gd->gd_cpuid];

	/*
	 * Only the fast path can run in an IPI.  Chain the stop request
	 * if we are racing cpu changes.
	 */
	for (;;) {
		globaldata_t tgd;
		int flags;
		int nflags;
		int cpuid;

		flags = c->c_flags;
		cpu_ccfence();

		cpuid = CALLOUT_FLAGS_TO_CPU(flags);

		/*
		 * Can't handle an armed callout in the fast path if it is
		 * not on the current cpu.  We must atomically increment the
		 * IPI count and break out of the fast path.
		 *
		 * If called from an IPI we chain the IPI instead.
		 */
		if ((flags & CALLOUT_ARMED) && gd->gd_cpuid != cpuid) {
			tgd = globaldata_find(cpuid);
			lwkt_send_ipiq3(tgd, callout_stop_ipi, c, issync);
			break;
		}

		/*
		 * NOTE: As an IPI ourselves we cannot wait for other IPIs
		 *	 to complete, and we are being executed in-order.
		 */

		/*
		 * Transition to the stopped state, recover the EXECUTED
		 * status, decrement the IPI count.  If pending we cannot
		 * clear ARMED until after we have removed (c) from the
		 * callwheel.
		 */
		nflags = flags & ~(CALLOUT_ACTIVE | CALLOUT_PENDING);
		nflags = nflags - 1;			/* dec ipi count */
		if ((flags & CALLOUT_PENDING) == 0)
			nflags &= ~CALLOUT_ARMED;
		if ((flags & CALLOUT_IPI_MASK) == 1)
			nflags &= ~(CALLOUT_WAITING | CALLOUT_EXECUTED);

		if (atomic_cmpset_int(&c->c_flags, flags, nflags)) {
			/*
			 * Can only remove from callwheel if currently
			 * pending.
			 */
			if (flags & CALLOUT_PENDING) {
				if (sc->next == c)
					sc->next = TAILQ_NEXT(c, c_links.tqe);
				TAILQ_REMOVE(
					&sc->callwheel[c->c_time & cwheelmask],
					c,
					c_links.tqe);
				c->c_func = NULL;

				/*
				 * NOTE: Can't clear ARMED until we have
				 *	 physically removed (c) from the
				 *	 callwheel.
				 *
				 * NOTE: WAITING bit race exists when doing
				 *	 unconditional bit clears.
				 */
				atomic_clear_int(&c->c_flags, CALLOUT_ARMED);
				if (c->c_flags & CALLOUT_WAITING)
					flags |= CALLOUT_WAITING;
			}

			/*
			 * ARMED has been cleared at this point and (c)
			 * might now be stale.  Only good for wakeup()s.
			 */
			if (flags & CALLOUT_WAITING)
				wakeup(c);
			break;
		}
		/* retry */
	}
}

int
callout_stop(struct callout *c)
{
	return _callout_stop(c, 0);
}

int
callout_stop_sync(struct callout *c)
{
	return _callout_stop(c, 1);
}

void
callout_stop_async(struct callout *c)
{
	_callout_stop(c, 0);
}

void
callout_terminate(struct callout *c)
{
	_callout_stop(c, 1);
	atomic_clear_int(&c->c_flags, CALLOUT_DID_INIT);
}

/*
 * Prepare a callout structure for use by callout_reset() and/or 
 * callout_stop().
 *
 * The MP version of this routine requires that the callback
 * function installed by callout_reset() be MP safe.
 *
 * The LK version of this routine is also MPsafe and will automatically
 * acquire the specified lock for the duration of the function call,
 * and release it after the function returns.  In addition, when autolocking
 * is used, callout_stop() becomes synchronous if the caller owns the lock.
 * callout_reset(), callout_stop(), and callout_stop_sync() will block
 * normally instead of spinning when a cpu race occurs.  Lock cancelation
 * is used to avoid deadlocks against the callout ring dispatch.
 *
 * The init functions can be called from any cpu and do not have to be
 * called from the cpu that the timer will eventually run on.
 */
static __inline
void
_callout_init(struct callout *c, int flags)
{
	bzero(c, sizeof *c);
	c->c_flags = flags;
}

void
callout_init(struct callout *c)
{
	_callout_init(c, CALLOUT_DID_INIT);
}

void
callout_init_mp(struct callout *c)
{
	_callout_init(c, CALLOUT_DID_INIT | CALLOUT_MPSAFE);
}

void
callout_init_lk(struct callout *c, struct lock *lk)
{
	_callout_init(c, CALLOUT_DID_INIT | CALLOUT_MPSAFE | CALLOUT_AUTOLOCK);
	c->c_lk = lk;
}
