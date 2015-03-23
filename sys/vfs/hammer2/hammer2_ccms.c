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
#include "hammer2.h"

int ccms_debug = 0;

void
ccms_cst_init(ccms_cst_t *cst)
{
	bzero(cst, sizeof(*cst));
	spin_init(&cst->spin, "ccmscst");
}

void
ccms_cst_uninit(ccms_cst_t *cst)
{
	KKASSERT(cst->count == 0);
	if (cst->state != CCMS_STATE_INVALID) {
		/* XXX */
	}
}

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
	LOCKENTER;
	if (cst->count < 0 && cst->td == curthread) {
		--cst->count;
		return;
	}

	/*
	 * Otherwise use the spinlock to interlock the operation and sleep
	 * as necessary.
	 */
	hammer2_spin_ex(&cst->spin);
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
		hammer2_spin_unex(&cst->spin);
		panic("ccms_thread_lock: bad state %d\n", state);
	}
	hammer2_spin_unex(&cst->spin);
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
		LOCKENTER;
		return(0);
	}

	hammer2_spin_ex(&cst->spin);
	if (state == CCMS_STATE_SHARED) {
		if (cst->count < 0 || cst->upgrade) {
			hammer2_spin_unex(&cst->spin);
			return (EBUSY);
		}
		++cst->count;
		KKASSERT(cst->td == NULL);
	} else if (state == CCMS_STATE_EXCLUSIVE) {
		if (cst->count != 0 || cst->upgrade) {
			hammer2_spin_unex(&cst->spin);
			return (EBUSY);
		}
		cst->count = -1;
		cst->td = curthread;
	} else {
		hammer2_spin_unex(&cst->spin);
		panic("ccms_thread_lock_nonblock: bad state %d\n", state);
	}
	hammer2_spin_unex(&cst->spin);
	LOCKENTER;
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
		hammer2_spin_ex(&cst->spin);
		++cst->upgrade;
		--cst->count;
		while (cst->count) {
			cst->blocked = 1;
			ssleep(cst, &cst->spin, 0, "ccmsupg", hz);
		}
		cst->count = -1;
		cst->td = curthread;
		hammer2_spin_unex(&cst->spin);
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
		hammer2_spin_ex(&cst->spin);
		--cst->upgrade;
		cst->count = 1;
		cst->td = NULL;
		if (cst->blocked) {
			cst->blocked = 0;
			hammer2_spin_unex(&cst->spin);
			wakeup(cst);
		} else {
			hammer2_spin_unex(&cst->spin);
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
	LOCKEXIT;
	if (cst->count < 0) {
		/*
		 * Exclusive
		 */
		KKASSERT(cst->td == curthread);
		if (cst->count < -1) {
			++cst->count;
			return;
		}
		hammer2_spin_ex(&cst->spin);
		KKASSERT(cst->count == -1);
		cst->count = 0;
		cst->td = NULL;
		if (cst->blocked) {
			cst->blocked = 0;
			hammer2_spin_unex(&cst->spin);
			wakeup(cst);
			return;
		}
		hammer2_spin_unex(&cst->spin);
	} else if (cst->count > 0) {
		/*
		 * Shared
		 */
		hammer2_spin_ex(&cst->spin);
		if (--cst->count == 0 && cst->blocked) {
			cst->blocked = 0;
			hammer2_spin_unex(&cst->spin);
			wakeup(cst);
			return;
		}
		hammer2_spin_unex(&cst->spin);
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
		LOCKEXIT;
		KKASSERT(cst->td == curthread);
		KKASSERT(cst->count == -1);
		hammer2_spin_ex(&cst->spin);
		--cst->upgrade;
		cst->count = 0;
		cst->td = NULL;
		if (cst->blocked) {
			cst->blocked = 0;
			hammer2_spin_unex(&cst->spin);
			wakeup(cst);
		} else {
			hammer2_spin_unex(&cst->spin);
		}
	} else {
		ccms_thread_unlock(cst);
	}
}

int
ccms_thread_lock_owned(ccms_cst_t *cst)
{
	return(cst->count < 0 && cst->td == curthread);
}
