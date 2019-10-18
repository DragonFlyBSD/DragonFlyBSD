/*
 * Copyright (c) 2004,2014,2019 The DragonFly Project.  All rights reserved.
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
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/spinlock.h>
#include <sys/callout.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/interrupt.h>
#include <sys/thread.h>
#include <sys/sysctl.h>
#ifdef CALLOUT_TYPESTABLE
#include <sys/typestable.h>
#endif
#include <vm/vm_extern.h>
#include <machine/atomic.h>

#include <sys/spinlock2.h>
#include <sys/thread2.h>
#include <sys/mplock2.h>

TAILQ_HEAD(colist, _callout);
struct softclock_pcpu;
struct _callout_mag;

/*
 * DID_INIT	- Sanity check
 * SYNC		- Synchronous waiter, request SYNCDONE and wakeup()
 * CANCEL_RES	- Flags that a cancel/stop prevented a callback
 * STOP_RES
 * RESET	- Callout_reset request queued
 * STOP		- Callout_stop request queued
 * INPROG	- Softclock_handler thread processing in-progress on callout
 * SET		- Callout is linked to queue (if INPROG not set)
 * AUTOLOCK	- Lockmgr cancelable interlock
 * MPSAFE	- Callout is MPSAFE
 * CANCEL	- callout_cancel requested queued
 * ACTIVE	- active/inactive tracking (see documentation).  This is
 *		  *NOT* the same as whether a callout is queued or not.
 */
#define CALLOUT_DID_INIT	0x00000001	/* frontend */
#define CALLOUT_UNUSED0002	0x00000002
#define CALLOUT_UNUSED0004	0x00000004
#define CALLOUT_CANCEL_RES	0x00000008	/* frontend */
#define CALLOUT_STOP_RES	0x00000010	/* frontend */
#define CALLOUT_RESET		0x00000020	/* backend */
#define CALLOUT_STOP		0x00000040	/* backend */
#define CALLOUT_INPROG		0x00000080	/* backend */
#define CALLOUT_SET		0x00000100	/* backend */
#define CALLOUT_AUTOLOCK	0x00000200	/* both */
#define CALLOUT_MPSAFE		0x00000400	/* both */
#define CALLOUT_CANCEL		0x00000800	/* backend */
#define CALLOUT_ACTIVE		0x00001000	/* frontend */

struct wheel {
	struct spinlock spin;
	struct colist	list;
};

struct softclock_pcpu {
	struct wheel	*callwheel;
	struct _callout *running;
	struct _callout * volatile next;
#ifdef CALLOUT_TYPESTABLE
	struct _callout *quick_obj;
#endif
	int		softticks;	/* softticks index */
	int		curticks;	/* per-cpu ticks counter */
	int		isrunning;
	struct thread	thread;
};

typedef struct softclock_pcpu *softclock_pcpu_t;

TAILQ_HEAD(maglist, _callout_mag);

#if 0
static int callout_debug = 0;
SYSCTL_INT(_debug, OID_AUTO, callout_debug, CTLFLAG_RW,
	   &callout_debug, 0, "");
#endif

#ifdef CALLOUT_TYPESTABLE
static MALLOC_DEFINE(M_CALLOUT, "callouts", "softclock callouts");
#endif

static int cwheelsize;
static int cwheelmask;
static softclock_pcpu_t softclock_pcpu_ary[MAXCPU];
#ifdef CALLOUT_TYPESTABLE
static struct typestable_glob callout_tsg;
#endif

static void softclock_handler(void *arg);
static void slotimer_callback(void *arg);

#ifdef CALLOUT_TYPESTABLE
/*
 * typestable callback functions.  The init function pre-initializes
 * the structure in order to allow for reuse without complete
 * reinitialization (i.e. our spinlock).
 *
 * The test function allows us to reject an allocation attempt due
 * to the object being reassociated out-of-band.
 */
static
void
_callout_typestable_init(void *obj)
{
	struct _callout *c = obj;

	spin_init(&c->spin, "_callout");
}

/*
 * Object may have been reassociated out-of-band.
 *
 * Return 1 on success with the spin-lock held, allowing reallocation.
 * Return 0 on failure with no side effects, rejecting reallocation.
 */
static
int
_callout_typestable_test(void *obj)
{
	struct _callout *c = obj;

	if (c->flags & (CALLOUT_SET | CALLOUT_INPROG))
		return 0;
	spin_lock(&c->spin);
	if (c->flags & (CALLOUT_SET | CALLOUT_INPROG)) {
		spin_unlock(&c->spin);
		return 0;
	} else {
		return 1;
	}
}

