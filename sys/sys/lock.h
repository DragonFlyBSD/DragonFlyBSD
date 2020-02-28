/* 
 * Copyright (c) 1995
 *	The Regents of the University of California.  All rights reserved.
 * Copyright (c) 2013-2017
 *	The DragonFly Project.  All rights reserved.
 *
 * This code contains ideas from software contributed to Berkeley by
 * Avadis Tevanian, Jr., Michael Wayne Young, and the Mach Operating
 * System project at Carnegie-Mellon University.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Matthew Dillon <dillon@backplane.com>
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

#ifndef	_SYS_LOCK_H_
#define	_SYS_LOCK_H_

/*
 * A number of third party programs #include <sys/lock.h> for no good
 * reason.  Don't actually include anything unless we are the kernel. 
 */
#if defined(_KERNEL) || defined(_KERNEL_STRUCTURES)

#include <machine/lock.h>
#ifndef _SYS_THREAD_H_
#include <sys/thread.h>		/* lwkt_token */
#endif
#ifndef _SYS_SPINLOCK_H_
#include <sys/spinlock.h>
#endif

/*
 * The general lock structure.  Provides for multiple shared locks,
 * upgrading from shared to exclusive, and sleeping until the lock
 * can be gained.
 *
 * NOTE: We don't __cachealign struct lock, its too much bloat.  Users
 *	 of struct lock may be able to arrange it within greater structures
 *	 in more SMP-friendly ways.
 */
struct thread;

struct lock {
	u_int	lk_flags;		/* see below */
	int	lk_timo;		/* maximum sleep time (for tsleep) */
	uint64_t lk_count;		/* see LKC_* bits */
	const char *lk_wmesg;		/* resource sleeping (for tsleep) */
	struct thread *lk_lockholder;	/* thread of excl lock holder */
};

/*
 * Lock request types:
 *
 *   LK_SHARED
 *	Get one of many possible shared locks. If a process holding an
 *	exclusive lock requests a shared lock, the exclusive lock(s) will
 *	be downgraded to shared locks.
 *
 *   LK_EXCLUSIVE
 *	Stop further shared locks, when they are cleared, grant a pending
 *	upgrade if it exists, then grant an exclusive lock. Only one exclusive
 *	lock may exist at a time, except that a process holding an exclusive
 *	lock may get additional exclusive locks if it explicitly sets the
 *	LK_CANRECURSE flag in the lock request, or if the LK_CANRECURSE flag
 *	was set when the lock was initialized.
 *
 *   LK_UPGRADE
 *	The process must hold a shared lock that it wants to have upgraded
 *	to an exclusive lock. Other processes may get exclusive access to
 *	the resource between the time that the upgrade is requested and the
 *	time that it is granted.
 *
 *   LK_EXCLUPGRADE
 *	the process must hold a shared lock that it wants to have upgraded
 *	to an exclusive lock. If the request succeeds, no other processes
 *	will have gotten exclusive access to the resource between the time
 *	that the upgrade is requested and the time that it is granted.
 *	However, if another process has already requested an upgrade, the
 *	request will fail (see error returns below).
 *
 *   LK_DOWNGRADE
 *	The process must hold an exclusive lock that it wants to have
 *	downgraded to a shared lock. If the process holds multiple (recursive)
 *	exclusive locks, they will all be downgraded to shared locks.
 *
 *   LK_RELEASE
 *	Release one instance of a lock.
 *
 *   LK_CANCEL_BEG
 *	The current exclusive lock holder can cancel any blocked lock requests,
 *	or any new requests, whos callers specified LK_CANCELABLE.  They will
 *	receive a ENOLCK error code.  Cancel beg/end does not stack.
 *
 *	The cancel command stays in effect until the exclusive lock holder
 *	releases the last count on the lock or issues a LK_CANCEL_END command.
 *
 *   LK_CANCEL_END
 *	The current exclusive lock holder can stop canceling new requests
 *	whos callers specify LK_CANCELABLE.  The exclusive lock is maintained.
 *
 *	Note that the last release of the exclusive lock will also
 *	automatically end cancel mode.
 *
 *
 * ---
 *
 *   LK_EXCLOTHER - return for lockstatus().  Used when another process
 *	holds the lock exclusively.
 *
 * These are flags that are passed to the lockmgr routine.
 */
