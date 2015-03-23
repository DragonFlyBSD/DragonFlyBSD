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
 * - Exclusive priority over shared to prevent SMP starvation.
 * - locks can be aborted (async callback, if any, will be made w/ENOLCK).
 * - locks can be asynchronous.
 * - synchronous fast path if no blocking occurs (async callback is not
 *   made in this case).
 *
 * Generally speaking any caller-supplied link state must be properly
 * initialized before use.
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

static int mtx_chain_link_ex(mtx_t *mtx, u_int olock);
static int mtx_chain_link_sh(mtx_t *mtx, u_int olock, int addcount);
static void mtx_delete_link(mtx_t *mtx, mtx_link_t *link);

/*
 * Exclusive-lock a mutex, block until acquired unless link is async.
 * Recursion is allowed.
 *
 * Returns 0 on success, the tsleep() return code on failure, EINPROGRESS
 * if async.  If immediately successful an async exclusive lock will return 0
 * and not issue the async callback or link the link structure.  The caller
 * must handle this case (typically this is an optimal code path).
 *
 * A tsleep() error can only be returned if PCATCH is specified in the flags.
 */
static __inline int
__mtx_lock_ex(mtx_t *mtx, mtx_link_t *link, int flags, int to)
{
	thread_t td;
	u_int	lock;
	u_int	nlock;
	int	error;
	int	isasync;

	for (;;) {
		lock = mtx->mtx_lock;
		cpu_ccfence();

		if (lock == 0) {
			nlock = MTX_EXCLUSIVE | 1;
			if (atomic_cmpset_int(&mtx->mtx_lock, 0, nlock)) {
				mtx->mtx_owner = curthread;
				link->state = MTX_LINK_ACQUIRED;
				error = 0;
				break;
			}
			continue;
		}
		if ((lock & MTX_EXCLUSIVE) && mtx->mtx_owner == curthread) {
			KKASSERT((lock & MTX_MASK) != MTX_MASK);
			nlock = lock + 1;
			if (atomic_cmpset_int(&mtx->mtx_lock, lock, nlock)) {
				link->state = MTX_LINK_ACQUIRED;
				error = 0;
				break;
			}
			continue;
		}

		/*
		 * We need MTX_LINKSPIN to manipulate exlink or
		 * shlink.
		 *
		 * We must set MTX_EXWANTED with MTX_LINKSPIN to indicate
		 * pending shared requests.  It cannot be set as a separate
		 * operation prior to acquiring MTX_LINKSPIN.
		 *
		 * To avoid unnecessary cpu cache traffic we poll
		 * for collisions.  It is also possible that EXWANTED
		 * state failing the above test was spurious, so all the
		 * tests must be repeated if we cannot obtain LINKSPIN
		 * with the prior state tests intact (i.e. don't reload
		 * the (lock) variable here, for heaven's sake!).
		 */
		if (lock & MTX_LINKSPIN) {
			cpu_pause();
			++mtx_collision_count;
			continue;
		}
		td = curthread;
		nlock = lock | MTX_EXWANTED | MTX_LINKSPIN;
		++td->td_critcount;
		if (atomic_cmpset_int(&mtx->mtx_lock, lock, nlock) == 0) {
			--td->td_critcount;
			continue;
		}

		/*
		 * Check for early abort.
		 */
		if (link->state == MTX_LINK_ABORTED) {
			if (mtx->mtx_exlink == NULL) {
				atomic_clear_int(&mtx->mtx_lock,
						 MTX_LINKSPIN |
						 MTX_EXWANTED);
			} else {
				atomic_clear_int(&mtx->mtx_lock,
						 MTX_LINKSPIN);
			}
			--td->td_critcount;
			link->state = MTX_LINK_IDLE;
			error = ENOLCK;
			break;
		}

		/*
		 * Add our link to the exlink list and release LINKSPIN.
		 */
		link->owner = td;
		link->state = MTX_LINK_LINKED_EX;
		if (mtx->mtx_exlink) {
			link->next = mtx->mtx_exlink;
			link->prev = link->next->prev;
			link->next->prev = link;
			link->prev->next = link;
		} else {
			link->next = link;
			link->prev = link;
			mtx->mtx_exlink = link;
		}
		isasync = (link->callback != NULL);
		atomic_clear_int(&mtx->mtx_lock, MTX_LINKSPIN);
		--td->td_critcount;

		/* 
		 * If asynchronous lock request return without
		 * blocking, leave link structure linked.
		 */
		if (isasync) {
			error = EINPROGRESS;
			break;
		}

		/*
		 * Wait for lock
		 */
		error = mtx_wait_link(mtx, link, flags, to);
		break;
	}
	return (error);
}

