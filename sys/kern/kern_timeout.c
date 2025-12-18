/*
 * Copyright (c) 2004,2014,2019-2020 The DragonFly Project.
 * All rights reserved.
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
#include <sys/exislock.h>
#include <vm/vm_extern.h>
#include <machine/atomic.h>

#include <sys/spinlock2.h>
#include <sys/thread2.h>
#include <sys/mplock2.h>
#include <sys/exislock2.h>

TAILQ_HEAD(colist, _callout);
struct softclock_pcpu;

/*
 * DID_INIT	- Sanity check
 * PREVENTED	- A callback was prevented
 * RESET	- Callout_reset requested
 * STOP		- Callout_stop requested
 * INPROG	- Softclock_handler thread processing in-progress on callout,
 *		  queue linkage is indeterminant.  Third parties must queue
 *		  a STOP or CANCEL and await completion.
 * SET		- Callout is linked to queue (if INPROG not set)
 * AUTOLOCK	- Lockmgr cancelable interlock (copied from frontend)
 * MPSAFE	- Callout is MPSAFE (copied from frontend)
 * CANCEL	- callout_cancel requested
 * ACTIVE	- active/inactive (frontend only, see documentation).
 *		  This is *NOT* the same as whether a callout is queued or
 *		  not.
 */
#define CALLOUT_DID_INIT	0x00000001	/* frontend */
#define CALLOUT_PREVENTED	0x00000002	/* backend */
#define CALLOUT_FREELIST	0x00000004	/* backend */
#define CALLOUT_UNUSED0008	0x00000008
#define CALLOUT_UNUSED0010	0x00000010
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
	struct colist	freelist;
	int		softticks;	/* softticks index */
	int		curticks;	/* per-cpu ticks counter */
	int		isrunning;
	struct thread	thread;
};

typedef struct softclock_pcpu *softclock_pcpu_t;

static int callout_debug = 0;
SYSCTL_INT(_debug, OID_AUTO, callout_debug, CTLFLAG_RW,
	   &callout_debug, 0, "");

static MALLOC_DEFINE(M_CALLOUT, "callouts", "softclock callouts");

static int cwheelsize;
static int cwheelmask;
static softclock_pcpu_t softclock_pcpu_ary[MAXCPU];

static void softclock_handler(void *arg);
static void slotimer_callback(void *arg);

/*
 * Handle pending requests.  No action can be taken if the callout is still
 * flagged INPROG.  Called from softclock for post-processing and from
 * various API functions.
 *
 * This routine does not block in any way.
 * Caller must hold c->spin.
 *
 * NOTE: Flags can be adjusted without holding c->spin, so atomic ops
 *	 must be used at all times.
 *
 * NOTE: The related (sc) might refer to another cpu.
 *
 * NOTE: The cc-vs-c frontend-vs-backend might be disconnected during the
 *	 operation, but the EXIS lock prevents (c) from being destroyed.
 */
