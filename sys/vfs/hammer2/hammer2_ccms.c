/*
 * Copyright (c) 2006,2012 The DragonFly Project.  All rights reserved.
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

struct ccms_lock_scan_info {
	ccms_inode_t	*cino;
	ccms_lock_t	*lock;
	ccms_cst_t	*coll_cst;
	int		rstate_upgrade_needed;
};

static int ccms_cst_cmp(ccms_cst_t *b1, ccms_cst_t *b2);
static int ccms_lock_scan_cmp(ccms_cst_t *b1, void *arg);

static int ccms_lock_get_match(ccms_cst_t *cst, void *arg);
static int ccms_lock_undo_match(ccms_cst_t *cst, void *arg);
static int ccms_lock_redo_match(ccms_cst_t *cst, void *arg);
static int ccms_lock_upgrade_match(ccms_cst_t *cst, void *arg);
static int ccms_lock_put_match(ccms_cst_t *cst, void *arg);

static void ccms_lstate_get(ccms_cst_t *cst, ccms_state_t state);
static void ccms_lstate_put(ccms_cst_t *cst);
static void ccms_rstate_get(ccms_cst_t *cst, ccms_state_t state);
static void ccms_rstate_put(ccms_cst_t *cst);

struct ccms_rb_tree;
RB_GENERATE3(ccms_rb_tree, ccms_cst, rbnode, ccms_cst_cmp,
	     ccms_off_t, beg_offset, end_offset);
static MALLOC_DEFINE(M_CCMS, "CCMS", "Cache Coherency Management System");

static int ccms_debug = 0;

/*
 * These helpers are called to manage the CST cache so we can avoid
 * unnecessary kmalloc()'s and kfree()'s in hot paths.
 *
 * ccms_free_pass1() must be called with the spinlock held.
 * ccms_free_pass2() must be called with the spinlock not held.
 */
static __inline
ccms_cst_t *
ccms_free_pass1(ccms_inode_t *cino, int keep)
{
	ccms_cst_t *cst;
	ccms_cst_t **cstp;

	cstp = &cino->free_cache;
	while ((cst = *cstp) != NULL && keep) {
		cstp = &cst->free_next;
		--keep;
	}
	*cstp = NULL;
	return (cst);
}

static __inline
void
ccms_free_pass2(ccms_cst_t *next)
{
	ccms_cst_t *cst;
	ccms_domain_t *dom;

	while ((cst = next) != NULL) {
		next = cst->free_next;
		cst->free_next = NULL;

		dom = cst->cino->domain;
		atomic_add_int(&dom->cst_count, -1);

		kfree(cst, dom->mcst);
	}
}

/*
 * Initialize a new CCMS dataspace.  Create a new RB tree with a single
 * element covering the entire 64 bit offset range.  This simplifies
 * algorithms enormously by removing a number of special cases.
 */
void
ccms_domain_init(ccms_domain_t *dom)
{
	bzero(dom, sizeof(*dom));
	kmalloc_create(&dom->mcst, "CCMS-cst");
	/*dom->root.domain = dom;*/
}

void
ccms_domain_uninit(ccms_domain_t *dom)
{
	kmalloc_destroy(&dom->mcst);
}

#if 0
/*
 * Initialize a ccms_inode for use.  The inode will be initialized but
 * is not yet connected to the rest of the topology.  However, it can
 * still be used stand-alone if desired without being connected to the
 * topology.
 */
void
ccms_inode_init(ccms_domain_t *dom, ccms_inode_t *cino, void *handle)
{
	ccms_cst_t *cst;

	bzero(cino, sizeof(*cino));

	spin_init(&cino->spin);
	RB_INIT(&cino->tree);
	cino->domain = dom;
	cino->handle = handle;
	/* cino->attr_cst.cino = cino; no rbtree association */
	cino->attr_cst.lstate = CCMS_STATE_INVALID;
	cino->attr_cst.rstate = CCMS_STATE_INVALID;

	/*
	 * The dataspace must be initialized w/cache-state set to INVALID
	 * for the entire range.
	 */
	cst = kmalloc(sizeof(*cst), dom->mcst, M_WAITOK | M_ZERO);
	cst->cino = cino;
	cst->flags = CCMS_CST_DYNAMIC;
	cst->beg_offset = 0;
	cst->end_offset = 0xFFFFFFFFFFFFFFFFLLU;
	cst->lstate = CCMS_STATE_INVALID;
	cst->rstate = CCMS_STATE_INVALID;
	RB_INSERT(ccms_rb_tree, &cino->tree, cst);
	atomic_add_int(&dom->cst_count, 1);
}

/*
 * Associate the topology CST with a CCMS inode.  The topology CST must
 * be held locked (typically SHARED) by the caller.  The caller is responsible
 * for interlocking a unique ccms_inode to prevent SMP races.
 */
