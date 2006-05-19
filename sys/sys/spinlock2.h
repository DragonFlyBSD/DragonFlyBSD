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
 * $DragonFly: src/sys/sys/spinlock2.h,v 1.7 2006/05/19 18:26:29 dillon Exp $
 */

#ifndef _SYS_SPINLOCK2_H_
#define _SYS_SPINLOCK2_H_

#include <sys/thread2.h>
#include <machine/atomic.h>
#include <machine/cpufunc.h>

#ifdef SMP

#ifdef INVARIANTS

static __inline void
spin_lock_debug(thread_t td, int count)
{
	td->td_spinlocks += count;
}

#endif

/*
 * Attempt to obtain a spinlock on behalf of the specified thread.  Returns
 * FALSE on failure, TRUE on success.
 */
static __inline boolean_t
spin_trylock(thread_t td, struct spinlock *mtx)
{
	if (atomic_swap_int(&mtx->lock, 1) == 0) {
#ifdef INVARIANTS
		spin_lock_debug(td, 1);
#endif
		return (TRUE);
	}
	return (FALSE);
}

/*
 * Relase a spinlock obtained via spin_trylock() on behalf of the specified
 * thread.  This function always succeeds.  It exists because the other
 * standard release functions only operate on the current thread.
 */
static __inline void
spin_tryunlock(thread_t td, struct spinlock *mtx)
{
#ifdef INVARIANTS
	if (td->td_spinlocks <= 0)
		panic("spin_tryunlock: wasn't locked!");
	spin_lock_debug(td, -1);
#endif
	cpu_sfence();
	mtx->lock = 0;		/* non-bus-locked lock release */
}

extern void spin_lock_contested(struct spinlock *mtx);

/*
 * The quick versions should be used only if you are already
 * in a critical section or you know the spinlock will never
 * be used by an hard interrupt, IPI, or soft interrupt.
 *
 * Obtain a spinlock and return.
 */
static __inline void
spin_lock_quick(struct spinlock *mtx)
{
#ifdef INVARIANTS
	spin_lock_debug(curthread, 1);
#endif
	if (atomic_swap_int(&mtx->lock, 1) != 0)
		spin_lock_contested(mtx);	/* slow path */
}

/*
 * Release a spinlock previously obtained by the current thread.
 */
static __inline void
spin_unlock_quick(struct spinlock *mtx)
{
#ifdef INVARIANTS
	if (curthread->td_spinlocks <= 0)
		panic("spin_unlock_quick: wasn't locked!");
	spin_lock_debug(curthread, -1);
#endif
	cpu_sfence();
	mtx->lock = 0;		/* non-bus-locked lock release */
}

/*
 * Returns whether a spinlock is locked or not.  0 indicates not locked,
 * non-zero indicates locked (by any thread, not necessarily the current
 * thread).
 */
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

static __inline void
spin_uninit(struct spinlock *mtx)
{
	/* unused */
}

#else	/* SMP */

/*
 * There is no spin_trylock(), spin_tryunlock(), or spin_is_locked()
 * for UP builds.  These functions are used by the kernel only in
 * situations where the spinlock actually has to work.
 *
 * We provide the rest of the calls for UP as degenerate inlines (note
 * that the non-quick versions still obtain/release a critical section!).
 * This way we don't have to have a billion #ifdef's floating around
 * the rest of the kernel.
 */

static __inline void	spin_lock_quick(struct spinlock *mtx) { }
static __inline void	spin_unlock_quick(struct spinlock *mtx) { }
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
	crit_enter_id("spin");
	spin_lock_quick(mtx);
}

static __inline void
spin_unlock(struct spinlock *mtx)
{
	spin_unlock_quick(mtx);
	crit_exit_id("spin");
}

#endif

