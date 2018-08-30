/* 
 * Copyright (c) 1995
 *	The Regents of the University of California.  All rights reserved.
 * Copyright (C) 1997
 *	John S. Dyson.  All rights reserved.
 * Copyright (C) 2013-2017
 *	Matthew Dillon, All rights reserved.
 *
 * This code contains ideas from software contributed to Berkeley by
 * Avadis Tevanian, Jr., Michael Wayne Young, and the Mach Operating
 * System project at Carnegie-Mellon University.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Matthew Dillon <dillon@backplane.com>.  Extensively rewritten.
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

#include "opt_lint.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/proc.h>
#include <sys/lock.h>
#include <sys/sysctl.h>
#include <sys/spinlock.h>
#include <sys/thread2.h>
#include <sys/spinlock2.h>
#include <sys/indefinite2.h>

static void undo_shreq(struct lock *lkp);
static int undo_upreq(struct lock *lkp);
static int undo_exreq(struct lock *lkp);

#ifdef DEBUG_CANCEL_LOCKS

static int sysctl_cancel_lock(SYSCTL_HANDLER_ARGS);
static int sysctl_cancel_test(SYSCTL_HANDLER_ARGS);

static struct lock cancel_lk;
LOCK_SYSINIT(cancellk, &cancel_lk, "cancel", 0);
SYSCTL_PROC(_kern, OID_AUTO, cancel_lock, CTLTYPE_INT|CTLFLAG_RW, 0, 0,
	    sysctl_cancel_lock, "I", "test cancelable locks");
SYSCTL_PROC(_kern, OID_AUTO, cancel_test, CTLTYPE_INT|CTLFLAG_RW, 0, 0,
	    sysctl_cancel_test, "I", "test cancelable locks");

#endif

int lock_test_mode;
SYSCTL_INT(_debug, OID_AUTO, lock_test_mode, CTLFLAG_RW,
	   &lock_test_mode, 0, "");

/*
 * Locking primitives implementation.
 * Locks provide shared/exclusive sychronization.
 */

#ifdef DEBUG_LOCKS
#define COUNT(td, x) (td)->td_locks += (x)
#else
#define COUNT(td, x) do { } while (0)
#endif

/*
 * Helper, assert basic conditions
 */
static __inline void
_lockmgr_assert(struct lock *lkp, u_int flags)
{
	if (mycpu->gd_intr_nesting_level &&
	    (flags & LK_NOWAIT) == 0 &&
	    (flags & LK_TYPE_MASK) != LK_RELEASE &&
	    panic_cpu_gd != mycpu
	) {
		panic("lockmgr %s from %p: called from interrupt, ipi, "
		      "or hard code section",
		      lkp->lk_wmesg, ((int **)&lkp)[-1]);
	}
}

/*
 * Acquire a shared lock
 */
int
lockmgr_shared(struct lock *lkp, u_int flags)
{
	uint32_t extflags;
	thread_t td;
	uint64_t count;
	int error;
	int pflags;
	int timo;
	int didloop;

	_lockmgr_assert(lkp, flags);
	extflags = (flags | lkp->lk_flags) & LK_EXTFLG_MASK;
	td = curthread;

	count = lkp->lk_count;
	cpu_ccfence();

	/*
	 * If the caller already holds the lock exclusively then
	 * we silently obtain another count on the exclusive lock.
	 * Avoid accessing lk_lockholder until testing exclusivity.
	 *
	 * WARNING!  The old FreeBSD behavior was to downgrade,
	 *	     but this creates a problem when recursions
	 *	     return to the caller and the caller expects
	 *	     its original exclusive lock to remain exclusively
	 *	     locked.
	 */
	if ((count & LKC_XMASK) && lkp->lk_lockholder == td) {
		KKASSERT(lkp->lk_count & LKC_XMASK);
		if ((extflags & LK_CANRECURSE) == 0) {
			if (extflags & LK_NOWAIT)
				return EBUSY;
			panic("lockmgr: locking against myself");
		}
		atomic_add_64(&lkp->lk_count, 1);
		COUNT(td, 1);
		return 0;
	}

	/*
	 * Unless TDF_DEADLKTREAT is set, we cannot add LKC_SCOUNT while
	 * SHARED is set and either EXREQ or UPREQ are set.
	 *
	 * NOTE: In the race-to-0 case (see undo_shreq()), we could
	 *	 theoretically work the SMASK == 0 case here.
	 */
	if ((td->td_flags & TDF_DEADLKTREAT) == 0) {
		while ((count & LKC_SHARED) &&
		       (count & (LKC_EXREQ | LKC_UPREQ))) {
			/*
			 * Immediate failure conditions
			 */
			if (extflags & LK_CANCELABLE) {
				if (count & LKC_CANCEL)
					return ENOLCK;
			}
			if (extflags & LK_NOWAIT)
				return EBUSY;

			/*
			 * Interlocked tsleep
			 */
			pflags = (extflags & LK_PCATCH) ? PCATCH : 0;
			timo = (extflags & LK_TIMELOCK) ? lkp->lk_timo : 0;

			tsleep_interlock(lkp, pflags);
			count = atomic_fetchadd_long(&lkp->lk_count, 0);

			if ((count & LKC_SHARED) &&
			    (count & (LKC_EXREQ | LKC_UPREQ))) {
				error = tsleep(lkp, pflags | PINTERLOCKED,
					       lkp->lk_wmesg, timo);
				if (error)
					return error;
				count = lkp->lk_count;
				cpu_ccfence();
				continue;
			}
			break;
		}
	}

	/*
	 * Bump the SCOUNT field.  The shared lock is granted only once
	 * the SHARED flag gets set.  If it is already set, we are done.
	 *
	 * (Racing an EXREQ or UPREQ operation is ok here, we already did
	 * our duty above).
	 */
	count = atomic_fetchadd_64(&lkp->lk_count, LKC_SCOUNT) + LKC_SCOUNT;
	error = 0;
	didloop = 0;

	for (;;) {
		/*
		 * We may be able to grant ourselves the bit trivially.
		 * We're done once the SHARED bit is granted.
		 */
		if ((count & (LKC_XMASK | LKC_EXREQ |
			      LKC_UPREQ | LKC_SHARED)) == 0) {
			if (atomic_fcmpset_64(&lkp->lk_count,
					      &count, count | LKC_SHARED)) {
				/* count |= LKC_SHARED; NOT USED */
				break;
			}
			continue;
		}
		if ((td->td_flags & TDF_DEADLKTREAT) &&
		    (count & (LKC_XMASK | LKC_SHARED)) == 0) {
			if (atomic_fcmpset_64(&lkp->lk_count,
					      &count, count | LKC_SHARED)) {
				/* count |= LKC_SHARED; NOT USED */
				break;
			}
			continue;
		}
		if (count & LKC_SHARED)
			break;

		/*
		 * Slow path
		 */
		pflags = (extflags & LK_PCATCH) ? PCATCH : 0;
		timo = (extflags & LK_TIMELOCK) ? lkp->lk_timo : 0;

		if (extflags & LK_CANCELABLE) {
			if (count & LKC_CANCEL) {
				undo_shreq(lkp);
				error = ENOLCK;
				break;
			}
		}
		if (extflags & LK_NOWAIT) {
			undo_shreq(lkp);
			error = EBUSY;
			break;
		}

		/*
		 * Interlocked after the first loop.
		 */
		if (didloop) {
			error = tsleep(lkp, pflags | PINTERLOCKED,
				       lkp->lk_wmesg, timo);
			if (extflags & LK_SLEEPFAIL) {
				undo_shreq(lkp);
				error = ENOLCK;
				break;
			}
			if (error) {
				undo_shreq(lkp);
				break;
			}
		}
		didloop = 1;

		/*
		 * Reload, shortcut grant case, then loop interlock
		 * and loop.
		 */
		count = lkp->lk_count;
		if (count & LKC_SHARED)
			break;
		tsleep_interlock(lkp, pflags);
		count = atomic_fetchadd_64(&lkp->lk_count, 0);
	}
	if (error == 0)
		COUNT(td, 1);

	return error;
}

