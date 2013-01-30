/*
 * Copyright (c) 2011-2012 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Matthew Dillon <dillon@dragonflybsd.org>
 * by Venkatesh Srinivas <vsrinivas@dragonflybsd.org>
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
 * This subsystem handles direct and indirect block searches, recursions,
 * creation, and deletion.  Chains of blockrefs are tracked and modifications
 * are flagged for propagation... eventually all the way back to the volume
 * header.  Any chain except the volume header can be flushed to disk at
 * any time... none of it matters until the volume header is dealt with
 * (which is not here, see hammer2_vfsops.c for the volume header disk
 * sequencing).
 *
 * Serialized flushes are not handled here, see hammer2_flush.c.  This module
 * can essentially work on the current version of data, which can be in memory
 * as well as on-disk due to the above.  However, we are responsible for
 * making a copy of the state when a modified chain is part of a flush
 * and we attempt to modify it again before the flush gets to it.  In that
 * situation we create an allocated copy of the state that the flush can
 * deal with.  If a chain undergoing deletion is part of a flush it is
 * marked DELETED and its bref index is kept intact for the flush, but the
 * chain is thereafter ignored by this module's because it is no longer
 * current.
 */
#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/types.h>
#include <sys/lock.h>
#include <sys/uuid.h>

#include "hammer2.h"

static int hammer2_indirect_optimize;	/* XXX SYSCTL */

static hammer2_chain_t *hammer2_chain_create_indirect(
			hammer2_mount_t *hmp, hammer2_chain_t *parent,
			hammer2_key_t key, int keybits,
			int *errorp);

/*
 * We use a red-black tree to guarantee safe lookups under shared locks.
 */
RB_GENERATE(hammer2_chain_tree, hammer2_chain, rbnode, hammer2_chain_cmp);

int
hammer2_chain_cmp(hammer2_chain_t *chain1, hammer2_chain_t *chain2)
{
	return(chain2->index - chain1->index);
}

/*
 * Recursively mark the parent chain elements so flushes can find
 * modified elements.  Stop when we hit a chain already flagged
 * SUBMODIFIED, but ignore the SUBMODIFIED bit that might be set
 * in chain itself.
 *
 * SUBMODIFIED is not set on the chain passed in.
 *
 * The chain->cst.spin lock can be held to stabilize the chain->parent
 * pointer.  The first parent is stabilized by virtue of chain being
 * fully locked.
 */
void
hammer2_chain_parent_setsubmod(hammer2_mount_t *hmp, hammer2_chain_t *chain)
{
	hammer2_chain_t *parent;

	parent = chain->parent;
	if (parent && (parent->flags & HAMMER2_CHAIN_SUBMODIFIED) == 0) {
		spin_lock(&parent->cst.spin);
		for (;;) {
			atomic_set_int(&parent->flags,
				       HAMMER2_CHAIN_SUBMODIFIED);
			if ((chain = parent->parent) == NULL)
				break;
			spin_lock(&chain->cst.spin);	/* upward interlock */
			spin_unlock(&parent->cst.spin);
			parent = chain;
		}
		spin_unlock(&parent->cst.spin);
	}
}

/*
 * Allocate a new disconnected chain element representing the specified
 * bref.  The chain element is locked exclusively and refs is set to 1.
 *
 * This essentially allocates a system memory structure representing one
 * of the media structure types, including inodes.
 */
hammer2_chain_t *
hammer2_chain_alloc(hammer2_mount_t *hmp, hammer2_blockref_t *bref)
{
	hammer2_chain_t *chain;
	hammer2_inode_t *ip;
	hammer2_indblock_t *np;
	hammer2_data_t *dp;
	u_int bytes = 1U << (int)(bref->data_off & HAMMER2_OFF_MASK_RADIX);

	/*
	 * Construct the appropriate system structure.
	 */
	switch(bref->type) {
	case HAMMER2_BREF_TYPE_INODE:
		ip = kmalloc(sizeof(*ip), hmp->minode, M_WAITOK | M_ZERO);
		chain = &ip->chain;
		chain->u.ip = ip;
		ip->hmp = hmp;
		break;
	case HAMMER2_BREF_TYPE_INDIRECT:
	case HAMMER2_BREF_TYPE_FREEMAP_ROOT:
	case HAMMER2_BREF_TYPE_FREEMAP_NODE:
		np = kmalloc(sizeof(*np), hmp->mchain, M_WAITOK | M_ZERO);
		chain = &np->chain;
		chain->u.np = np;
		break;
	case HAMMER2_BREF_TYPE_DATA:
	case HAMMER2_BREF_TYPE_FREEMAP_LEAF:
		dp = kmalloc(sizeof(*dp), hmp->mchain, M_WAITOK | M_ZERO);
		chain = &dp->chain;
		chain->u.dp = dp;
		break;
	case HAMMER2_BREF_TYPE_VOLUME:
		chain = NULL;
		panic("hammer2_chain_alloc volume type illegal for op");
	default:
		chain = NULL;
		panic("hammer2_chain_alloc: unrecognized blockref type: %d",
		      bref->type);
	}

	/*
	 * Only set bref_flush if the bref has a real media offset, otherwise
	 * the caller has to wait for the chain to be modified/block-allocated
	 * before a blockref can be synchronized with its (future) parent.
	 */
	chain->bref = *bref;
	if (bref->data_off & ~HAMMER2_OFF_MASK_RADIX)
		chain->bref_flush = *bref;
	chain->index = -1;		/* not yet assigned */
	chain->refs = 1;
	chain->bytes = bytes;
	ccms_cst_init(&chain->cst, chain);
	ccms_thread_lock(&chain->cst, CCMS_STATE_EXCLUSIVE);

	return (chain);
}

/*
 * Deallocate a chain (the step before freeing it).  Remove the chain from
 * its parent's tree.
 *
 * Caller must hold the parent and the chain exclusively locked, and
 * chain->refs must be 0.
 *
 * This function unlocks, removes, and destroys chain, and will recursively
 * destroy any sub-chains under chain (whos refs must also be 0 at this
 * point).
 *
 * parent can be NULL.
 */
static void
hammer2_chain_dealloc(hammer2_mount_t *hmp, hammer2_chain_t *chain)
{
	hammer2_inode_t *ip;
	hammer2_chain_t *parent;
	hammer2_chain_t *child;

	KKASSERT(chain->refs == 0);
	KKASSERT(chain->flushing == 0);
	KKASSERT((chain->flags &
		  (HAMMER2_CHAIN_MOVED | HAMMER2_CHAIN_MODIFIED)) == 0);

	if (chain->bref.type == HAMMER2_BREF_TYPE_INODE)
		ip = chain->u.ip;
	else
		ip = NULL;

	/*
	 * If the sub-tree is not empty all the elements on it must have
	 * 0 refs and be deallocatable.
	 */
	while ((child = RB_ROOT(&chain->rbhead)) != NULL) {
		ccms_thread_lock(&child->cst, CCMS_STATE_EXCLUSIVE);
		hammer2_chain_dealloc(hmp, child);
	}

	/*
	 * If the DELETED flag is not set the chain must be removed from
	 * its parent's tree.
	 *
	 * WARNING! chain->cst.spin must be held when chain->parent is
	 *	    modified, even though we own the full blown lock,
	 *	    to deal with setsubmod and rename races.
	 */
	if (chain->flags & HAMMER2_CHAIN_ONRBTREE) {
		spin_lock(&chain->cst.spin);	/* shouldn't be needed */
		parent = chain->parent;
		RB_REMOVE(hammer2_chain_tree, &parent->rbhead, chain);
		atomic_clear_int(&chain->flags, HAMMER2_CHAIN_ONRBTREE);
		if (ip)
			ip->pip = NULL;
		chain->parent = NULL;
		spin_unlock(&chain->cst.spin);
	}

	/*
	 * When cleaning out a hammer2_inode we must
	 * also clean out the related ccms_inode.
	 */
	if (ip)
		ccms_cst_uninit(&ip->topo_cst);
	hammer2_chain_free(hmp, chain);
}

/*
 * Free a disconnected chain element
 */
void
hammer2_chain_free(hammer2_mount_t *hmp, hammer2_chain_t *chain)
{
	void *mem;

	if (chain->bref.type == HAMMER2_BREF_TYPE_INODE ||
	    chain->bref.type == HAMMER2_BREF_TYPE_VOLUME) {
		chain->data = NULL;
	}

	KKASSERT(chain->bp == NULL);
	KKASSERT(chain->data == NULL);
	KKASSERT(chain->bref.type != HAMMER2_BREF_TYPE_INODE ||
		 chain->u.ip->vp == NULL);
	ccms_thread_unlock(&chain->cst);
	KKASSERT(chain->cst.count == 0);
	KKASSERT(chain->cst.upgrade == 0);

	if ((mem = chain->u.mem) != NULL) {
		chain->u.mem = NULL;
		if (chain->bref.type == HAMMER2_BREF_TYPE_INODE)
			kfree(mem, hmp->minode);
		else
			kfree(mem, hmp->mchain);
	}
}

/*
 * Add a reference to a chain element, preventing its destruction.
 *
 * The parent chain must be locked shared or exclusive or otherwise be
 * stable and already have a reference.
 */
void
hammer2_chain_ref(hammer2_mount_t *hmp, hammer2_chain_t *chain)
{
	u_int refs;

	while (chain) {
		refs = chain->refs;
		KKASSERT(chain->refs >= 0);
		cpu_ccfence();
		if (refs == 0) {
			/*
			 * 0 -> 1 transition must bump the refs on the parent
			 * too.  The caller has stabilized the parent.
			 */
			if (atomic_cmpset_int(&chain->refs, 0, 1)) {
				chain = chain->parent;
				KKASSERT(chain == NULL || chain->refs > 0);
			}
			/* retry or continue along the parent chain */
		} else {
			/*
			 * N -> N+1
			 */
			if (atomic_cmpset_int(&chain->refs, refs, refs + 1))
				break;
			/* retry */
		}
	}
}

/*
 * Drop the callers reference to the chain element.  If the ref count
 * reaches zero we attempt to recursively drop the parent.
 *
 * MOVED and MODIFIED elements hold additional references so it should not
 * be possible for the count on a modified element to drop to 0.
 *
 * The chain element must NOT be locked by the caller on the 1->0 transition.
 *
 * The parent might or might not be locked by the caller.  If we are unable
 * to lock the parent on the 1->0 transition the destruction of the chain
 * will be deferred but we still recurse upward and drop the ref on the
 * parent (see the lastdrop() function)
 */
static hammer2_chain_t *hammer2_chain_lastdrop(hammer2_mount_t *hmp,
						hammer2_chain_t *chain);

void
hammer2_chain_drop(hammer2_mount_t *hmp, hammer2_chain_t *chain)
{
	u_int refs;

	while (chain) {
		refs = chain->refs;
		cpu_ccfence();
		KKASSERT(refs > 0);
		if (refs == 1) {
			/*
			 * (1) lastdrop successfully drops the chain to 0
			 *     refs and may may not have destroyed it.
			 *     lastdrop will return the parent so we can
			 *     recursively drop the implied ref from the
			 *     1->0 transition.
			 *
			 * (2) lastdrop fails to transition refs from 1 to 0
			 *     and returns the same chain, we retry.
			 */
			chain = hammer2_chain_lastdrop(hmp, chain);
		} else {
			if (atomic_cmpset_int(&chain->refs, refs, refs - 1)) {
				/*
				 * Succeeded, count did not reach zero so
				 * cut out of the loop.
				 */
				break;
			}
			/* retry the same chain */
		}
	}
}