/*
 * NOTE: sc might refer to a different cpu.
 */
static __inline
void
_callout_typestable_free(softclock_pcpu_t sc, void *obj, int tentitive)
{
	if (tentitive == 0) {
		obj = atomic_swap_ptr((void *)&sc->quick_obj, obj);
		if (obj == NULL)
			return;
	}
	typestable_free(&callout_tsg, obj, tentitive);
}
#endif

/*
 * Post-processing helper for a callout executes any pending request.
 * This routine handles post-processing from the softclock thread and
 * also handles request processing from the API.
 *
 * This routine does not block in any way.
 * Caller must hold c->spin.
 *
 * INPROG  - Callback is in-processing / in-progress.
 *
 * SET     - Assigned to queue or is in-processing.  If INPROG is set,
 *	     however, the _callout is no longer in the queue.
 *
 * RESET   - New timeout was installed.
 *
 * STOP    - Stop requested.
 *
 * ACTIVE  - Set on callout_reset(), cleared by callout_stop()
 *	     or callout_cancel().  Starts out cleared.
 *
 * NOTE: Flags can be adjusted without holding c->spin, so atomic ops
 *	 must be used at all times.
 *
 * NOTE: The passed-in (sc) might refer to another cpu.
 */
static __inline
int
_callout_process_spinlocked(struct _callout *c, int fromsoftclock)
{
	struct wheel *wheel;
	int res = -1;

	/*
	 * If a callback manipulates the callout-in-progress we do
	 * a partial 'completion' of the operation so the operation
	 * can be processed synchronously and tell the softclock_handler
	 * to stop messing with it.
	 */
	if (fromsoftclock == 0 && curthread == &c->qsc->thread &&
	    c->qsc->running == c) {
		c->qsc->running = NULL;
		atomic_clear_int(&c->flags, CALLOUT_SET |
					    CALLOUT_INPROG);
	}

	/*
	 * Based on source and state
	 */
	if (fromsoftclock) {
		/*
		 * From control thread, INPROG is set, handle pending
		 * request and normal termination.
		 */
#ifdef CALLOUT_TYPESTABLE
		KASSERT(c->verifier->toc == c,
			("callout corrupt: c=%p %s/%d\n",
			 c, c->ident, c->lineno));
#else
		KASSERT(&c->verifier->toc == c,
			("callout corrupt: c=%p %s/%d\n",
			 c, c->ident, c->lineno));
#endif
		if (c->flags & CALLOUT_CANCEL) {
			/*
			 * CANCEL overrides everything.
			 *
			 * If a RESET is pending it counts as canceling a
			 * running timer.
			 */
			if (c->flags & CALLOUT_RESET)
				atomic_set_int(&c->verifier->flags,
					       CALLOUT_CANCEL_RES |
					       CALLOUT_STOP_RES);
			atomic_clear_int(&c->flags, CALLOUT_SET |
						    CALLOUT_INPROG |
						    CALLOUT_STOP |
						    CALLOUT_CANCEL |
						    CALLOUT_RESET);
			if (c->waiters)
				wakeup(c->verifier);
			res = 0;
		} else if (c->flags & CALLOUT_RESET) {
			/*
			 * RESET request pending, requeue appropriately.
			 */
			atomic_clear_int(&c->flags, CALLOUT_RESET |
						    CALLOUT_INPROG);
			atomic_set_int(&c->flags, CALLOUT_SET);
			c->qsc = c->rsc;
			c->qarg = c->rarg;
			c->qfunc = c->rfunc;
			c->qtick = c->rtick;

			/*
			 * Do not queue to current or past wheel or the
			 * callout will be lost for ages.
			 */
			wheel = &c->qsc->callwheel[c->qtick & cwheelmask];
			spin_lock(&wheel->spin);
			while (c->qtick - c->qsc->softticks <= 0) {
				c->qtick = c->qsc->softticks + 1;
				spin_unlock(&wheel->spin);
				wheel = &c->qsc->callwheel[c->qtick &
							   cwheelmask];
				spin_lock(&wheel->spin);
			}
			TAILQ_INSERT_TAIL(&wheel->list, c, entry);
			spin_unlock(&wheel->spin);
		} else {
			/*
			 * STOP request pending or normal termination.  Since
			 * this is from our control thread the callout has
			 * already been removed from the queue.
			 */
			atomic_clear_int(&c->flags, CALLOUT_SET |
						    CALLOUT_INPROG |
						    CALLOUT_STOP);
			if (c->waiters)
				wakeup(c->verifier);
			res = 1;
		}
	} else if (c->flags & CALLOUT_SET) {
		/*
		 * Process request from an API function.  qtick and ACTIVE
		 * are stable while we hold c->spin.  Checking INPROG requires
		 * holding wheel->spin.
		 *
		 * If INPROG is set the control thread must handle the request
		 * for us.
		 */
		softclock_pcpu_t sc;

		sc = c->qsc;

		wheel = &sc->callwheel[c->qtick & cwheelmask];
		spin_lock(&wheel->spin);
		if (c->flags & CALLOUT_INPROG) {
			/*
			 * API requests are deferred if a callback is in
			 * progress and will be handled after the callback
			 * returns.
			 */
		} else if (c->flags & CALLOUT_CANCEL) {
			/*
			 * CANCEL request overrides everything except INPROG
			 * (for INPROG the CANCEL is handled upon completion).
			 */
			if (sc->next == c)
				sc->next = TAILQ_NEXT(c, entry);
			TAILQ_REMOVE(&wheel->list, c, entry);
			atomic_set_int(&c->verifier->flags, CALLOUT_CANCEL_RES |
							    CALLOUT_STOP_RES);
			atomic_clear_int(&c->flags, CALLOUT_STOP |
						    CALLOUT_SET |
						    CALLOUT_CANCEL |
						    CALLOUT_RESET);
			if (c->waiters)
				wakeup(c->verifier);
			res = 0;
		} else if (c->flags & CALLOUT_RESET) {
			/*
			 * RESET request pending, requeue appropriately.
			 *
			 * (ACTIVE is governed by c->spin so we do not have
			 *  to clear it prior to releasing wheel->spin).
			 */
			if (sc->next == c)
				sc->next = TAILQ_NEXT(c, entry);
			TAILQ_REMOVE(&wheel->list, c, entry);
			spin_unlock(&wheel->spin);

			atomic_clear_int(&c->flags, CALLOUT_RESET);
			/* remain ACTIVE */
			sc = c->rsc;
			c->qsc = sc;
			c->qarg = c->rarg;
			c->qfunc = c->rfunc;
			c->qtick = c->rtick;

			/*
			 * Do not queue to current or past wheel or the
			 * callout will be lost for ages.
			 */
			wheel = &sc->callwheel[c->qtick & cwheelmask];
			spin_lock(&wheel->spin);
			while (c->qtick - sc->softticks <= 0) {
				c->qtick = sc->softticks + 1;
				spin_unlock(&wheel->spin);
				wheel = &sc->callwheel[c->qtick & cwheelmask];
				spin_lock(&wheel->spin);
			}
			TAILQ_INSERT_TAIL(&wheel->list, c, entry);
		} else if (c->flags & CALLOUT_STOP) {
			/*
			 * STOP request
			 */
			if (sc->next == c)
				sc->next = TAILQ_NEXT(c, entry);
			TAILQ_REMOVE(&wheel->list, c, entry);
			atomic_set_int(&c->verifier->flags, CALLOUT_STOP_RES);
			atomic_clear_int(&c->flags, CALLOUT_STOP |
						    CALLOUT_SET);
			if (c->waiters)
				wakeup(c->verifier);
			res = 1;
		} else {
			/*
			 * No request pending (someone else processed the
			 * request before we could)
			 */
			/* nop */
		}
		spin_unlock(&wheel->spin);
	} else {
		/*
		 * Process request from API function.  callout is not
		 * active so there's nothing for us to remove.
		 */
		KKASSERT((c->flags & CALLOUT_INPROG) == 0);
		if (c->flags & CALLOUT_CANCEL) {
			/*
			 * CANCEL request (nothing to cancel)
			 */
			if (c->flags & CALLOUT_RESET) {
				atomic_set_int(&c->verifier->flags,
					       CALLOUT_CANCEL_RES |
					       CALLOUT_STOP_RES);
			}
			atomic_clear_int(&c->flags, CALLOUT_STOP |
						    CALLOUT_CANCEL |
						    CALLOUT_RESET);
			if (c->waiters)
				wakeup(c->verifier);
			res = 0;
		} else if (c->flags & CALLOUT_RESET) {
			/*
			 * RESET request pending, queue appropriately.
			 * Do not queue to currently-processing tick.
			 */
			softclock_pcpu_t sc;

			sc = c->rsc;
			atomic_clear_int(&c->flags, CALLOUT_RESET);
			atomic_set_int(&c->flags, CALLOUT_SET);
			c->qsc = sc;
			c->qarg = c->rarg;
			c->qfunc = c->rfunc;
			c->qtick = c->rtick;

			/*
			 * Do not queue to current or past wheel or the
			 * callout will be lost for ages.
			 */
			wheel = &sc->callwheel[c->qtick & cwheelmask];
			spin_lock(&wheel->spin);
			while (c->qtick - sc->softticks <= 0) {
				c->qtick = sc->softticks + 1;
				spin_unlock(&wheel->spin);
				wheel = &sc->callwheel[c->qtick & cwheelmask];
				spin_lock(&wheel->spin);
			}
			TAILQ_INSERT_TAIL(&wheel->list, c, entry);
			spin_unlock(&wheel->spin);
		} else if (c->flags & CALLOUT_STOP) {
			/*
			 * STOP request (nothing to stop)
			 */
			atomic_clear_int(&c->flags, CALLOUT_STOP);
			if (c->waiters)
				wakeup(c->verifier);
			res = 1;
		} else {
			/*
			 * No request pending (someone else processed the
			 * request before we could)
			 */
			/* nop */
		}
	}
	return res;
}

