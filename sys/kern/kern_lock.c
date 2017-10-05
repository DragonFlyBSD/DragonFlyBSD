/* 
 * Copyright (c) 1995
 *	The Regents of the University of California.  All rights reserved.
 * Copyright (C) 1997
 *	John S. Dyson.  All rights reserved.
 * Copyright (C) 2013-2014
 *	Matthew Dillon, All rights reserved.
 *
 * This code contains ideas from software contributed to Berkeley by
 * Avadis Tevanian, Jr., Michael Wayne Young, and the Mach Operating
 * System project at Carnegie-Mellon University.
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

static void undo_upreq(struct lock *lkp);

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
#define COUNT(td, x)
#endif

static int lockmgr_waitupgrade(struct lock *lkp, u_int flags);

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
#ifndef DEBUG_LOCKS
		panic("lockmgr %s from %p: called from interrupt, ipi, "
		      "or hard code section",
		      lkp->lk_wmesg, ((int **)&lkp)[-1]);
#else
		panic("lockmgr %s from %s:%d: called from interrupt, ipi, "
		      "or hard code section",
		      lkp->lk_wmesg, file, line);
#endif
	}

#ifdef DEBUG_LOCKS
	if (mycpu->gd_spinlocks && ((flags & LK_NOWAIT) == 0)) {
		panic("lockmgr %s from %s:%d: called with %d spinlocks held",
		      lkp->lk_wmesg, file, line, mycpu->gd_spinlocks);
	}
#endif
}

/*
 * Acquire a shared lock
 */
int
lockmgr_shared(struct lock *lkp, u_int flags)
{
	uint32_t extflags;
	thread_t td;
	int count;
	int error;
	int pflags;
	int wflags;
	int timo;

	_lockmgr_assert(lkp, flags);
	extflags = (flags | lkp->lk_flags) & LK_EXTFLG_MASK;
	td = curthread;
	error = 0;

	for (;;) {
		count = lkp->lk_count;
		cpu_ccfence();

		/*
		 * Normal case
		 */
		if ((count & (LKC_EXREQ|LKC_UPREQ|LKC_EXCL)) == 0) {
			if (atomic_cmpset_int(&lkp->lk_count,
					      count, count + 1)) {
				COUNT(td, 1);
				break;
			}
			continue;
		}

		/*
		 * If the caller already holds the lock exclusively then
		 * we silently obtain another count on the exclusive lock.
		 *
		 * WARNING!  The old FreeBSD behavior was to downgrade,
		 *	     but this creates a problem when recursions
		 *	     return to the caller and the caller expects
		 *	     its original exclusive lock to remain exclusively
		 *	     locked.
		 */
		if (lkp->lk_lockholder == td) {
			KKASSERT(count & LKC_EXCL);
			if ((extflags & LK_CANRECURSE) == 0) {
				if (extflags & LK_NOWAIT) {
					error = EBUSY;
					break;
				}
				panic("lockmgr: locking against myself");
			}
			atomic_add_int(&lkp->lk_count, 1);
			COUNT(td, 1);
			break;
		}

		/*
		 * Slow path
		 */
		pflags = (extflags & LK_PCATCH) ? PCATCH : 0;
		timo = (extflags & LK_TIMELOCK) ? lkp->lk_timo : 0;
		wflags = (td->td_flags & TDF_DEADLKTREAT) ?
				LKC_EXCL : (LKC_EXCL|LKC_EXREQ|LKC_UPREQ);

		/*
		 * Block while the lock is held exclusively or, conditionally,
		 * if other threads are trying to obtain an exclusive lock or
		 * upgrade to one.
		 */
		if (count & wflags) {
			if (extflags & LK_CANCELABLE) {
				if (count & LKC_CANCEL) {
					error = ENOLCK;
					break;
				}
			}
			if (extflags & LK_NOWAIT) {
				error = EBUSY;
				break;
			}

			if ((extflags & LK_NOCOLLSTATS) == 0) {
				indefinite_info_t info;

				flags |= LK_NOCOLLSTATS;
				indefinite_init(&info, lkp->lk_wmesg, 1, 'l');
				error = lockmgr_shared(lkp, flags);
				indefinite_done(&info);
				break;
			}

			tsleep_interlock(lkp, pflags);
			if (!atomic_cmpset_int(&lkp->lk_count, count,
					      count | LKC_SHREQ)) {
				continue;
			}
			error = tsleep(lkp, pflags | PINTERLOCKED,
				       lkp->lk_wmesg, timo);
			if (error)
				break;
			if (extflags & LK_SLEEPFAIL) {
				error = ENOLCK;
				break;
			}
			continue;
		}

		/*
		 * Otherwise we can bump the count
		 */
		if (atomic_cmpset_int(&lkp->lk_count, count, count + 1)) {
			COUNT(td, 1);
			break;
		}
		/* retry */
	}
	return error;
}

