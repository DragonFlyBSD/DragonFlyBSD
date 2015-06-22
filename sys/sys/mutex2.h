/*
 * Copyright (c) 2009 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Matthew Dillon <dillon@backplane.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
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

#ifndef	_SYS_MUTEX2_H_
#define	_SYS_MUTEX2_H_

#ifndef _SYS_MUTEX_H_
#include <sys/mutex.h>
#endif
#ifndef _SYS_THREAD2_H_
#include <sys/thread2.h>
#endif
#ifndef _SYS_GLOBALDATA_H_
#include <sys/globaldata.h>
#endif
#include <machine/atomic.h>

/*
 * Initialize a new mutex, placing it in an unlocked state with no refs.
 */
static __inline void
mtx_init(mtx_t *mtx, const char *ident)
{
	mtx->mtx_lock = 0;
	mtx->mtx_owner = NULL;
	mtx->mtx_exlink = NULL;
	mtx->mtx_shlink = NULL;
	mtx->mtx_ident = ident;
}

/*
 * Initialize a mtx link structure for deeper control over the mutex
 * operation.
 */
static __inline void
mtx_link_init(mtx_link_t *link)
{
	link->state = MTX_LINK_IDLE;
	link->callback = NULL;
	link->arg = NULL;
}

/*
 * A link structure initialized this way causes mutex operations to not block,
 * caller must specify a callback.  Caller may still abort the mutex via
 * the link.
 */
static __inline void
mtx_link_init_async(mtx_link_t *link,
		    void (*callback)(mtx_link_t *link, void *arg, int error),
		    void *arg)
{
	link->state = MTX_LINK_IDLE;
	link->callback = callback;
	link->arg = arg;
}

/*
 * Deinitialize a mutex
 */
static __inline void
mtx_uninit(mtx_t *mtx)
{
	/* empty */
}

/*
 * Exclusive-lock a mutex, block until acquired or aborted.  Recursion
 * is allowed.
 *
 * This version of the function allows the mtx_link to be passed in, thus
 * giving the caller visibility for the link structure which is required
 * when calling mtx_abort_ex_link() or when requesting an asynchronous lock.
 *
 * The mutex may be aborted at any time while the passed link structure
 * is valid.
 */
static __inline int
mtx_lock_ex_link(mtx_t *mtx, mtx_link_t *link, int flags, int to)
{
	if (atomic_cmpset_int(&mtx->mtx_lock, 0, MTX_EXCLUSIVE | 1) == 0)
		return(_mtx_lock_ex_link(mtx, link, flags, to));
	mtx->mtx_owner = curthread;
	link->state = MTX_LINK_ACQUIRED;

	return(0);
}

/*
 * Short-form exclusive-lock a mutex, block until acquired.  Recursion is
 * allowed.  This is equivalent to mtx_lock_ex(mtx, "mtxex", 0, 0).
 */
static __inline void
mtx_lock(mtx_t *mtx)
{
	if (atomic_cmpset_int(&mtx->mtx_lock, 0, MTX_EXCLUSIVE | 1) == 0) {
		_mtx_lock_ex(mtx, 0, 0);
		return;
	}
	mtx->mtx_owner = curthread;
}

/*
 * Exclusive-lock a mutex, block until acquired.  Recursion is allowed.
 *
 * Returns 0 on success, or the tsleep() return code on failure.
 * An error can only be returned if PCATCH is specified in the flags.
 */
static __inline int
mtx_lock_ex(mtx_t *mtx, int flags, int to)
{
	if (atomic_cmpset_int(&mtx->mtx_lock, 0, MTX_EXCLUSIVE | 1) == 0)
		return(_mtx_lock_ex(mtx, flags, to));
	mtx->mtx_owner = curthread;
	return(0);
}

static __inline int
mtx_lock_ex_quick(mtx_t *mtx)
{
	if (atomic_cmpset_int(&mtx->mtx_lock, 0, MTX_EXCLUSIVE | 1) == 0)
		return(_mtx_lock_ex_quick(mtx));
	mtx->mtx_owner = curthread;
	return(0);
}

static __inline int
mtx_lock_sh_link(mtx_t *mtx, mtx_link_t *link, int flags, int to)
{
	if (atomic_cmpset_int(&mtx->mtx_lock, 0, 1) == 0)
		return(_mtx_lock_sh_link(mtx, link, flags, to));
	link->state = MTX_LINK_ACQUIRED;
	return(0);
}

/*
 * Share-lock a mutex, block until acquired.  Recursion is allowed.
 *
 * Returns 0 on success, or the tsleep() return code on failure.
 * An error can only be returned if PCATCH is specified in the flags.
 */