/*
 * System init
 */
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

#ifdef CALLOUT_TYPESTABLE
	typestable_init_glob(&callout_tsg, M_CALLOUT,
			     sizeof(struct _callout),
			     _callout_typestable_test,
			     _callout_typestable_init);
#endif

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
		for (i = 0; i < cwheelsize; ++i) {
			spin_init(&sc->callwheel[i].spin, "wheel");
			TAILQ_INIT(&sc->callwheel[i].list);
		}

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
 * sc->softticks is adjusted by either this routine or our helper thread
 * depending on whether the helper thread is running or not.
 *
 * sc->curticks and sc->softticks are adjusted using atomic ops in order
 * to ensure that remote cpu callout installation does not race the thread.
 */
void
hardclock_softtick(globaldata_t gd)
{
	softclock_pcpu_t sc;
	struct wheel *wheel;

	sc = softclock_pcpu_ary[gd->gd_cpuid];
	atomic_add_int(&sc->curticks, 1);
	if (sc->isrunning)
		return;
	if (sc->softticks == sc->curticks) {
		/*
		 * In sync, only wakeup the thread if there is something to
		 * do.
		 */
		wheel = &sc->callwheel[sc->softticks & cwheelmask];
		spin_lock(&wheel->spin);
		if (TAILQ_FIRST(&wheel->list)) {
			sc->isrunning = 1;
			spin_unlock(&wheel->spin);
			lwkt_schedule(&sc->thread);
		} else {
			atomic_add_int(&sc->softticks, 1);
			spin_unlock(&wheel->spin);
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
 * sc->isrunning flag prevents us from racing hardclock_softtick().
 *
 * The thread starts with the MP lock released and not in a critical
 * section.  The loop itself is MP safe while individual callbacks
 * may or may not be, so we obtain or release the MP lock as appropriate.
 */
static void
softclock_handler(void *arg)
{
	softclock_pcpu_t sc;
	struct _callout *c;
	struct wheel *wheel;
	struct callout slotimer;
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
loop:
	while (sc->softticks != (int)(sc->curticks + 1)) {
		wheel = &sc->callwheel[sc->softticks & cwheelmask];

		spin_lock(&wheel->spin);
		sc->next = TAILQ_FIRST(&wheel->list);
		while ((c = sc->next) != NULL) {
			int error;
			int res;

			/*
			 * Match callouts for this tick.  The wheel spinlock
			 * is sufficient to set INPROG.  Once set, other
			 * threads can make only limited changes to (c)
			 */
			sc->next = TAILQ_NEXT(c, entry);
			if (c->qtick != sc->softticks)
				continue;
			TAILQ_REMOVE(&wheel->list, c, entry);
			atomic_set_int(&c->flags, CALLOUT_INPROG);
			sc->running = c;
			spin_unlock(&wheel->spin);

			/*
			 * legacy mplock support
			 */
			if (c->flags & CALLOUT_MPSAFE) {
				if (mpsafe == 0) {
					mpsafe = 1;
					rel_mplock();
				}
			} else {
				if (mpsafe) {
					mpsafe = 0;
					get_mplock();
				}
			}

			/*
			 * Execute function (protected by INPROG)
			 */
			if (c->flags & (CALLOUT_STOP | CALLOUT_CANCEL)) {
				/*
				 * Raced a stop or cancel request, do
				 * not execute.  The processing code
				 * thinks its a normal completion so
				 * flag the fact that cancel/stop actually
				 * prevented a callout here.
				 */
				if (c->flags & CALLOUT_CANCEL) {
					atomic_set_int(&c->verifier->flags,
						       CALLOUT_CANCEL_RES |
						       CALLOUT_STOP_RES);
				} else if (c->flags & CALLOUT_STOP) {
					atomic_set_int(&c->verifier->flags,
						       CALLOUT_STOP_RES);
				}
			} else if (c->flags & CALLOUT_RESET) {
				/*
				 * A RESET raced, make it seem like it
				 * didn't.  Do nothing here and let the
				 * process routine requeue us.
				 */
			} else if (c->flags & CALLOUT_AUTOLOCK) {
				/*
				 * Interlocked cancelable call.  If the
				 * lock gets canceled we have to flag the
				 * fact that the cancel/stop actually
				 * prevented the callout here.
				 */
				error = lockmgr(c->lk, LK_EXCLUSIVE |
						       LK_CANCELABLE);
				if (error == 0) {
					c->qfunc(c->qarg);
					lockmgr(c->lk, LK_RELEASE);
				} else if (c->flags & CALLOUT_CANCEL) {
					atomic_set_int(&c->verifier->flags,
						       CALLOUT_CANCEL_RES |
						       CALLOUT_STOP_RES);
				} else if (c->flags & CALLOUT_STOP) {
					atomic_set_int(&c->verifier->flags,
						       CALLOUT_STOP_RES);
				}
			} else {
				/*
				 * Normal call
				 */
				c->qfunc(c->qarg);
			}

			if (sc->running == c) {
				/*
				 * We are still INPROG so (c) remains valid, but
				 * the callout is now governed by its internal
				 * spin-lock.
				 */
				spin_lock(&c->spin);
				res = _callout_process_spinlocked(c, 1);
				spin_unlock(&c->spin);
#ifdef CALLOUT_TYPESTABLE
				if (res >= 0)
					_callout_typestable_free(sc, c, res);
#endif
			}
			spin_lock(&wheel->spin);
		}
		sc->running = NULL;
		spin_unlock(&wheel->spin);
		atomic_add_int(&sc->softticks, 1);
	}

	/*
	 * Don't leave us holding the MP lock when we deschedule ourselves.
	 */
	if (mpsafe == 0) {
		mpsafe = 1;
		rel_mplock();
	}

	/*
	 * Recheck in critical section to interlock against hardlock
	 */
	crit_enter();
	if (sc->softticks == (int)(sc->curticks + 1)) {
		sc->isrunning = 0;
		lwkt_deschedule_self(&sc->thread);	/* == curthread */
		lwkt_switch();
	}
	crit_exit();
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
 * API FUNCTIONS
 */

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
 * callout_reset(), callout_stop(), and callout_cancel() will block
 * normally instead of spinning when a cpu race occurs.  Lock cancelation
 * is used to avoid deadlocks against the callout ring dispatch.
 *
 * The init functions can be called from any cpu and do not have to be
 * called from the cpu that the timer will eventually run on.
 */
static __inline void
_callout_setup(struct callout *cc, int flags CALLOUT_DEBUG_ARGS)
{
	bzero(cc, sizeof(*cc));
	cc->flags = flags;		/* frontend flags */
#ifdef CALLOUT_DEBUG
#ifdef CALLOUT_TYPESTABLE
	cc->ident = ident;
	cc->lineno = lineno;
#else
	cc->toc.verifier = cc;		/* corruption detector */
	cc->toc.ident = ident;
	cc->toc.lineno = lineno;
	cc->toc.flags = flags;		/* backend flags */
#endif
#endif
}

/*
 * Associate an internal _callout with the external callout and
 * verify that the type-stable structure is still applicable (inactive
 * type-stable _callouts might have been reused for a different callout).
 * If not, a new internal structure will be allocated.
 *
 * Returns the _callout already spin-locked.
 */
static __inline
struct _callout *
_callout_gettoc(struct callout *cc)
{
	struct _callout *c;
#ifdef CALLOUT_TYPESTABLE
	softclock_pcpu_t sc;

	KKASSERT(cc->flags & CALLOUT_DID_INIT);
	for (;;) {
		c = cc->toc;
		cpu_ccfence();
		if (c == NULL) {
			sc = softclock_pcpu_ary[mycpu->gd_cpuid];
			c = atomic_swap_ptr((void *)&sc->quick_obj, NULL);
			if (c == NULL || _callout_typestable_test(c) == 0)
				c = typestable_alloc(&callout_tsg);
			/* returns spin-locked */
			c->verifier = cc;
			c->flags = cc->flags;
			c->lk = cc->lk;
			c->ident = cc->ident;
			c->lineno = cc->lineno;
			if (atomic_cmpset_ptr(&cc->toc, NULL, c)) {
				break;
			}
			c->verifier = NULL;
			spin_unlock(&c->spin);
			_callout_typestable_free(sc, c, 0);
		} else {
			spin_lock(&c->spin);
			if (c->verifier == cc)
				break;
			spin_unlock(&c->spin);
			/* ok if atomic op fails */
			(void)atomic_cmpset_ptr(&cc->toc, c, NULL);
		}
	}
#else
	c = &cc->toc;
	spin_lock(&c->spin);
#endif
	/* returns with spin-lock held */
	return c;
}

/*
 * Macrod in sys/callout.h for debugging
 *
 * WARNING! tsleep() assumes this will not block
 */
void
_callout_init(struct callout *cc CALLOUT_DEBUG_ARGS)
{
	_callout_setup(cc, CALLOUT_DID_INIT
			CALLOUT_DEBUG_PASSTHRU);
}

void
_callout_init_mp(struct callout *cc CALLOUT_DEBUG_ARGS)
{
	_callout_setup(cc, CALLOUT_DID_INIT | CALLOUT_MPSAFE
			CALLOUT_DEBUG_PASSTHRU);
}

void
_callout_init_lk(struct callout *cc, struct lock *lk CALLOUT_DEBUG_ARGS)
{
	_callout_setup(cc, CALLOUT_DID_INIT | CALLOUT_MPSAFE |
			   CALLOUT_AUTOLOCK
			CALLOUT_DEBUG_PASSTHRU);
#ifdef CALLOUT_TYPESTABLE
	cc->lk = lk;
#else
	cc->toc.lk = lk;
#endif
}

/*
 * Start or restart a timeout.  New timeouts can be installed while the
 * current one is running.
 *
 * Start or restart a timeout.  Installs the callout structure on the
 * callwheel of the current cpu.  Callers may legally pass any value, even
 * if 0 or negative, but since the sc->curticks index may have already
 * been processed a minimum timeout of 1 tick will be enforced.
 *
 * This function will not deadlock against a running call.
 *
 * WARNING! tsleep() assumes this will not block
 */
void
callout_reset(struct callout *cc, int to_ticks, void (*ftn)(void *), void *arg)
{
	softclock_pcpu_t sc;
	struct _callout *c;
	int res;

	atomic_set_int(&cc->flags, CALLOUT_ACTIVE);
	c = _callout_gettoc(cc);

	/*
	 * Set RESET.  Do not clear STOP here (let the process code do it).
	 */
	atomic_set_int(&c->flags, CALLOUT_RESET);
	sc = softclock_pcpu_ary[mycpu->gd_cpuid];
	c->rsc = sc;
	c->rtick = sc->curticks + to_ticks;
	c->rfunc = ftn;
	c->rarg = arg;
#ifdef CALLOUT_TYPESTABLE
	cc->arg = arg;	/* only used by callout_arg() */
#endif
	res = _callout_process_spinlocked(c, 0);
	spin_unlock(&c->spin);
#ifdef CALLOUT_TYPESTABLE
	if (res >= 0)
		_callout_typestable_free(sc, c, res);
#endif
}

/*
 * Same as callout_reset() but the timeout will run on a particular cpu.
 */
void
callout_reset_bycpu(struct callout *cc, int to_ticks, void (*ftn)(void *),
		    void *arg, int cpuid)
{
	softclock_pcpu_t sc;
	struct _callout *c;
	int res;

	atomic_set_int(&cc->flags, CALLOUT_ACTIVE);
	c = _callout_gettoc(cc);

	/*
	 * Set RESET.  Do not clear STOP here (let the process code do it).
	 */
	atomic_set_int(&c->flags, CALLOUT_RESET);

	sc = softclock_pcpu_ary[cpuid];
	c->rsc = sc;
	c->rtick = sc->curticks + to_ticks;
	c->rfunc = ftn;
	c->rarg = arg;
#ifdef CALLOUT_TYPESTABLE
	cc->arg = arg;	/* only used by callout_arg() */
#endif
	res = _callout_process_spinlocked(c, 0);
	spin_unlock(&c->spin);
#ifdef CALLOUT_TYPESTABLE
	if (res >= 0)
		_callout_typestable_free(sc, c, res);
#endif
}

static __inline
void
_callout_cancel_or_stop(struct callout *cc, uint32_t flags)
{
	struct _callout *c;
	softclock_pcpu_t sc;
	int res;

#ifdef CALLOUT_TYPESTABLE
	if (cc->toc == NULL || cc->toc->verifier != cc)
		return;
#else
	KKASSERT(cc->toc.verifier == cc);
#endif
	/*
	 * Setup for synchronous
	 */
	atomic_clear_int(&cc->flags, CALLOUT_ACTIVE);
	c = _callout_gettoc(cc);

	/*
	 * Set STOP or CANCEL request.  If this is a STOP, clear a queued
	 * RESET now.
	 */
	atomic_set_int(&c->flags, flags);
	if (flags & CALLOUT_STOP) {
		if (c->flags & CALLOUT_RESET) {
			atomic_set_int(&cc->flags, CALLOUT_STOP_RES);
			atomic_clear_int(&c->flags, CALLOUT_RESET);
		}
	}
	sc = softclock_pcpu_ary[mycpu->gd_cpuid];
	res = _callout_process_spinlocked(c, 0);
	spin_unlock(&c->spin);
#ifdef CALLOUT_TYPESTABLE
	if (res >= 0)
		_callout_typestable_free(sc, c, res);
#endif

	/*
	 * Wait for the CANCEL or STOP to finish.
	 *
	 * WARNING! (c) can go stale now, so do not use (c) after this
	 *	    point. XXX
	 */
	if (c->flags & flags) {
		atomic_add_int(&c->waiters, 1);
#ifdef CALLOUT_TYPESTABLE
		if (cc->flags & CALLOUT_AUTOLOCK)
			lockmgr(cc->lk, LK_CANCEL_BEG);
#else
		if (cc->flags & CALLOUT_AUTOLOCK)
			lockmgr(c->lk, LK_CANCEL_BEG);
#endif
		for (;;) {
			tsleep_interlock(cc, 0);
			if ((atomic_fetchadd_int(&c->flags, 0) & flags) == 0)
				break;
			tsleep(cc, PINTERLOCKED, "costp", 0);
		}
#ifdef CALLOUT_TYPESTABLE
		if (cc->flags & CALLOUT_AUTOLOCK)
			lockmgr(cc->lk, LK_CANCEL_END);
#else
		if (cc->flags & CALLOUT_AUTOLOCK)
			lockmgr(c->lk, LK_CANCEL_END);
#endif
		atomic_add_int(&c->waiters, -1);
	}
	KKASSERT(cc->toc.verifier == cc);
}

/*
 * This is a synchronous STOP which cancels the callout.  If AUTOLOCK
 * then a CANCEL will be issued to the lock holder.  Unlike STOP, the
 * cancel function prevents any new callout_reset()s from being issued
 * in addition to canceling the lock.  The lock will also be deactivated.
 *
 * Returns 0 if the callout was not active (or was active and completed,
 *	     but didn't try to start a new timeout).
 * Returns 1 if the cancel is responsible for stopping the callout.
 */
int
callout_cancel(struct callout *cc)
{
	atomic_clear_int(&cc->flags, CALLOUT_CANCEL_RES);
	_callout_cancel_or_stop(cc, CALLOUT_CANCEL);

	return ((cc->flags & CALLOUT_CANCEL_RES) ? 1 : 0);
}

/*
 * Currently the same as callout_cancel.  Ultimately we may wish the
 * drain function to allow a pending callout to proceed, but for now
 * we will attempt to to cancel it.
 *
 * Returns 0 if the callout was not active (or was active and completed,
 *	     but didn't try to start a new timeout).
 * Returns 1 if the drain is responsible for stopping the callout.
 */
int
callout_drain(struct callout *cc)
{
	atomic_clear_int(&cc->flags, CALLOUT_CANCEL_RES);
	_callout_cancel_or_stop(cc, CALLOUT_CANCEL);

	return ((cc->flags & CALLOUT_CANCEL_RES) ? 1 : 0);
}

/*
 * Stops a callout if it is pending or queued, does not block.
 * This function does not interlock against a callout that is in-progress.
 *
 * Returns whether the STOP operation was responsible for removing a
 * queued or pending callout.
 */
int
callout_stop_async(struct callout *cc)
{
	softclock_pcpu_t sc;
	struct _callout *c;
	uint32_t flags;
	int res;

	atomic_clear_int(&cc->flags, CALLOUT_STOP_RES | CALLOUT_ACTIVE);
#ifdef CALLOUT_TYPESTABLE
	if (cc->toc == NULL || cc->toc->verifier != cc)
		return 0;
#else
	KKASSERT(cc->toc.verifier == cc);
#endif
	c = _callout_gettoc(cc);

	/*
	 * Set STOP or CANCEL request.  If this is a STOP, clear a queued
	 * RESET now.
	 */
	atomic_set_int(&c->flags, CALLOUT_STOP);
	if (c->flags & CALLOUT_RESET) {
		atomic_set_int(&cc->flags, CALLOUT_STOP_RES);
		atomic_clear_int(&c->flags, CALLOUT_RESET);
	}
	sc = softclock_pcpu_ary[mycpu->gd_cpuid];
	res = _callout_process_spinlocked(c, 0);
	flags = cc->flags;
	spin_unlock(&c->spin);
#ifdef CALLOUT_TYPESTABLE
	if (res >= 0)
		_callout_typestable_free(sc, c, res);
#endif

	return ((flags & CALLOUT_STOP_RES) ? 1 : 0);
}

/*
 * Callout deactivate merely clears the CALLOUT_ACTIVE bit
 * Stops a callout if it is pending or queued, does not block.
 * This function does not interlock against a callout that is in-progress.
 */
void
callout_deactivate(struct callout *cc)
{
	atomic_clear_int(&cc->flags, CALLOUT_ACTIVE);
}

/*
 * lock-aided callouts are STOPped synchronously using STOP semantics
 * (meaning that another thread can start the callout again before we
 * return).
 *
 * non-lock-aided callouts
 *
 * Stops a callout if it is pending or queued, does not block.
 * This function does not interlock against a callout that is in-progress.
 */
int
callout_stop(struct callout *cc)
{
	if (cc->flags & CALLOUT_AUTOLOCK) {
		atomic_clear_int(&cc->flags, CALLOUT_STOP_RES);
		_callout_cancel_or_stop(cc, CALLOUT_STOP);
		return ((cc->flags & CALLOUT_STOP_RES) ? 1 : 0);
	} else {
		return callout_stop_async(cc);
	}
}

/*
 * Terminates a callout by canceling operations and then clears the
 * INIT bit.  Upon return, the callout structure must not be used.
 */
void
callout_terminate(struct callout *cc)
{
	_callout_cancel_or_stop(cc, CALLOUT_CANCEL);
	atomic_clear_int(&cc->flags, CALLOUT_DID_INIT);
#ifdef CALLOUT_TYPESTABLE
	atomic_swap_ptr((void *)&cc->toc, NULL);
#else
	cc->toc.verifier = NULL;
#endif
}

/*
 * Returns whether a callout is queued and the time has not yet
 * arrived (the callout is not yet in-progress).
 */
int
callout_pending(struct callout *cc)
{
	struct _callout *c;
	int res = 0;

	/*
	 * Don't instantiate toc to test pending
	 */
#ifdef CALLOUT_TYPESTABLE
	if ((c = cc->toc) != NULL) {
#else
	c = &cc->toc;
	KKASSERT(c->verifier == cc);
	{
#endif
		spin_lock(&c->spin);
		if (c->verifier == cc) {
			res = ((c->flags & (CALLOUT_SET|CALLOUT_INPROG)) ==
			       CALLOUT_SET);
		}
		spin_unlock(&c->spin);
	}
	return res;
}

/*
 * Returns whether a callout is active or not.  A callout is active when
 * a timeout is set and remains active upon normal termination, even if
 * it does not issue a new timeout.  A callout is inactive if a timeout has
 * never been set or if the callout has been stopped or canceled.  The next
 * timeout that is set will re-set the active state.
 */
int
callout_active(struct callout *cc)
{
	return ((cc->flags & CALLOUT_ACTIVE) ? 1 : 0);
}
