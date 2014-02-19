/*
 * Copyright (c) 2006,2012-2014 The DragonFly Project.  All rights reserved.
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
 * The Cache Coherency Management System (CCMS)
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/objcache.h>
#include <sys/sysctl.h>
#include <sys/uio.h>
#include <machine/limits.h>

#include <sys/spinlock2.h>

#include "hammer2_ccms.h"

int ccms_debug = 0;

/*
 * Initialize a new CCMS dataspace.  Create a new RB tree with a single
 * element covering the entire 64 bit offset range.  This simplifies
 * algorithms enormously by removing a number of special cases.
 */
void
ccms_domain_init(ccms_domain_t *dom)
{
	bzero(dom, sizeof(*dom));
	/*kmalloc_create(&dom->mcst, "CCMS-cst");*/
	/*dom->root.domain = dom;*/
}

void
ccms_domain_uninit(ccms_domain_t *dom)
{
	/*kmalloc_destroy(&dom->mcst);*/
}

void
ccms_cst_init(ccms_cst_t *cst, void *handle)
{
	bzero(cst, sizeof(*cst));
	spin_init(&cst->spin);
	cst->handle = handle;
}

void
ccms_cst_uninit(ccms_cst_t *cst)
{
	KKASSERT(cst->count == 0);
	if (cst->state != CCMS_STATE_INVALID) {
		/* XXX */
	}
	cst->handle = NULL;
}

#if 0
/*
 * Acquire an operational CCMS lock on multiple CSTs.
 *
 * This code is in the critical path and highly streamlined.
 */
void
ccms_lock_get(ccms_lock_t *lock)
{
	ccms_inode_t *cino = lock->cino;

again:
	lock->flags &= ~CCMS_LOCK_FAILED;

	/*
	 * Acquire all local locks first, then resolve them against the
	 * remote cache state.  Order is important here.
	 */
	if (lock->req_t) {
		KKASSERT(lock->req_d <= lock->req_t);
		KKASSERT(lock->req_a <= lock->req_t);
		ccms_thread_lock(&cino->topo_cst, lock->req_t);
	}
	if (lock->req_a)
		ccms_thread_lock(&cino->attr_cst, lock->req_a);
	if (lock->req_d)
		ccms_thread_lock(&cino->data_cst[0], lock->req_d);

	/*
	 * Once the local locks are established the CST grant state cannot
	 * be pulled out from under us.  However, it is entirely possible
	 * to deadlock on it so when CST grant state cannot be obtained
	 * trivially we have to unwind our local locks, then get the state,
	 * and then loop.
	 */
	if (lock->req_t > cino->topo_cst.state) {
		ccms_rstate_get(lock, &cino->topo_cst, lock->req_t);
	} else if (cino->topo_cst.state == CCMS_STATE_INVALID) {
		ccms_rstate_get(lock, &cino->topo_cst, CCMS_STATE_ALLOWED);
	} else if (cino->topo_cst.state == CCMS_STATE_SHARED &&
		    (lock->req_d > CCMS_STATE_SHARED ||
		     lock->req_a > CCMS_STATE_SHARED)) {
		ccms_rstate_get(lock, &cino->topo_cst, CCMS_STATE_ALLOWED);
	}
	/* else the rstate is compatible */

	if (lock->req_a > cino->attr_cst.state)
		ccms_rstate_get(lock, &cino->attr_cst, lock->req_a);

	if (lock->req_d > cino->data_cst[0].state)
		ccms_rstate_get(lock, &cino->data_cst[0], lock->req_d);

	/*
	 * If the ccms_rstate_get() code deadlocks (or even if it just
	 * blocks), it will release all local locks and set the FAILED
	 * bit.  The routine will still acquire the requested remote grants
	 * before returning but since the local locks are lost at that
	 * point the remote grants are no longer protected and we have to
	 * retry.
	 */
	if (lock->flags & CCMS_LOCK_FAILED) {
		goto again;
	}
}

/*
 * Release a previously acquired CCMS lock.
 */
void
ccms_lock_put(ccms_lock_t *lock)
{
	ccms_inode_t *cino = lock->cino;

	if (lock->req_d) {
		ccms_thread_unlock(&cino->data_cst[0]);
	}
	if (lock->req_a) {
		ccms_thread_unlock(&cino->attr_cst);
	}
	if (lock->req_t) {
		ccms_thread_unlock(&cino->topo_cst);
	}
}

#endif

/************************************************************************
 *			    CST SUPPORT FUNCTIONS			*
 ************************************************************************/

