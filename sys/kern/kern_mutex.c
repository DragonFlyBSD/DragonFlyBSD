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
/*
 * Implement fast persistent locks based on atomic_cmpset_int() with
 * semantics similar to lockmgr locks but faster and taking up much less
 * space.  Taken from HAMMER's lock implementation.
 *
 * These are meant to complement our LWKT tokens.  Tokens are only held
 * while the thread is running.  Mutexes can be held across blocking
 * conditions.
 *
 * Most of the support is in sys/mutex[2].h.  We mostly provide backoff
 * functions here.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/sysctl.h>
#include <sys/thread.h>
#include <sys/mutex.h>

#include <machine/cpufunc.h>

#include <sys/thread2.h>
#include <sys/mutex2.h>

static __int64_t mtx_contention_count;
static __int64_t mtx_collision_count;
static __int64_t mtx_wakeup_count;

SYSCTL_QUAD(_kern, OID_AUTO, mtx_contention_count, CTLFLAG_RW,
	    &mtx_contention_count, 0, "");
SYSCTL_QUAD(_kern, OID_AUTO, mtx_collision_count, CTLFLAG_RW,
	    &mtx_collision_count, 0, "");
SYSCTL_QUAD(_kern, OID_AUTO, mtx_wakeup_count, CTLFLAG_RW,
	    &mtx_wakeup_count, 0, "");

/*
 * Exclusive-lock a mutex, block until acquired.  Recursion is allowed.
 */
void
_mtx_lock_ex(mtx_t mtx, const char *ident, int flags)
{
	u_int	lock;
	u_int	nlock;

	for (;;) {
		lock = mtx->mtx_lock;
		if (lock == 0) {
			nlock = MTX_EXCLUSIVE | 1;
			if (atomic_cmpset_int(&mtx->mtx_lock, 0, nlock)) {
				/* mtx_owner set by caller */
				return;
			}
		} else if ((lock & MTX_EXCLUSIVE) &&
			   mtx->mtx_owner == curthread) {
			KKASSERT((lock & MTX_MASK) != MTX_MASK);
			nlock = (lock + 1);
			if (atomic_cmpset_int(&mtx->mtx_lock, 0, nlock))
				return;
		} else {
			nlock = lock | MTX_EXWANTED;
			tsleep_interlock(&mtx->mtx_owner, 0);
			if (atomic_cmpset_int(&mtx->mtx_lock, 0, nlock)) {
				++mtx_contention_count;
				tsleep(&mtx->mtx_owner, flags, ident, 0);
			} else {
				tsleep_remove(curthread);
			}
		}
		++mtx_collision_count;
	}
}

/*
 * Share-lock a mutex, block until acquired.  Recursion is allowed.
 */
void
_mtx_lock_sh(mtx_t mtx, const char *ident, int flags)
{
	u_int	lock;
	u_int	nlock;

	for (;;) {
		lock = mtx->mtx_lock;
		if ((lock & MTX_EXCLUSIVE) == 0) {
			KKASSERT((lock & MTX_MASK) != MTX_MASK);
			nlock = lock + 1;
			if (atomic_cmpset_int(&mtx->mtx_lock, lock, nlock))
				return;
		} else {
			nlock = lock | MTX_SHWANTED;
			tsleep_interlock(mtx, 0);
			if (atomic_cmpset_int(&mtx->mtx_lock, lock, nlock)) {
				++mtx_contention_count;
				tsleep(mtx, flags, ident, 0);
			} else {
				tsleep_remove(curthread);
			}
		}
		++mtx_collision_count;
	}
}

void
_mtx_spinlock_ex(mtx_t mtx)
{
	u_int	lock;
	u_int	nlock;
	int	bb = 1;
	int	bo;

	for (;;) {
		lock = mtx->mtx_lock;
		if (lock == 0) {
			nlock = MTX_EXCLUSIVE | 1;
			if (atomic_cmpset_int(&mtx->mtx_lock, 0, nlock)) {
				/* mtx_owner set by caller */
				return;
			}
		} else if ((lock & MTX_EXCLUSIVE) &&
			   mtx->mtx_owner == curthread) {
			KKASSERT((lock & MTX_MASK) != MTX_MASK);
			nlock = (lock + 1);
			if (atomic_cmpset_int(&mtx->mtx_lock, 0, nlock))
				return;
		} else {
			/* MWAIT here */
			if (bb < 1000)
				++bb;
			cpu_pause();
			for (bo = 0; bo < bb; ++bo)
				;
			++mtx_contention_count;
		}
		++mtx_collision_count;
	}
}

void
_mtx_spinlock_sh(mtx_t mtx)
{
	u_int	lock;
	u_int	nlock;
	int	bb = 1;
	int	bo;

	for (;;) {
		lock = mtx->mtx_lock;
		if ((lock & MTX_EXCLUSIVE) == 0) {
			KKASSERT((lock & MTX_MASK) != MTX_MASK);
			nlock = lock + 1;
			if (atomic_cmpset_int(&mtx->mtx_lock, lock, nlock))
				return;
		} else {
			/* MWAIT here */
			if (bb < 1000)
				++bb;
			cpu_pause();
			for (bo = 0; bo < bb; ++bo)
				;
			++mtx_contention_count;
		}
		++mtx_collision_count;
	}
}