void
ccms_inode_associate(ccms_inode_t *cino, ccms_cst_t *topo_cst)
{
	KKASSERT(topo_cst->tag.cino == NULL);

	spin_lock(&cino->spin);
	topo_cst->tag.cino = cino;
	topo_cst->flags |= CCMS_CST_INODE;

	cino->topo_cst = topo_cst;
	cino->parent = topo_cst->cino;
	cino->flags |= CCMS_INODE_INSERTED;
	spin_unlock(&cino->spin);
}

#if 0

int
ccms_lock_get(ccms_inode_t *cino, ccms_lock_t *lock)

	spin_lock(&cpar->spin);
	spin_lock(&cino->spin);

	KKASSERT((cino->flags & CCMS_INODE_INSERTED) == 0);
	cino->topo_cst.beg_offset = key;
	cino->topo_cst.end_offset = key;

	if (RB_INSERT(ccms_rb_tree, &cpar->tree, &cino->topo_cst)) {
		spin_unlock(&cino->spin);
		spin_unlock(&cpar->spin);
		panic("ccms_inode_insert: duplicate entry");
	}
	cino->parent = cpar;
	cino->flags |= CCMS_INODE_INSERTED;
	spin_unlock(&cino->spin);
	spin_unlock(&cpar->spin);
}

#endif

/*
 * Delete an inode from the topology.  The inode can remain in active use
 * after the deletion (e.g. when unlinking a file which still has open
 * descriptors) but it's topo_cst is removed from its parent.
 *
 * If the caller is destroying the ccms_inode the caller must call
 * ccms_inode_uninit() to invalidate the cache state (which can block).
 */
void
ccms_inode_disassociate(ccms_inode_t *cino)
{
	ccms_inode_t *cpar;
	ccms_cst_t *topo_cst;
	int flags;

	/*
	 * Interlock with the DELETING flag.
	 */
	spin_lock(&cino->spin);
	flags = cino->flags;
	cino->flags |= CCMS_INODE_DELETING;
	spin_unlock(&cino->spin);

	if (flags & CCMS_INODE_DELETING)
		return;
	if ((flags & CCMS_INODE_INSERTED) == 0)
		return;

	/*
	 *
	 */
	topo_cst = cino->topo_cst;

ccms_lock_put(ccms_inode_t *cino, ccms_lock_t *lock)

	/*
	 * We have the interlock, we are the only ones who can delete
	 * the inode now.
	 */
	cpar = cino->parent;
	spin_lock(&cpar->spin);
	spin_lock(&cino->spin);
	KKASSERT(cpar == cino->parent);

	cino->flags &= ~CCMS_INODE_INSERTED;
	RB_REMOVE(ccms_rb_tree, &cpar->tree, &cino->topo_cst);

	spin_unlock(&cino->spin);
	spin_unlock(&cpar->spin);
}

/*
 * The caller has removed the inode from the topology and is now trying
 * to destroy the structure.  This routine flushes the cache state and
 * can block on third-party interactions.
 *
 * NOTE: Caller must have already destroyed any recursive inode state.
 */
void
ccms_inode_uninit(ccms_inode_t *cino)
{
	ccms_cst_t *scan;

	KKASSERT((cino->flags & CCMS_INODE_INSERTED) == 0);
	spin_lock(&cino->spin);

	while ((scan = RB_ROOT(&cino->tree)) != NULL) {
		KKASSERT(scan->flags & CCMS_CST_DYNAMIC);
		KKASSERT((scan->flags & CCMS_CST_DELETING) == 0);
		RB_REMOVE(ccms_rb_tree, &cino->tree, scan);
		scan->flags |= CCMS_CST_DELETING;
		scan->flags &= ~CCMS_CST_INSERTED;
		spin_unlock(&cino->spin);

		/*
		 * Inval can be called without the inode spinlock because
		 * we own the DELETING flag.
		 */
		ccms_lstate_put(scan);
		ccms_rstate_put(scan);
		atomic_add_int(&cino->domain->cst_count, -1);

		kfree(scan, cino->domain->mcst);
		spin_lock(&cino->spin);
	}
	KKASSERT((cino->attr_cst.flags & CCMS_CST_DELETING) == 0);
	cino->attr_cst.flags |= CCMS_CST_DELETING;
	KKASSERT((cino->topo_cst.flags & CCMS_CST_DELETING) == 0);
	cino->topo_cst.flags |= CCMS_CST_DELETING;
	spin_unlock(&cino->spin);

	/*
	 * Inval can be called without the inode spinlock because
	 * we own the DELETING flag.  Similarly we can clear cino->domain
	 * and cino->handle because we own the DELETING flag on the cino.
	 */
	ccms_lstate_put(&cino->attr_cst);
	ccms_rstate_put(&cino->attr_cst);
	ccms_lstate_put(&cino->topo_cst);
	ccms_rstate_put(&cino->topo_cst);

	/*
	 * Clean out the ccms_inode free CST cache
	 */
	spin_lock(&cino->spin);
	scan = ccms_free_pass1(cino, 0);
	spin_unlock(&cino->spin);
	ccms_free_pass2(scan);

	cino->domain = NULL;
	cino->handle = NULL;
}

