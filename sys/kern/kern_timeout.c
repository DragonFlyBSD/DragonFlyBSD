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
 * The per-cpu augmentation was done by Matthew Dillon.  This file has
 * essentially been rewritten pretty much from scratch by Matt.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/callout.h>
#include <sys/kernel.h>
#include <sys/interrupt.h>
#include <sys/thread.h>

#include <sys/thread2.h>
#include <sys/mplock2.h>

#include <vm/vm_extern.h>

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
static softclock_pcpu_t softclock_pcpu_ary[MAXCPU];

static void softclock_handler(void *arg);
static void slotimer_callback(void *arg);
static void callout_reset_ipi(void *arg);
static void callout_stop_ipi(void *arg, int issync, struct intrframe *frame);

static __inline int
callout_setclear(struct callout *c, int sflags, int cflags)
{
	int flags;
	int nflags;

	for (;;) {
		flags = c->c_flags;
		cpu_ccfence();
		nflags = (flags | sflags) & ~cflags;
		if (atomic_cmpset_int(&c->c_flags, flags, nflags))
			break;
	}
	return flags;
}

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
		int wheel_sz;

		sc = (void *)kmem_alloc3(&kernel_map, sizeof(*sc),
					 VM_SUBSYS_GD, KM_CPU(cpu));
		memset(sc, 0, sizeof(*sc));
		softclock_pcpu_ary[cpu] = sc;

		wheel_sz = sizeof(*sc->callwheel) * cwheelsize;
		sc->callwheel = (void *)kmem_alloc3(&kernel_map, wheel_sz,
						    VM_SUBSYS_GD, KM_CPU(cpu));
		memset(sc->callwheel, 0, wheel_sz);
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
		lwkt_create(softclock_handler, sc, NULL, &sc->thread,
			    TDF_NOSTART | TDF_INTTHREAD,
			    cpu, "softclock %d", cpu);
	}
}

/*
 * Must occur after ncpus has been initialized.
 */