/*
 * Acquire an exclusive lock
 */
int
lockmgr_exclusive(struct lock *lkp, u_int flags)
{
	uint64_t count;
	uint64_t ncount;
	uint32_t extflags;
	thread_t td;
	int error;
	int pflags;
	int timo;

	_lockmgr_assert(lkp, flags);
	extflags = (flags | lkp->lk_flags) & LK_EXTFLG_MASK;
	td = curthread;

	error = 0;
	count = lkp->lk_count;
	cpu_ccfence();

	/*
	 * Recursive lock if we already hold it exclusively.  Avoid testing
	 * lk_lockholder until after testing lk_count.
	 */
	if ((count & LKC_XMASK) && lkp->lk_lockholder == td) {
		if ((extflags & LK_CANRECURSE) == 0) {
			if (extflags & LK_NOWAIT)
				return EBUSY;
			panic("lockmgr: locking against myself");
		}
		count = atomic_fetchadd_64(&lkp->lk_count, 1) + 1;
		KKASSERT((count & LKC_XMASK) > 1);
		COUNT(td, 1);
		return 0;
	}

	/*
	 * Trivially acquire the lock, or block until we can set EXREQ.
	 * Set EXREQ2 if EXREQ is already set or the lock is already
	 * held exclusively.  EXREQ2 is an aggregation bit to request
	 * a wakeup.
	 *
	 * WARNING! We cannot set EXREQ if the lock is already held
	 *	    exclusively because it may race another EXREQ
	 *	    being cleared and granted.  We use the exclusivity
	 *	    to prevent both EXREQ and UPREQ from being set.
	 *
	 *	    This means that both shared and exclusive requests
	 *	    have equal priority against a current exclusive holder's
	 *	    release.  Exclusive requests still have priority over
	 *	    new shared requests when the lock is already held shared.
	 */
	for (;;) {
		/*
		 * Normal trivial case
		 */
		if ((count & (LKC_UPREQ | LKC_EXREQ |
			      LKC_XMASK)) == 0 &&
		    ((count & LKC_SHARED) == 0 ||
		     (count & LKC_SMASK) == 0)) {
			ncount = (count + 1) & ~LKC_SHARED;
			if (atomic_fcmpset_64(&lkp->lk_count,
					      &count, ncount)) {
				lkp->lk_lockholder = td;
				COUNT(td, 1);
				return 0;
			}
			continue;
		}

		if (extflags & LK_CANCELABLE) {
			if (count & LKC_CANCEL)
				return ENOLCK;
		}
		if (extflags & LK_NOWAIT)
			return EBUSY;

		/*
		 * Interlock to set EXREQ or EXREQ2
		 */
		pflags = (extflags & LK_PCATCH) ? PCATCH : 0;
		timo = (extflags & LK_TIMELOCK) ? lkp->lk_timo : 0;

		if (count & (LKC_EXREQ | LKC_XMASK))
			ncount = count | LKC_EXREQ2;
		else
			ncount = count | LKC_EXREQ;
		tsleep_interlock(lkp, pflags);
		if (atomic_fcmpset_64(&lkp->lk_count, &count, ncount)) {
			/*
			 * If we successfully transitioned to EXREQ we
			 * can break out, otherwise we had set EXREQ2 and
			 * we block.
			 */
			if ((count & (LKC_EXREQ | LKC_XMASK)) == 0) {
				count = ncount;
				break;
			}

			error = tsleep(lkp, pflags | PINTERLOCKED,
				       lkp->lk_wmesg, timo);
			count = lkp->lk_count;	/* relod */
			cpu_ccfence();
		}
#ifdef INVARIANTS
		if (lock_test_mode > 0) {
			--lock_test_mode;
			print_backtrace(8);
		}
#endif
		if (error)
			return error;
		if (extflags & LK_SLEEPFAIL)
			return ENOLCK;
	}

	/*
	 * Once EXREQ has been set, wait for it to be granted
	 * We enter the loop with tsleep_interlock() already called.
	 */
	for (;;) {
		/*
		 * Waiting for EXREQ to be granted to us.
		 *
		 * NOTE! If we try to trivially get the exclusive lock
		 *	 (basically by racing undo_shreq()) and succeed,
		 *	 we must still wakeup(lkp) for another exclusive
		 *	 lock trying to acquire EXREQ.  Easier to simply
		 *	 wait for our own wakeup.
		 */
		if ((count & LKC_EXREQ) == 0) {
			KKASSERT(count & LKC_XMASK);
			lkp->lk_lockholder = td;
			COUNT(td, 1);
			break;
		}

		/*
		 * Block waiting for our exreq to be granted.
		 * Check cancelation.  NOWAIT was already dealt with.
		 */
		if (extflags & LK_CANCELABLE) {
			if (count & LKC_CANCEL) {
				if (undo_exreq(lkp) == 0) {
					lkp->lk_lockholder = LK_KERNTHREAD;
					lockmgr_release(lkp, 0);
				}
				error = ENOLCK;
				break;
			}
		}

		pflags = (extflags & LK_PCATCH) ? PCATCH : 0;
		timo = (extflags & LK_TIMELOCK) ? lkp->lk_timo : 0;

		error = tsleep(lkp, pflags | PINTERLOCKED, lkp->lk_wmesg, timo);
#ifdef INVARIANTS
		if (lock_test_mode > 0) {
			--lock_test_mode;
			print_backtrace(8);
		}
#endif
		/*
		 * A tsleep error is uncommon.  If it occurs we have to
		 * undo our EXREQ.  If we are granted the exclusive lock
		 * as we try to undo we have to deal with it.
		 */
		if (extflags & LK_SLEEPFAIL) {
			if (undo_exreq(lkp) == 0) {
				lkp->lk_lockholder = LK_KERNTHREAD;
				lockmgr_release(lkp, 0);
			}
			if (error == 0)
				error = ENOLCK;
			break;
		}
		if (error) {
			if (undo_exreq(lkp))
				break;
			lkp->lk_lockholder = td;
			COUNT(td, 1);
			error = 0;
			break;
		}

		/*
		 * Reload after sleep, shortcut grant case.
		 * Then set the interlock and loop.
		 */
		count = lkp->lk_count;
		cpu_ccfence();
		if ((count & LKC_EXREQ) == 0) {
			KKASSERT(count & LKC_XMASK);
			lkp->lk_lockholder = td;
			COUNT(td, 1);
			break;
		}
		tsleep_interlock(lkp, pflags);
		count = atomic_fetchadd_64(&lkp->lk_count, 0);
	}
	return error;
}

