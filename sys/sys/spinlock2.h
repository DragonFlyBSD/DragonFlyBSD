/*
 * Copyright (c) 2005 Jeffrey M. Hsu.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Jeffrey M. Hsu.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
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
 *
 * $DragonFly: src/sys/sys/spinlock2.h,v 1.10 2006/05/29 16:50:06 dillon Exp $
 */

#ifndef _SYS_SPINLOCK2_H_
#define _SYS_SPINLOCK2_H_

#ifndef _KERNEL

#error "This file should not be included by userland programs."

#else

#ifndef _SYS_THREAD2_H_
#include <sys/thread2.h>
#endif
#ifndef _SYS_GLOBALDATA_H_
#include <sys/globaldata.h>
#endif
#ifndef _MACHINE_ATOMIC_H_
#include <machine/atomic.h>
#endif
#ifndef _MACHINE_CPUFUNC_H_
#include <machine/cpufunc.h>
#endif

/*
 * SPECIAL NOTE!  Obtaining a spinlock does not enter a critical section
 * or protect against FAST interrupts but it will prevent thread preemption.
 * Because the spinlock code path is ultra critical, we do not check for
 * LWKT reschedule requests (due to an interrupt thread not being able to
 * preempt).
 */

#ifdef SMP

extern int spin_trylock_wr_contested(struct spinlock *mtx, int value);
extern void spin_lock_wr_contested(struct spinlock *mtx, int value);
extern void spin_lock_rd_contested(struct spinlock *mtx);

#endif

#ifdef SMP

/*
 * Attempt to obtain an exclusive spinlock.  Returns FALSE on failure,
 * TRUE on success.  Since the caller assumes that spinlocks must actually
 * work when using this function, it is only made available to SMP builds.
 */
static __inline boolean_t
spin_trylock_wr(struct spinlock *mtx)
{
	globaldata_t gd = mycpu;
	int value;

	++gd->gd_spinlocks_wr;
	if ((value = atomic_swap_int(&mtx->lock, SPINLOCK_EXCLUSIVE)) != 0)
		return (spin_trylock_wr_contested(mtx, value));
	return (TRUE);
}

#endif

/*
 * Obtain an exclusive spinlock and return.  Shortcut the case where the only
 * cached read lock was from our own cpu (it can just be cleared).
 */
static __inline void
spin_lock_wr_quick(globaldata_t gd, struct spinlock *mtx)
{
#ifdef SMP
	int value;
#endif

	++gd->gd_spinlocks_wr;
#ifdef SMP
	if ((value = atomic_swap_int(&mtx->lock, SPINLOCK_EXCLUSIVE)) != 0) {
		value &= ~gd->gd_cpumask;
		if (value)
			spin_lock_wr_contested(mtx, value);
	}
#endif
}

static __inline void
spin_lock_wr(struct spinlock *mtx)
{
	spin_lock_wr_quick(mycpu, mtx);
}

#if 0

/*
 * Upgrade a shared spinlock to exclusive.  Return TRUE if we were
 * able to upgrade without another exclusive holder getting in before
 * us, FALSE otherwise.
 */
static __inline int
spin_lock_upgrade(struct spinlock *mtx)
{
	globaldata_t gd = mycpu;
#ifdef SMP
	int value;
#endif

	++gd->gd_spinlocks_wr;
#ifdef SMP
	value = atomic_swap_int(&mtx->lock, SPINLOCK_EXCLUSIVE);
	cpu_sfence();
#endif
	--gd->gd_spinlocks_rd;
#ifdef SMP
	value &= ~gd->gd_cpumask;
	if (value) {
		spin_lock_wr_contested(mtx, value);
		if (value & SPINLOCK_EXCLUSIVE)
			return (FALSE);
		XXX regain original shared lock?
	}
	return (TRUE);
#endif
}

#endif

/*
 * Obtain a shared spinlock and return.  This is a critical code path.
 *
 * The vast majority of the overhead is in the cpu_mfence() (5ns vs 1ns for
 * the entire rest of the procedure).  Unfortunately we have to ensure that
 * spinlock count is written out before we check the cpumask to interlock
 * against an exclusive spinlock that clears the cpumask and then checks
 * the spinlock count.
 *
 * But what is EXTREMELY important here is that we do not have to perform
 * a locked bus cycle on the spinlock itself if the shared bit for our cpu
 * is already found to be set.  We only need the mfence, and the mfence is
 * local to the cpu and never conflicts with other cpu's.
 *
 * This means that multiple parallel shared acessors (e.g. filedescriptor
 * table lookups, namecache lookups) run at full speed and incur NO cache
 * contention at all.  Its the difference between 10ns and 40-100ns.
 */
static __inline void
spin_lock_rd_quick(globaldata_t gd, struct spinlock *mtx)
{
	++gd->gd_spinlocks_rd;
#ifdef SMP
	cpu_mfence();
	if ((mtx->lock & gd->gd_cpumask) == 0)
		spin_lock_rd_contested(mtx);
#endif
}

static __inline void
spin_lock_rd(struct spinlock *mtx)
{
	spin_lock_rd_quick(mycpu,mtx);
}

/*
 * Release an exclusive spinlock.  We can just do this passively, only
 * ensuring that our spinlock count is left intact until the mutex is
 * cleared.
 */
static __inline void
spin_unlock_wr_quick(globaldata_t gd, struct spinlock *mtx)
{
#ifdef SMP
	mtx->lock = 0;
#endif
	--gd->gd_spinlocks_wr;
}

static __inline void
spin_unlock_wr(struct spinlock *mtx)
{
	spin_unlock_wr_quick(mycpu, mtx);
}

/*
 * Release a shared spinlock.  We leave the shared bit set in the spinlock
 * as a cache and simply decrement the spinlock count for the cpu.  This
 * fast-paths another shared lock later at the cost of an exclusive lock
 * having to check per-cpu spinlock counts to determine when there are no
 * shared holders remaining.
 */
static __inline void
spin_unlock_rd_quick(globaldata_t gd, struct spinlock *mtx)
{
	--gd->gd_spinlocks_rd;
}

static __inline void
spin_unlock_rd(struct spinlock *mtx)
{
	spin_unlock_rd_quick(mycpu, mtx);
}

static __inline void
spin_init(struct spinlock *mtx)
{
        mtx->lock = 0;
}

static __inline void
spin_uninit(struct spinlock *mtx)
{
	/* unused */
}

#endif	/* _KERNEL */
#endif	/* _SYS_SPINLOCK2_H_ */

