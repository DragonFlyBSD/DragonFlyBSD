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
 * $DragonFly: src/sys/kern/kern_lock.c,v 1.13 2005/01/19 18:00:39 drhodus Exp $
 */

#include "opt_lint.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/proc.h>
#include <sys/lock.h>
#include <sys/sysctl.h>

/*
 * 0: no warnings, 1: warnings, 2: panic
 */
static int lockmgr_from_int = 1;
SYSCTL_INT(_debug, OID_AUTO, lockmgr_from_int, CTLFLAG_RW, &lockmgr_from_int, 0, "");

/*
 * Locking primitives implementation.
 * Locks provide shared/exclusive sychronization.
 */

#ifdef SIMPLELOCK_DEBUG
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
static int acquiredrain(struct lock *lkp, int extflags) ;

static LOCK_INLINE void
sharelock(struct lock *lkp, int incr) {
	lkp->lk_flags |= LK_SHARE_NONZERO;
	lkp->lk_sharecount += incr;
}

static LOCK_INLINE void
shareunlock(struct lock *lkp, int decr) {

	KASSERT(lkp->lk_sharecount >= decr, ("shareunlock: count < decr"));

	if (lkp->lk_sharecount == decr) {
		lkp->lk_flags &= ~LK_SHARE_NONZERO;
		if (lkp->lk_flags & (LK_WANT_UPGRADE | LK_WANT_EXCL)) {
			wakeup(lkp);
		}
		lkp->lk_sharecount = 0;
	} else {
		lkp->lk_sharecount -= decr;
	}
}

static int
acquire(struct lock *lkp, int extflags, int wanted) 
{
	int s, error;

	if ((extflags & LK_NOWAIT) && (lkp->lk_flags & wanted)) {
		return EBUSY;
	}

	if (((lkp->lk_flags | extflags) & LK_NOPAUSE) == 0) {
		if ((lkp->lk_flags & wanted) == 0)
			return 0;
	}

	s = splhigh();
	while ((lkp->lk_flags & wanted) != 0) {
		lkp->lk_flags |= LK_WAIT_NONZERO;
		lkp->lk_waitcount++;
		/* note: serialization lock is held through tsleep */
		error = tsleep(lkp, lkp->lk_prio, lkp->lk_wmesg, 
			    ((extflags & LK_TIMELOCK) ? lkp->lk_timo : 0));
		if (lkp->lk_waitcount == 1) {
			lkp->lk_flags &= ~LK_WAIT_NONZERO;
			lkp->lk_waitcount = 0;
		} else {
			lkp->lk_waitcount--;
		}
		if (error) {
			splx(s);
			return error;
		}
		if (extflags & LK_SLEEPFAIL) {
			splx(s);
			return ENOLCK;
		}
	}
	splx(s);
	return 0;
}

/*
 * Set, change, or release a lock.
 *
 * Shared requests increment the shared count. Exclusive requests set the
 * LK_WANT_EXCL flag (preventing further shared locks), and wait for already
 * accepted shared locks and shared-to-exclusive upgrades to go away.
 */
int
#ifndef	DEBUG_LOCKS
lockmgr(struct lock *lkp, u_int flags, lwkt_tokref_t interlkp,
	struct thread *td)
#else
debuglockmgr(struct lock *lkp, u_int flags, lwkt_tokref_t interlkp,
	struct thread *td, const char *name, const char *file, int line)