/*
 * Downgrade an exclusive lock to shared.
 *
 * This function always succeeds as long as the caller owns a legal
 * exclusive lock with one reference.  UPREQ and EXREQ is ignored.
 */
int
lockmgr_downgrade(struct lock *lkp, u_int flags)
{
	uint64_t count;
	uint64_t ncount;
	uint32_t extflags;
	thread_t otd;
	thread_t td;

	extflags = (flags | lkp->lk_flags) & LK_EXTFLG_MASK;
	td = curthread;
	count = lkp->lk_count;

	for (;;) {
		cpu_ccfence();

		/*
		 * Downgrade an exclusive lock into a shared lock.  All
		 * counts on a recursive exclusive lock become shared.
		 *
		 * NOTE: Currently to reduce confusion we only allow
		 *	 there to be one exclusive lock count, and panic
		 *	 if there are more.
		 */
		if (lkp->lk_lockholder != td || (count & LKC_XMASK) != 1) {
			panic("lockmgr: not holding exclusive lock: "
			      "%p/%p %016jx", lkp->lk_lockholder, td, count);
		}

		/*
		 * NOTE! Must NULL-out lockholder before releasing the
		 *	 exclusive lock.
		 *
		 * NOTE! There might be pending shared requests, check
		 *	 and wake them up.
		 */
		otd = lkp->lk_lockholder;
		lkp->lk_lockholder = NULL;
		ncount = (count & ~(LKC_XMASK | LKC_EXREQ2)) +
			 ((count & LKC_XMASK) << LKC_SSHIFT);
		ncount |= LKC_SHARED;

		if (atomic_fcmpset_64(&lkp->lk_count, &count, ncount)) {
			/*
			 * Wakeup any shared waiters (prior SMASK), or
			 * any exclusive requests that couldn't set EXREQ
			 * because the lock had been held exclusively.
			 */
			if (count & (LKC_SMASK | LKC_EXREQ2))
				wakeup(lkp);
			/* count = ncount; NOT USED */
			break;
		}
		lkp->lk_lockholder = otd;
		/* retry */
	}
	return 0;
}