/*
 * Acquire local cache state & lock.  If the current thread already holds
 * the lock exclusively we bump the exclusive count, even if the thread is
 * trying to get a shared lock.
 */
void
ccms_thread_lock(ccms_cst_t *cst, ccms_state_t state)
{
	/*
	 * Regardless of the type of lock requested if the current thread
	 * already holds an exclusive lock we bump the exclusive count and
	 * return.  This requires no spinlock.
	 */
	if (cst->count < 0 && cst->td == curthread) {
		--cst->count;
		return;
	}

	/*
	 * Otherwise use the spinlock to interlock the operation and sleep
	 * as necessary.
	 */
	spin_lock(&cst->spin);
	if (state == CCMS_STATE_SHARED) {
		while (cst->count < 0 || cst->upgrade) {
			cst->blocked = 1;
			ssleep(cst, &cst->spin, 0, "ccmslck", hz);
		}
		++cst->count;
		KKASSERT(cst->td == NULL);
	} else if (state == CCMS_STATE_EXCLUSIVE) {
		while (cst->count != 0 || cst->upgrade) {
			cst->blocked = 1;
			ssleep(cst, &cst->spin, 0, "ccmslck", hz);
		}
		cst->count = -1;
		cst->td = curthread;
	} else {
		spin_unlock(&cst->spin);
		panic("ccms_thread_lock: bad state %d\n", state);
	}
	spin_unlock(&cst->spin);
}

/*
 * Same as ccms_thread_lock() but acquires the lock non-blocking.  Returns
 * 0 on success, EBUSY on failure.
 */
int
ccms_thread_lock_nonblock(ccms_cst_t *cst, ccms_state_t state)
{
	if (cst->count < 0 && cst->td == curthread) {
		--cst->count;
		return(0);
	}

	spin_lock(&cst->spin);
	if (state == CCMS_STATE_SHARED) {
		if (cst->count < 0 || cst->upgrade) {
			spin_unlock(&cst->spin);
			return (EBUSY);
		}
		++cst->count;
		KKASSERT(cst->td == NULL);
	} else if (state == CCMS_STATE_EXCLUSIVE) {
		if (cst->count != 0 || cst->upgrade) {
			spin_unlock(&cst->spin);
			return (EBUSY);
		}
		cst->count = -1;
		cst->td = curthread;
	} else {
		spin_unlock(&cst->spin);
		panic("ccms_thread_lock_nonblock: bad state %d\n", state);
	}
	spin_unlock(&cst->spin);
	return(0);
}

ccms_state_t
ccms_thread_lock_temp_release(ccms_cst_t *cst)
{
	if (cst->count < 0) {
		ccms_thread_unlock(cst);
		return(CCMS_STATE_EXCLUSIVE);
	}
	if (cst->count > 0) {
		ccms_thread_unlock(cst);
		return(CCMS_STATE_SHARED);
	}
	return (CCMS_STATE_INVALID);
}

void
ccms_thread_lock_temp_restore(ccms_cst_t *cst, ccms_state_t ostate)
{
	ccms_thread_lock(cst, ostate);
}

/*
 * Temporarily upgrade a thread lock for making local structural changes.
 * No new shared or exclusive locks can be acquired by others while we are
 * upgrading, but other upgraders are allowed.
 */
ccms_state_t
ccms_thread_lock_upgrade(ccms_cst_t *cst)
{
	/*
	 * Nothing to do if already exclusive
	 */
	if (cst->count < 0) {
		KKASSERT(cst->td == curthread);
		return(CCMS_STATE_EXCLUSIVE);
	}

	/*
	 * Convert a shared lock to exclusive.
	 */
	if (cst->count > 0) {
		spin_lock(&cst->spin);
		++cst->upgrade;
		--cst->count;
		while (cst->count) {
			cst->blocked = 1;
			ssleep(cst, &cst->spin, 0, "ccmsupg", hz);
		}
		cst->count = -1;
		cst->td = curthread;
		spin_unlock(&cst->spin);
		return(CCMS_STATE_SHARED);
	}
	panic("ccms_thread_lock_upgrade: not locked");
	/* NOT REACHED */
	return(0);
}

void
ccms_thread_lock_downgrade(ccms_cst_t *cst, ccms_state_t ostate)
{
	if (ostate == CCMS_STATE_SHARED) {
		KKASSERT(cst->td == curthread);
		KKASSERT(cst->count == -1);
		spin_lock(&cst->spin);
		--cst->upgrade;
		cst->count = 1;
		cst->td = NULL;
		if (cst->blocked) {
			cst->blocked = 0;
			spin_unlock(&cst->spin);
			wakeup(cst);
		} else {
			spin_unlock(&cst->spin);
		}
	}
	/* else nothing to do if excl->excl */
}

