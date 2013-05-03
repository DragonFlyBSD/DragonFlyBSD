/*
 * Copyright (c) 2011-2013 The DragonFly Project.  All rights reserved.
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
 * This subsystem implements most of the core support functions for
 * the hammer2_chain and hammer2_chain_core structures.
 *
 * Chains represent the filesystem media topology in-memory.  Any given
 * chain can represent an inode, indirect block, data, or other types
 * of blocks.
 *
 * This module provides APIs for direct and indirect block searches,
 * iterations, recursions, creation, deletion, replication, and snapshot
 * views (used by the flush and snapshot code).
 *
 * Generally speaking any modification made to a chain must propagate all
 * the way back to the volume header, issuing copy-on-write updates to the
 * blockref tables all the way up.  Any chain except the volume header itself
 * can be flushed to disk at any time, in any order.  None of it matters
 * until we get to the point where we want to synchronize the volume header
 * (see the flush code).
 *
 * The chain structure supports snapshot views in time, which are primarily
 * used until the related data and meta-data is flushed to allow the
 * filesystem to make snapshots without requiring it to first flush,
 * and to allow the filesystem flush and modify the filesystem concurrently
 * with minimal or no stalls.
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
		hammer2_trans_t *trans, hammer2_chain_t *parent,
		hammer2_key_t key, int keybits, int *errorp);

/*
 * We use a red-black tree to guarantee safe lookups under shared locks.
 *
 * Chains can be overloaded onto the same index, creating a different
 * view of a blockref table based on a transaction id.  The RBTREE
 * deconflicts the view by sub-sorting on delete_tid.
 *
 * NOTE: Any 'current' chain which is not yet deleted will have a
 *	 delete_tid of HAMMER2_MAX_TID (0xFFF....FFFLLU).
 */
RB_GENERATE(hammer2_chain_tree, hammer2_chain, rbnode, hammer2_chain_cmp);

int
hammer2_chain_cmp(hammer2_chain_t *chain1, hammer2_chain_t *chain2)
{
	if (chain1->index < chain2->index)
		return(-1);
	if (chain1->index > chain2->index)
		return(1);
	if (chain1->delete_tid < chain2->delete_tid)
		return(-1);
	if (chain1->delete_tid > chain2->delete_tid)
		return(1);
	return(0);
}

/*
 * Flag chain->parent SUBMODIFIED recursively up to the root.  The
 * recursion can terminate when a parent is encountered with SUBMODIFIED
 * already set.  The flag is NOT set on the passed-in chain.
 *
 * This can be confusing because even though chains are multi-homed,
 * each chain has a specific idea of its parent (chain->parent) which
 * is singly-homed.
 *
 * This flag is used by the flusher's downward recursion to detect
 * modifications and can only be cleared bottom-up.
 *
 * The parent pointer is protected by all the modified children below it
 * and cannot be changed until they have all been flushed.  However, setsubmod
 * operations on new modifications can race flushes in progress, so we use
 * the chain->core->cst.spin lock to handle collisions.
 */
void
hammer2_chain_parent_setsubmod(hammer2_chain_t *chain)
{
	hammer2_chain_t *parent;
	hammer2_chain_core_t *core;

	while ((parent = chain->parent) != NULL) {
		core = parent->core;
		spin_lock(&core->cst.spin);
		/*
		 * XXX flush synchronization
		 */
		while (parent->duplink &&
		       (parent->flags & HAMMER2_CHAIN_DELETED)) {
			parent = parent->duplink;
		}
		if (parent->flags & HAMMER2_CHAIN_SUBMODIFIED) {
			spin_unlock(&core->cst.spin);
			break;
		}
		atomic_set_int(&parent->flags, HAMMER2_CHAIN_SUBMODIFIED);
		spin_unlock(&core->cst.spin);
		chain = parent;
	}
}

/*
 * Allocate a new disconnected chain element representing the specified
 * bref.  chain->refs is set to 1 and the passed bref is copied to
 * chain->bref.  chain->bytes is derived from the bref.
 *
 * chain->core is NOT allocated and the media data and bp pointers are left
 * NULL.  The caller must call chain_core_alloc() to allocate or associate
 * a core with the chain.
 *
 * NOTE: Returns a referenced but unlocked (because there is no core) chain.
 */
hammer2_chain_t *
hammer2_chain_alloc(hammer2_mount_t *hmp, hammer2_blockref_t *bref)
{
	hammer2_chain_t *chain;
	u_int bytes = 1U << (int)(bref->data_off & HAMMER2_OFF_MASK_RADIX);

	/*
	 * Construct the appropriate system structure.
	 */
	switch(bref->type) {
	case HAMMER2_BREF_TYPE_INODE:
	case HAMMER2_BREF_TYPE_INDIRECT:
	case HAMMER2_BREF_TYPE_FREEMAP_ROOT:
	case HAMMER2_BREF_TYPE_FREEMAP_NODE:
	case HAMMER2_BREF_TYPE_DATA:
	case HAMMER2_BREF_TYPE_FREEMAP_LEAF:
		chain = kmalloc(sizeof(*chain), hmp->mchain, M_WAITOK | M_ZERO);
		break;
	case HAMMER2_BREF_TYPE_VOLUME:
		chain = NULL;
		panic("hammer2_chain_alloc volume type illegal for op");
	default:
		chain = NULL;
		panic("hammer2_chain_alloc: unrecognized blockref type: %d",
		      bref->type);
	}

	chain->hmp = hmp;
	chain->bref = *bref;
	chain->index = -1;		/* not yet assigned */
	chain->bytes = bytes;
	chain->refs = 1;
	chain->flags = HAMMER2_CHAIN_ALLOCATED;
	chain->delete_tid = HAMMER2_MAX_TID;

	return (chain);
}

/*
 * Associate an existing core with the chain or allocate a new core.
 *
 * The core is not locked.  No additional refs on the chain are made.
 */
void
hammer2_chain_core_alloc(hammer2_chain_t *chain, hammer2_chain_core_t *core)
{
	KKASSERT(chain->core == NULL);

	if (core == NULL) {
		core = kmalloc(sizeof(*core), chain->hmp->mchain,
			       M_WAITOK | M_ZERO);
		RB_INIT(&core->rbtree);
		core->sharecnt = 1;
		chain->core = core;
		ccms_cst_init(&core->cst, chain);
	} else {
		atomic_add_int(&core->sharecnt, 1);
		chain->core = core;
	}
}

/*
 * Deallocate a chain after the caller has transitioned its refs to 0
 * and disassociated it from its parent.
 *
 * We must drop sharecnt on the core (if any) and handle its 1->0 transition
 * too.
 */