#define LK_TYPE_MASK	0x0000000f	/* type of lock sought */
#define LK_SHARED	0x00000001	/* shared lock */
#define LK_EXCLUSIVE	0x00000002	/* exclusive lock */
#define LK_UPGRADE	0x00000003	/* shared-to-exclusive upgrade */
#define LK_EXCLUPGRADE	0x00000004	/* first shared-to-exclusive upgrade */
#define LK_DOWNGRADE	0x00000005	/* exclusive-to-shared downgrade */
#define LK_RELEASE	0x00000006	/* release any type of lock */
#define LK_WAITUPGRADE	0x00000007
#define LK_EXCLOTHER	0x00000008	/* other process holds lock */
#define LK_CANCEL_BEG	0x00000009	/* cancel other requests */
#define LK_CANCEL_END	0x0000000a	/* stop canceling other requests */

/*
 * lk_count bit fields.
 *
 * Positive count is exclusive, negative count is shared.  The count field
 * must be large enough to accomodate all possible threads.
 */
#define LKC_RESERVED8	0x0000000080000000LU	/* (DNU, insn optimization) */
#define LKC_EXREQ	0x0000000040000000LU	/* waiting for excl lock */
#define LKC_SHARED	0x0000000020000000LU	/* shared lock(s) granted */
#define LKC_UPREQ	0x0000000010000000LU	/* waiting for upgrade */
#define LKC_EXREQ2	0x0000000008000000LU	/* multi-wait for EXREQ */
#define LKC_CANCEL	0x0000000004000000LU	/* cancel in effect */
#define LKC_XMASK	0x0000000003FFFFFFLU
#define LKC_SMASK	0xFFFFFFFF00000000LU
#define LKC_SCOUNT	0x0000000100000000LU
#define LKC_SSHIFT	32

/*
 * External lock flags.
 *
 * The first three flags may be set in lock_init to set their mode permanently,
 * or passed in as arguments to the lock manager.
 */
#define LK_EXTFLG_MASK	0x070000F0	/* mask of external flags */
#define LK_NOWAIT	0x00000010	/* do not sleep to await lock */
#define LK_SLEEPFAIL	0x00000020	/* sleep, then return failure */
#define LK_CANRECURSE	0x00000040	/* allow recursive exclusive lock */
#define LK_NOCOLLSTATS	0x00000080	/* v_lock_coll not applicable */
#define	LK_CANCELABLE	0x01000000	/* blocked caller can be canceled */
#define LK_TIMELOCK	0x02000000
#define LK_PCATCH	0x04000000	/* timelocked with signal catching */

/*
 * Control flags
 *
 * Non-persistent external flags.
 */
#define LK_FAILRECLAIM	0x00010000 /* vn_lock: allowed to fail on reclaim */
#define LK_RETRY	0x00020000 /* vn_lock: retry until locked */
#define	LK_UNUSED40000	0x00040000
#define	LK_UNUSED80000	0x00080000

/*
 * Lock return status.
 *
 * Successfully obtained locks return 0. Locks will always succeed
 * unless one of the following is true:
 *	LK_FORCEUPGRADE is requested and some other process has already
 *	    requested a lock upgrade (returns EBUSY).
 *	LK_WAIT is set and a sleep would be required (returns EBUSY).
 *	LK_SLEEPFAIL is set and a sleep was done (returns ENOLCK).
 *	PCATCH is set in lock priority and a signal arrives (returns
 *	    either EINTR or ERESTART if system calls is to be restarted).
 *	Non-null lock timeout and timeout expires (returns EWOULDBLOCK).
 * A failed lock attempt always returns a non-zero error value. No lock
 * is held after an error return (in particular, a failed LK_UPGRADE
 * or LK_FORCEUPGRADE will have released its shared access lock).
 */

/*
 * Indicator that no process holds exclusive lock
 */
#define LK_KERNTHREAD ((struct thread *)-2)

#ifdef _KERNEL

void dumplockinfo(struct lock *lkp);
struct proc;

struct lock_args {
	struct lock	*la_lock;
	const char 	*la_desc;
	int		la_flags;
};