#endif
{
	int error;
	int extflags;
	lwkt_tokref ilock;
	static int didpanic;

	error = 0;

	if (lockmgr_from_int && mycpu->gd_intr_nesting_level &&
	    (flags & LK_NOWAIT) == 0 &&
	    (flags & LK_TYPE_MASK) != LK_RELEASE && didpanic == 0) {
#ifndef DEBUG_LOCKS
		    if (lockmgr_from_int == 2) {
			    didpanic = 1;
			    panic(
				"lockmgr %s from %p: called from interrupt",
				lkp->lk_wmesg, ((int **)&lkp)[-1]);
			    didpanic = 0;
		    } else {
			    printf(
				"lockmgr %s from %p: called from interrupt\n",
				lkp->lk_wmesg, ((int **)&lkp)[-1]);
		    }
#else
		    if (lockmgr_from_int == 2) {
			    didpanic = 1;
			    panic(
				"lockmgr %s from %s:%d: called from interrupt",
				lkp->lk_wmesg, file, line);
			    didpanic = 0;
		    } else {
			    printf(
				"lockmgr %s from %s:%d: called from interrupt\n",
				lkp->lk_wmesg, file, line);
		    }
#endif
	}

	lwkt_gettoken(&ilock, &lkp->lk_interlock);
	if (flags & LK_INTERLOCK)
		lwkt_reltoken(interlkp);

	extflags = (flags | lkp->lk_flags) & LK_EXTFLG_MASK;

	switch (flags & LK_TYPE_MASK) {

	case LK_SHARED:
		/*
		 * If we are not the exclusive lock holder, we have to block
		 * while there is an exclusive lock holder or while an
		 * exclusive lock request or upgrade request is in progress.
		 *
		 * However, if P_DEADLKTREAT is set, we override exclusive
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
		if (lkp->lk_lockholder != td || lkp->lk_exclusivecount == 0)
			panic("lockmgr: not holding exclusive lock");
		sharelock(lkp, lkp->lk_exclusivecount);
		lkp->lk_exclusivecount = 0;
		lkp->lk_flags &= ~LK_HAVE_EXCL;
		lkp->lk_lockholder = LK_NOTHREAD;
		if (lkp->lk_waitcount)
			wakeup((void *)lkp);
		break;

	case LK_EXCLUPGRADE:
		/*
		 * If another process is ahead of us to get an upgrade,
		 * then we want to fail rather than have an intervening
		 * exclusive access.
		 */
		if (lkp->lk_flags & LK_WANT_UPGRADE) {
			shareunlock(lkp, 1);
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
		if ((lkp->lk_lockholder == td) || (lkp->lk_sharecount <= 0))
			panic("lockmgr: upgrade exclusive lock");
		shareunlock(lkp, 1);
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
			 */
			lkp->lk_flags |= LK_WANT_UPGRADE;
			error = acquire(lkp, extflags, LK_SHARE_NONZERO);
			lkp->lk_flags &= ~LK_WANT_UPGRADE;

			if (error)
				break;
			lkp->lk_flags |= LK_HAVE_EXCL;
			lkp->lk_lockholder = td;
			if (lkp->lk_exclusivecount != 0)
				panic("lockmgr: non-zero exclusive count");
			lkp->lk_exclusivecount = 1;
#if defined(DEBUG_LOCKS)
			lkp->lk_filename = file;
			lkp->lk_lineno = line;
			lkp->lk_lockername = name;
#endif
			COUNT(td, 1);
			break;
		}
		/*
		 * Someone else has requested upgrade. Release our shared
		 * lock, awaken upgrade requestor if we are the last shared
		 * lock, then request an exclusive lock.
		 */
		if ( (lkp->lk_flags & (LK_SHARE_NONZERO|LK_WAIT_NONZERO)) ==
			LK_WAIT_NONZERO)
			wakeup((void *)lkp);
		/* fall into exclusive request */

	case LK_EXCLUSIVE:
		if (lkp->lk_lockholder == td && td != LK_KERNTHREAD) {
			/*
			 *	Recursive lock.
			 */
			if ((extflags & (LK_NOWAIT | LK_CANRECURSE)) == 0)
				panic("lockmgr: locking against myself");
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
		    (lkp->lk_flags & (LK_HAVE_EXCL | LK_WANT_EXCL | LK_WANT_UPGRADE | LK_SHARE_NONZERO))) {
			error = EBUSY;
			break;
		}
		/*
		 * Try to acquire the want_exclusive flag.
		 */
		error = acquire(lkp, extflags, (LK_HAVE_EXCL | LK_WANT_EXCL));
		if (error)
			break;
		lkp->lk_flags |= LK_WANT_EXCL;
		/*
		 * Wait for shared locks and upgrades to finish.
		 */
		error = acquire(lkp, extflags, LK_WANT_UPGRADE | LK_SHARE_NONZERO);
		lkp->lk_flags &= ~LK_WANT_EXCL;
		if (error)
			break;
		lkp->lk_flags |= LK_HAVE_EXCL;
		lkp->lk_lockholder = td;
		if (lkp->lk_exclusivecount != 0)
			panic("lockmgr: non-zero exclusive count");
		lkp->lk_exclusivecount = 1;
#if defined(DEBUG_LOCKS)
			lkp->lk_filename = file;
			lkp->lk_lineno = line;
			lkp->lk_lockername = name;
#endif
		COUNT(td, 1);
		break;

	case LK_RELEASE:
		if (lkp->lk_exclusivecount != 0) {
			if (lkp->lk_lockholder != td &&
			    lkp->lk_lockholder != LK_KERNTHREAD) {
				panic("lockmgr: pid %d, not %s thr %p unlocking",
				    (td->td_proc ? td->td_proc->p_pid : -99),
				    "exclusive lock holder",
				    lkp->lk_lockholder);
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
		} else if (lkp->lk_flags & LK_SHARE_NONZERO) {
			shareunlock(lkp, 1);
			COUNT(td, -1);
		}
		if (lkp->lk_flags & LK_WAIT_NONZERO)
			wakeup((void *)lkp);
		break;

	case LK_DRAIN:
		/*
		 * Check that we do not already hold the lock, as it can 
		 * never drain if we do. Unfortunately, we have no way to
		 * check for holding a shared lock, but at least we can
		 * check for an exclusive one.
		 */
		if (lkp->lk_lockholder == td)
			panic("lockmgr: draining against myself");

		error = acquiredrain(lkp, extflags);
		if (error)
			break;
		lkp->lk_flags |= LK_DRAINING | LK_HAVE_EXCL;
		lkp->lk_lockholder = td;
		lkp->lk_exclusivecount = 1;
#if defined(DEBUG_LOCKS)
			lkp->lk_filename = file;
			lkp->lk_lineno = line;
			lkp->lk_lockername = name;
#endif
		COUNT(td, 1);
		break;

	default:
		lwkt_reltoken(&ilock);
		panic("lockmgr: unknown locktype request %d",
		    flags & LK_TYPE_MASK);
		/* NOTREACHED */
	}
	if ((lkp->lk_flags & LK_WAITDRAIN) &&
	    (lkp->lk_flags & (LK_HAVE_EXCL | LK_WANT_EXCL | LK_WANT_UPGRADE |
		LK_SHARE_NONZERO | LK_WAIT_NONZERO)) == 0) {
		lkp->lk_flags &= ~LK_WAITDRAIN;
		wakeup((void *)&lkp->lk_flags);
	}
	lwkt_reltoken(&ilock);
	return (error);
}

static int
acquiredrain(struct lock *lkp, int extflags)
{
	int error;

	if ((extflags & LK_NOWAIT) && (lkp->lk_flags & LK_ALL)) {
		return EBUSY;
	}

	if ((lkp->lk_flags & LK_ALL) == 0)
		return 0;

	while (lkp->lk_flags & LK_ALL) {
		lkp->lk_flags |= LK_WAITDRAIN;
		/* interlock serialization held through tsleep */
		error = tsleep(&lkp->lk_flags, lkp->lk_prio,
			lkp->lk_wmesg, 
			((extflags & LK_TIMELOCK) ? lkp->lk_timo : 0));
		if (error)
			return error;
		if (extflags & LK_SLEEPFAIL) {
			return ENOLCK;
		}
	}
	return 0;
}

/*
 * Initialize a lock; required before use.
 */
void
lockinit(struct lock *lkp, int prio, char *wmesg, int timo, int flags)
{
	lwkt_token_init(&lkp->lk_interlock);
	lkp->lk_flags = (flags & LK_EXTFLG_MASK);
	lkp->lk_sharecount = 0;
	lkp->lk_waitcount = 0;
	lkp->lk_exclusivecount = 0;
	lkp->lk_prio = prio;
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
lockreinit(struct lock *lkp, int prio, char *wmesg, int timo, int flags)
{
	lkp->lk_flags = (lkp->lk_flags & ~(LK_EXTFLG_MASK|LK_DRAINING)) |
			(flags & LK_EXTFLG_MASK);
	lkp->lk_prio = prio;
	lkp->lk_wmesg = wmesg;
	lkp->lk_timo = timo;
}

/*
 * Determine the status of a lock.
 */
int
lockstatus(struct lock *lkp, struct thread *td)
{
	lwkt_tokref ilock;
	int lock_type = 0;

	lwkt_gettoken(&ilock, &lkp->lk_interlock);
	if (lkp->lk_exclusivecount != 0) {
		if (td == NULL || lkp->lk_lockholder == td)
			lock_type = LK_EXCLUSIVE;
		else
			lock_type = LK_EXCLOTHER;
	} else if (lkp->lk_sharecount != 0) {
		lock_type = LK_SHARED;
	}
	lwkt_reltoken(&ilock);
	return (lock_type);
}

/*
 * Determine the number of holders of a lock.
 *
 * The non-blocking version can usually be used for assertions.
 */
int
lockcount(struct lock *lkp)
{
	lwkt_tokref ilock;
	int count;

	lwkt_gettoken(&ilock, &lkp->lk_interlock);
	count = lkp->lk_exclusivecount + lkp->lk_sharecount;
	lwkt_reltoken(&ilock);
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
		printf(" lock type %s: SHARED (count %d)", lkp->lk_wmesg,
		    lkp->lk_sharecount);
	else if (lkp->lk_flags & LK_HAVE_EXCL)
		printf(" lock type %s: EXCL (count %d) by td %p pid %d",
		    lkp->lk_wmesg, lkp->lk_exclusivecount, td,
		    p ? p->p_pid : -99);
	if (lkp->lk_waitcount > 0)
		printf(" with %d pending", lkp->lk_waitcount);
}