/*
 * Upgrade a shared lock to exclusive.  If LK_EXCLUPGRADE then guarantee
 * that no other exclusive requester can get in front of us and fail
 * immediately if another upgrade is pending.  If we fail, the shared
 * lock is released.
 *
 * If LK_EXCLUPGRADE is not set and we cannot upgrade because someone
 * else is in front of us, we release the shared lock and acquire the
 * exclusive lock normally.  If a failure occurs, the shared lock is
 * released.
 */
int
lockmgr_upgrade(struct lock *lkp, u_int flags)
{
	uint64_t count;
	uint64_t ncount;
	uint32_t extflags;
	thread_t td;
	int error;
	int pflags;
	int timo;

	_lockmgr_assert(lkp, flags);
	extflags = (flags | lkp->lk_flags) & LK_EXTFLG_MASK;
	td = curthread;
	error = 0;
	count = lkp->lk_count;
	cpu_ccfence();

	/*
	 * If we already hold the lock exclusively this operation
	 * succeeds and is a NOP.
	 */
	if (count & LKC_XMASK) {
		if (lkp->lk_lockholder == td)
			return 0;
		panic("lockmgr: upgrade unowned lock");
	}
	if ((count & LKC_SMASK) == 0)
		panic("lockmgr: upgrade unowned lock");

	/*
	 * Loop to acquire LKC_UPREQ
	 */
	for (;;) {
		/*
		 * If UPREQ is already pending, release the shared lock
		 * and acquire an exclusive lock normally.
		 *
		 * If NOWAIT or EXCLUPGRADE the operation must be atomic,
		 * and this isn't, so we fail.
		 */
		if (count & LKC_UPREQ) {
			lockmgr_release(lkp, 0);
			if ((flags & LK_TYPE_MASK) == LK_EXCLUPGRADE)
				error = EBUSY;
			else if (extflags & LK_NOWAIT)
				error = EBUSY;
			else
				error = lockmgr_exclusive(lkp, flags);
			return error;
		}

		/*
		 * Try to immediately grant the upgrade, handle NOWAIT,
		 * or release the shared lock and simultaneously set UPREQ.
		 */
		if ((count & LKC_SMASK) == LKC_SCOUNT) {
			/*
			 * Immediate grant
			 */
			ncount = (count - LKC_SCOUNT + 1) & ~LKC_SHARED;
			if (atomic_fcmpset_64(&lkp->lk_count, &count, ncount)) {
				lkp->lk_lockholder = td;
				return 0;
			}
		} else if (extflags & LK_NOWAIT) {
			/*
			 * Early EBUSY if an immediate grant is impossible
			 */
			lockmgr_release(lkp, 0);
			return EBUSY;
		} else {
			/*
			 * Multiple shared locks present, request the
			 * upgrade and break to the next loop.
			 */
			pflags = (extflags & LK_PCATCH) ? PCATCH : 0;
			tsleep_interlock(lkp, pflags);
			ncount = (count - LKC_SCOUNT) | LKC_UPREQ;
			if (atomic_fcmpset_64(&lkp->lk_count, &count, ncount)) {
				count = ncount;
				break;
			}
		}
		/* retry */
	}

	/*
	 * We have acquired LKC_UPREQ, wait until the upgrade is granted
	 * or the tsleep fails.
	 *
	 * NOWAIT and EXCLUPGRADE have already been handled.  The first
	 * tsleep_interlock() has already been associated.
	 */
	for (;;) {
		cpu_ccfence();

		/*
		 * We were granted our upgrade.  No other UPREQ can be
		 * made pending because we are now exclusive.
		 */
		if ((count & LKC_UPREQ) == 0) {
			KKASSERT((count & LKC_XMASK) == 1);
			lkp->lk_lockholder = td;
			break;
		}

		if (extflags & LK_CANCELABLE) {
			if (count & LKC_CANCEL) {
				if (undo_upreq(lkp) == 0) {
					lkp->lk_lockholder = LK_KERNTHREAD;
					lockmgr_release(lkp, 0);
				}
				error = ENOLCK;
				break;
			}
		}

		pflags = (extflags & LK_PCATCH) ? PCATCH : 0;
		timo = (extflags & LK_TIMELOCK) ? lkp->lk_timo : 0;

		error = tsleep(lkp, pflags | PINTERLOCKED, lkp->lk_wmesg, timo);
		if (extflags & LK_SLEEPFAIL) {
			if (undo_upreq(lkp) == 0) {
				lkp->lk_lockholder = LK_KERNTHREAD;
				lockmgr_release(lkp, 0);
			}
			if (error == 0)
				error = ENOLCK;
			break;
		}
		if (error) {
			if (undo_upreq(lkp))
				break;
			error = 0;
		}

		/*
		 * Reload the lock, short-cut the UPGRANT code before
		 * taking the time to interlock and loop.
		 */
		count = lkp->lk_count;
		if ((count & LKC_UPREQ) == 0) {
			KKASSERT((count & LKC_XMASK) == 1);
			lkp->lk_lockholder = td;
			break;
		}
		tsleep_interlock(lkp, pflags);
		count = atomic_fetchadd_64(&lkp->lk_count, 0);
		/* retry */
	}
	return error;
}

/*
 * Release a held lock
 *
 * NOTE: When releasing to an unlocked state, we set the SHARED bit
 *	 to optimize shared lock requests.
 */
