/* 
 * Copyright (c) 1995
 *	The Regents of the University of California.  All rights reserved.
 *
 * Copyright (C) 1997
 *	John S. Dyson.  All rights reserved.
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
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
 *	@(#)kern_lock.c	8.18 (Berkeley) 5/21/95
 * $FreeBSD: src/sys/kern/kern_lock.c,v 1.31.2.3 2001/12/25 01:44:44 dillon Exp $
 * $DragonFly: src/sys/kern/kern_lock.c,v 1.27 2008/01/09 10:59:12 corecode Exp $
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

#if defined(DIAGNOSTIC)
#define LOCK_INLINE
#else
#define LOCK_INLINE __inline
#endif

#define LK_ALL (LK_HAVE_EXCL | LK_WANT_EXCL | LK_WANT_UPGRADE | \
		LK_SHARE_NONZERO | LK_WAIT_NONZERO)

static int acquire(struct lock *lkp, int extflags, int wanted);

static LOCK_INLINE void
sharelock(struct lock *lkp, int incr)
{
	lkp->lk_flags |= LK_SHARE_NONZERO;
	lkp->lk_sharecount += incr;
}

static LOCK_INLINE int
shareunlock(struct lock *lkp, int decr) 
{
	int dowakeup = 0;

	KASSERT(lkp->lk_sharecount >= decr, ("shareunlock: count < decr"));

	if (lkp->lk_sharecount == decr) {
		lkp->lk_flags &= ~LK_SHARE_NONZERO;
		if (lkp->lk_flags & (LK_WANT_UPGRADE | LK_WANT_EXCL)) {
			dowakeup = 1;
		}
		lkp->lk_sharecount = 0;
	} else {
		lkp->lk_sharecount -= decr;
	}
	return(dowakeup);
}

/*
 * lock acquisition helper routine.  Called with the lock's spinlock held.
 */
static int
acquire(struct lock *lkp, int extflags, int wanted) 
{
	int error;

	if ((extflags & LK_NOWAIT) && (lkp->lk_flags & wanted)) {
		return EBUSY;
	}

	while ((lkp->lk_flags & wanted) != 0) {
		lkp->lk_flags |= LK_WAIT_NONZERO;
		lkp->lk_waitcount++;

		/*
		 * Atomic spinlock release/sleep/reacquire.
		 */
		error = ssleep(lkp, &lkp->lk_spinlock,
			       ((extflags & LK_PCATCH) ? PCATCH : 0),
			       lkp->lk_wmesg, 
			       ((extflags & LK_TIMELOCK) ? lkp->lk_timo : 0));
		if (lkp->lk_waitcount == 1) {
			lkp->lk_flags &= ~LK_WAIT_NONZERO;
			lkp->lk_waitcount = 0;
		} else {
			lkp->lk_waitcount--;
		}
		if (error)
			return error;
		if (extflags & LK_SLEEPFAIL)
			return ENOLCK;
	}
	return 0;
}