/*
 * Acquire an exclusive lock
 */
int
lockmgr_exclusive(struct lock *lkp, u_int flags)
{
	uint32_t extflags;
	thread_t td;
	int count;
	int error;
	int pflags;
	int timo;

	_lockmgr_assert(lkp, flags);
	extflags = (flags | lkp->lk_flags) & LK_EXTFLG_MASK;
	td = curthread;

	error = 0;

	for (;;) {
		count = lkp->lk_count;
		cpu_ccfence();

		/*
		 * Exclusive lock critical path.
		 */
		if (count == 0) {
			if (atomic_cmpset_int(&lkp->lk_count, count,
					      LKC_EXCL | (count + 1))) {
				lkp->lk_lockholder = td;
				COUNT(td, 1);
				break;
			}
			continue;
		}

		/*
		 * Recursive lock if we already hold it exclusively.
		 */
		if (lkp->lk_lockholder == td) {
			KKASSERT(count & LKC_EXCL);
			if ((extflags & LK_CANRECURSE) == 0) {
				if (extflags & LK_NOWAIT) {
					error = EBUSY;
					break;
				}
				panic("lockmgr: locking against myself");
			}
			atomic_add_int(&lkp->lk_count, 1);
			COUNT(td, 1);
			break;
		}

		/*
		 * We will block, handle LK_NOWAIT
		 */
		if (extflags & LK_NOWAIT) {
			error = EBUSY;
			break;
		}
		if (extflags & LK_CANCELABLE) {
			if (count & LKC_CANCEL) {
				error = ENOLCK;
				break;
			}
		}

		if ((extflags & LK_NOCOLLSTATS) == 0) {
			indefinite_info_t info;

			flags |= LK_NOCOLLSTATS;
			indefinite_init(&info, lkp->lk_wmesg, 1, 'L');
			error = lockmgr_exclusive(lkp, flags);
			indefinite_done(&info);
			break;
		}

		/*
		 * Wait until we can obtain the exclusive lock.  EXREQ is
		 * automatically cleared when all current holders release
		 * so if we abort the operation we can safely leave it set.
		 * There might be other exclusive requesters.
		 */
		pflags = (extflags & LK_PCATCH) ? PCATCH : 0;
		timo = (extflags & LK_TIMELOCK) ? lkp->lk_timo : 0;

		tsleep_interlock(lkp, pflags);
		if (!atomic_cmpset_int(&lkp->lk_count, count,
				       count | LKC_EXREQ)) {
			continue;
		}

		error = tsleep(lkp, pflags | PINTERLOCKED,
			       lkp->lk_wmesg, timo);
		if (error)
			break;
		if (extflags & LK_SLEEPFAIL) {
			error = ENOLCK;
			break;
		}
		/* retry */
	}
	return error;
}

/*
 * Downgrade an exclusive lock to shared
 */