static void
hammer2_chain_dealloc(hammer2_chain_t *chain)
{
	hammer2_chain_core_t *core;

	/*
	 * Chain's flags are expected to be sane.
	 */
	KKASSERT((chain->flags & (HAMMER2_CHAIN_MOVED |
				  HAMMER2_CHAIN_MODIFIED |
				  HAMMER2_CHAIN_ONRBTREE)) == 0);
	KKASSERT(chain->duplink == NULL);

	/*
	 * Disconnect chain->core from chain and free core if it was the
	 * last core.  If any children are present in the core's rbtree
	 * they cannot have a pointer to our chain by definition because
	 * our chain's refs have dropped to 0.  If this is the last sharecnt
	 * on core, then core's rbtree must be empty by definition.
	 */
	if ((core = chain->core) != NULL) {
		/*
		 * Other chains may reference the same core so the core's
		 * spinlock is needed to safely disconnect it.
		 */
		spin_lock(&core->cst.spin);
		chain->core = NULL;
		if (atomic_fetchadd_int(&core->sharecnt, -1) == 1) {
			spin_unlock(&core->cst.spin);
			KKASSERT(RB_EMPTY(&core->rbtree));
			KKASSERT(core->cst.count == 0);
			KKASSERT(core->cst.upgrade == 0);
			kfree(core, chain->hmp->mchain);
		} else {
			spin_unlock(&core->cst.spin);
		}
		core = NULL;		/* safety */
	}

	/*
	 * Finally free the structure and return for possible recursion.
	 */
	hammer2_chain_free(chain);
}

/*
 * Free a disconnected chain element.
 */
void
hammer2_chain_free(hammer2_chain_t *chain)
{
	hammer2_mount_t *hmp = chain->hmp;

	switch(chain->bref.type) {
	case HAMMER2_BREF_TYPE_VOLUME:
		chain->data = NULL;
		break;
	case HAMMER2_BREF_TYPE_INODE:
		if (chain->data) {
			kfree(chain->data, hmp->minode);
			chain->data = NULL;
		}
		break;
	default:
		KKASSERT(chain->data == NULL);
		break;
	}

	KKASSERT(chain->core == NULL);
	KKASSERT(chain->bp == NULL);
	chain->hmp = NULL;

	if (chain->flags & HAMMER2_CHAIN_ALLOCATED)
		kfree(chain, hmp->mchain);
}

/*
 * Add a reference to a chain element, preventing its destruction.
 */
void
hammer2_chain_ref(hammer2_chain_t *chain)
{
	atomic_add_int(&chain->refs, 1);
}

/*
 * Drop the caller's reference to the chain.  When the ref count drops to
 * zero this function will disassociate the chain from its parent and
 * deallocate it, then recursely drop the parent using the implied ref
 * from the chain's chain->parent.
 *
 * WARNING! Just because we are able to deallocate a chain doesn't mean
 *	    that chain->core->rbtree is empty.  There can still be a sharecnt
 *	    on chain->core and RBTREE entries that refer to different parents.
 */
static hammer2_chain_t *hammer2_chain_lastdrop(hammer2_chain_t *chain);

void
hammer2_chain_drop(hammer2_chain_t *chain)
{
	u_int refs;
	u_int need = 0;

#if 1
	if (chain->flags & HAMMER2_CHAIN_MOVED)
		++need;
	if (chain->flags & HAMMER2_CHAIN_MODIFIED)
		++need;
	KKASSERT(chain->refs > need);
#endif

	while (chain) {
		refs = chain->refs;
		cpu_ccfence();
		KKASSERT(refs > 0);

		if (refs == 1) {
			if (chain->parent) {
				chain = hammer2_chain_lastdrop(chain);
				/* recursively drop parent or retry same */
			} else if (atomic_cmpset_int(&chain->refs, 1, 0)) {
				hammer2_chain_dealloc(chain);
				chain = NULL;
				/* no parent to recurse on */
			} else {
				/* retry the same chain */
			}
		} else {
			if (atomic_cmpset_int(&chain->refs, refs, refs - 1))
				break;
			/* retry the same chain */
		}
	}
}

/*
 * Safe handling of the 1->0 transition on chain when the chain has a
 * parent.
 *
 * NOTE: A chain can only be removed from its parent core's RBTREE on
 *	 the 1->0 transition by definition.  No other code is allowed
 *	 to remove chain from its RBTREE, so no race is possible.
 */