#endif

/*
 * This is the core CCMS lock acquisition code and is typically called
 * by program-specific wrappers which initialize the lock structure.
 *
 * Three cache coherent domains can be obtained, the topological 't'
 * domain, the attribute 'a' domain, and a range in the data 'd' domain.
 *
 * A topological CCMS lock covers the entire attribute and data domain
 * plus recursively covers the entire directory sub-tree, so if a topo
 * lock is requested the other 'a' and 'd' locks currently assert if
 * specified in the same request.
 *
 * You can get both an 'a' and a 'd' lock at the same time and, in
 * particular, a VFS can use the 'a' lock to also lock the related
 * VFS inode structure if it desires to.  HAMMER2 utilizes this feature.
 *
 * Topo locks are typically needed for rename operations and topo CST
 * cache state on the backend can be used to limit the number of dynamic
 * CST allocations backing the live CCMS locks.
 */
int
ccms_lock_get(ccms_inode_t *cino, ccms_lock_t *lock)
{
	struct ccms_lock_scan_info info;
	ccms_cst_t *cst;
	int use_redo = 0;
	ccms_state_t highest_state;

	/*
	 * Live local locks prevent remotes from downgrading the rstate,
	 * so we have to acquire a local lock before testing rstate.  If
	 *
	 * The local lock must be released if a remote upgrade is required
	 * to avoid a deadlock, and we retry in that situation.
	 */
again:
	if (lock->tstate) {
		KKASSERT(lock->astate == 0 && lock->dstate == 0);
		lock->icst = &cino->topo_cst;
		ccms_lstate_get(lock->icst, lock->tstate);

		if (cino->topo_cst.rstate < lock->tstate) {
			ccms_lstate_put(&cino->topo_cst);
			ccms_rstate_get(&cino->topo_cst, lock->tstate);
			goto again;
		}
	} else {
		/*
		 * The topo rstate must be at least ALLOWED for us to be
		 * able to acquire any other cache state.  If the topo
		 * rstate is already higher than that then we may have
		 * to upgrade it further to cover the lstate's we are
		 * requesting.
		 */
		highest_state = CCMS_STATE_ALLOWED;
		if (cino->topo_cst.rstate > highest_state) {
			if (highest_state < lock->astate)
				highest_state = lock->astate;
			if (highest_state < lock->dstate)
				highest_state = lock->dstate;
		}
		if (cino->topo_cst.rstate < highest_state)
			ccms_rstate_get(&cino->topo_cst, highest_state);
		/* no need to retry */
	}
	if (lock->astate) {
		lock->icst = &cino->attr_cst;
		ccms_lstate_get(lock->icst, lock->astate);

		if (cino->attr_cst.rstate < lock->astate) {
			ccms_lstate_put(&cino->attr_cst);
			if (lock->tstate)
				ccms_lstate_put(&cino->topo_cst);
			ccms_rstate_get(&cino->attr_cst, lock->astate);
			goto again;
		}
	}

	/*
	 * The data-lock is a range-lock and requires a bit more code.
	 * The CST space is partitioned so the precise range is covered.
	 *
	 * Multiple CST's may be involved and dcst points to the left hand
	 * edge.
	 */
	if (lock->dstate) {
		info.lock = lock;
		info.cino = cino;
		info.coll_cst = NULL;

		spin_lock(&cino->spin);

		/*
		 * Make sure cino has enough free CSTs to cover the operation,
		 * so we can hold the spinlock through the scan later on.
		 */
		while (cino->free_cache == NULL ||
		       cino->free_cache->free_next == NULL) {
			spin_unlock(&cino->spin);
			cst = kmalloc(sizeof(*cst), cino->domain->mcst,
				      M_WAITOK | M_ZERO);
			atomic_add_int(&cino->domain->cst_count, 1);
			spin_lock(&cino->spin);
			cst->free_next = cino->free_cache;
			cino->free_cache = cst;
		}

		/*
		 * The partitioning code runs with the spinlock held.  If
		 * we've already partitioned due to having to do an rstate
		 * upgrade we run a redo instead of a get.
		 */
		info.rstate_upgrade_needed = 0;
		if (use_redo == 0) {
			RB_SCAN(ccms_rb_tree, &cino->tree, ccms_lock_scan_cmp,
				ccms_lock_get_match, &info);
		} else {
			RB_SCAN(ccms_rb_tree, &cino->tree, ccms_lock_scan_cmp,
				ccms_lock_redo_match, &info);
		}

		/*
		 * If a collision occured, undo the fragments we were able
		 * to obtain, block, and try again.
		 */
		while (info.coll_cst != NULL) {
			RB_SCAN(ccms_rb_tree, &cino->tree, ccms_lock_scan_cmp,
				ccms_lock_undo_match, &info);
			info.coll_cst->blocked = 1;
			info.coll_cst = NULL;
			ssleep(info.coll_cst, &cino->spin, 0, "ccmsget", hz);
			info.rstate_upgrade_needed = 0;
			RB_SCAN(ccms_rb_tree, &cino->tree, ccms_lock_scan_cmp,
				ccms_lock_redo_match, &info);
		}

		/*
		 * If the rstate needs to be upgraded we have to undo the
		 * local locks (but we retain the partitioning).
		 *
		 * Set use_redo to indicate that the partioning was retained
		 * (i.e. lrefs and rrefs remain intact).
		 */
		if (info.rstate_upgrade_needed) {
			RB_SCAN(ccms_rb_tree, &cino->tree, ccms_lock_scan_cmp,
				ccms_lock_undo_match, &info);
			spin_unlock(&cino->spin);
			if (lock->astate)
				ccms_lstate_put(&cino->attr_cst);
			if (lock->tstate)
				ccms_lstate_put(&cino->topo_cst);
			spin_lock(&cino->spin);
			RB_SCAN(ccms_rb_tree, &cino->tree, ccms_lock_scan_cmp,
				ccms_lock_upgrade_match, &info);
			spin_unlock(&cino->spin);
			use_redo = 1;
			goto again;
		}

		/*
		 * Cleanup free CSTs beyond the 2 we wish to retain.
		 */
		cst = ccms_free_pass1(cino, 2);
		spin_unlock(&cino->spin);
		ccms_free_pass2(cst);
	}

	/*
	 * Ok, everything is in good shape EXCEPT we might not have
	 * sufficient topo_cst.rstate.  It could have gotten ripped
	 * out from under us.  Once we have the local locks it can
	 * no longer be downgraded so a check here suffices.
	 */
	highest_state = CCMS_STATE_ALLOWED;
	if (highest_state < lock->tstate)
		highest_state = lock->tstate;
	if (highest_state < lock->astate)
		highest_state = lock->astate;
	if (highest_state < lock->dstate)
		highest_state = lock->dstate;

	if (cino->topo_cst.rstate < highest_state) {
		if (lock->dstate) {
			spin_lock(&cino->spin);
			RB_SCAN(ccms_rb_tree, &cino->tree, ccms_lock_scan_cmp,
				ccms_lock_put_match, &info);
			spin_unlock(&cino->spin);
		}
		if (lock->astate)
			ccms_lstate_put(&cino->attr_cst);
		if (lock->tstate)
			ccms_lstate_put(&cino->topo_cst);
		ccms_rstate_get(&cino->topo_cst, highest_state);
		use_redo = 0;
		goto again;
	}
	return(0);
}