int
lockmgr_release(struct lock *lkp, u_int flags)
{
	uint64_t count;
	uint64_t ncount;
	uint32_t extflags;
	thread_t otd;
	thread_t td;

	extflags = (flags | lkp->lk_flags) & LK_EXTFLG_MASK;
	td = curthread;

	count = lkp->lk_count;
	cpu_ccfence();

	for (;;) {
		/*
		 * Release the currently held lock, grant all requests
		 * possible.
		 *
		 * WARNING! lksleep() assumes that LK_RELEASE does not
		 *	    block.
		 *
		 * Always succeeds.
		 * Never blocks.
		 */
		if ((count & (LKC_SMASK | LKC_XMASK)) == 0)
			panic("lockmgr: LK_RELEASE: no lock held");

		if (count & LKC_XMASK) {
			/*
			 * Release exclusively held lock
			 */
			if (lkp->lk_lockholder != LK_KERNTHREAD &&
			    lkp->lk_lockholder != td) {
				panic("lockmgr: pid %d, not exclusive "
				      "lock holder thr %p/%p unlocking",
				    (td->td_proc ? td->td_proc->p_pid : -1),
				    td, lkp->lk_lockholder);
			}
			if ((count & (LKC_UPREQ | LKC_EXREQ |
				      LKC_XMASK)) == 1) {
				/*
				 * Last exclusive count is being released
				 * with no UPREQ or EXREQ.  The SHARED
				 * bit can be set or not without messing
				 * anything up, so precondition it to
				 * SHARED (which is the most cpu-optimal).
				 *
				 * Wakeup any EXREQ2.  EXREQ cannot be
				 * set while an exclusive count is present
				 * so we have to wakeup any EXREQ2 we find.
				 *
				 * We could hint the EXREQ2 by leaving
				 * SHARED unset, but atm I don't see any
				 * usefulness.
				 */
				otd = lkp->lk_lockholder;
				lkp->lk_lockholder = NULL;
				ncount = (count - 1);
				ncount &= ~(LKC_CANCEL | LKC_EXREQ2);
				ncount |= LKC_SHARED;
				if (atomic_fcmpset_64(&lkp->lk_count,
						      &count, ncount)) {
					if (count & (LKC_SMASK | LKC_EXREQ2))
						wakeup(lkp);
					if (otd != LK_KERNTHREAD)
						COUNT(td, -1);
					/* count = ncount; NOT USED */
					break;
				}
				lkp->lk_lockholder = otd;
				/* retry */
			} else if ((count & (LKC_UPREQ | LKC_XMASK)) ==
				   (LKC_UPREQ | 1)) {
				/*
				 * Last exclusive count is being released but
				 * an upgrade request is present, automatically
				 * grant an exclusive state to the owner of
				 * the upgrade request.  Transfer count to
				 * grant.
				 *
				 * EXREQ cannot be set while an exclusive
				 * holder exists, so do not clear EXREQ2.
				 */
				otd = lkp->lk_lockholder;
				lkp->lk_lockholder = NULL;
				ncount = count & ~LKC_UPREQ;
				if (atomic_fcmpset_64(&lkp->lk_count,
						      &count, ncount)) {
					wakeup(lkp);
					if (otd != LK_KERNTHREAD)
						COUNT(td, -1);
					/* count = ncount; NOT USED */
					break;
				}
				lkp->lk_lockholder = otd;
				/* retry */
			} else if ((count & (LKC_EXREQ | LKC_XMASK)) ==
				   (LKC_EXREQ | 1)) {
				/*
				 * Last exclusive count is being released but
				 * an exclusive request is present.  We
				 * automatically grant an exclusive state to
				 * the owner of the exclusive request,
				 * transfering our count.
				 *
				 * This case virtually never occurs because
				 * EXREQ is not set while exclusive holders
				 * exist.  However, it might be set if a
				 * an exclusive request is pending and a
				 * shared holder upgrades.
				 *
				 * Don't bother clearing EXREQ2.  A thread
				 * waiting to set EXREQ can't do it while
				 * an exclusive lock is present.
				 */
				otd = lkp->lk_lockholder;
				lkp->lk_lockholder = NULL;
				ncount = count & ~LKC_EXREQ;
				if (atomic_fcmpset_64(&lkp->lk_count,
						      &count, ncount)) {
					wakeup(lkp);
					if (otd != LK_KERNTHREAD)
						COUNT(td, -1);
					/* count = ncount; NOT USED */
					break;
				}
				lkp->lk_lockholder = otd;
				/* retry */
			} else {
				/*
				 * Multiple exclusive counts, drop by 1.
				 * Since we are the holder and there is more
				 * than one count, we can just decrement it.
				 */
				count =
				    atomic_fetchadd_long(&lkp->lk_count, -1);
				/* count = count - 1  NOT NEEDED */
				if (lkp->lk_lockholder != LK_KERNTHREAD)
					COUNT(td, -1);
				break;
			}
			/* retry */
		} else {
			/*
			 * Release shared lock
			 */
			KKASSERT((count & LKC_SHARED) && (count & LKC_SMASK));
			if ((count & (LKC_EXREQ | LKC_UPREQ | LKC_SMASK)) ==
			    LKC_SCOUNT) {
				/*
				 * Last shared count is being released,
				 * no exclusive or upgrade request present.
				 * Generally leave the shared bit set.
				 * Clear the CANCEL bit.
				 */
				ncount = (count - LKC_SCOUNT) & ~LKC_CANCEL;
				if (atomic_fcmpset_64(&lkp->lk_count,
						      &count, ncount)) {
					COUNT(td, -1);
					/* count = ncount; NOT USED */
					break;
				}
				/* retry */
			} else if ((count & (LKC_UPREQ | LKC_SMASK)) ==
				   (LKC_UPREQ | LKC_SCOUNT)) {
				/*
				 * Last shared count is being released but
				 * an upgrade request is present, automatically
				 * grant an exclusive state to the owner of
				 * the upgrade request and transfer the count.
				 */
				ncount = (count - LKC_SCOUNT + 1) &
					 ~(LKC_UPREQ | LKC_CANCEL | LKC_SHARED);
				if (atomic_fcmpset_64(&lkp->lk_count,
						      &count, ncount)) {
					wakeup(lkp);
					COUNT(td, -1);
					/* count = ncount; NOT USED */
					break;
				}
				/* retry */
			} else if ((count & (LKC_EXREQ | LKC_SMASK)) ==
				   (LKC_EXREQ | LKC_SCOUNT)) {
				/*
				 * Last shared count is being released but
				 * an exclusive request is present, we
				 * automatically grant an exclusive state to
				 * the owner of the request and transfer
				 * the count.
				 */
				ncount = (count - LKC_SCOUNT + 1) &
					 ~(LKC_EXREQ | LKC_EXREQ2 |
					   LKC_CANCEL | LKC_SHARED);
				if (atomic_fcmpset_64(&lkp->lk_count,
						      &count, ncount)) {
					wakeup(lkp);
					COUNT(td, -1);
					/* count = ncount; NOT USED */
					break;
				}
				/* retry */
			} else {
				/*
				 * Shared count is greater than 1.  We can
				 * just use undo_shreq() to clean things up.
				 * undo_shreq() will also handle races to 0
				 * after the fact.
				 */
				undo_shreq(lkp);
				COUNT(td, -1);
				break;
			}
			/* retry */
		}
		/* retry */
	}
	return 0;
}