static __inline
void
_callout_update_spinlocked(struct _callout *c)
{
	struct wheel *wheel;

	if ((c->flags & CALLOUT_INPROG) && curthread != &c->qsc->thread) {
		/*
		 * If the callout is in-progress the SET queuing state is
		 * indeterminant and no action can be taken at this time.
		 *
		 * (however, recursive calls from the call-back are not
		 * indeterminant and must be processed at this time).
		 */
		/* nop */
	} else if (c->flags & CALLOUT_SET) {
		/*
		 * If the callout is SET it is queued on a callwheel, process
		 * various requests relative to it being in this queued state.
		 *
		 * c->q* fields are stable while we hold c->spin and
		 * wheel->spin.
		 */
		softclock_pcpu_t sc;

		sc = c->qsc;
		wheel = &sc->callwheel[c->qtick & cwheelmask];
		spin_lock(&wheel->spin);

		if ((c->flags & CALLOUT_INPROG) &&
		    curthread != &c->qsc->thread) {
			/*
			 * Raced against INPROG getting set by the softclock
			 * handler while we were acquiring wheel->spin.  We
			 * can do nothing at this time.
			 *
			 * (however, recursive calls from the call-back are not
			 * indeterminant and must be processed at this time).
			 */
			/* nop */
		} else if (c->flags & CALLOUT_CANCEL) {
			/*
			 * CANCEL requests override everything else.
			 */
			if (sc->next == c)
				sc->next = TAILQ_NEXT(c, entry);
			TAILQ_REMOVE(&wheel->list, c, entry);
			atomic_clear_int(&c->flags, CALLOUT_SET |
						    CALLOUT_STOP |
						    CALLOUT_CANCEL |
						    CALLOUT_RESET);
			atomic_set_int(&c->flags, CALLOUT_PREVENTED);
			if (c->waiters)
				wakeup(c);
		} else if (c->flags & CALLOUT_RESET) {
			/*
			 * RESET requests reload the callout, potentially
			 * to a different cpu.  Once removed from the wheel,
			 * the retention of c->spin prevents further races.
			 *
			 * Leave SET intact.
			 */
			if (sc->next == c)
				sc->next = TAILQ_NEXT(c, entry);
			TAILQ_REMOVE(&wheel->list, c, entry);
			spin_unlock(&wheel->spin);

			atomic_clear_int(&c->flags, CALLOUT_RESET);
			sc = c->rsc;
			c->qsc = sc;
			c->qarg = c->rarg;
			c->qfunc = c->rfunc;
			c->qtick = c->rtick;

			/*
			 * Do not queue to a current or past wheel slot or
			 * the callout will be lost for ages.  Handle
			 * potential races against soft ticks.
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
			 * STOP request simply unloads the callout.
			 */
			if (sc->next == c)
				sc->next = TAILQ_NEXT(c, entry);
			TAILQ_REMOVE(&wheel->list, c, entry);
			atomic_clear_int(&c->flags, CALLOUT_STOP |
						    CALLOUT_SET);

			atomic_set_int(&c->flags, CALLOUT_PREVENTED);
			if (c->waiters)
				wakeup(c);
		} else {
			/*
			 * Do nothing if no request is pending.
			 */
			/* nop */
		}
		spin_unlock(&wheel->spin);
	} else {
		/*
		 * If the callout is not SET it is not queued to any callwheel,
		 * process various requests relative to it not being queued.
		 *
		 * c->q* fields are stable while we hold c->spin.
		 */
		if (c->flags & CALLOUT_CANCEL) {
			/*
			 * CANCEL requests override everything else.
			 *
			 * There is no state being canceled in this case,
			 * so do not set the PREVENTED flag.
			 */
			atomic_clear_int(&c->flags, CALLOUT_STOP |
						    CALLOUT_CANCEL |
						    CALLOUT_RESET);
			if (c->waiters)
				wakeup(c);
		} else if (c->flags & CALLOUT_RESET) {
			/*
			 * RESET requests get queued.  Do not queue to the
			 * currently-processing tick.
			 */
			softclock_pcpu_t sc;

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
			atomic_clear_int(&c->flags, CALLOUT_RESET);
			atomic_set_int(&c->flags, CALLOUT_SET);
			spin_unlock(&wheel->spin);
		} else if (c->flags & CALLOUT_STOP) {
			/*
			 * STOP requests.
			 *
			 * There is no state being stopped in this case,
			 * so do not set the PREVENTED flag.
			 */
			atomic_clear_int(&c->flags, CALLOUT_STOP);
			if (c->waiters)
				wakeup(c);
		} else {
			/*
			 * No request pending (someone else processed the
			 * request before we could)
			 */
			/* nop */
		}
	}
}