/*
 * Release a local thread lock
 */
void
ccms_thread_unlock(ccms_cst_t *cst)
{
	if (cst->count < 0) {
		/*
		 * Exclusive
		 */
		KKASSERT(cst->td == curthread);
		if (cst->count < -1) {
			++cst->count;
			return;
		}
		spin_lock(&cst->spin);
		KKASSERT(cst->count == -1);
		cst->count = 0;
		cst->td = NULL;
		if (cst->blocked) {
			cst->blocked = 0;
			spin_unlock(&cst->spin);
			wakeup(cst);
			return;
		}
		spin_unlock(&cst->spin);
	} else if (cst->count > 0) {
		/*
		 * Shared
		 */
		spin_lock(&cst->spin);
		if (--cst->count == 0 && cst->blocked) {
			cst->blocked = 0;
			spin_unlock(&cst->spin);
			wakeup(cst);
			return;
		}
		spin_unlock(&cst->spin);
	} else {
		panic("ccms_thread_unlock: bad zero count\n");
	}
}

void
ccms_thread_lock_setown(ccms_cst_t *cst)
{
	KKASSERT(cst->count < 0);
	cst->td = curthread;
}

/*
 * Release a previously upgraded local thread lock
 */
void
ccms_thread_unlock_upgraded(ccms_cst_t *cst, ccms_state_t ostate)
{
	if (ostate == CCMS_STATE_SHARED) {
		KKASSERT(cst->td == curthread);
		KKASSERT(cst->count == -1);
		spin_lock(&cst->spin);
		--cst->upgrade;
		cst->count = 0;
		cst->td = NULL;
		if (cst->blocked) {
			cst->blocked = 0;
			spin_unlock(&cst->spin);
			wakeup(cst);
		} else {
			spin_unlock(&cst->spin);
		}
	} else {
		ccms_thread_unlock(cst);
	}
}

#if 0
/*
 * Release a local thread lock with special handling of the last lock
 * reference.
 *
 * If no upgrades are in progress then the last reference to the lock will
 * upgrade the lock to exclusive (if it was shared) and return 0 without
 * unlocking it.
 *
 * If more than one reference remains, or upgrades are in progress,
 * we drop the reference and return non-zero to indicate that more
 * locks are present or pending.
 */
int
ccms_thread_unlock_zero(ccms_cst_t *cst)
{
	if (cst->count < 0) {
		/*
		 * Exclusive owned by us, no races possible as long as it
		 * remains negative.  Return 0 and leave us locked on the
		 * last lock.
		 */
		KKASSERT(cst->td == curthread);
		if (cst->count == -1) {
			spin_lock(&cst->spin);
			if (cst->upgrade) {
				cst->count = 0;
				if (cst->blocked) {
					cst->blocked = 0;
					spin_unlock(&cst->spin);
					wakeup(cst);
				} else {
					spin_unlock(&cst->spin);
				}
				return(1);
			}
			spin_unlock(&cst->spin);
			return(0);
		}
		++cst->count;
	} else {
		/*
		 * Convert the last shared lock to an exclusive lock
		 * and return 0.
		 *
		 * If there are upgrades pending the cst is unlocked and
		 * the upgrade waiters are woken up.  The upgrade count
		 * prevents new exclusive holders for the duration.
		 */
		spin_lock(&cst->spin);
		KKASSERT(cst->count > 0);
		if (cst->count == 1) {
			if (cst->upgrade) {
				cst->count = 0;
				if (cst->blocked) {
					cst->blocked = 0;
					spin_unlock(&cst->spin);
					wakeup(cst);
				} else {
					spin_unlock(&cst->spin);
				}
				return(1);
			} else {
				cst->count = -1;
				cst->td = curthread;
				spin_unlock(&cst->spin);
				return(0);
			}
		}
		--cst->count;
		spin_unlock(&cst->spin);
	}
	return(1);
}
#endif

int
ccms_thread_lock_owned(ccms_cst_t *cst)
{
	return(cst->count < 0 && cst->td == curthread);
}


#if 0
/*
 * Acquire remote grant state.  This routine can be used to upgrade or
 * downgrade the state.  If it blocks it will release any local locks
 * acquired via (lock) but then it will continue getting the requested
 * remote grant.
 */
static
void
ccms_rstate_get(ccms_lock_t *lock, ccms_cst_t *cst, ccms_state_t state)
{
	/* XXX */
	cst->state = state;
}

#endif