/*
 * Set, change, or release a lock.
 *
 * Shared requests increment the shared count. Exclusive requests set the
 * LK_WANT_EXCL flag (preventing further shared locks), and wait for already
 * accepted shared locks and shared-to-exclusive upgrades to go away.
 *
 * A spinlock is held for most of the procedure.  We must not do anything
 * fancy while holding the spinlock.
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
	int error;
	int extflags;
	int dowakeup;
#ifdef DEBUG_LOCKS
	int i;
#endif

	error = 0;
	dowakeup = 0;

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
	if (mycpu->gd_spinlocks_wr &&
	    ((flags & LK_NOWAIT) == 0)
	) {
		panic("lockmgr %s from %s:%d: called with %d spinlocks held",
		      lkp->lk_wmesg, file, line, mycpu->gd_spinlocks_wr);
	}
#endif

	spin_lock(&lkp->lk_spinlock);

	extflags = (flags | lkp->lk_flags) & LK_EXTFLG_MASK;
	td = curthread;

	switch (flags & LK_TYPE_MASK) {
	case LK_SHARED:
		/*
		 * If we are not the exclusive lock holder, we have to block
		 * while there is an exclusive lock holder or while an
		 * exclusive lock request or upgrade request is in progress.
		 *
		 * However, if TDF_DEADLKTREAT is set, we override exclusive
		 * lock requests or upgrade requests ( but not the exclusive
		 * lock itself ).
		 */
		if (lkp->lk_lockholder != td) {
			if (td->td_flags & TDF_DEADLKTREAT) {
				error = acquire(
					    lkp,
					    extflags,
					    LK_HAVE_EXCL
					);
			} else {
				error = acquire(
					    lkp, 
					    extflags,
					    LK_HAVE_EXCL | LK_WANT_EXCL | 
					     LK_WANT_UPGRADE
					);
			}
			if (error)
				break;
			sharelock(lkp, 1);
			COUNT(td, 1);
			break;
		}
		/*
		 * We hold an exclusive lock, so downgrade it to shared.
		 * An alternative would be to fail with EDEADLK.
		 */
		sharelock(lkp, 1);
		COUNT(td, 1);
		/* fall into downgrade */

	case LK_DOWNGRADE:
		if (lkp->lk_lockholder != td || lkp->lk_exclusivecount == 0) {
			spin_unlock(&lkp->lk_spinlock);
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

		sharelock(lkp, lkp->lk_exclusivecount);
		lkp->lk_exclusivecount = 0;
		lkp->lk_flags &= ~LK_HAVE_EXCL;
		lkp->lk_lockholder = LK_NOTHREAD;
		if (lkp->lk_waitcount)
			dowakeup = 1;
		break;

	case LK_EXCLUPGRADE:
		/*
		 * If another process is ahead of us to get an upgrade,
		 * then we want to fail rather than have an intervening
		 * exclusive access.
		 */
		if (lkp->lk_flags & LK_WANT_UPGRADE) {
			dowakeup = shareunlock(lkp, 1);
			COUNT(td, -1);
			error = EBUSY;
			break;
		}
		/* fall into normal upgrade */

	case LK_UPGRADE:
		/*
		 * Upgrade a shared lock to an exclusive one. If another
		 * shared lock has already requested an upgrade to an
		 * exclusive lock, our shared lock is released and an
		 * exclusive lock is requested (which will be granted
		 * after the upgrade). If we return an error, the file
		 * will always be unlocked.
		 */
		if ((lkp->lk_lockholder == td) || (lkp->lk_sharecount <= 0)) {
			spin_unlock(&lkp->lk_spinlock);
			panic("lockmgr: upgrade exclusive lock");
		}
		dowakeup += shareunlock(lkp, 1);
		COUNT(td, -1);
		/*
		 * If we are just polling, check to see if we will block.
		 */
		if ((extflags & LK_NOWAIT) &&
		    ((lkp->lk_flags & LK_WANT_UPGRADE) ||
		     lkp->lk_sharecount > 1)) {
			error = EBUSY;
			break;
		}
		if ((lkp->lk_flags & LK_WANT_UPGRADE) == 0) {
			/*
			 * We are first shared lock to request an upgrade, so
			 * request upgrade and wait for the shared count to
			 * drop to zero, then take exclusive lock.
			 *
			 * Although I don't think this can occur for
			 * robustness we also wait for any exclusive locks
			 * to be released.  LK_WANT_UPGRADE is supposed to
			 * prevent new exclusive locks but might not in the
			 * future.
			 */
			lkp->lk_flags |= LK_WANT_UPGRADE;
			error = acquire(lkp, extflags,
					LK_HAVE_EXCL | LK_SHARE_NONZERO);
			lkp->lk_flags &= ~LK_WANT_UPGRADE;

			if (error)
				break;
			lkp->lk_flags |= LK_HAVE_EXCL;
			lkp->lk_lockholder = td;
			if (lkp->lk_exclusivecount != 0) {
				spin_unlock(&lkp->lk_spinlock);
				panic("lockmgr(1): non-zero exclusive count");
			}
			lkp->lk_exclusivecount = 1;
#if defined(DEBUG_LOCKS)
			lkp->lk_filename = file;
			lkp->lk_lineno = line;
			lkp->lk_lockername = name;

        	        for (i = 0; i < LOCKMGR_DEBUG_ARRAY_SIZE; i++) {
				/*
				 * Recursive lockmgr path
			 	 */
				if (td->td_lockmgr_stack[i] == lkp &&
				    td->td_lockmgr_stack_id[i] != 0
				) {
					td->td_lockmgr_stack_id[i]++;
					goto lkmatch2;
				}
 	               }

			for (i = 0; i < LOCKMGR_DEBUG_ARRAY_SIZE; i++) {
				/*
				 * Use new lockmgr tracking slot
			 	 */
        	        	if (td->td_lockmgr_stack_id[i] == 0) {
                	        	td->td_lockmgr_stack_id[i]++;
                        		td->td_lockmgr_stack[i] = lkp;
                        		break;
                        	}
			}
lkmatch2:
			;
#endif
			COUNT(td, 1);
			break;
		}
		/*
		 * Someone else has requested upgrade. Release our shared
		 * lock, awaken upgrade requestor if we are the last shared
		 * lock, then request an exclusive lock.
		 */
		if ((lkp->lk_flags & (LK_SHARE_NONZERO|LK_WAIT_NONZERO)) ==
		    LK_WAIT_NONZERO) {
			++dowakeup;
		}
		/* fall into exclusive request */

	case LK_EXCLUSIVE:
		if (lkp->lk_lockholder == td && td != LK_KERNTHREAD) {
			/*
			 *	Recursive lock.
			 */
			if ((extflags & (LK_NOWAIT | LK_CANRECURSE)) == 0) {
				spin_unlock(&lkp->lk_spinlock);
				panic("lockmgr: locking against myself");
			}
			if ((extflags & LK_CANRECURSE) != 0) {
				lkp->lk_exclusivecount++;
				COUNT(td, 1);
				break;
			}
		}
		/*
		 * If we are just polling, check to see if we will sleep.
		 */
		if ((extflags & LK_NOWAIT) &&
		    (lkp->lk_flags & (LK_HAVE_EXCL | LK_WANT_EXCL |
				      LK_WANT_UPGRADE | LK_SHARE_NONZERO))) {
			error = EBUSY;
			break;
		}
		/*
		 * Wait for exclusive lock holders to release and try to
		 * acquire the want_exclusive flag.
		 */
		error = acquire(lkp, extflags, (LK_HAVE_EXCL | LK_WANT_EXCL));
		if (error)
			break;
		lkp->lk_flags |= LK_WANT_EXCL;

		/*
		 * Wait for shared locks and upgrades to finish.  We can lose
		 * the race against a successful shared lock upgrade in which
		 * case LK_HAVE_EXCL will get set regardless of our
		 * acquisition of LK_WANT_EXCL, so we have to acquire
		 * LK_HAVE_EXCL here as well.
		 */
		error = acquire(lkp, extflags, LK_HAVE_EXCL |
					       LK_WANT_UPGRADE |
					       LK_SHARE_NONZERO);
		lkp->lk_flags &= ~LK_WANT_EXCL;
		if (error)
			break;
		lkp->lk_flags |= LK_HAVE_EXCL;
		lkp->lk_lockholder = td;
		if (lkp->lk_exclusivecount != 0) {
			spin_unlock(&lkp->lk_spinlock);
			panic("lockmgr(2): non-zero exclusive count");
		}
		lkp->lk_exclusivecount = 1;
#if defined(DEBUG_LOCKS)
		lkp->lk_filename = file;
		lkp->lk_lineno = line;
		lkp->lk_lockername = name;

                for (i = 0; i < LOCKMGR_DEBUG_ARRAY_SIZE; i++) {
			/*
			 * Recursive lockmgr path
			 */
			if (td->td_lockmgr_stack[i] == lkp &&
			    td->td_lockmgr_stack_id[i] != 0
			) {
				td->td_lockmgr_stack_id[i]++;
				goto lkmatch1;
			}
                }

		for (i = 0; i < LOCKMGR_DEBUG_ARRAY_SIZE; i++) {
			/*
			 * Use new lockmgr tracking slot
			 */
                	if (td->td_lockmgr_stack_id[i] == 0) {
                        	td->td_lockmgr_stack_id[i]++;
                        	td->td_lockmgr_stack[i] = lkp;
                        	break;
                        }
		}
lkmatch1:
		;
#endif
		COUNT(td, 1);
		break;

	case LK_RELEASE:
		if (lkp->lk_exclusivecount != 0) {
			if (lkp->lk_lockholder != td &&
			    lkp->lk_lockholder != LK_KERNTHREAD) {
				spin_unlock(&lkp->lk_spinlock);
				panic("lockmgr: pid %d, not %s thr %p/%p unlocking",
				    (td->td_proc ? td->td_proc->p_pid : -1),
				    "exclusive lock holder",
				    td, lkp->lk_lockholder);
			}
			if (lkp->lk_lockholder != LK_KERNTHREAD) {
				COUNT(td, -1);
			}
			if (lkp->lk_exclusivecount == 1) {
				lkp->lk_flags &= ~LK_HAVE_EXCL;
				lkp->lk_lockholder = LK_NOTHREAD;
				lkp->lk_exclusivecount = 0;
			} else {
				lkp->lk_exclusivecount--;
			}
#ifdef DEBUG_LOCKS
			for (i = 0; i < LOCKMGR_DEBUG_ARRAY_SIZE; i++) {
				if (td->td_lockmgr_stack[i] == lkp &&
				    td->td_lockmgr_stack_id[i] > 0
				) {
					td->td_lockmgr_stack_id[i]--;
					lkp->lk_filename = file;
					lkp->lk_lineno = line;
					break;
				}
			}
#endif
		} else if (lkp->lk_flags & LK_SHARE_NONZERO) {
			dowakeup += shareunlock(lkp, 1);
			COUNT(td, -1);
		} else {
			panic("lockmgr: LK_RELEASE: no lock held");
		}
		if (lkp->lk_flags & LK_WAIT_NONZERO)
			++dowakeup;
		break;

	default:
		spin_unlock(&lkp->lk_spinlock);
		panic("lockmgr: unknown locktype request %d",
		    flags & LK_TYPE_MASK);
		/* NOTREACHED */
	}
	spin_unlock(&lkp->lk_spinlock);
	if (dowakeup)
		wakeup(lkp);
	return (error);
}