/*
 * Handle SMP races during the last drop.  We must obtain a lock on
 * chain->parent to stabilize the last pointer reference to chain
 * (if applicable).  This reference does not have a parallel ref count,
 * that is idle chains in the topology can have a ref count of 0.
 *
 * The 1->0 transition implies a ref on the parent.
 */
static
hammer2_chain_t *
hammer2_chain_lastdrop(hammer2_mount_t *hmp, hammer2_chain_t *chain)
{
	hammer2_chain_t *parent;

	/*
	 * Stablize chain->parent with the chain cst's spinlock.
	 * (parent can be NULL here).
	 *
	 * cst.spin locks are allowed to be nested bottom-up (the reverse
	 * of the normal top-down for full-blown cst locks), so this also
	 * allows us to attempt to obtain the parent's cst lock non-blocking
	 * (which must acquire the parent's spinlock unconditionally) while
	 * we are still holding the chain's spinlock.
	 */
	spin_lock(&chain->cst.spin);
	parent = chain->parent;

	/*
	 * If chain->flushing is non-zero we cannot deallocate the chain
	 * here.  The flushing field will be serialized for the inline
	 * unlock made by the flusher itself and we don't care about races
	 * in any other situation because the thread lock on the parent
	 * will fail in other situations.
	 *
	 * If we have a non-NULL parent but cannot acquire its thread
	 * lock, we also cannot deallocate the chain.
	 */
	if (chain->flushing ||
	    (parent && ccms_thread_lock_nonblock(&parent->cst,
						 CCMS_STATE_EXCLUSIVE))) {
		if (atomic_cmpset_int(&chain->refs, 1, 0)) {
			spin_unlock(&chain->cst.spin);	/* success */
			return(parent);
		} else {
			spin_unlock(&chain->cst.spin);	/* failure */
			return(chain);
		}
	}
	spin_unlock(&chain->cst.spin);

	/*
	 * With the parent now held we control the last pointer reference
	 * to chain ONLY IF this is the 1->0 drop.  If we fail to transition
	 * from 1->0 we raced a refs change and must retry at chain.
	 */
	if (atomic_cmpset_int(&chain->refs, 1, 0) == 0) {
		/* failure */
		if (parent)
			ccms_thread_unlock(&parent->cst);
		return(chain);
	}

	/*
	 * Ok, we succeeded.  We now own the implied ref on the parent
	 * associated with the 1->0 transition of the child.  It should not
	 * be possible for ANYTHING to access the child now, as we own the
	 * lock on the parent, so we should be able to safely lock the
	 * child and destroy it.
	 */
	ccms_thread_lock(&chain->cst, CCMS_STATE_EXCLUSIVE);
	hammer2_chain_dealloc(hmp, chain);

	/*
	 * We want to return parent with its implied ref to the caller
	 * to recurse and drop the parent.
	 */
	if (parent)
		ccms_thread_unlock(&parent->cst);
	return (parent);
}

/*
 * Ref and lock a chain element, acquiring its data with I/O if necessary,
 * and specify how you would like the data to be resolved.
 *
 * Returns 0 on success or an error code if the data could not be acquired.
 * The chain element is locked either way.
 *
 * The lock is allowed to recurse, multiple locking ops will aggregate
 * the requested resolve types.  Once data is assigned it will not be
 * removed until the last unlock.
 *
 * HAMMER2_RESOLVE_NEVER - Do not resolve the data element.
 *			   (typically used to avoid device/logical buffer
 *			    aliasing for data)
 *
 * HAMMER2_RESOLVE_MAYBE - Do not resolve data elements for chains in
 *			   the INITIAL-create state (indirect blocks only).
 *
 *			   Do not resolve data elements for DATA chains.
 *			   (typically used to avoid device/logical buffer
 *			    aliasing for data)
 *
 * HAMMER2_RESOLVE_ALWAYS- Always resolve the data element.
 *
 * HAMMER2_RESOLVE_SHARED- (flag) The chain is locked shared, otherwise
 *			   it will be locked exclusive.
 *
 * NOTE: Embedded elements (volume header, inodes) are always resolved
 *	 regardless.
 *
 * NOTE: Specifying HAMMER2_RESOLVE_ALWAYS on a newly-created non-embedded
 *	 element will instantiate and zero its buffer, and flush it on
 *	 release.
 *
 * NOTE: (data) elements are normally locked RESOLVE_NEVER or RESOLVE_MAYBE
 *	 so as not to instantiate a device buffer, which could alias against
 *	 a logical file buffer.  However, if ALWAYS is specified the
 *	 device buffer will be instantiated anyway.
 */
int
hammer2_chain_lock(hammer2_mount_t *hmp, hammer2_chain_t *chain, int how)
{
	hammer2_blockref_t *bref;
	hammer2_off_t pbase;
	hammer2_off_t peof;
	ccms_state_t ostate;
	size_t boff;
	size_t bbytes;
	int error;
	char *bdata;

	/*
	 * Ref and lock the element.  Recursive locks are allowed.
	 */
	hammer2_chain_ref(hmp, chain);
	if (how & HAMMER2_RESOLVE_SHARED)
		ccms_thread_lock(&chain->cst, CCMS_STATE_SHARED);
	else
		ccms_thread_lock(&chain->cst, CCMS_STATE_EXCLUSIVE);

	/*
	 * If we already have a valid data pointer no further action is
	 * necessary.
	 */
	if (chain->data)
		return (0);

	/*
	 * Do we have to resolve the data?
	 */
	switch(how & HAMMER2_RESOLVE_MASK) {
	case HAMMER2_RESOLVE_NEVER:
		return(0);
	case HAMMER2_RESOLVE_MAYBE:
		if (chain->flags & HAMMER2_CHAIN_INITIAL)
			return(0);
		if (chain->bref.type == HAMMER2_BREF_TYPE_DATA)
			return(0);
		if (chain->bref.type == HAMMER2_BREF_TYPE_FREEMAP_LEAF)
			return(0);
		/* fall through */
	case HAMMER2_RESOLVE_ALWAYS:
		break;
	}

	/*
	 * Upgrade to an exclusive lock so we can safely manipulate the
	 * buffer cache.  If another thread got to it before us we
	 * can just return.
	 */
	ostate = ccms_thread_lock_upgrade(&chain->cst);
	if (chain->data) {
		ccms_thread_lock_restore(&chain->cst, ostate);
		return (0);
	}

	/*
	 * We must resolve to a device buffer, either by issuing I/O or
	 * by creating a zero-fill element.  We do not mark the buffer
	 * dirty when creating a zero-fill element (the hammer2_chain_modify()
	 * API must still be used to do that).
	 *
	 * The device buffer is variable-sized in powers of 2 down
	 * to HAMMER2_MINALLOCSIZE (typically 1K).  A 64K physical storage
	 * chunk always contains buffers of the same size. (XXX)
	 *
	 * The minimum physical IO size may be larger than the variable
	 * block size.
	 */
	bref = &chain->bref;

	if ((bbytes = chain->bytes) < HAMMER2_MINIOSIZE)
		bbytes = HAMMER2_MINIOSIZE;
	pbase = bref->data_off & ~(hammer2_off_t)(bbytes - 1);
	peof = (pbase + HAMMER2_PBUFSIZE64) & ~HAMMER2_PBUFMASK64;
	boff = bref->data_off & HAMMER2_OFF_MASK & (bbytes - 1);
	KKASSERT(pbase != 0);

	/*
	 * The getblk() optimization can only be used on newly created
	 * elements if the physical block size matches the request.
	 */
	if ((chain->flags & HAMMER2_CHAIN_INITIAL) &&
	    chain->bytes == bbytes) {
		chain->bp = getblk(hmp->devvp, pbase, bbytes, 0, 0);
		error = 0;
	} else if (hammer2_cluster_enable) {
		error = cluster_read(hmp->devvp, peof, pbase, bbytes,
				     HAMMER2_PBUFSIZE, HAMMER2_PBUFSIZE,
				     &chain->bp);
	} else {
		error = bread(hmp->devvp, pbase, bbytes, &chain->bp);
	}

	if (error) {
		kprintf("hammer2_chain_get: I/O error %016jx: %d\n",
			(intmax_t)pbase, error);
		bqrelse(chain->bp);
		chain->bp = NULL;
		ccms_thread_lock_restore(&chain->cst, ostate);
		return (error);
	}

	/*
	 * Zero the data area if the chain is in the INITIAL-create state.
	 * Mark the buffer for bdwrite().
	 */
	bdata = (char *)chain->bp->b_data + boff;
	if (chain->flags & HAMMER2_CHAIN_INITIAL) {
		bzero(bdata, chain->bytes);
		atomic_set_int(&chain->flags, HAMMER2_CHAIN_DIRTYBP);
	}

	/*
	 * Setup the data pointer, either pointing it to an embedded data
	 * structure and copying the data from the buffer, or pointing it
	 * into the buffer.
	 *
	 * The buffer is not retained when copying to an embedded data
	 * structure in order to avoid potential deadlocks or recursions
	 * on the same physical buffer.
	 */
	switch (bref->type) {
	case HAMMER2_BREF_TYPE_VOLUME:
		/*
		 * Copy data from bp to embedded buffer
		 */
		panic("hammer2_chain_lock: called on unresolved volume header");
#if 0
		/* NOT YET */
		KKASSERT(pbase == 0);
		KKASSERT(chain->bytes == HAMMER2_PBUFSIZE);
		bcopy(bdata, &hmp->voldata, chain->bytes);
		chain->data = (void *)&hmp->voldata;
		bqrelse(chain->bp);
		chain->bp = NULL;
#endif
		break;
	case HAMMER2_BREF_TYPE_INODE:
		/*
		 * Copy data from bp to embedded buffer, do not retain the
		 * device buffer.
		 */
		bcopy(bdata, &chain->u.ip->ip_data, chain->bytes);
		chain->data = (void *)&chain->u.ip->ip_data;
		bqrelse(chain->bp);
		chain->bp = NULL;
		break;
	case HAMMER2_BREF_TYPE_INDIRECT:
	case HAMMER2_BREF_TYPE_DATA:
	case HAMMER2_BREF_TYPE_FREEMAP_ROOT:
	case HAMMER2_BREF_TYPE_FREEMAP_NODE:
	case HAMMER2_BREF_TYPE_FREEMAP_LEAF:
	default:
		/*
		 * Point data at the device buffer and leave bp intact.
		 */
		chain->data = (void *)bdata;
		break;
	}

	/*
	 * Make sure the bp is not specifically owned by this thread before
	 * restoring to a possibly shared lock, so another hammer2 thread
	 * can release it.
	 */
	if (chain->bp)
		BUF_KERNPROC(chain->bp);
	ccms_thread_lock_restore(&chain->cst, ostate);
	return (0);
}

/*
 * Unlock and deref a chain element.
 *
 * On the last lock release any non-embedded data (chain->bp) will be
 * retired.
 */
