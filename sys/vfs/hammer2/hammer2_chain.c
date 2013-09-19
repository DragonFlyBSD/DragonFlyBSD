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
#include <sys/kern_syscall.h>
#include <sys/uuid.h>

#include "hammer2.h"

static int hammer2_indirect_optimize;	/* XXX SYSCTL */

static hammer2_chain_t *hammer2_chain_create_indirect(
		hammer2_trans_t *trans, hammer2_chain_t *parent,
		hammer2_key_t key, int keybits, int for_type, int *errorp);
static void hammer2_chain_drop_data(hammer2_chain_t *chain, int lastdrop);
static void adjreadcounter(hammer2_blockref_t *bref, size_t bytes);

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

	/*
	 * Multiple deletions in the same transaction are possible.  We
	 * still need to detect SMP races on _get() so only do this
	 * conditionally.
	 */
	if ((chain1->flags & HAMMER2_CHAIN_DELETED) &&
	    (chain2->flags & HAMMER2_CHAIN_DELETED)) {
		if (chain1 < chain2)
			return(-1);
		if (chain1 > chain2)
			return(1);
	}

	return(0);
}

static __inline
int
hammer2_isclusterable(hammer2_chain_t *chain)
{
	if (hammer2_cluster_enable) {
		if (chain->bref.type == HAMMER2_BREF_TYPE_INDIRECT ||
		    chain->bref.type == HAMMER2_BREF_TYPE_INODE ||
		    chain->bref.type == HAMMER2_BREF_TYPE_DATA) {
			return(1);
		}
	}
	return(0);
}

/*
 * Recursively set the SUBMODIFIED flag up to the root starting at chain's
 * parent.  SUBMODIFIED is not set in chain itself.
 *
 * This function only operates on current-time transactions and is not
 * used during flushes.  Instead, the flush code manages the flag itself.
 */
