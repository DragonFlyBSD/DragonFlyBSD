/* 
 * Copyright (c) 1995
 *	The Regents of the University of California.  All rights reserved.
 * Copyright (C) 1997
 *	John S. Dyson.  All rights reserved.
 * Copyright (C) 2013
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

static void undo_upreq(struct lock *lkp);

/*
 * Locking primitives implementation.
 * Locks provide shared/exclusive sychronization.
 */

#ifdef DEBUG_LOCKS
#define COUNT(td, x) (td)->td_locks += (x)
#else
#define COUNT(td, x)
#endif

#define LOCK_WAIT_TIME 100
#define LOCK_SAMPLE_WAIT 7

/*
 * Set, change, or release a lock.
 *
 */
int
#ifndef	DEBUG_LOCKS
lockmgr(struct lock *lkp, u_int flags)
#else
debuglockmgr(struct lock *lkp, u_int flags,
	     const char *name, const char *file, int line)
#endif
{
	thread_t td;
	thread_t otd;
	int error;
	int extflags;
	int count;
	int pflags;
	int wflags;
	int timo;
#ifdef DEBUG_LOCKS
	int i;
#endif

	error = 0;

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

	extflags = (flags | lkp->lk_flags) & LK_EXTFLG_MASK;
	td = curthread;

again:
	count = lkp->lk_count;
	cpu_ccfence();

	switch (flags & LK_TYPE_MASK) {
	case LK_SHARED:
		/*
		 * Shared lock critical path case
		 */
		if ((count & (LKC_EXREQ|LKC_UPREQ|LKC_EXCL)) == 0) {
			if (atomic_cmpset_int(&lkp->lk_count,
					      count, count + 1)) {
				COUNT(td, 1);
				break;
			}
			goto again;
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
		 * if other threads are tring to obtain an exclusive lock or
		 * upgrade to one.
		 */
		if (count & wflags) {
			if (extflags & LK_NOWAIT) {
				error = EBUSY;
				break;
			}
			tsleep_interlock(lkp, pflags);
			if (!atomic_cmpset_int(&lkp->lk_count, count,
					      count | LKC_SHREQ)) {
				goto again;
			}

			mycpu->gd_cnt.v_lock_name[0] = 'S';
			strncpy(mycpu->gd_cnt.v_lock_name + 1,
				lkp->lk_wmesg,
				sizeof(mycpu->gd_cnt.v_lock_name) - 2);
			++mycpu->gd_cnt.v_lock_colls;

			error = tsleep(lkp, pflags | PINTERLOCKED,
				       lkp->lk_wmesg, timo);
			if (error)
				break;
			if (extflags & LK_SLEEPFAIL) {
				error = ENOLCK;
				break;
			}
			goto again;
		}

		/*
		 * Otherwise we can bump the count
		 */
		if (atomic_cmpset_int(&lkp->lk_count, count, count + 1)) {
			COUNT(td, 1);
			break;
		}
		goto again;

	case LK_EXCLUSIVE:
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
			goto again;
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
			goto again;
		}

		mycpu->gd_cnt.v_lock_name[0] = 'X';
		strncpy(mycpu->gd_cnt.v_lock_name + 1,
			lkp->lk_wmesg,
			sizeof(mycpu->gd_cnt.v_lock_name) - 2);
		++mycpu->gd_cnt.v_lock_colls;

		error = tsleep(lkp, pflags | PINTERLOCKED,
			       lkp->lk_wmesg, timo);
		if (error)
			break;
		if (extflags & LK_SLEEPFAIL) {
			error = ENOLCK;
			break;
		}
		goto again;

	case LK_DOWNGRADE:
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

#ifdef DEBUG_LOCKS
		for (i = 0; i < LOCKMGR_DEBUG_ARRAY_SIZE; i++) {
			if (td->td_lockmgr_stack[i] == lkp &&
			    td->td_lockmgr_stack_id[i] > 0
			) {
				td->td_lockmgr_stack_id[i]--;
				break;
			}
		}
#endif
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
		goto again;

	case LK_EXCLUPGRADE:
		/*
		 * Upgrade from a single shared lock to an exclusive lock.
		 *
		 * If another process is ahead of us to get an upgrade,
		 * then we want to fail rather than have an intervening
		 * exclusive access.  The shared lock is released on
		 * failure.
		 */
		if (count & LKC_UPREQ) {
			flags = LK_RELEASE;
			error = EBUSY;
			goto again;
		}
		/* fall through into normal upgrade */

	case LK_UPGRADE:
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
			goto again;
		}

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
			flags = LK_RELEASE;
			error = EBUSY;
			goto again;
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
		 * allow it to be granted, we have to grant it.  Otherwise
		 * we release the shared lock.
		 */
		if ((count & (LKC_UPREQ|LKC_MASK)) == (LKC_UPREQ | 1)) {
			wflags |= LKC_EXCL | LKC_UPGRANT;
			wflags |= count;
			wflags &= ~LKC_UPREQ;
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

			mycpu->gd_cnt.v_lock_name[0] = 'U';
			strncpy(mycpu->gd_cnt.v_lock_name + 1,
				lkp->lk_wmesg,
				sizeof(mycpu->gd_cnt.v_lock_name) - 2);
			++mycpu->gd_cnt.v_lock_colls;

			error = tsleep(lkp, pflags | PINTERLOCKED,
				       lkp->lk_wmesg, timo);
			if (error)
				break;
			if (extflags & LK_SLEEPFAIL) {
				error = ENOLCK;
				break;
			}

			/*
			 * Refactor to either LK_EXCLUSIVE or LK_WAITUPGRADE,
			 * depending on whether we were able to acquire the
			 * LKC_UPREQ bit.
			 */
			if (count & LKC_UPREQ)
				flags = LK_EXCLUSIVE;	/* someone else */
			else
				flags = LK_WAITUPGRADE;	/* we own the bit */
		}
		goto again;

	case LK_WAITUPGRADE:
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
		} else {
			pflags = (extflags & LK_PCATCH) ? PCATCH : 0;
			timo = (extflags & LK_TIMELOCK) ? lkp->lk_timo : 0;
			tsleep_interlock(lkp, pflags);
			if (atomic_cmpset_int(&lkp->lk_count, count, count)) {

				mycpu->gd_cnt.v_lock_name[0] = 'U';
				strncpy(mycpu->gd_cnt.v_lock_name + 1,
					lkp->lk_wmesg,
					sizeof(mycpu->gd_cnt.v_lock_name) - 2);
				++mycpu->gd_cnt.v_lock_colls;

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
		goto again;

	case LK_RELEASE:
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
					   ~(LKC_EXCL|LKC_EXREQ|LKC_SHREQ))) {
					lkp->lk_lockholder = otd;
					goto again;
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
				}
				wakeup(lkp);
				/* success */
			} else {
				otd = lkp->lk_lockholder;
				if (!atomic_cmpset_int(&lkp->lk_count, count,
						       count - 1)) {
					goto again;
				}
				/* success */
			}
			/* success */
			if (otd != LK_KERNTHREAD)
				COUNT(td, -1);
		} else {
			if ((count & (LKC_UPREQ|LKC_MASK)) == 1) {
				/*
				 * Last shared count is being released.
				 */
				if (!atomic_cmpset_int(&lkp->lk_count, count,
					      (count - 1) &
					       ~(LKC_EXREQ|LKC_SHREQ))) {
					goto again;
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
				 * the upgrade request.
				 */
				if (!atomic_cmpset_int(&lkp->lk_count, count,
					      (count & ~LKC_UPREQ) |
					      LKC_EXCL | LKC_UPGRANT)) {
					goto again;
				}
				wakeup(lkp);
			} else {
				if (!atomic_cmpset_int(&lkp->lk_count, count,
						       count - 1)) {
					goto again;
				}
			}
			/* success */
			COUNT(td, -1);
		}
		break;

	default:
		panic("lockmgr: unknown locktype request %d",
		    flags & LK_TYPE_MASK);
		/* NOTREACHED */
	}
	return (error);
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
		    ("lockmgr_kernproc: lock not owned by curthread %p", td));
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