void
hammer2_chain_unlock(hammer2_mount_t *hmp, hammer2_chain_t *chain)
{
	long *counterp;

	/*
	 * Release the CST lock but with a special 1->0 transition case.
	 *
	 * Returns non-zero if lock references remain.  When zero is
	 * returned the last lock reference is retained and any shared
	 * lock is upgraded to an exclusive lock for final disposition.
	 */
	if (ccms_thread_unlock_zero(&chain->cst)) {
		KKASSERT(chain->refs > 1);
		atomic_add_int(&chain->refs, -1);
		return;
	}

	/*
	 * Shortcut the case if the data is embedded or not resolved.
	 *
	 * Do NOT null-out pointers to embedded data (e.g. inode).
	 *
	 * The DIRTYBP flag is non-applicable in this situation and can
	 * be cleared to keep the flags state clean.
	 */
	if (chain->bp == NULL) {
		atomic_clear_int(&chain->flags, HAMMER2_CHAIN_DIRTYBP);
		ccms_thread_unlock(&chain->cst);
		hammer2_chain_drop(hmp, chain);
		return;
	}

	/*
	 * Statistics
	 */
	if ((chain->flags & HAMMER2_CHAIN_DIRTYBP) == 0) {
		;
	} else if (chain->flags & HAMMER2_CHAIN_IOFLUSH) {
		switch(chain->bref.type) {
		case HAMMER2_BREF_TYPE_DATA:
			counterp = &hammer2_ioa_file_write;
			break;
		case HAMMER2_BREF_TYPE_INODE:
			counterp = &hammer2_ioa_meta_write;
			break;
		case HAMMER2_BREF_TYPE_INDIRECT:
			counterp = &hammer2_ioa_indr_write;
			break;
		case HAMMER2_BREF_TYPE_FREEMAP_ROOT:
		case HAMMER2_BREF_TYPE_FREEMAP_NODE:
		case HAMMER2_BREF_TYPE_FREEMAP_LEAF:
			counterp = &hammer2_ioa_fmap_write;
			break;
		default:
			counterp = &hammer2_ioa_volu_write;
			break;
		}
		++*counterp;
	} else {
		switch(chain->bref.type) {
		case HAMMER2_BREF_TYPE_DATA:
			counterp = &hammer2_iod_file_write;
			break;
		case HAMMER2_BREF_TYPE_INODE:
			counterp = &hammer2_iod_meta_write;
			break;
		case HAMMER2_BREF_TYPE_INDIRECT:
			counterp = &hammer2_iod_indr_write;
			break;
		case HAMMER2_BREF_TYPE_FREEMAP_ROOT:
		case HAMMER2_BREF_TYPE_FREEMAP_NODE:
		case HAMMER2_BREF_TYPE_FREEMAP_LEAF:
			counterp = &hammer2_iod_fmap_write;
			break;
		default:
			counterp = &hammer2_iod_volu_write;
			break;
		}
		++*counterp;
	}

	/*
	 * Clean out the bp.
	 *
	 * If a device buffer was used for data be sure to destroy the
	 * buffer when we are done to avoid aliases (XXX what about the
	 * underlying VM pages?).
	 *
	 * NOTE: Freemap leaf's use reserved blocks and thus no aliasing
	 *	 is possible.
	 */
	if (chain->bref.type == HAMMER2_BREF_TYPE_DATA)
		chain->bp->b_flags |= B_RELBUF;

	/*
	 * The DIRTYBP flag tracks whether we have to bdwrite() the buffer
	 * or not.  The flag will get re-set when chain_modify() is called,
	 * even if MODIFIED is already set, allowing the OS to retire the
	 * buffer independent of a hammer2 flus.
	 */
	chain->data = NULL;
	if (chain->flags & HAMMER2_CHAIN_DIRTYBP) {
		atomic_clear_int(&chain->flags, HAMMER2_CHAIN_DIRTYBP);
		if (chain->flags & HAMMER2_CHAIN_IOFLUSH) {
			atomic_clear_int(&chain->flags,
					 HAMMER2_CHAIN_IOFLUSH);
			chain->bp->b_flags |= B_RELBUF;
			cluster_awrite(chain->bp);
		} else {
			chain->bp->b_flags |= B_CLUSTEROK;
			bdwrite(chain->bp);
		}
	} else {
		if (chain->flags & HAMMER2_CHAIN_IOFLUSH) {
			atomic_clear_int(&chain->flags,
					 HAMMER2_CHAIN_IOFLUSH);
			chain->bp->b_flags |= B_RELBUF;
			brelse(chain->bp);
		} else {
			/* bp might still be dirty */
			bqrelse(chain->bp);
		}
	}
	chain->bp = NULL;
	ccms_thread_unlock(&chain->cst);
	hammer2_chain_drop(hmp, chain);
}

/*
 * Resize the chain's physical storage allocation.  Chains can be resized
 * smaller without reallocating the storage.  Resizing larger will reallocate
 * the storage.
 *
 * Must be passed a locked chain.
 *
 * If you want the resize code to copy the data to the new block then the
 * caller should lock the chain RESOLVE_MAYBE or RESOLVE_ALWAYS.
 *
 * If the caller already holds a logical buffer containing the data and
 * intends to bdwrite() that buffer resolve with RESOLVE_NEVER.  The resize
 * operation will then not copy the data.
 *
 * This function is mostly used with DATA blocks locked RESOLVE_NEVER in order
 * to avoid instantiating a device buffer that conflicts with the vnode
 * data buffer.
 *
 * XXX flags currently ignored, uses chain->bp to detect data/no-data.
 */
void
hammer2_chain_resize(hammer2_inode_t *ip, hammer2_chain_t *chain,
		     int nradix, int flags)
{
	hammer2_mount_t *hmp = ip->hmp;
	struct buf *nbp;
	hammer2_off_t pbase;
	size_t obytes;
	size_t nbytes;
	size_t bbytes;
	int boff;
	char *bdata;
	int error;

	/*
	 * Only data and indirect blocks can be resized for now.
	 * (The volu root, inodes, and freemap elements use a fixed size).
	 */
	KKASSERT(chain != &hmp->vchain);
	KKASSERT(chain->bref.type == HAMMER2_BREF_TYPE_DATA ||
		 chain->bref.type == HAMMER2_BREF_TYPE_INDIRECT);

	/*
	 * Nothing to do if the element is already the proper size
	 */
	obytes = chain->bytes;
	nbytes = 1U << nradix;
	if (obytes == nbytes)
		return;

	/*
	 * Set MODIFIED and add a chain ref to prevent destruction.  Both
	 * modified flags share the same ref.
	 *
	 * If the chain is already marked MODIFIED then we can safely
	 * return the previous allocation to the pool without having to
	 * worry about snapshots.
	 */
	if ((chain->flags & HAMMER2_CHAIN_MODIFIED) == 0) {
		atomic_set_int(&chain->flags, HAMMER2_CHAIN_MODIFIED |
					      HAMMER2_CHAIN_MODIFY_TID);
		hammer2_chain_ref(hmp, chain);
	} else {
		hammer2_freemap_free(hmp, chain->bref.data_off,
				     chain->bref.type);
	}

	/*
	 * Relocate the block, even if making it smaller (because different
	 * block sizes may be in different regions).
	 */
	chain->bref.data_off = hammer2_freemap_alloc(hmp, chain->bref.type,
						     nbytes);
	chain->bytes = nbytes;
	ip->delta_dcount += (ssize_t)(nbytes - obytes); /* XXX atomic */

	/*
	 * The device buffer may be larger than the allocation size.
	 */
	if ((bbytes = chain->bytes) < HAMMER2_MINIOSIZE)
		bbytes = HAMMER2_MINIOSIZE;
	pbase = chain->bref.data_off & ~(hammer2_off_t)(bbytes - 1);
	boff = chain->bref.data_off & HAMMER2_OFF_MASK & (bbytes - 1);

	/*
	 * Only copy the data if resolved, otherwise the caller is
	 * responsible.
	 */
	if (chain->bp) {
		KKASSERT(chain->bref.type == HAMMER2_BREF_TYPE_INDIRECT ||
			 chain->bref.type == HAMMER2_BREF_TYPE_DATA);
		KKASSERT(chain != &hmp->vchain);	/* safety */

		/*
		 * The getblk() optimization can only be used if the
		 * physical block size matches the request.
		 */
		if (nbytes == bbytes) {
			nbp = getblk(hmp->devvp, pbase, bbytes, 0, 0);
			error = 0;
		} else {
			error = bread(hmp->devvp, pbase, bbytes, &nbp);
			KKASSERT(error == 0);
		}
		bdata = (char *)nbp->b_data + boff;

		if (nbytes < obytes) {
			bcopy(chain->data, bdata, nbytes);
		} else {
			bcopy(chain->data, bdata, obytes);
			bzero(bdata + obytes, nbytes - obytes);
		}

		/*
		 * NOTE: The INITIAL state of the chain is left intact.
		 *	 We depend on hammer2_chain_modify() to do the
		 *	 right thing.
		 *
		 * NOTE: We set B_NOCACHE to throw away the previous bp and
		 *	 any VM backing store, even if it was dirty.
		 *	 Otherwise we run the risk of a logical/device
		 *	 conflict on reallocation.
		 */
		chain->bp->b_flags |= B_RELBUF | B_NOCACHE;
		brelse(chain->bp);
		chain->bp = nbp;
		chain->data = (void *)bdata;
		hammer2_chain_modify(hmp, chain, 0);
	}

	/*
	 * Make sure the chain is marked MOVED and SUBMOD is set in the
	 * parent(s) so the adjustments are picked up by flush.
	 */
	if ((chain->flags & HAMMER2_CHAIN_MOVED) == 0) {
		hammer2_chain_ref(hmp, chain);
		atomic_set_int(&chain->flags, HAMMER2_CHAIN_MOVED);
	}
	hammer2_chain_parent_setsubmod(hmp, chain);
}

/*
 * Convert a locked chain that was retrieved read-only to read-write.
 *
 * If not already marked modified a new physical block will be allocated
 * and assigned to the bref.
 *
 * Non-data blocks - The chain should be locked to at least the RESOLVE_MAYBE
 *		     level or the COW operation will not work.
 *
 * Data blocks	   - The chain is usually locked RESOLVE_NEVER so as not to
 *		     run the data through the device buffers.
 */