void
lockmgr_kernproc(struct lock *lp)
{
	struct thread *td __debugvar = curthread;

	if (lp->lk_lockholder != LK_KERNTHREAD) {
		KASSERT(lp->lk_lockholder == td, 
		    ("lockmgr_kernproc: lock not owned by curthread %p", td));
		COUNT(td, -1);
		lp->lk_lockholder = LK_KERNTHREAD;
	}
}

#if 0
/*
 * Set the lock to be exclusively held.  The caller is holding the lock's
 * spinlock and the spinlock remains held on return.  A panic will occur
 * if the lock cannot be set to exclusive.
 *
 * XXX not only unused but these functions also break EXCLUPGRADE's
 * atomicy.
 */
void
lockmgr_setexclusive_interlocked(struct lock *lkp)
{
	thread_t td = curthread;

	KKASSERT((lkp->lk_flags & (LK_HAVE_EXCL|LK_SHARE_NONZERO)) == 0);
	KKASSERT(lkp->lk_exclusivecount == 0);
	lkp->lk_flags |= LK_HAVE_EXCL;
	lkp->lk_lockholder = td;
	lkp->lk_exclusivecount = 1;
	COUNT(td, 1);
}

/*
 * Clear the caller's exclusive lock.  The caller is holding the lock's
 * spinlock.  THIS FUNCTION WILL UNLOCK THE SPINLOCK.
 *
 * A panic will occur if the caller does not hold the lock.
 */