int
_mtx_lock_ex_link(mtx_t *mtx, mtx_link_t *link, int flags, int to)
{
	return(__mtx_lock_ex(mtx, link, flags, to));
}

int
_mtx_lock_ex(mtx_t *mtx, int flags, int to)
{
	mtx_link_t link;

	mtx_link_init(&link);
	return(__mtx_lock_ex(mtx, &link, flags, to));
}

int
_mtx_lock_ex_quick(mtx_t *mtx)
{
	mtx_link_t link;

	mtx_link_init(&link);
	return(__mtx_lock_ex(mtx, &link, 0, 0));
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
__mtx_lock_sh(mtx_t *mtx, mtx_link_t *link, int flags, int to)
{
	thread_t td;
	u_int	lock;
	u_int	nlock;
	int	error;
	int	isasync;

	for (;;) {
		lock = mtx->mtx_lock;
		cpu_ccfence();

		if (lock == 0) {
			nlock = 1;
			if (atomic_cmpset_int(&mtx->mtx_lock, 0, nlock)) {
				error = 0;
				link->state = MTX_LINK_ACQUIRED;
				break;
			}
			continue;
		}
		if ((lock & (MTX_EXCLUSIVE | MTX_EXWANTED)) == 0) {
			KKASSERT((lock & MTX_MASK) != MTX_MASK);
			nlock = lock + 1;
			if (atomic_cmpset_int(&mtx->mtx_lock, lock, nlock)) {
				error = 0;
				link->state = MTX_LINK_ACQUIRED;
				break;
			}
			continue;
		}

		/*
		 * We need MTX_LINKSPIN to manipulate exlink or
		 * shlink.
		 *
		 * We must set MTX_SHWANTED with MTX_LINKSPIN to indicate
		 * pending shared requests.  It cannot be set as a separate
		 * operation prior to acquiring MTX_LINKSPIN.
		 *
		 * To avoid unnecessary cpu cache traffic we poll
		 * for collisions.  It is also possible that EXWANTED
		 * state failing the above test was spurious, so all the
		 * tests must be repeated if we cannot obtain LINKSPIN
		 * with the prior state tests intact (i.e. don't reload
		 * the (lock) variable here, for heaven's sake!).
		 */
		if (lock & MTX_LINKSPIN) {
			cpu_pause();
			++mtx_collision_count;
			continue;
		}
		td = curthread;
		nlock = lock | MTX_SHWANTED | MTX_LINKSPIN;
		++td->td_critcount;
		if (atomic_cmpset_int(&mtx->mtx_lock, lock, nlock) == 0) {
			--td->td_critcount;
			continue;
		}

		/*
		 * Check for early abort.
		 */
		if (link->state == MTX_LINK_ABORTED) {
			if (mtx->mtx_exlink == NULL) {
				atomic_clear_int(&mtx->mtx_lock,
						 MTX_LINKSPIN |
						 MTX_SHWANTED);
			} else {
				atomic_clear_int(&mtx->mtx_lock,
						 MTX_LINKSPIN);
			}
			--td->td_critcount;
			link->state = MTX_LINK_IDLE;
			error = ENOLCK;
			break;
		}

		/*
		 * Add our link to the exlink list and release LINKSPIN.
		 */
		link->owner = td;
		link->state = MTX_LINK_LINKED_SH;
		if (mtx->mtx_shlink) {
			link->next = mtx->mtx_shlink;
			link->prev = link->next->prev;
			link->next->prev = link;
			link->prev->next = link;
		} else {
			link->next = link;
			link->prev = link;
			mtx->mtx_shlink = link;
		}
		isasync = (link->callback != NULL);
		atomic_clear_int(&mtx->mtx_lock, MTX_LINKSPIN);
		--td->td_critcount;

		/* 
		 * If asynchronous lock request return without
		 * blocking, leave link structure linked.
		 */
		if (isasync) {
			error = EINPROGRESS;
			break;
		}

		/*
		 * Wait for lock
		 */
		error = mtx_wait_link(mtx, link, flags, to);
		break;
	}
	return (error);
}

int
_mtx_lock_sh_link(mtx_t *mtx, mtx_link_t *link, int flags, int to)
{
	return(__mtx_lock_sh(mtx, link, flags, to));
}

int
_mtx_lock_sh(mtx_t *mtx, int flags, int to)
{
	mtx_link_t link;

	mtx_link_init(&link);
	return(__mtx_lock_sh(mtx, &link, flags, to));
}

int
_mtx_lock_sh_quick(mtx_t *mtx)
{
	mtx_link_t link;

	mtx_link_init(&link);
	return(__mtx_lock_sh(mtx, &link, 0, 0));
}

/*
 * Get an exclusive spinlock the hard way.
 */
void
_mtx_spinlock(mtx_t *mtx)
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
_mtx_spinlock_try(mtx_t *mtx)
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
_mtx_spinlock_sh(mtx_t *mtx)
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
_mtx_lock_ex_try(mtx_t *mtx)
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
			error = EAGAIN;
			break;
		}
		cpu_pause();
		++mtx_collision_count;
	}
	return (error);
}