void
hammer2_chain_modify(hammer2_mount_t *hmp, hammer2_chain_t *chain, int flags)
{
	struct buf *nbp;
	int error;
	hammer2_off_t pbase;
	size_t bbytes;
	size_t boff;
	void *bdata;

	/*
	 * Tells flush that modify_tid must be updated, otherwise only
	 * mirror_tid is updated.  This is the default.
	 */
	if ((flags & HAMMER2_MODIFY_NO_MODIFY_TID) == 0)
		atomic_set_int(&chain->flags, HAMMER2_CHAIN_MODIFY_TID);

	/*
	 * If the chain is already marked MODIFIED we can just return.
	 *
	 * However, it is possible that a prior lock/modify sequence
	 * retired the buffer.  During this lock/modify sequence MODIFIED
	 * may still be set but the buffer could wind up clean.  Since
	 * the caller is going to modify the buffer further we have to
	 * be sure that DIRTYBP is set again.
	 */
	if (chain->flags & HAMMER2_CHAIN_MODIFIED) {
		if ((flags & HAMMER2_MODIFY_OPTDATA) == 0 &&
		    chain->bp == NULL) {
			goto skip1;
		}
		atomic_set_int(&chain->flags, HAMMER2_CHAIN_DIRTYBP);
		return;
	}

	/*
	 * Set MODIFIED and add a chain ref to prevent destruction.  Both
	 * modified flags share the same ref.
	 */
	atomic_set_int(&chain->flags, HAMMER2_CHAIN_MODIFIED);
	hammer2_chain_ref(hmp, chain);

	/*
	 * We must allocate the copy-on-write block.
	 *
	 * If the data is embedded no other action is required.
	 *
	 * If the data is not embedded we acquire and clear the
	 * new block.  If chain->data is not NULL we then do the
	 * copy-on-write.  chain->data will then be repointed to the new
	 * buffer and the old buffer will be released.
	 *
	 * For newly created elements with no prior allocation we go
	 * through the copy-on-write steps except without the copying part.
	 */
	if (chain != &hmp->vchain) {
		if ((hammer2_debug & 0x0001) &&
		    (chain->bref.data_off & HAMMER2_OFF_MASK)) {
			kprintf("Replace %d\n", chain->bytes);
		}
		chain->bref.data_off =
			hammer2_freemap_alloc(hmp, chain->bref.type,
					      chain->bytes);
		/* XXX failed allocation */
	}

	/*
	 * If data instantiation is optional and the chain has no current
	 * data association (typical for DATA and newly-created INDIRECT
	 * elements), don't instantiate the buffer now.
	 */
	if ((flags & HAMMER2_MODIFY_OPTDATA) && chain->bp == NULL)
		goto skip2;

skip1:
	/*
	 * Setting the DIRTYBP flag will cause the buffer to be dirtied or
	 * written-out on unlock.  This bit is independent of the MODIFIED
	 * bit because the chain may still need meta-data adjustments done
	 * by virtue of MODIFIED for its parent, and the buffer can be
	 * flushed out (possibly multiple times) by the OS before that.
	 *
	 * Clearing the INITIAL flag (for indirect blocks) indicates that
	 * a zero-fill buffer has been instantiated.
	 */
	atomic_set_int(&chain->flags, HAMMER2_CHAIN_DIRTYBP);
	atomic_clear_int(&chain->flags, HAMMER2_CHAIN_INITIAL);

	/*
	 * We currently should never instantiate a device buffer for a
	 * file data chain.  (We definitely can for a freemap chain).
	 */
	KKASSERT(chain->bref.type != HAMMER2_BREF_TYPE_DATA);

	/*
	 * Execute COW operation
	 */
	switch(chain->bref.type) {
	case HAMMER2_BREF_TYPE_VOLUME:
	case HAMMER2_BREF_TYPE_INODE:
		/*
		 * The data is embedded, no copy-on-write operation is
		 * needed.
		 */
		KKASSERT(chain->bp == NULL);
		break;
	case HAMMER2_BREF_TYPE_DATA:
	case HAMMER2_BREF_TYPE_INDIRECT:
	case HAMMER2_BREF_TYPE_FREEMAP_ROOT:
	case HAMMER2_BREF_TYPE_FREEMAP_NODE:
	case HAMMER2_BREF_TYPE_FREEMAP_LEAF:
		/*
		 * Perform the copy-on-write operation
		 */
		KKASSERT(chain != &hmp->vchain);	/* safety */
		/*
		 * The device buffer may be larger than the allocation size.
		 */
		if ((bbytes = chain->bytes) < HAMMER2_MINIOSIZE)
			bbytes = HAMMER2_MINIOSIZE;
		pbase = chain->bref.data_off & ~(hammer2_off_t)(bbytes - 1);
		boff = chain->bref.data_off & HAMMER2_OFF_MASK & (bbytes - 1);

		/*
		 * The getblk() optimization can only be used if the
		 * physical block size matches the request.
		 */
		if (chain->bytes == bbytes) {
			nbp = getblk(hmp->devvp, pbase, bbytes, 0, 0);
			error = 0;
		} else {
			error = bread(hmp->devvp, pbase, bbytes, &nbp);
			KKASSERT(error == 0);
		}
		bdata = (char *)nbp->b_data + boff;

		/*
		 * Copy or zero-fill on write depending on whether
		 * chain->data exists or not.
		 */
		if (chain->data) {
			bcopy(chain->data, bdata, chain->bytes);
			KKASSERT(chain->bp != NULL);
		} else {
			bzero(bdata, chain->bytes);
		}
		if (chain->bp) {
			chain->bp->b_flags |= B_RELBUF;
			brelse(chain->bp);
		}
		chain->bp = nbp;
		chain->data = bdata;
		break;
	default:
		panic("hammer2_chain_modify: illegal non-embedded type %d",
		      chain->bref.type);
		break;

	}
skip2:
	if ((flags & HAMMER2_MODIFY_NOSUB) == 0)
		hammer2_chain_parent_setsubmod(hmp, chain);
}

/*
 * Mark the volume as having been modified.  This short-cut version
 * does not have to lock the volume's chain, which allows the ioctl
 * code to make adjustments to connections without deadlocking.
 */
void
hammer2_modify_volume(hammer2_mount_t *hmp)
{
	hammer2_voldata_lock(hmp);
	atomic_set_int(&hmp->vchain.flags, HAMMER2_CHAIN_MODIFIED_AUX);
	hammer2_voldata_unlock(hmp);
}

/*
 * Locate an in-memory chain.  The parent must be locked.  The in-memory
 * chain is returned or NULL if no in-memory chain is present.
 *
 * NOTE: A chain on-media might exist for this index when NULL is returned.
 */
hammer2_chain_t *
hammer2_chain_find(hammer2_mount_t *hmp, hammer2_chain_t *parent, int index)
{
	hammer2_chain_t dummy;
	hammer2_chain_t *chain;

	dummy.index = index;
	chain = RB_FIND(hammer2_chain_tree, &parent->rbhead, &dummy);
	return (chain);
}

/*
 * Return a locked chain structure with all associated data acquired.
 *
 * Caller must lock the parent on call, the returned child will be locked.
 */
hammer2_chain_t *
hammer2_chain_get(hammer2_mount_t *hmp, hammer2_chain_t *parent,
		  int index, int flags)
{
	hammer2_blockref_t *bref;
	hammer2_inode_t *ip;
	hammer2_chain_t *chain;
	hammer2_chain_t dummy;
	int how;
	ccms_state_t ostate;

	/*
	 * Figure out how to lock.  MAYBE can be used to optimized
	 * the initial-create state for indirect blocks.
	 */
	if (flags & (HAMMER2_LOOKUP_NODATA | HAMMER2_LOOKUP_NOLOCK))
		how = HAMMER2_RESOLVE_NEVER;
	else
		how = HAMMER2_RESOLVE_MAYBE;
	if (flags & (HAMMER2_LOOKUP_SHARED | HAMMER2_LOOKUP_NOLOCK))
		how |= HAMMER2_RESOLVE_SHARED;

	/*
	 * First see if we have a (possibly modified) chain element cached
	 * for this (parent, index).  Acquire the data if necessary.
	 *
	 * If chain->data is non-NULL the chain should already be marked
	 * modified.
	 */
	dummy.index = index;
	chain = RB_FIND(hammer2_chain_tree, &parent->rbhead, &dummy);
	if (chain) {
		if (flags & HAMMER2_LOOKUP_NOLOCK)
			hammer2_chain_ref(hmp, chain);
		else
			hammer2_chain_lock(hmp, chain, how);
		return(chain);
	}

	/*
	 * Upgrade our thread lock and handle any race that may have
	 * occurred.  Leave the lock upgraded for the rest of the get.
	 * We have to do this because we will be modifying the chain
	 * structure.
	 */
	ostate = ccms_thread_lock_upgrade(&parent->cst);
	chain = RB_FIND(hammer2_chain_tree, &parent->rbhead, &dummy);
	if (chain) {
		if (flags & HAMMER2_LOOKUP_NOLOCK)
			hammer2_chain_ref(hmp, chain);
		else
			hammer2_chain_lock(hmp, chain, how);
		ccms_thread_lock_restore(&parent->cst, ostate);
		return(chain);
	}

	/*
	 * The get function must always succeed, panic if there's no
	 * data to index.
	 */
	if (parent->flags & HAMMER2_CHAIN_INITIAL) {
		ccms_thread_lock_restore(&parent->cst, ostate);
		panic("hammer2_chain_get: Missing bref(1)");
		/* NOT REACHED */
	}

	/*
	 * Otherwise lookup the bref and issue I/O (switch on the parent)
	 */
	switch(parent->bref.type) {
	case HAMMER2_BREF_TYPE_INODE:
		KKASSERT(index >= 0 && index < HAMMER2_SET_COUNT);
		bref = &parent->data->ipdata.u.blockset.blockref[index];
		break;
	case HAMMER2_BREF_TYPE_INDIRECT:
	case HAMMER2_BREF_TYPE_FREEMAP_ROOT:
	case HAMMER2_BREF_TYPE_FREEMAP_NODE:
		KKASSERT(parent->data != NULL);
		KKASSERT(index >= 0 &&
			 index < parent->bytes / sizeof(hammer2_blockref_t));
		bref = &parent->data->npdata.blockref[index];
		break;
	case HAMMER2_BREF_TYPE_VOLUME:
		KKASSERT(index >= 0 && index < HAMMER2_SET_COUNT);
		bref = &hmp->voldata.sroot_blockset.blockref[index];
		break;
	default:
		bref = NULL;
		panic("hammer2_chain_get: unrecognized blockref type: %d",
		      parent->bref.type);
	}
	if (bref->type == 0) {
		panic("hammer2_chain_get: Missing bref(2)");
		/* NOT REACHED */
	}

	/*
	 * Allocate a chain structure representing the existing media
	 * entry.
	 *
	 * The locking operation we do later will issue I/O to read it.
	 */
	chain = hammer2_chain_alloc(hmp, bref);

	/*
	 * Link the chain into its parent.  Caller is expected to hold an
	 * exclusive lock on the parent.
	 */
	chain->parent = parent;
	chain->index = index;
	if (RB_INSERT(hammer2_chain_tree, &parent->rbhead, chain))
		panic("hammer2_chain_link: collision");
	atomic_set_int(&chain->flags, HAMMER2_CHAIN_ONRBTREE);
	KKASSERT(parent->refs > 0);
	atomic_add_int(&parent->refs, 1);	/* for red-black entry */
	ccms_thread_lock_restore(&parent->cst, ostate);

	/*
	 * Additional linkage for inodes.  Reuse the parent pointer to
	 * find the parent directory.
	 *
	 * The ccms_inode is initialized from its parent directory.  The
	 * chain of ccms_inode's is seeded by the mount code.
	 */
	if (bref->type == HAMMER2_BREF_TYPE_INODE) {
		ip = chain->u.ip;
		while (parent->bref.type == HAMMER2_BREF_TYPE_INDIRECT)
			parent = parent->parent;
		if (parent->bref.type == HAMMER2_BREF_TYPE_INODE) {
			ip->pip = parent->u.ip;
			ip->pmp = parent->u.ip->pmp;
			ccms_cst_init(&ip->topo_cst, &ip->chain);
		}
	}

	/*
	 * Our new chain structure has already been referenced and locked
	 * but the lock code handles the I/O so call it to resolve the data.
	 * Then release one of our two exclusive locks.
	 *
	 * If NOLOCK is set the release will release the one-and-only lock.
	 */
	if ((flags & HAMMER2_LOOKUP_NOLOCK) == 0) {
		hammer2_chain_lock(hmp, chain, how);	/* recusive lock */
		hammer2_chain_drop(hmp, chain);		/* excess ref */
	}
	ccms_thread_unlock(&chain->cst);			/* from alloc */

	return (chain);
}

