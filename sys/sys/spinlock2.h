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
#include <machine/atomic.h>
#include <machine/cpufunc.h>

extern struct spinlock pmap_spin;

int spin_trylock_contested(struct spinlock *spin);
void _spin_lock_contested(struct spinlock *spin, const char *ident);
void _spin_lock_shared_contested(struct spinlock *spin, const char *ident);
void _spin_pool_lock(void *chan, const char *ident);
void _spin_pool_unlock(void *chan);

#define spin_lock(spin)			_spin_lock(spin, __func__)
#define spin_lock_quick(spin)		_spin_lock_quick(spin, __func__)
#define spin_lock_shared(spin)		_spin_lock_shared(spin, __func__)
#define spin_lock_shared_quick(spin)	_spin_lock_shared_quick(spin, __func__)
#define spin_pool_lock(chan)		_spin_pool_lock(chan, __func__)

/*
 * Attempt to obtain an exclusive spinlock.  Returns FALSE on failure,
 * TRUE on success.
 */
static __inline boolean_t
spin_trylock(struct spinlock *spin)
{
	globaldata_t gd = mycpu;

	++gd->gd_curthread->td_critcount;
	cpu_ccfence();
	++gd->gd_spinlocks;
	if (atomic_cmpset_int(&spin->counta, 0, 1) == 0)
		return (spin_trylock_contested(spin));
#ifdef DEBUG_LOCKS
	int i;
	for (i = 0; i < SPINLOCK_DEBUG_ARRAY_SIZE; i++) {
		if (gd->gd_curthread->td_spinlock_stack_id[i] == 0) {
			gd->gd_curthread->td_spinlock_stack_id[i] = 1;
			gd->gd_curthread->td_spinlock_stack[i] = spin;
			gd->gd_curthread->td_spinlock_caller_pc[i] =
				__builtin_return_address(0);
			break;
		}
	}
#endif
	return (TRUE);
}

/*
 * Return TRUE if the spinlock is held (we can't tell by whom, though)
 */
static __inline int
spin_held(struct spinlock *spin)
{
	return(spin->counta != 0);
}

/*
 * Obtain an exclusive spinlock and return.
 */
static __inline void
_spin_lock_quick(globaldata_t gd, struct spinlock *spin, const char *ident)
{
	++gd->gd_curthread->td_critcount;
	cpu_ccfence();
	++gd->gd_spinlocks;
	atomic_add_int(&spin->counta, 1);
	if (spin->counta != 1)
		_spin_lock_contested(spin, ident);
#ifdef DEBUG_LOCKS
	int i;
	for (i = 0; i < SPINLOCK_DEBUG_ARRAY_SIZE; i++) {
		if (gd->gd_curthread->td_spinlock_stack_id[i] == 0) {
			gd->gd_curthread->td_spinlock_stack_id[i] = 1;
			gd->gd_curthread->td_spinlock_stack[i] = spin;
			gd->gd_curthread->td_spinlock_caller_pc[i] =
				__builtin_return_address(0);
			break;
		}
	}
#endif
}

static __inline void
_spin_lock(struct spinlock *spin, const char *ident)
{
	_spin_lock_quick(mycpu, spin, ident);
}

/*
 * Release an exclusive spinlock.  We can just do this passively, only
 * ensuring that our spinlock count is left intact until the mutex is
 * cleared.
 */
