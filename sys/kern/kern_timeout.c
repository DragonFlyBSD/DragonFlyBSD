/*
 * Copyright (c) 2004 The DragonFly Project.  All rights reserved.
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
 *
 *	From: @(#)kern_clock.c	8.5 (Berkeley) 1/21/94
 * $FreeBSD: src/sys/kern/kern_timeout.c,v 1.59.2.1 2001/11/13 18:24:52 archie Exp $
 */
/*
 * DRAGONFLY BGL STATUS
 *
 *	All the API functions should be MP safe.
 *
 *	The callback functions will be flagged as being MP safe if the
 *	timeout structure is initialized with callout_init_mp() instead of
 *	callout_init().
 *
 *	The helper threads cannot be made preempt-capable until after we
 *	clean up all the uses of splsoftclock() and related interlocks (which
 *	require the related functions to be MP safe as well).
 */
/*
 * The callout mechanism is based on the work of Adam M. Costello and 
 * George Varghese, published in a technical report entitled "Redesigning
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

#ifndef MAX_SOFTCLOCK_STEPS
#define MAX_SOFTCLOCK_STEPS 100 /* Maximum allowed value of steps. */
#endif


struct softclock_pcpu {
	struct callout_tailq *callwheel;
	struct callout * volatile next;
	struct callout *running;/* currently running callout */
	int softticks;		/* softticks index */
	int curticks;		/* per-cpu ticks counter */
	int isrunning;
	struct thread thread;

};

typedef struct softclock_pcpu *softclock_pcpu_t;

/*
 * TODO:
 *	allocate more timeout table slots when table overflows.
 */
static MALLOC_DEFINE(M_CALLOUT, "callout", "callout structures");
static int callwheelsize;
static int callwheelmask;
static struct softclock_pcpu softclock_pcpu_ary[MAXCPU];