/*
 * Locate any key between key_beg and key_end inclusive.  (*parentp)
 * typically points to an inode but can also point to a related indirect
 * block and this function will recurse upwards and find the inode again.
 *
 * WARNING!  THIS DOES NOT RETURN KEYS IN LOGICAL KEY ORDER!  ANY KEY
 *	     WITHIN THE RANGE CAN BE RETURNED.  HOWEVER, AN ITERATION
 *	     WHICH PICKS UP WHERE WE LEFT OFF WILL CONTINUE THE SCAN.
 *
 * (*parentp) must be exclusively locked and referenced and can be an inode
 * or an existing indirect block within the inode.
 *
 * On return (*parentp) will be modified to point at the deepest parent chain
 * element encountered during the search, as a helper for an insertion or
 * deletion.   The new (*parentp) will be locked and referenced and the old
 * will be unlocked and dereferenced (no change if they are both the same).
 *
 * The matching chain will be returned exclusively locked and referenced.
 *
 * NULL is returned if no match was found, but (*parentp) will still
 * potentially be adjusted.
 *
 * This function will also recurse up the chain if the key is not within the
 * current parent's range.  (*parentp) can never be set to NULL.  An iteration
 * can simply allow (*parentp) to float inside the loop.
 */
hammer2_chain_t *
hammer2_chain_lookup(hammer2_mount_t *hmp, hammer2_chain_t **parentp,
		     hammer2_key_t key_beg, hammer2_key_t key_end,
		     int flags)
{
	hammer2_chain_t *parent;
	hammer2_chain_t *chain;
	hammer2_chain_t *tmp;
	hammer2_blockref_t *base;
	hammer2_blockref_t *bref;
	hammer2_key_t scan_beg;
	hammer2_key_t scan_end;
	int count = 0;
	int i;
	int how_always = HAMMER2_RESOLVE_ALWAYS;
	int how_maybe = HAMMER2_RESOLVE_MAYBE;

	if (flags & (HAMMER2_LOOKUP_SHARED | HAMMER2_LOOKUP_NOLOCK)) {
		how_maybe |= HAMMER2_RESOLVE_SHARED;
		how_always |= HAMMER2_RESOLVE_SHARED;
	}

	/*
	 * Recurse (*parentp) upward if necessary until the parent completely
	 * encloses the key range or we hit the inode.
	 */
	parent = *parentp;
	while (parent->bref.type == HAMMER2_BREF_TYPE_INDIRECT ||
	       parent->bref.type == HAMMER2_BREF_TYPE_FREEMAP_NODE) {
		scan_beg = parent->bref.key;
		scan_end = scan_beg +
			   ((hammer2_key_t)1 << parent->bref.keybits) - 1;
		if (key_beg >= scan_beg && key_end <= scan_end)
			break;
		hammer2_chain_ref(hmp, parent);		/* ref old parent */
		hammer2_chain_unlock(hmp, parent);	/* unlock old parent */
		parent = parent->parent;
							/* lock new parent */
		hammer2_chain_lock(hmp, parent, how_maybe);
		hammer2_chain_drop(hmp, *parentp);	/* drop old parent */
		*parentp = parent;			/* new parent */
	}

again:
	/*
	 * Locate the blockref array.  Currently we do a fully associative
	 * search through the array.
	 */
	switch(parent->bref.type) {
	case HAMMER2_BREF_TYPE_INODE:
		/*
		 * Special shortcut for embedded data returns the inode
		 * itself.  Callers must detect this condition and access
		 * the embedded data (the strategy code does this for us).
		 *
		 * This is only applicable to regular files and softlinks.
		 */
		if (parent->data->ipdata.op_flags & HAMMER2_OPFLAG_DIRECTDATA) {
			if (flags & HAMMER2_LOOKUP_NOLOCK)
				hammer2_chain_ref(hmp, parent);
			else
				hammer2_chain_lock(hmp, parent, how_always);
			return (parent);
		}
		base = &parent->data->ipdata.u.blockset.blockref[0];
		count = HAMMER2_SET_COUNT;
		break;
	case HAMMER2_BREF_TYPE_INDIRECT:
	case HAMMER2_BREF_TYPE_FREEMAP_ROOT:
	case HAMMER2_BREF_TYPE_FREEMAP_NODE:
		/*
		 * Optimize indirect blocks in the INITIAL state to avoid
		 * I/O.
		 */
		if (parent->flags & HAMMER2_CHAIN_INITIAL) {
			base = NULL;
		} else {
			if (parent->data == NULL)
				panic("parent->data is NULL");
			base = &parent->data->npdata.blockref[0];
		}
		count = parent->bytes / sizeof(hammer2_blockref_t);
		break;
	case HAMMER2_BREF_TYPE_VOLUME:
		base = &hmp->voldata.sroot_blockset.blockref[0];
		count = HAMMER2_SET_COUNT;
		break;
	default:
		panic("hammer2_chain_lookup: unrecognized blockref type: %d",
		      parent->bref.type);
		base = NULL;	/* safety */
		count = 0;	/* safety */
	}

	/*
	 * If the element and key overlap we use the element.
	 *
	 * NOTE!  Deleted elements are effectively invisible.  A Deleted
	 *	  elements covers (makes invisible) any original media
	 *	  data.
	 */
	bref = NULL;
	for (i = 0; i < count; ++i) {
		tmp = hammer2_chain_find(hmp, parent, i);
		if (tmp) {
			if (tmp->flags & HAMMER2_CHAIN_DELETED)
				continue;
			bref = &tmp->bref;
			KKASSERT(bref->type != 0);
		} else if (base == NULL || base[i].type == 0) {
			continue;
		} else {
			bref = &base[i];
		}
		scan_beg = bref->key;
		scan_end = scan_beg + ((hammer2_key_t)1 << bref->keybits) - 1;
		if (key_beg <= scan_end && key_end >= scan_beg)
			break;
	}
	if (i == count) {
		if (key_beg == key_end)
			return (NULL);
		return (hammer2_chain_next(hmp, parentp, NULL,
					   key_beg, key_end, flags));
	}

	/*
	 * Acquire the new chain element.  If the chain element is an
	 * indirect block we must search recursively.
	 */
	chain = hammer2_chain_get(hmp, parent, i, flags);
	if (chain == NULL)
		return (NULL);

	/*
	 * If the chain element is an indirect block it becomes the new
	 * parent and we loop on it.
	 *
	 * The parent always has to be locked with at least RESOLVE_MAYBE,
	 * so it might need a fixup if the caller passed incompatible flags.
	 */
	if (chain->bref.type == HAMMER2_BREF_TYPE_INDIRECT ||
	    chain->bref.type == HAMMER2_BREF_TYPE_FREEMAP_NODE) {
		hammer2_chain_unlock(hmp, parent);
		*parentp = parent = chain;
		if (flags & HAMMER2_LOOKUP_NOLOCK) {
			hammer2_chain_lock(hmp, chain, how_maybe);
			hammer2_chain_drop(hmp, chain);	/* excess ref */
		} else if (flags & HAMMER2_LOOKUP_NODATA) {
			hammer2_chain_lock(hmp, chain, how_maybe);
			hammer2_chain_unlock(hmp, chain);
		}
		goto again;
	}

	/*
	 * All done, return chain
	 */
	return (chain);
}

/*
 * After having issued a lookup we can iterate all matching keys.
 *
 * If chain is non-NULL we continue the iteration from just after it's index.
 *
 * If chain is NULL we assume the parent was exhausted and continue the
 * iteration at the next parent.
 *
 * parent must be locked on entry and remains locked throughout.  chain's
 * lock status must match flags.
 */
hammer2_chain_t *
hammer2_chain_next(hammer2_mount_t *hmp, hammer2_chain_t **parentp,
		   hammer2_chain_t *chain,
		   hammer2_key_t key_beg, hammer2_key_t key_end,
		   int flags)
{
	hammer2_chain_t *parent;
	hammer2_chain_t *tmp;
	hammer2_blockref_t *base;
	hammer2_blockref_t *bref;
	hammer2_key_t scan_beg;
	hammer2_key_t scan_end;
	int i;
	int how_maybe = HAMMER2_RESOLVE_MAYBE;
	int count;

	if (flags & (HAMMER2_LOOKUP_SHARED | HAMMER2_LOOKUP_NOLOCK))
		how_maybe |= HAMMER2_RESOLVE_SHARED;

	parent = *parentp;

again:
	/*
	 * Calculate the next index and recalculate the parent if necessary.
	 */
	if (chain) {
		/*
		 * Continue iteration within current parent.  If not NULL
		 * the passed-in chain may or may not be locked, based on
		 * the LOOKUP_NOLOCK flag (passed in as returned from lookup
		 * or a prior next).
		 */
		i = chain->index + 1;
		if (flags & HAMMER2_LOOKUP_NOLOCK)
			hammer2_chain_drop(hmp, chain);
		else
			hammer2_chain_unlock(hmp, chain);

		/*
		 * Any scan where the lookup returned degenerate data embedded
		 * in the inode has an invalid index and must terminate.
		 */
		if (chain == parent)
			return(NULL);
		chain = NULL;
	} else if (parent->bref.type != HAMMER2_BREF_TYPE_INDIRECT &&
		   parent->bref.type != HAMMER2_BREF_TYPE_FREEMAP_NODE) {
		/*
		 * We reached the end of the iteration.
		 */
		return (NULL);
	} else {
		/*
		 * Continue iteration with next parent unless the current
		 * parent covers the range.
		 */
		hammer2_chain_t *nparent;

		scan_beg = parent->bref.key;
		scan_end = scan_beg +
			    ((hammer2_key_t)1 << parent->bref.keybits) - 1;
		if (key_beg >= scan_beg && key_end <= scan_end)
			return (NULL);

		i = parent->index + 1;
		nparent = parent->parent;
		hammer2_chain_ref(hmp, nparent);	/* ref new parent */
		hammer2_chain_unlock(hmp, parent);	/* unlock old parent */
							/* lock new parent */
		hammer2_chain_lock(hmp, nparent, how_maybe);
		hammer2_chain_drop(hmp, nparent);	/* drop excess ref */
		*parentp = parent = nparent;
	}

again2:
	/*
	 * Locate the blockref array.  Currently we do a fully associative
	 * search through the array.
	 */
	switch(parent->bref.type) {
	case HAMMER2_BREF_TYPE_INODE:
		base = &parent->data->ipdata.u.blockset.blockref[0];
		count = HAMMER2_SET_COUNT;
		break;
	case HAMMER2_BREF_TYPE_INDIRECT:
	case HAMMER2_BREF_TYPE_FREEMAP_ROOT:
	case HAMMER2_BREF_TYPE_FREEMAP_NODE:
		if (parent->flags & HAMMER2_CHAIN_INITIAL) {
			base = NULL;
		} else {
			KKASSERT(parent->data != NULL);
			base = &parent->data->npdata.blockref[0];
		}
		count = parent->bytes / sizeof(hammer2_blockref_t);
		break;
	case HAMMER2_BREF_TYPE_VOLUME:
		base = &hmp->voldata.sroot_blockset.blockref[0];
		count = HAMMER2_SET_COUNT;
		break;
	default:
		panic("hammer2_chain_next: unrecognized blockref type: %d",
		      parent->bref.type);
		base = NULL;	/* safety */
		count = 0;	/* safety */
		break;
	}
	KKASSERT(i <= count);

	/*
	 * Look for the key.  If we are unable to find a match and an exact
	 * match was requested we return NULL.  If a range was requested we
	 * run hammer2_chain_next() to iterate.
	 *
	 * NOTE!  Deleted elements are effectively invisible.  A Deleted
	 *	  elements covers (makes invisible) any original media
	 *	  data.
	 */
	bref = NULL;
	while (i < count) {
		tmp = hammer2_chain_find(hmp, parent, i);
		if (tmp) {
			if (tmp->flags & HAMMER2_CHAIN_DELETED) {
				++i;
				continue;
			}
			bref = &tmp->bref;
		} else if (base == NULL || base[i].type == 0) {
			++i;
			continue;
		} else {
			bref = &base[i];
		}
		scan_beg = bref->key;
		scan_end = scan_beg + ((hammer2_key_t)1 << bref->keybits) - 1;
		if (key_beg <= scan_end && key_end >= scan_beg)
			break;
		++i;
	}

	/*
	 * If we couldn't find a match recurse up a parent to continue the
	 * search.
	 */
	if (i == count)
		goto again;

	/*
	 * Acquire the new chain element.  If the chain element is an
	 * indirect block we must search recursively.
	 */
	chain = hammer2_chain_get(hmp, parent, i, flags);
	if (chain == NULL)
		return (NULL);

	/*
	 * If the chain element is an indirect block it becomes the new
	 * parent and we loop on it.
	 *
	 * The parent always has to be locked with at least RESOLVE_MAYBE,
	 * so it might need a fixup if the caller passed incompatible flags.
	 */
	if (chain->bref.type == HAMMER2_BREF_TYPE_INDIRECT ||
	    chain->bref.type == HAMMER2_BREF_TYPE_FREEMAP_NODE) {
		hammer2_chain_unlock(hmp, parent);
		*parentp = parent = chain;
		chain = NULL;
		if (flags & HAMMER2_LOOKUP_NOLOCK) {
			hammer2_chain_lock(hmp, parent, how_maybe);
			hammer2_chain_drop(hmp, parent);	/* excess ref */
		} else if (flags & HAMMER2_LOOKUP_NODATA) {
			hammer2_chain_lock(hmp, parent, how_maybe);
			hammer2_chain_unlock(hmp, parent);
		}
		i = 0;
		goto again2;
	}

	/*
	 * All done, return chain
	 */
	return (chain);
}