/*
 * Obtain a CCMS lock, initialize the lock structure based on the uio.
 *
 * Both the attribute AND a ranged-data lock is acquired.
 */
int
ccms_lock_get_uio(ccms_inode_t *cino, ccms_lock_t *lock, struct uio *uio)
{
	ccms_state_t dstate;
	ccms_off_t eoff;

	if (uio->uio_rw == UIO_READ)
		dstate = CCMS_STATE_SHARED;
	else
		dstate = CCMS_STATE_MODIFIED;

	/*
	 * Calculate the ending offset (byte inclusive), make sure a seek
	 * overflow does not blow us up.
	 */
	eoff = uio->uio_offset + uio->uio_resid - 1;
	if (eoff < uio->uio_offset)
		eoff = 0x7FFFFFFFFFFFFFFFLL;
	lock->beg_offset = uio->uio_offset;
	lock->end_offset = eoff;
	lock->tstate = 0;
	lock->astate = dstate;
	lock->dstate = dstate;
	return (ccms_lock_get(cino, lock));
}

/*
 * Obtain a CCMS lock.  Only the attribute lock is acquired.
 */
int
ccms_lock_get_attr(ccms_inode_t *cino, ccms_lock_t *lock, ccms_state_t astate)
{
	lock->tstate = 0;
	lock->astate = astate;
	lock->dstate = 0;
	return (ccms_lock_get(cino, lock));
}

/*
 * Helper routine.
 *
 * NOTE: called with spinlock held.
 */
