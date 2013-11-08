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

static void mtx_chain_link(mtx_t mtx);
static void mtx_delete_link(mtx_t mtx, mtx_link_t link);

/*
 * Exclusive-lock a mutex, block until acquired.  Recursion is allowed.
 *
 * Returns 0 on success, or the tsleep() return code on failure.
 * An error can only be returned if PCATCH is specified in the flags.
 */
static __inline int
__mtx_lock_ex(mtx_t mtx, mtx_link_t link, const char *ident, int flags, int to)
{
	u_int	lock;
	u_int	nlock;
	int	error;

	for (;;) {
		lock = mtx->mtx_lock;
		if (lock == 0) {
			nlock = MTX_EXCLUSIVE | 1;
			if (atomic_cmpset_int(&mtx->mtx_lock, 0, nlock)) {
				mtx->mtx_owner = curthread;
				error = 0;
				break;
			}
		} else if ((lock & MTX_EXCLUSIVE) &&
			   mtx->mtx_owner == curthread) {
			KKASSERT((lock & MTX_MASK) != MTX_MASK);
			nlock = lock + 1;
			if (atomic_cmpset_int(&mtx->mtx_lock, lock, nlock)) {
				error = 0;
				break;
			}
		} else {
			/*
			 * Clearing MTX_EXLINK in lock causes us to loop until
			 * MTX_EXLINK is available.  However, to avoid
			 * unnecessary cpu cache traffic we poll instead.
			 *
			 * Setting MTX_EXLINK in nlock causes us to loop until
			 * we can acquire MTX_EXLINK.
			 *
			 * Also set MTX_EXWANTED coincident with EXLINK, if
			 * not already set.
			 */
			thread_t td;

			if (lock & MTX_EXLINK) {
				cpu_pause();
				++mtx_collision_count;
				continue;
			}
			td = curthread;
			/*lock &= ~MTX_EXLINK;*/
			nlock = lock | MTX_EXWANTED | MTX_EXLINK;
			++td->td_critcount;
			if (atomic_cmpset_int(&mtx->mtx_lock, lock, nlock)) {
				/*
				 * Check for early abort
				 */
				if (link->state == MTX_LINK_ABORTED) {
					atomic_clear_int(&mtx->mtx_lock,
							 MTX_EXLINK);
					--td->td_critcount;
					error = ENOLCK;
					if (mtx->mtx_link == NULL) {
						atomic_clear_int(&mtx->mtx_lock,
								 MTX_EXWANTED);
					}
					break;
				}

				/*
				 * Success.  Link in our structure then
				 * release EXLINK and sleep.
				 */
				link->owner = td;
				link->state = MTX_LINK_LINKED;
				if (mtx->mtx_link) {
					link->next = mtx->mtx_link;
					link->prev = link->next->prev;
					link->next->prev = link;
					link->prev->next = link;
				} else {
					link->next = link;
					link->prev = link;
					mtx->mtx_link = link;
				}
				tsleep_interlock(link, 0);
				atomic_clear_int(&mtx->mtx_lock, MTX_EXLINK);
				--td->td_critcount;

				mycpu->gd_cnt.v_lock_name[0] = 'X';
				strncpy(mycpu->gd_cnt.v_lock_name + 1,
					ident,
					sizeof(mycpu->gd_cnt.v_lock_name) - 2);
				++mycpu->gd_cnt.v_lock_colls;

				error = tsleep(link, flags | PINTERLOCKED,
					       ident, to);
				++mtx_contention_count;

				/*
				 * Normal unlink, we should own the exclusive
				 * lock now.
				 */
				if (link->state == MTX_LINK_LINKED)
					mtx_delete_link(mtx, link);
				if (link->state == MTX_LINK_ACQUIRED) {
					KKASSERT(mtx->mtx_owner == link->owner);
					error = 0;
					break;
				}

				/*
				 * Aborted lock (mtx_abort_ex called).
				 */
				if (link->state == MTX_LINK_ABORTED) {
					error = ENOLCK;
					break;
				}

				/*
				 * tsleep error, else retry.
				 */
				if (error)
					break;
			} else {
				--td->td_critcount;
			}
		}
		++mtx_collision_count;
	}
	return (error);
}

int
_mtx_lock_ex_link(mtx_t mtx, mtx_link_t link,
		  const char *ident, int flags, int to)
{
	return(__mtx_lock_ex(mtx, link, ident, flags, to));
}

int
_mtx_lock_ex(mtx_t mtx, const char *ident, int flags, int to)
{
	struct mtx_link link;

	mtx_link_init(&link);
	return(__mtx_lock_ex(mtx, &link, ident, flags, to));
}

