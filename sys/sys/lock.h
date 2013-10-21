/* 
 * Copyright (c) 1995
 *	The Regents of the University of California.  All rights reserved.
 * Copyright (c) 2013
 *	The DragonFly Project.  All rights reserved.
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
 */
struct thread;

struct lock {
	u_int	lk_flags;		/* see below */
	int	lk_count;		/* -shared, +exclusive */
	int	lk_timo;		/* maximum sleep time (for tsleep) */
	const char *lk_wmesg;		/* resource sleeping (for tsleep) */
	struct thread *lk_lockholder;	/* thread of excl lock holder */
#ifdef	DEBUG_LOCKS
	const char *lk_filename;
	const char *lk_lockername;
	int     lk_lineno;
#endif
};

/*
 * Lock request types:
 *   LK_SHARED - get one of many possible shared locks. If a process
 *	holding an exclusive lock requests a shared lock, the exclusive
 *	lock(s) will be downgraded to shared locks.
 *   LK_EXCLUSIVE - stop further shared locks, when they are cleared,
 *	grant a pending upgrade if it exists, then grant an exclusive
 *	lock. Only one exclusive lock may exist at a time, except that
 *	a process holding an exclusive lock may get additional exclusive
 *	locks if it explicitly sets the LK_CANRECURSE flag in the lock
 *	request, or if the LK_CANRECUSE flag was set when the lock was
 *	initialized.
 *   LK_UPGRADE - the process must hold a shared lock that it wants to
 *	have upgraded to an exclusive lock. Other processes may get
 *	exclusive access to the resource between the time that the upgrade
 *	is requested and the time that it is granted.
 *   LK_EXCLUPGRADE - the process must hold a shared lock that it wants to
 *	have upgraded to an exclusive lock. If the request succeeds, no
 *	other processes will have gotten exclusive access to the resource
 *	between the time that the upgrade is requested and the time that
 *	it is granted. However, if another process has already requested
 *	an upgrade, the request will fail (see error returns below).
 *   LK_DOWNGRADE - the process must hold an exclusive lock that it wants
 *	to have downgraded to a shared lock. If the process holds multiple
 *	(recursive) exclusive locks, they will all be downgraded to shared
 *	locks.
 *   LK_RELEASE - release one instance of a lock.
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

/*
 * lk_count bit fields.
 *
 * Positive count is exclusive, negative count is shared.
 */
#define LKC_EXREQ	0x80000000	/* waiting for exclusive lock */
#define LKC_SHREQ	0x40000000	/* waiting for shared lock */
#define LKC_UPREQ	0x20000000	/* waiting for upgrade */
#define LKC_EXCL	0x10000000	/* exclusive (else shr or unlcoked) */
#define LKC_UPGRANT	0x08000000	/* upgrade granted */
#define LKC_MASK	0x07FFFFFF

/*
 * External lock flags.
 *
 * The first three flags may be set in lock_init to set their mode permanently,
 * or passed in as arguments to the lock manager.
 */
#define LK_EXTFLG_MASK	0x07000070	/* mask of external flags */
#define LK_NOWAIT	0x00000010	/* do not sleep to await lock */
#define LK_SLEEPFAIL	0x00000020	/* sleep, then return failure */
#define LK_CANRECURSE	0x00000040	/* allow recursive exclusive lock */
#define LK_UNUSED0080	0x00000080
#define	LK_UNUSED0100x	0x01000000
#define LK_TIMELOCK	0x02000000
#define LK_PCATCH	0x04000000	/* timelocked with signal catching */

/*
 * Control flags
 *
 * Non-persistent external flags.
 */
#define LK_UNUSED10000	0x00010000
#define LK_RETRY	0x00020000 /* vn_lock: retry until locked */
#define	LK_NOOBJ	0x00040000 /* vget: don't create object */
#define	LK_THISLAYER	0x00080000 /* vn_lock: lock/unlock only current layer */

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
#define LK_NOTHREAD ((struct thread *)-1)

#ifdef _KERNEL

void dumplockinfo(struct lock *lkp);
struct proc;

struct lock_args {
	struct lock	*la_lock;
	const char 	*la_desc;
	int		la_flags;
};

void	lockinit (struct lock *, const char *wmesg, int timo, int flags);
void	lockreinit (struct lock *, const char *wmesg, int timo, int flags);
void	lockuninit(struct lock *);
void	lock_sysinit(struct lock_args *);
#ifdef DEBUG_LOCKS
int	debuglockmgr (struct lock *, u_int flags,
			const char *,
			const char *,
			int);
#define lockmgr(lockp, flags) \
	debuglockmgr((lockp), (flags), "lockmgr", __FILE__, __LINE__)
#else
int	lockmgr (struct lock *, u_int flags);
#endif
void	lockmgr_setexclusive_interlocked(struct lock *);
void	lockmgr_clrexclusive_interlocked(struct lock *);
void	lockmgr_kernproc (struct lock *);
void	lockmgr_printinfo (struct lock *);
int	lockstatus (struct lock *, struct thread *);
int	lockowned (struct lock *);
int	lockcount (struct lock *);
int	lockcountnb (struct lock *);

#define	LOCK_SYSINIT(name, lock, desc, flags)				\
	static struct lock_args name##_args = {				\
		(lock),							\
		(desc),							\
		(flags)							\
	};								\
	SYSINIT(name##_lock_sysinit, SI_SUB_DRIVERS, SI_ORDER_MIDDLE,	\
	    lock_sysinit, &name##_args);					\
	SYSUNINIT(name##_lock_sysuninit, SI_SUB_DRIVERS, SI_ORDER_MIDDLE,	\
	    lockuninit, (lock))

#endif /* _KERNEL */
#endif /* _KERNEL || _KERNEL_STRUCTURES */
#endif /* _SYS_LOCK_H_ */