static
int
ccms_lock_get_match(ccms_cst_t *cst, void *arg)
{
	struct ccms_lock_scan_info *info = arg;
	ccms_lock_t *lock = info->lock;
	ccms_cst_t *ncst;

	/*
	 * If the lock's left edge is within the CST we must split the CST
	 * into two pieces [cst][ncst].  lrefs must be bumped on the CST
	 * containing the left edge.
	 *
	 * NOTE! cst->beg_offset may not be modified.  This allows us to
	 *	 avoid having to manipulate the cst's position in the tree.
	 */
	if (lock->beg_offset > cst->beg_offset) {
		ncst = info->cino->free_cache;
		info->cino->free_cache = ncst->free_next;
		ncst->free_next = NULL;
		KKASSERT(ncst != NULL);

		*ncst = *cst;
		cst->end_offset = lock->beg_offset - 1;
		cst->rrefs = 0;
		ncst->beg_offset = lock->beg_offset;
		ncst->lrefs = 1;
		RB_INSERT(ccms_rb_tree, &info->cino->tree, ncst);

		/*
		 * ncst becomes our 'matching' cst.
		 */
		cst = ncst;
	} else if (lock->beg_offset == cst->beg_offset) {
		++cst->lrefs;
	}

	/*
	 * If the lock's right edge is within the CST we must split the CST
	 * into two pieces [cst][ncst].  rrefs must be bumped on the CST
	 * containing the right edge.
	 *
	 * NOTE! cst->beg_offset may not be modified.  This allows us to
	 * avoid having to manipulate the cst's position in the tree.
	 */
	if (lock->end_offset < cst->end_offset) {
		ncst = info->cino->free_cache;
		info->cino->free_cache = ncst->free_next;
		ncst->free_next = NULL;
		KKASSERT(ncst != NULL);

		*ncst = *cst;
		cst->end_offset = lock->end_offset;
		cst->rrefs = 1;
		ncst->beg_offset = lock->end_offset + 1;
		ncst->lrefs = 0;
		RB_INSERT(ccms_rb_tree, &info->cino->tree, ncst);
		/* cst remains our 'matching' cst */
	} else if (lock->end_offset == cst->end_offset) {
		++cst->rrefs;
	}

	/*
	 * The lock covers the CST, so increment the CST's coverage count.
	 * Then attempt to obtain the shared/exclusive lock.  The coverage
	 * count is maintained until the put operation.
	 */
	++cst->xrefs;
	if (cst->lstate < lock->dstate)
		cst->lstate = lock->dstate;

	/*
	 * If we have already collided we make no more modifications
	 * to cst->count, but we must continue the scan to properly
	 * partition the cst.
	 */
	if (info->coll_cst)
		return(0);

	switch(lock->dstate) {
	case CCMS_STATE_INVALID:
		break;
	case CCMS_STATE_ALLOWED:
	case CCMS_STATE_SHARED:
	case CCMS_STATE_SLAVE:
		if (cst->count < 0) {
			info->coll_cst = cst;
		} else {
			++cst->count;
			if (ccms_debug >= 9) {
				kprintf("CST SHARE %d %lld-%lld\n",
					cst->count,
					(long long)cst->beg_offset,
					(long long)cst->end_offset);
			}
		}
		break;
	case CCMS_STATE_MASTER:
	case CCMS_STATE_EXCLUSIVE:
		if (cst->count != 0) {
			info->coll_cst = cst;
		} else {
			--cst->count;
			if (ccms_debug >= 9) {
				kprintf("CST EXCLS %d %lld-%lld\n",
					cst->count,
					(long long)cst->beg_offset,
					(long long)cst->end_offset);
			}
		}
		break;
	case CCMS_STATE_MODIFIED:
		if (cst->count != 0) {
			info->coll_cst = cst;
		} else {
			--cst->count;
			if (cst->lstate <= CCMS_STATE_EXCLUSIVE)
				cst->lstate = CCMS_STATE_MODIFIED;
			if (ccms_debug >= 9) {
				kprintf("CST MODXL %d %lld-%lld\n",
					cst->count,
					(long long)cst->beg_offset,
					(long long)cst->end_offset);
			}
		}
		break;
	default:
		panic("ccms_lock_get_match: bad state %d\n", lock->dstate);
		break;
	}
	return(0);
}

/*
 * Undo a partially resolved ccms_ltype rangelock.  This is atomic with
 * the scan/redo code so there should not be any blocked locks when
 * transitioning to 0.  lrefs and rrefs are not touched in order to
 * retain the partitioning.
 *
 * If coll_cst is non-NULL we stop when we hit this element as locks on
 * no further elements were obtained.  This element might not represent
 * a left or right edge but coll_cst can only be non-NULL if the spinlock
 * was held throughout the get/redo and the undo.
 *
 * NOTE: called with spinlock held.
 */
static
int
ccms_lock_undo_match(ccms_cst_t *cst, void *arg)
{
	struct ccms_lock_scan_info *info = arg;
	ccms_lock_t *lock = info->lock;

	if (cst == info->coll_cst)
		return(-1);

	switch (lock->dstate) {
	case CCMS_STATE_INVALID:
		break;
	case CCMS_STATE_ALLOWED:
	case CCMS_STATE_SHARED:
	case CCMS_STATE_SLAVE:
		KKASSERT(cst->count > 0);
		--cst->count;
		KKASSERT(cst->count || cst->blocked == 0);
		break;
	case CCMS_STATE_MASTER:
	case CCMS_STATE_EXCLUSIVE:
	case CCMS_STATE_MODIFIED:
		KKASSERT(cst->count < 0);
		++cst->count;
		KKASSERT(cst->count || cst->blocked == 0);
		break;
	default:
		panic("ccms_lock_undo_match: bad state %d\n", lock->dstate);
		break;
	}
	return(0);
}

