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
 * $DragonFly: src/sys/sys/spinlock2.h,v 1.2 2005/11/08 22:40:00 dillon Exp $
 */

#ifndef _SYS_SPINLOCK2_H_
#define _SYS_SPINLOCK2_H_

#include <sys/thread2.h>
#include <machine/atomic.h>
#include <machine/cpufunc.h>

#ifdef SMP

static __inline void
spin_lock_debug(int count)
{
#ifdef INVARIANTS
	curthread->td_spinlocks += count;
#endif
}

static __inline boolean_t
spin_trylock(struct spinlock *mtx)
{
	if (atomic_swap_int(&mtx->lock, 1) == 0) {
		spin_lock_debug(1);
		return (TRUE);
	}
	return (FALSE);
}

extern void spin_lock_contested(struct spinlock *mtx);

/*
 * The quick versions should be used only if you are already
 * in a critical section or you know the spinlock will never
 * be used by an hard interrupt or soft interrupt.
 */
static __inline void
spin_lock_quick(struct spinlock *mtx)
{
	spin_lock_debug(1);
	if (atomic_swap_int(&mtx->lock, 1) != 0)
		spin_lock_contested(mtx);	/* slow path */
}

static __inline void
spin_unlock_quick(struct spinlock *mtx)
{
	spin_lock_debug(-1);
	cpu_sfence();
	mtx->lock = 0;		/* non-bus-locked lock release */
}

static __inline boolean_t
spin_is_locked(struct spinlock *mtx)
{
	return (mtx->lock);
}

static __inline void
spin_init(struct spinlock *mtx)
{
        mtx->lock = 0;
}

#else	/* SMP */

static __inline boolean_t
spin_trylock(struct spinlock *mtx)
{
	return (TRUE);
}

static __inline boolean_t
spin_is_locked(struct spinlock *mtx)
{
	return (FALSE);
}

static __inline void	spin_lock(struct spinlock *mtx) { }
static __inline void	spin_unlock(struct spinlock *mtx) { }
static __inline void	spin_init(struct spinlock *mtx) { }

#endif	/* SMP */

/*
 * The normal spin_lock() API automatically enters and exits a
 * critical section, preventing deadlocks from interrupt preemption
 * if the interrupt thread accesses the same spinlock.
 */
static __inline void
spin_lock(struct spinlock *mtx)
{
	crit_enter();
	spin_lock_quick(mtx);
}

static __inline void
spin_unlock(struct spinlock *mtx)
{
	spin_unlock_quick(mtx);
	crit_exit();
}

#endif