/*
 * Create and return a new hammer2 system memory structure of the specified
 * key, type and size and insert it RELATIVE TO (PARENT).
 *
 * (parent) is typically either an inode or an indirect block, acquired
 * acquired as a side effect of issuing a prior failed lookup.  parent
 * must be locked and held.  Do not pass the inode chain to this function
 * unless that is the chain returned by the failed lookup.
 *
 * Non-indirect types will automatically allocate indirect blocks as required
 * if the new item does not fit in the current (parent).
 *
 * Indirect types will move a portion of the existing blockref array in
 * (parent) into the new indirect type and then use one of the free slots
 * to emplace the new indirect type.
 *
 * A new locked, referenced chain element is returned of the specified type.
 * The element may or may not have a data area associated with it:
 *
 *	VOLUME		not allowed here
 *	INODE		embedded data are will be set-up
 *	INDIRECT	not allowed here
 *	DATA		no data area will be set-up (caller is expected
 *			to have logical buffers, we don't want to alias
 *			the data onto device buffers!).
 *
 * Requires an exclusively locked parent.
 */
hammer2_chain_t *
hammer2_chain_create(hammer2_mount_t *hmp, hammer2_chain_t *parent,
		     hammer2_chain_t *chain,
		     hammer2_key_t key, int keybits, int type, size_t bytes,
		     int *errorp)
{
	hammer2_blockref_t dummy;
	hammer2_blockref_t *base;
	hammer2_chain_t dummy_chain;
	int unlock_parent = 0;
	int allocated = 0;
	int count;
	int i;

	KKASSERT(ccms_thread_lock_owned(&parent->cst));
	*errorp = 0;

	if (chain == NULL) {
		/*
		 * First allocate media space and construct the dummy bref,
		 * then allocate the in-memory chain structure.
		 */
		bzero(&dummy, sizeof(dummy));
		dummy.type = type;
		dummy.key = key;
		dummy.keybits = keybits;
		dummy.data_off = hammer2_allocsize(bytes);
		dummy.methods = parent->bref.methods;
		chain = hammer2_chain_alloc(hmp, &dummy);
		allocated = 1;

		/*
		 * We do NOT set INITIAL here (yet).  INITIAL is only
		 * used for indirect blocks.
		 *
		 * Recalculate bytes to reflect the actual media block
		 * allocation.
		 */
		bytes = (hammer2_off_t)1 <<
			(int)(chain->bref.data_off & HAMMER2_OFF_MASK_RADIX);
		chain->bytes = bytes;

		switch(type) {
		case HAMMER2_BREF_TYPE_VOLUME:
			panic("hammer2_chain_create: called with volume type");
			break;
		case HAMMER2_BREF_TYPE_INODE:
			KKASSERT(bytes == HAMMER2_INODE_BYTES);
			chain->data = (void *)&chain->u.ip->ip_data;
			break;
		case HAMMER2_BREF_TYPE_INDIRECT:
			panic("hammer2_chain_create: cannot be used to"
			      "create indirect block");
			break;
		case HAMMER2_BREF_TYPE_FREEMAP_ROOT:
		case HAMMER2_BREF_TYPE_FREEMAP_NODE:
			panic("hammer2_chain_create: cannot be used to"
			      "create freemap root or node");
			break;
		case HAMMER2_BREF_TYPE_FREEMAP_LEAF:
		case HAMMER2_BREF_TYPE_DATA:
		default:
			/* leave chain->data NULL */
			KKASSERT(chain->data == NULL);
			break;
		}
	} else {
		/*
		 * Potentially update the chain's key/keybits.
		 */
		chain->bref.key = key;
		chain->bref.keybits = keybits;
	}

again:
	/*
	 * Locate a free blockref in the parent's array
	 */
	switch(parent->bref.type) {
	case HAMMER2_BREF_TYPE_INODE:
		KKASSERT((parent->u.ip->ip_data.op_flags &
			  HAMMER2_OPFLAG_DIRECTDATA) == 0);
		KKASSERT(parent->data != NULL);
		base = &parent->data->ipdata.u.blockset.blockref[0];
		count = HAMMER2_SET_COUNT;
		break;
	case HAMMER2_BREF_TYPE_INDIRECT:
	case HAMMER2_BREF_TYPE_FREEMAP_ROOT:
	case HAMMER2_BREF_TYPE_FREEMAP_NODE:
		if (parent->flags & HAMMER2_CHAIN_INITIAL) {
			base = NULL;
		} else {
			KKASSERT(parent->data != NULL);
			base = &parent->data->npdata.blockref[0];
		}
		count = parent->bytes / sizeof(hammer2_blockref_t);
		break;
	case HAMMER2_BREF_TYPE_VOLUME:
		KKASSERT(parent->data != NULL);
		base = &hmp->voldata.sroot_blockset.blockref[0];
		count = HAMMER2_SET_COUNT;
		break;
	default:
		panic("hammer2_chain_create: unrecognized blockref type: %d",
		      parent->bref.type);
		count = 0;
		break;
	}

	/*
	 * Scan for an unallocated bref, also skipping any slots occupied
	 * by in-memory chain elements that may not yet have been updated
	 * in the parent's bref array.
	 */
	bzero(&dummy_chain, sizeof(dummy_chain));
	for (i = 0; i < count; ++i) {
		if (base == NULL) {
			dummy_chain.index = i;
			if (RB_FIND(hammer2_chain_tree,
				    &parent->rbhead, &dummy_chain) == NULL) {
				break;
			}
		} else if (base[i].type == 0) {
			dummy_chain.index = i;
			if (RB_FIND(hammer2_chain_tree,
				    &parent->rbhead, &dummy_chain) == NULL) {
				break;
			}
		}
	}

	/*
	 * If no free blockref could be found we must create an indirect
	 * block and move a number of blockrefs into it.  With the parent
	 * locked we can safely lock each child in order to move it without
	 * causing a deadlock.
	 *
	 * This may return the new indirect block or the old parent depending
	 * on where the key falls.  NULL is returned on error.  The most
	 * typical error is EAGAIN (flush conflict during chain move).
	 */
	if (i == count) {
		hammer2_chain_t *nparent;

		nparent = hammer2_chain_create_indirect(hmp, parent,
							key, keybits,
							errorp);
		if (nparent == NULL) {
			if (allocated)
				hammer2_chain_free(hmp, chain);
			chain = NULL;
			goto done;
		}
		if (parent != nparent) {
			if (unlock_parent)
				hammer2_chain_unlock(hmp, parent);
			parent = nparent;
			unlock_parent = 1;
		}
		goto again;
	}

	/*
	 * Link the chain into its parent.  Later on we will have to set
	 * the MOVED bit in situations where we don't mark the new chain
	 * as being modified.
	 */
	if (chain->parent != NULL)
		panic("hammer2: hammer2_chain_create: chain already connected");
	KKASSERT(chain->parent == NULL);
	chain->parent = parent;
	chain->index = i;
	if (RB_INSERT(hammer2_chain_tree, &parent->rbhead, chain))
		panic("hammer2_chain_link: collision");
	atomic_set_int(&chain->flags, HAMMER2_CHAIN_ONRBTREE);
	KKASSERT((chain->flags & HAMMER2_CHAIN_DELETED) == 0);
	KKASSERT(parent->refs > 0);
	atomic_add_int(&parent->refs, 1);

	/*
	 * Additional linkage for inodes.  Reuse the parent pointer to
	 * find the parent directory.
	 *
	 * Cumulative adjustments are inherited on [re]attach and will
	 * propagate up the tree on the next flush.
	 *
	 * The ccms_inode is initialized from its parent directory.  The
	 * chain of ccms_inode's is seeded by the mount code.
	 */
	if (chain->bref.type == HAMMER2_BREF_TYPE_INODE) {
		hammer2_chain_t *scan = parent;
		hammer2_inode_t *ip = chain->u.ip;

		while (scan->bref.type == HAMMER2_BREF_TYPE_INDIRECT)
			scan = scan->parent;
		if (scan->bref.type == HAMMER2_BREF_TYPE_INODE) {
			ip->pip = scan->u.ip;
			ip->pmp = scan->u.ip->pmp;
			ip->pip->delta_icount += ip->ip_data.inode_count;
			ip->pip->delta_dcount += ip->ip_data.data_count;
			++ip->pip->delta_icount;
			ccms_cst_init(&ip->topo_cst, &ip->chain);
		}
	}

	/*
	 * (allocated) indicates that this is a newly-created chain element
	 * rather than a renamed chain element.  In this situation we want
	 * to place the chain element in the MODIFIED state.
	 *
	 * The data area will be set up as follows:
	 *
	 *	VOLUME		not allowed here.
	 *
	 *	INODE		embedded data are will be set-up.
	 *
	 *	INDIRECT	not allowed here.
	 *
	 *	DATA		no data area will be set-up (caller is expected
	 *			to have logical buffers, we don't want to alias
	 *			the data onto device buffers!).
	 */
	if (allocated) {
		switch(chain->bref.type) {
		case HAMMER2_BREF_TYPE_DATA:
		case HAMMER2_BREF_TYPE_FREEMAP_LEAF:
			hammer2_chain_modify(hmp, chain,
					     HAMMER2_MODIFY_OPTDATA);
			break;
		case HAMMER2_BREF_TYPE_INDIRECT:
		case HAMMER2_BREF_TYPE_FREEMAP_ROOT:
		case HAMMER2_BREF_TYPE_FREEMAP_NODE:
			/* not supported in this function */
			panic("hammer2_chain_create: bad type");
			atomic_set_int(&chain->flags, HAMMER2_CHAIN_INITIAL);
			hammer2_chain_modify(hmp, chain,
					     HAMMER2_MODIFY_OPTDATA);
			break;
		default:
			hammer2_chain_modify(hmp, chain, 0);
			break;
		}
	} else {
		/*
		 * When reconnecting inodes we have to call setsubmod()
		 * to ensure that its state propagates up the newly
		 * connected parent.
		 *
		 * Make sure MOVED is set but do not update bref_flush.  If
		 * the chain is undergoing modification bref_flush will be
		 * updated when it gets flushed.  If it is not then the
		 * bref may not have been flushed yet and we do not want to
		 * set MODIFIED here as this could result in unnecessary
		 * reallocations.
		 */
		if ((chain->flags & HAMMER2_CHAIN_MOVED) == 0) {
			hammer2_chain_ref(hmp, chain);
			atomic_set_int(&chain->flags, HAMMER2_CHAIN_MOVED);
		}
		hammer2_chain_parent_setsubmod(hmp, chain);
	}

done:
	if (unlock_parent)
		hammer2_chain_unlock(hmp, parent);
	return (chain);
}