/*
 * Redo the local lock request for a range which has already been
 * partitioned.
 *
 * NOTE: called with spinlock held.
 */
static
int
ccms_lock_redo_match(ccms_cst_t *cst, void *arg)
{
	struct ccms_lock_scan_info *info = arg;
	ccms_lock_t *lock = info->lock;

	KKASSERT(info->coll_cst == NULL);

	switch(lock->dstate) {
	case CCMS_STATE_INVALID:
		break;
	case CCMS_STATE_ALLOWED:
	case CCMS_STATE_SHARED:
	case CCMS_STATE_SLAVE:
		if (cst->count < 0) {
			info->coll_cst = cst;
		} else {
			if (ccms_debug >= 9) {
				kprintf("CST SHARE %d %lld-%lld\n",
					cst->count,
					(long long)cst->beg_offset,
					(long long)cst->end_offset);
			}
			++cst->count;
		}
		break;
	case CCMS_STATE_MASTER:
	case CCMS_STATE_EXCLUSIVE:
		if (cst->count != 0) {
			info->coll_cst = cst;
		} else {
			--cst->count;
			if (ccms_debug >= 9) {
				kprintf("CST EXCLS %d %lld-%lld\n",
					cst->count,
					(long long)cst->beg_offset,
					(long long)cst->end_offset);
			}
		}
		break;
	case CCMS_STATE_MODIFIED:
		if (cst->count != 0) {
			info->coll_cst = cst;
		} else {
			--cst->count;
			if (ccms_debug >= 9) {
				kprintf("CST MODXL %d %lld-%lld\n",
					cst->count,
					(long long)cst->beg_offset,
					(long long)cst->end_offset);
			}
		}
		break;
	default:
		panic("ccms_lock_redo_match: bad state %d\n", lock->dstate);
		break;
	}

	if (info->coll_cst)
		return(-1);	/* stop the scan */
	return(0);		/* continue the scan */
}

/*
 * Upgrade the rstate for the matching range.
 *
 * NOTE: Called with spinlock held.
 */
static
int
ccms_lock_upgrade_match(ccms_cst_t *cst, void *arg)
{
	struct ccms_lock_scan_info *info = arg;
	ccms_lock_t *lock = info->lock;

	/*
	 * ccms_rstate_get() can block so we must release the spinlock.
	 * To prevent the cst from getting ripped out on us we temporarily
	 * bump both lrefs and rrefs.
	 */
	if (cst->rstate < lock->dstate) {
		++cst->lrefs;
		++cst->rrefs;
		spin_unlock(&info->cino->spin);
		ccms_rstate_get(cst, lock->dstate);
		spin_lock(&info->cino->spin);
		--cst->lrefs;
		--cst->rrefs;
	}
	return(0);
}

/*
 * Release a previously acquired CCMS lock.
 */
int
ccms_lock_put(ccms_inode_t *cino, ccms_lock_t *lock)
{
	struct ccms_lock_scan_info info;
	ccms_cst_t *scan;

	if (lock->tstate) {
		ccms_lstate_put(lock->icst);
		lock->tstate = 0;
		lock->icst = NULL;
	} else if (lock->astate) {
		ccms_lstate_put(lock->icst);
		lock->astate = 0;
		lock->icst = NULL;
	}

	if (lock->dstate) {
		info.lock = lock;
		info.cino = cino;
		spin_lock(&cino->spin);
		RB_SCAN(ccms_rb_tree, &cino->tree, ccms_lock_scan_cmp,
			ccms_lock_put_match, &info);
		scan = ccms_free_pass1(cino, 2);
		spin_unlock(&cino->spin);
		ccms_free_pass2(scan);
		lock->dstate = 0;
		lock->dcst = NULL;
	}

	return(0);
}

/*
 * Release a local lock.  The related CST's lstate is set to INVALID once
 * the coverage drops to 0 and adjacent compatible entries will be
 * recombined.
 *
 * NOTE: called with spinlock held.
 */
