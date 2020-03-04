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
#endif

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
void _spin_lock_contested(struct spinlock *spin, const char *ident, int count);
void _spin_lock_shared_contested(struct spinlock *spin, const char *ident);

#define spin_lock(spin)			_spin_lock(spin, __func__)
#define spin_lock_quick(spin)		_spin_lock_quick(spin, __func__)
#define spin_lock_shared(spin)		_spin_lock_shared(spin, __func__)
#define spin_lock_shared_quick(spin)	_spin_lock_shared_quick(spin, __func__)

/*
 * Attempt to obtain an exclusive spinlock.  Returns FALSE on failure,
 * TRUE on success.
 */
static __inline boolean_t
spin_trylock(struct spinlock *spin)
{
	globaldata_t gd = mycpu;

	crit_enter_raw(gd->gd_curthread);
	++gd->gd_spinlocks;
	cpu_ccfence();
	if (atomic_cmpset_int(&spin->lock, 0, 1) == 0)
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
	return((spin->lock & ~SPINLOCK_SHARED) != 0);
}

/*
 * Obtain an exclusive spinlock and return.  It is possible for the
 * SPINLOCK_SHARED bit to already be set, in which case the contested
 * code is called to fix it up.
 */
static __inline void
_spin_lock_quick(globaldata_t gd, struct spinlock *spin, const char *ident)
{
	int count;

	crit_enter_raw(gd->gd_curthread);
	++gd->gd_spinlocks;
	cpu_ccfence();

	count = atomic_fetchadd_int(&spin->lock, 1);
	if (__predict_false(count != 0)) {
		_spin_lock_contested(spin, ident, count);
	}
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
 *
 * NOTE: Actually works for shared OR exclusive spinlocks.  spin_unlock_any()
 *	 assumes this too.
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
	 * reading spin->lock prior to writing to it.
	 */
#ifdef DEBUG_LOCKS
	KKASSERT(spin->lock != 0);
#endif
	cpu_sfence();
	atomic_add_int(&spin->lock, -1);
	cpu_sfence();
#ifdef DEBUG_LOCKS
	KKASSERT(gd->gd_spinlocks > 0);
#endif
	cpu_ccfence();
	--gd->gd_spinlocks;
	crit_exit_quick(gd->gd_curthread);
}

static __inline void
spin_unlock(struct spinlock *spin)
{
	spin_unlock_quick(mycpu, spin);
}

static __inline void
spin_unlock_any(struct spinlock *spin)
{
	spin_unlock_quick(mycpu, spin);
}

/*
 * Shared spinlock.  Acquire a count, if SPINLOCK_SHARED is not already
 * set then try a trivial conversion and drop into the contested code if
 * the trivial cocnversion fails.  The SHARED bit is 'cached' when lock
 * counts go to 0 so the critical path is typically just the fetchadd.
 *
 * WARNING!  Due to the way exclusive conflict resolution works, we cannot
 *	     just unconditionally set the SHARED bit on previous-count == 0.
 *	     Doing so will interfere with the exclusive contended code.
 */
static __inline void
_spin_lock_shared_quick(globaldata_t gd, struct spinlock *spin,
			const char *ident)
{
	int lock;

	crit_enter_raw(gd->gd_curthread);
	++gd->gd_spinlocks;
	cpu_ccfence();

	lock = atomic_fetchadd_int(&spin->lock, 1);
	if (__predict_false((lock & SPINLOCK_SHARED) == 0)) {
		if (lock != 0 ||
		    !atomic_cmpset_int(&spin->lock, 1, SPINLOCK_SHARED | 1)) {
			_spin_lock_shared_contested(spin, ident);
		}
	}
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

/*
 * Unlock a shared lock.  For convenience we allow the last transition
 * to be to (SPINLOCK_SHARED|0), leaving the SPINLOCK_SHARED bit set
 * with a count to 0 which will optimize the next shared lock obtained.
 *
 * WARNING! In order to implement shared and exclusive spinlocks, an
 *	    exclusive request will convert a multiply-held shared lock
 *	    to exclusive and wait for shared holders to unlock.  So keep
 *	    in mind that as of now the spinlock could actually be in an
 *	    exclusive state.
 */
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
	KKASSERT(spin->lock != 0);
#endif
	cpu_sfence();
	atomic_add_int(&spin->lock, -1);

#ifdef DEBUG_LOCKS
	KKASSERT(gd->gd_spinlocks > 0);
#endif
	cpu_ccfence();
	--gd->gd_spinlocks;
	crit_exit_quick(gd->gd_curthread);
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

/*
 * Attempt to upgrade a shared spinlock to exclusive.  Return non-zero
 * on success, 0 on failure.
 */
static __inline int
spin_lock_upgrade_try(struct spinlock *spin)
{
	if (atomic_cmpset_int(&spin->lock, SPINLOCK_SHARED|1, 1))
		return 1;
	else
		return 0;
}

static __inline void
spin_init(struct spinlock *spin, const char *descr __unused)
{
	spin->lock = 0;
	spin->update = 0;
#if 0
	spin->descr  = descr;
#endif
}

static __inline void
spin_uninit(struct spinlock *spin)
{
	/* unused */
}

/*
 * SMP friendly update counter support.  Allows protected structures to
 * be accessed and retried without dirtying the cache line.  Retries if
 * modified, gains shared spin-lock if modification is underway.
 *
 * The returned value from spin_access_start() must be passed into
 * spin_access_end().
 */
static __inline int
spin_access_start(struct spinlock *spin)
{
	int v;

	v = *(volatile int *)&spin->update;
	cpu_lfence();
	if (__predict_false(v & 1))
		spin_lock_shared(spin);
	return v;
}

static __inline int
spin_access_end(struct spinlock *spin, int v)
{
	if (__predict_false(v & 1)) {
		spin_unlock_shared(spin);
		return 0;
	}
	cpu_lfence();
	return(*(volatile int *)&spin->update != v);
}

static __inline void
spin_lock_update(struct spinlock *spin)
{
	spin_lock(spin);
	atomic_add_int_nonlocked(&spin->update, 1);
	cpu_sfence();
	KKASSERT_UNSPIN((spin->update & 1), spin);
}

static __inline void
spin_unlock_update(struct spinlock *spin)
{
	cpu_sfence();
	atomic_add_int_nonlocked(&spin->update, 1);
	KKASSERT_UNSPIN(((spin->update & 1) == 0), spin);
	spin_unlock(spin);
}

/*
 * API that doesn't integrate the acquisition of the spin-lock
 */
static __inline int
spin_access_start_only(struct spinlock *spin)
{
	int v;

	v = *(volatile int *)&spin->update;
	cpu_lfence();

	return v;
}

static __inline int
spin_access_check_inprog(int v)
{
	return (v & 1);
}

static __inline int
spin_access_end_only(struct spinlock *spin, int v)
{
	cpu_lfence();
	return(*(volatile int *)&spin->update != v);
}

static __inline void
spin_lock_update_only(struct spinlock *spin)
{
	atomic_add_int_nonlocked(&spin->update, 1);
	cpu_sfence();
	KKASSERT(spin->update & 1);
}

static __inline void
spin_unlock_update_only(struct spinlock *spin)
{
	cpu_sfence();
	atomic_add_int_nonlocked(&spin->update, 1);
	KKASSERT((spin->update & 1) == 0);
}

#endif	/* _SYS_SPINLOCK2_H_ */