/*
 * Create an indirect block that covers one or more of the elements in the
 * current parent.  Either returns the existing parent with no locking or
 * ref changes or returns the new indirect block locked and referenced
 * and leaving the original parent lock/ref intact as well.
 *
 * If an error occurs, NULL is returned and *errorp is set to the error.
 * EAGAIN can be returned to indicate a flush collision which requires the
 * caller to retry.
 *
 * The returned chain depends on where the specified key falls.
 *
 * The key/keybits for the indirect mode only needs to follow three rules:
 *
 * (1) That all elements underneath it fit within its key space and
 *
 * (2) That all elements outside it are outside its key space.
 *
 * (3) When creating the new indirect block any elements in the current
 *     parent that fit within the new indirect block's keyspace must be
 *     moved into the new indirect block.
 *
 * (4) The keyspace chosen for the inserted indirect block CAN cover a wider
 *     keyspace the the current parent, but lookup/iteration rules will
 *     ensure (and must ensure) that rule (2) for all parents leading up
 *     to the nearest inode or the root volume header is adhered to.  This
 *     is accomplished by always recursing through matching keyspaces in
 *     the hammer2_chain_lookup() and hammer2_chain_next() API.
 *
 * The current implementation calculates the current worst-case keyspace by
 * iterating the current parent and then divides it into two halves, choosing
 * whichever half has the most elements (not necessarily the half containing
 * the requested key).
 *
 * We can also opt to use the half with the least number of elements.  This
 * causes lower-numbered keys (aka logical file offsets) to recurse through
 * fewer indirect blocks and higher-numbered keys to recurse through more.
 * This also has the risk of not moving enough elements to the new indirect
 * block and being forced to create several indirect blocks before the element
 * can be inserted.
 *
 * Must be called with an exclusively locked parent.
 */
static
hammer2_chain_t *
hammer2_chain_create_indirect(hammer2_mount_t *hmp, hammer2_chain_t *parent,
			      hammer2_key_t create_key, int create_bits,
			      int *errorp)
{
	hammer2_blockref_t *base;
	hammer2_blockref_t *bref;
	hammer2_chain_t *chain;
	hammer2_chain_t *ichain;
	hammer2_chain_t dummy;
	hammer2_key_t key = create_key;
	int keybits = create_bits;
	int locount = 0;
	int hicount = 0;
	int count;
	int nbytes;
	int i;

	/*
	 * Calculate the base blockref pointer or NULL if the chain
	 * is known to be empty.  We need to calculate the array count
	 * for RB lookups either way.
	 */
	KKASSERT(ccms_thread_lock_owned(&parent->cst));
	*errorp = 0;

	hammer2_chain_modify(hmp, parent, HAMMER2_MODIFY_OPTDATA);
	if (parent->flags & HAMMER2_CHAIN_INITIAL) {
		base = NULL;

		switch(parent->bref.type) {
		case HAMMER2_BREF_TYPE_INODE:
			count = HAMMER2_SET_COUNT;
			break;
		case HAMMER2_BREF_TYPE_INDIRECT:
		case HAMMER2_BREF_TYPE_FREEMAP_ROOT:
		case HAMMER2_BREF_TYPE_FREEMAP_NODE:
			count = parent->bytes / sizeof(hammer2_blockref_t);
			break;
		case HAMMER2_BREF_TYPE_VOLUME:
			count = HAMMER2_SET_COUNT;
			break;
		default:
			panic("hammer2_chain_create_indirect: "
			      "unrecognized blockref type: %d",
			      parent->bref.type);
			count = 0;
			break;
		}
	} else {
		switch(parent->bref.type) {
		case HAMMER2_BREF_TYPE_INODE:
			base = &parent->data->ipdata.u.blockset.blockref[0];
			count = HAMMER2_SET_COUNT;
			break;
		case HAMMER2_BREF_TYPE_INDIRECT:
		case HAMMER2_BREF_TYPE_FREEMAP_ROOT:
		case HAMMER2_BREF_TYPE_FREEMAP_NODE:
			base = &parent->data->npdata.blockref[0];
			count = parent->bytes / sizeof(hammer2_blockref_t);
			break;
		case HAMMER2_BREF_TYPE_VOLUME:
			base = &hmp->voldata.sroot_blockset.blockref[0];
			count = HAMMER2_SET_COUNT;
			break;
		default:
			panic("hammer2_chain_create_indirect: "
			      "unrecognized blockref type: %d",
			      parent->bref.type);
			count = 0;
			break;
		}
	}

	/*
	 * Scan for an unallocated bref, also skipping any slots occupied
	 * by in-memory chain elements which may not yet have been updated
	 * in the parent's bref array.
	 */
	bzero(&dummy, sizeof(dummy));
	for (i = 0; i < count; ++i) {
		int nkeybits;

		dummy.index = i;
		chain = RB_FIND(hammer2_chain_tree, &parent->rbhead, &dummy);
		if (chain) {
			/*
			 * NOTE! CHAIN_DELETED elements have to be adjusted
			 *	 too, they cannot be ignored.
			 */
			bref = &chain->bref;
		} else if (base && base[i].type) {
			bref = &base[i];
		} else {
			continue;
		}

		/*
		 * Expand our calculated key range (key, keybits) to fit
		 * the scanned key.  nkeybits represents the full range
		 * that we will later cut in half (two halves @ nkeybits - 1).
		 */
		nkeybits = keybits;
		if (nkeybits < bref->keybits)
			nkeybits = bref->keybits;
		while (nkeybits < 64 &&
		       (~(((hammer2_key_t)1 << nkeybits) - 1) &
		        (key ^ bref->key)) != 0) {
			++nkeybits;
		}

		/*
		 * If the new key range is larger we have to determine
		 * which side of the new key range the existing keys fall
		 * under by checking the high bit, then collapsing the
		 * locount into the hicount or vise-versa.
		 */
		if (keybits != nkeybits) {
			if (((hammer2_key_t)1 << (nkeybits - 1)) & key) {
				hicount += locount;
				locount = 0;
			} else {
				locount += hicount;
				hicount = 0;
			}
			keybits = nkeybits;
		}

		/*
		 * The newly scanned key will be in the lower half or the
		 * higher half of the (new) key range.
		 */
		if (((hammer2_key_t)1 << (nkeybits - 1)) & bref->key)
			++hicount;
		else
			++locount;
	}

	/*
	 * Adjust keybits to represent half of the full range calculated
	 * above (radix 63 max)
	 */
	--keybits;

	/*
	 * Select whichever half contains the most elements.  Theoretically
	 * we can select either side as long as it contains at least one
	 * element (in order to ensure that a free slot is present to hold
	 * the indirect block).
	 */
	key &= ~(((hammer2_key_t)1 << keybits) - 1);
	if (hammer2_indirect_optimize) {
		/*
		 * Insert node for least number of keys, this will arrange
		 * the first few blocks of a large file or the first few
		 * inodes in a directory with fewer indirect blocks when
		 * created linearly.
		 */
		if (hicount < locount && hicount != 0)
			key |= (hammer2_key_t)1 << keybits;
		else
			key &= ~(hammer2_key_t)1 << keybits;
	} else {
		/*
		 * Insert node for most number of keys, best for heavily
		 * fragmented files.
		 */
		if (hicount > locount)
			key |= (hammer2_key_t)1 << keybits;
		else
			key &= ~(hammer2_key_t)1 << keybits;
	}

	/*
	 * How big should our new indirect block be?  It has to be at least
	 * as large as its parent.
	 */
	if (parent->bref.type == HAMMER2_BREF_TYPE_INODE)
		nbytes = HAMMER2_IND_BYTES_MIN;
	else
		nbytes = HAMMER2_IND_BYTES_MAX;
	if (nbytes < count * sizeof(hammer2_blockref_t))
		nbytes = count * sizeof(hammer2_blockref_t);

	/*
	 * Ok, create our new indirect block
	 */
	switch(parent->bref.type) {
	case HAMMER2_BREF_TYPE_FREEMAP_ROOT:
	case HAMMER2_BREF_TYPE_FREEMAP_NODE:
		dummy.bref.type = HAMMER2_BREF_TYPE_FREEMAP_NODE;
		break;
	default:
		dummy.bref.type = HAMMER2_BREF_TYPE_INDIRECT;
		break;
	}
	dummy.bref.key = key;
	dummy.bref.keybits = keybits;
	dummy.bref.data_off = hammer2_allocsize(nbytes);
	dummy.bref.methods = parent->bref.methods;
	ichain = hammer2_chain_alloc(hmp, &dummy.bref);
	atomic_set_int(&ichain->flags, HAMMER2_CHAIN_INITIAL);

	/*
	 * Iterate the original parent and move the matching brefs into
	 * the new indirect block.
	 */
	for (i = 0; i < count; ++i) {
		/*
		 * For keying purposes access the bref from the media or
		 * from our in-memory cache.  In cases where the in-memory
		 * cache overrides the media the keyrefs will be the same
		 * anyway so we can avoid checking the cache when the media
		 * has a key.
		 */
		dummy.index = i;
		chain = RB_FIND(hammer2_chain_tree, &parent->rbhead, &dummy);
		if (chain) {
			/*
			 * NOTE! CHAIN_DELETED elements have to be adjusted
			 *	 too, they cannot be ignored.
			 */
			bref = &chain->bref;
		} else if (base && base[i].type) {
			bref = &base[i];
		} else {
			if (ichain->index < 0)
				ichain->index = i;
			continue;
		}

		/*
		 * Skip keys not in the chosen half (low or high), only bit
		 * (keybits - 1) needs to be compared but for safety we
		 * will compare all msb bits plus that bit again.
		 */
		if ((~(((hammer2_key_t)1 << keybits) - 1) &
		    (key ^ bref->key)) != 0) {
			continue;
		}

		/*
		 * This element is being moved from the parent, its slot
		 * is available for our new indirect block.
		 */
		if (ichain->index < 0)
			ichain->index = i;

		/*
		 * Load the new indirect block by acquiring or allocating
		 * the related chain entries, then simply move them to the
		 * new parent (ichain).  We cannot move chains which are
		 * undergoing flushing and will break out of the loop in
		 * that case.
		 *
		 * When adjusting the parent/child relationship we must
		 * set the MOVED bit but we do NOT update bref_flush
		 * because otherwise we might synchronize a bref that has
		 * not yet been flushed.  We depend on chain's bref_flush
		 * either being correct or the chain being in a MODIFIED
		 * state.
		 *
		 * We do not want to set MODIFIED here as this would result
		 * in unnecessary reallocations.
		 *
		 * We must still set SUBMODIFIED in the parent but we do
		 * that after the loop.
		 *
		 * WARNING! chain->cst.spin must be held when chain->parent is
		 *	    modified, even though we own the full blown lock,
		 *	    to deal with setsubmod and rename races.
		 */
		chain = hammer2_chain_get(hmp, parent, i,
					  HAMMER2_LOOKUP_NODATA);
		if (chain->flushing) {
			hammer2_chain_unlock(hmp, chain);
			break;
		}

		spin_lock(&chain->cst.spin);
		RB_REMOVE(hammer2_chain_tree, &parent->rbhead, chain);
		if (RB_INSERT(hammer2_chain_tree, &ichain->rbhead, chain))
			panic("hammer2_chain_create_indirect: collision");
		chain->parent = ichain;
		spin_unlock(&chain->cst.spin);

		if (base)
			bzero(&base[i], sizeof(base[i]));
		atomic_add_int(&parent->refs, -1);
		atomic_add_int(&ichain->refs, 1);
		if ((chain->flags & HAMMER2_CHAIN_MOVED) == 0) {
			hammer2_chain_ref(hmp, chain);
			atomic_set_int(&chain->flags, HAMMER2_CHAIN_MOVED);
		}
		hammer2_chain_unlock(hmp, chain);
		KKASSERT(parent->refs > 0);
		chain = NULL;
	}

	/*
	 * If we hit a chain that is undergoing flushing we're screwed and
	 * we have to duno the whole mess.  Since ichain has not been linked
	 * in yet, the moved chains are not reachable and will not have been
	 * disposed of.
	 *
	 * WARNING! This code is pretty hairy because the flusher is sitting
	 *	    on the parent processing one of the children that we
	 *	    haven't yet moved, and will do a RB_NEXT loop on that
	 *	    child.  So the children we're moving back have to be
	 *	    returned to the same place in the iteration that they
	 *	    were removed from.
	 */
	if (i != count) {
		kprintf("hammer2_chain_create_indirect: EAGAIN\n");
		*errorp = EAGAIN;
		while ((chain = RB_ROOT(&ichain->rbhead)) != NULL) {
			hammer2_chain_lock(hmp, chain, HAMMER2_RESOLVE_NEVER);
			KKASSERT(chain->flushing == 0);
			RB_REMOVE(hammer2_chain_tree, &ichain->rbhead, chain);
			if (RB_INSERT(hammer2_chain_tree, &parent->rbhead, chain))
				panic("hammer2_chain_create_indirect: collision");
			chain->parent = parent;
			atomic_add_int(&parent->refs, 1);
			atomic_add_int(&ichain->refs, -1);
			/* MOVED bit might have been inherited, cannot undo */
			hammer2_chain_unlock(hmp, chain);
		}
		hammer2_chain_free(hmp, ichain);
		return(NULL);
	}

	/*
	 * Insert the new indirect block into the parent now that we've
	 * cleared out some entries in the parent.  We calculated a good
	 * insertion index in the loop above (ichain->index).
	 *
	 * We don't have to set MOVED here because we mark ichain modified
	 * down below (so the normal modified -> flush -> set-moved sequence
	 * applies).
	 */
	KKASSERT(ichain->index >= 0);
	if (RB_INSERT(hammer2_chain_tree, &parent->rbhead, ichain))
		panic("hammer2_chain_create_indirect: ichain insertion");
	atomic_set_int(&ichain->flags, HAMMER2_CHAIN_ONRBTREE);
	ichain->parent = parent;
	atomic_add_int(&parent->refs, 1);

	/*
	 * Mark the new indirect block modified after insertion, which
	 * will propagate up through parent all the way to the root and
	 * also allocate the physical block in ichain for our caller,
	 * and assign ichain->data to a pre-zero'd space (because there
	 * is not prior data to copy into it).
	 *
	 * We have to set SUBMODIFIED in ichain's flags manually so the
	 * flusher knows it has to recurse through it to get to all of
	 * our moved blocks, then call setsubmod() to set the bit
	 * recursively.
	 */
	hammer2_chain_modify(hmp, ichain, HAMMER2_MODIFY_OPTDATA);
	hammer2_chain_parent_setsubmod(hmp, ichain);
	atomic_set_int(&ichain->flags, HAMMER2_CHAIN_SUBMODIFIED);

	/*
	 * Figure out what to return.
	 */
	if (create_bits > keybits) {
		/*
		 * Key being created is way outside the key range,
		 * return the original parent.
		 */
		hammer2_chain_unlock(hmp, ichain);
	} else if (~(((hammer2_key_t)1 << keybits) - 1) &
		   (create_key ^ key)) {
		/*
		 * Key being created is outside the key range,
		 * return the original parent.
		 */
		hammer2_chain_unlock(hmp, ichain);
	} else {
		/*
		 * Otherwise its in the range, return the new parent.
		 * (leave both the new and old parent locked).
		 */
		parent = ichain;
	}

	return(parent);
}