static
int
ccms_lock_put_match(ccms_cst_t *cst, void *arg)
{
	struct ccms_lock_scan_info *info = arg;
	ccms_lock_t *lock = info->lock;
	ccms_cst_t *ocst;

	/*
	 * Undo the local shared/exclusive rangelock.
	 */
	switch(lock->dstate) {
	case CCMS_STATE_INVALID:
		break;
	case CCMS_STATE_ALLOWED:
	case CCMS_STATE_SHARED:
	case CCMS_STATE_SLAVE:
		KKASSERT(cst->count > 0);
		--cst->count;
		if (ccms_debug >= 9) {
			kprintf("CST UNSHR %d %lld-%lld (%d)\n", cst->count,
				(long long)cst->beg_offset,
				(long long)cst->end_offset,
				cst->blocked);
		}
		if (cst->blocked && cst->count == 0) {
			cst->blocked = 0;
			wakeup(cst);
		}
		break;
	case CCMS_STATE_MASTER:
	case CCMS_STATE_EXCLUSIVE:
	case CCMS_STATE_MODIFIED:
		KKASSERT(cst->count < 0);
		++cst->count;
		if (ccms_debug >= 9) {
			kprintf("CST UNEXC %d %lld-%lld (%d)\n", cst->count,
				(long long)cst->beg_offset,
				(long long)cst->end_offset,
				cst->blocked);
		}
		if (cst->blocked && cst->count == 0) {
			cst->blocked = 0;
			wakeup(cst);
		}
		break;
	default:
		panic("ccms_lock_put_match: bad state %d\n", lock->dstate);
		break;
	}

	/*
	 * Decrement the lock coverage count on the CST.  Decrement the left
	 * and right edge counts as appropriate.
	 *
	 * When lrefs or rrefs drops to zero we check the adjacent entry to
	 * determine whether a merge is possible.  If the appropriate refs
	 * field (rrefs for the entry to our left, lrefs for the entry to
	 * our right) is 0, then all covering locks must cover both entries
	 * and the xrefs field must match.  We can then merge the entries
	 * if they have compatible cache states.
	 *
	 * However, because we are cleaning up the shared/exclusive count
	 * at the same time, the count field may be temporarily out of
	 * sync, so require that the count field also match before doing
	 * a merge.
	 *
	 * When merging an element which is being blocked on, the blocking
	 * thread(s) will be woken up.
	 *
	 * If the dataspace has too many CSTs we may be able to merge the
	 * entries even if their cache states are not the same, by dropping
	 * both to a compatible (lower) cache state and performing the
	 * appropriate management operations.  XXX
	 */
	if (--cst->xrefs == 0)
		cst->lstate = CCMS_STATE_INVALID;

	if (lock->beg_offset == cst->beg_offset && --cst->lrefs == 0) {
		if ((ocst = RB_PREV(ccms_rb_tree,
				    &info->cino->tree, cst)) != NULL &&
		    ocst->rrefs == 0 &&
		    ocst->lstate == cst->lstate &&
		    ocst->rstate == cst->rstate &&
		    ocst->count == cst->count
		) {
			KKASSERT(ocst->xrefs == cst->xrefs);
			KKASSERT(ocst->end_offset + 1 == cst->beg_offset);
			RB_REMOVE(ccms_rb_tree, &info->cino->tree, ocst);
			cst->beg_offset = ocst->beg_offset;
			cst->lrefs = ocst->lrefs;
			if (ccms_debug >= 9) {
				kprintf("MERGELEFT %p %lld-%lld (%d)\n",
				       ocst,
				       (long long)cst->beg_offset,
				       (long long)cst->end_offset,
				       cst->blocked);
			}
			if (ocst->blocked) {
				ocst->blocked = 0;
				wakeup(ocst);
			}
			ocst->free_next = info->cino->free_cache;
			info->cino->free_cache = ocst;
		}
	}
	if (lock->end_offset == cst->end_offset && --cst->rrefs == 0) {
		if ((ocst = RB_NEXT(ccms_rb_tree,
				    &info->cino->tree, cst)) != NULL &&
		    ocst->lrefs == 0 &&
		    ocst->lstate == cst->lstate &&
		    ocst->rstate == cst->rstate &&
		    ocst->count == cst->count
		) {
			KKASSERT(ocst->xrefs == cst->xrefs);
			KKASSERT(cst->end_offset + 1 == ocst->beg_offset);
			RB_REMOVE(ccms_rb_tree, &info->cino->tree, ocst);
			cst->end_offset = ocst->end_offset;
			cst->rrefs = ocst->rrefs;
			if (ccms_debug >= 9) {
				kprintf("MERGERIGHT %p %lld-%lld\n",
				       ocst,
				       (long long)cst->beg_offset,
				       (long long)cst->end_offset);
			}
			ocst->free_next = info->cino->free_cache;
			info->cino->free_cache = ocst;
		}
	}
	return(0);
}

/*
 * RB tree compare function for insertions and deletions.  This function
 * compares two CSTs.
 */
static int
ccms_cst_cmp(ccms_cst_t *b1, ccms_cst_t *b2)
{
	if (b1->end_offset < b2->beg_offset)
		return(-1);
	if (b1->beg_offset > b2->end_offset)
		return(1);
	return(0);
}

/*
 * RB tree scanning compare function.  This function compares the CST
 * from the tree against the supplied ccms_lock and returns the CST's
 * placement relative to the lock.
 */
