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
 * $DragonFly: src/sys/sys/spinlock2.h,v 1.12 2008/06/04 04:34:54 nth Exp $
 */

#ifndef _SYS_SPINLOCK2_H_
#define _SYS_SPINLOCK2_H_

#ifndef _KERNEL

#error "This file should not be included by userland programs."

#else

#ifndef _SYS_SYSTM_H_
#include <sys/systm.h>
#endif
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

#ifdef SMP

extern int spin_trylock_wr_contested2(globaldata_t gd);
extern void spin_lock_wr_contested2(struct spinlock *mtx);

#endif

#ifdef SMP

/*
 * Attempt to obtain an exclusive spinlock.  Returns FALSE on failure,
 * TRUE on success.
 */
static __inline boolean_t
spin_trylock(struct spinlock *mtx)
{
	globaldata_t gd = mycpu;
	int value;

	++gd->gd_curthread->td_critcount;
	cpu_ccfence();
	++gd->gd_spinlocks_wr;
	if ((value = atomic_swap_int(&mtx->lock, SPINLOCK_EXCLUSIVE)) != 0)
		return (spin_trylock_wr_contested2(gd));
#ifdef SMP
#ifdef DEBUG_LOCKS
	int i;
	for (i = 0; i < SPINLOCK_DEBUG_ARRAY_SIZE; i++) {
		if (gd->gd_curthread->td_spinlock_stack_id[i] == 0) {
			gd->gd_curthread->td_spinlock_stack_id[i] = 1;
			gd->gd_curthread->td_spinlock_stack[i] = mtx;
			gd->gd_curthread->td_spinlock_caller_pc[i] =
						__builtin_return_address(0);
			break;
		}
	}
#endif
#endif
	return (TRUE);
}

#else

static __inline boolean_t
spin_trylock(struct spinlock *mtx)
{
	globaldata_t gd = mycpu;

	++gd->gd_curthread->td_critcount;
	cpu_ccfence();
	++gd->gd_spinlocks_wr;
	return (TRUE);
}

#endif

/*
 * Obtain an exclusive spinlock and return.
 */
static __inline void
spin_lock_quick(globaldata_t gd, struct spinlock *mtx)
{
#ifdef SMP
	int value;
#endif

	++gd->gd_curthread->td_critcount;
	cpu_ccfence();
	++gd->gd_spinlocks_wr;
#ifdef SMP
	if ((value = atomic_swap_int(&mtx->lock, SPINLOCK_EXCLUSIVE)) != 0)
		spin_lock_wr_contested2(mtx);
#ifdef DEBUG_LOCKS
	int i;
	for (i = 0; i < SPINLOCK_DEBUG_ARRAY_SIZE; i++) {
		if (gd->gd_curthread->td_spinlock_stack_id[i] == 0) {
			gd->gd_curthread->td_spinlock_stack_id[i] = 1;
			gd->gd_curthread->td_spinlock_stack[i] = mtx;
			gd->gd_curthread->td_spinlock_caller_pc[i] =
				__builtin_return_address(0);
			break;
		}
	}
#endif
#endif
}

static __inline void
spin_lock(struct spinlock *mtx)
{
	spin_lock_quick(mycpu, mtx);
}

/*
 * Release an exclusive spinlock.  We can just do this passively, only
 * ensuring that our spinlock count is left intact until the mutex is
 * cleared.
 */
static __inline void
spin_unlock_quick(globaldata_t gd, struct spinlock *mtx)
{
#ifdef SMP
#ifdef DEBUG_LOCKS
	int i;
	for (i = 0; i < SPINLOCK_DEBUG_ARRAY_SIZE; i++) {
		if ((gd->gd_curthread->td_spinlock_stack_id[i] == 1) &&
		    (gd->gd_curthread->td_spinlock_stack[i] == mtx)) {
			gd->gd_curthread->td_spinlock_stack_id[i] = 0;
			gd->gd_curthread->td_spinlock_stack[i] = NULL;
			gd->gd_curthread->td_spinlock_caller_pc[i] = NULL;
			break;
		}
	}
#endif
	mtx->lock = 0;
#endif
	KKASSERT(gd->gd_spinlocks_wr > 0);
	--gd->gd_spinlocks_wr;
	cpu_ccfence();
	--gd->gd_curthread->td_critcount;
}

static __inline void
spin_unlock(struct spinlock *mtx)
{
	spin_unlock_quick(mycpu, mtx);
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