SYSINIT(softclock_setup, SI_BOOT2_SOFTCLOCK, SI_ORDER_SECOND,
	swi_softclock_setup, NULL);

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

	sc = softclock_pcpu_ary[gd->gd_cpuid];
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
			void (*c_func)(void *);
			void *c_arg;
			struct lock *c_lk;
			int error;

			if (c->c_time != sc->softticks) {
				sc->next = TAILQ_NEXT(c, c_links.tqe);
				continue;
			}

			/*
			 * Synchronize with mpsafe requirements
			 */
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

			KASSERT((c->c_flags & CALLOUT_DID_INIT) &&
				(c->c_flags & CALLOUT_PENDING) &&
				CALLOUT_FLAGS_TO_CPU(c->c_flags) ==
				mycpu->gd_cpuid,
				("callout %p: bad flags %08x", c, c->c_flags));

			/*
			 * Once CALLOUT_PENDING is cleared only the IPI_MASK
			 * prevents the callout from being moved to another
			 * cpu.  However, callout_stop() will also check
			 * sc->running on the assigned cpu if CALLOUT_EXECUTED
			 * is set.  CALLOUT_EXECUTE implies a callback
			 * interlock is needed when cross-cpu.
			 */
			sc->running = (intptr_t)c;
			c_func = c->c_func;
			c_arg = c->c_arg;
			c_lk = c->c_lk;
			c->c_func = NULL;

			if ((flags & (CALLOUT_AUTOLOCK | CALLOUT_ACTIVE)) ==
			    (CALLOUT_AUTOLOCK | CALLOUT_ACTIVE)) {
				error = lockmgr(c_lk, LK_EXCLUSIVE |
						      LK_CANCELABLE);
				if (error == 0) {
					flags = callout_setclear(c,
							CALLOUT_EXECUTED,
							CALLOUT_PENDING |
							CALLOUT_WAITING);
					crit_exit();
					c_func(c_arg);
					crit_enter();
					lockmgr(c_lk, LK_RELEASE);
				} else {
					flags = callout_setclear(c,
							0,
							CALLOUT_PENDING);
				}
			} else if (flags & CALLOUT_ACTIVE) {
				flags = callout_setclear(c,
						CALLOUT_EXECUTED,
						CALLOUT_PENDING |
						CALLOUT_WAITING);
				crit_exit();
				c_func(c_arg);
				crit_enter();
			} else {
				flags = callout_setclear(c,
						0,
						CALLOUT_PENDING |
						CALLOUT_WAITING);
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
 * callwheel of the current cpu.  Callers may legally pass any value, even
 * if 0 or negative, but since the sc->curticks index may have already
 * been processed a minimum timeout of 1 tick will be enforced.
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
	sc = softclock_pcpu_ary[gd->gd_cpuid];
	crit_enter_gd(gd);

	/*
	 * Our cpu must gain ownership of the callout and cancel anything
	 * still running, which is complex.  The easiest way to do it is to
	 * issue a callout_stop_sync().  callout_stop_sync() will also
	 * handle CALLOUT_EXECUTED (dispatch waiting), and clear it.
	 *
	 * WARNING: callout_stop_sync()'s return state can race other
	 *	    callout_*() calls due to blocking, so we must re-check.
	 */
	for (;;) {
		int flags;
		int nflags;

		if (c->c_flags & (CALLOUT_ARMED_MASK | CALLOUT_EXECUTED))
			callout_stop_sync(c);
		flags = c->c_flags & ~(CALLOUT_ARMED_MASK | CALLOUT_EXECUTED);
		nflags = (flags & ~CALLOUT_CPU_MASK) |
			 CALLOUT_CPU_TO_FLAGS(gd->gd_cpuid) |
			 CALLOUT_PENDING |
			 CALLOUT_ACTIVE;
		if (atomic_cmpset_int(&c->c_flags, flags, nflags))
			break;
		cpu_pause();
	}

	/*
	 * With the critical section held and PENDING set we now 'own' the
	 * callout.
	 */
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
 * to run a callout on a specific cpu which does not nominally change.  This
 * callout_reset() will be issued asynchronously via an IPI.
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
	 * This code is similar to the code in callout_reset() but we assign
	 * the callout to the target cpu.  We cannot set PENDING here since
	 * we cannot atomically add the callout to the target cpu's queue.
	 * However, incrementing the IPI count has the effect of locking
	 * the cpu assignment.
	 *
	 * WARNING: callout_stop_sync()'s return state can race other
	 *	    callout_*() calls due to blocking, so we must re-check.
	 */
	for (;;) {
		int flags;
		int nflags;

		if (c->c_flags & (CALLOUT_ARMED_MASK | CALLOUT_EXECUTED))
			callout_stop_sync(c);
		flags = c->c_flags & ~(CALLOUT_ARMED_MASK | CALLOUT_EXECUTED);
		nflags = (flags & ~(CALLOUT_CPU_MASK |
				    CALLOUT_EXECUTED)) |
			 CALLOUT_CPU_TO_FLAGS(tgd->gd_cpuid) |
			 CALLOUT_ACTIVE;
		nflags = nflags + 1;		/* bump IPI count */
		if (atomic_cmpset_int(&c->c_flags, flags, nflags))
			break;
		cpu_pause();
	}

	/*
	 * Since we control our +1 in the IPI count, the target cpu cannot
	 * now change until our IPI is processed.
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
 * Remote IPI for callout_reset_bycpu().  The cpu assignment cannot be
 * ripped out from under us due to the count in IPI_MASK, but it is possible
 * that other IPIs executed so we must deal with other flags that might
 * have been set or cleared.
 */
static void
callout_reset_ipi(void *arg)
{
	struct callout *c = arg;
	globaldata_t gd = mycpu;
	softclock_pcpu_t sc;
	int flags;
	int nflags;

	sc = softclock_pcpu_ary[gd->gd_cpuid];

	for (;;) {
		flags = c->c_flags;
		cpu_ccfence();
		KKASSERT((flags & CALLOUT_IPI_MASK) > 0 &&
			 CALLOUT_FLAGS_TO_CPU(flags) == gd->gd_cpuid);

		nflags = (flags - 1) & ~(CALLOUT_EXECUTED | CALLOUT_WAITING);
		nflags |= CALLOUT_PENDING;

		/*
		 * Put us on the queue
		 */
		if (atomic_cmpset_int(&c->c_flags, flags, nflags)) {
			if (flags & CALLOUT_PENDING) {
				if (sc->next == c)
					sc->next = TAILQ_NEXT(c, c_links.tqe);
				TAILQ_REMOVE(
					&sc->callwheel[c->c_time & cwheelmask],
					c,
					c_links.tqe);
			}
			c->c_time = sc->curticks + c->c_load;
			TAILQ_INSERT_TAIL(
				&sc->callwheel[c->c_time & cwheelmask],
				c, c_links.tqe);
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

retry:
	/*
	 * Adjust flags for the required operation.  If the callout is
	 * armed on another cpu we break out into the remote-cpu code which
	 * will issue an IPI.  If it is not armed we are trivially done,
	 * but may still need to test EXECUTED.
	 */
	for (;;) {
		flags = c->c_flags;
		cpu_ccfence();

		cpuid = CALLOUT_FLAGS_TO_CPU(flags);

		/*
		 * Armed on remote cpu (break to remote-cpu code)
		 */
		if ((flags & CALLOUT_ARMED_MASK) && gd->gd_cpuid != cpuid) {
			nflags = flags + 1;
			if (atomic_cmpset_int(&c->c_flags, flags, nflags)) {
				/*
				 * BREAK TO REMOTE-CPU CODE HERE
				 */
				break;
			}
			cpu_pause();
			continue;
		}

		/*
		 * Armed or armable on current cpu
		 */
		if (flags & CALLOUT_IPI_MASK) {
			lwkt_process_ipiq();
			cpu_pause();
			continue;	/* retry */
		}

		/*
		 * If PENDING is set we can remove the callout from our
		 * queue and also use the side effect that the bit causes
		 * the callout to be locked to our cpu.
		 */
		if (flags & CALLOUT_PENDING) {
			sc = softclock_pcpu_ary[gd->gd_cpuid];
			if (sc->next == c)
				sc->next = TAILQ_NEXT(c, c_links.tqe);
			TAILQ_REMOVE(
				&sc->callwheel[c->c_time & cwheelmask],
				c,
				c_links.tqe);
			c->c_func = NULL;

			for (;;) {
				flags = c->c_flags;
				cpu_ccfence();
				nflags = flags & ~(CALLOUT_ACTIVE |
						   CALLOUT_EXECUTED |
						   CALLOUT_WAITING |
						   CALLOUT_PENDING);
				if (atomic_cmpset_int(&c->c_flags,
						      flags, nflags)) {
					goto skip_slow;
				}
				cpu_pause();
			}
			/* NOT REACHED */
		}

		/*
		 * If PENDING was not set the callout might not be locked
		 * to this cpu.
		 */
		nflags = flags & ~(CALLOUT_ACTIVE |
				   CALLOUT_EXECUTED |
				   CALLOUT_WAITING |
				   CALLOUT_PENDING);
		if (atomic_cmpset_int(&c->c_flags, flags, nflags)) {
			goto skip_slow;
		}
		cpu_pause();
		/* retry */
	}

	/*
	 * Remote cpu path.  We incremented the IPI_MASK count so the callout
	 * is now locked to the remote cpu and we can safely send an IPI
	 * to it.
	 *
	 * Once sent, wait for all IPIs to be processed.  If PENDING remains
	 * set after all IPIs have processed we raced a callout or
	 * callout_reset and must retry.  Callers expect the callout to
	 * be completely stopped upon return, so make sure it is.
	 */
	tgd = globaldata_find(cpuid);
	lwkt_send_ipiq3(tgd, callout_stop_ipi, c, issync);

	for (;;) {
		flags = c->c_flags;
		cpu_ccfence();

		if ((flags & CALLOUT_IPI_MASK) == 0)
			break;

		nflags = flags | CALLOUT_WAITING;
		tsleep_interlock(c, 0);
		if (atomic_cmpset_int(&c->c_flags, flags, nflags)) {
			tsleep(c, PINTERLOCKED, "cstp1", 0);
		}
	}
	if (flags & CALLOUT_PENDING)
		goto retry;

	/*
	 * Caller expects callout_stop_sync() to clear EXECUTED and return
	 * its previous status.
	 */
	atomic_clear_int(&c->c_flags, CALLOUT_EXECUTED);

skip_slow:
	if (flags & CALLOUT_WAITING)
		wakeup(c);

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

		sc = softclock_pcpu_ary[cpuid];
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

/*
 * IPI for stop function.  The callout is locked to the receiving cpu
 * by the IPI_MASK count.
 */
static void
callout_stop_ipi(void *arg, int issync, struct intrframe *frame)
{
	globaldata_t gd = mycpu;
	struct callout *c = arg;
	softclock_pcpu_t sc;
	int flags;
	int nflags;

	flags = c->c_flags;
	cpu_ccfence();

	KKASSERT(CALLOUT_FLAGS_TO_CPU(flags) == gd->gd_cpuid);

	/*
	 * We can handle the PENDING flag immediately.
	 */
	if (flags & CALLOUT_PENDING) {
		sc = softclock_pcpu_ary[gd->gd_cpuid];
		if (sc->next == c)
			sc->next = TAILQ_NEXT(c, c_links.tqe);
		TAILQ_REMOVE(
			&sc->callwheel[c->c_time & cwheelmask],
			c,
			c_links.tqe);
		c->c_func = NULL;
	}

	/*
	 * Transition to the stopped state and decrement the IPI count.
	 * Leave the EXECUTED bit alone (the next callout_reset() will
	 * have to deal with it).
	 */
	for (;;) {
		flags = c->c_flags;
		cpu_ccfence();
		nflags = (flags - 1) & ~(CALLOUT_ACTIVE |
					 CALLOUT_PENDING |
					 CALLOUT_WAITING);

		if (atomic_cmpset_int(&c->c_flags, flags, nflags))
			break;
		cpu_pause();
	}
	if (flags & CALLOUT_WAITING)
		wakeup(c);
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
static __inline void
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