static int
ccms_lock_scan_cmp(ccms_cst_t *cst, void *arg)
{
	struct ccms_lock_scan_info *info = arg;
	ccms_lock_t *lock = info->lock;

	if (cst->end_offset < lock->beg_offset)
		return(-1);
	if (cst->beg_offset > lock->end_offset)
		return(1);
	return(0);
}

/************************************************************************
 *		STANDALONE LSTATE AND RSTATE SUPPORT FUNCTIONS		*
 ************************************************************************
 *
 * These functions are used to perform work on the attr_cst and topo_cst
 * embedded in a ccms_inode, and to issue remote state operations.  These
 * functions are called without the ccms_inode spinlock held.
 */

static
void
ccms_lstate_get(ccms_cst_t *cst, ccms_state_t state)
{
	int blocked;

	spin_lock(&cst->cino->spin);
	++cst->xrefs;

	for (;;) {
		blocked = 0;

		switch(state) {
		case CCMS_STATE_INVALID:
			break;
		case CCMS_STATE_ALLOWED:
		case CCMS_STATE_SHARED:
		case CCMS_STATE_SLAVE:
			if (cst->count < 0) {
				blocked = 1;
			} else {
				++cst->count;
				if (ccms_debug >= 9) {
					kprintf("CST SHARE %d %lld-%lld\n",
						cst->count,
						(long long)cst->beg_offset,
						(long long)cst->end_offset);
				}
			}
			break;
		case CCMS_STATE_MASTER:
		case CCMS_STATE_EXCLUSIVE:
			if (cst->count != 0) {
				blocked = 1;
			} else {
				--cst->count;
				if (ccms_debug >= 9) {
					kprintf("CST EXCLS %d %lld-%lld\n",
						cst->count,
						(long long)cst->beg_offset,
						(long long)cst->end_offset);
				}
			}
			break;
		case CCMS_STATE_MODIFIED:
			if (cst->count != 0) {
				blocked = 1;
			} else {
				--cst->count;
				if (cst->lstate <= CCMS_STATE_EXCLUSIVE)
					cst->lstate = CCMS_STATE_MODIFIED;
				if (ccms_debug >= 9) {
					kprintf("CST MODXL %d %lld-%lld\n",
						cst->count,
						(long long)cst->beg_offset,
						(long long)cst->end_offset);
				}
			}
			break;
		default:
			panic("ccms_lock_get_match: bad state %d\n", state);
			break;
		}
		if (blocked == 0)
			break;
		ssleep(cst, &cst->cino->spin, 0, "ccmslget", hz);
	}
	if (cst->lstate < state)
		cst->lstate = state;
	spin_unlock(&cst->cino->spin);
}

static
void
ccms_lstate_put(ccms_cst_t *cst)
{
	spin_lock(&cst->cino->spin);

	switch(cst->lstate) {
	case CCMS_STATE_INVALID:
		break;
	case CCMS_STATE_ALLOWED:
	case CCMS_STATE_SHARED:
	case CCMS_STATE_SLAVE:
		KKASSERT(cst->count > 0);
		--cst->count;
		if (ccms_debug >= 9) {
			kprintf("CST UNSHR %d %lld-%lld (%d)\n", cst->count,
				(long long)cst->beg_offset,
				(long long)cst->end_offset,
				cst->blocked);
		}
		if (cst->blocked && cst->count == 0) {
			cst->blocked = 0;
			wakeup(cst);
		}
		break;
	case CCMS_STATE_MASTER:
	case CCMS_STATE_EXCLUSIVE:
	case CCMS_STATE_MODIFIED:
		KKASSERT(cst->count < 0);
		++cst->count;
		if (ccms_debug >= 9) {
			kprintf("CST UNEXC %d %lld-%lld (%d)\n", cst->count,
				(long long)cst->beg_offset,
				(long long)cst->end_offset,
				cst->blocked);
		}
		if (cst->blocked && cst->count == 0) {
			cst->blocked = 0;
			wakeup(cst);
		}
		break;
	default:
		panic("ccms_lock_put_match: bad state %d\n", cst->lstate);
		break;
	}

	if (--cst->xrefs == 0)
		cst->lstate = CCMS_STATE_INVALID;
	spin_unlock(&cst->cino->spin);
}

/*
 * XXX third-party interaction & granularity
 */
static
void
ccms_rstate_get(ccms_cst_t *cst, ccms_state_t state)
{
	spin_lock(&cst->cino->spin);
	if (cst->rstate < state)
		cst->rstate = state;
	spin_unlock(&cst->cino->spin);
}

/*
 * XXX third-party interaction & granularity
 */
static
void
ccms_rstate_put(ccms_cst_t *cst)
{
	spin_lock(&cst->cino->spin);
	cst->rstate = CCMS_STATE_INVALID;
	spin_unlock(&cst->cino->spin);
}