static __inline
void
_callout_free(struct _callout *c)
{
	softclock_pcpu_t sc;

	sc = softclock_pcpu_ary[mycpu->gd_cpuid];

	crit_enter();
	exis_terminate(&c->exis);
	atomic_set_int(&c->flags, CALLOUT_FREELIST);
	atomic_clear_int(&c->flags, CALLOUT_DID_INIT);
	TAILQ_INSERT_TAIL(&sc->freelist, c, entry);
	crit_exit();
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

	/*
	 * Initialize per-cpu data structures.
	 */
	for (cpu = 0; cpu < ncpus; ++cpu) {
		softclock_pcpu_t sc;
		int wheel_sz;

		sc = (void *)kmem_alloc3(kernel_map, sizeof(*sc),
					 VM_SUBSYS_GD, KM_CPU(cpu));
		memset(sc, 0, sizeof(*sc));
		TAILQ_INIT(&sc->freelist);
		softclock_pcpu_ary[cpu] = sc;

		wheel_sz = sizeof(*sc->callwheel) * cwheelsize;
		sc->callwheel = (void *)kmem_alloc3(kernel_map, wheel_sz,
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
	struct callout slotimer1;
	struct _callout slotimer2;
	int mpsafe = 1;

	/*
	 * Setup pcpu slow clocks which we want to run from the callout
	 * thread.  This thread starts very early and cannot kmalloc(),
	 * so use internal functions to supply the _callout.
	 */
	_callout_setup_quick(&slotimer1, &slotimer2, hz * 10,
			     slotimer_callback, &slotimer1);

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

			/*
			 * Match callouts for this tick.
			 */
			sc->next = TAILQ_NEXT(c, entry);
			if (c->qtick != sc->softticks)
				continue;

			/*
			 * Double check the validity of the callout, detect
			 * if the originator's structure has been ripped out.
			 *
			 * Skip the address range check for virtual kernels
			 * since vkernel addresses are in host user space.
			 */
#ifndef _KERNEL_VIRTUAL
			if ((uintptr_t)c->verifier < VM_MAX_USER_ADDRESS) {
				spin_unlock(&wheel->spin);
				panic("_callout %p verifier %p failed "
				      "func %p/%p\n",
				      c, c->verifier, c->rfunc, c->qfunc);
			}
#endif

			if (c->verifier->toc != c) {
				spin_unlock(&wheel->spin);
				panic("_callout %p verifier %p toc %p (expected %p) "
				      "func %p/%p\n",
				      c, c->verifier, c->verifier->toc, c,
				      c->rfunc, c->qfunc);
			}

			/*
			 * The wheel spinlock is sufficient to set INPROG and
			 * remove (c) from the list.  Once INPROG is set,
			 * other threads can only make limited changes to (c).
			 *
			 * Setting INPROG masks SET tests in all other
			 * conditionals except the 'quick' code (which is
			 * always same-cpu and doesn't race).  This means
			 * that we can clear SET here without obtaining
			 * c->spin.
			 */
			TAILQ_REMOVE(&wheel->list, c, entry);
			atomic_set_int(&c->flags, CALLOUT_INPROG);
			atomic_clear_int(&c->flags, CALLOUT_SET);
			sc->running = c;
			spin_unlock(&wheel->spin);

			/*
			 * Legacy mplock support
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
			 * Execute the 'q' function (protected by INPROG)
			 */
			if (c->flags & (CALLOUT_STOP | CALLOUT_CANCEL)) {
				/*
				 * Raced a stop or cancel request, do
				 * not execute.  The processing code
				 * thinks its a normal completion so
				 * flag the fact that cancel/stop actually
				 * prevented a callout here.
				 */
				if (c->flags &
				    (CALLOUT_CANCEL | CALLOUT_STOP)) {
					atomic_set_int(&c->verifier->flags,
						       CALLOUT_PREVENTED);
				}
			} else if (c->flags & CALLOUT_RESET) {
				/*
				 * A RESET raced, make it seem like it
				 * didn't.  Do nothing here and let the
				 * update procedure requeue us.
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
				} else if (c->flags &
					   (CALLOUT_CANCEL | CALLOUT_STOP)) {
					atomic_set_int(&c->verifier->flags,
						       CALLOUT_PREVENTED);
				}
			} else {
				/*
				 * Normal call
				 */
				c->qfunc(c->qarg);
			}

			/*
			 * INPROG will prevent SET from being set again.
			 * Once we clear INPROG, update the callout to
			 * handle any pending operations that have built-up.
			 */

			/*
			 * Interlocked clearing of INPROG, then handle any
			 * queued request (such as a callout_reset() request).
			 */
			spin_lock(&c->spin);
			atomic_clear_int(&c->flags, CALLOUT_INPROG);
			sc->running = NULL;
			_callout_update_spinlocked(c);
			spin_unlock(&c->spin);

			spin_lock(&wheel->spin);
		}
		spin_unlock(&wheel->spin);
		atomic_add_int(&sc->softticks, 1);

		/*
		 * Clean up any _callout structures which are now allowed
		 * to be freed.
		 */
		crit_enter();
		while ((c = TAILQ_FIRST(&sc->freelist)) != NULL) {
			if (!exis_freeable(&c->exis))
				break;
			TAILQ_REMOVE(&sc->freelist, c, entry);
			c->flags = 0;
			kfree(c, M_CALLOUT);
			if (callout_debug)
				kprintf("KFREEB %p\n", c);
		}
		crit_exit();
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

static __inline
struct _callout *
_callout_gettoc(struct callout *cc)
{
	globaldata_t gd = mycpu;
	struct _callout *c;
	softclock_pcpu_t sc;

	KKASSERT(cc->flags & CALLOUT_DID_INIT);
	exis_hold_gd(gd);
	for (;;) {
		c = cc->toc;
		cpu_ccfence();
		if (c) {
			KKASSERT(c->verifier == cc);
			spin_lock(&c->spin);
			break;
		}
		sc = softclock_pcpu_ary[gd->gd_cpuid];
		c = kmalloc(sizeof(*c), M_CALLOUT, M_INTWAIT | M_ZERO);
		if (callout_debug)
			kprintf("ALLOC %p\n", c);
		c->flags = cc->flags;
		c->lk = cc->lk;
		c->verifier = cc;
		exis_init(&c->exis);
		spin_init(&c->spin, "calou");
		spin_lock(&c->spin);
		if (atomic_cmpset_ptr(&cc->toc, NULL, c))
			break;
		spin_unlock(&c->spin);
		c->verifier = NULL;
		kfree(c, M_CALLOUT);
		if (callout_debug)
			kprintf("KFREEA %p\n", c);
	}
	exis_drop_gd(gd);

	/*
	 * Return internal __callout with spin-lock held
	 */
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
	bzero(cc, sizeof(*cc));
	cc->flags = CALLOUT_DID_INIT;
}

void
_callout_init_mp(struct callout *cc CALLOUT_DEBUG_ARGS)
{
	bzero(cc, sizeof(*cc));
	cc->flags = CALLOUT_DID_INIT | CALLOUT_MPSAFE;
}

void
_callout_init_lk(struct callout *cc, struct lock *lk CALLOUT_DEBUG_ARGS)
{
	bzero(cc, sizeof(*cc));
	cc->flags = CALLOUT_DID_INIT | CALLOUT_MPSAFE | CALLOUT_AUTOLOCK;
	cc->lk = lk;
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

	/*
	 * We need to acquire/associate a _callout.
	 * gettoc spin-locks (c).
	 */
	KKASSERT(cc->flags & CALLOUT_DID_INIT);
	atomic_set_int(&cc->flags, CALLOUT_ACTIVE);
	c = _callout_gettoc(cc);

	/*
	 * Request a RESET.  This automatically overrides a STOP in
	 * _callout_update_spinlocked().
	 */
	atomic_set_int(&c->flags, CALLOUT_RESET);
	sc = softclock_pcpu_ary[mycpu->gd_cpuid];
	c->rsc = sc;
	c->rtick = sc->curticks + to_ticks;
	c->rfunc = ftn;
	c->rarg = arg;
	_callout_update_spinlocked(c);
	spin_unlock(&c->spin);
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

	/*
	 * We need to acquire/associate a _callout.
	 * gettoc spin-locks (c).
	 */
	KKASSERT(cc->flags & CALLOUT_DID_INIT);
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
	_callout_update_spinlocked(c);
	spin_unlock(&c->spin);
}

/*
 * Issue synchronous or asynchronous cancel or stop
 */
static __inline
int
_callout_cancel_or_stop(struct callout *cc, uint32_t flags, int sync)
{
	globaldata_t gd = mycpu;
	struct _callout *c;
	int res;

	/*
	 * Callout is inactive after cancel or stop.  Degenerate case if
	 * no _callout is currently associated.
	 */
	atomic_clear_int(&cc->flags, CALLOUT_ACTIVE);
	if (cc->toc == NULL)
		return 0;

	/*
	 * Ensure that the related (c) is not destroyed.  Set the CANCEL
	 * or STOP request flag, clear the PREVENTED status flag, and update.
	 */
	exis_hold_gd(gd);
	c = _callout_gettoc(cc);
	atomic_clear_int(&c->flags, CALLOUT_PREVENTED);
	atomic_set_int(&c->flags, flags);
	_callout_update_spinlocked(c);
	spin_unlock(&c->spin);

	/*
	 * If the operation is still in-progress then re-acquire the spin-lock
	 * and block if necessary.  Also initiate the lock cancel.
	 */
	if (sync == 0 || (c->flags & (CALLOUT_INPROG | CALLOUT_SET)) == 0) {
		exis_drop_gd(gd);
		return 0;
	}
	if (c->flags & CALLOUT_AUTOLOCK)
		lockmgr(c->lk, LK_CANCEL_BEG);
	spin_lock(&c->spin);
	if ((c->flags & (CALLOUT_INPROG | CALLOUT_SET)) == 0) {
		spin_unlock(&c->spin);
		if (c->flags & CALLOUT_AUTOLOCK)
			lockmgr(c->lk, LK_CANCEL_END);
		exis_drop_gd(gd);
		return ((c->flags & CALLOUT_PREVENTED) != 0);
	}

	/*
	 * With c->spin held we can synchronously wait completion of our
	 * request.
	 *
	 * If INPROG is set and we are recursing from the callback the
	 * function completes immediately.
	 */
	++c->waiters;
	for (;;) {
		cpu_ccfence();
		if ((c->flags & flags) == 0)
			break;
		if ((c->flags & CALLOUT_INPROG) &&
		    curthread == &c->qsc->thread) {
			_callout_update_spinlocked(c);
			break;
		}
		ssleep(c, &c->spin, 0, "costp", 0);
	}
	--c->waiters;
	spin_unlock(&c->spin);
	if (c->flags & CALLOUT_AUTOLOCK)
		lockmgr(c->lk, LK_CANCEL_END);
	res = ((c->flags & CALLOUT_PREVENTED) != 0);
	exis_drop_gd(gd);

	return res;
}

/*
 * Internalized special low-overhead version without normal safety
 * checks or allocations.  Used by tsleep().
 *
 * Must be called from critical section, specify both the external
 * and internal callout structure and set timeout on the current cpu.
 */
void
_callout_setup_quick(struct callout *cc, struct _callout *c, int ticks,
		     void (*ftn)(void *), void *arg)
{
	softclock_pcpu_t sc;
	struct wheel *wheel;

	/*
	 * Request a RESET.  This automatically overrides a STOP in
	 * _callout_update_spinlocked().
	 */
	sc = softclock_pcpu_ary[mycpu->gd_cpuid];

	cc->flags = CALLOUT_DID_INIT | CALLOUT_MPSAFE;
	cc->toc = c;
	cc->lk = NULL;
	c->flags = cc->flags | CALLOUT_SET;
	c->lk = NULL;
	c->verifier = cc;
	c->qsc = sc;
	c->qtick = sc->curticks + ticks;
	c->qfunc = ftn;
	c->qarg = arg;
	spin_init(&c->spin, "calou");

	/*
	 * Since we are on the same cpu with a critical section, we can
	 * do this with only the wheel spinlock.
	 */
	if (c->qtick - sc->softticks <= 0)
		c->qtick = sc->softticks + 1;
	wheel = &sc->callwheel[c->qtick & cwheelmask];

	spin_lock(&wheel->spin);
	TAILQ_INSERT_TAIL(&wheel->list, c, entry);
	spin_unlock(&wheel->spin);
}

/*
 * Internalized special low-overhead version without normal safety
 * checks or allocations.  Used by tsleep().
 *
 * Must be called on the same cpu that queued the timeout.
 * Must be called with a critical section already held.
 */
void
_callout_cancel_quick(struct _callout *c)
{
	softclock_pcpu_t sc;
	struct wheel *wheel;

	/*
	 * Wakeup callouts for tsleep() should never block, so this flag
	 * had better never be found set.
	 */
	KKASSERT((c->flags & CALLOUT_INPROG) == 0);

	/*
	 * Remove from queue if necessary.  Since we are in a critical
	 * section on the same cpu, the queueing status should not change.
	 */
	if (c->flags & CALLOUT_SET) {
		sc = c->qsc;
		KKASSERT(sc == softclock_pcpu_ary[mycpu->gd_cpuid]);
		wheel = &sc->callwheel[c->qtick & cwheelmask];

		/*
		 * NOTE: We must still spin-lock the wheel because other
		 *	 cpus can manipulate the list, and adjust sc->next
		 *	 if necessary.
		 */
		spin_lock(&wheel->spin);
		if (sc->next == c)
			sc->next = TAILQ_NEXT(c, entry);
		TAILQ_REMOVE(&wheel->list, c, entry);
		c->flags &= ~(CALLOUT_SET | CALLOUT_STOP |
			      CALLOUT_CANCEL | CALLOUT_RESET);
		spin_unlock(&wheel->spin);
	}
	c->verifier = NULL;
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
	return _callout_cancel_or_stop(cc, CALLOUT_CANCEL, 1);
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
	return _callout_cancel_or_stop(cc, CALLOUT_CANCEL, 1);
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
	return _callout_cancel_or_stop(cc, CALLOUT_STOP, 0);
}

/*
 * Callout deactivate merely clears the CALLOUT_ACTIVE bit and stop a
 * callout if it is pending or queued.  However this cannot stop a callout
 * whos callback is in-progress.
 *
 *
 * This function does not interlock against a callout that is in-progress.
 */
void
callout_deactivate(struct callout *cc)
{
	atomic_clear_int(&cc->flags, CALLOUT_ACTIVE);
	callout_stop_async(cc);
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
	return _callout_cancel_or_stop(cc, CALLOUT_STOP, 1);
}

/*
 * Destroy the callout.  Synchronously cancel any operation in progress,
 * clear the INIT flag, and disconnect the internal _callout.  The internal
 * callout will be safely freed via EXIS.
 *
 * Upon return, the callout structure may only be reused if re-initialized.
 */
void
callout_terminate(struct callout *cc)
{
	struct _callout *c;

	exis_hold();

	_callout_cancel_or_stop(cc, CALLOUT_CANCEL, 1);
	KKASSERT(cc->flags & CALLOUT_DID_INIT);
	atomic_clear_int(&cc->flags, CALLOUT_DID_INIT);
	c = atomic_swap_ptr((void *)&cc->toc, NULL);
	if (c) {
		KKASSERT(c->verifier == cc);
		c->verifier = NULL;
		_callout_free(c);
	}

	exis_drop();
}

/*
 * Returns whether a callout is queued and the time has not yet
 * arrived (the callout is not yet in-progress).
 */
int
callout_pending(struct callout *cc)
{
	struct _callout *c;

	/*
	 * Don't instantiate toc to test pending
	 */
	if (cc->toc == NULL)
		return 0;
	c = _callout_gettoc(cc);
	if ((c->flags & (CALLOUT_SET | CALLOUT_INPROG)) == CALLOUT_SET) {
		spin_unlock(&c->spin);
		return 1;
	}
	spin_unlock(&c->spin);

	return 0;
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