void
lockmgr_clrexclusive_interlocked(struct lock *lkp)
{
	thread_t td __debugvar = curthread;
	int dowakeup = 0;

	KKASSERT((lkp->lk_flags & LK_HAVE_EXCL) && lkp->lk_exclusivecount == 1
		 && lkp->lk_lockholder == td);
	lkp->lk_lockholder = LK_NOTHREAD;
	lkp->lk_flags &= ~LK_HAVE_EXCL;
	lkp->lk_exclusivecount = 0;
	if (lkp->lk_flags & LK_WAIT_NONZERO)
		dowakeup = 1;
	COUNT(td, -1);
	spin_unlock(&lkp->lk_spinlock);
	if (dowakeup)
		wakeup((void *)lkp);
}

#endif

/*
 * Initialize a lock; required before use.
 */
void
lockinit(struct lock *lkp, const char *wmesg, int timo, int flags)
{
	spin_init(&lkp->lk_spinlock);
	lkp->lk_flags = (flags & LK_EXTFLG_MASK);
	lkp->lk_sharecount = 0;
	lkp->lk_waitcount = 0;
	lkp->lk_exclusivecount = 0;
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
	spin_lock(&lkp->lk_spinlock);
	lkp->lk_flags = (lkp->lk_flags & ~LK_EXTFLG_MASK) |
			(flags & LK_EXTFLG_MASK);
	lkp->lk_wmesg = wmesg;
	lkp->lk_timo = timo;
	spin_unlock(&lkp->lk_spinlock);
}