static
hammer2_chain_t *
hammer2_chain_lastdrop(hammer2_chain_t *chain)
{
	hammer2_chain_t *parent;
	hammer2_chain_t *tmp;
	hammer2_chain_core_t *parent_core;

	parent = chain->parent;
	parent_core = parent->core;
	KKASSERT(chain->flags & HAMMER2_CHAIN_ONRBTREE);

	spin_lock(&parent_core->cst.spin);
	if (atomic_cmpset_int(&chain->refs, 1, 0)) {
		RB_REMOVE(hammer2_chain_tree, &parent_core->rbtree, chain);
		atomic_clear_int(&chain->flags, HAMMER2_CHAIN_ONRBTREE);
		chain->parent = NULL;	/* NULL field, must drop implied ref */
		spin_unlock(&parent_core->cst.spin);
		if ((tmp = chain->duplink) != NULL) {
			chain->duplink = NULL;
			hammer2_chain_drop(tmp);
		}
		hammer2_chain_dealloc(chain);
		chain = parent;		/* recursively drop parent */
	} else {
		spin_unlock(&parent_core->cst.spin);
	}
	return (chain);
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
 *
 * WARNING! If data must be fetched a shared lock will temporarily be
 *	    upgraded to exclusive.  However, a deadlock can occur if
 *	    the caller owns more than one shared lock.
 */
int
hammer2_chain_lock(hammer2_chain_t *chain, int how)
{
	hammer2_mount_t *hmp;
	hammer2_chain_core_t *core;
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
	if ((how & HAMMER2_RESOLVE_NOREF) == 0)
		hammer2_chain_ref(chain);
	hmp = chain->hmp;
	KKASSERT(hmp != NULL);

	/*
	 * Get the appropriate lock.
	 */
	core = chain->core;
	if (how & HAMMER2_RESOLVE_SHARED)
		ccms_thread_lock(&core->cst, CCMS_STATE_SHARED);
	else
		ccms_thread_lock(&core->cst, CCMS_STATE_EXCLUSIVE);

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
	ostate = ccms_thread_lock_upgrade(&core->cst);
	if (chain->data) {
		ccms_thread_lock_downgrade(&core->cst, ostate);
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
		ccms_thread_lock_downgrade(&core->cst, ostate);
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
		KKASSERT(chain->bytes == sizeof(chain->data->ipdata));
		chain->data = kmalloc(sizeof(chain->data->ipdata),
				      hmp->minode, M_WAITOK | M_ZERO);
		bcopy(bdata, &chain->data->ipdata, chain->bytes);
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
	ccms_thread_lock_downgrade(&core->cst, ostate);
	return (0);
}

/*
 * Unlock and deref a chain element.
 *
 * On the last lock release any non-embedded data (chain->bp) will be
 * retired.
 */
void
hammer2_chain_unlock(hammer2_chain_t *chain)
{
	hammer2_chain_core_t *core = chain->core;
	long *counterp;

	/*
	 * Release the CST lock but with a special 1->0 transition case
	 * to also drop the refs on chain.  Multiple CST locks only
	 *
	 * Returns non-zero if lock references remain.  When zero is
	 * returned the last lock reference is retained and any shared
	 * lock is upgraded to an exclusive lock for final disposition.
	 */
	if (ccms_thread_unlock_zero(&core->cst)) {
		KKASSERT(chain->refs > 1);
		atomic_add_int(&chain->refs, -1);
		return;
	}

	/*
	 * Shortcut the case if the data is embedded or not resolved.
	 *
	 * Do NOT NULL out chain->data (e.g. inode data), it might be
	 * dirty.
	 *
	 * The DIRTYBP flag is non-applicable in this situation and can
	 * be cleared to keep the flags state clean.
	 */
	if (chain->bp == NULL) {
		atomic_clear_int(&chain->flags, HAMMER2_CHAIN_DIRTYBP);
		ccms_thread_unlock(&core->cst);
		hammer2_chain_drop(chain);
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
	ccms_thread_unlock(&core->cst);
	hammer2_chain_drop(chain);
}

/*
 * Resize the chain's physical storage allocation in-place.  This may
 * replace the passed-in chain with a new chain.
 *
 * Chains can be resized smaller without reallocating the storage.
 * Resizing larger will reallocate the storage.
 *
 * Must be passed an exclusively locked parent and chain, returns a new
 * exclusively locked chain at the same index and unlocks the old chain.
 * Flushes the buffer if necessary.
 *
 * This function is mostly used with DATA blocks locked RESOLVE_NEVER in order
 * to avoid instantiating a device buffer that conflicts with the vnode
 * data buffer.  That is, the passed-in bp is a logical buffer, whereas
 * any chain-oriented bp would be a device buffer.
 *
 * XXX flags currently ignored, uses chain->bp to detect data/no-data.
 * XXX return error if cannot resize.
 */
void
hammer2_chain_resize(hammer2_trans_t *trans, hammer2_inode_t *ip,
		     struct buf *bp,
		     hammer2_chain_t *parent, hammer2_chain_t **chainp,
		     int nradix, int flags)
{
	hammer2_mount_t *hmp = trans->hmp;
	hammer2_chain_t *chain = *chainp;
#if 0
	struct buf *nbp;
	char *bdata;
	int error;
#endif
	hammer2_off_t pbase;
	size_t obytes;
	size_t nbytes;
	size_t bbytes;
	int boff;

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
	 * Delete the old chain and duplicate it at the same (parent, index),
	 * returning a new chain.  This allows the old chain to still be
	 * used by the flush code.  Duplication occurs in-place.
	 *
	 * NOTE: If we are not crossing a synchronization point the
	 *	 duplication code will simply reuse the existing chain
	 *	 structure.
	 */
	hammer2_chain_delete(trans, parent, chain);
	hammer2_chain_duplicate(trans, parent, chain->index, &chain, NULL);

	/*
	 * Set MODIFIED and add a chain ref to prevent destruction.  Both
	 * modified flags share the same ref.  (duplicated chains do not
	 * start out MODIFIED unless possibly if the duplication code
	 * decided to reuse the existing chain as-is).
	 *
	 * If the chain is already marked MODIFIED then we can safely
	 * return the previous allocation to the pool without having to
	 * worry about snapshots.  XXX check flush synchronization.
	 */
	if ((chain->flags & HAMMER2_CHAIN_MODIFIED) == 0) {
		atomic_set_int(&ip->flags, HAMMER2_INODE_MODIFIED);
		atomic_set_int(&chain->flags, HAMMER2_CHAIN_MODIFIED);
		hammer2_chain_ref(chain);
	} else {
#if 0
		hammer2_freemap_free(hmp, chain->bref.data_off,
				     chain->bref.type);
#endif
	}

	/*
	 * Relocate the block, even if making it smaller (because different
	 * block sizes may be in different regions).
	 */
	chain->bref.data_off = hammer2_freemap_alloc(hmp, chain->bref.type,
						     nbytes);
	chain->bytes = nbytes;
	/*ip->delta_dcount += (ssize_t)(nbytes - obytes);*/ /* XXX atomic */

	/*
	 * The device buffer may be larger than the allocation size.
	 */
	if ((bbytes = chain->bytes) < HAMMER2_MINIOSIZE)
		bbytes = HAMMER2_MINIOSIZE;
	pbase = chain->bref.data_off & ~(hammer2_off_t)(bbytes - 1);
	boff = chain->bref.data_off & HAMMER2_OFF_MASK & (bbytes - 1);

	KKASSERT(chain->bp == NULL);
#if 0
	/*
	 * Only copy the data if resolved, otherwise the caller is
	 * responsible.
	 *
	 * XXX handle device-buffer resizing case too.  Right now we
	 *     only handle logical buffer resizing.
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

		/*
		 * chain->bp and chain->data represent the on-disk version
		 * of the data, where as the passed-in bp is usually a
		 * more up-to-date logical buffer.  However, there is no
		 * need to synchronize the more up-to-date data in (bp)
		 * as it will do that on its own when it flushes.
		 */
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
		hammer2_chain_modify(trans, chain, 0);
	}
#endif

	/*
	 * Make sure the chain is marked MOVED and SUBMOD is set in the
	 * parent(s) so the adjustments are picked up by flush.
	 */
	if ((chain->flags & HAMMER2_CHAIN_MOVED) == 0) {
		hammer2_chain_ref(chain);
		atomic_set_int(&chain->flags, HAMMER2_CHAIN_MOVED);
	}
	hammer2_chain_parent_setsubmod(chain);
	*chainp = chain;
}

/*
 * Convert a locked chain that was retrieved read-only to read-write,
 * duplicating it if necessary to satisfy active flush points.
 *
 * If not already marked modified a new physical block will be allocated
 * and assigned to the bref.
 *
 * If already modified and the new modification crosses a synchronization
 * point the chain is duplicated in order to allow the flush to synchronize
 * the old chain.  The new chain replaces the old.
 *
 * Non-data blocks - The chain should be locked to at least the RESOLVE_MAYBE
 *		     level or the COW operation will not work.
 *
 * Data blocks	   - The chain is usually locked RESOLVE_NEVER so as not to
 *		     run the data through the device buffers.
 *
 * This function may return a different chain than was passed, in which case
 * the old chain will be unlocked and the new chain will be locked.
 */
hammer2_inode_data_t *
hammer2_chain_modify_ip(hammer2_trans_t *trans, hammer2_inode_t *ip,
			     int flags)
{
#if 0
	hammer2_chain_t *ochain;

	ochain = ip->chain;
#endif
	hammer2_chain_modify(trans, ip->chain, flags);
#if 0
	if (ochain != ip->chain) {
		hammer2_chain_ref(ip->chain);
		hammer2_chain_drop(ochain);
	}
#endif
	return(&ip->chain->data->ipdata);
}

void
hammer2_chain_modify(hammer2_trans_t *trans, hammer2_chain_t *chain, int flags)
{
	hammer2_mount_t *hmp = trans->hmp;
	hammer2_off_t pbase;
	struct buf *nbp;
	int error;
	size_t bbytes;
	size_t boff;
	void *bdata;

	/*
	 * modify_tid is only update for primary modifications, not for
	 * propagated brefs.  mirror_tid will be updated regardless during
	 * the flush, no need to set it here.
	 */
	if ((flags & HAMMER2_MODIFY_NO_MODIFY_TID) == 0)
		chain->bref.modify_tid = trans->sync_tid;

	/*
	 * If the chain is already marked MODIFIED we can usually just
	 * return.
	 *
	 * WARNING!  It is possible that a prior lock/modify sequence
	 *	     retired the buffer.  During this lock/modify sequence
	 *	     MODIFIED may still be set but the buffer could wind up
	 *	     clean.  Since the caller is going to modify the buffer
	 *	     further we have to be sure that DIRTYBP is set again.
	 *
	 * WARNING!  Currently the caller is responsible for handling
	 *	     any delete/duplication roll of the chain to account
	 *	     for modifications crossing synchronization points.
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
	hammer2_chain_ref(chain);

	/*
	 * Adjust chain->modify_tid so the flusher knows when the
	 * modification occurred.
	 */
	chain->modify_tid = trans->sync_tid;

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
		hammer2_chain_parent_setsubmod(chain);
}

/*
 * Mark the volume as having been modified.  This short-cut version
 * does not have to lock the volume's chain, which allows the ioctl
 * code to make adjustments to connections without deadlocking.  XXX
 *
 * No ref is made on vchain when flagging it MODIFIED.
 */
void
hammer2_modify_volume(hammer2_mount_t *hmp)
{
	hammer2_voldata_lock(hmp);
	hammer2_voldata_unlock(hmp, 1);
}

/*
 * Locate an in-memory chain.  The parent must be locked.  The in-memory
 * chain is returned with a reference and without a lock, or NULL
 * if not found.
 *
 * This function returns the chain at the specified index with the highest
 * delete_tid.  The caller must check whether the chain is flagged
 * CHAIN_DELETED or not.
 *
 * NOTE: If no chain is found the caller usually must check the on-media
 *	 array to determine if a blockref exists at the index.
 */
struct hammer2_chain_find_info {
	hammer2_chain_t *best;
	hammer2_tid_t	delete_tid;
	int index;
};

static
int
hammer2_chain_find_cmp(hammer2_chain_t *child, void *data)
{
	struct hammer2_chain_find_info *info = data;

	if (child->index < info->index)
		return(-1);
	if (child->index > info->index)
		return(1);
	return(0);
}

static
int
hammer2_chain_find_callback(hammer2_chain_t *child, void *data)
{
	struct hammer2_chain_find_info *info = data;

	if (info->delete_tid < child->delete_tid) {
		info->delete_tid = child->delete_tid;
		info->best = child;
	}
	return(0);
}

static
hammer2_chain_t *
hammer2_chain_find_locked(hammer2_chain_t *parent, int index)
{
	struct hammer2_chain_find_info info;

	info.index = index;
	info.delete_tid = 0;
	info.best = NULL;

	RB_SCAN(hammer2_chain_tree, &parent->core->rbtree,
		hammer2_chain_find_cmp, hammer2_chain_find_callback,
		&info);

	return (info.best);
}

hammer2_chain_t *
hammer2_chain_find(hammer2_chain_t *parent, int index)
{
	hammer2_chain_t *chain;

	spin_lock(&parent->core->cst.spin);
	chain = hammer2_chain_find_locked(parent, index);
	if (chain)
		hammer2_chain_ref(chain);
	spin_unlock(&parent->core->cst.spin);

	return (chain);
}

/*
 * Return a locked chain structure with all associated data acquired.
 * (if LOOKUP_NOLOCK is requested the returned chain is only referenced).
 *
 * Caller must hold the parent locked shared or exclusive since we may
 * need the parent's bref array to find our block.
 *
 * The returned child is locked as requested.  If NOLOCK, the returned
 * child is still at least referenced.
 */
hammer2_chain_t *
hammer2_chain_get(hammer2_chain_t *parent, int index, int flags)
{
	hammer2_blockref_t *bref;
	hammer2_mount_t *hmp = parent->hmp;
	hammer2_chain_t *chain;
	hammer2_chain_t dummy;
	int how;

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

retry:
	/*
	 * First see if we have a (possibly modified) chain element cached
	 * for this (parent, index).  Acquire the data if necessary.
	 *
	 * If chain->data is non-NULL the chain should already be marked
	 * modified.
	 */
	dummy.flags = 0;
	dummy.index = index;
	dummy.delete_tid = HAMMER2_MAX_TID;
	spin_lock(&parent->core->cst.spin);
	chain = RB_FIND(hammer2_chain_tree, &parent->core->rbtree, &dummy);
	if (chain) {
		hammer2_chain_ref(chain);
		spin_unlock(&parent->core->cst.spin);
		if ((flags & HAMMER2_LOOKUP_NOLOCK) == 0)
			hammer2_chain_lock(chain, how | HAMMER2_RESOLVE_NOREF);
		return(chain);
	}
	spin_unlock(&parent->core->cst.spin);

	/*
	 * The parent chain must not be in the INITIAL state.
	 */
	if (parent->flags & HAMMER2_CHAIN_INITIAL) {
		panic("hammer2_chain_get: Missing bref(1)");
		/* NOT REACHED */
	}

	/*
	 * No RBTREE entry found, lookup the bref and issue I/O (switch on
	 * the parent's bref to determine where and how big the array is).
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
	 * entry.  Resulting chain has one ref and is not locked.
	 *
	 * The locking operation we do later will issue I/O to read it.
	 */
	chain = hammer2_chain_alloc(hmp, bref);
	hammer2_chain_core_alloc(chain, NULL);	/* ref'd chain returned */

	/*
	 * Link the chain into its parent.  A spinlock is required to safely
	 * access the RBTREE, and it is possible to collide with another
	 * hammer2_chain_get() operation because the caller might only hold
	 * a shared lock on the parent.
	 */
	KKASSERT(parent->refs > 0);
	spin_lock(&parent->core->cst.spin);
	chain->parent = parent;
	chain->index = index;
	if (RB_INSERT(hammer2_chain_tree, &parent->core->rbtree, chain)) {
		chain->parent = NULL;
		chain->index = -1;
		spin_unlock(&parent->core->cst.spin);
		hammer2_chain_drop(chain);
		goto retry;
	}
	atomic_set_int(&chain->flags, HAMMER2_CHAIN_ONRBTREE);
	hammer2_chain_ref(parent);		/* chain->parent ref */
	spin_unlock(&parent->core->cst.spin);

	/*
	 * Our new chain is referenced but NOT locked.  Lock the chain
	 * below.  The locking operation also resolves its data.
	 *
	 * If NOLOCK is set the release will release the one-and-only lock.
	 */
	if ((flags & HAMMER2_LOOKUP_NOLOCK) == 0) {
		hammer2_chain_lock(chain, how);	/* recusive lock */
		hammer2_chain_drop(chain);	/* excess ref */
	}
	return (chain);
}

/*
 * Lookup initialization/completion API
 */
hammer2_chain_t *
hammer2_chain_lookup_init(hammer2_chain_t *parent, int flags)
{
	if (flags & HAMMER2_LOOKUP_SHARED) {
		hammer2_chain_lock(parent, HAMMER2_RESOLVE_ALWAYS |
					   HAMMER2_RESOLVE_SHARED);
	} else {
		hammer2_chain_lock(parent, HAMMER2_RESOLVE_ALWAYS);
	}
	return (parent);
}

void
hammer2_chain_lookup_done(hammer2_chain_t *parent)
{
	if (parent)
		hammer2_chain_unlock(parent);
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
 * The matching chain will be returned exclusively locked.  If NOLOCK is
 * requested the chain will be returned only referenced.
 *
 * NULL is returned if no match was found, but (*parentp) will still
 * potentially be adjusted.
 *
 * This function will also recurse up the chain if the key is not within the
 * current parent's range.  (*parentp) can never be set to NULL.  An iteration
 * can simply allow (*parentp) to float inside the loop.
 */
hammer2_chain_t *
hammer2_chain_lookup(hammer2_chain_t **parentp,
		     hammer2_key_t key_beg, hammer2_key_t key_end,
		     int flags)
{
	hammer2_mount_t *hmp;
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
	hmp = parent->hmp;

	while (parent->bref.type == HAMMER2_BREF_TYPE_INDIRECT ||
	       parent->bref.type == HAMMER2_BREF_TYPE_FREEMAP_NODE) {
		scan_beg = parent->bref.key;
		scan_end = scan_beg +
			   ((hammer2_key_t)1 << parent->bref.keybits) - 1;
		if (key_beg >= scan_beg && key_end <= scan_end)
			break;
		/*
		 * XXX flush synchronization
		 */
		tmp = parent->parent;
		while (tmp->duplink &&
		       (tmp->flags & HAMMER2_CHAIN_DELETED)) {
			tmp = tmp->duplink;
		}
		hammer2_chain_ref(tmp);		/* ref new parent */
		hammer2_chain_unlock(parent);	/* unlock old parent */
						/* lock new parent */
		hammer2_chain_lock(tmp, how_maybe |
				        HAMMER2_RESOLVE_NOREF);
		*parentp = parent = tmp;		/* new parent */
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
				hammer2_chain_ref(parent);
			else
				hammer2_chain_lock(parent, how_always);
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
	 * NOTE! Deleted elements are effectively invisible.  Deletions
	 *	 proactively clear the parent bref to the deleted child
	 *	 so we do not try to shadow here to avoid parent updates
	 *	 (which would be difficult since multiple deleted elements
	 *	 might represent different flush synchronization points).
	 */
	bref = NULL;
	for (i = 0; i < count; ++i) {
		tmp = hammer2_chain_find(parent, i);
		if (tmp) {
			if (tmp->flags & HAMMER2_CHAIN_DELETED) {
				hammer2_chain_drop(tmp);
				continue;
			}
			bref = &tmp->bref;
			KKASSERT(bref->type != 0);
		} else if (base == NULL || base[i].type == 0) {
			continue;
		} else {
			bref = &base[i];
		}
		scan_beg = bref->key;
		scan_end = scan_beg + ((hammer2_key_t)1 << bref->keybits) - 1;
		if (tmp)
			hammer2_chain_drop(tmp);
		if (key_beg <= scan_end && key_end >= scan_beg)
			break;
	}
	if (i == count) {
		if (key_beg == key_end)
			return (NULL);
		return (hammer2_chain_next(parentp, NULL,
					   key_beg, key_end, flags));
	}

	/*
	 * Acquire the new chain element.  If the chain element is an
	 * indirect block we must search recursively.
	 *
	 * It is possible for the tmp chain above to be removed from
	 * the RBTREE but the parent lock ensures it would not have been
	 * destroyed from the media, so the chain_get() code will simply
	 * reload it from the media in that case.
	 */
	chain = hammer2_chain_get(parent, i, flags);
	if (chain == NULL)
		return (NULL);

	/*
	 * If the chain element is an indirect block it becomes the new
	 * parent and we loop on it.
	 *
	 * The parent always has to be locked with at least RESOLVE_MAYBE
	 * so we can access its data.  It might need a fixup if the caller
	 * passed incompatible flags.  Be careful not to cause a deadlock
	 * as a data-load requires an exclusive lock.
	 */
	if (chain->bref.type == HAMMER2_BREF_TYPE_INDIRECT ||
	    chain->bref.type == HAMMER2_BREF_TYPE_FREEMAP_NODE) {
		hammer2_chain_unlock(parent);
		*parentp = parent = chain;
		if (flags & HAMMER2_LOOKUP_NOLOCK) {
			hammer2_chain_lock(chain, how_maybe |
						  HAMMER2_RESOLVE_NOREF);
		} else if ((flags & HAMMER2_LOOKUP_NODATA) &&
			   chain->data == NULL) {
			hammer2_chain_ref(chain);
			hammer2_chain_unlock(chain);
			hammer2_chain_lock(chain, how_maybe |
						  HAMMER2_RESOLVE_NOREF);
		}
		goto again;
	}

	/*
	 * All done, return the chain
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
 * lock status must match flags.  Chain is always at least referenced.
 */
hammer2_chain_t *
hammer2_chain_next(hammer2_chain_t **parentp, hammer2_chain_t *chain,
		   hammer2_key_t key_beg, hammer2_key_t key_end,
		   int flags)
{
	hammer2_mount_t *hmp;
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
	hmp = parent->hmp;

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
			hammer2_chain_drop(chain);
		else
			hammer2_chain_unlock(chain);

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
		scan_beg = parent->bref.key;
		scan_end = scan_beg +
			    ((hammer2_key_t)1 << parent->bref.keybits) - 1;
		if (key_beg >= scan_beg && key_end <= scan_end)
			return (NULL);

		i = parent->index + 1;
		/*
		 * XXX flush synchronization
		 */
		tmp = parent->parent;
		while (tmp->duplink &&
		       (tmp->flags & HAMMER2_CHAIN_DELETED)) {
			tmp = tmp->duplink;
		}
		hammer2_chain_ref(tmp);		/* ref new parent */
		hammer2_chain_unlock(parent);	/* unlock old parent */
						/* lock new parent */
		hammer2_chain_lock(tmp, how_maybe |
				        HAMMER2_RESOLVE_NOREF);
		*parentp = parent = tmp;
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
	 * NOTE! Deleted elements are effectively invisible.  Deletions
	 *	 proactively clear the parent bref to the deleted child
	 *	 so we do not try to shadow here to avoid parent updates
	 *	 (which would be difficult since multiple deleted elements
	 *	 might represent different flush synchronization points).
	 */
	bref = NULL;
	while (i < count) {
		tmp = hammer2_chain_find(parent, i);
		if (tmp) {
			if (tmp->flags & HAMMER2_CHAIN_DELETED) {
				hammer2_chain_drop(tmp);
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
		if (tmp)
			hammer2_chain_drop(tmp);
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
	chain = hammer2_chain_get(parent, i, flags);
	if (chain == NULL)
		return (NULL);

	/*
	 * If the chain element is an indirect block it becomes the new
	 * parent and we loop on it.
	 *
	 * The parent always has to be locked with at least RESOLVE_MAYBE
	 * so we can access its data.  It might need a fixup if the caller
	 * passed incompatible flags.  Be careful not to cause a deadlock
	 * as a data-load requires an exclusive lock.
	 */
	if (chain->bref.type == HAMMER2_BREF_TYPE_INDIRECT ||
	    chain->bref.type == HAMMER2_BREF_TYPE_FREEMAP_NODE) {
		hammer2_chain_unlock(parent);
		*parentp = parent = chain;
		chain = NULL;
		if (flags & HAMMER2_LOOKUP_NOLOCK) {
			hammer2_chain_lock(parent, how_maybe |
						   HAMMER2_RESOLVE_NOREF);
		} else if ((flags & HAMMER2_LOOKUP_NODATA) &&
			   parent->data == NULL) {
			hammer2_chain_ref(parent);
			hammer2_chain_unlock(parent);
			hammer2_chain_lock(parent, how_maybe |
						   HAMMER2_RESOLVE_NOREF);
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
 * (*chainp) is either NULL, a newly allocated chain, or a chain allocated
 * via hammer2_chain_duplicate().  When not NULL, the passed-in chain must
 * NOT be attached to any parent, and will be attached by this function.
 * This mechanic is used by the rename code.
 *
 * Non-indirect types will automatically allocate indirect blocks as required
 * if the new item does not fit in the current (parent).
 *
 * Indirect types will move a portion of the existing blockref array in
 * (parent) into the new indirect type and then use one of the free slots
 * to emplace the new indirect type.
 *
 * A new locked chain element is returned of the specified type.  The
 * element may or may not have a data area associated with it:
 *
 *	VOLUME		not allowed here
 *	INODE		kmalloc()'d data area is set up
 *	INDIRECT	not allowed here
 *	DATA		no data area will be set-up (caller is expected
 *			to have logical buffers, we don't want to alias
 *			the data onto device buffers!).
 *
 * Requires an exclusively locked parent.
 */
int
hammer2_chain_create(hammer2_trans_t *trans, hammer2_chain_t **parentp,
		     hammer2_chain_t **chainp,
		     hammer2_key_t key, int keybits, int type, size_t bytes)
{
	hammer2_mount_t *hmp;
	hammer2_chain_t *chain;
	hammer2_chain_t *child;
	hammer2_chain_t *parent = *parentp;
	hammer2_blockref_t dummy;
	hammer2_blockref_t *base;
	int allocated = 0;
	int error = 0;
	int count;
	int i;

	KKASSERT(ccms_thread_lock_owned(&parent->core->cst));
	hmp = parent->hmp;
	chain = *chainp;

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
		hammer2_chain_core_alloc(chain, NULL);
		ccms_thread_lock(&chain->core->cst, CCMS_STATE_EXCLUSIVE);
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
			chain->data = kmalloc(sizeof(chain->data->ipdata),
					      hmp->minode, M_WAITOK | M_ZERO);
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
		KKASSERT((parent->data->ipdata.op_flags &
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
	 *
	 * We don't have to hold the spinlock to save an empty slot as
	 * new slots can only transition from empty if the parent is
	 * locked exclusively.
	 */

	spin_lock(&parent->core->cst.spin);
	for (i = 0; i < count; ++i) {
		child = hammer2_chain_find_locked(parent, i);
		if (child) {
			if (child->flags & HAMMER2_CHAIN_DELETED)
				break;
			continue;
		}
		if (base == NULL)
			break;
		if (base[i].type == 0)
			break;
	}
	spin_unlock(&parent->core->cst.spin);

	/*
	 * If no free blockref could be found we must create an indirect
	 * block and move a number of blockrefs into it.  With the parent
	 * locked we can safely lock each child in order to move it without
	 * causing a deadlock.
	 *
	 * This may return the new indirect block or the old parent depending
	 * on where the key falls.  NULL is returned on error.
	 */
	if (i == count) {
		hammer2_chain_t *nparent;

		nparent = hammer2_chain_create_indirect(trans, parent,
							key, keybits,
							&error);
		if (nparent == NULL) {
			if (allocated)
				hammer2_chain_free(chain);
			chain = NULL;
			goto done;
		}
		if (parent != nparent) {
			hammer2_chain_unlock(parent);
			parent = *parentp = nparent;
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
	KKASSERT((chain->flags & HAMMER2_CHAIN_DELETED) == 0);

	chain->parent = parent;
	chain->index = i;
	KKASSERT(parent->refs > 0);
	spin_lock(&parent->core->cst.spin);
	if (RB_INSERT(hammer2_chain_tree, &parent->core->rbtree, chain))
		panic("hammer2_chain_link: collision");
	atomic_set_int(&chain->flags, HAMMER2_CHAIN_ONRBTREE);
	hammer2_chain_ref(parent);		/* chain->parent ref */
	spin_unlock(&parent->core->cst.spin);

	/*
	 * (allocated) indicates that this is a newly-created chain element
	 * rather than a renamed chain element.
	 *
	 * In this situation we want to place the chain element in
	 * the MODIFIED state.  The caller expects it to NOT be in the
	 * INITIAL state.
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
			hammer2_chain_modify(trans, chain,
					     HAMMER2_MODIFY_OPTDATA);
			break;
		case HAMMER2_BREF_TYPE_INDIRECT:
		case HAMMER2_BREF_TYPE_FREEMAP_ROOT:
		case HAMMER2_BREF_TYPE_FREEMAP_NODE:
			/* not supported in this function */
			panic("hammer2_chain_create: bad type");
			atomic_set_int(&chain->flags, HAMMER2_CHAIN_INITIAL);
			hammer2_chain_modify(trans, chain,
					     HAMMER2_MODIFY_OPTDATA);
			break;
		default:
			hammer2_chain_modify(trans, chain, 0);
			break;
		}
	} else {
		/*
		 * When reconnecting a chain we must set MOVED and setsubmod
		 * so the flush recognizes that it must update the bref in
		 * the parent.
		 */
		if ((chain->flags & HAMMER2_CHAIN_MOVED) == 0) {
			hammer2_chain_ref(chain);
			atomic_set_int(&chain->flags, HAMMER2_CHAIN_MOVED);
		}
		hammer2_chain_parent_setsubmod(chain);
	}

done:
	*chainp = chain;

	return (error);
}

/*
 * Replace (*chainp) with a duplicate.  The original *chainp is unlocked
 * and the replacement will be returned locked.  Both the original and the
 * new chain will share the same RBTREE (have the same chain->core), with
 * the new chain becoming the 'current' chain (meaning it is the first in
 * the linked list at core->chain_first).
 *
 * If (parent, i) then the new duplicated chain is inserted under the parent
 * at the specified index (the parent must not have a ref at that index).
 *
 * If (NULL, -1) then the new duplicated chain is not inserted anywhere,
 * similar to if it had just been chain_alloc()'d (suitable for passing into
 * hammer2_chain_create() after this function returns).
 *
 * NOTE! Duplication is used in order to retain the original topology to
 *	 support flush synchronization points.  Both the original and the
 *	 new chain will have the same transaction id and thus the operation
 *	 appears atomic on the media.
 */
void
hammer2_chain_duplicate(hammer2_trans_t *trans, hammer2_chain_t *parent, int i,
			hammer2_chain_t **chainp, hammer2_blockref_t *bref)
{
	hammer2_mount_t *hmp = trans->hmp;
	hammer2_blockref_t *base;
	hammer2_chain_t *ochain;
	hammer2_chain_t *nchain;
	hammer2_chain_t *scan;
	size_t bytes;
	int count;

	/*
	 * First create a duplicate of the chain structure, associating
	 * it with the same core, making it the same size, pointing it
	 * to the same bref (the same media block), and copying any inline
	 * data.
	 */
	ochain = *chainp;
	if (bref == NULL)
		bref = &ochain->bref;
	nchain = hammer2_chain_alloc(hmp, bref);
	hammer2_chain_core_alloc(nchain, ochain->core);

	bytes = (hammer2_off_t)1 <<
		(int)(bref->data_off & HAMMER2_OFF_MASK_RADIX);
	nchain->bytes = bytes;

	/*
	 * Be sure to copy the INITIAL flag as well or we could end up
	 * loading garbage from the bref.
	 */
	if (ochain->flags & HAMMER2_CHAIN_INITIAL)
		atomic_set_int(&nchain->flags, HAMMER2_CHAIN_INITIAL);

	/*
	 * If the old chain is modified the new one must be too,
	 * but we only want to allocate a new bref.
	 */
	if (ochain->flags & HAMMER2_CHAIN_MODIFIED) {
		/*
		 * When duplicating chains the MODIFIED state is inherited.
		 * A new bref typically must be allocated.  However, file
		 * data chains may already have the data offset assigned
		 * to a logical buffer cache buffer so we absolutely cannot
		 * allocate a new bref here for TYPE_DATA.
		 *
		 * Basically the flusher core only dumps media topology
		 * and meta-data, not file data.  The VOP_FSYNC code deals
		 * with the file data.  XXX need back-pointer to inode.
		 */
		if (nchain->bref.type == HAMMER2_BREF_TYPE_DATA) {
			atomic_set_int(&nchain->flags, HAMMER2_CHAIN_MODIFIED);
			hammer2_chain_ref(nchain);
		} else {
			hammer2_chain_modify(trans, nchain,
					     HAMMER2_MODIFY_OPTDATA);
		}
	} else if (nchain->flags & HAMMER2_CHAIN_INITIAL) {
		/*
		 * When duplicating chains in the INITITAL state we need
		 * to ensure that the chain is marked modified so a
		 * block is properly assigned to it, otherwise the MOVED
		 * bit won't do the right thing.
		 */
		KKASSERT (nchain->bref.type != HAMMER2_BREF_TYPE_DATA);
		hammer2_chain_modify(trans, nchain, HAMMER2_MODIFY_OPTDATA);
	}
	if (parent || (ochain->flags & HAMMER2_CHAIN_MOVED)) {
		atomic_set_int(&nchain->flags, HAMMER2_CHAIN_MOVED);
		hammer2_chain_ref(nchain);
	}
	atomic_set_int(&nchain->flags, HAMMER2_CHAIN_SUBMODIFIED);

	switch(nchain->bref.type) {
	case HAMMER2_BREF_TYPE_VOLUME:
		panic("hammer2_chain_duplicate: cannot be called w/volhdr");
		break;
	case HAMMER2_BREF_TYPE_INODE:
		KKASSERT(bytes == HAMMER2_INODE_BYTES);
		if (ochain->data) {
			nchain->data = kmalloc(sizeof(nchain->data->ipdata),
					      hmp->minode, M_WAITOK | M_ZERO);
			nchain->data->ipdata = ochain->data->ipdata;
		}
		break;
	case HAMMER2_BREF_TYPE_INDIRECT:
		if ((nchain->flags & HAMMER2_CHAIN_MODIFIED) &&
		    nchain->data) {
			bcopy(ochain->data, nchain->data,
			      nchain->bytes);
		}
		break;
	case HAMMER2_BREF_TYPE_FREEMAP_ROOT:
	case HAMMER2_BREF_TYPE_FREEMAP_NODE:
		panic("hammer2_chain_duplicate: cannot be used to"
		      "create a freemap root or node");
		break;
	case HAMMER2_BREF_TYPE_FREEMAP_LEAF:
	case HAMMER2_BREF_TYPE_DATA:
	default:
		if ((nchain->flags & HAMMER2_CHAIN_MODIFIED) &&
		    nchain->data) {
			bcopy(ochain->data, nchain->data,
			      nchain->bytes);
		}
		/* leave chain->data NULL */
		KKASSERT(nchain->data == NULL);
		break;
	}

	/*
	 * Both chains must be locked for us to be able to set the
	 * duplink.  The caller may expect valid data.
	 *
	 * Unmodified duplicated blocks may have the same bref, we
	 * must be careful to avoid buffer cache deadlocks so we
	 * unlock the old chain before resolving the new one.
	 *
	 * Insert nchain at the end of the duplication list.
	 */
	hammer2_chain_lock(nchain, HAMMER2_RESOLVE_NEVER);
	/* extra ref still present from original allocation */

	spin_lock(&ochain->core->cst.spin);
	KKASSERT(nchain->duplink == NULL);
	nchain->duplink = ochain->duplink;
	ochain->duplink = nchain;	/* inherits excess ref from alloc */
	spin_unlock(&ochain->core->cst.spin);

	hammer2_chain_unlock(ochain);
	*chainp = nchain;
	hammer2_chain_lock(nchain, HAMMER2_RESOLVE_MAYBE);
	hammer2_chain_unlock(nchain);

	/*
	 * If parent is not NULL, insert into the parent at the requested
	 * index.  The newly duplicated chain must be marked MOVED and
	 * SUBMODIFIED set in its parent(s).
	 */
	if (parent) {
		/*
		 * Locate a free blockref in the parent's array
		 */
		KKASSERT(ccms_thread_lock_owned(&parent->core->cst));
		switch(parent->bref.type) {
		case HAMMER2_BREF_TYPE_INODE:
			KKASSERT((parent->data->ipdata.op_flags &
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
			panic("hammer2_chain_create: unrecognized "
			      "blockref type: %d",
			      parent->bref.type);
			count = 0;
			break;
		}
		KKASSERT(i >= 0 && i < count);

		nchain->parent = parent;
		nchain->index = i;
		KKASSERT((nchain->flags & HAMMER2_CHAIN_DELETED) == 0);
		KKASSERT(parent->refs > 0);

		spin_lock(&parent->core->cst.spin);
		scan = hammer2_chain_find_locked(parent, i);
		KKASSERT(base == NULL || base[i].type == 0 ||
			 scan == NULL ||
			 (scan->flags & HAMMER2_CHAIN_DELETED));
		if (RB_INSERT(hammer2_chain_tree, &parent->core->rbtree,
			      nchain)) {
			panic("hammer2_chain_link: collision");
		}
		atomic_set_int(&nchain->flags, HAMMER2_CHAIN_ONRBTREE);
		hammer2_chain_ref(parent);	/* nchain->parent ref */
		spin_unlock(&parent->core->cst.spin);

		if ((nchain->flags & HAMMER2_CHAIN_MOVED) == 0) {
			hammer2_chain_ref(nchain);
			atomic_set_int(&nchain->flags, HAMMER2_CHAIN_MOVED);
		}
		hammer2_chain_parent_setsubmod(nchain);
	}
}

/*
 * Create an indirect block that covers one or more of the elements in the
 * current parent.  Either returns the existing parent with no locking or
 * ref changes or returns the new indirect block locked and referenced
 * and leaving the original parent lock/ref intact as well.
 *
 * If an error occurs, NULL is returned and *errorp is set to the error.
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
hammer2_chain_create_indirect(hammer2_trans_t *trans, hammer2_chain_t *parent,
			      hammer2_key_t create_key, int create_bits,
			      int *errorp)
{
	hammer2_mount_t *hmp = trans->hmp;
	hammer2_blockref_t *base;
	hammer2_blockref_t *bref;
	hammer2_chain_t *chain;
	hammer2_chain_t *child;
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
	KKASSERT(ccms_thread_lock_owned(&parent->core->cst));
	*errorp = 0;

	/*hammer2_chain_modify(trans, parent, HAMMER2_MODIFY_OPTDATA);*/
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
	 *
	 * Deleted elements are ignored.
	 */
	bzero(&dummy, sizeof(dummy));
	dummy.delete_tid = HAMMER2_MAX_TID;

	spin_lock(&parent->core->cst.spin);
	for (i = 0; i < count; ++i) {
		int nkeybits;

		child = hammer2_chain_find_locked(parent, i);
		if (child) {
			if (child->flags & HAMMER2_CHAIN_DELETED)
				continue;
			bref = &child->bref;
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
		if (nkeybits < bref->keybits) {
			if (bref->keybits > 64) {
				kprintf("bad bref index %d chain %p bref %p\n", i, chain, bref);
				Debugger("fubar");
			}
			nkeybits = bref->keybits;
		}
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
	spin_unlock(&parent->core->cst.spin);
	bref = NULL;	/* now invalid (safety) */

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
	hammer2_chain_core_alloc(ichain, NULL);
	hammer2_chain_lock(ichain, HAMMER2_RESOLVE_MAYBE);
	hammer2_chain_drop(ichain);	/* excess ref from alloc */

	/*
	 * We have to mark it modified to allocate its block, but use
	 * OPTDATA to allow it to remain in the INITIAL state.  Otherwise
	 * it won't be acted upon by the flush code.
	 */
	hammer2_chain_modify(trans, ichain, HAMMER2_MODIFY_OPTDATA);

	/*
	 * Iterate the original parent and move the matching brefs into
	 * the new indirect block.
	 *
	 * XXX handle flushes.
	 */
	spin_lock(&parent->core->cst.spin);
	for (i = 0; i < count; ++i) {
		/*
		 * For keying purposes access the bref from the media or
		 * from our in-memory cache.  In cases where the in-memory
		 * cache overrides the media the keyrefs will be the same
		 * anyway so we can avoid checking the cache when the media
		 * has a key.
		 */
		child = hammer2_chain_find_locked(parent, i);
		if (child) {
			if (child->flags & HAMMER2_CHAIN_DELETED) {
				if (ichain->index < 0)
					ichain->index = i;
				continue;
			}
			bref = &child->bref;
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
		 * the related chain entries, then move them to the new
		 * parent (ichain) by deleting them from their old location
		 * and inserting a duplicate of the chain and any modified
		 * sub-chain in the new location.
		 *
		 * We must set MOVED in the chain being duplicated and
		 * SUBMODIFIED in the parent(s) so the flush code knows
		 * what is going on.  The latter is done after the loop.
		 *
		 * WARNING! chain->cst.spin must be held when chain->parent is
		 *	    modified, even though we own the full blown lock,
		 *	    to deal with setsubmod and rename races.
		 *	    (XXX remove this req).
		 */
		spin_unlock(&parent->core->cst.spin);
		chain = hammer2_chain_get(parent, i, HAMMER2_LOOKUP_NODATA);
		hammer2_chain_delete(trans, parent, chain);
		hammer2_chain_duplicate(trans, ichain, i, &chain, NULL);

		hammer2_chain_unlock(chain);
		KKASSERT(parent->refs > 0);
		chain = NULL;
		spin_lock(&parent->core->cst.spin);
	}
	spin_unlock(&parent->core->cst.spin);

	/*
	 * Insert the new indirect block into the parent now that we've
	 * cleared out some entries in the parent.  We calculated a good
	 * insertion index in the loop above (ichain->index).
	 *
	 * We don't have to set MOVED here because we mark ichain modified
	 * down below (so the normal modified -> flush -> set-moved sequence
	 * applies).
	 *
	 * The insertion shouldn't race as this is a completely new block
	 * and the parent is locked.
	 */
	if (ichain->index < 0)
		kprintf("indirect parent %p count %d key %016jx/%d\n",
			parent, count, (intmax_t)key, keybits);
	KKASSERT(ichain->index >= 0);
	KKASSERT((ichain->flags & HAMMER2_CHAIN_ONRBTREE) == 0);
	spin_lock(&parent->core->cst.spin);
	if (RB_INSERT(hammer2_chain_tree, &parent->core->rbtree, ichain))
		panic("hammer2_chain_create_indirect: ichain insertion");
	atomic_set_int(&ichain->flags, HAMMER2_CHAIN_ONRBTREE);
	ichain->parent = parent;
	hammer2_chain_ref(parent);	/* ichain->parent ref */
	spin_unlock(&parent->core->cst.spin);
	KKASSERT(parent->duplink == NULL); /* XXX mus be inside spin */

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
	/*hammer2_chain_modify(trans, ichain, HAMMER2_MODIFY_OPTDATA);*/
	hammer2_chain_parent_setsubmod(ichain);
	atomic_set_int(&ichain->flags, HAMMER2_CHAIN_SUBMODIFIED);

	/*
	 * Figure out what to return.
	 */
	if (create_bits > keybits) {
		/*
		 * Key being created is way outside the key range,
		 * return the original parent.
		 */
		hammer2_chain_unlock(ichain);
	} else if (~(((hammer2_key_t)1 << keybits) - 1) &
		   (create_key ^ key)) {
		/*
		 * Key being created is outside the key range,
		 * return the original parent.
		 */
		hammer2_chain_unlock(ichain);
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
 * Sets CHAIN_DELETED and CHAIN_MOVED in the chain being deleted and
 * set chain->delete_tid.
 *
 * This function does NOT generate a modification to the parent.  It
 * would be nearly impossible to figure out which parent to modify anyway.
 * Such modifications are handled by the flush code and are properly merged
 * using the flush synchronization point.
 *
 * The find/get code will properly overload the RBTREE check on top of
 * the bref check to detect deleted entries.
 *
 * This function is NOT recursive.  Any entity already pushed into the
 * chain (such as an inode) may still need visibility into its contents,
 * as well as the ability to read and modify the contents.  For example,
 * for an unlinked file which is still open.
 *
 * NOTE: This function does NOT set chain->modify_tid, allowing future
 *	 code to distinguish between live and deleted chains by testing
 *	 sync_tid.
 *
 * NOTE: Deletions normally do not occur in the middle of a duplication
 *	 chain but we use a trick for hardlink migration that refactors
 *	 the originating inode without deleting it, so we make no assumptions
 *	 here.
 */
void
hammer2_chain_delete(hammer2_trans_t *trans, hammer2_chain_t *parent,
		     hammer2_chain_t *chain)
{
	KKASSERT(ccms_thread_lock_owned(&parent->core->cst));

	/*
	 * Nothing to do if already marked.
	 */
	if (chain->flags & HAMMER2_CHAIN_DELETED)
		return;

	/*
	 * We must set MOVED along with DELETED for the flush code to
	 * recognize the operation and properly disconnect the chain
	 * in-memory.
	 *
	 * The setting of DELETED causes finds, lookups, and _next iterations
	 * to no longer recognize the chain.  RB_SCAN()s will still have
	 * visibility (needed for flush serialization points).
	 */
	atomic_set_int(&chain->flags, HAMMER2_CHAIN_DELETED);
	if ((chain->flags & HAMMER2_CHAIN_MOVED) == 0) {
		hammer2_chain_ref(chain);
		atomic_set_int(&chain->flags, HAMMER2_CHAIN_MOVED);
	}
	chain->delete_tid = trans->sync_tid;
	hammer2_chain_parent_setsubmod(chain);
}

void
hammer2_chain_wait(hammer2_chain_t *chain)
{
	tsleep(chain, 0, "chnflw", 1);
}