/*
 * Start canceling blocked requesters or later requestors.
 * Only blocked requesters using CANCELABLE can be canceled.
 *
 * This is intended to then allow other requesters (usually the
 * caller) to obtain a non-cancelable lock.
 *
 * Don't waste time issuing a wakeup if nobody is pending.
 */
int
lockmgr_cancel_beg(struct lock *lkp, u_int flags)
{
	uint64_t count;

	count = lkp->lk_count;
	for (;;) {
		cpu_ccfence();

		KKASSERT((count & LKC_CANCEL) == 0);	/* disallowed case */

		/* issue w/lock held */
		KKASSERT((count & (LKC_XMASK | LKC_SMASK)) != 0);

		if (!atomic_fcmpset_64(&lkp->lk_count,
				       &count, count | LKC_CANCEL)) {
			continue;
		}
		/* count |= LKC_CANCEL; NOT USED */

		/*
		 * Wakeup any waiters.
		 *
		 * NOTE: EXREQ2 only matters when EXREQ is set, so don't
		 *	 bother checking EXREQ2.
		 */
		if (count & (LKC_EXREQ | LKC_SMASK | LKC_UPREQ)) {
			wakeup(lkp);
		}
		break;
	}
	return 0;
}

/*
 * End our cancel request (typically after we have acquired
 * the lock ourselves).
 */
int
lockmgr_cancel_end(struct lock *lkp, u_int flags)
{
	atomic_clear_long(&lkp->lk_count, LKC_CANCEL);

	return 0;
}

/*
 * Backout SCOUNT from a failed shared lock attempt and handle any race
 * to 0.  This function is also used by the release code for the less
 * optimal race to 0 case.
 *
 * WARNING! Since we are unconditionally decrementing LKC_SCOUNT, it is
 *	    possible for the lock to get into a LKC_SHARED + ZERO SCOUNT
 *	    situation.  A shared request can block with a ZERO SCOUNT if
 *	    EXREQ or UPREQ is pending in this situation.  Be sure to always
 *	    issue a wakeup() in this situation if we are unable to
 *	    transition to an exclusive lock, to handle the race.
 *
 * Always succeeds
 * Must not block
 */
static void
undo_shreq(struct lock *lkp)
{
	uint64_t count;
	uint64_t ncount;

	count = atomic_fetchadd_64(&lkp->lk_count, -LKC_SCOUNT) - LKC_SCOUNT;
	while ((count & (LKC_EXREQ | LKC_UPREQ | LKC_CANCEL)) &&
	       (count & (LKC_SMASK | LKC_XMASK)) == 0) {
		/*
		 * Note that UPREQ must have priority over EXREQ, and EXREQ
		 * over CANCEL, so if the atomic op fails we have to loop up.
		 */
		if (count & LKC_UPREQ) {
			ncount = (count + 1) & ~(LKC_UPREQ | LKC_CANCEL |
						 LKC_SHARED);
			if (atomic_fcmpset_64(&lkp->lk_count, &count, ncount)) {
				wakeup(lkp);
				/* count = ncount; NOT USED */
				break;
			}
			wakeup(lkp);
			continue;
		}
		if (count & LKC_EXREQ) {
			ncount = (count + 1) & ~(LKC_EXREQ | LKC_EXREQ2 |
						 LKC_CANCEL | LKC_SHARED);
			if (atomic_fcmpset_64(&lkp->lk_count, &count, ncount)) {
				wakeup(lkp);
				/* count = ncount; NOT USED */
				break;
			}
			wakeup(lkp);
			continue;
		}
		if (count & LKC_CANCEL) {
			ncount = count & ~LKC_CANCEL;
			if (atomic_fcmpset_64(&lkp->lk_count, &count, ncount)) {
				wakeup(lkp);
				/* count = ncount; NOT USED */
				break;
			}
		}
		/* retry */
	}
}

/*
 * Undo an exclusive request.  Returns EBUSY if we were able to undo the
 * request, and 0 if the request was granted before we could undo it.
 * When 0 is returned, the lock state has not been modified.  The caller
 * is responsible for setting the lockholder to curthread.
 */