int
_mtx_lock_ex_quick(mtx_t mtx, const char *ident)
{
	struct mtx_link link;

	mtx_link_init(&link);
	return(__mtx_lock_ex(mtx, &link, ident, 0, 0));
}

/*
 * Share-lock a mutex, block until acquired.  Recursion is allowed.
 *
 * Returns 0 on success, or the tsleep() return code on failure.
 * An error can only be returned if PCATCH is specified in the flags.
 *
 * NOTE: Shared locks get a mass-wakeup so if the tsleep fails we
 *	 do not have to chain the wakeup().
 */
static __inline int
__mtx_lock_sh(mtx_t mtx, const char *ident, int flags, int to)
{
	u_int	lock;
	u_int	nlock;
	int	error;

	for (;;) {
		lock = mtx->mtx_lock;
		if ((lock & MTX_EXCLUSIVE) == 0) {
			KKASSERT((lock & MTX_MASK) != MTX_MASK);
			nlock = lock + 1;
			if (atomic_cmpset_int(&mtx->mtx_lock, lock, nlock)) {
				error = 0;
				break;
			}
		} else {
			nlock = lock | MTX_SHWANTED;
			tsleep_interlock(mtx, 0);
			if (atomic_cmpset_int(&mtx->mtx_lock, lock, nlock)) {

				mycpu->gd_cnt.v_lock_name[0] = 'S';
				strncpy(mycpu->gd_cnt.v_lock_name + 1,
					ident,
					sizeof(mycpu->gd_cnt.v_lock_name) - 2);
				++mycpu->gd_cnt.v_lock_colls;

				error = tsleep(mtx, flags | PINTERLOCKED,
					       ident, to);
				if (error)
					break;
				++mtx_contention_count;
				/* retry */
			} else {
				crit_enter();
				tsleep_remove(curthread);
				crit_exit();
			}
		}
		++mtx_collision_count;
	}
	return (error);
}

int
_mtx_lock_sh(mtx_t mtx, const char *ident, int flags, int to)
{
	return (__mtx_lock_sh(mtx, ident, flags, to));
}

int
_mtx_lock_sh_quick(mtx_t mtx, const char *ident)
{
	return (__mtx_lock_sh(mtx, ident, 0, 0));
}

/*
 * Get an exclusive spinlock the hard way.
 */
void
_mtx_spinlock(mtx_t mtx)
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
				mtx->mtx_owner = curthread;
				break;
			}
		} else if ((lock & MTX_EXCLUSIVE) &&
			   mtx->mtx_owner == curthread) {
			KKASSERT((lock & MTX_MASK) != MTX_MASK);
			nlock = lock + 1;
			if (atomic_cmpset_int(&mtx->mtx_lock, lock, nlock))
				break;
		} else {
			/* MWAIT here */
			if (bb < 1000)
				++bb;
			cpu_pause();
			for (bo = 0; bo < bb; ++bo)
				;
			++mtx_contention_count;
		}
		cpu_pause();
		++mtx_collision_count;
	}
}

/*
 * Attempt to acquire a spinlock, if we fail we must undo the
 * gd->gd_spinlocks/gd->gd_curthead->td_critcount predisposition.
 *
 * Returns 0 on success, EAGAIN on failure.
 */
int
_mtx_spinlock_try(mtx_t mtx)
{
	globaldata_t gd = mycpu;
	u_int	lock;
	u_int	nlock;
	int	res = 0;

	for (;;) {
		lock = mtx->mtx_lock;
		if (lock == 0) {
			nlock = MTX_EXCLUSIVE | 1;
			if (atomic_cmpset_int(&mtx->mtx_lock, 0, nlock)) {
				mtx->mtx_owner = gd->gd_curthread;
				break;
			}
		} else if ((lock & MTX_EXCLUSIVE) &&
			   mtx->mtx_owner == gd->gd_curthread) {
			KKASSERT((lock & MTX_MASK) != MTX_MASK);
			nlock = lock + 1;
			if (atomic_cmpset_int(&mtx->mtx_lock, lock, nlock))
				break;
		} else {
			--gd->gd_spinlocks;
			cpu_ccfence();
			--gd->gd_curthread->td_critcount;
			res = EAGAIN;
			break;
		}
		cpu_pause();
		++mtx_collision_count;
	}
	return res;
}

#if 0

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
				break;
		} else {
			/* MWAIT here */
			if (bb < 1000)
				++bb;
			cpu_pause();
			for (bo = 0; bo < bb; ++bo)
				;
			++mtx_contention_count;
		}
		cpu_pause();
		++mtx_collision_count;
	}
}