static __inline int
mtx_lock_sh(mtx_t *mtx, int flags, int to)
{
	if (atomic_cmpset_int(&mtx->mtx_lock, 0, 1) == 0)
		return(_mtx_lock_sh(mtx, flags, to));
	return(0);
}

static __inline int
mtx_lock_sh_quick(mtx_t *mtx)
{
	if (atomic_cmpset_int(&mtx->mtx_lock, 0, 1) == 0)
		return(_mtx_lock_sh_quick(mtx));
	return(0);
}

/*
 * Short-form exclusive spinlock a mutex.  Must be paired with
 * mtx_spinunlock().
 */
static __inline void
mtx_spinlock(mtx_t *mtx)
{
	globaldata_t gd = mycpu;

	/*
	 * Predispose a hard critical section
	 */
	++gd->gd_curthread->td_critcount;
	cpu_ccfence();
	++gd->gd_spinlocks;

	/*
	 * If we cannot get it trivially get it the hard way.
	 *
	 * Note that mtx_owner will be set twice if we fail to get it
	 * trivially, but there's no point conditionalizing it as a
	 * conditional will be slower.
	 */
	if (atomic_cmpset_int(&mtx->mtx_lock, 0, MTX_EXCLUSIVE | 1) == 0)
		_mtx_spinlock(mtx);
	mtx->mtx_owner = gd->gd_curthread;
}

static __inline int
mtx_spinlock_try(mtx_t *mtx)
{
	globaldata_t gd = mycpu;

	/*
	 * Predispose a hard critical section
	 */
	++gd->gd_curthread->td_critcount;
	cpu_ccfence();
	++gd->gd_spinlocks;

	/*
	 * If we cannot get it trivially call _mtx_spinlock_try().  This
	 * function will clean up the hard critical section if it fails.
	 */
	if (atomic_cmpset_int(&mtx->mtx_lock, 0, MTX_EXCLUSIVE | 1) == 0)
		return(_mtx_spinlock_try(mtx));
	mtx->mtx_owner = gd->gd_curthread;
	return (0);
}

/*
 * Short-form exclusive-lock a mutex, spin until acquired.  Recursion is
 * allowed.  This form is identical to mtx_spinlock_ex().
 *
 * Attempt to exclusive-lock a mutex, return 0 on success and
 * EAGAIN on failure.
 */
static __inline int
mtx_lock_ex_try(mtx_t *mtx)
{
	if (atomic_cmpset_int(&mtx->mtx_lock, 0, MTX_EXCLUSIVE | 1) == 0)
		return (_mtx_lock_ex_try(mtx));
	mtx->mtx_owner = curthread;
	return (0);
}

/*
 * Attempt to share-lock a mutex, return 0 on success and
 * EAGAIN on failure.
 */
static __inline int
mtx_lock_sh_try(mtx_t *mtx)
{
	if (atomic_cmpset_int(&mtx->mtx_lock, 0, 1) == 0)
		return (_mtx_lock_sh_try(mtx));
	return (0);
}

/*
 * If the lock is held exclusively it must be owned by the caller.  If the
 * lock is already a shared lock this operation is a NOP.    A panic will
 * occur if the lock is not held either shared or exclusive.
 *
 * The exclusive count is converted to a shared count.
 */
static __inline void
mtx_downgrade(mtx_t *mtx)
{
	mtx->mtx_owner = NULL;
	if (atomic_cmpset_int(&mtx->mtx_lock, MTX_EXCLUSIVE | 1, 1) == 0)
		_mtx_downgrade(mtx);
}

/*
 * Upgrade a shared lock to an exclusive lock.  The upgrade will fail if
 * the shared lock has a count other then 1.  Optimize the most likely case
 * but note that a single cmpset can fail due to WANTED races.
 *
 * If the lock is held exclusively it must be owned by the caller and
 * this function will simply return without doing anything.  A panic will
 * occur if the lock is held exclusively by someone other then the caller.
 *
 * Returns 0 on success, EDEADLK on failure.
 */
static __inline int
mtx_upgrade_try(mtx_t *mtx)
{
	if (atomic_cmpset_int(&mtx->mtx_lock, 1, MTX_EXCLUSIVE | 1))
		return(0);
	return (_mtx_upgrade_try(mtx));
}

/*
 * Optimized unlock cases.
 *
 * NOTE: mtx_unlock() handles any type of mutex: exclusive, shared, and
 *	 both blocking and spin methods.
 *
 *	 The mtx_unlock_ex/sh() forms are optimized for exclusive or shared
 *	 mutexes and produce less code, but it is ok for code to just use
 *	 mtx_unlock() and, in fact, if code uses the short-form mtx_lock()
 *	 or mtx_spinlock() to lock it should also use mtx_unlock() to unlock.
 */