static
int
undo_exreq(struct lock *lkp)
{
	uint64_t count;
	uint64_t ncount;
	int error;

	count = lkp->lk_count;
	error = 0;

	for (;;) {
		cpu_ccfence();

		if ((count & LKC_EXREQ) == 0) {
			/*
			 * EXREQ was granted.  We own the exclusive lock.
			 */
			break;
		}
		if (count & LKC_XMASK) {
			/*
			 * Clear the EXREQ we still own.  Only wakeup on
			 * EXREQ2 if no UPREQ.  There are still exclusive
			 * holders so do not wake up any shared locks or
			 * any UPREQ.
			 *
			 * If there is an UPREQ it will issue a wakeup()
			 * for any EXREQ wait looops, so we can clear EXREQ2
			 * now.
			 */
			ncount = count & ~(LKC_EXREQ | LKC_EXREQ2);
			if (atomic_fcmpset_64(&lkp->lk_count, &count, ncount)) {
				if ((count & (LKC_EXREQ2 | LKC_UPREQ)) ==
				    LKC_EXREQ2) {
					wakeup(lkp);
				}
				error = EBUSY;
				/* count = ncount; NOT USED */
				break;
			}
			/* retry */
		} else if (count & LKC_UPREQ) {
			/*
			 * Clear the EXREQ we still own.  We cannot wakeup any
			 * shared or exclusive waiters because there is an
			 * uprequest pending (that we do not handle here).
			 *
			 * If there is an UPREQ it will issue a wakeup()
			 * for any EXREQ wait looops, so we can clear EXREQ2
			 * now.
			 */
			ncount = count & ~(LKC_EXREQ | LKC_EXREQ2);
			if (atomic_fcmpset_64(&lkp->lk_count, &count, ncount)) {
				error = EBUSY;
				break;
			}
			/* retry */
		} else if ((count & LKC_SHARED) && (count & LKC_SMASK)) {
			/*
			 * No UPREQ, lock not held exclusively, but the lock
			 * is held shared.  Clear EXREQ, wakeup anyone trying
			 * to get the EXREQ bit (they have to set it
			 * themselves, EXREQ2 is an aggregation).
			 *
			 * We must also wakeup any shared locks blocked
			 * by the EXREQ, so just issue the wakeup
			 * unconditionally.  See lockmgr_shared() + 76 lines
			 * or so.
			 */
			ncount = count & ~(LKC_EXREQ | LKC_EXREQ2);
			if (atomic_fcmpset_64(&lkp->lk_count, &count, ncount)) {
				wakeup(lkp);
				error = EBUSY;
				/* count = ncount; NOT USED */
				break;
			}
			/* retry */
		} else {
			/*
			 * No UPREQ, lock not held exclusively or shared.
			 * Grant the EXREQ and wakeup anyone waiting on
			 * EXREQ2.
			 *
			 * We must also issue a wakeup if SHARED is set,
			 * even without an SCOUNT, due to pre-shared blocking
			 * that can occur on EXREQ in lockmgr_shared().
			 */
			ncount = (count + 1) & ~(LKC_EXREQ | LKC_EXREQ2);
			if (atomic_fcmpset_64(&lkp->lk_count, &count, ncount)) {
				if (count & (LKC_EXREQ2 | LKC_SHARED))
					wakeup(lkp);
				/* count = ncount; NOT USED */
				/* we are granting, error == 0 */
				break;
			}
			/* retry */
		}
		/* retry */
	}
	return error;
}

/*
 * Undo an upgrade request.  Returns EBUSY if we were able to undo the
 * request, and 0 if the request was granted before we could undo it.
 * When 0 is returned, the lock state has not been modified.  The caller
 * is responsible for setting the lockholder to curthread.
 */
static
int
undo_upreq(struct lock *lkp)
{
	uint64_t count;
	uint64_t ncount;
	int error;

	count = lkp->lk_count;
	error = 0;

	for (;;) {
		cpu_ccfence();

		if ((count & LKC_UPREQ) == 0) {
			/*
			 * UPREQ was granted
			 */
			break;
		}
		if (count & LKC_XMASK) {
			/*
			 * Clear the UPREQ we still own.  Nobody to wakeup
			 * here because there is an existing exclusive
			 * holder.
			 */
			if (atomic_fcmpset_64(&lkp->lk_count, &count,
					      count & ~LKC_UPREQ)) {
				error = EBUSY;
				/* count &= ~LKC_UPREQ; NOT USED */
				break;
			}
		} else if (count & LKC_EXREQ) {
			/*
			 * Clear the UPREQ we still own.  Grant the exclusive
			 * request and wake it up.
			 */
			ncount = (count + 1);
			ncount &= ~(LKC_EXREQ | LKC_EXREQ2 | LKC_UPREQ);

			if (atomic_fcmpset_64(&lkp->lk_count, &count, ncount)) {
				wakeup(lkp);
				error = EBUSY;
				/* count = ncount; NOT USED */
				break;
			}
		} else {
			/*
			 * Clear the UPREQ we still own.  Wakeup any shared
			 * waiters.
			 *
			 * We must also issue a wakeup if SHARED was set
			 * even if no shared waiters due to pre-shared blocking
			 * that can occur on UPREQ.
			 */
			ncount = count & ~LKC_UPREQ;
			if (count & LKC_SMASK)
				ncount |= LKC_SHARED;

			if (atomic_fcmpset_64(&lkp->lk_count, &count, ncount)) {
				if ((count & LKC_SHARED) ||
				    (ncount & LKC_SHARED)) {
					wakeup(lkp);
				}
				error = EBUSY;
				/* count = ncount; NOT USED */
				break;
			}
		}
		/* retry */
	}
	return error;
}