#define LOCK_INITIALIZER(wmesg, timo, flags)	\
{						\
	.lk_flags = ((flags) & LK_EXTFLG_MASK),	\
	.lk_timo = (timo),			\
	.lk_count = 0,				\
	.lk_wmesg = wmesg,			\
	.lk_lockholder = NULL			\
}

void	lockinit (struct lock *, const char *wmesg, int timo, int flags);
void	lockreinit (struct lock *, const char *wmesg, int timo, int flags);
void	lockuninit(struct lock *);
void	lock_sysinit(struct lock_args *);
int	lockmgr_shared (struct lock *, u_int flags);
int	lockmgr_exclusive (struct lock *, u_int flags);
int	lockmgr_downgrade (struct lock *, u_int flags);
int	lockmgr_upgrade (struct lock *, u_int flags);
int	lockmgr_release (struct lock *, u_int flags);
int	lockmgr_cancel_beg (struct lock *, u_int flags);
int	lockmgr_cancel_end (struct lock *, u_int flags);
void	lockmgr_kernproc (struct lock *);
void	lockmgr_printinfo (struct lock *);
int	lockstatus (struct lock *, struct thread *);
int	lockowned (struct lock *);

#define	LOCK_SYSINIT(name, lock, desc, flags)				\
	static struct lock_args name##_args = {				\
		(lock),							\
		(desc),							\
		(flags)							\
	};								\
	SYSINIT(name##_lock_sysinit, SI_SUB_DRIVERS, SI_ORDER_MIDDLE,	\
	    lock_sysinit, &name##_args);				\
	SYSUNINIT(name##_lock_sysuninit, SI_SUB_DRIVERS, SI_ORDER_MIDDLE, \
	    lockuninit, (lock))

/*
 * Most lockmgr() calls pass a constant flags parameter which
 * we can optimize-out with an inline.
 */
static __inline
int
lockmgr(struct lock *lkp, u_int flags)
{
	switch(flags & LK_TYPE_MASK) {
	case LK_SHARED:
		return lockmgr_shared(lkp, flags);
	case LK_EXCLUSIVE:
		return lockmgr_exclusive(lkp, flags);
	case LK_DOWNGRADE:
		return lockmgr_downgrade(lkp, flags);
	case LK_EXCLUPGRADE:
	case LK_UPGRADE:
		return lockmgr_upgrade(lkp, flags);
	case LK_RELEASE:
		return lockmgr_release(lkp, flags);
	case LK_CANCEL_BEG:
		return lockmgr_cancel_beg(lkp, flags);
	case LK_CANCEL_END:
		return lockmgr_cancel_end(lkp, flags);
	default:
		panic("lockmgr: unknown locktype request %d",
		      flags & LK_TYPE_MASK);
		return EINVAL;	/* NOT REACHED */
	}
}

/*
 * Returns non-zero if the lock is in-use.  Cannot be used to count
 * refs on a lock (refs cannot be safely counted due to the use of
 * atomic_fetchadd_int() for shared locks.
 */
static __inline
int
lockinuse(struct lock *lkp)
{
	return ((lkp->lk_count & (LKC_SMASK | LKC_XMASK)) != 0);
}

/*
 * Returns true if the lock was acquired. Can be used to port
 * FreeBSD's mtx_trylock() and similar functions.
 */
static __inline
boolean_t
lockmgr_try(struct lock *lkp, u_int flags)
{
	return (lockmgr(lkp, flags | LK_NOWAIT) == 0);
}

/*
 * Returns true if the lock is exclusively held by anyone
 */
static __inline
boolean_t
lockmgr_anyexcl(struct lock *lkp)
{
	return ((lkp->lk_count & LKC_XMASK) != 0);
}

static __inline
boolean_t
lockmgr_oneexcl(struct lock *lkp)
{
	return ((lkp->lk_count & LKC_XMASK) == 1);
}

static __inline
boolean_t
lockmgr_exclpending(struct lock *lkp)
{
	return ((lkp->lk_count & (LKC_EXREQ | LKC_EXREQ2)) != 0);
}

#endif /* _KERNEL */
#endif /* _KERNEL || _KERNEL_STRUCTURES */
#endif /* _SYS_LOCK_H_ */