int
_mtx_lock_sh_try(mtx_t *mtx)
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
_mtx_downgrade(mtx_t *mtx)
{
	u_int	lock;
	u_int	nlock;

	for (;;) {
		lock = mtx->mtx_lock;
		cpu_ccfence();

		/*
		 * NOP if already shared.
		 */
		if ((lock & MTX_EXCLUSIVE) == 0) {
			KKASSERT((lock & MTX_MASK) > 0);
			break;
		}

		/*
		 * Transfer count to shared.  Any additional pending shared
		 * waiters must be woken up.
		 */
		if (lock & MTX_SHWANTED) {
			if (mtx_chain_link_sh(mtx, lock, 1))
				break;
			/* retry */
		} else {
			nlock = lock & ~MTX_EXCLUSIVE;
			if (atomic_cmpset_int(&mtx->mtx_lock, lock, nlock))
				break;
			/* retry */
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
_mtx_upgrade_try(mtx_t *mtx)
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
 * On the last release we handle any pending chains.
 */
void
_mtx_unlock(mtx_t *mtx)
{
	u_int	lock;
	u_int	nlock;

	for (;;) {
		lock = mtx->mtx_lock;
		cpu_ccfence();

		switch(lock) {
		case MTX_EXCLUSIVE | 1:
			/*
			 * Last release, exclusive lock.
			 * No exclusive or shared requests pending.
			 */
			mtx->mtx_owner = NULL;
			nlock = 0;
			if (atomic_cmpset_int(&mtx->mtx_lock, lock, nlock))
				goto done;
			break;
		case MTX_EXCLUSIVE | MTX_EXWANTED | 1:
		case MTX_EXCLUSIVE | MTX_EXWANTED | MTX_SHWANTED | 1:
			/*
			 * Last release, exclusive lock.
			 * Exclusive requests pending.
			 * Exclusive requests have priority over shared reqs.
			 */
			if (mtx_chain_link_ex(mtx, lock))
				goto done;
			break;
		case MTX_EXCLUSIVE | MTX_SHWANTED | 1:
			/*
			 * Last release, exclusive lock.
			 *
			 * Shared requests are pending.  Transfer our count (1)
			 * to the first shared request, wakeup all shared reqs.
			 */
			if (mtx_chain_link_sh(mtx, lock, 0))
				goto done;
			break;
		case 1:
			/*
			 * Last release, shared lock.
			 * No exclusive or shared requests pending.
			 */
			nlock = 0;
			if (atomic_cmpset_int(&mtx->mtx_lock, lock, nlock))
				goto done;
			break;
		case MTX_EXWANTED | 1:
		case MTX_EXWANTED | MTX_SHWANTED | 1:
			/*
			 * Last release, shared lock.
			 *
			 * Exclusive requests are pending.  Transfer our
			 * count (1) to the next exclusive request.
			 *
			 * Exclusive requests have priority over shared reqs.
			 */
			if (mtx_chain_link_ex(mtx, lock))
				goto done;
			break;
		case MTX_SHWANTED | 1:
			/*
			 * Last release, shared lock.
			 * Shared requests pending.
			 */
			if (mtx_chain_link_sh(mtx, lock, 0))
				goto done;
			break;
		default:
			/*
			 * We have to loop if this is the last release but
			 * someone is fiddling with LINKSPIN.
			 */
			if ((lock & MTX_MASK) == 1) {
				KKASSERT(lock & MTX_LINKSPIN);
				break;
			}

			/*
			 * Not the last release (shared or exclusive)
			 */
			nlock = lock - 1;
			KKASSERT((nlock & MTX_MASK) != MTX_MASK);
			if (atomic_cmpset_int(&mtx->mtx_lock, lock, nlock))
				goto done;
			break;
		}
		/* loop try again */
		cpu_pause();
		++mtx_collision_count;
	}
done:
	;
}

/*
 * Chain pending links.  Called on the last release of an exclusive or
 * shared lock when the appropriate WANTED bit is set.  mtx_lock old state
 * is passed in with the count left at 1, which we can inherit, and other
 * bits which we must adjust in a single atomic operation.
 *
 * Return non-zero on success, 0 if caller needs to retry.
 *
 * NOTE: It's ok if MTX_EXWANTED is in an indeterminant state while we are
 *	 acquiring LINKSPIN as all other cases will also need to acquire
 *	 LINKSPIN when handling the EXWANTED case.
 */
static int
mtx_chain_link_ex(mtx_t *mtx, u_int olock)
{
	thread_t td = curthread;
	mtx_link_t *link;
	u_int	nlock;

	olock &= ~MTX_LINKSPIN;
	nlock = olock | MTX_LINKSPIN | MTX_EXCLUSIVE;
	++td->td_critcount;
	if (atomic_cmpset_int(&mtx->mtx_lock, olock, nlock)) {
		link = mtx->mtx_exlink;
		KKASSERT(link != NULL);
		if (link->next == link) {
			mtx->mtx_exlink = NULL;
			nlock = MTX_LINKSPIN | MTX_EXWANTED;	/* to clear */
		} else {
			mtx->mtx_exlink = link->next;
			link->next->prev = link->prev;
			link->prev->next = link->next;
			nlock = MTX_LINKSPIN;			/* to clear */
		}
		KKASSERT(link->state == MTX_LINK_LINKED_EX);
		mtx->mtx_owner = link->owner;
		cpu_sfence();

		/*
		 * WARNING! The callback can only be safely
		 *	    made with LINKSPIN still held
		 *	    and in a critical section.
		 *
		 * WARNING! The link can go away after the
		 *	    state is set, or after the
		 *	    callback.
		 */
		if (link->callback) {
			link->state = MTX_LINK_CALLEDBACK;
			link->callback(link, link->arg, 0);
		} else {
			link->state = MTX_LINK_ACQUIRED;
			wakeup(link);
		}
		atomic_clear_int(&mtx->mtx_lock, nlock);
		--td->td_critcount;
		++mtx_wakeup_count;
		return 1;
	}
	/* retry */
	--td->td_critcount;
	return 0;
}

/*
 * Flush waiting shared locks.  The lock's prior state is passed in and must
 * be adjusted atomically only if it matches.
 *
 * If addcount is 0, the count for the first shared lock in the chain is
 * assumed to have already been accounted for.
 *
 * If addcount is 1, the count for the first shared lock in the chain has
 * not yet been accounted for.
 */
static int
mtx_chain_link_sh(mtx_t *mtx, u_int olock, int addcount)
{
	thread_t td = curthread;
	mtx_link_t *link;
	u_int	nlock;

	olock &= ~MTX_LINKSPIN;
	nlock = olock | MTX_LINKSPIN;
	nlock &= ~MTX_EXCLUSIVE;
	++td->td_critcount;
	if (atomic_cmpset_int(&mtx->mtx_lock, olock, nlock)) {
		KKASSERT(mtx->mtx_shlink != NULL);
		for (;;) {
			link = mtx->mtx_shlink;
			atomic_add_int(&mtx->mtx_lock, addcount);
			KKASSERT(link->state == MTX_LINK_LINKED_SH);
			if (link->next == link) {
				mtx->mtx_shlink = NULL;
				cpu_sfence();

				/*
				 * WARNING! The callback can only be safely
				 *	    made with LINKSPIN still held
				 *	    and in a critical section.
				 *
				 * WARNING! The link can go away after the
				 *	    state is set, or after the
				 *	    callback.
				 */
				if (link->callback) {
					link->state = MTX_LINK_CALLEDBACK;
					link->callback(link, link->arg, 0);
				} else {
					link->state = MTX_LINK_ACQUIRED;
					wakeup(link);
				}
				++mtx_wakeup_count;
				break;
			}
			mtx->mtx_shlink = link->next;
			link->next->prev = link->prev;
			link->prev->next = link->next;
			cpu_sfence();
			link->state = MTX_LINK_ACQUIRED;
			/* link can go away */
			wakeup(link);
			++mtx_wakeup_count;
			addcount = 1;
		}
		atomic_clear_int(&mtx->mtx_lock, MTX_LINKSPIN |
						 MTX_SHWANTED);
		--td->td_critcount;
		return 1;
	}
	/* retry */
	--td->td_critcount;
	return 0;
}

/*
 * Delete a link structure after tsleep has failed.  This code is not
 * in the critical path as most exclusive waits are chained.
 */
static
void
mtx_delete_link(mtx_t *mtx, mtx_link_t *link)
{
	thread_t td = curthread;
	u_int	lock;
	u_int	nlock;

	/*
	 * Acquire MTX_LINKSPIN.
	 *
	 * Do not use cmpxchg to wait for LINKSPIN to clear as this might
	 * result in too much cpu cache traffic.
	 */
	++td->td_critcount;
	for (;;) {
		lock = mtx->mtx_lock;
		if (lock & MTX_LINKSPIN) {
			cpu_pause();
			++mtx_collision_count;
			continue;
		}
		nlock = lock | MTX_LINKSPIN;
		if (atomic_cmpset_int(&mtx->mtx_lock, lock, nlock))
			break;
		cpu_pause();
		++mtx_collision_count;
	}

	/*
	 * Delete the link and release LINKSPIN.
	 */
	nlock = MTX_LINKSPIN;	/* to clear */

	switch(link->state) {
	case MTX_LINK_LINKED_EX:
		if (link->next == link) {
			mtx->mtx_exlink = NULL;
			nlock |= MTX_EXWANTED;	/* to clear */
		} else {
			mtx->mtx_exlink = link->next;
			link->next->prev = link->prev;
			link->prev->next = link->next;
		}
		break;
	case MTX_LINK_LINKED_SH:
		if (link->next == link) {
			mtx->mtx_shlink = NULL;
			nlock |= MTX_SHWANTED;	/* to clear */
		} else {
			mtx->mtx_shlink = link->next;
			link->next->prev = link->prev;
			link->prev->next = link->next;
		}
		break;
	default:
		/* no change */
		break;
	}
	atomic_clear_int(&mtx->mtx_lock, nlock);
	--td->td_critcount;
}

/*
 * Wait for async lock completion or abort.  Returns ENOLCK if an abort
 * occurred.
 */
int
mtx_wait_link(mtx_t *mtx, mtx_link_t *link, int flags, int to)
{
	int error;

	/*
	 * Sleep.  Handle false wakeups, interruptions, etc.
	 * The link may also have been aborted.
	 */
	error = 0;
	while (link->state & MTX_LINK_LINKED) {
		tsleep_interlock(link, 0);
		cpu_lfence();
		if (link->state & MTX_LINK_LINKED) {
			++mtx_contention_count;
			if (link->state & MTX_LINK_LINKED_SH)
				mycpu->gd_cnt.v_lock_name[0] = 'S';
			else
				mycpu->gd_cnt.v_lock_name[0] = 'X';
			strncpy(mycpu->gd_cnt.v_lock_name + 1,
				mtx->mtx_ident,
				sizeof(mycpu->gd_cnt.v_lock_name) - 2);
			++mycpu->gd_cnt.v_lock_colls;

			error = tsleep(link, flags | PINTERLOCKED,
				       mtx->mtx_ident, to);
			if (error)
				break;
		}
	}

	/*
	 * We are done, make sure the link structure is unlinked.
	 * It may still be on the list due to e.g. EINTR or
	 * EWOULDBLOCK.
	 *
	 * It is possible for the tsleep to race an ABORT and cause
	 * error to be 0.
	 *
	 * The tsleep() can be woken up for numerous reasons and error
	 * might be zero in situations where we intend to return an error.
	 *
	 * (This is the synchronous case so state cannot be CALLEDBACK)
	 */
	switch(link->state) {
	case MTX_LINK_ACQUIRED:
	case MTX_LINK_CALLEDBACK:
		error = 0;
		break;
	case MTX_LINK_ABORTED:
		error = ENOLCK;
		break;
	case MTX_LINK_LINKED_EX:
	case MTX_LINK_LINKED_SH:
		mtx_delete_link(mtx, link);
		/* fall through */
	default:
		if (error == 0)
			error = EWOULDBLOCK;
		break;
	}

	/*
	 * Clear state on status returned.
	 */
	link->state = MTX_LINK_IDLE;

	return error;
}

/*
 * Abort a mutex locking operation, causing mtx_lock_ex_link() to
 * return ENOLCK.  This may be called at any time after the mtx_link
 * is initialized or the status from a previous lock has been
 * returned.  If called prior to the next (non-try) lock attempt, the
 * next lock attempt using this link structure will abort instantly.
 *
 * Caller must still wait for the operation to complete, either from a
 * blocking call that is still in progress or by calling mtx_wait_link().
 *
 * If an asynchronous lock request is possibly in-progress, the caller
 * should call mtx_wait_link() synchronously.  Note that the asynchronous
 * lock callback will NOT be called if a successful abort occurred. XXX
 */
void
mtx_abort_link(mtx_t *mtx, mtx_link_t *link)
{
	thread_t td = curthread;
	u_int	lock;
	u_int	nlock;

	/*
	 * Acquire MTX_LINKSPIN
	 */
	++td->td_critcount;
	for (;;) {
		lock = mtx->mtx_lock;
		if (lock & MTX_LINKSPIN) {
			cpu_pause();
			++mtx_collision_count;
			continue;
		}
		nlock = lock | MTX_LINKSPIN;
		if (atomic_cmpset_int(&mtx->mtx_lock, lock, nlock))
			break;
		cpu_pause();
		++mtx_collision_count;
	}

	/*
	 * Do the abort.
	 *
	 * WARNING! Link structure can disappear once link->state is set.
	 */
	nlock = MTX_LINKSPIN;	/* to clear */

	switch(link->state) {
	case MTX_LINK_IDLE:
		/*
		 * Link not started yet
		 */
		link->state = MTX_LINK_ABORTED;
		break;
	case MTX_LINK_LINKED_EX:
		/*
		 * de-link, mark aborted, and potentially wakeup the thread
		 * or issue the callback.
		 */
		if (link->next == link) {
			if (mtx->mtx_exlink == link) {
				mtx->mtx_exlink = NULL;
				nlock |= MTX_EXWANTED;	/* to clear */
			}
		} else {
			if (mtx->mtx_exlink == link)
				mtx->mtx_exlink = link->next;
			link->next->prev = link->prev;
			link->prev->next = link->next;
		}

		/*
		 * When aborting the async callback is still made.  We must
		 * not set the link status to ABORTED in the callback case
		 * since there is nothing else to clear its status if the
		 * link is reused.
		 */
		if (link->callback) {
			link->state = MTX_LINK_CALLEDBACK;
			link->callback(link, link->arg, ENOLCK);
		} else {
			link->state = MTX_LINK_ABORTED;
			wakeup(link);
		}
		++mtx_wakeup_count;
		break;
	case MTX_LINK_LINKED_SH:
		/*
		 * de-link, mark aborted, and potentially wakeup the thread
		 * or issue the callback.
		 */
		if (link->next == link) {
			if (mtx->mtx_shlink == link) {
				mtx->mtx_shlink = NULL;
				nlock |= MTX_SHWANTED;	/* to clear */
			}
		} else {
			if (mtx->mtx_shlink == link)
				mtx->mtx_shlink = link->next;
			link->next->prev = link->prev;
			link->prev->next = link->next;
		}

		/*
		 * When aborting the async callback is still made.  We must
		 * not set the link status to ABORTED in the callback case
		 * since there is nothing else to clear its status if the
		 * link is reused.
		 */
		if (link->callback) {
			link->state = MTX_LINK_CALLEDBACK;
			link->callback(link, link->arg, ENOLCK);
		} else {
			link->state = MTX_LINK_ABORTED;
			wakeup(link);
		}
		++mtx_wakeup_count;
		break;
	case MTX_LINK_ACQUIRED:
	case MTX_LINK_CALLEDBACK:
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
	atomic_clear_int(&mtx->mtx_lock, nlock);
	--td->td_critcount;
}