static void softclock_handler(void *arg);
static void slotimer_callback(void *arg);

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

	callwheelsize = 1;
	while (callwheelsize < target)
		callwheelsize <<= 1;
	callwheelmask = callwheelsize - 1;

	/*
	 * Initialize per-cpu data structures.
	 */
	for (cpu = 0; cpu < ncpus; ++cpu) {
		softclock_pcpu_t sc;

		sc = &softclock_pcpu_ary[cpu];

		sc->callwheel = kmalloc(sizeof(*sc->callwheel) * callwheelsize,
					M_CALLOUT, M_WAITOK|M_ZERO);
		for (i = 0; i < callwheelsize; ++i)
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
		 * in sync, only wakeup the thread if there is something to
		 * do.
		 */
		if (TAILQ_FIRST(&sc->callwheel[sc->softticks & callwheelmask]))
		{
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
	void (*c_func)(void *);
	void *c_arg;
	int mpsafe = 1;

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

	sc = arg;
	crit_enter();
loop:
	while (sc->softticks != (int)(sc->curticks + 1)) {
		bucket = &sc->callwheel[sc->softticks & callwheelmask];

		for (c = TAILQ_FIRST(bucket); c; c = sc->next) {
			if (c->c_time != sc->softticks) {
				sc->next = TAILQ_NEXT(c, c_links.tqe);
				continue;
			}
			if (c->c_flags & CALLOUT_MPSAFE) {
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
			sc->next = TAILQ_NEXT(c, c_links.tqe);
			TAILQ_REMOVE(bucket, c, c_links.tqe);

			sc->running = c;
			c_func = c->c_func;
			c_arg = c->c_arg;
			c->c_func = NULL;
			KKASSERT(c->c_flags & CALLOUT_DID_INIT);
			c->c_flags &= ~CALLOUT_PENDING;
			crit_exit();
			c_func(c_arg);
			crit_enter();
			sc->running = NULL;
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
 * New interface; clients allocate their own callout structures.
 *
 * callout_reset() - establish or change a timeout
 * callout_stop() - disestablish a timeout
 * callout_init() - initialize a callout structure so that it can
 *			safely be passed to callout_reset() and callout_stop()
 * callout_init_mp() - same but any installed functions must be MP safe.
 *
 * <sys/callout.h> defines three convenience macros:
 *
 * callout_active() - returns truth if callout has not been serviced
 * callout_pending() - returns truth if callout is still waiting for timeout
 * callout_deactivate() - marks the callout as having been serviced
 */

/*
 * Start or restart a timeout.  Install the callout structure in the 
 * callwheel.  Callers may legally pass any value, even if 0 or negative,
 * but since the sc->curticks index may have already been processed a
 * minimum timeout of 1 tick will be enforced.
 *
 * The callout is installed on and will be processed on the current cpu's
 * callout wheel.
 *
 * WARNING! This function may be called from any cpu but the caller must
 * serialize callout_stop() and callout_reset() calls on the passed
 * structure regardless of cpu.
 */
void
callout_reset(struct callout *c, int to_ticks, void (*ftn)(void *), 
		void *arg)
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

	if (c->c_flags & CALLOUT_ACTIVE)
		callout_stop(c);

	if (to_ticks <= 0)
		to_ticks = 1;

	c->c_arg = arg;
	c->c_flags |= (CALLOUT_ACTIVE | CALLOUT_PENDING);
	c->c_func = ftn;
	c->c_time = sc->curticks + to_ticks;
	c->c_gd = gd;

	TAILQ_INSERT_TAIL(&sc->callwheel[c->c_time & callwheelmask], 
			  c, c_links.tqe);
	crit_exit_gd(gd);
}

struct callout_remote_arg {
	struct callout	*c;
	void		(*ftn)(void *);
	void		*arg;
	int		to_ticks;
};

static void
callout_reset_ipi(void *arg)
{
	struct callout_remote_arg *rmt = arg;

	callout_reset(rmt->c, rmt->to_ticks, rmt->ftn, rmt->arg);
}

void
callout_reset_bycpu(struct callout *c, int to_ticks, void (*ftn)(void *),
    void *arg, int cpuid)
{
	KASSERT(cpuid >= 0 && cpuid < ncpus, ("invalid cpuid %d", cpuid));

	if (cpuid == mycpuid) {
		callout_reset(c, to_ticks, ftn, arg);
	} else {
		struct globaldata *target_gd;
		struct callout_remote_arg rmt;
		int seq;

		rmt.c = c;
		rmt.ftn = ftn;
		rmt.arg = arg;
		rmt.to_ticks = to_ticks;

		target_gd = globaldata_find(cpuid);

		seq = lwkt_send_ipiq(target_gd, callout_reset_ipi, &rmt);
		lwkt_wait_ipiq(target_gd, seq);
	}
}

/*
 * Stop a running timer.  WARNING!  If called on a cpu other then the one
 * the callout was started on this function will liveloop on its IPI to
 * the target cpu to process the request.  It is possible for the callout
 * to execute in that case.
 *
 * WARNING! This function may be called from any cpu but the caller must
 * serialize callout_stop() and callout_reset() calls on the passed
 * structure regardless of cpu.
 *
 * WARNING! This routine may be called from an IPI
 *
 * WARNING! This function can return while it's c_func is still running
 *	    in the callout thread, a secondary check may be needed.
 *	    Use callout_stop_sync() to wait for any callout function to
 *	    complete before returning, being sure that no deadlock is
 *	    possible if you do.
 */
int
callout_stop(struct callout *c)
{
	globaldata_t gd = mycpu;
	globaldata_t tgd;
	softclock_pcpu_t sc;

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
	 * Don't attempt to delete a callout that's not on the queue.  The
	 * callout may not have a cpu assigned to it.  Callers do not have
	 * to be on the issuing cpu but must still serialize access to the
	 * callout structure.
	 *
	 * We are not cpu-localized here and cannot safely modify the
	 * flags field in the callout structure.  Note that most of the
	 * time CALLOUT_ACTIVE will be 0 if CALLOUT_PENDING is also 0.
	 *
	 * If we race another cpu's dispatch of this callout it is possible
	 * for CALLOUT_ACTIVE to be set with CALLOUT_PENDING unset.  This
	 * will cause us to fall through and synchronize with the other
	 * cpu.
	 */
	if ((c->c_flags & CALLOUT_PENDING) == 0) {
		if ((c->c_flags & CALLOUT_ACTIVE) == 0) {
			crit_exit_gd(gd);
			return (0);
		}
		if (c->c_gd == NULL || c->c_gd == gd) {
			c->c_flags &= ~CALLOUT_ACTIVE;
			crit_exit_gd(gd);
			return (0);
		}
	}
	if ((tgd = c->c_gd) != gd) {
		/*
		 * If the callout is owned by a different CPU we have to
		 * execute the function synchronously on the target cpu.
		 */
		int seq;

		cpu_ccfence();	/* don't let tgd alias c_gd */
		seq = lwkt_send_ipiq(tgd, (void *)callout_stop, c);
		lwkt_wait_ipiq(tgd, seq);
	} else {
		/*
		 * If the callout is owned by the same CPU we can
		 * process it directly, but if we are racing our helper
		 * thread (sc->next), we have to adjust sc->next.  The
		 * race is interlocked by a critical section.
		 */
		sc = &softclock_pcpu_ary[gd->gd_cpuid];

		c->c_flags &= ~(CALLOUT_ACTIVE | CALLOUT_PENDING);
		if (sc->next == c)
			sc->next = TAILQ_NEXT(c, c_links.tqe);

		TAILQ_REMOVE(&sc->callwheel[c->c_time & callwheelmask], 
				c, c_links.tqe);
		c->c_func = NULL;
	}
	crit_exit_gd(gd);
	return (1);
}

/*
 * Issue a callout_stop() and ensure that any callout race completes
 * before returning.  Does NOT de-initialized the callout.
 */
void
callout_stop_sync(struct callout *c)
{
	softclock_pcpu_t sc;

	while (c->c_flags & CALLOUT_DID_INIT) {
		callout_stop(c);
		if (c->c_gd) {
			sc = &softclock_pcpu_ary[c->c_gd->gd_cpuid];
			if (sc->running == c) {
				while (sc->running == c)
					tsleep(&sc->running, 0, "crace", 1);
			}
		}
		if ((c->c_flags & (CALLOUT_PENDING | CALLOUT_ACTIVE)) == 0)
			break;
		kprintf("Warning: %s: callout race\n", curthread->td_comm);
	}
}

/*
 * Terminate a callout
 *
 * This function will stop any pending callout and also block while the
 * callout's function is running.  It should only be used in cases where
 * no deadlock is possible (due to the callout function acquiring locks
 * that the current caller of callout_terminate() already holds), when
 * the caller is ready to destroy the callout structure.
 *
 * This function clears the CALLOUT_DID_INIT flag.
 *
 * lwkt_token locks are ok.
 */
void
callout_terminate(struct callout *c)
{
	softclock_pcpu_t sc;

	if (c->c_flags & CALLOUT_DID_INIT) {
		callout_stop(c);
		sc = &softclock_pcpu_ary[c->c_gd->gd_cpuid];
		if (sc->running == c) {
			while (sc->running == c)
				tsleep(&sc->running, 0, "crace", 1);
		}
		KKASSERT((c->c_flags & (CALLOUT_PENDING|CALLOUT_ACTIVE)) == 0);
		c->c_flags &= ~CALLOUT_DID_INIT;
	}
}

/*
 * Prepare a callout structure for use by callout_reset() and/or 
 * callout_stop().  The MP version of this routine requires that the callback
 * function installed by callout_reset() be MP safe.
 *
 * The init functions can be called from any cpu and do not have to be
 * called from the cpu that the timer will eventually run on.
 */
void
callout_init(struct callout *c)
{
	bzero(c, sizeof *c);
	c->c_flags = CALLOUT_DID_INIT;
}

void
callout_init_mp(struct callout *c)
{
	callout_init(c);
	c->c_flags |= CALLOUT_MPSAFE;
}