#endif

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
				mtx->mtx_owner = curthread;
				break;
			}
		} else if ((lock & MTX_EXCLUSIVE) &&
			   mtx->mtx_owner == curthread) {
			KKASSERT((lock & MTX_MASK) != MTX_MASK);
			nlock = lock + 1;
			if (atomic_cmpset_int(&mtx->mtx_lock, lock, nlock))
				break;
		} else {
			error = EAGAIN;
			break;
		}
		cpu_pause();
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
		cpu_pause();
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
				wakeup(mtx);
				++mtx_wakeup_count;
			}
			break;
		}
		cpu_pause();
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
		cpu_pause();
		++mtx_collision_count;
	}
	return (error);
}

/*
 * Unlock a lock.  The caller must hold the lock either shared or exclusive.
 *
 * Any release which makes the lock available when others want an exclusive
 * lock causes us to chain the owner to the next exclusive lock instead of
 * releasing the lock.
 */
void
_mtx_unlock(mtx_t mtx)
{
	u_int	lock;
	u_int	nlock;

	for (;;) {
		lock = mtx->mtx_lock;
		nlock = lock & ~(MTX_SHWANTED | MTX_EXLINK);

		if (nlock == 1) {
			/*
			 * Last release, shared lock, no exclusive waiters.
			 */
			nlock = lock & MTX_EXLINK;
			if (atomic_cmpset_int(&mtx->mtx_lock, lock, nlock))
				break;
		} else if (nlock == (MTX_EXCLUSIVE | 1)) {
			/*
			 * Last release, exclusive lock, no exclusive waiters.
			 * Wake up any shared waiters.
			 */
			mtx->mtx_owner = NULL;
			nlock = lock & MTX_EXLINK;
			if (atomic_cmpset_int(&mtx->mtx_lock, lock, nlock)) {
				if (lock & MTX_SHWANTED) {
					wakeup(mtx);
					++mtx_wakeup_count;
				}
				break;
			}
		} else if (nlock == (MTX_EXWANTED | 1)) {
			/*
			 * Last release, shared lock, with exclusive
			 * waiters.
			 *
			 * Wait for EXLINK to clear, then acquire it.
			 * We could use the cmpset for this but polling
			 * is better on the cpu caches.
			 *
			 * Acquire an exclusive lock leaving the lockcount
			 * set to 1, and get EXLINK for access to mtx_link.
			 */
			thread_t td;

			if (lock & MTX_EXLINK) {
				cpu_pause();
				++mtx_collision_count;
				continue;
			}
			td = curthread;
			/*lock &= ~MTX_EXLINK;*/
			nlock |= MTX_EXLINK | MTX_EXCLUSIVE;
			nlock |= (lock & MTX_SHWANTED);
			++td->td_critcount;
			if (atomic_cmpset_int(&mtx->mtx_lock, lock, nlock)) {
				mtx_chain_link(mtx);
				--td->td_critcount;
				break;
			}
			--td->td_critcount;
		} else if (nlock == (MTX_EXCLUSIVE | MTX_EXWANTED | 1)) {
			/*
			 * Last release, exclusive lock, with exclusive
			 * waiters.
			 *
			 * leave the exclusive lock intact and the lockcount
			 * set to 1, and get EXLINK for access to mtx_link.
			 */
			thread_t td;

			if (lock & MTX_EXLINK) {
				cpu_pause();
				++mtx_collision_count;
				continue;
			}
			td = curthread;
			/*lock &= ~MTX_EXLINK;*/
			nlock |= MTX_EXLINK;
			nlock |= (lock & MTX_SHWANTED);
			++td->td_critcount;
			if (atomic_cmpset_int(&mtx->mtx_lock, lock, nlock)) {
				mtx_chain_link(mtx);
				--td->td_critcount;
				break;
			}
			--td->td_critcount;
		} else {
			/*
			 * Not the last release (shared or exclusive)
			 */
			nlock = lock - 1;
			KKASSERT((nlock & MTX_MASK) != MTX_MASK);
			if (atomic_cmpset_int(&mtx->mtx_lock, lock, nlock))
				break;
		}
		cpu_pause();
		++mtx_collision_count;
	}
}

/*
 * Chain mtx_chain_link.  Called with the lock held exclusively with a
 * single ref count, and also with MTX_EXLINK held.
 */