void
lockmgr_kernproc(struct lock *lp)
{
	struct thread *td __debugvar = curthread;

	if (lp->lk_lockholder != LK_KERNTHREAD) {
		KASSERT(lp->lk_lockholder == td,
		    ("lockmgr_kernproc: lock not owned by curthread %p: %p",
		    td, lp->lk_lockholder));
		lp->lk_lockholder = LK_KERNTHREAD;
		COUNT(td, -1);
	}
}

/*
 * Initialize a lock; required before use.
 */
void
lockinit(struct lock *lkp, const char *wmesg, int timo, int flags)
{
	lkp->lk_flags = (flags & LK_EXTFLG_MASK);
	lkp->lk_count = 0;
	lkp->lk_wmesg = wmesg;
	lkp->lk_timo = timo;
	lkp->lk_lockholder = NULL;
}

/*
 * Reinitialize a lock that is being reused for a different purpose, but
 * which may have pending (blocked) threads sitting on it.  The caller
 * must already hold the interlock.
 */
void
lockreinit(struct lock *lkp, const char *wmesg, int timo, int flags)
{
	lkp->lk_wmesg = wmesg;
	lkp->lk_timo = timo;
}

/*
 * De-initialize a lock.  The structure must no longer be used by anyone.
 */
void
lockuninit(struct lock *lkp)
{
	uint64_t count __unused;

	count = lkp->lk_count;
	cpu_ccfence();
	KKASSERT((count & (LKC_EXREQ | LKC_UPREQ)) == 0 &&
		 ((count & LKC_SHARED) || (count & LKC_SMASK) == 0));
}

/*
 * Determine the status of a lock.
 */
int
lockstatus(struct lock *lkp, struct thread *td)
{
	int lock_type = 0;
	uint64_t count;

	count = lkp->lk_count;
	cpu_ccfence();

	if (count & (LKC_XMASK | LKC_SMASK | LKC_EXREQ | LKC_UPREQ)) {
		if (count & LKC_XMASK) {
			if (td == NULL || lkp->lk_lockholder == td)
				lock_type = LK_EXCLUSIVE;
			else
				lock_type = LK_EXCLOTHER;
		} else if ((count & LKC_SMASK) && (count & LKC_SHARED)) {
			lock_type = LK_SHARED;
		}
	}
	return (lock_type);
}

/*
 * Return non-zero if the caller owns the lock shared or exclusive.
 * We can only guess re: shared locks.
 */
int
lockowned(struct lock *lkp)
{
	thread_t td = curthread;
	uint64_t count;

	count = lkp->lk_count;
	cpu_ccfence();

	if (count & LKC_XMASK)
		return(lkp->lk_lockholder == td);
	else
		return((count & LKC_SMASK) != 0);
}

#if 0
/*
 * Determine the number of holders of a lock.
 *
 * REMOVED - Cannot be used due to our use of atomic_fetchadd_64()
 *	     for shared locks.  Caller can only test if the lock has
 *	     a count or not using lockinuse(lk) (sys/lock.h)
 */
int
lockcount(struct lock *lkp)
{
	panic("lockcount cannot be used");
}

int
lockcountnb(struct lock *lkp)
{
	panic("lockcount cannot be used");
}
#endif

/*
 * Print out information about state of a lock. Used by VOP_PRINT
 * routines to display status about contained locks.
 */
void
lockmgr_printinfo(struct lock *lkp)
{
	struct thread *td = lkp->lk_lockholder;
	struct proc *p;
	uint64_t count;

	count = lkp->lk_count;
	cpu_ccfence();

	if (td && td != LK_KERNTHREAD)
		p = td->td_proc;
	else
		p = NULL;

	if (count & LKC_XMASK) {
		kprintf(" lock type %s: EXCLUS (count %016jx) by td %p pid %d",
		    lkp->lk_wmesg, (intmax_t)count, td,
		    p ? p->p_pid : -99);
	} else if ((count & LKC_SMASK) && (count & LKC_SHARED)) {
		kprintf(" lock type %s: SHARED (count %016jx)",
		    lkp->lk_wmesg, (intmax_t)count);
	} else {
		kprintf(" lock type %s: NOTHELD", lkp->lk_wmesg);
	}
	if ((count & (LKC_EXREQ | LKC_UPREQ)) ||
	    ((count & LKC_XMASK) && (count & LKC_SMASK)))
		kprintf(" with waiters\n");
	else
		kprintf("\n");
}

void
lock_sysinit(struct lock_args *arg)
{
	lockinit(arg->la_lock, arg->la_desc, 0, arg->la_flags);
}

#ifdef DEBUG_CANCEL_LOCKS

static
int
sysctl_cancel_lock(SYSCTL_HANDLER_ARGS)
{
	int error;

	if (req->newptr) {
		SYSCTL_XUNLOCK();
		lockmgr(&cancel_lk, LK_EXCLUSIVE);
		error = tsleep(&error, PCATCH, "canmas", hz * 5);
		lockmgr(&cancel_lk, LK_CANCEL_BEG);
		error = tsleep(&error, PCATCH, "canmas", hz * 5);
		lockmgr(&cancel_lk, LK_RELEASE);
		SYSCTL_XLOCK();
		SYSCTL_OUT(req, &error, sizeof(error));
	}
	error = 0;

	return error;
}

static
int
sysctl_cancel_test(SYSCTL_HANDLER_ARGS)
{
	int error;

	if (req->newptr) {
		error = lockmgr(&cancel_lk, LK_EXCLUSIVE|LK_CANCELABLE);
		if (error == 0)
			lockmgr(&cancel_lk, LK_RELEASE);
		SYSCTL_OUT(req, &error, sizeof(error));
		kprintf("test %d\n", error);
	}

	return 0;
}

#endif