static __inline void
mtx_unlock(mtx_t *mtx)
{
	u_int lock = mtx->mtx_lock;

	if (lock == (MTX_EXCLUSIVE | 1)) {
		mtx->mtx_owner = NULL;
		if (atomic_cmpset_int(&mtx->mtx_lock, lock, 0) == 0)
			_mtx_unlock(mtx);
	} else if (lock == 1) {
		if (atomic_cmpset_int(&mtx->mtx_lock, lock, 0) == 0)
			_mtx_unlock(mtx);
	} else {
		_mtx_unlock(mtx);
	}
}

static __inline void
mtx_unlock_ex(mtx_t *mtx)
{
	u_int lock = mtx->mtx_lock;

	if (lock == (MTX_EXCLUSIVE | 1)) {
		mtx->mtx_owner = NULL;
		if (atomic_cmpset_int(&mtx->mtx_lock, lock, 0) == 0)
			_mtx_unlock(mtx);
	} else {
		_mtx_unlock(mtx);
	}
}

static __inline void
mtx_unlock_sh(mtx_t *mtx)
{
	if (atomic_cmpset_int(&mtx->mtx_lock, 1, 0) == 0)
		_mtx_unlock(mtx);
}

/*
 * NOTE: spinlocks are exclusive-only
 */
static __inline void
mtx_spinunlock(mtx_t *mtx)
{
	globaldata_t gd = mycpu;

	mtx_unlock(mtx);

	--gd->gd_spinlocks;
	cpu_ccfence();
	--gd->gd_curthread->td_critcount;
}

/*
 * Return TRUE (non-zero) if the mutex is locked shared or exclusive by
 * anyone, including the owner.
 */
static __inline int
mtx_islocked(mtx_t *mtx)
{
	return(mtx->mtx_lock != 0);
}

/*
 * Return TRUE (non-zero) if the mutex is locked exclusively by anyone,
 * including the owner.  Returns FALSE (0) if the mutex is unlocked or
 * if it is locked shared by one or more entities.
 *
 * A caller wishing to check whether a lock is owned exclusively by it
 * should use mtx_owned().
 */
static __inline int
mtx_islocked_ex(mtx_t *mtx)
{
	return((mtx->mtx_lock & MTX_EXCLUSIVE) != 0);
}

/*
 * Return TRUE (non-zero) if the mutex is not locked.
 */
static __inline int
mtx_notlocked(mtx_t *mtx)
{
	return(mtx->mtx_lock == 0);
}

/*
 * Return TRUE (non-zero) if the mutex is not locked exclusively.
 * The mutex may in an unlocked or shared lock state.
 */
static __inline int
mtx_notlocked_ex(mtx_t *mtx)
{
	return((mtx->mtx_lock & MTX_EXCLUSIVE) != 0);
}

/*
 * Return TRUE (non-zero) if the mutex is exclusively locked by
 * the caller.
 */
static __inline int
mtx_owned(mtx_t *mtx)
{
	return((mtx->mtx_lock & MTX_EXCLUSIVE) && mtx->mtx_owner == curthread);
}

/*
 * Return TRUE (non-zero) if the mutex is not exclusively locked by
 * the caller.
 */
static __inline int
mtx_notowned(mtx_t *mtx)
{
	return((mtx->mtx_lock & MTX_EXCLUSIVE) == 0 ||
	       mtx->mtx_owner != curthread);
}

/*
 * Return the shared or exclusive lock count.  A return value of 0
 * indicate that the mutex is not locked.
 *
 * NOTE: If the mutex is held exclusively by someone other then the
 *	 caller the lock count for the other owner is still returned.
 */
static __inline
int
mtx_lockrefs(mtx_t *mtx)
{
	return(mtx->mtx_lock & MTX_MASK);
}

/*
 * Lock must held and will be released on return.  Returns state
 * which can be passed to mtx_lock_temp_restore() to return the
 * lock to its previous state.
 */
static __inline
mtx_state_t
mtx_lock_temp_release(mtx_t *mtx)
{
	mtx_state_t state;

	state = (mtx->mtx_lock & MTX_EXCLUSIVE);
	mtx_unlock(mtx);

	return state;
}

/*
 * Restore the previous state of a lock released with
 * mtx_lock_temp_release() or mtx_lock_upgrade().
 */
static __inline
void
mtx_lock_temp_restore(mtx_t *mtx, mtx_state_t state)
{
	if (state & MTX_EXCLUSIVE)
		mtx_lock_ex_quick(mtx);
	else
		mtx_lock_sh_quick(mtx);
}

#endif