void
hammer2_chain_setsubmod(hammer2_trans_t *trans, hammer2_chain_t *chain)
{
	hammer2_chain_core_t *above;

	if (trans->flags & HAMMER2_TRANS_ISFLUSH)
		return;
	while ((above = chain->above) != NULL) {
		spin_lock(&above->cst.spin);
		chain = above->first_parent;
		while (hammer2_chain_refactor_test(chain, 1))
			chain = chain->next_parent;
		atomic_set_int(&chain->flags, HAMMER2_CHAIN_SUBMODIFIED);
		spin_unlock(&above->cst.spin);
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
hammer2_chain_alloc(hammer2_mount_t *hmp, hammer2_pfsmount_t *pmp,
		    hammer2_trans_t *trans, hammer2_blockref_t *bref)
{
	hammer2_chain_t *chain;
	u_int bytes = 1U << (int)(bref->data_off & HAMMER2_OFF_MASK_RADIX);

	/*
	 * Construct the appropriate system structure.
	 */
	switch(bref->type) {
	case HAMMER2_BREF_TYPE_INODE:
	case HAMMER2_BREF_TYPE_INDIRECT:
	case HAMMER2_BREF_TYPE_FREEMAP_NODE:
	case HAMMER2_BREF_TYPE_DATA:
	case HAMMER2_BREF_TYPE_FREEMAP_LEAF:
		/*
		 * Chain's are really only associated with the hmp but we maintain
		 * a pmp association for per-mount memory tracking purposes.  The
		 * pmp can be NULL.
		 */
		chain = kmalloc(sizeof(*chain), hmp->mchain, M_WAITOK | M_ZERO);
		if (pmp) {
			chain->pmp = pmp;
			atomic_add_long(&pmp->inmem_chains, 1);
		}
		break;
	case HAMMER2_BREF_TYPE_VOLUME:
	case HAMMER2_BREF_TYPE_FREEMAP:
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
	if (trans)
		chain->modify_tid = trans->sync_tid;

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
	hammer2_chain_t **scanp;

	KKASSERT(chain->core == NULL);
	KKASSERT(chain->next_parent == NULL);

	if (core == NULL) {
		core = kmalloc(sizeof(*core), chain->hmp->mchain,
			       M_WAITOK | M_ZERO);
		RB_INIT(&core->rbtree);
		core->sharecnt = 1;
		chain->core = core;
		ccms_cst_init(&core->cst, chain);
		core->first_parent = chain;
	} else {
		atomic_add_int(&core->sharecnt, 1);
		chain->core = core;
		spin_lock(&core->cst.spin);
		if (core->first_parent == NULL) {
			core->first_parent = chain;
		} else {
			scanp = &core->first_parent;
			while (*scanp)
				scanp = &(*scanp)->next_parent;
			*scanp = chain;
			hammer2_chain_ref(chain);	/* next_parent link */
		}
		spin_unlock(&core->cst.spin);
	}
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
			chain = hammer2_chain_lastdrop(chain);
		} else {
			if (atomic_cmpset_int(&chain->refs, refs, refs - 1))
				break;
			/* retry the same chain */
		}
	}
}

/*
 * Safe handling of the 1->0 transition on chain.  Returns a chain for
 * recursive drop or NULL, possibly returning the same chain of the atomic
 * op fails.
 *
 * The cst spinlock is allowed nest child-to-parent (not parent-to-child).
 */
static
hammer2_chain_t *
hammer2_chain_lastdrop(hammer2_chain_t *chain)
{
	hammer2_pfsmount_t *pmp;
	hammer2_mount_t *hmp;
	hammer2_chain_core_t *above;
	hammer2_chain_core_t *core;
	hammer2_chain_t *rdrop1;
	hammer2_chain_t *rdrop2;

	/*
	 * Spinlock the core and check to see if it is empty.  If it is
	 * not empty we leave chain intact with refs == 0.
	 */
	if ((core = chain->core) != NULL) {
		spin_lock(&core->cst.spin);
		if (RB_ROOT(&core->rbtree)) {
			if (atomic_cmpset_int(&chain->refs, 1, 0)) {
				/* 1->0 transition successful */
				spin_unlock(&core->cst.spin);
				return(NULL);
			} else {
				/* 1->0 transition failed, retry */
				spin_unlock(&core->cst.spin);
				return(chain);
			}
		}
	}

	hmp = chain->hmp;
	pmp = chain->pmp;	/* can be NULL */
	rdrop1 = NULL;
	rdrop2 = NULL;

	/*
	 * Spinlock the parent and try to drop the last ref.  On success
	 * remove chain from its parent.
	 */
	if ((above = chain->above) != NULL) {
		spin_lock(&above->cst.spin);
		if (!atomic_cmpset_int(&chain->refs, 1, 0)) {
			/* 1->0 transition failed */
			spin_unlock(&above->cst.spin);
			if (core)
				spin_unlock(&core->cst.spin);
			return(chain);
			/* stop */
		}

		/*
		 * 1->0 transition successful
		 */
		KKASSERT(chain->flags & HAMMER2_CHAIN_ONRBTREE);
		RB_REMOVE(hammer2_chain_tree, &above->rbtree, chain);
		atomic_clear_int(&chain->flags, HAMMER2_CHAIN_ONRBTREE);
		chain->above = NULL;

		/*
		 * Calculate a chain to return for a recursive drop.
		 *
		 * XXX this needs help, we have a potential deep-recursion
		 * problem which we try to address but sometimes we wind up
		 * with two elements that have to be dropped.
		 *
		 * If the chain has an associated core with refs at 0
		 * the chain must be the first in the core's linked list
		 * by definition, and we will recursively drop the ref
		 * implied by the chain->next_parent field.
		 *
		 * Otherwise if the rbtree containing chain is empty we try
		 * to recursively drop our parent (only the first one could
		 * possibly have refs == 0 since the rest are linked via
		 * next_parent).
		 *
		 * Otherwise we try to recursively drop a sibling.
		 */
		if (chain->next_parent) {
			KKASSERT(core != NULL);
			rdrop1 = chain->next_parent;
		}
		if (RB_EMPTY(&above->rbtree)) {
			rdrop2 = above->first_parent;
			if (rdrop2 == NULL || rdrop2->refs ||
			    atomic_cmpset_int(&rdrop2->refs, 0, 1) == 0) {
				rdrop2 = NULL;
			}
		} else {
			rdrop2 = RB_ROOT(&above->rbtree);
			if (atomic_cmpset_int(&rdrop2->refs, 0, 1) == 0)
				rdrop2 = NULL;
		}
		spin_unlock(&above->cst.spin);
		above = NULL;	/* safety */
	} else {
		if (chain->next_parent) {
			KKASSERT(core != NULL);
			rdrop1 = chain->next_parent;
		}
	}

	/*
	 * We still have the core spinlock (if core is non-NULL).  The
	 * above spinlock is gone.
	 */
	if (core) {
		KKASSERT(core->first_parent == chain);
		if (chain->next_parent) {
			/* parent should already be set */
			KKASSERT(rdrop1 == chain->next_parent);
		}
		core->first_parent = chain->next_parent;
		chain->next_parent = NULL;
		chain->core = NULL;

		if (atomic_fetchadd_int(&core->sharecnt, -1) == 1) {
			/*
			 * On the 1->0 transition of core we can destroy
			 * it.
			 */
			spin_unlock(&core->cst.spin);
			KKASSERT(core->cst.count == 0);
			KKASSERT(core->cst.upgrade == 0);
			kfree(core, hmp->mchain);
		} else {
			spin_unlock(&core->cst.spin);
		}
		core = NULL;	/* safety */
	}

	/*
	 * All spin locks are gone, finish freeing stuff.
	 */
	KKASSERT((chain->flags & (HAMMER2_CHAIN_MOVED |
				  HAMMER2_CHAIN_MODIFIED)) == 0);

	hammer2_chain_drop_data(chain, 1);

	KKASSERT(chain->bp == NULL);
	chain->hmp = NULL;

	if (chain->flags & HAMMER2_CHAIN_ALLOCATED) {
		chain->flags &= ~HAMMER2_CHAIN_ALLOCATED;
		kfree(chain, hmp->mchain);
		if (pmp) {
			atomic_add_long(&pmp->inmem_chains, -1);
			hammer2_chain_memory_wakeup(pmp);
		}
	}
	if (rdrop1 && rdrop2) {
		hammer2_chain_drop(rdrop1);
		return(rdrop2);
	} else if (rdrop1)
		return(rdrop1);
	else
		return(rdrop2);
}

/*
 * On either last lock release or last drop
 */
static void
hammer2_chain_drop_data(hammer2_chain_t *chain, int lastdrop)
{
	hammer2_mount_t *hmp = chain->hmp;

	switch(chain->bref.type) {
	case HAMMER2_BREF_TYPE_VOLUME:
	case HAMMER2_BREF_TYPE_FREEMAP:
		if (lastdrop)
			chain->data = NULL;
		break;
	case HAMMER2_BREF_TYPE_INODE:
		if (chain->data) {
			kfree(chain->data, hmp->mchain);
			chain->data = NULL;
		}
		break;
	case HAMMER2_BREF_TYPE_FREEMAP_LEAF:
		if (chain->data) {
			kfree(chain->data, hmp->mchain);
			chain->data = NULL;
		}
		break;
	default:
		KKASSERT(chain->data == NULL);
		break;
	}
}


/*
 * Ref and lock a chain element, acquiring its data with I/O if necessary,
 * and specify how you would like the data to be resolved.
 *
 * Returns 0 on success or an error code if the data could not be acquired.
 * The chain element is locked on return regardless of whether an error
 * occurred or not.
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
	hammer2_off_t pmask;
	hammer2_off_t peof;
	ccms_state_t ostate;
	size_t boff;
	size_t psize;
	int error;
	char *bdata;

	/*
	 * Ref and lock the element.  Recursive locks are allowed.
	 */
	if ((how & HAMMER2_RESOLVE_NOREF) == 0)
		hammer2_chain_ref(chain);
	atomic_add_int(&chain->lockcnt, 1);

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
#if 0
		if (chain->bref.type == HAMMER2_BREF_TYPE_FREEMAP_NODE)
			return(0);
#endif
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
	 * to HAMMER2_MIN_ALLOC (typically 1K).  A 64K physical storage
	 * chunk always contains buffers of the same size. (XXX)
	 *
	 * The minimum physical IO size may be larger than the variable
	 * block size.
	 */
	bref = &chain->bref;

	psize = hammer2_devblksize(chain->bytes);
	pmask = (hammer2_off_t)psize - 1;
	pbase = bref->data_off & ~pmask;
	boff = bref->data_off & (HAMMER2_OFF_MASK & pmask);
	KKASSERT(pbase != 0);
	peof = (pbase + HAMMER2_SEGMASK64) & ~HAMMER2_SEGMASK64;

	/*
	 * The getblk() optimization can only be used on newly created
	 * elements if the physical block size matches the request.
	 */
	if ((chain->flags & HAMMER2_CHAIN_INITIAL) &&
	    chain->bytes == psize) {
		chain->bp = getblk(hmp->devvp, pbase, psize, 0, 0);
		error = 0;
	} else if (hammer2_isclusterable(chain)) {
		error = cluster_read(hmp->devvp, peof, pbase, psize,
				     psize, HAMMER2_PBUFSIZE*4,
				     &chain->bp);
		adjreadcounter(&chain->bref, chain->bytes);
	} else {
		error = bread(hmp->devvp, pbase, psize, &chain->bp);
		adjreadcounter(&chain->bref, chain->bytes);
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
	 * Mark the buffer for bdwrite().  This clears the INITIAL state
	 * but does not mark the chain modified.
	 */
	bdata = (char *)chain->bp->b_data + boff;
	if (chain->flags & HAMMER2_CHAIN_INITIAL) {
		atomic_clear_int(&chain->flags, HAMMER2_CHAIN_INITIAL);
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
	case HAMMER2_BREF_TYPE_FREEMAP:
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
		atomic_set_int(&chain->flags, HAMMER2_CHAIN_EMBEDDED);
		chain->data = kmalloc(sizeof(chain->data->ipdata),
				      hmp->mchain, M_WAITOK | M_ZERO);
		bcopy(bdata, &chain->data->ipdata, chain->bytes);
		bqrelse(chain->bp);
		chain->bp = NULL;
		break;
	case HAMMER2_BREF_TYPE_FREEMAP_LEAF:
		KKASSERT(chain->bytes == sizeof(chain->data->bmdata));
		atomic_set_int(&chain->flags, HAMMER2_CHAIN_EMBEDDED);
		chain->data = kmalloc(sizeof(chain->data->bmdata),
				      hmp->mchain, M_WAITOK | M_ZERO);
		bcopy(bdata, &chain->data->bmdata, chain->bytes);
		bqrelse(chain->bp);
		chain->bp = NULL;
		break;
	case HAMMER2_BREF_TYPE_INDIRECT:
	case HAMMER2_BREF_TYPE_DATA:
	case HAMMER2_BREF_TYPE_FREEMAP_NODE:
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
 * Asynchronously read the device buffer (dbp) and execute the specified
 * callback.  The caller should pass-in a locked chain (shared lock is ok).
 * The function is responsible for unlocking the chain and for disposing
 * of dbp.
 *
 * NOTE!  A NULL dbp (but non-NULL data) will be passed to the function
 *	  if the dbp is integrated into the chain, because we do not want
 *	  the caller to dispose of dbp in that situation.
 */
static void hammer2_chain_load_async_callback(struct bio *bio);

void
hammer2_chain_load_async(hammer2_chain_t *chain,
	void (*func)(hammer2_chain_t *, struct buf *, char *, void *),
	void *arg)
{
	hammer2_cbinfo_t *cbinfo;
	hammer2_mount_t *hmp;
	hammer2_blockref_t *bref;
	hammer2_off_t pbase;
	hammer2_off_t pmask;
	hammer2_off_t peof;
	struct buf *dbp;
	size_t boff;
	size_t psize;
	char *bdata;

	if (chain->data) {
		func(chain, NULL, (char *)chain->data, arg);
		return;
	}

	/*
	 * We must resolve to a device buffer, either by issuing I/O or
	 * by creating a zero-fill element.  We do not mark the buffer
	 * dirty when creating a zero-fill element (the hammer2_chain_modify()
	 * API must still be used to do that).
	 *
	 * The device buffer is variable-sized in powers of 2 down
	 * to HAMMER2_MIN_ALLOC (typically 1K).  A 64K physical storage
	 * chunk always contains buffers of the same size. (XXX)
	 *
	 * The minimum physical IO size may be larger than the variable
	 * block size.
	 */
	bref = &chain->bref;

	psize = hammer2_devblksize(chain->bytes);
	pmask = (hammer2_off_t)psize - 1;
	pbase = bref->data_off & ~pmask;
	boff = bref->data_off & (HAMMER2_OFF_MASK & pmask);
	KKASSERT(pbase != 0);
	peof = (pbase + HAMMER2_SEGMASK64) & ~HAMMER2_SEGMASK64;

	hmp = chain->hmp;

	/*
	 * The getblk() optimization can only be used on newly created
	 * elements if the physical block size matches the request.
	 */
	if ((chain->flags & HAMMER2_CHAIN_INITIAL) &&
	    chain->bytes == psize) {
		dbp = getblk(hmp->devvp, pbase, psize, 0, 0);
		/*atomic_clear_int(&chain->flags, HAMMER2_CHAIN_INITIAL);*/
		bdata = (char *)dbp->b_data + boff;
		bzero(bdata, chain->bytes);
		/*atomic_set_int(&chain->flags, HAMMER2_CHAIN_DIRTYBP);*/
		func(chain, dbp, bdata, arg);
		bqrelse(dbp);
		return;
	}

	adjreadcounter(&chain->bref, chain->bytes);
	cbinfo = kmalloc(sizeof(*cbinfo), hmp->mchain, M_INTWAIT | M_ZERO);
	cbinfo->chain = chain;
	cbinfo->func = func;
	cbinfo->arg = arg;
	cbinfo->boff = boff;

	cluster_readcb(hmp->devvp, peof, pbase, psize,
		HAMMER2_PBUFSIZE*4, HAMMER2_PBUFSIZE*4,
		hammer2_chain_load_async_callback, cbinfo);
}

static void
hammer2_chain_load_async_callback(struct bio *bio)
{
	hammer2_cbinfo_t *cbinfo;
	hammer2_mount_t *hmp;
	struct buf *dbp;
	char *data;

	/*
	 * Nobody is waiting for bio/dbp to complete, we are
	 * responsible for handling the biowait() equivalent
	 * on dbp which means clearing BIO_DONE and BIO_SYNC
	 * and calling bpdone() if it hasn't already been called
	 * to restore any covered holes in the buffer's backing
	 * store.
	 */
	dbp = bio->bio_buf;
	if ((bio->bio_flags & BIO_DONE) == 0)
		bpdone(dbp, 0);
	bio->bio_flags &= ~(BIO_DONE | BIO_SYNC);

	/*
	 * Extract the auxillary info and issue the callback.
	 * Finish up with the dbp after it returns.
	 */
	cbinfo = bio->bio_caller_info1.ptr;
	/*ccms_thread_lock_setown(cbinfo->chain->core);*/
	data = dbp->b_data + cbinfo->boff;
	hmp = cbinfo->chain->hmp;

	cbinfo = bio->bio_caller_info1.ptr;
	if (cbinfo->chain->flags & HAMMER2_CHAIN_INITIAL)
		bzero(data, cbinfo->chain->bytes);
	cbinfo->func(cbinfo->chain, dbp, data, cbinfo->arg);
	/* cbinfo->chain is stale now */
	bqrelse(dbp);
	kfree(cbinfo, hmp->mchain);
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
	ccms_state_t ostate;
	long *counterp;
	u_int lockcnt;

	/*
	 * The core->cst lock can be shared across several chains so we
	 * need to track the per-chain lockcnt separately.
	 *
	 * If multiple locks are present (or being attempted) on this
	 * particular chain we can just unlock, drop refs, and return.
	 *
	 * Otherwise fall-through on the 1->0 transition.
	 */
	for (;;) {
		lockcnt = chain->lockcnt;
		KKASSERT(lockcnt > 0);
		cpu_ccfence();
		if (lockcnt > 1) {
			if (atomic_cmpset_int(&chain->lockcnt,
					      lockcnt, lockcnt - 1)) {
				ccms_thread_unlock(&core->cst);
				hammer2_chain_drop(chain);
				return;
			}
		} else {
			if (atomic_cmpset_int(&chain->lockcnt, 1, 0))
				break;
		}
		/* retry */
	}

	/*
	 * On the 1->0 transition we upgrade the core lock (if necessary)
	 * to exclusive for terminal processing.  If after upgrading we find
	 * that lockcnt is non-zero, another thread is racing us and will
	 * handle the unload for us later on, so just cleanup and return
	 * leaving the data/bp intact
	 *
	 * Otherwise if lockcnt is still 0 it is possible for it to become
	 * non-zero and race, but since we hold the core->cst lock
	 * exclusively all that will happen is that the chain will be
	 * reloaded after we unload it.
	 */
	ostate = ccms_thread_lock_upgrade(&core->cst);
	if (chain->lockcnt) {
		ccms_thread_unlock_upgraded(&core->cst, ostate);
		hammer2_chain_drop(chain);
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
		if ((chain->flags & HAMMER2_CHAIN_MODIFIED) == 0)
			hammer2_chain_drop_data(chain, 0);
		ccms_thread_unlock_upgraded(&core->cst, ostate);
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
		case HAMMER2_BREF_TYPE_FREEMAP_NODE:
		case HAMMER2_BREF_TYPE_FREEMAP_LEAF:
			counterp = &hammer2_ioa_fmap_write;
			break;
		default:
			counterp = &hammer2_ioa_volu_write;
			break;
		}
		*counterp += chain->bytes;
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
		case HAMMER2_BREF_TYPE_FREEMAP_NODE:
		case HAMMER2_BREF_TYPE_FREEMAP_LEAF:
			counterp = &hammer2_iod_fmap_write;
			break;
		default:
			counterp = &hammer2_iod_volu_write;
			break;
		}
		*counterp += chain->bytes;
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
#if 0
	/*
	 * XXX our primary cache is now the block device, not
	 * the logical file. don't release the buffer.
	 */
	if (chain->bref.type == HAMMER2_BREF_TYPE_DATA)
		chain->bp->b_flags |= B_RELBUF;
#endif

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
	ccms_thread_unlock_upgraded(&core->cst, ostate);
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
		     hammer2_chain_t *parent, hammer2_chain_t **chainp,
		     int nradix, int flags)
{
	hammer2_mount_t *hmp;
	hammer2_chain_t *chain;
	hammer2_off_t pbase;
	size_t obytes;
	size_t nbytes;
	size_t bbytes;
	int boff;

	chain = *chainp;
	hmp = chain->hmp;

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
	 * The parent does not have to be locked for the delete/duplicate call,
	 * but is in this particular code path.
	 *
	 * NOTE: If we are not crossing a synchronization point the
	 *	 duplication code will simply reuse the existing chain
	 *	 structure.
	 */
	hammer2_chain_delete_duplicate(trans, &chain, 0);

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
		atomic_set_int(&chain->flags, HAMMER2_CHAIN_MODIFIED);
		hammer2_chain_ref(chain);
	}

	/*
	 * Relocate the block, even if making it smaller (because different
	 * block sizes may be in different regions).
	 */
	hammer2_freemap_alloc(trans, chain->hmp, &chain->bref, nbytes);
	chain->bytes = nbytes;
	/*ip->delta_dcount += (ssize_t)(nbytes - obytes);*/ /* XXX atomic */

	/*
	 * The device buffer may be larger than the allocation size.
	 */
	bbytes = hammer2_devblksize(chain->bytes);
	pbase = chain->bref.data_off & ~(hammer2_off_t)(bbytes - 1);
	boff = chain->bref.data_off & HAMMER2_OFF_MASK & (bbytes - 1);

	/*
	 * For now just support it on DATA chains (and not on indirect
	 * blocks).
	 */
	KKASSERT(chain->bp == NULL);

	/*
	 * Make sure the chain is marked MOVED and SUBMOD is set in the
	 * parent(s) so the adjustments are picked up by flush.
	 */
	if ((chain->flags & HAMMER2_CHAIN_MOVED) == 0) {
		hammer2_chain_ref(chain);
		atomic_set_int(&chain->flags, HAMMER2_CHAIN_MOVED);
	}
	hammer2_chain_setsubmod(trans, chain);
	*chainp = chain;
}

/*
 * Set a chain modified, making it read-write and duplicating it if necessary.
 * This function will assign a new physical block to the chain if necessary
 *
 * Duplication of already-modified chains is possible when the modification
 * crosses a flush synchronization boundary.
 *
 * Non-data blocks - The chain should be locked to at least the RESOLVE_MAYBE
 *		     level or the COW operation will not work.
 *
 * Data blocks	   - The chain is usually locked RESOLVE_NEVER so as not to
 *		     run the data through the device buffers.
 *
 * This function may return a different chain than was passed, in which case
 * the old chain will be unlocked and the new chain will be locked.
 *
 * ip->chain may be adjusted by hammer2_chain_modify_ip().
 */
hammer2_inode_data_t *
hammer2_chain_modify_ip(hammer2_trans_t *trans, hammer2_inode_t *ip,
			hammer2_chain_t **chainp, int flags)
{
	atomic_set_int(&ip->flags, HAMMER2_INODE_MODIFIED);
	hammer2_chain_modify(trans, chainp, flags);
	if (ip->chain != *chainp)
		hammer2_inode_repoint(ip, NULL, *chainp);
	return(&ip->chain->data->ipdata);
}

void
hammer2_chain_modify(hammer2_trans_t *trans, hammer2_chain_t **chainp,
		     int flags)
{
	hammer2_mount_t *hmp;
	hammer2_chain_t *chain;
	hammer2_off_t pbase;
	hammer2_off_t pmask;
	hammer2_off_t peof;
	hammer2_tid_t flush_tid;
	struct buf *nbp;
	int error;
	int wasinitial;
	size_t psize;
	size_t boff;
	void *bdata;

	/*
	 * Data must be resolved if already assigned unless explicitly
	 * flagged otherwise.
	 */
	chain = *chainp;
	hmp = chain->hmp;
	if (chain->data == NULL && (flags & HAMMER2_MODIFY_OPTDATA) == 0 &&
	    (chain->bref.data_off & ~HAMMER2_OFF_MASK_RADIX)) {
		hammer2_chain_lock(chain, HAMMER2_RESOLVE_ALWAYS);
		hammer2_chain_unlock(chain);
	}

	/*
	 * data is not optional for freemap chains (we must always be sure
	 * to copy the data on COW storage allocations).
	 */
	if (chain->bref.type == HAMMER2_BREF_TYPE_FREEMAP_NODE ||
	    chain->bref.type == HAMMER2_BREF_TYPE_FREEMAP_LEAF) {
		KKASSERT((chain->flags & HAMMER2_CHAIN_INITIAL) ||
			 (flags & HAMMER2_MODIFY_OPTDATA) == 0);
	}

	/*
	 * If the chain is already marked MODIFIED we can usually just
	 * return.  However, if a modified chain is modified again in
	 * a synchronization-point-crossing manner we have to issue a
	 * delete/duplicate on the chain to avoid flush interference.
	 */
	if (chain->flags & HAMMER2_CHAIN_MODIFIED) {
		/*
		 * Which flush_tid do we need to check?  If the chain is
		 * related to the freemap we have to use the freemap flush
		 * tid (free_flush_tid), otherwise we use the normal filesystem
		 * flush tid (topo_flush_tid).  The two flush domains are
		 * almost completely independent of each other.
		 */
		if (chain->bref.type == HAMMER2_BREF_TYPE_FREEMAP_NODE ||
		    chain->bref.type == HAMMER2_BREF_TYPE_FREEMAP_LEAF) {
			flush_tid = hmp->topo_flush_tid; /* XXX */
			goto skipxx;	/* XXX */
		} else {
			flush_tid = hmp->topo_flush_tid;
		}

		/*
		 * Main tests
		 */
		if (chain->modify_tid <= flush_tid &&
		    trans->sync_tid > flush_tid) {
			/*
			 * Modifications cross synchronization point,
			 * requires delete-duplicate.
			 */
			KKASSERT((flags & HAMMER2_MODIFY_ASSERTNOCOPY) == 0);
			hammer2_chain_delete_duplicate(trans, chainp, 0);
			chain = *chainp;
			/* fall through using duplicate */
		}
skipxx: /* XXX */
		/*
		 * Quick return path, set DIRTYBP to ensure that
		 * the later retirement of bp will write it out.
		 *
		 * quick return path also needs the modify_tid
		 * logic.
		 */
		if (chain->bp)
			atomic_set_int(&chain->flags, HAMMER2_CHAIN_DIRTYBP);
		if ((flags & HAMMER2_MODIFY_NO_MODIFY_TID) == 0)
			chain->bref.modify_tid = trans->sync_tid;
		chain->modify_tid = trans->sync_tid;
		return;
	}

	/*
	 * modify_tid is only update for primary modifications, not for
	 * propagated brefs.  mirror_tid will be updated regardless during
	 * the flush, no need to set it here.
	 */
	if ((flags & HAMMER2_MODIFY_NO_MODIFY_TID) == 0)
		chain->bref.modify_tid = trans->sync_tid;

	/*
	 * Set MODIFIED and add a chain ref to prevent destruction.  Both
	 * modified flags share the same ref.
	 */
	if ((chain->flags & HAMMER2_CHAIN_MODIFIED) == 0) {
		atomic_set_int(&chain->flags, HAMMER2_CHAIN_MODIFIED);
		hammer2_chain_ref(chain);
	}

	/*
	 * Adjust chain->modify_tid so the flusher knows when the
	 * modification occurred.
	 */
	chain->modify_tid = trans->sync_tid;

	/*
	 * The modification or re-modification requires an allocation and
	 * possible COW.
	 *
	 * We normally always allocate new storage here.  If storage exists
	 * and MODIFY_NOREALLOC is passed in, we do not allocate new storage.
	 */
	if (chain != &hmp->vchain &&
	    chain != &hmp->fchain &&
	    ((chain->bref.data_off & ~HAMMER2_OFF_MASK_RADIX) == 0 ||
	     (flags & HAMMER2_MODIFY_NOREALLOC) == 0)
	) {
		hammer2_freemap_alloc(trans, chain->hmp,
				      &chain->bref, chain->bytes);
		/* XXX failed allocation */
	}

	/*
	 * Do not COW if OPTDATA is set.  INITIAL flag remains unchanged.
	 * (OPTDATA does not prevent [re]allocation of storage, only the
	 * related copy-on-write op).
	 */
	if (flags & HAMMER2_MODIFY_OPTDATA)
		goto skip2;

	/*
	 * Clearing the INITIAL flag (for indirect blocks) indicates that
	 * we've processed the uninitialized storage allocation.
	 *
	 * If this flag is already clear we are likely in a copy-on-write
	 * situation but we have to be sure NOT to bzero the storage if
	 * no data is present.
	 */
	if (chain->flags & HAMMER2_CHAIN_INITIAL) {
		atomic_clear_int(&chain->flags, HAMMER2_CHAIN_INITIAL);
		wasinitial = 1;
	} else {
		wasinitial = 0;
	}

#if 0
	/*
	 * We currently should never instantiate a device buffer for a
	 * file data chain.  (We definitely can for a freemap chain).
	 *
	 * XXX we can now do this
	 */
	KKASSERT(chain->bref.type != HAMMER2_BREF_TYPE_DATA);
#endif

	/*
	 * Instantiate data buffer and possibly execute COW operation
	 */
	switch(chain->bref.type) {
	case HAMMER2_BREF_TYPE_VOLUME:
	case HAMMER2_BREF_TYPE_FREEMAP:
	case HAMMER2_BREF_TYPE_INODE:
	case HAMMER2_BREF_TYPE_FREEMAP_LEAF:
		/*
		 * The data is embedded, no copy-on-write operation is
		 * needed.
		 */
		KKASSERT(chain->bp == NULL);
		break;
	case HAMMER2_BREF_TYPE_DATA:
	case HAMMER2_BREF_TYPE_INDIRECT:
	case HAMMER2_BREF_TYPE_FREEMAP_NODE:
		/*
		 * Perform the copy-on-write operation
		 */
		KKASSERT(chain != &hmp->vchain && chain != &hmp->fchain);

		psize = hammer2_devblksize(chain->bytes);
		pmask = (hammer2_off_t)psize - 1;
		pbase = chain->bref.data_off & ~pmask;
		boff = chain->bref.data_off & (HAMMER2_OFF_MASK & pmask);
		KKASSERT(pbase != 0);
		peof = (pbase + HAMMER2_SEGMASK64) & ~HAMMER2_SEGMASK64;

		/*
		 * The getblk() optimization can only be used if the
		 * chain element size matches the physical block size.
		 */
		if (chain->bp && chain->bp->b_loffset == pbase) {
			nbp = chain->bp;
			error = 0;
		} else if (chain->bytes == psize) {
			nbp = getblk(hmp->devvp, pbase, psize, 0, 0);
			error = 0;
		} else if (hammer2_isclusterable(chain)) {
			error = cluster_read(hmp->devvp, peof, pbase, psize,
					     psize, HAMMER2_PBUFSIZE*4,
					     &nbp);
			adjreadcounter(&chain->bref, chain->bytes);
		} else {
			error = bread(hmp->devvp, pbase, psize, &nbp);
			adjreadcounter(&chain->bref, chain->bytes);
		}
		KKASSERT(error == 0);
		bdata = (char *)nbp->b_data + boff;

		/*
		 * Copy or zero-fill on write depending on whether
		 * chain->data exists or not.  Retire the existing bp
		 * based on the DIRTYBP flag.  Set the DIRTYBP flag to
		 * indicate that retirement of nbp should use bdwrite().
		 */
		if (chain->data) {
			KKASSERT(chain->bp != NULL);
			if (chain->data != bdata) {
				bcopy(chain->data, bdata, chain->bytes);
			}
		} else if (wasinitial) {
			bzero(bdata, chain->bytes);
		} else {
			/*
			 * We have a problem.  We were asked to COW but
			 * we don't have any data to COW with!
			 */
			panic("hammer2_chain_modify: having a COW %p\n",
			      chain);
		}
		if (chain->bp != nbp) {
			if (chain->bp) {
				if (chain->flags & HAMMER2_CHAIN_DIRTYBP) {
					chain->bp->b_flags |= B_CLUSTEROK;
					bdwrite(chain->bp);
				} else {
					chain->bp->b_flags |= B_RELBUF;
					brelse(chain->bp);
				}
			}
			chain->bp = nbp;
			BUF_KERNPROC(chain->bp);
		}
		chain->data = bdata;
		atomic_set_int(&chain->flags, HAMMER2_CHAIN_DIRTYBP);
		break;
	default:
		panic("hammer2_chain_modify: illegal non-embedded type %d",
		      chain->bref.type);
		break;

	}
skip2:
	hammer2_chain_setsubmod(trans, chain);
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
 * CHAIN_DELETED or not.  However, because chain iterations can be removed
 * from memory we must ALSO check that DELETED chains are not flushed.  A
 * DELETED chain which has been flushed must be ignored (the caller must
 * check the parent's blockref array).
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
		/*
		 * Normally the child with the larger delete_tid (which would
		 * be MAX_TID if the child is not deleted) wins.  However, if
		 * the child was deleted AND flushed (DELETED set and MOVED
		 * no longer set), the parent bref is now valid and we don't
		 * want the child to improperly shadow it.
		 */
		if ((child->flags &
		     (HAMMER2_CHAIN_DELETED | HAMMER2_CHAIN_MOVED)) !=
		    HAMMER2_CHAIN_DELETED) {
			info->delete_tid = child->delete_tid;
			info->best = child;
		}
	}
	return(0);
}

static
hammer2_chain_t *
hammer2_chain_find_locked(hammer2_chain_t *parent, int index)
{
	struct hammer2_chain_find_info info;
	hammer2_chain_t *child;

	info.index = index;
	info.delete_tid = 0;
	info.best = NULL;

	RB_SCAN(hammer2_chain_tree, &parent->core->rbtree,
		hammer2_chain_find_cmp, hammer2_chain_find_callback,
		&info);
	child = info.best;

	return (child);
}

hammer2_chain_t *
hammer2_chain_find(hammer2_chain_t *parent, int index)
{
	hammer2_chain_t *child;

	spin_lock(&parent->core->cst.spin);
	child = hammer2_chain_find_locked(parent, index);
	if (child)
		hammer2_chain_ref(child);
	spin_unlock(&parent->core->cst.spin);

	return (child);
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
	hammer2_chain_core_t *above = parent->core;
	hammer2_chain_t *chain;
	hammer2_chain_t dummy;
	int how;

	/*
	 * Figure out how to lock.  MAYBE can be used to optimized
	 * the initial-create state for indirect blocks.
	 */
	if (flags & HAMMER2_LOOKUP_ALWAYS)
		how = HAMMER2_RESOLVE_ALWAYS;
	else if (flags & (HAMMER2_LOOKUP_NODATA | HAMMER2_LOOKUP_NOLOCK))
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
	spin_lock(&above->cst.spin);
	chain = RB_FIND(hammer2_chain_tree, &above->rbtree, &dummy);
	if (chain) {
		hammer2_chain_ref(chain);
		spin_unlock(&above->cst.spin);
		if ((flags & HAMMER2_LOOKUP_NOLOCK) == 0)
			hammer2_chain_lock(chain, how | HAMMER2_RESOLVE_NOREF);
		return(chain);
	}
	spin_unlock(&above->cst.spin);

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
	case HAMMER2_BREF_TYPE_FREEMAP_NODE:
		KKASSERT(parent->data != NULL);
		KKASSERT(index >= 0 &&
			 index < parent->bytes / sizeof(hammer2_blockref_t));
		bref = &parent->data->npdata[index];
		break;
	case HAMMER2_BREF_TYPE_VOLUME:
		KKASSERT(index >= 0 && index < HAMMER2_SET_COUNT);
		bref = &hmp->voldata.sroot_blockset.blockref[index];
		break;
	case HAMMER2_BREF_TYPE_FREEMAP:
		KKASSERT(index >= 0 && index < HAMMER2_SET_COUNT);
		bref = &hmp->voldata.freemap_blockset.blockref[index];
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
	chain = hammer2_chain_alloc(hmp, parent->pmp, NULL, bref);
	hammer2_chain_core_alloc(chain, NULL);	/* ref'd chain returned */

	/*
	 * Link the chain into its parent.  A spinlock is required to safely
	 * access the RBTREE, and it is possible to collide with another
	 * hammer2_chain_get() operation because the caller might only hold
	 * a shared lock on the parent.
	 */
	KKASSERT(parent->refs > 0);
	spin_lock(&above->cst.spin);
	chain->above = above;
	chain->index = index;
	if (RB_INSERT(hammer2_chain_tree, &above->rbtree, chain)) {
		chain->above = NULL;
		chain->index = -1;
		spin_unlock(&above->cst.spin);
		hammer2_chain_drop(chain);
		goto retry;
	}
	atomic_set_int(&chain->flags, HAMMER2_CHAIN_ONRBTREE);
	spin_unlock(&above->cst.spin);

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

static
hammer2_chain_t *
hammer2_chain_getparent(hammer2_chain_t **parentp, int how)
{
	hammer2_chain_t *oparent;
	hammer2_chain_t *nparent;
	hammer2_chain_core_t *above;

	oparent = *parentp;
	above = oparent->above;

	spin_lock(&above->cst.spin);
	nparent = above->first_parent;
	while (hammer2_chain_refactor_test(nparent, 1))
		nparent = nparent->next_parent;
	hammer2_chain_ref(nparent);	/* protect nparent, use in lock */
	spin_unlock(&above->cst.spin);

	hammer2_chain_unlock(oparent);
	hammer2_chain_lock(nparent, how | HAMMER2_RESOLVE_NOREF);
	*parentp = nparent;

	return (nparent);
}

/*
 * Locate any key between key_beg and key_end inclusive.  (*parentp)
 * typically points to an inode but can also point to a related indirect
 * block and this function will recurse upwards and find the inode again.
 *
 * WARNING!  THIS DOES NOT RETURN KEYS IN LOGICAL KEY ORDER!  ANY KEY
 *	     WITHIN THE RANGE CAN BE RETURNED.  HOWEVER, AN ITERATION
 *	     WHICH PICKS UP WHERE WE LEFT OFF WILL CONTINUE THE SCAN
 *	     AND ALL IN-RANGE KEYS WILL EVENTUALLY BE RETURNED (NOT
 *	     NECESSARILY IN ORDER).
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
 *
 * NOTE!  chain->data is not always resolved.  By default it will not be
 *	  resolved for BREF_TYPE_DATA, FREEMAP_NODE, or FREEMAP_LEAF.  Use
 *	  HAMMER2_LOOKUP_ALWAYS to force resolution (but be careful w/
 *	  BREF_TYPE_DATA as the device buffer can alias the logical file
 *	  buffer).
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

	if (flags & HAMMER2_LOOKUP_ALWAYS)
		how_maybe = how_always;

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
		parent = hammer2_chain_getparent(parentp, how_maybe);
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
	case HAMMER2_BREF_TYPE_FREEMAP_NODE:
	case HAMMER2_BREF_TYPE_INDIRECT:
		/*
		 * Handle MATCHIND on the parent
		 */
		if (flags & HAMMER2_LOOKUP_MATCHIND) {
			scan_beg = parent->bref.key;
			scan_end = scan_beg +
			       ((hammer2_key_t)1 << parent->bref.keybits) - 1;
			if (key_beg == scan_beg && key_end == scan_end) {
				chain = parent;
				hammer2_chain_lock(chain, how_maybe);
				goto done;
			}
		}
		/*
		 * Optimize indirect blocks in the INITIAL state to avoid
		 * I/O.
		 */
		if (parent->flags & HAMMER2_CHAIN_INITIAL) {
			base = NULL;
		} else {
			if (parent->data == NULL)
				panic("parent->data is NULL");
			base = &parent->data->npdata[0];
		}
		count = parent->bytes / sizeof(hammer2_blockref_t);
		break;
	case HAMMER2_BREF_TYPE_VOLUME:
		base = &hmp->voldata.sroot_blockset.blockref[0];
		count = HAMMER2_SET_COUNT;
		break;
	case HAMMER2_BREF_TYPE_FREEMAP:
		base = &hmp->voldata.freemap_blockset.blockref[0];
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
	scan_beg = 0;	/* avoid compiler warning */
	scan_end = 0;	/* avoid compiler warning */

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
	 *
	 * If HAMMER2_LOOKUP_MATCHIND is set and the indirect block's key
	 * range is within the requested key range we return the indirect
	 * block and do NOT loop.  This is usually only used to acquire
	 * freemap nodes.
	 */
	if (chain->bref.type == HAMMER2_BREF_TYPE_INDIRECT ||
	    chain->bref.type == HAMMER2_BREF_TYPE_FREEMAP_NODE) {
		hammer2_chain_unlock(parent);
		*parentp = parent = chain;
		if (flags & HAMMER2_LOOKUP_NOLOCK) {
			hammer2_chain_lock(chain,
					   how_maybe |
					   HAMMER2_RESOLVE_NOREF);
		} else if ((flags & HAMMER2_LOOKUP_NODATA) &&
			   chain->data == NULL) {
			hammer2_chain_ref(chain);
			hammer2_chain_unlock(chain);
			hammer2_chain_lock(chain,
					   how_maybe |
					   HAMMER2_RESOLVE_NOREF);
		}
		goto again;
	}
done:
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
 *
 * WARNING!  The MATCHIND flag does not apply to this function.
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
		parent = hammer2_chain_getparent(parentp, how_maybe);
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
	case HAMMER2_BREF_TYPE_FREEMAP_NODE:
		if (parent->flags & HAMMER2_CHAIN_INITIAL) {
			base = NULL;
		} else {
			KKASSERT(parent->data != NULL);
			base = &parent->data->npdata[0];
		}
		count = parent->bytes / sizeof(hammer2_blockref_t);
		break;
	case HAMMER2_BREF_TYPE_VOLUME:
		base = &hmp->voldata.sroot_blockset.blockref[0];
		count = HAMMER2_SET_COUNT;
		break;
	case HAMMER2_BREF_TYPE_FREEMAP:
		base = &hmp->voldata.freemap_blockset.blockref[0];
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
	scan_beg = 0;	/* avoid compiler warning */
	scan_end = 0;	/* avoid compiler warning */

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
	 *
	 * If HAMMER2_LOOKUP_MATCHIND is set and the indirect block's key
	 * range is within the requested key range we return the indirect
	 * block and do NOT loop.  This is usually only used to acquire
	 * freemap nodes.
	 */
	if (chain->bref.type == HAMMER2_BREF_TYPE_INDIRECT ||
	    chain->bref.type == HAMMER2_BREF_TYPE_FREEMAP_NODE) {
		if ((flags & HAMMER2_LOOKUP_MATCHIND) == 0 ||
		    key_beg > scan_beg || key_end < scan_end) {
			hammer2_chain_unlock(parent);
			*parentp = parent = chain;
			chain = NULL;
			if (flags & HAMMER2_LOOKUP_NOLOCK) {
				hammer2_chain_lock(parent,
						   how_maybe |
						   HAMMER2_RESOLVE_NOREF);
			} else if ((flags & HAMMER2_LOOKUP_NODATA) &&
				   parent->data == NULL) {
				hammer2_chain_ref(parent);
				hammer2_chain_unlock(parent);
				hammer2_chain_lock(parent,
						   how_maybe |
						   HAMMER2_RESOLVE_NOREF);
			}
			i = 0;
			goto again2;
		}
	}

	/*
	 * All done, return chain
	 */
	return (chain);
}

/*
 * Loop on parent's children, issuing the callback for each child.
 *
 * Uses LOOKUP flags.
 */
int
hammer2_chain_iterate(hammer2_chain_t *parent,
		      int (*callback)(hammer2_chain_t *parent,
				      hammer2_chain_t **chainp,
				      void *arg),
		      void *arg, int flags)
{
	hammer2_chain_t *chain;
	hammer2_blockref_t *base;
	int count;
	int i;
	int res;

	/*
	 * Scan the children (if any)
	 */
	res = 0;
	i = 0;
	for (;;) {
		/*
		 * Calculate the blockref array on each loop in order
		 * to allow the callback to temporarily unlock/relock
		 * the parent.
		 */
		switch(parent->bref.type) {
		case HAMMER2_BREF_TYPE_INODE:
			base = &parent->data->ipdata.u.blockset.blockref[0];
			count = HAMMER2_SET_COUNT;
			break;
		case HAMMER2_BREF_TYPE_INDIRECT:
		case HAMMER2_BREF_TYPE_FREEMAP_NODE:
			if (parent->flags & HAMMER2_CHAIN_INITIAL) {
				base = NULL;
			} else {
				KKASSERT(parent->data != NULL);
				base = &parent->data->npdata[0];
			}
			count = parent->bytes / sizeof(hammer2_blockref_t);
			break;
		case HAMMER2_BREF_TYPE_VOLUME:
			base = &parent->hmp->voldata.sroot_blockset.blockref[0];
			count = HAMMER2_SET_COUNT;
			break;
		case HAMMER2_BREF_TYPE_FREEMAP:
			base = &parent->hmp->voldata.freemap_blockset.blockref[0];
			count = HAMMER2_SET_COUNT;
			break;
		default:
			/*
			 * The function allows calls on non-recursive
			 * chains and will effectively be a nop() in that
			 * case.
			 */
			base = NULL;
			count = 0;
			break;
		}

		/*
		 * Loop termination
		 */
		if (i >= count)
			break;

		/*
		 * Lookup the child, properly overloading any elements
		 * held in memory.
		 *
		 * NOTE: Deleted elements cover any underlying base[] entry
		 *	 (which might not have been zero'd out yet).
		 *
		 * NOTE: The fact that there can be multiple stacked
		 *	 deleted elements at the same index is hidden
		 *	 by hammer2_chain_find().
		 */
		chain = hammer2_chain_find(parent, i);
		if (chain) {
			if (chain->flags & HAMMER2_CHAIN_DELETED) {
				hammer2_chain_drop(chain);
				++i;
				continue;
			}
		} else if (base == NULL || base[i].type == 0) {
			++i;
			continue;
		}
		if (chain)
			hammer2_chain_drop(chain);
		chain = hammer2_chain_get(parent, i, flags);
		if (chain) {
			res = callback(parent, &chain, arg);
			if (chain) {
				if (flags & HAMMER2_LOOKUP_NOLOCK)
					hammer2_chain_drop(chain);
				else
					hammer2_chain_unlock(chain);
			}
			if (res < 0)
				break;
		}
		++i;
	}
	return res;
}

/*
 * Create and return a new hammer2 system memory structure of the specified
 * key, type and size and insert it under (*parentp).  This is a full
 * insertion, based on the supplied key/keybits, and may involve creating
 * indirect blocks and moving other chains around via delete/duplicate.
 *
 * (*parentp) must be exclusive locked and may be replaced on return
 * depending on how much work the function had to do.
 *
 * (*chainp) usually starts out NULL and returns the newly created chain,
 * but if the caller desires the caller may allocate a disconnected chain
 * and pass it in instead.  (It is also possible for the caller to use
 * chain_duplicate() to create a disconnected chain, manipulate it, then
 * pass it into this function to insert it).
 *
 * This function should NOT be used to insert INDIRECT blocks.  It is
 * typically used to create/insert inodes and data blocks.
 *
 * Caller must pass-in an exclusively locked parent the new chain is to
 * be inserted under, and optionally pass-in a disconnected, exclusively
 * locked chain to insert (else we create a new chain).  The function will
 * adjust (*parentp) as necessary, create or connect the chain, and
 * return an exclusively locked chain in *chainp.
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
	hammer2_chain_core_t *above;
	hammer2_blockref_t dummy;
	hammer2_blockref_t *base;
	int allocated = 0;
	int error = 0;
	int count;
	int i;

	above = parent->core;
	KKASSERT(ccms_thread_lock_owned(&above->cst));
	hmp = parent->hmp;
	chain = *chainp;

	if (chain == NULL) {
		/*
		 * First allocate media space and construct the dummy bref,
		 * then allocate the in-memory chain structure.  Set the
		 * INITIAL flag for fresh chains.
		 */
		bzero(&dummy, sizeof(dummy));
		dummy.type = type;
		dummy.key = key;
		dummy.keybits = keybits;
		dummy.data_off = hammer2_getradix(bytes);
		dummy.methods = parent->bref.methods;
		chain = hammer2_chain_alloc(hmp, parent->pmp, trans, &dummy);
		hammer2_chain_core_alloc(chain, NULL);

		atomic_set_int(&chain->flags, HAMMER2_CHAIN_INITIAL);

		/*
		 * Lock the chain manually, chain_lock will load the chain
		 * which we do NOT want to do.  (note: chain->refs is set
		 * to 1 by chain_alloc() for us, but lockcnt is not).
		 */
		chain->lockcnt = 1;
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
		case HAMMER2_BREF_TYPE_FREEMAP:
			panic("hammer2_chain_create: called with volume type");
			break;
		case HAMMER2_BREF_TYPE_INODE:
			KKASSERT(bytes == HAMMER2_INODE_BYTES);
			atomic_set_int(&chain->flags, HAMMER2_CHAIN_EMBEDDED);
			chain->data = kmalloc(sizeof(chain->data->ipdata),
					      hmp->mchain, M_WAITOK | M_ZERO);
			break;
		case HAMMER2_BREF_TYPE_INDIRECT:
			panic("hammer2_chain_create: cannot be used to"
			      "create indirect block");
			break;
		case HAMMER2_BREF_TYPE_FREEMAP_NODE:
			panic("hammer2_chain_create: cannot be used to"
			      "create freemap root or node");
			break;
		case HAMMER2_BREF_TYPE_FREEMAP_LEAF:
			KKASSERT(bytes == sizeof(chain->data->bmdata));
			atomic_set_int(&chain->flags, HAMMER2_CHAIN_EMBEDDED);
			chain->data = kmalloc(sizeof(chain->data->bmdata),
					      hmp->mchain, M_WAITOK | M_ZERO);
			break;
		case HAMMER2_BREF_TYPE_DATA:
		default:
			/* leave chain->data NULL */
			KKASSERT(chain->data == NULL);
			break;
		}
	} else {
		/*
		 * Potentially update the existing chain's key/keybits.
		 *
		 * Do NOT mess with the current state of the INITIAL flag.
		 */
		chain->bref.key = key;
		chain->bref.keybits = keybits;
		KKASSERT(chain->above == NULL);
	}