int
_mtx_lock_ex_try(mtx_t mtx)
{
	u_int	lock;
	u_int	nlock;
	int	error = 0;

	for (;;) {
		lock = mtx->mtx_lock;
		if (lock == 0) {
			nlock = MTX_EXCLUSIVE | 1;
			if (atomic_cmpset_int(&mtx->mtx_lock, 0, nlock)) {
				/* mtx_owner set by caller */
				break;
			}
		} else if ((lock & MTX_EXCLUSIVE) &&
			   mtx->mtx_owner == curthread) {
			KKASSERT((lock & MTX_MASK) != MTX_MASK);
			nlock = (lock + 1);
			if (atomic_cmpset_int(&mtx->mtx_lock, 0, nlock))
				break;
		} else {
			error = EAGAIN;
			break;
		}
		++mtx_collision_count;
	}
	return (error);
}

int
_mtx_lock_sh_try(mtx_t mtx)
{
	u_int	lock;
	u_int	nlock;
	int	error = 0;

	for (;;) {
		lock = mtx->mtx_lock;
		if ((lock & MTX_EXCLUSIVE) == 0) {
			KKASSERT((lock & MTX_MASK) != MTX_MASK);
			nlock = lock + 1;
			if (atomic_cmpset_int(&mtx->mtx_lock, lock, nlock))
				break;
		} else {
			error = EAGAIN;
			break;
		}
		++mtx_collision_count;
	}
	return (error);
}

/*
 * If the lock is held exclusively it must be owned by the caller.  If the
 * lock is already a shared lock this operation is a NOP.  A panic will
 * occur if the lock is not held either shared or exclusive.
 *
 * The exclusive count is converted to a shared count.
 */
void
_mtx_downgrade(mtx_t mtx)
{
	u_int	lock;
	u_int	nlock;

	for (;;) {
		lock = mtx->mtx_lock;
		if ((lock & MTX_EXCLUSIVE) == 0) {
			KKASSERT((lock & MTX_MASK) > 0);
			break;
		}
		KKASSERT(mtx->mtx_owner == curthread);
		nlock = lock & ~(MTX_EXCLUSIVE | MTX_SHWANTED);
		if (atomic_cmpset_int(&mtx->mtx_lock, lock, nlock)) {
			if (lock & MTX_SHWANTED) {
				++mtx_wakeup_count;
				wakeup(mtx);
			}
			break;
		}
		++mtx_collision_count;
	}
}

/*
 * Upgrade a shared lock to an exclusive lock.  The upgrade will fail if
 * the shared lock has a count other then 1.  Optimize the most likely case
 * but note that a single cmpset can fail due to WANTED races.
 *
 * If the lock is held exclusively it must be owned by the caller and
 * this function will simply return without doing anything.   A panic will
 * occur if the lock is held exclusively by someone other then the caller.
 *
 * Returns 0 on success, EDEADLK on failure.
 */
int
_mtx_upgrade_try(mtx_t mtx)
{
	u_int	lock;
	u_int	nlock;
	int	error = 0;

	for (;;) {
		lock = mtx->mtx_lock;

		if ((lock & ~MTX_EXWANTED) == 1) {
			nlock = lock | MTX_EXCLUSIVE;
			if (atomic_cmpset_int(&mtx->mtx_lock, lock, nlock)) {
				mtx->mtx_owner = curthread;
				break;
			}
		} else if (lock & MTX_EXCLUSIVE) {
			KKASSERT(mtx->mtx_owner == curthread);
			break;
		} else {
			error = EDEADLK;
			break;
		}
		++mtx_collision_count;
	}
	return (error);
}

/*
 * Unlock a lock.  The caller must hold the lock either shared or exclusive.
 */
void
_mtx_unlock(mtx_t mtx)
{
	u_int	lock;
	u_int	nlock;

	for (;;) {
		lock = mtx->mtx_lock;
		nlock = (lock & (MTX_EXCLUSIVE | MTX_MASK)) - 1;
		if (nlock == 0) {
			if (atomic_cmpset_int(&mtx->mtx_lock, lock, 0)) {
				if (lock & MTX_SHWANTED) {
					++mtx_wakeup_count;
					wakeup(mtx);
				}
				if (lock & MTX_EXWANTED) {
					++mtx_wakeup_count;
					wakeup_one(&mtx->mtx_owner);
				}
			}
		} else if (nlock == MTX_EXCLUSIVE) {
			mtx->mtx_owner = NULL;
			if (atomic_cmpset_int(&mtx->mtx_lock, lock, 0)) {
				if (lock & MTX_SHWANTED) {
					++mtx_wakeup_count;
					wakeup(mtx);
				}
				if (lock & MTX_EXWANTED) {
					++mtx_wakeup_count;
					wakeup_one(&mtx->mtx_owner);
				}
				break;
			}
		} else {
			nlock = lock - 1;
			KKASSERT((nlock & MTX_MASK) != MTX_MASK);
			if (atomic_cmpset_int(&mtx->mtx_lock, lock, nlock))
				break;
		}
		++mtx_collision_count;
	}
}