int
lockmgr_downgrade(struct lock *lkp, u_int flags)
{
	uint32_t extflags;
	thread_t otd;
	thread_t td;
	int count;

	extflags = (flags | lkp->lk_flags) & LK_EXTFLG_MASK;
	td = curthread;

	for (;;) {
		count = lkp->lk_count;
		cpu_ccfence();

		/*
		 * Downgrade an exclusive lock into a shared lock.  All
		 * counts on a recursive exclusive lock become shared.
		 *
		 * This function always succeeds.
		 */
		if (lkp->lk_lockholder != td ||
		    (count & (LKC_EXCL|LKC_MASK)) != (LKC_EXCL|1)) {
			panic("lockmgr: not holding exclusive lock");
		}

		/*
		 * NOTE! Must NULL-out lockholder before releasing LKC_EXCL.
		 */
		otd = lkp->lk_lockholder;
		lkp->lk_lockholder = NULL;
		if (atomic_cmpset_int(&lkp->lk_count, count,
				      count & ~(LKC_EXCL|LKC_SHREQ))) {
			if (count & LKC_SHREQ)
				wakeup(lkp);
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
 * immediately if another upgrade is pending.
 */
int
lockmgr_upgrade(struct lock *lkp, u_int flags)
{
	uint32_t extflags;
	thread_t td;
	int count;
	int error;
	int pflags;
	int wflags;
	int timo;

	_lockmgr_assert(lkp, flags);
	extflags = (flags | lkp->lk_flags) & LK_EXTFLG_MASK;
	td = curthread;
	error = 0;

	for (;;) {
		count = lkp->lk_count;
		cpu_ccfence();

		/*
		 * Upgrade from a single shared lock to an exclusive lock.
		 *
		 * If another process is ahead of us to get an upgrade,
		 * then we want to fail rather than have an intervening
		 * exclusive access.  The shared lock is released on
		 * failure.
		 */
		if ((flags & LK_TYPE_MASK) == LK_EXCLUPGRADE) {
			if (count & LKC_UPREQ) {
				lockmgr_release(lkp, LK_RELEASE);
				error = EBUSY;
				break;
			}
		}
		/* fall through into normal upgrade */

		/*
		 * Upgrade a shared lock to an exclusive one.  This can cause
		 * the lock to be temporarily released and stolen by other
		 * threads.  LK_SLEEPFAIL or LK_NOWAIT may be used to detect
		 * this case, or use LK_EXCLUPGRADE.
		 *
		 * If the lock is already exclusively owned by us, this
		 * operation is a NOP.
		 *
		 * If we return an error (even NOWAIT), the current lock will
		 * be released.
		 *
		 * Start with the critical path.
		 */
		if ((count & (LKC_UPREQ|LKC_EXCL|LKC_MASK)) == 1) {
			if (atomic_cmpset_int(&lkp->lk_count, count,
					      count | LKC_EXCL)) {
				lkp->lk_lockholder = td;
				break;
			}
			continue;
		}

		/*
		 * We own a lock coming into this, so there cannot be an
		 * UPGRANT already flagged.
		 */
		KKASSERT((count & LKC_UPGRANT) == 0);

		/*
		 * If we already hold the lock exclusively this operation
		 * succeeds and is a NOP.
		 */
		if (count & LKC_EXCL) {
			if (lkp->lk_lockholder == td)
				break;
			panic("lockmgr: upgrade unowned lock");
		}
		if ((count & LKC_MASK) == 0)
			panic("lockmgr: upgrade unowned lock");

		/*
		 * We cannot upgrade without blocking at this point.
		 */
		if (extflags & LK_NOWAIT) {
			lockmgr_release(lkp, LK_RELEASE);
			error = EBUSY;
			break;
		}
		if (extflags & LK_CANCELABLE) {
			if (count & LKC_CANCEL) {
				error = ENOLCK;
				break;
			}
		}

		if ((extflags & LK_NOCOLLSTATS) == 0) {
			indefinite_info_t info;

			flags |= LK_NOCOLLSTATS;
			indefinite_init(&info, lkp->lk_wmesg, 1, 'U');
			error = lockmgr_upgrade(lkp, flags);
			indefinite_done(&info);
			break;
		}

		/*
		 * Release the shared lock and request the upgrade.
		 */
		pflags = (extflags & LK_PCATCH) ? PCATCH : 0;
		timo = (extflags & LK_TIMELOCK) ? lkp->lk_timo : 0;
		tsleep_interlock(lkp, pflags);
		wflags = (count & LKC_UPREQ) ? LKC_EXREQ : LKC_UPREQ;

		/*
		 * If someone else owns UPREQ and this transition would
		 * allow it to be granted, we have to grant it.  Our
		 * lock count is transfered (we effectively release).
		 * We will then request a normal exclusive lock.
		 *
		 * Otherwise we release the shared lock and either do
		 * an UPREQ or an EXREQ.  The count is always > 1 in
		 * this case since we handle all other count == 1
		 * situations here and above.
		 */
		if ((count & (LKC_UPREQ|LKC_MASK)) == (LKC_UPREQ | 1)) {
			wflags |= LKC_EXCL | LKC_UPGRANT;
			wflags |= count;
			wflags &= ~LKC_UPREQ;	/* was set from count */
		} else {
			wflags |= (count - 1);
		}

		if (atomic_cmpset_int(&lkp->lk_count, count, wflags)) {
			COUNT(td, -1);

			/*
			 * Must wakeup the thread granted the upgrade.
			 */
			if ((count & (LKC_UPREQ|LKC_MASK)) == (LKC_UPREQ | 1))
				wakeup(lkp);

			error = tsleep(lkp, pflags | PINTERLOCKED,
				       lkp->lk_wmesg, timo);
			if (error) {
				if ((count & LKC_UPREQ) == 0)
					undo_upreq(lkp);
				break;
			}
			if (extflags & LK_SLEEPFAIL) {
				if ((count & LKC_UPREQ) == 0)
					undo_upreq(lkp);
				error = ENOLCK;
				break;
			}

			/*
			 * Refactor to either LK_EXCLUSIVE or LK_WAITUPGRADE,
			 * depending on whether we were able to acquire the
			 * LKC_UPREQ bit.
			 */
			if (count & LKC_UPREQ)
				error = lockmgr_exclusive(lkp, flags);
			else
				error = lockmgr_waitupgrade(lkp, flags);
			break;
		}
		/* retry */
	}
	return error;
}

/*
 * (internal helper)
 */
static int
lockmgr_waitupgrade(struct lock *lkp, u_int flags)
{
	uint32_t extflags;
	thread_t td;
	int count;
	int error;
	int pflags;
	int timo;

	extflags = (flags | lkp->lk_flags) & LK_EXTFLG_MASK;
	td = curthread;
	error = 0;

	for (;;) {
		count = lkp->lk_count;
		cpu_ccfence();

		/*
		 * We own the LKC_UPREQ bit, wait until we are granted the
		 * exclusive lock (LKC_UPGRANT is set).
		 *
		 * IF THE OPERATION FAILS (tsleep error tsleep+LK_SLEEPFAIL),
		 * we have to undo the upgrade request and clean up any lock
		 * that might have been granted via a race.
		 */
		if (count & LKC_UPGRANT) {
			if (atomic_cmpset_int(&lkp->lk_count, count,
					      count & ~LKC_UPGRANT)) {
				lkp->lk_lockholder = td;
				KKASSERT(count & LKC_EXCL);
				break;
			}
			/* retry */
		} else if ((count & LKC_CANCEL) && (extflags & LK_CANCELABLE)) {
			undo_upreq(lkp);
			error = ENOLCK;
			break;
		} else {
			pflags = (extflags & LK_PCATCH) ? PCATCH : 0;
			timo = (extflags & LK_TIMELOCK) ? lkp->lk_timo : 0;
			tsleep_interlock(lkp, pflags);
			if (atomic_fetchadd_int(&lkp->lk_count, 0) == count) {
				error = tsleep(lkp, pflags | PINTERLOCKED,
					       lkp->lk_wmesg, timo);
				if (error) {
					undo_upreq(lkp);
					break;
				}
				if (extflags & LK_SLEEPFAIL) {
					error = ENOLCK;
					undo_upreq(lkp);
					break;
				}
			}
			/* retry */
		}
		/* retry */
	}
	return error;
}

/*
 * Release a held lock
 */
int
lockmgr_release(struct lock *lkp, u_int flags)
{
	uint32_t extflags;
	thread_t otd;
	thread_t td;
	int count;

	extflags = (flags | lkp->lk_flags) & LK_EXTFLG_MASK;
	td = curthread;

	for (;;) {
		count = lkp->lk_count;
		cpu_ccfence();

		/*
		 * Release the currently held lock.  If releasing the current
		 * lock as part of an error return, error will ALREADY be
		 * non-zero.
		 *
		 * When releasing the last lock we automatically transition
		 * LKC_UPREQ to LKC_EXCL|1.
		 *
		 * WARNING! We cannot detect when there are multiple exclusive
		 *	    requests pending.  We clear EXREQ unconditionally
		 *	    on the 1->0 transition so it is possible for
		 *	    shared requests to race the next exclusive
		 *	    request.
		 *
		 * WAERNING! lksleep() assumes that LK_RELEASE does not
		 *	    block.
		 *
		 * Always succeeds.
		 */
		if ((count & LKC_MASK) == 0)
			panic("lockmgr: LK_RELEASE: no lock held");

		if (count & LKC_EXCL) {
			if (lkp->lk_lockholder != LK_KERNTHREAD &&
			    lkp->lk_lockholder != td) {
				panic("lockmgr: pid %d, not exlusive "
				      "lock holder thr %p/%p unlocking",
				    (td->td_proc ? td->td_proc->p_pid : -1),
				    td, lkp->lk_lockholder);
			}
			if ((count & (LKC_UPREQ|LKC_MASK)) == 1) {
				/*
				 * Last exclusive count is being released
				 */
				otd = lkp->lk_lockholder;
				lkp->lk_lockholder = NULL;
				if (!atomic_cmpset_int(&lkp->lk_count, count,
					      (count - 1) &
					   ~(LKC_EXCL | LKC_EXREQ |
					     LKC_SHREQ| LKC_CANCEL))) {
					lkp->lk_lockholder = otd;
					continue;
				}
				if (count & (LKC_EXREQ|LKC_SHREQ))
					wakeup(lkp);
				/* success */
			} else if ((count & (LKC_UPREQ|LKC_MASK)) ==
				   (LKC_UPREQ | 1)) {
				/*
				 * Last exclusive count is being released but
				 * an upgrade request is present, automatically
				 * grant an exclusive state to the owner of
				 * the upgrade request.
				 */
				otd = lkp->lk_lockholder;
				lkp->lk_lockholder = NULL;
				if (!atomic_cmpset_int(&lkp->lk_count, count,
						(count & ~LKC_UPREQ) |
						LKC_UPGRANT)) {
					lkp->lk_lockholder = otd;
					continue;
				}
				wakeup(lkp);
				/* success */
			} else {
				otd = lkp->lk_lockholder;
				if (!atomic_cmpset_int(&lkp->lk_count, count,
						       count - 1)) {
					continue;
				}
				/* success */
			}
			/* success */
			if (otd != LK_KERNTHREAD)
				COUNT(td, -1);
		} else {
			if ((count & (LKC_UPREQ|LKC_MASK)) == 1) {
				/*
				 * Last shared count is being released,
				 * no upgrade request present.
				 */
				if (!atomic_cmpset_int(&lkp->lk_count, count,
					      (count - 1) &
					       ~(LKC_EXREQ | LKC_SHREQ |
						 LKC_CANCEL))) {
					continue;
				}
				if (count & (LKC_EXREQ|LKC_SHREQ))
					wakeup(lkp);
				/* success */
			} else if ((count & (LKC_UPREQ|LKC_MASK)) ==
				   (LKC_UPREQ | 1)) {
				/*
				 * Last shared count is being released but
				 * an upgrade request is present, automatically
				 * grant an exclusive state to the owner of
				 * the upgrade request.  Masked count
				 * remains 1.
				 */
				if (!atomic_cmpset_int(&lkp->lk_count, count,
					      (count & ~(LKC_UPREQ |
							 LKC_CANCEL)) |
					      LKC_EXCL | LKC_UPGRANT)) {
					continue;
				}
				wakeup(lkp);
			} else {
				/*
				 * Shared count is greater than 1, just
				 * decrement it by one.
				 */
				if (!atomic_cmpset_int(&lkp->lk_count, count,
						       count - 1)) {
					continue;
				}
			}
			/* success */
			COUNT(td, -1);
		}
		break;
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
	int count;

	for (;;) {
		count = lkp->lk_count;
		cpu_ccfence();

		KKASSERT((count & LKC_CANCEL) == 0);	/* disallowed case */
		KKASSERT((count & LKC_MASK) != 0);	/* issue w/lock held */
		if (!atomic_cmpset_int(&lkp->lk_count,
				       count, count | LKC_CANCEL)) {
			continue;
		}
		if (count & (LKC_EXREQ|LKC_SHREQ|LKC_UPREQ)) {
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
	atomic_clear_int(&lkp->lk_count, LKC_CANCEL);

	return 0;
}

/*
 * Undo an upgrade request
 */
static
void
undo_upreq(struct lock *lkp)
{
	int count;

	for (;;) {
		count = lkp->lk_count;
		cpu_ccfence();

		if (count & LKC_UPGRANT) {
			/*
			 * UPREQ was shifted to UPGRANT.  We own UPGRANT now,
			 * another thread might own UPREQ.  Clear UPGRANT
			 * and release the granted lock.
			 */
			if (atomic_cmpset_int(&lkp->lk_count, count,
					      count & ~LKC_UPGRANT)) {
				lkp->lk_lockholder = curthread;
				lockmgr(lkp, LK_RELEASE);
				break;
			}
		} else if (count & LKC_EXCL) {
			/*
			 * Clear the UPREQ we still own.  Nobody to wakeup
			 * here because there is an existing exclusive
			 * holder.
			 */
			KKASSERT(count & LKC_UPREQ);
			KKASSERT((count & LKC_MASK) > 0);
			if (atomic_cmpset_int(&lkp->lk_count, count,
					      count & ~LKC_UPREQ)) {
				wakeup(lkp);
				break;
			}
		} else if (count & LKC_EXREQ) {
			/*
			 * Clear the UPREQ we still own.  We cannot wakeup any
			 * shared waiters because there is an exclusive
			 * request pending.
			 */
			KKASSERT(count & LKC_UPREQ);
			KKASSERT((count & LKC_MASK) > 0);
			if (atomic_cmpset_int(&lkp->lk_count, count,
					      count & ~LKC_UPREQ)) {
				break;
			}
		} else {
			/*
			 * Clear the UPREQ we still own.  Wakeup any shared
			 * waiters.
			 */
			KKASSERT(count & LKC_UPREQ);
			KKASSERT((count & LKC_MASK) > 0);
			if (atomic_cmpset_int(&lkp->lk_count, count,
					      count &
					      ~(LKC_UPREQ | LKC_SHREQ))) {
				if (count & LKC_SHREQ)
					wakeup(lkp);
				break;
			}
		}
		/* retry */
	}
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
	lkp->lk_lockholder = LK_NOTHREAD;
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
	KKASSERT((lkp->lk_count & (LKC_EXREQ|LKC_SHREQ|LKC_UPREQ)) == 0);
}

/*
 * Determine the status of a lock.
 */
int
lockstatus(struct lock *lkp, struct thread *td)
{
	int lock_type = 0;
	int count;

	count = lkp->lk_count;
	cpu_ccfence();

	if (count & LKC_EXCL) {
		if (td == NULL || lkp->lk_lockholder == td)
			lock_type = LK_EXCLUSIVE;
		else
			lock_type = LK_EXCLOTHER;
	} else if (count & LKC_MASK) {
		lock_type = LK_SHARED;
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
	int count;

	count = lkp->lk_count;
	cpu_ccfence();

	if (count & LKC_EXCL)
		return(lkp->lk_lockholder == td);
	else
		return((count & LKC_MASK) != 0);
}

/*
 * Determine the number of holders of a lock.
 *
 * The non-blocking version can usually be used for assertions.
 */
int
lockcount(struct lock *lkp)
{
	return(lkp->lk_count & LKC_MASK);
}

int
lockcountnb(struct lock *lkp)
{
	return(lkp->lk_count & LKC_MASK);
}

/*
 * Print out information about state of a lock. Used by VOP_PRINT
 * routines to display status about contained locks.
 */
void
lockmgr_printinfo(struct lock *lkp)
{
	struct thread *td = lkp->lk_lockholder;
	struct proc *p;
	int count;

	count = lkp->lk_count;
	cpu_ccfence();

	if (td && td != LK_KERNTHREAD && td != LK_NOTHREAD)
		p = td->td_proc;
	else
		p = NULL;

	if (count & LKC_EXCL) {
		kprintf(" lock type %s: EXCLUS (count %08x) by td %p pid %d",
		    lkp->lk_wmesg, count, td,
		    p ? p->p_pid : -99);
	} else if (count & LKC_MASK) {
		kprintf(" lock type %s: SHARED (count %08x)",
		    lkp->lk_wmesg, count);
	} else {
		kprintf(" lock type %s: NOTHELD", lkp->lk_wmesg);
	}
	if (count & (LKC_EXREQ|LKC_SHREQ))
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