static __inline void
spin_unlock_quick(globaldata_t gd, struct spinlock *spin)
{
#ifdef DEBUG_LOCKS
	int i;
	for (i = 0; i < SPINLOCK_DEBUG_ARRAY_SIZE; i++) {
		if ((gd->gd_curthread->td_spinlock_stack_id[i] == 1) &&
		    (gd->gd_curthread->td_spinlock_stack[i] == spin)) {
			gd->gd_curthread->td_spinlock_stack_id[i] = 0;
			gd->gd_curthread->td_spinlock_stack[i] = NULL;
			gd->gd_curthread->td_spinlock_caller_pc[i] = NULL;
			break;
		}
	}
#endif
	/*
	 * Don't use a locked instruction here.  To reduce latency we avoid
	 * reading spin->counta prior to writing to it.
	 */
#ifdef DEBUG_LOCKS
	KKASSERT(spin->counta != 0);
#endif
	cpu_sfence();
	atomic_add_int(&spin->counta, -1);
	cpu_sfence();
#ifdef DEBUG_LOCKS
	KKASSERT(gd->gd_spinlocks > 0);
#endif
	--gd->gd_spinlocks;
	cpu_ccfence();
	--gd->gd_curthread->td_critcount;
}

static __inline void
spin_unlock(struct spinlock *spin)
{
	spin_unlock_quick(mycpu, spin);
}

/*
 * Shared spinlocks
 */
static __inline void
_spin_lock_shared_quick(globaldata_t gd, struct spinlock *spin,
			const char *ident)
{
	++gd->gd_curthread->td_critcount;
	cpu_ccfence();
	++gd->gd_spinlocks;
	if (atomic_cmpset_int(&spin->counta, 0, SPINLOCK_SHARED | 1) == 0)
		_spin_lock_shared_contested(spin, ident);
#ifdef DEBUG_LOCKS
	int i;
	for (i = 0; i < SPINLOCK_DEBUG_ARRAY_SIZE; i++) {
		if (gd->gd_curthread->td_spinlock_stack_id[i] == 0) {
			gd->gd_curthread->td_spinlock_stack_id[i] = 1;
			gd->gd_curthread->td_spinlock_stack[i] = spin;
			gd->gd_curthread->td_spinlock_caller_pc[i] =
				__builtin_return_address(0);
			break;
		}
	}
#endif
}

static __inline void
spin_unlock_shared_quick(globaldata_t gd, struct spinlock *spin)
{
#ifdef DEBUG_LOCKS
	int i;
	for (i = 0; i < SPINLOCK_DEBUG_ARRAY_SIZE; i++) {
		if ((gd->gd_curthread->td_spinlock_stack_id[i] == 1) &&
		    (gd->gd_curthread->td_spinlock_stack[i] == spin)) {
			gd->gd_curthread->td_spinlock_stack_id[i] = 0;
			gd->gd_curthread->td_spinlock_stack[i] = NULL;
			gd->gd_curthread->td_spinlock_caller_pc[i] = NULL;
			break;
		}
	}
#endif
#ifdef DEBUG_LOCKS
	KKASSERT(spin->counta != 0);
#endif
	cpu_sfence();
	atomic_add_int(&spin->counta, -1);

	/*
	 * Make sure SPINLOCK_SHARED is cleared.  If another cpu tries to
	 * get a shared or exclusive lock this loop will break out.  We're
	 * only talking about a very trivial edge case here.
	 */
	while (spin->counta == SPINLOCK_SHARED) {
		if (atomic_cmpset_int(&spin->counta, SPINLOCK_SHARED, 0))
			break;
	}
	cpu_sfence();
#ifdef DEBUG_LOCKS
	KKASSERT(gd->gd_spinlocks > 0);
#endif
	--gd->gd_spinlocks;
	cpu_ccfence();
	--gd->gd_curthread->td_critcount;
}

static __inline void
_spin_lock_shared(struct spinlock *spin, const char *ident)
{
	_spin_lock_shared_quick(mycpu, spin, ident);
}

static __inline void
spin_unlock_shared(struct spinlock *spin)
{
	spin_unlock_shared_quick(mycpu, spin);
}

static __inline void
spin_pool_unlock(void *chan)
{
	_spin_pool_unlock(chan);
}

static __inline void
spin_init(struct spinlock *spin)
{
        spin->counta = 0;
        spin->countb = 0;
}

static __inline void
spin_uninit(struct spinlock *spin)
{
	/* unused */
}

#endif	/* _KERNEL */
#endif	/* _SYS_SPINLOCK2_H_ */