again:
	above = parent->core;

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
	case HAMMER2_BREF_TYPE_FREEMAP_NODE:
		if (parent->flags & HAMMER2_CHAIN_INITIAL) {
			base = NULL;
		} else {
			KKASSERT(parent->data != NULL);
			base = &parent->data->npdata[0];
		}
		count = parent->bytes / sizeof(hammer2_blockref_t);
		break;
	case HAMMER2_BREF_TYPE_VOLUME:
		KKASSERT(parent->data != NULL);
		base = &hmp->voldata.sroot_blockset.blockref[0];
		count = HAMMER2_SET_COUNT;
		break;
	case HAMMER2_BREF_TYPE_FREEMAP:
		KKASSERT(parent->data != NULL);
		base = &hmp->voldata.freemap_blockset.blockref[0];
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
	spin_lock(&above->cst.spin);
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
	spin_unlock(&above->cst.spin);

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
							type, &error);
		if (nparent == NULL) {
			if (allocated)
				hammer2_chain_drop(chain);
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
	if (chain->above != NULL)
		panic("hammer2: hammer2_chain_create: chain already connected");
	KKASSERT(chain->above == NULL);
	KKASSERT((chain->flags & HAMMER2_CHAIN_DELETED) == 0);

	chain->above = above;
	chain->index = i;
	spin_lock(&above->cst.spin);
	if (RB_INSERT(hammer2_chain_tree, &above->rbtree, chain))
		panic("hammer2_chain_create: collision");
	atomic_set_int(&chain->flags, HAMMER2_CHAIN_ONRBTREE);
	spin_unlock(&above->cst.spin);

	if (allocated) {
		/*
		 * Mark the newly created chain modified.
		 *
		 * Device buffers are not instantiated for DATA elements
		 * as these are handled by logical buffers.
		 *
		 * Indirect and freemap node indirect blocks are handled
		 * by hammer2_chain_create_indirect() and not by this
		 * function.
		 *
		 * Data for all other bref types is expected to be
		 * instantiated (INODE, LEAF).
		 */
		switch(chain->bref.type) {
		case HAMMER2_BREF_TYPE_DATA:
			hammer2_chain_modify(trans, &chain,
					     HAMMER2_MODIFY_OPTDATA |
					     HAMMER2_MODIFY_ASSERTNOCOPY);
			break;
		case HAMMER2_BREF_TYPE_FREEMAP_LEAF:
		case HAMMER2_BREF_TYPE_INODE:
			hammer2_chain_modify(trans, &chain,
					     HAMMER2_MODIFY_ASSERTNOCOPY);
			break;
		default:
			/*
			 * Remaining types are not supported by this function.
			 * In particular, INDIRECT and LEAF_NODE types are
			 * handled by create_indirect().
			 */
			panic("hammer2_chain_create: bad type: %d",
			      chain->bref.type);
			/* NOT REACHED */
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
		hammer2_chain_setsubmod(trans, chain);
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
 *	 appears atomic w/regards to media flushes.
 */
static void hammer2_chain_dup_fixup(hammer2_chain_t *ochain,
				    hammer2_chain_t *nchain);

void
hammer2_chain_duplicate(hammer2_trans_t *trans, hammer2_chain_t *parent, int i,
			hammer2_chain_t **chainp, hammer2_blockref_t *bref)
{
	hammer2_mount_t *hmp;
	hammer2_blockref_t *base;
	hammer2_chain_t *ochain;
	hammer2_chain_t *nchain;
	hammer2_chain_t *scan;
	hammer2_chain_core_t *above;
	size_t bytes;
	int count;
	int oflags;
	void *odata;

	/*
	 * First create a duplicate of the chain structure, associating
	 * it with the same core, making it the same size, pointing it
	 * to the same bref (the same media block).
	 */
	ochain = *chainp;
	hmp = ochain->hmp;
	if (bref == NULL)
		bref = &ochain->bref;
	nchain = hammer2_chain_alloc(hmp, ochain->pmp, trans, bref);
	hammer2_chain_core_alloc(nchain, ochain->core);
	bytes = (hammer2_off_t)1 <<
		(int)(bref->data_off & HAMMER2_OFF_MASK_RADIX);
	nchain->bytes = bytes;
	nchain->modify_tid = ochain->modify_tid;

	hammer2_chain_lock(nchain, HAMMER2_RESOLVE_NEVER);
	hammer2_chain_dup_fixup(ochain, nchain);

	/*
	 * If parent is not NULL, insert into the parent at the requested
	 * index.  The newly duplicated chain must be marked MOVED and
	 * SUBMODIFIED set in its parent(s).
	 *
	 * Having both chains locked is extremely important for atomicy.
	 */
	if (parent) {
		/*
		 * Locate a free blockref in the parent's array
		 */
		above = parent->core;
		KKASSERT(ccms_thread_lock_owned(&above->cst));

		switch(parent->bref.type) {
		case HAMMER2_BREF_TYPE_INODE:
			KKASSERT((parent->data->ipdata.op_flags &
				  HAMMER2_OPFLAG_DIRECTDATA) == 0);
			KKASSERT(parent->data != NULL);
			base = &parent->data->ipdata.u.blockset.blockref[0];
			count = HAMMER2_SET_COUNT;
			break;
		case HAMMER2_BREF_TYPE_INDIRECT:
		case HAMMER2_BREF_TYPE_FREEMAP_NODE:
			if (parent->flags & HAMMER2_CHAIN_INITIAL) {
				base = NULL;
			} else {
				KKASSERT(parent->data != NULL);
				base = &parent->data->npdata[0];
			}
			count = parent->bytes / sizeof(hammer2_blockref_t);
			break;
		case HAMMER2_BREF_TYPE_VOLUME:
			KKASSERT(parent->data != NULL);
			base = &hmp->voldata.sroot_blockset.blockref[0];
			count = HAMMER2_SET_COUNT;
			break;
		case HAMMER2_BREF_TYPE_FREEMAP:
			KKASSERT(parent->data != NULL);
			base = &hmp->voldata.freemap_blockset.blockref[0];
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

		KKASSERT((nchain->flags & HAMMER2_CHAIN_DELETED) == 0);
		KKASSERT(parent->refs > 0);

		spin_lock(&above->cst.spin);
		nchain->above = above;
		nchain->index = i;
		scan = hammer2_chain_find_locked(parent, i);
		KKASSERT(base == NULL || base[i].type == 0 ||
			 scan == NULL ||
			 (scan->flags & HAMMER2_CHAIN_DELETED));
		if (RB_INSERT(hammer2_chain_tree, &above->rbtree,
			      nchain)) {
			panic("hammer2_chain_duplicate: collision");
		}
		atomic_set_int(&nchain->flags, HAMMER2_CHAIN_ONRBTREE);
		spin_unlock(&above->cst.spin);

		if ((nchain->flags & HAMMER2_CHAIN_MOVED) == 0) {
			hammer2_chain_ref(nchain);
			atomic_set_int(&nchain->flags, HAMMER2_CHAIN_MOVED);
		}
		hammer2_chain_setsubmod(trans, nchain);
	}

	/*
	 * We have to unlock ochain to flush any dirty data, asserting the
	 * case (data == NULL) to catch any extra locks that might have been
	 * present, then transfer state to nchain.
	 */
	oflags = ochain->flags;
	odata = ochain->data;
	hammer2_chain_unlock(ochain);
	KKASSERT((ochain->flags & HAMMER2_CHAIN_EMBEDDED) ||
		 ochain->data == NULL);

	if (oflags & HAMMER2_CHAIN_INITIAL)
		atomic_set_int(&nchain->flags, HAMMER2_CHAIN_INITIAL);

	/*
	 * WARNING!  We should never resolve DATA to device buffers
	 *	     (XXX allow it if the caller did?), and since
	 *	     we currently do not have the logical buffer cache
	 *	     buffer in-hand to fix its cached physical offset
	 *	     we also force the modify code to not COW it. XXX
	 */
	if (oflags & HAMMER2_CHAIN_MODIFIED) {
		if (nchain->bref.type == HAMMER2_BREF_TYPE_DATA) {
			hammer2_chain_modify(trans, &nchain,
					     HAMMER2_MODIFY_OPTDATA |
					     HAMMER2_MODIFY_NOREALLOC |
					     HAMMER2_MODIFY_ASSERTNOCOPY);
		} else if (oflags & HAMMER2_CHAIN_INITIAL) {
			hammer2_chain_modify(trans, &nchain,
					     HAMMER2_MODIFY_OPTDATA |
					     HAMMER2_MODIFY_ASSERTNOCOPY);
		} else {
			hammer2_chain_modify(trans, &nchain,
					     HAMMER2_MODIFY_ASSERTNOCOPY);
		}
		hammer2_chain_drop(nchain);
	} else {
		if (nchain->bref.type == HAMMER2_BREF_TYPE_DATA) {
			hammer2_chain_drop(nchain);
		} else if (oflags & HAMMER2_CHAIN_INITIAL) {
			hammer2_chain_drop(nchain);
		} else {
			hammer2_chain_lock(nchain, HAMMER2_RESOLVE_ALWAYS |
						   HAMMER2_RESOLVE_NOREF);
			hammer2_chain_unlock(nchain);
		}
	}
	atomic_set_int(&nchain->flags, HAMMER2_CHAIN_SUBMODIFIED);
	*chainp = nchain;
}

#if 0
		/*
		 * When the chain is in the INITIAL state we must still
		 * ensure that a block has been assigned so MOVED processing
		 * works as expected.
		 */
		KKASSERT (nchain->bref.type != HAMMER2_BREF_TYPE_DATA);
		hammer2_chain_modify(trans, &nchain,
				     HAMMER2_MODIFY_OPTDATA |
				     HAMMER2_MODIFY_ASSERTNOCOPY);


	hammer2_chain_lock(nchain, HAMMER2_RESOLVE_MAYBE |
				   HAMMER2_RESOLVE_NOREF); /* eat excess ref */
	hammer2_chain_unlock(nchain);
#endif

/*
 * Special in-place delete-duplicate sequence which does not require a
 * locked parent.  (*chainp) is marked DELETED and atomically replaced
 * with a duplicate.  Atomicy is at the very-fine spin-lock level in
 * order to ensure that lookups do not race us.
 *
 * If the input chain is already marked deleted the duplicated chain will
 * also be marked deleted.  This case can occur when an inode is removed
 * from the filesystem but programs still have an open descriptor to it.
 */
void
hammer2_chain_delete_duplicate(hammer2_trans_t *trans, hammer2_chain_t **chainp,
			       int flags)
{
	hammer2_mount_t *hmp;
	hammer2_chain_t *ochain;
	hammer2_chain_t *nchain;
	hammer2_chain_core_t *above;
	size_t bytes;
	int oflags;
	void *odata;

	ochain = *chainp;
	oflags = ochain->flags;
	hmp = ochain->hmp;

	/*
	 * Shortcut DELETED case if possible (only if delete_tid already matches the
	 * transaction id).
	 */
	if ((oflags & HAMMER2_CHAIN_DELETED) &&
	    ochain->delete_tid == trans->sync_tid) {
		return;
	}

	/*
	 * First create a duplicate of the chain structure
	 */
	nchain = hammer2_chain_alloc(hmp, ochain->pmp, trans, &ochain->bref);    /* 1 ref */
	if (flags & HAMMER2_DELDUP_RECORE)
		hammer2_chain_core_alloc(nchain, NULL);
	else
		hammer2_chain_core_alloc(nchain, ochain->core);
	above = ochain->above;

	bytes = (hammer2_off_t)1 <<
		(int)(ochain->bref.data_off & HAMMER2_OFF_MASK_RADIX);
	nchain->bytes = bytes;
	nchain->modify_tid = ochain->modify_tid;
	nchain->data_count += ochain->data_count;
	nchain->inode_count += ochain->inode_count;

	/*
	 * Lock nchain so both chains are now locked (extremely important
	 * for atomicy).  Mark ochain deleted and reinsert into the topology
	 * and insert nchain all in one go.
	 *
	 * If the ochain is already deleted it is left alone and nchain
	 * is inserted into the topology as a deleted chain.  This is
	 * important because it allows ongoing operations to be executed
	 * on a deleted inode which still has open descriptors.
	 */
	hammer2_chain_lock(nchain, HAMMER2_RESOLVE_NEVER);
	hammer2_chain_dup_fixup(ochain, nchain);
	/* extra ref still present from original allocation */

	nchain->index = ochain->index;

	KKASSERT(ochain->flags & HAMMER2_CHAIN_ONRBTREE);
	spin_lock(&above->cst.spin);
	KKASSERT(ochain->flags & HAMMER2_CHAIN_ONRBTREE);

	if (oflags & HAMMER2_CHAIN_DELETED) {
		atomic_set_int(&nchain->flags, HAMMER2_CHAIN_DELETED);
		nchain->delete_tid = trans->sync_tid;
	} else {
		RB_REMOVE(hammer2_chain_tree, &above->rbtree, ochain);
		ochain->delete_tid = trans->sync_tid;
		atomic_set_int(&ochain->flags, HAMMER2_CHAIN_DELETED);
		if (RB_INSERT(hammer2_chain_tree, &above->rbtree, ochain))
			panic("chain_delete: reinsertion failed %p", ochain);
	}

	nchain->above = above;
	atomic_set_int(&nchain->flags, HAMMER2_CHAIN_ONRBTREE);
	if (RB_INSERT(hammer2_chain_tree, &above->rbtree, nchain)) {
		panic("hammer2_chain_delete_duplicate: collision");
	}

	if ((ochain->flags & HAMMER2_CHAIN_MOVED) == 0) {
		hammer2_chain_ref(ochain);
		atomic_set_int(&ochain->flags, HAMMER2_CHAIN_MOVED);
	}
	spin_unlock(&above->cst.spin);

	/*
	 * We have to unlock ochain to flush any dirty data, asserting the
	 * case (data == NULL) to catch any extra locks that might have been
	 * present, then transfer state to nchain.
	 */
	odata = ochain->data;
	hammer2_chain_unlock(ochain);	/* replacing ochain */
	KKASSERT(ochain->bref.type == HAMMER2_BREF_TYPE_INODE ||
		 ochain->data == NULL);

	if (oflags & HAMMER2_CHAIN_INITIAL)
		atomic_set_int(&nchain->flags, HAMMER2_CHAIN_INITIAL);

	/*
	 * WARNING!  We should never resolve DATA to device buffers
	 *	     (XXX allow it if the caller did?), and since
	 *	     we currently do not have the logical buffer cache
	 *	     buffer in-hand to fix its cached physical offset
	 *	     we also force the modify code to not COW it. XXX
	 */
	if (oflags & HAMMER2_CHAIN_MODIFIED) {
		if (nchain->bref.type == HAMMER2_BREF_TYPE_DATA) {
			hammer2_chain_modify(trans, &nchain,
					     HAMMER2_MODIFY_OPTDATA |
					     HAMMER2_MODIFY_NOREALLOC |
					     HAMMER2_MODIFY_ASSERTNOCOPY);
		} else if (oflags & HAMMER2_CHAIN_INITIAL) {
			hammer2_chain_modify(trans, &nchain,
					     HAMMER2_MODIFY_OPTDATA |
					     HAMMER2_MODIFY_ASSERTNOCOPY);
		} else {
			hammer2_chain_modify(trans, &nchain,
					     HAMMER2_MODIFY_ASSERTNOCOPY);
		}
		hammer2_chain_drop(nchain);
	} else {
		if (nchain->bref.type == HAMMER2_BREF_TYPE_DATA) {
			hammer2_chain_drop(nchain);
		} else if (oflags & HAMMER2_CHAIN_INITIAL) {
			hammer2_chain_drop(nchain);
		} else {
			hammer2_chain_lock(nchain, HAMMER2_RESOLVE_ALWAYS |
						   HAMMER2_RESOLVE_NOREF);
			hammer2_chain_unlock(nchain);
		}
	}

	/*
	 * Unconditionally set the MOVED and SUBMODIFIED bit to force
	 * update of parent bref and indirect blockrefs during flush.
	 */
	if ((nchain->flags & HAMMER2_CHAIN_MOVED) == 0) {
		atomic_set_int(&nchain->flags, HAMMER2_CHAIN_MOVED);
		hammer2_chain_ref(nchain);
	}
	atomic_set_int(&nchain->flags, HAMMER2_CHAIN_SUBMODIFIED);
	hammer2_chain_setsubmod(trans, nchain);
	*chainp = nchain;
}

/*
 * Helper function to fixup inodes.  The caller procedure stack may hold
 * multiple locks on ochain if it represents an inode, preventing our
 * unlock from retiring its state to the buffer cache.
 *
 * In this situation any attempt to access the buffer cache could result
 * either in stale data or a deadlock.  Work around the problem by copying
 * the embedded data directly.
 */
static
void
hammer2_chain_dup_fixup(hammer2_chain_t *ochain, hammer2_chain_t *nchain)
{
	if (ochain->data == NULL)
		return;
	switch(ochain->bref.type) {
	case HAMMER2_BREF_TYPE_INODE:
		KKASSERT(nchain->data == NULL);
		atomic_set_int(&nchain->flags, HAMMER2_CHAIN_EMBEDDED);
		nchain->data = kmalloc(sizeof(nchain->data->ipdata),
				       ochain->hmp->mchain, M_WAITOK | M_ZERO);
		nchain->data->ipdata = ochain->data->ipdata;
		break;
	case HAMMER2_BREF_TYPE_FREEMAP_LEAF:
		KKASSERT(nchain->data == NULL);
		atomic_set_int(&nchain->flags, HAMMER2_CHAIN_EMBEDDED);
		nchain->data = kmalloc(sizeof(nchain->data->bmdata),
				       ochain->hmp->mchain, M_WAITOK | M_ZERO);
		bcopy(ochain->data->bmdata,
		      nchain->data->bmdata,
		      sizeof(nchain->data->bmdata));
		break;
	default:
		break;
	}
}

/*
 * Create a snapshot of the specified {parent, chain} with the specified
 * label.
 *
 * (a) We create a duplicate connected to the super-root as the specified
 *     label.
 *
 * (b) We issue a restricted flush using the current transaction on the
 *     duplicate.
 *
 * (c) We disconnect and reallocate the duplicate's core.
 */
int
hammer2_chain_snapshot(hammer2_trans_t *trans, hammer2_inode_t *ip,
		       hammer2_ioc_pfs_t *pfs)
{
	hammer2_cluster_t *cluster;
	hammer2_mount_t *hmp;
	hammer2_chain_t *chain;
	hammer2_chain_t *nchain;
	hammer2_chain_t *parent;
	hammer2_inode_data_t *ipdata;
	size_t name_len;
	hammer2_key_t lhc;
	int error;

	name_len = strlen(pfs->name);
	lhc = hammer2_dirhash(pfs->name, name_len);
	cluster = ip->pmp->mount_cluster;
	hmp = ip->chain->hmp;
	KKASSERT(hmp == cluster->hmp);	/* XXX */

	/*
	 * Create disconnected duplicate
	 */
	KKASSERT((trans->flags & HAMMER2_TRANS_RESTRICTED) == 0);
	nchain = ip->chain;
	hammer2_chain_lock(nchain, HAMMER2_RESOLVE_MAYBE);
	hammer2_chain_duplicate(trans, NULL, -1, &nchain, NULL);
	atomic_set_int(&nchain->flags, HAMMER2_CHAIN_RECYCLE |
				       HAMMER2_CHAIN_SNAPSHOT);

	/*
	 * Create named entry in the super-root.
	 */
        parent = hammer2_chain_lookup_init(hmp->schain, 0);
	error = 0;
	while (error == 0) {
		chain = hammer2_chain_lookup(&parent, lhc, lhc, 0);
		if (chain == NULL)
			break;
		if ((lhc & HAMMER2_DIRHASH_LOMASK) == HAMMER2_DIRHASH_LOMASK)
			error = ENOSPC;
		hammer2_chain_unlock(chain);
		chain = NULL;
		++lhc;
	}
	hammer2_chain_create(trans, &parent, &nchain, lhc, 0,
			     HAMMER2_BREF_TYPE_INODE,
			     HAMMER2_INODE_BYTES);
	hammer2_chain_modify(trans, &nchain, HAMMER2_MODIFY_ASSERTNOCOPY);
	hammer2_chain_lookup_done(parent);
	parent = NULL;	/* safety */

	/*
	 * Name fixup
	 */
	ipdata = &nchain->data->ipdata;
	ipdata->name_key = lhc;
	ipdata->name_len = name_len;
	ksnprintf(ipdata->filename, sizeof(ipdata->filename), "%s", pfs->name);

	/*
	 * Set PFS type, generate a unique filesystem id, and generate
	 * a cluster id.  Use the same clid when snapshotting a PFS root,
	 * which theoretically allows the snapshot to be used as part of
	 * the same cluster (perhaps as a cache).
	 */
	ipdata->pfs_type = HAMMER2_PFSTYPE_SNAPSHOT;
	kern_uuidgen(&ipdata->pfs_fsid, 1);
	if (ip->chain == cluster->rchain)
		ipdata->pfs_clid = ip->chain->data->ipdata.pfs_clid;
	else
		kern_uuidgen(&ipdata->pfs_clid, 1);

	/*
	 * Issue a restricted flush of the snapshot.  This is a synchronous
	 * operation.
	 */
	trans->flags |= HAMMER2_TRANS_RESTRICTED;
	kprintf("SNAPSHOTA\n");
	tsleep(trans, 0, "snapslp", hz*4);
	kprintf("SNAPSHOTB\n");
	hammer2_chain_flush(trans, nchain);
	trans->flags &= ~HAMMER2_TRANS_RESTRICTED;

#if 0
	/*
	 * Remove the link b/c nchain is a snapshot and snapshots don't
	 * follow CHAIN_DELETED semantics ?
	 */
	chain = ip->chain;


	KKASSERT(chain->duplink == nchain);
	KKASSERT(chain->core == nchain->core);
	KKASSERT(nchain->refs >= 2);
	chain->duplink = nchain->duplink;
	atomic_clear_int(&nchain->flags, HAMMER2_CHAIN_DUPTARGET);
	hammer2_chain_drop(nchain);
#endif

	kprintf("snapshot %s nchain->refs %d nchain->flags %08x\n",
		pfs->name, nchain->refs, nchain->flags);
	hammer2_chain_unlock(nchain);

	return (error);
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
static int hammer2_chain_indkey_freemap(hammer2_chain_t *parent,
				hammer2_key_t *keyp, int keybits,
				hammer2_blockref_t *base, int count);
static int hammer2_chain_indkey_normal(hammer2_chain_t *parent,
				hammer2_key_t *keyp, int keybits,
				hammer2_blockref_t *base, int count);
static
hammer2_chain_t *
hammer2_chain_create_indirect(hammer2_trans_t *trans, hammer2_chain_t *parent,
			      hammer2_key_t create_key, int create_bits,
			      int for_type, int *errorp)
{
	hammer2_mount_t *hmp;
	hammer2_chain_core_t *above;
	hammer2_chain_core_t *icore;
	hammer2_blockref_t *base;
	hammer2_blockref_t *bref;
	hammer2_chain_t *chain;
	hammer2_chain_t *child;
	hammer2_chain_t *ichain;
	hammer2_chain_t dummy;
	hammer2_key_t key = create_key;
	int keybits = create_bits;
	int count;
	int nbytes;
	int i;

	/*
	 * Calculate the base blockref pointer or NULL if the chain
	 * is known to be empty.  We need to calculate the array count
	 * for RB lookups either way.
	 */
	hmp = parent->hmp;
	*errorp = 0;
	KKASSERT(ccms_thread_lock_owned(&parent->core->cst));
	above = parent->core;

	/*hammer2_chain_modify(trans, &parent, HAMMER2_MODIFY_OPTDATA);*/
	if (parent->flags & HAMMER2_CHAIN_INITIAL) {
		base = NULL;

		switch(parent->bref.type) {
		case HAMMER2_BREF_TYPE_INODE:
			count = HAMMER2_SET_COUNT;
			break;
		case HAMMER2_BREF_TYPE_INDIRECT:
		case HAMMER2_BREF_TYPE_FREEMAP_NODE:
			count = parent->bytes / sizeof(hammer2_blockref_t);
			break;
		case HAMMER2_BREF_TYPE_VOLUME:
			count = HAMMER2_SET_COUNT;
			break;
		case HAMMER2_BREF_TYPE_FREEMAP:
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
		case HAMMER2_BREF_TYPE_FREEMAP_NODE:
			base = &parent->data->npdata[0];
			count = parent->bytes / sizeof(hammer2_blockref_t);
			break;
		case HAMMER2_BREF_TYPE_VOLUME:
			base = &hmp->voldata.sroot_blockset.blockref[0];
			count = HAMMER2_SET_COUNT;
			break;
		case HAMMER2_BREF_TYPE_FREEMAP:
			base = &hmp->voldata.freemap_blockset.blockref[0];
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
	 * dummy used in later chain allocation (no longer used for lookups).
	 */
	bzero(&dummy, sizeof(dummy));
	dummy.delete_tid = HAMMER2_MAX_TID;

	/*
	 * When creating an indirect block for a freemap node or leaf
	 * the key/keybits must be fitted to static radix levels because
	 * particular radix levels use particular reserved blocks in the
	 * related zone.
	 *
	 * This routine calculates the key/radix of the indirect block
	 * we need to create, and whether it is on the high-side or the
	 * low-side.
	 */
	if (for_type == HAMMER2_BREF_TYPE_FREEMAP_NODE ||
	    for_type == HAMMER2_BREF_TYPE_FREEMAP_LEAF) {
		keybits = hammer2_chain_indkey_freemap(parent, &key, keybits,
						       base, count);
	} else {
		keybits = hammer2_chain_indkey_normal(parent, &key, keybits,
						      base, count);
	}

	/*
	 * Normalize the key for the radix being represented, keeping the
	 * high bits and throwing away the low bits.
	 */
	key &= ~(((hammer2_key_t)1 << keybits) - 1);

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
	if (for_type == HAMMER2_BREF_TYPE_FREEMAP_NODE ||
	    for_type == HAMMER2_BREF_TYPE_FREEMAP_LEAF) {
		dummy.bref.type = HAMMER2_BREF_TYPE_FREEMAP_NODE;
	} else {
		dummy.bref.type = HAMMER2_BREF_TYPE_INDIRECT;
	}
	dummy.bref.key = key;
	dummy.bref.keybits = keybits;
	dummy.bref.data_off = hammer2_getradix(nbytes);
	dummy.bref.methods = parent->bref.methods;

	ichain = hammer2_chain_alloc(hmp, parent->pmp, trans, &dummy.bref);
	atomic_set_int(&ichain->flags, HAMMER2_CHAIN_INITIAL);
	hammer2_chain_core_alloc(ichain, NULL);
	icore = ichain->core;
	hammer2_chain_lock(ichain, HAMMER2_RESOLVE_MAYBE);
	hammer2_chain_drop(ichain);	/* excess ref from alloc */

	/*
	 * We have to mark it modified to allocate its block, but use
	 * OPTDATA to allow it to remain in the INITIAL state.  Otherwise
	 * it won't be acted upon by the flush code.
	 *
	 * XXX leave the node unmodified, depend on the SUBMODIFIED
	 * flush to assign and modify parent blocks.
	 */
	hammer2_chain_modify(trans, &ichain, HAMMER2_MODIFY_OPTDATA);

	/*
	 * Iterate the original parent and move the matching brefs into
	 * the new indirect block.
	 *
	 * At the same time locate an empty slot (or what will become an
	 * empty slot) and assign the new indirect block to that slot.
	 *
	 * XXX handle flushes.
	 */
	spin_lock(&above->cst.spin);
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
		 * Skip keys that are not within the key/radix of the new
		 * indirect block.  They stay in the parent.
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
		 * WARNING! above->cst.spin must be held when parent is
		 *	    modified, even though we own the full blown lock,
		 *	    to deal with setsubmod and rename races.
		 *	    (XXX remove this req).
		 */
		spin_unlock(&above->cst.spin);
		chain = hammer2_chain_get(parent, i, HAMMER2_LOOKUP_NODATA);
		hammer2_chain_delete(trans, chain, HAMMER2_DELETE_WILLDUP);
		hammer2_chain_duplicate(trans, ichain, i, &chain, NULL);
		hammer2_chain_unlock(chain);
		KKASSERT(parent->refs > 0);
		chain = NULL;
		spin_lock(&above->cst.spin);
	}
	spin_unlock(&above->cst.spin);

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
	spin_lock(&above->cst.spin);
	if (RB_INSERT(hammer2_chain_tree, &above->rbtree, ichain))
		panic("hammer2_chain_create_indirect: ichain insertion");
	atomic_set_int(&ichain->flags, HAMMER2_CHAIN_ONRBTREE);
	ichain->above = above;
	spin_unlock(&above->cst.spin);

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
	/*hammer2_chain_modify(trans, &ichain, HAMMER2_MODIFY_OPTDATA);*/
	atomic_set_int(&ichain->flags, HAMMER2_CHAIN_SUBMODIFIED);
	hammer2_chain_setsubmod(trans, ichain);

	/*
	 * Figure out what to return.
	 */
	if (~(((hammer2_key_t)1 << keybits) - 1) &
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
 * Calculate the keybits and highside/lowside of the freemap node the
 * caller is creating.
 *
 * This routine will specify the next higher-level freemap key/radix
 * representing the lowest-ordered set.  By doing so, eventually all
 * low-ordered sets will be moved one level down.
 *
 * We have to be careful here because the freemap reserves a limited
 * number of blocks for a limited number of levels.  So we can't just
 * push indiscriminately.
 */
int
hammer2_chain_indkey_freemap(hammer2_chain_t *parent, hammer2_key_t *keyp,
			     int keybits, hammer2_blockref_t *base, int count)
{
	hammer2_chain_core_t *above;
	hammer2_chain_t *child;
	hammer2_blockref_t *bref;
	hammer2_key_t key;
	int locount;
	int hicount;
	int i;

	key = *keyp;
	above = parent->core;
	locount = 0;
	hicount = 0;
	keybits = 64;

	/*
	 * Calculate the range of keys in the array being careful to skip
	 * slots which are overridden with a deletion.
	 */
	spin_lock(&above->cst.spin);
	for (i = 0; i < count; ++i) {
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

		if (keybits > bref->keybits) {
			key = bref->key;
			keybits = bref->keybits;
		} else if (keybits == bref->keybits && bref->key < key) {
			key = bref->key;
		}
	}
	spin_unlock(&above->cst.spin);

	/*
	 * Return the keybits for a higher-level FREEMAP_NODE covering
	 * this node.
	 */
	switch(keybits) {
	case HAMMER2_FREEMAP_LEVEL0_RADIX:
		keybits = HAMMER2_FREEMAP_LEVEL1_RADIX;
		break;
	case HAMMER2_FREEMAP_LEVEL1_RADIX:
		keybits = HAMMER2_FREEMAP_LEVEL2_RADIX;
		break;
	case HAMMER2_FREEMAP_LEVEL2_RADIX:
		keybits = HAMMER2_FREEMAP_LEVEL3_RADIX;
		break;
	case HAMMER2_FREEMAP_LEVEL3_RADIX:
		keybits = HAMMER2_FREEMAP_LEVEL4_RADIX;
		break;
	case HAMMER2_FREEMAP_LEVEL4_RADIX:
		panic("hammer2_chain_indkey_freemap: level too high");
		break;
	default:
		panic("hammer2_chain_indkey_freemap: bad radix");
		break;
	}
	*keyp = key;

	return (keybits);
}

/*
 * Calculate the keybits and highside/lowside of the indirect block the
 * caller is creating.
 */
static int
hammer2_chain_indkey_normal(hammer2_chain_t *parent, hammer2_key_t *keyp,
			    int keybits, hammer2_blockref_t *base, int count)
{
	hammer2_chain_core_t *above;
	hammer2_chain_t *child;
	hammer2_blockref_t *bref;
	hammer2_key_t key;
	int nkeybits;
	int locount;
	int hicount;
	int i;

	key = *keyp;
	above = parent->core;
	locount = 0;
	hicount = 0;

	/*
	 * Calculate the range of keys in the array being careful to skip
	 * slots which are overridden with a deletion.  Once the scan
	 * completes we will cut the key range in half and shift half the
	 * range into the new indirect block.
	 */
	spin_lock(&above->cst.spin);
	for (i = 0; i < count; ++i) {
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
				kprintf("bad bref index %d chain %p bref %p\n",
					i, child, bref);
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
	spin_unlock(&above->cst.spin);
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
	*keyp = key;

	return (keybits);
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
hammer2_chain_delete(hammer2_trans_t *trans, hammer2_chain_t *chain, int flags)
{
	KKASSERT(ccms_thread_lock_owned(&chain->core->cst));

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
	 *
	 * We need the spinlock on the core whos RBTREE contains chain
	 * to protect against races.
	 */
	KKASSERT(chain->flags & HAMMER2_CHAIN_ONRBTREE);
	spin_lock(&chain->above->cst.spin);

	RB_REMOVE(hammer2_chain_tree, &chain->above->rbtree, chain);
	chain->delete_tid = trans->sync_tid;
	atomic_set_int(&chain->flags, HAMMER2_CHAIN_DELETED);
	if (RB_INSERT(hammer2_chain_tree, &chain->above->rbtree, chain))
		panic("chain_delete: reinsertion failed %p", chain);

	if ((chain->flags & HAMMER2_CHAIN_MOVED) == 0) {
		hammer2_chain_ref(chain);
		atomic_set_int(&chain->flags, HAMMER2_CHAIN_MOVED);
	}
	spin_unlock(&chain->above->cst.spin);

	/*
	 * Mark the underlying block as possibly being free unless WILLDUP
	 * is set.  Duplication can occur in many situations, particularly
	 * when chains are moved to indirect blocks.
	 */
	if ((flags & HAMMER2_DELETE_WILLDUP) == 0)
		hammer2_freemap_free(trans, chain->hmp, &chain->bref, 0);
	hammer2_chain_setsubmod(trans, chain);
}

void
hammer2_chain_wait(hammer2_chain_t *chain)
{
	tsleep(chain, 0, "chnflw", 1);
}

/*
 * Manage excessive memory resource use for chain and related
 * structures.
 */
void
hammer2_chain_memory_wait(hammer2_pfsmount_t *pmp)
{
#if 0
	while (pmp->inmem_chains > desiredvnodes / 10 &&
	       pmp->inmem_chains > pmp->mp->mnt_nvnodelistsize * 2) {
		kprintf("w");
		speedup_syncer();
		pmp->inmem_waiting = 1;
		tsleep(&pmp->inmem_waiting, 0, "chnmem", hz);
	}
#endif
#if 0
	if (pmp->inmem_chains > desiredvnodes / 10 &&
	    pmp->inmem_chains > pmp->mp->mnt_nvnodelistsize * 7 / 4) {
		speedup_syncer();
	}
#endif
}

void
hammer2_chain_memory_wakeup(hammer2_pfsmount_t *pmp)
{
	if (pmp->inmem_waiting &&
	    (pmp->inmem_chains <= desiredvnodes / 10 ||
	     pmp->inmem_chains <= pmp->mp->mnt_nvnodelistsize * 2)) {
		kprintf("s");
		pmp->inmem_waiting = 0;
		wakeup(&pmp->inmem_waiting);
	}
}

static
void
adjreadcounter(hammer2_blockref_t *bref, size_t bytes)
{
	long *counterp;

	switch(bref->type) {
	case HAMMER2_BREF_TYPE_DATA:
		counterp = &hammer2_iod_file_read;
		break;
	case HAMMER2_BREF_TYPE_INODE:
		counterp = &hammer2_iod_meta_read;
		break;
	case HAMMER2_BREF_TYPE_INDIRECT:
		counterp = &hammer2_iod_indr_read;
		break;
	case HAMMER2_BREF_TYPE_FREEMAP_NODE:
	case HAMMER2_BREF_TYPE_FREEMAP_LEAF:
		counterp = &hammer2_iod_fmap_read;
		break;
	default:
		counterp = &hammer2_iod_volu_read;
		break;
	}
	*counterp += bytes;
}