/*
 * Requires that the caller is the exclusive owner of this lock.
 */
void
lockuninit(struct lock *l)
{
	/*
	 * At this point we should have removed all the references to this lock
	 * so there can't be anyone waiting on it.
	 */
	KKASSERT(l->lk_waitcount == 0);

	spin_uninit(&l->lk_spinlock);
}

/*
 * Determine the status of a lock.
 */
int
lockstatus(struct lock *lkp, struct thread *td)
{
	int lock_type = 0;

	spin_lock(&lkp->lk_spinlock);
	if (lkp->lk_exclusivecount != 0) {
		if (td == NULL || lkp->lk_lockholder == td)
			lock_type = LK_EXCLUSIVE;
		else
			lock_type = LK_EXCLOTHER;
	} else if (lkp->lk_sharecount != 0) {
		lock_type = LK_SHARED;
	}
	spin_unlock(&lkp->lk_spinlock);
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

	if (lkp->lk_exclusivecount)
		return(lkp->lk_lockholder == td);
	return(lkp->lk_sharecount != 0);
}

/*
 * Determine the number of holders of a lock.
 *
 * The non-blocking version can usually be used for assertions.
 */
int
lockcount(struct lock *lkp)
{
	int count;

	spin_lock(&lkp->lk_spinlock);
	count = lkp->lk_exclusivecount + lkp->lk_sharecount;
	spin_unlock(&lkp->lk_spinlock);
	return (count);
}

int
lockcountnb(struct lock *lkp)
{
	return (lkp->lk_exclusivecount + lkp->lk_sharecount);
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

	if (td && td != LK_KERNTHREAD && td != LK_NOTHREAD)
		p = td->td_proc;
	else
		p = NULL;

	if (lkp->lk_sharecount)
		kprintf(" lock type %s: SHARED (count %d)", lkp->lk_wmesg,
		    lkp->lk_sharecount);
	else if (lkp->lk_flags & LK_HAVE_EXCL)
		kprintf(" lock type %s: EXCL (count %d) by td %p pid %d",
		    lkp->lk_wmesg, lkp->lk_exclusivecount, td,
		    p ? p->p_pid : -99);
	if (lkp->lk_waitcount > 0)
		kprintf(" with %d pending", lkp->lk_waitcount);
}

void
lock_sysinit(struct lock_args *arg)
{
	lockinit(arg->la_lock, arg->la_desc, 0, arg->la_flags);
}