/*
 * Physically delete the specified chain element.  Note that inodes with
 * open descriptors should not be deleted (as with other filesystems) until
 * the last open descriptor is closed.
 *
 * This routine will remove the chain element from its parent and potentially
 * also recurse upward and delete indirect blocks which become empty as a
 * side effect.
 *
 * The caller must pass a pointer to the chain's parent, also locked and
 * referenced.  (*parentp) will be modified in a manner similar to a lookup
 * or iteration when indirect blocks are also deleted as a side effect.
 *
 * XXX This currently does not adhere to the MOVED flag protocol in that
 *     the removal is immediately indicated in the parent's blockref[]
 *     array.
 *
 * Must be called with an exclusively locked parent and chain.
 */
void
hammer2_chain_delete(hammer2_mount_t *hmp, hammer2_chain_t *parent,
		     hammer2_chain_t *chain, int retain)
{
	hammer2_blockref_t *base;
	hammer2_inode_t *ip;
	int count;

	if (chain->parent != parent)
		panic("hammer2_chain_delete: parent mismatch");
	KKASSERT(ccms_thread_lock_owned(&parent->cst));

	/*
	 * Mark the parent modified so our base[] pointer remains valid
	 * while we move entries.  For the optimized indirect block
	 * case mark the parent moved instead.
	 *
	 * Calculate the blockref reference in the parent
	 */
	switch(parent->bref.type) {
	case HAMMER2_BREF_TYPE_INODE:
		hammer2_chain_modify(hmp, parent, HAMMER2_MODIFY_NO_MODIFY_TID);
		base = &parent->data->ipdata.u.blockset.blockref[0];
		count = HAMMER2_SET_COUNT;
		break;
	case HAMMER2_BREF_TYPE_INDIRECT:
	case HAMMER2_BREF_TYPE_FREEMAP_ROOT:
	case HAMMER2_BREF_TYPE_FREEMAP_NODE:
		hammer2_chain_modify(hmp, parent, HAMMER2_MODIFY_OPTDATA |
						  HAMMER2_MODIFY_NO_MODIFY_TID);
		if (parent->flags & HAMMER2_CHAIN_INITIAL)
			base = NULL;
		else
			base = &parent->data->npdata.blockref[0];
		count = parent->bytes / sizeof(hammer2_blockref_t);
		break;
	case HAMMER2_BREF_TYPE_VOLUME:
		hammer2_chain_modify(hmp, parent, HAMMER2_MODIFY_NO_MODIFY_TID);
		base = &hmp->voldata.sroot_blockset.blockref[0];
		count = HAMMER2_SET_COUNT;
		break;
	default:
		panic("hammer2_chain_delete: unrecognized blockref type: %d",
		      parent->bref.type);
		count = 0;
		break;
	}
	KKASSERT(chain->index >= 0 && chain->index < count);

	/*
	 * We may not be able to immediately disconnect the chain if a
	 * flush is in progress.  If retain is non-zero we MUST disconnect
	 * the chain now and callers are responsible for making sure that
	 * flushing is zero.
	 */
	spin_lock(&chain->cst.spin);
	if ((retain || chain->flushing == 0) &&
	    (chain->flags & HAMMER2_CHAIN_ONRBTREE)) {
		if (base)
			bzero(&base[chain->index], sizeof(*base));
		KKASSERT(chain->flushing == 0);
		RB_REMOVE(hammer2_chain_tree, &parent->rbhead, chain);
		atomic_clear_int(&chain->flags, HAMMER2_CHAIN_ONRBTREE);
		atomic_add_int(&parent->refs, -1);   /* for red-black entry */
		chain->index = -1;
		chain->parent = NULL;
	}
	spin_unlock(&chain->cst.spin);

	/*
	 * Cumulative adjustments must be propagated to the parent inode
	 * when deleting and synchronized to ip.  This occurs even if we
	 * cannot detach the chain from its parent.
	 *
	 * NOTE:  We do not propagate ip->delta_*count to the parent because
	 *	  these represent adjustments that have not yet been
	 *	  propagated upward, so we don't need to remove them from
	 *	  the parent.
	 *
	 * Clear the pointer to the parent inode.
	 */
	if ((chain->flags & HAMMER2_CHAIN_DELETED) == 0 &&
	    chain->bref.type == HAMMER2_BREF_TYPE_INODE) {
		ip = chain->u.ip;
		if (ip->pip) {
			/* XXX SMP, pip chain not necessarily parent chain */
			ip->pip->delta_icount -= ip->ip_data.inode_count;
			ip->pip->delta_dcount -= ip->ip_data.data_count;
			ip->ip_data.inode_count += ip->delta_icount;
			ip->ip_data.data_count += ip->delta_dcount;
			ip->delta_icount = 0;
			ip->delta_dcount = 0;
			--ip->pip->delta_icount;
			spin_lock(&chain->cst.spin); /* XXX */
			ip->pip = NULL;
			spin_unlock(&chain->cst.spin);
		}
	}

	/*
	 * If retain is 0 the deletion is permanent.  Because the chain is
	 * no longer connected to the topology a flush will have no
	 * visibility into it.  We must dispose of the references related
	 * to the MODIFIED and MOVED flags, otherwise the ref count will
	 * never transition to 0.
	 *
	 * If retain is non-zero the deleted element is likely an inode
	 * which the vnops frontend will mark DESTROYED and flush.  In that
	 * situation we must retain the flags for any open file descriptors
	 * on the (removed) inode.  The final close will destroy the
	 * disconnected chain.
	 */
	if (retain == 0) {
		atomic_set_int(&chain->flags, HAMMER2_CHAIN_DELETED);
		if (chain->flags & HAMMER2_CHAIN_MODIFIED) {
			atomic_clear_int(&chain->flags, HAMMER2_CHAIN_MODIFIED);
			hammer2_chain_drop(hmp, chain);
		}
		if (chain->flags & HAMMER2_CHAIN_MOVED) {
			atomic_clear_int(&chain->flags, HAMMER2_CHAIN_MOVED);
			hammer2_chain_drop(hmp, chain);
		}
	}

	/*
	 * The chain is still likely referenced, possibly even by a vnode
	 * (if an inode), so defer further action until the chain gets
	 * dropped.
	 */
}

void
hammer2_chain_wait(hammer2_mount_t *hmp, hammer2_chain_t *chain)
{
	tsleep(chain, 0, "chnflw", 1);
}