static void
mtx_chain_link(mtx_t mtx)
{
	mtx_link_t link;
	u_int	lock;
	u_int	nlock;
	u_int	clock;	/* bits we own and want to clear */

	/*
	 * Chain the exclusive lock to the next link.  The caller cleared
	 * SHWANTED so if there is no link we have to wake up any shared
	 * waiters.
	 */
	clock = MTX_EXLINK;
	if ((link = mtx->mtx_link) != NULL) {
		KKASSERT(link->state == MTX_LINK_LINKED);
		if (link->next == link) {
			mtx->mtx_link = NULL;
			clock |= MTX_EXWANTED;
		} else {
			mtx->mtx_link = link->next;
			link->next->prev = link->prev;
			link->prev->next = link->next;
		}
		link->state = MTX_LINK_ACQUIRED;
		mtx->mtx_owner = link->owner;
	} else {
		/*
		 * Chain was empty, release the exclusive lock's last count
		 * as well the bits shown.
		 */
		clock |= MTX_EXCLUSIVE | MTX_EXWANTED | MTX_SHWANTED | 1;
	}

	/*
	 * We have to uset cmpset here to deal with MTX_SHWANTED.  If
	 * we just clear the bits we can miss a wakeup or, worse,
	 * leave mtx_lock unlocked with MTX_SHWANTED still set.
	 */
	for (;;) {
		lock = mtx->mtx_lock;
		nlock = lock & ~clock;

		if (atomic_cmpset_int(&mtx->mtx_lock, lock, nlock)) {
			if (link) {
				/*
				 * Wakeup new exclusive holder.  Leave
				 * SHWANTED intact.
				 */
				wakeup(link);
			} else if (lock & MTX_SHWANTED) {
				/*
				 * Signal any shared waiters (and we also
				 * clear SHWANTED).
				 */
				mtx->mtx_owner = NULL;
				wakeup(mtx);
				++mtx_wakeup_count;
			}
			break;
		}
		cpu_pause();
		++mtx_collision_count;
	}
}

/*
 * Delete a link structure after tsleep has failed.  This code is not
 * in the critical path as most exclusive waits are chained.
 */
static
void
mtx_delete_link(mtx_t mtx, mtx_link_t link)
{
	thread_t td = curthread;
	u_int	lock;
	u_int	nlock;

	/*
	 * Acquire MTX_EXLINK.
	 *
	 * Do not use cmpxchg to wait for EXLINK to clear as this might
	 * result in too much cpu cache traffic.
	 */
	++td->td_critcount;
	for (;;) {
		lock = mtx->mtx_lock;
		if (lock & MTX_EXLINK) {
			cpu_pause();
			++mtx_collision_count;
			continue;
		}
		/* lock &= ~MTX_EXLINK; */
		nlock = lock | MTX_EXLINK;
		if (atomic_cmpset_int(&mtx->mtx_lock, lock, nlock))
			break;
		cpu_pause();
		++mtx_collision_count;
	}

	/*
	 * Delete the link and release EXLINK.
	 */
	if (link->state == MTX_LINK_LINKED) {
		if (link->next == link) {
			mtx->mtx_link = NULL;
		} else {
			mtx->mtx_link = link->next;
			link->next->prev = link->prev;
			link->prev->next = link->next;
		}
		link->state = MTX_LINK_IDLE;
	}
	atomic_clear_int(&mtx->mtx_lock, MTX_EXLINK);
	--td->td_critcount;
}

/*
 * Abort a mutex locking operation, causing mtx_lock_ex_link() to
 * return ENOLCK.  This may be called at any time after the
 * mtx_link is initialized, including both before and after the call
 * to mtx_lock_ex_link().
 */
void
mtx_abort_ex_link(mtx_t mtx, mtx_link_t link)
{
	thread_t td = curthread;
	u_int	lock;
	u_int	nlock;

	/*
	 * Acquire MTX_EXLINK
	 */
	++td->td_critcount;
	for (;;) {
		lock = mtx->mtx_lock;
		if (lock & MTX_EXLINK) {
			cpu_pause();
			++mtx_collision_count;
			continue;
		}
		/* lock &= ~MTX_EXLINK; */
		nlock = lock | MTX_EXLINK;
		if (atomic_cmpset_int(&mtx->mtx_lock, lock, nlock))
			break;
		cpu_pause();
		++mtx_collision_count;
	}

	/*
	 * Do the abort
	 */
	switch(link->state) {
	case MTX_LINK_IDLE:
		/*
		 * Link not started yet
		 */
		link->state = MTX_LINK_ABORTED;
		break;
	case MTX_LINK_LINKED:
		/*
		 * de-link, mark aborted, and wakeup the thread.
		 */
		if (link->next == link) {
			mtx->mtx_link = NULL;
		} else {
			mtx->mtx_link = link->next;
			link->next->prev = link->prev;
			link->prev->next = link->next;
		}
		link->state = MTX_LINK_ABORTED;
		wakeup(link);
		break;
	case MTX_LINK_ACQUIRED:
		/*
		 * Too late, the lock was acquired.  Let it complete.
		 */
		break;
	default:
		/*
		 * link already aborted, do nothing.
		 */
		break;
	}
	atomic_clear_int(&mtx->mtx_lock, MTX_EXLINK);
	--td->td_critcount;
}
