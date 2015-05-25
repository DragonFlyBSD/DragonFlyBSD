/*
 * Copyright (c) 2011-2014 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Matthew Dillon <dillon@dragonflybsd.org>
 * and Venkatesh Srinivas <vsrinivas@dragonflybsd.org>
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
 * the hammer2_chain structure.
 *
 * Chains are the in-memory version on media objects (volume header, inodes,
 * indirect blocks, data blocks, etc).  Chains represent a portion of the
 * HAMMER2 topology.
 *
 * Chains are no-longer delete-duplicated.  Instead, the original in-memory
 * chain will be moved along with its block reference (e.g. for things like
 * renames, hardlink operations, modifications, etc), and will be indexed
 * on a secondary list for flush handling instead of propagating a flag
 * upward to the root.
 *
 * Concurrent front-end operations can still run against backend flushes
 * as long as they do not cross the current flush boundary.  An operation
 * running above the current flush (in areas not yet flushed) can become
 * part of the current flush while ano peration running below the current
 * flush can become part of the next flush.
 */
#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/types.h>
#include <sys/lock.h>
#include <sys/kern_syscall.h>
#include <sys/uuid.h>

#include <crypto/sha2/sha2.h>

#include "hammer2.h"

static int hammer2_indirect_optimize;	/* XXX SYSCTL */

static hammer2_chain_t *hammer2_chain_create_indirect(
		hammer2_trans_t *trans, hammer2_chain_t *parent,
		hammer2_key_t key, int keybits, int for_type, int *errorp);
static void hammer2_chain_drop_data(hammer2_chain_t *chain, int lastdrop);
static hammer2_chain_t *hammer2_combined_find(
		hammer2_chain_t *parent,
		hammer2_blockref_t *base, int count,
		int *cache_indexp, hammer2_key_t *key_nextp,
		hammer2_key_t key_beg, hammer2_key_t key_end,
		hammer2_blockref_t **bresp);

/*
 * Basic RBTree for chains (core->rbtree and core->dbtree).  Chains cannot
 * overlap in the RB trees.  Deleted chains are moved from rbtree to either
 * dbtree or to dbq.
 *
 * Chains in delete-duplicate sequences can always iterate through core_entry
 * to locate the live version of the chain.
 */
RB_GENERATE(hammer2_chain_tree, hammer2_chain, rbnode, hammer2_chain_cmp);

int
hammer2_chain_cmp(hammer2_chain_t *chain1, hammer2_chain_t *chain2)
{
	hammer2_key_t c1_beg;
	hammer2_key_t c1_end;
	hammer2_key_t c2_beg;
	hammer2_key_t c2_end;

	/*
	 * Compare chains.  Overlaps are not supposed to happen and catch
	 * any software issues early we count overlaps as a match.
	 */
	c1_beg = chain1->bref.key;
	c1_end = c1_beg + ((hammer2_key_t)1 << chain1->bref.keybits) - 1;
	c2_beg = chain2->bref.key;
	c2_end = c2_beg + ((hammer2_key_t)1 << chain2->bref.keybits) - 1;

	if (c1_end < c2_beg)	/* fully to the left */
		return(-1);
	if (c1_beg > c2_end)	/* fully to the right */
		return(1);
	return(0);		/* overlap (must not cross edge boundary) */
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
 * Make a chain visible to the flusher.  The flusher needs to be able to
 * do flushes of subdirectory chains or single files so it does a top-down
 * recursion using the ONFLUSH flag for the recursion.  It locates MODIFIED
 * or UPDATE chains and flushes back up the chain to the volume root.
 *
 * This routine sets ONFLUSH upward until it hits the volume root.  For
 * simplicity we ignore PFSROOT boundaries whos rules can be complex.
 * Extra ONFLUSH flagging doesn't hurt the filesystem.
 */
void
hammer2_chain_setflush(hammer2_trans_t *trans, hammer2_chain_t *chain)
{
	hammer2_chain_t *parent;

	if ((chain->flags & HAMMER2_CHAIN_ONFLUSH) == 0) {
		hammer2_spin_sh(&chain->core.spin);
		while ((chain->flags & HAMMER2_CHAIN_ONFLUSH) == 0) {
			atomic_set_int(&chain->flags, HAMMER2_CHAIN_ONFLUSH);
			if ((parent = chain->parent) == NULL)
				break;
			hammer2_spin_sh(&parent->core.spin);
			hammer2_spin_unsh(&chain->core.spin);
			chain = parent;
		}
		hammer2_spin_unsh(&chain->core.spin);
	}
}

/*
 * Allocate a new disconnected chain element representing the specified
 * bref.  chain->refs is set to 1 and the passed bref is copied to
 * chain->bref.  chain->bytes is derived from the bref.
 *
 * chain->pmp inherits pmp unless the chain is an inode (other than the
 * super-root inode).
 *
 * NOTE: Returns a referenced but unlocked (because there is no core) chain.
 */
hammer2_chain_t *
hammer2_chain_alloc(hammer2_dev_t *hmp, hammer2_pfs_t *pmp,
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
		 * Chain's are really only associated with the hmp but we
		 * maintain a pmp association for per-mount memory tracking
		 * purposes.  The pmp can be NULL.
		 */
		chain = kmalloc(sizeof(*chain), hmp->mchain, M_WAITOK | M_ZERO);
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

	/*
	 * Initialize the new chain structure.  pmp must be set to NULL for
	 * chains belonging to the super-root topology of a device mount.
	 */
	if (pmp == hmp->spmp)
		chain->pmp = NULL;
	else
		chain->pmp = pmp;
	chain->hmp = hmp;
	chain->bref = *bref;
	chain->bytes = bytes;
	chain->refs = 1;
	chain->flags = HAMMER2_CHAIN_ALLOCATED;

	/*
	 * Set the PFS boundary flag if this chain represents a PFS root.
	 */
	if (bref->flags & HAMMER2_BREF_FLAG_PFSROOT)
		chain->flags |= HAMMER2_CHAIN_PFSBOUNDARY;
	hammer2_chain_core_init(chain);

	return (chain);
}

/*
 * Initialize a chain's core structure.  This structure used to be allocated
 * but is now embedded.
 *
 * The core is not locked.  No additional refs on the chain are made.
 * (trans) must not be NULL if (core) is not NULL.
 */
void
hammer2_chain_core_init(hammer2_chain_t *chain)
{
	hammer2_chain_core_t *core = &chain->core;

	/*
	 * Fresh core under nchain (no multi-homing of ochain's
	 * sub-tree).
	 */
	RB_INIT(&core->rbtree);	/* live chains */
	hammer2_mtx_init(&core->lock, "h2chain");
}

/*
 * Add a reference to a chain element, preventing its destruction.
 *
 * (can be called with spinlock held)
 */
void
hammer2_chain_ref(hammer2_chain_t *chain)
{
	atomic_add_int(&chain->refs, 1);
#if 0
	kprintf("REFC %p %d %08x\n", chain, chain->refs - 1, chain->flags);
	print_backtrace(8);
#endif
}

/*
 * Insert the chain in the core rbtree.
 *
 * Normal insertions are placed in the live rbtree.  Insertion of a deleted
 * chain is a special case used by the flush code that is placed on the
 * unstaged deleted list to avoid confusing the live view.
 */
#define HAMMER2_CHAIN_INSERT_SPIN	0x0001
#define HAMMER2_CHAIN_INSERT_LIVE	0x0002
#define HAMMER2_CHAIN_INSERT_RACE	0x0004

static
int
hammer2_chain_insert(hammer2_chain_t *parent, hammer2_chain_t *chain,
		     int flags, int generation)
{
	hammer2_chain_t *xchain;
	int error = 0;

	if (flags & HAMMER2_CHAIN_INSERT_SPIN)
		hammer2_spin_ex(&parent->core.spin);

	/*
	 * Interlocked by spinlock, check for race
	 */
	if ((flags & HAMMER2_CHAIN_INSERT_RACE) &&
	    parent->core.generation != generation) {
		error = EAGAIN;
		goto failed;
	}

	/*
	 * Insert chain
	 */
	xchain = RB_INSERT(hammer2_chain_tree, &parent->core.rbtree, chain);
	KASSERT(xchain == NULL,
		("hammer2_chain_insert: collision %p %p", chain, xchain));
	atomic_set_int(&chain->flags, HAMMER2_CHAIN_ONRBTREE);
	chain->parent = parent;
	++parent->core.chain_count;
	++parent->core.generation;	/* XXX incs for _get() too, XXX */

	/*
	 * We have to keep track of the effective live-view blockref count
	 * so the create code knows when to push an indirect block.
	 */
	if (flags & HAMMER2_CHAIN_INSERT_LIVE)
		atomic_add_int(&parent->core.live_count, 1);
failed:
	if (flags & HAMMER2_CHAIN_INSERT_SPIN)
		hammer2_spin_unex(&parent->core.spin);
	return error;
}

/*
 * Drop the caller's reference to the chain.  When the ref count drops to
 * zero this function will try to disassociate the chain from its parent and
 * deallocate it, then recursely drop the parent using the implied ref
 * from the chain's chain->parent.
 */
static hammer2_chain_t *hammer2_chain_lastdrop(hammer2_chain_t *chain);

void
hammer2_chain_drop(hammer2_chain_t *chain)
{
	u_int refs;
	u_int need = 0;

	if (hammer2_debug & 0x200000)
		Debugger("drop");
#if 0
	kprintf("DROP %p %d %08x\n", chain, chain->refs - 1, chain->flags);
	print_backtrace(8);
#endif

	if (chain->flags & HAMMER2_CHAIN_UPDATE)
		++need;
	if (chain->flags & HAMMER2_CHAIN_MODIFIED)
		++need;
	KKASSERT(chain->refs > need);

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
 * recursive drop or NULL, possibly returning the same chain if the atomic
 * op fails.
 *
 * Whem two chains need to be recursively dropped we use the chain
 * we would otherwise free to placehold the additional chain.  It's a bit
 * convoluted but we can't just recurse without potentially blowing out
 * the kernel stack.
 *
 * The chain cannot be freed if it has any children.
 *
 * The core spinlock is allowed nest child-to-parent (not parent-to-child).
 */
static
hammer2_chain_t *
hammer2_chain_lastdrop(hammer2_chain_t *chain)
{
	hammer2_pfs_t *pmp;
	hammer2_dev_t *hmp;
	hammer2_chain_t *parent;
	hammer2_chain_t *rdrop;

	/*
	 * Spinlock the core and check to see if it is empty.  If it is
	 * not empty we leave chain intact with refs == 0.  The elements
	 * in core->rbtree are associated with other chains contemporary
	 * with ours but not with our chain directly.
	 */
	hammer2_spin_ex(&chain->core.spin);

	/*
	 * We can't free non-stale chains with children until we are
	 * able to free the children because there might be a flush
	 * dependency.  Flushes of stale children (which should also
	 * have their deleted flag set) short-cut recursive flush
	 * dependencies and can be freed here.  Any flushes which run
	 * through stale children due to the flush synchronization
	 * point should have a FLUSH_* bit set in the chain and not
	 * reach lastdrop at this time.
	 *
	 * NOTE: We return (chain) on failure to retry.
	 */
	if (chain->core.chain_count) {
		if (atomic_cmpset_int(&chain->refs, 1, 0)) {
			hammer2_spin_unex(&chain->core.spin);
			chain = NULL;	/* success */
		} else {
			hammer2_spin_unex(&chain->core.spin);
		}
		return(chain);
	}
	/* no chains left under us */

	/*
	 * chain->core has no children left so no accessors can get to our
	 * chain from there.  Now we have to lock the parent core to interlock
	 * remaining possible accessors that might bump chain's refs before
	 * we can safely drop chain's refs with intent to free the chain.
	 */
	hmp = chain->hmp;
	pmp = chain->pmp;	/* can be NULL */
	rdrop = NULL;

	/*
	 * Spinlock the parent and try to drop the last ref on chain.
	 * On success remove chain from its parent, otherwise return NULL.
	 *
	 * (normal core locks are top-down recursive but we define core
	 *  spinlocks as bottom-up recursive, so this is safe).
	 */
	if ((parent = chain->parent) != NULL) {
		hammer2_spin_ex(&parent->core.spin);
		if (atomic_cmpset_int(&chain->refs, 1, 0) == 0) {
			/* 1->0 transition failed */
			hammer2_spin_unex(&parent->core.spin);
			hammer2_spin_unex(&chain->core.spin);
			return(chain);	/* retry */
		}

		/*
		 * 1->0 transition successful, remove chain from its
		 * above core.
		 */
		if (chain->flags & HAMMER2_CHAIN_ONRBTREE) {
			RB_REMOVE(hammer2_chain_tree,
				  &parent->core.rbtree, chain);
			atomic_clear_int(&chain->flags, HAMMER2_CHAIN_ONRBTREE);
			--parent->core.chain_count;
			chain->parent = NULL;
		}

		/*
		 * If our chain was the last chain in the parent's core the
		 * core is now empty and its parent might have to be
		 * re-dropped if it has 0 refs.
		 */
		if (parent->core.chain_count == 0) {
			rdrop = parent;
			if (atomic_cmpset_int(&rdrop->refs, 0, 1) == 0) {
				rdrop = NULL;
			}
		}
		hammer2_spin_unex(&parent->core.spin);
		parent = NULL;	/* safety */
	}

	/*
	 * Successful 1->0 transition and the chain can be destroyed now.
	 *
	 * We still have the core spinlock, and core's chain_count is 0.
	 * Any parent spinlock is gone.
	 */
	hammer2_spin_unex(&chain->core.spin);
	KKASSERT(RB_EMPTY(&chain->core.rbtree) &&
		 chain->core.chain_count == 0);

	/*
	 * All spin locks are gone, finish freeing stuff.
	 */
	KKASSERT((chain->flags & (HAMMER2_CHAIN_UPDATE |
				  HAMMER2_CHAIN_MODIFIED)) == 0);
	hammer2_chain_drop_data(chain, 1);

	KKASSERT(chain->dio == NULL);

	/*
	 * Once chain resources are gone we can use the now dead chain
	 * structure to placehold what might otherwise require a recursive
	 * drop, because we have potentially two things to drop and can only
	 * return one directly.
	 */
	if (chain->flags & HAMMER2_CHAIN_ALLOCATED) {
		chain->flags &= ~HAMMER2_CHAIN_ALLOCATED;
		chain->hmp = NULL;
		kfree(chain, hmp->mchain);
	}

	/*
	 * Possible chaining loop when parent re-drop needed.
	 */
	return(rdrop);
}

/*
 * On either last lock release or last drop
 */
static void
hammer2_chain_drop_data(hammer2_chain_t *chain, int lastdrop)
{
	/*hammer2_dev_t *hmp = chain->hmp;*/

	switch(chain->bref.type) {
	case HAMMER2_BREF_TYPE_VOLUME:
	case HAMMER2_BREF_TYPE_FREEMAP:
		if (lastdrop)
			chain->data = NULL;
		break;
	default:
		KKASSERT(chain->data == NULL);
		break;
	}
}

/*
 * Lock a referenced chain element, acquiring its data with I/O if necessary,
 * and specify how you would like the data to be resolved.
 *
 * If an I/O or other fatal error occurs, chain->error will be set to non-zero.
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
void
hammer2_chain_lock(hammer2_chain_t *chain, int how)
{
	hammer2_dev_t *hmp;
	hammer2_blockref_t *bref;
	hammer2_mtx_state_t ostate;
	char *bdata;
	int error;

	/*
	 * Ref and lock the element.  Recursive locks are allowed.
	 */
	KKASSERT(chain->refs > 0);
	atomic_add_int(&chain->lockcnt, 1);

	hmp = chain->hmp;
	KKASSERT(hmp != NULL);

	/*
	 * Get the appropriate lock.
	 */
	if (how & HAMMER2_RESOLVE_SHARED)
		hammer2_mtx_sh(&chain->core.lock);
	else
		hammer2_mtx_ex(&chain->core.lock);

	/*
	 * If we already have a valid data pointer no further action is
	 * necessary.
	 */
	if (chain->data)
		return;

	/*
	 * Do we have to resolve the data?
	 */
	switch(how & HAMMER2_RESOLVE_MASK) {
	case HAMMER2_RESOLVE_NEVER:
		return;
	case HAMMER2_RESOLVE_MAYBE:
		if (chain->flags & HAMMER2_CHAIN_INITIAL)
			return;
		if (chain->bref.type == HAMMER2_BREF_TYPE_DATA)
			return;
#if 0
		if (chain->bref.type == HAMMER2_BREF_TYPE_FREEMAP_NODE)
			return;
		if (chain->bref.type == HAMMER2_BREF_TYPE_FREEMAP_LEAF)
			return;
#endif
		/* fall through */
	case HAMMER2_RESOLVE_ALWAYS:
		break;
	}

	/*
	 * Upgrade to an exclusive lock so we can safely manipulate the
	 * buffer cache.  If another thread got to it before us we
	 * can just return.
	 */
	ostate = hammer2_mtx_upgrade(&chain->core.lock);
	if (chain->data) {
		hammer2_mtx_downgrade(&chain->core.lock, ostate);
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

	/*
	 * The getblk() optimization can only be used on newly created
	 * elements if the physical block size matches the request.
	 */
	if (chain->flags & HAMMER2_CHAIN_INITIAL) {
		error = hammer2_io_new(hmp, bref->data_off, chain->bytes,
					&chain->dio);
	} else {
		error = hammer2_io_bread(hmp, bref->data_off, chain->bytes,
					 &chain->dio);
		hammer2_adjreadcounter(&chain->bref, chain->bytes);
	}
	if (error) {
		chain->error = HAMMER2_ERROR_IO;
		kprintf("hammer2_chain_lock: I/O error %016jx: %d\n",
			(intmax_t)bref->data_off, error);
		hammer2_io_bqrelse(&chain->dio);
		hammer2_mtx_downgrade(&chain->core.lock, ostate);
		return;
	}
	chain->error = 0;

	/*
	 * NOTE: A locked chain's data cannot be modified without first
	 *	 calling hammer2_chain_modify().
	 */

	/*
	 * Clear INITIAL.  In this case we used io_new() and the buffer has
	 * been zero'd and marked dirty.
	 */
	bdata = hammer2_io_data(chain->dio, chain->bref.data_off);
	if (chain->flags & HAMMER2_CHAIN_INITIAL) {
		atomic_clear_int(&chain->flags, HAMMER2_CHAIN_INITIAL);
		chain->bref.flags |= HAMMER2_BREF_FLAG_ZERO;
	} else if (chain->flags & HAMMER2_CHAIN_MODIFIED) {
		/*
		 * check data not currently synchronized due to
		 * modification.  XXX assumes data stays in the buffer
		 * cache, which might not be true (need biodep on flush
		 * to calculate crc?  or simple crc?).
		 */
	} else {
		if (hammer2_chain_testcheck(chain, bdata) == 0) {
			kprintf("chain %016jx.%02x meth=%02x "
				"CHECK FAIL %08x (flags=%08x)\n",
				chain->bref.data_off,
				chain->bref.type,
				chain->bref.methods,
				hammer2_icrc32(bdata, chain->bytes),
				chain->flags);
			chain->error = HAMMER2_ERROR_CHECK;
		}
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
		break;
	case HAMMER2_BREF_TYPE_INODE:
	case HAMMER2_BREF_TYPE_FREEMAP_LEAF:
	case HAMMER2_BREF_TYPE_INDIRECT:
	case HAMMER2_BREF_TYPE_DATA:
	case HAMMER2_BREF_TYPE_FREEMAP_NODE:
	default:
		/*
		 * Point data at the device buffer and leave dio intact.
		 */
		chain->data = (void *)bdata;
		break;
	}
	hammer2_mtx_downgrade(&chain->core.lock, ostate);
}

/*
 * Unlock and deref a chain element.
 *
 * On the last lock release any non-embedded data (chain->dio) will be
 * retired.
 */
void
hammer2_chain_unlock(hammer2_chain_t *chain)
{
	hammer2_mtx_state_t ostate;
	long *counterp;
	u_int lockcnt;

	/*
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
				hammer2_mtx_unlock(&chain->core.lock);
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
	 * leaving the data/io intact
	 *
	 * Otherwise if lockcnt is still 0 it is possible for it to become
	 * non-zero and race, but since we hold the core->lock exclusively
	 * all that will happen is that the chain will be reloaded after we
	 * unload it.
	 */
	ostate = hammer2_mtx_upgrade(&chain->core.lock);
	if (chain->lockcnt) {
		hammer2_mtx_unlock(&chain->core.lock);
		return;
	}

	/*
	 * Shortcut the case if the data is embedded or not resolved.
	 *
	 * Do NOT NULL out chain->data (e.g. inode data), it might be
	 * dirty.
	 */
	if (chain->dio == NULL) {
		if ((chain->flags & HAMMER2_CHAIN_MODIFIED) == 0)
			hammer2_chain_drop_data(chain, 0);
		hammer2_mtx_unlock(&chain->core.lock);
		return;
	}

	/*
	 * Statistics
	 */
	if (hammer2_io_isdirty(chain->dio) == 0) {
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
	 * Clean out the dio.
	 *
	 * If a device buffer was used for data be sure to destroy the
	 * buffer when we are done to avoid aliases (XXX what about the
	 * underlying VM pages?).
	 *
	 * NOTE: Freemap leaf's use reserved blocks and thus no aliasing
	 *	 is possible.
	 *
	 * NOTE: The isdirty check tracks whether we have to bdwrite() the
	 *	 buffer or not.  The buffer might already be dirty.  The
	 *	 flag is re-set when chain_modify() is called, even if
	 *	 MODIFIED is already set, allowing the OS to retire the
	 *	 buffer independent of a hammer2 flush.
	 */
	chain->data = NULL;
	if ((chain->flags & HAMMER2_CHAIN_IOFLUSH) &&
	    hammer2_io_isdirty(chain->dio)) {
		hammer2_io_bawrite(&chain->dio);
	} else {
		hammer2_io_bqrelse(&chain->dio);
	}
	hammer2_mtx_unlock(&chain->core.lock);
}

/*
 * This counts the number of live blockrefs in a block array and
 * also calculates the point at which all remaining blockrefs are empty.
 * This routine can only be called on a live chain (DUPLICATED flag not set).
 *
 * NOTE: Flag is not set until after the count is complete, allowing
 *	 callers to test the flag without holding the spinlock.
 *
 * NOTE: If base is NULL the related chain is still in the INITIAL
 *	 state and there are no blockrefs to count.
 *
 * NOTE: live_count may already have some counts accumulated due to
 *	 creation and deletion and could even be initially negative.
 */
void
hammer2_chain_countbrefs(hammer2_chain_t *chain,
			 hammer2_blockref_t *base, int count)
{
	hammer2_spin_ex(&chain->core.spin);
        if ((chain->core.flags & HAMMER2_CORE_COUNTEDBREFS) == 0) {
		if (base) {
			while (--count >= 0) {
				if (base[count].type)
					break;
			}
			chain->core.live_zero = count + 1;
			while (count >= 0) {
				if (base[count].type)
					atomic_add_int(&chain->core.live_count,
						       1);
				--count;
			}
		} else {
			chain->core.live_zero = 0;
		}
		/* else do not modify live_count */
		atomic_set_int(&chain->core.flags, HAMMER2_CORE_COUNTEDBREFS);
	}
	hammer2_spin_unex(&chain->core.spin);
}

/*
 * Resize the chain's physical storage allocation in-place.  This function does
 * not adjust the data pointer and must be followed by (typically) a
 * hammer2_chain_modify() call to copy any old data over and adjust the
 * data pointer.
 *
 * Chains can be resized smaller without reallocating the storage.  Resizing
 * larger will reallocate the storage.  Excess or prior storage is reclaimed
 * asynchronously at a later time.
 *
 * Must be passed an exclusively locked parent and chain.
 *
 * This function is mostly used with DATA blocks locked RESOLVE_NEVER in order
 * to avoid instantiating a device buffer that conflicts with the vnode data
 * buffer.  However, because H2 can compress or encrypt data, the chain may
 * have a dio assigned to it in those situations, and they do not conflict.
 *
 * XXX return error if cannot resize.
 */
void
hammer2_chain_resize(hammer2_trans_t *trans, hammer2_inode_t *ip,
		     hammer2_chain_t *parent, hammer2_chain_t *chain,
		     int nradix, int flags)
{
	hammer2_dev_t *hmp;
	size_t obytes;
	size_t nbytes;

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
	chain->data_count += (ssize_t)(nbytes - obytes);

	/*
	 * Make sure the old data is instantiated so we can copy it.  If this
	 * is a data block, the device data may be superfluous since the data
	 * might be in a logical block, but compressed or encrypted data is
	 * another matter.
	 *
	 * NOTE: The modify will set BMAPUPD for us if BMAPPED is set.
	 */
	hammer2_chain_modify(trans, chain, 0);

	/*
	 * Relocate the block, even if making it smaller (because different
	 * block sizes may be in different regions).
	 *
	 * (data blocks only, we aren't copying the storage here).
	 */
	hammer2_freemap_alloc(trans, chain, nbytes);
	chain->bytes = nbytes;
	/*ip->delta_dcount += (ssize_t)(nbytes - obytes);*/ /* XXX atomic */

	/*
	 * We don't want the followup chain_modify() to try to copy data
	 * from the old (wrong-sized) buffer.  It won't know how much to
	 * copy.  This case should only occur during writes when the
	 * originator already has the data to write in-hand.
	 */
	if (chain->dio) {
		KKASSERT(chain->bref.type == HAMMER2_BREF_TYPE_DATA);
		hammer2_io_brelse(&chain->dio);
		chain->data = NULL;
	}
}

void
hammer2_chain_modify(hammer2_trans_t *trans, hammer2_chain_t *chain, int flags)
{
	hammer2_blockref_t obref;
	hammer2_dev_t *hmp;
	hammer2_io_t *dio;
	int error;
	int wasinitial;
	int newmod;
	char *bdata;

	hmp = chain->hmp;
	obref = chain->bref;
	KKASSERT((chain->flags & HAMMER2_CHAIN_FICTITIOUS) == 0);

	/*
	 * Data is not optional for freemap chains (we must always be sure
	 * to copy the data on COW storage allocations).
	 */
	if (chain->bref.type == HAMMER2_BREF_TYPE_FREEMAP_NODE ||
	    chain->bref.type == HAMMER2_BREF_TYPE_FREEMAP_LEAF) {
		KKASSERT((chain->flags & HAMMER2_CHAIN_INITIAL) ||
			 (flags & HAMMER2_MODIFY_OPTDATA) == 0);
	}

	/*
	 * Data must be resolved if already assigned unless explicitly
	 * flagged otherwise.
	 */
	if (chain->data == NULL && (flags & HAMMER2_MODIFY_OPTDATA) == 0 &&
	    (chain->bref.data_off & ~HAMMER2_OFF_MASK_RADIX)) {
		hammer2_chain_lock(chain, HAMMER2_RESOLVE_ALWAYS);
		hammer2_chain_unlock(chain);
	}

	/*
	 * Otherwise do initial-chain handling.  Set MODIFIED to indicate
	 * that the chain has been modified.  Set UPDATE to ensure that
	 * the blockref is updated in the parent.
	 */
	if ((chain->flags & HAMMER2_CHAIN_MODIFIED) == 0) {
		atomic_set_int(&chain->flags, HAMMER2_CHAIN_MODIFIED);
		hammer2_chain_ref(chain);
		hammer2_pfs_memory_inc(chain->pmp);	/* can be NULL */
		newmod = 1;
	} else {
		newmod = 0;
	}
	if ((chain->flags & HAMMER2_CHAIN_UPDATE) == 0) {
		atomic_set_int(&chain->flags, HAMMER2_CHAIN_UPDATE);
		hammer2_chain_ref(chain);
	}

	/*
	 * The modification or re-modification requires an allocation and
	 * possible COW.
	 *
	 * We normally always allocate new storage here.  If storage exists
	 * and MODIFY_NOREALLOC is passed in, we do not allocate new storage.
	 */
	if (chain != &hmp->vchain && chain != &hmp->fchain) {
		if ((chain->bref.data_off & ~HAMMER2_OFF_MASK_RADIX) == 0 ||
		     ((flags & HAMMER2_MODIFY_NOREALLOC) == 0 && newmod)
		) {
			hammer2_freemap_alloc(trans, chain, chain->bytes);
			/* XXX failed allocation */
		}
	}

	/*
	 * Update mirror_tid and modify_tid.  modify_tid is only updated
	 * automatically by this function when used from the frontend.
	 * Flushes and synchronization adjust the flag manually.
	 *
	 * NOTE: chain->pmp could be the device spmp.
	 */
	chain->bref.mirror_tid = hmp->voldata.mirror_tid + 1;
	if (chain->pmp && (trans->flags & (HAMMER2_TRANS_KEEPMODIFY |
					   HAMMER2_TRANS_ISFLUSH)) == 0) {
		chain->bref.modify_tid = chain->pmp->modify_tid + 1;
	}

	/*
	 * Set BMAPUPD to tell the flush code that an existing blockmap entry
	 * requires updating as well as to tell the delete code that the
	 * chain's blockref might not exactly match (in terms of physical size
	 * or block offset) the one in the parent's blocktable.  The base key
	 * of course will still match.
	 */
	if (chain->flags & HAMMER2_CHAIN_BMAPPED)
		atomic_set_int(&chain->flags, HAMMER2_CHAIN_BMAPUPD);

	/*
	 * Short-cut data blocks which the caller does not need an actual
	 * data reference to (aka OPTDATA), as long as the chain does not
	 * already have a data pointer to the data.  This generally means
	 * that the modifications are being done via the logical buffer cache.
	 * The INITIAL flag relates only to the device data buffer and thus
	 * remains unchange in this situation.
	 */
	if (chain->bref.type == HAMMER2_BREF_TYPE_DATA &&
	    (flags & HAMMER2_MODIFY_OPTDATA) &&
	    chain->data == NULL) {
		goto skip2;
	}

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

	/*
	 * Instantiate data buffer and possibly execute COW operation
	 */
	switch(chain->bref.type) {
	case HAMMER2_BREF_TYPE_VOLUME:
	case HAMMER2_BREF_TYPE_FREEMAP:
		/*
		 * The data is embedded, no copy-on-write operation is
		 * needed.
		 */
		KKASSERT(chain->dio == NULL);
		break;
	case HAMMER2_BREF_TYPE_INODE:
	case HAMMER2_BREF_TYPE_FREEMAP_LEAF:
	case HAMMER2_BREF_TYPE_DATA:
	case HAMMER2_BREF_TYPE_INDIRECT:
	case HAMMER2_BREF_TYPE_FREEMAP_NODE:
		/*
		 * Perform the copy-on-write operation
		 *
		 * zero-fill or copy-on-write depending on whether
		 * chain->data exists or not and set the dirty state for
		 * the new buffer.  hammer2_io_new() will handle the
		 * zero-fill.
		 */
		KKASSERT(chain != &hmp->vchain && chain != &hmp->fchain);

		if (wasinitial) {
			error = hammer2_io_new(hmp, chain->bref.data_off,
					       chain->bytes, &dio);
		} else {
			error = hammer2_io_bread(hmp, chain->bref.data_off,
						 chain->bytes, &dio);
		}
		hammer2_adjreadcounter(&chain->bref, chain->bytes);

		/*
		 * If an I/O error occurs make sure callers cannot accidently
		 * modify the old buffer's contents and corrupt the filesystem.
		 */
		if (error) {
			kprintf("hammer2_chain_modify: hmp=%p I/O error\n",
				hmp);
			chain->error = HAMMER2_ERROR_IO;
			hammer2_io_brelse(&dio);
			hammer2_io_brelse(&chain->dio);
			chain->data = NULL;
			break;
		}
		chain->error = 0;
		bdata = hammer2_io_data(dio, chain->bref.data_off);

		if (chain->data) {
			KKASSERT(chain->dio != NULL);
			if (chain->data != (void *)bdata) {
				bcopy(chain->data, bdata, chain->bytes);
			}
		} else if (wasinitial == 0) {
			/*
			 * We have a problem.  We were asked to COW but
			 * we don't have any data to COW with!
			 */
			panic("hammer2_chain_modify: having a COW %p\n",
			      chain);
		}

		/*
		 * Retire the old buffer, replace with the new.  Dirty or
		 * redirty the new buffer.
		 *
		 * WARNING! The system buffer cache may have already flushed
		 *	    the buffer, so we must be sure to [re]dirty it
		 *	    for further modification.
		 */
		if (chain->dio)
			hammer2_io_brelse(&chain->dio);
		chain->data = (void *)bdata;
		chain->dio = dio;
		hammer2_io_setdirty(dio);	/* modified by bcopy above */
		break;
	default:
		panic("hammer2_chain_modify: illegal non-embedded type %d",
		      chain->bref.type);
		break;

	}
skip2:
	/*
	 * setflush on parent indicating that the parent must recurse down
	 * to us.  Do not call on chain itself which might already have it
	 * set.
	 */
	if (chain->parent)
		hammer2_chain_setflush(trans, chain->parent);
}

/*
 * Volume header data locks
 */
void
hammer2_voldata_lock(hammer2_dev_t *hmp)
{
	lockmgr(&hmp->vollk, LK_EXCLUSIVE);
}

void
hammer2_voldata_unlock(hammer2_dev_t *hmp)
{
	lockmgr(&hmp->vollk, LK_RELEASE);
}

void
hammer2_voldata_modify(hammer2_dev_t *hmp)
{
	if ((hmp->vchain.flags & HAMMER2_CHAIN_MODIFIED) == 0) {
		atomic_set_int(&hmp->vchain.flags, HAMMER2_CHAIN_MODIFIED);
		hammer2_chain_ref(&hmp->vchain);
		hammer2_pfs_memory_inc(hmp->vchain.pmp);
	}
}

/*
 * This function returns the chain at the nearest key within the specified
 * range.  The returned chain will be referenced but not locked.
 *
 * This function will recurse through chain->rbtree as necessary and will
 * return a *key_nextp suitable for iteration.  *key_nextp is only set if
 * the iteration value is less than the current value of *key_nextp.
 *
 * The caller should use (*key_nextp) to calculate the actual range of
 * the returned element, which will be (key_beg to *key_nextp - 1), because
 * there might be another element which is superior to the returned element
 * and overlaps it.
 *
 * (*key_nextp) can be passed as key_beg in an iteration only while non-NULL
 * chains continue to be returned.  On EOF (*key_nextp) may overflow since
 * it will wind up being (key_end + 1).
 *
 * WARNING!  Must be called with child's spinlock held.  Spinlock remains
 *	     held through the operation.
 */
struct hammer2_chain_find_info {
	hammer2_chain_t		*best;
	hammer2_key_t		key_beg;
	hammer2_key_t		key_end;
	hammer2_key_t		key_next;
};

static int hammer2_chain_find_cmp(hammer2_chain_t *child, void *data);
static int hammer2_chain_find_callback(hammer2_chain_t *child, void *data);

static
hammer2_chain_t *
hammer2_chain_find(hammer2_chain_t *parent, hammer2_key_t *key_nextp,
			  hammer2_key_t key_beg, hammer2_key_t key_end)
{
	struct hammer2_chain_find_info info;

	info.best = NULL;
	info.key_beg = key_beg;
	info.key_end = key_end;
	info.key_next = *key_nextp;

	RB_SCAN(hammer2_chain_tree, &parent->core.rbtree,
		hammer2_chain_find_cmp, hammer2_chain_find_callback,
		&info);
	*key_nextp = info.key_next;
#if 0
	kprintf("chain_find %p %016jx:%016jx next=%016jx\n",
		parent, key_beg, key_end, *key_nextp);
#endif

	return (info.best);
}

static
int
hammer2_chain_find_cmp(hammer2_chain_t *child, void *data)
{
	struct hammer2_chain_find_info *info = data;
	hammer2_key_t child_beg;
	hammer2_key_t child_end;

	child_beg = child->bref.key;
	child_end = child_beg + ((hammer2_key_t)1 << child->bref.keybits) - 1;

	if (child_end < info->key_beg)
		return(-1);
	if (child_beg > info->key_end)
		return(1);
	return(0);
}

static
int
hammer2_chain_find_callback(hammer2_chain_t *child, void *data)
{
	struct hammer2_chain_find_info *info = data;
	hammer2_chain_t *best;
	hammer2_key_t child_end;

	/*
	 * WARNING! Do not discard DUPLICATED chains, it is possible that
	 *	    we are catching an insertion half-way done.  If a
	 *	    duplicated chain turns out to be the best choice the
	 *	    caller will re-check its flags after locking it.
	 *
	 * WARNING! Layerq is scanned forwards, exact matches should keep
	 *	    the existing info->best.
	 */
	if ((best = info->best) == NULL) {
		/*
		 * No previous best.  Assign best
		 */
		info->best = child;
	} else if (best->bref.key <= info->key_beg &&
		   child->bref.key <= info->key_beg) {
		/*
		 * Illegal overlap.
		 */
		KKASSERT(0);
		/*info->best = child;*/
	} else if (child->bref.key < best->bref.key) {
		/*
		 * Child has a nearer key and best is not flush with key_beg.
		 * Set best to child.  Truncate key_next to the old best key.
		 */
		info->best = child;
		if (info->key_next > best->bref.key || info->key_next == 0)
			info->key_next = best->bref.key;
	} else if (child->bref.key == best->bref.key) {
		/*
		 * If our current best is flush with the child then this
		 * is an illegal overlap.
		 *
		 * key_next will automatically be limited to the smaller of
		 * the two end-points.
		 */
		KKASSERT(0);
		info->best = child;
	} else {
		/*
		 * Keep the current best but truncate key_next to the child's
		 * base.
		 *
		 * key_next will also automatically be limited to the smaller
		 * of the two end-points (probably not necessary for this case
		 * but we do it anyway).
		 */
		if (info->key_next > child->bref.key || info->key_next == 0)
			info->key_next = child->bref.key;
	}

	/*
	 * Always truncate key_next based on child's end-of-range.
	 */
	child_end = child->bref.key + ((hammer2_key_t)1 << child->bref.keybits);
	if (child_end && (info->key_next > child_end || info->key_next == 0))
		info->key_next = child_end;

	return(0);
}

/*
 * Retrieve the specified chain from a media blockref, creating the
 * in-memory chain structure which reflects it.
 *
 * To handle insertion races pass the INSERT_RACE flag along with the
 * generation number of the core.  NULL will be returned if the generation
 * number changes before we have a chance to insert the chain.  Insert
 * races can occur because the parent might be held shared.
 *
 * Caller must hold the parent locked shared or exclusive since we may
 * need the parent's bref array to find our block.
 *
 * WARNING! chain->pmp is always set to NULL for any chain representing
 *	    part of the super-root topology.
 */
hammer2_chain_t *
hammer2_chain_get(hammer2_chain_t *parent, int generation,
		  hammer2_blockref_t *bref)
{
	hammer2_dev_t *hmp = parent->hmp;
	hammer2_chain_t *chain;
	int error;

	/*
	 * Allocate a chain structure representing the existing media
	 * entry.  Resulting chain has one ref and is not locked.
	 */
	if (bref->flags & HAMMER2_BREF_FLAG_PFSROOT)
		chain = hammer2_chain_alloc(hmp, NULL, NULL, bref);
	else
		chain = hammer2_chain_alloc(hmp, parent->pmp, NULL, bref);
	/* ref'd chain returned */

	/*
	 * Flag that the chain is in the parent's blockmap so delete/flush
	 * knows what to do with it.
	 */
	atomic_set_int(&chain->flags, HAMMER2_CHAIN_BMAPPED);

	/*
	 * Link the chain into its parent.  A spinlock is required to safely
	 * access the RBTREE, and it is possible to collide with another
	 * hammer2_chain_get() operation because the caller might only hold
	 * a shared lock on the parent.
	 */
	KKASSERT(parent->refs > 0);
	error = hammer2_chain_insert(parent, chain,
				     HAMMER2_CHAIN_INSERT_SPIN |
				     HAMMER2_CHAIN_INSERT_RACE,
				     generation);
	if (error) {
		KKASSERT((chain->flags & HAMMER2_CHAIN_ONRBTREE) == 0);
		kprintf("chain %p get race\n", chain);
		hammer2_chain_drop(chain);
		chain = NULL;
	} else {
		KKASSERT(chain->flags & HAMMER2_CHAIN_ONRBTREE);
	}

	/*
	 * Return our new chain referenced but not locked, or NULL if
	 * a race occurred.
	 */
	return (chain);
}

/*
 * Lookup initialization/completion API
 */
hammer2_chain_t *
hammer2_chain_lookup_init(hammer2_chain_t *parent, int flags)
{
	hammer2_chain_ref(parent);
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
	if (parent) {
		hammer2_chain_unlock(parent);
		hammer2_chain_drop(parent);
	}
}

static
hammer2_chain_t *
hammer2_chain_getparent(hammer2_chain_t **parentp, int how)
{
	hammer2_chain_t *oparent;
	hammer2_chain_t *nparent;

	/*
	 * Be careful of order, oparent must be unlocked before nparent
	 * is locked below to avoid a deadlock.
	 */
	oparent = *parentp;
	hammer2_spin_ex(&oparent->core.spin);
	nparent = oparent->parent;
	hammer2_chain_ref(nparent);
	hammer2_spin_unex(&oparent->core.spin);
	if (oparent) {
		hammer2_chain_unlock(oparent);
		hammer2_chain_drop(oparent);
		oparent = NULL;
	}

	hammer2_chain_lock(nparent, how);
	*parentp = nparent;

	return (nparent);
}

/*
 * Locate the first chain whos key range overlaps (key_beg, key_end) inclusive.
 * (*parentp) typically points to an inode but can also point to a related
 * indirect block and this function will recurse upwards and find the inode
 * again.
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
 * requested the chain will be returned only referenced.  Note that the
 * parent chain must always be locked shared or exclusive, matching the
 * HAMMER2_LOOKUP_SHARED flag.  We can conceivably lock it SHARED temporarily
 * when NOLOCK is specified but that complicates matters if *parentp must
 * inherit the chain.
 *
 * NOLOCK also implies NODATA, since an unlocked chain usually has a NULL
 * data pointer or can otherwise be in flux.
 *
 * NULL is returned if no match was found, but (*parentp) will still
 * potentially be adjusted.
 *
 * If a fatal error occurs (typically an I/O error), a dummy chain is
 * returned with chain->error and error-identifying information set.  This
 * chain will assert if you try to do anything fancy with it.
 *
 * XXX Depending on where the error occurs we should allow continued iteration.
 *
 * On return (*key_nextp) will point to an iterative value for key_beg.
 * (If NULL is returned (*key_nextp) is set to (key_end + 1)).
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
hammer2_chain_lookup(hammer2_chain_t **parentp, hammer2_key_t *key_nextp,
		     hammer2_key_t key_beg, hammer2_key_t key_end,
		     int *cache_indexp, int flags)
{
	hammer2_dev_t *hmp;
	hammer2_chain_t *parent;
	hammer2_chain_t *chain;
	hammer2_blockref_t *base;
	hammer2_blockref_t *bref;
	hammer2_blockref_t bcopy;
	hammer2_key_t scan_beg;
	hammer2_key_t scan_end;
	int count = 0;
	int how_always = HAMMER2_RESOLVE_ALWAYS;
	int how_maybe = HAMMER2_RESOLVE_MAYBE;
	int how;
	int generation;
	int maxloops = 300000;

	if (flags & HAMMER2_LOOKUP_ALWAYS) {
		how_maybe = how_always;
		how = HAMMER2_RESOLVE_ALWAYS;
	} else if (flags & (HAMMER2_LOOKUP_NODATA | HAMMER2_LOOKUP_NOLOCK)) {
		how = HAMMER2_RESOLVE_NEVER;
	} else {
		how = HAMMER2_RESOLVE_MAYBE;
	}
	if (flags & HAMMER2_LOOKUP_SHARED) {
		how_maybe |= HAMMER2_RESOLVE_SHARED;
		how_always |= HAMMER2_RESOLVE_SHARED;
		how |= HAMMER2_RESOLVE_SHARED;
	}

	/*
	 * Recurse (*parentp) upward if necessary until the parent completely
	 * encloses the key range or we hit the inode.
	 *
	 * This function handles races against the flusher doing a delete-
	 * duplicate above us and re-homes the parent to the duplicate in
	 * that case, otherwise we'd wind up recursing down a stale chain.
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
	if (--maxloops == 0)
		panic("hammer2_chain_lookup: maxloops");
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
			if (flags & HAMMER2_LOOKUP_NODIRECT) {
				chain = NULL;
				*key_nextp = key_end + 1;
				goto done;
			}
			hammer2_chain_ref(parent);
			if ((flags & HAMMER2_LOOKUP_NOLOCK) == 0)
				hammer2_chain_lock(parent, how_always);
			*key_nextp = key_end + 1;
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
				hammer2_chain_ref(chain);
				hammer2_chain_lock(chain, how_maybe);
				*key_nextp = scan_end + 1;
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
		kprintf("hammer2_chain_lookup: unrecognized "
			"blockref(B) type: %d",
			parent->bref.type);
		while (1)
			tsleep(&base, 0, "dead", 0);
		panic("hammer2_chain_lookup: unrecognized "
		      "blockref(B) type: %d",
		      parent->bref.type);
		base = NULL;	/* safety */
		count = 0;	/* safety */
	}

	/*
	 * Merged scan to find next candidate.
	 *
	 * hammer2_base_*() functions require the parent->core.live_* fields
	 * to be synchronized.
	 *
	 * We need to hold the spinlock to access the block array and RB tree
	 * and to interlock chain creation.
	 */
	if ((parent->core.flags & HAMMER2_CORE_COUNTEDBREFS) == 0)
		hammer2_chain_countbrefs(parent, base, count);

	/*
	 * Combined search
	 */
	hammer2_spin_ex(&parent->core.spin);
	chain = hammer2_combined_find(parent, base, count,
				      cache_indexp, key_nextp,
				      key_beg, key_end,
				      &bref);
	generation = parent->core.generation;

	/*
	 * Exhausted parent chain, iterate.
	 */
	if (bref == NULL) {
		hammer2_spin_unex(&parent->core.spin);
		if (key_beg == key_end)	/* short cut single-key case */
			return (NULL);

		/*
		 * Stop if we reached the end of the iteration.
		 */
		if (parent->bref.type != HAMMER2_BREF_TYPE_INDIRECT &&
		    parent->bref.type != HAMMER2_BREF_TYPE_FREEMAP_NODE) {
			return (NULL);
		}

		/*
		 * Calculate next key, stop if we reached the end of the
		 * iteration, otherwise go up one level and loop.
		 */
		key_beg = parent->bref.key +
			  ((hammer2_key_t)1 << parent->bref.keybits);
		if (key_beg == 0 || key_beg > key_end)
			return (NULL);
		parent = hammer2_chain_getparent(parentp, how_maybe);
		goto again;
	}

	/*
	 * Selected from blockref or in-memory chain.
	 */
	if (chain == NULL) {
		bcopy = *bref;
		hammer2_spin_unex(&parent->core.spin);
		chain = hammer2_chain_get(parent, generation,
					  &bcopy);
		if (chain == NULL) {
			kprintf("retry lookup parent %p keys %016jx:%016jx\n",
				parent, key_beg, key_end);
			goto again;
		}
		if (bcmp(&bcopy, bref, sizeof(bcopy))) {
			hammer2_chain_drop(chain);
			goto again;
		}
	} else {
		hammer2_chain_ref(chain);
		hammer2_spin_unex(&parent->core.spin);
	}

	/*
	 * chain is referenced but not locked.  We must lock the chain
	 * to obtain definitive DUPLICATED/DELETED state
	 */
	if (chain->bref.type == HAMMER2_BREF_TYPE_INDIRECT ||
	    chain->bref.type == HAMMER2_BREF_TYPE_FREEMAP_NODE) {
		hammer2_chain_lock(chain, how_maybe);
	} else {
		hammer2_chain_lock(chain, how);
	}

	/*
	 * Skip deleted chains (XXX cache 'i' end-of-block-array? XXX)
	 *
	 * NOTE: Chain's key range is not relevant as there might be
	 *	 one-offs within the range that are not deleted.
	 *
	 * NOTE: Lookups can race delete-duplicate because
	 *	 delete-duplicate does not lock the parent's core
	 *	 (they just use the spinlock on the core).  We must
	 *	 check for races by comparing the DUPLICATED flag before
	 *	 releasing the spinlock with the flag after locking the
	 *	 chain.
	 */
	if (chain->flags & HAMMER2_CHAIN_DELETED) {
		hammer2_chain_unlock(chain);
		hammer2_chain_drop(chain);
		key_beg = *key_nextp;
		if (key_beg == 0 || key_beg > key_end)
			return(NULL);
		goto again;
	}

	/*
	 * If the chain element is an indirect block it becomes the new
	 * parent and we loop on it.  We must maintain our top-down locks
	 * to prevent the flusher from interfering (i.e. doing a
	 * delete-duplicate and leaving us recursing down a deleted chain).
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
		hammer2_chain_drop(parent);
		*parentp = parent = chain;
		goto again;
	}
done:
	/*
	 * All done, return the chain.
	 *
	 * If the caller does not want a locked chain, replace the lock with
	 * a ref.  Perhaps this can eventually be optimized to not obtain the
	 * lock in the first place for situations where the data does not
	 * need to be resolved.
	 */
	if (chain) {
		if (flags & HAMMER2_LOOKUP_NOLOCK)
			hammer2_chain_unlock(chain);
	}

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
 * If a fatal error occurs (typically an I/O error), a dummy chain is
 * returned with chain->error and error-identifying information set.  This
 * chain will assert if you try to do anything fancy with it.
 *
 * XXX Depending on where the error occurs we should allow continued iteration.
 *
 * parent must be locked on entry and remains locked throughout.  chain's
 * lock status must match flags.  Chain is always at least referenced.
 *
 * WARNING!  The MATCHIND flag does not apply to this function.
 */
hammer2_chain_t *
hammer2_chain_next(hammer2_chain_t **parentp, hammer2_chain_t *chain,
		   hammer2_key_t *key_nextp,
		   hammer2_key_t key_beg, hammer2_key_t key_end,
		   int *cache_indexp, int flags)
{
	hammer2_chain_t *parent;
	int how_maybe;

	/*
	 * Calculate locking flags for upward recursion.
	 */
	how_maybe = HAMMER2_RESOLVE_MAYBE;
	if (flags & HAMMER2_LOOKUP_SHARED)
		how_maybe |= HAMMER2_RESOLVE_SHARED;

	parent = *parentp;

	/*
	 * Calculate the next index and recalculate the parent if necessary.
	 */
	if (chain) {
		key_beg = chain->bref.key +
			  ((hammer2_key_t)1 << chain->bref.keybits);
		if ((flags & HAMMER2_LOOKUP_NOLOCK) == 0)
			hammer2_chain_unlock(chain);
		hammer2_chain_drop(chain);

		/*
		 * chain invalid past this point, but we can still do a
		 * pointer comparison w/parent.
		 *
		 * Any scan where the lookup returned degenerate data embedded
		 * in the inode has an invalid index and must terminate.
		 */
		if (chain == parent)
			return(NULL);
		if (key_beg == 0 || key_beg > key_end)
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
		key_beg = parent->bref.key +
			  ((hammer2_key_t)1 << parent->bref.keybits);
		if (key_beg == 0 || key_beg > key_end)
			return (NULL);
		parent = hammer2_chain_getparent(parentp, how_maybe);
	}

	/*
	 * And execute
	 */
	return (hammer2_chain_lookup(parentp, key_nextp,
				     key_beg, key_end,
				     cache_indexp, flags));
}

/*
 * The raw scan function is similar to lookup/next but does not seek to a key.
 * Blockrefs are iterated via first_chain = (parent, NULL) and
 * next_chain = (parent, chain).
 *
 * The passed-in parent must be locked and its data resolved.  The returned
 * chain will be locked.  Pass chain == NULL to acquire the first sub-chain
 * under parent and then iterate with the passed-in chain (which this
 * function will unlock).
 */
hammer2_chain_t *
hammer2_chain_scan(hammer2_chain_t *parent, hammer2_chain_t *chain,
		   int *cache_indexp, int flags)
{
	hammer2_dev_t *hmp;
	hammer2_blockref_t *base;
	hammer2_blockref_t *bref;
	hammer2_blockref_t bcopy;
	hammer2_key_t key;
	hammer2_key_t next_key;
	int count = 0;
	int how_always = HAMMER2_RESOLVE_ALWAYS;
	int how_maybe = HAMMER2_RESOLVE_MAYBE;
	int how;
	int generation;
	int maxloops = 300000;

	hmp = parent->hmp;

	/*
	 * Scan flags borrowed from lookup.
	 */
	if (flags & HAMMER2_LOOKUP_ALWAYS) {
		how_maybe = how_always;
		how = HAMMER2_RESOLVE_ALWAYS;
	} else if (flags & (HAMMER2_LOOKUP_NODATA | HAMMER2_LOOKUP_NOLOCK)) {
		how = HAMMER2_RESOLVE_NEVER;
	} else {
		how = HAMMER2_RESOLVE_MAYBE;
	}
	if (flags & HAMMER2_LOOKUP_SHARED) {
		how_maybe |= HAMMER2_RESOLVE_SHARED;
		how_always |= HAMMER2_RESOLVE_SHARED;
		how |= HAMMER2_RESOLVE_SHARED;
	}

	/*
	 * Calculate key to locate first/next element, unlocking the previous
	 * element as we go.  Be careful, the key calculation can overflow.
	 */
	if (chain) {
		key = chain->bref.key +
		      ((hammer2_key_t)1 << chain->bref.keybits);
		hammer2_chain_unlock(chain);
		hammer2_chain_drop(chain);
		chain = NULL;
		if (key == 0)
			goto done;
	} else {
		key = 0;
	}

again:
	KKASSERT(parent->error == 0);	/* XXX case not handled yet */
	if (--maxloops == 0)
		panic("hammer2_chain_scan: maxloops");
	/*
	 * Locate the blockref array.  Currently we do a fully associative
	 * search through the array.
	 */
	switch(parent->bref.type) {
	case HAMMER2_BREF_TYPE_INODE:
		/*
		 * An inode with embedded data has no sub-chains.
		 */
		if (parent->data->ipdata.op_flags & HAMMER2_OPFLAG_DIRECTDATA)
			goto done;
		base = &parent->data->ipdata.u.blockset.blockref[0];
		count = HAMMER2_SET_COUNT;
		break;
	case HAMMER2_BREF_TYPE_FREEMAP_NODE:
	case HAMMER2_BREF_TYPE_INDIRECT:
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
	 * Merged scan to find next candidate.
	 *
	 * hammer2_base_*() functions require the parent->core.live_* fields
	 * to be synchronized.
	 *
	 * We need to hold the spinlock to access the block array and RB tree
	 * and to interlock chain creation.
	 */
	if ((parent->core.flags & HAMMER2_CORE_COUNTEDBREFS) == 0)
		hammer2_chain_countbrefs(parent, base, count);

	next_key = 0;
	hammer2_spin_ex(&parent->core.spin);
	chain = hammer2_combined_find(parent, base, count,
				      cache_indexp, &next_key,
				      key, HAMMER2_KEY_MAX,
				      &bref);
	generation = parent->core.generation;

	/*
	 * Exhausted parent chain, we're done.
	 */
	if (bref == NULL) {
		hammer2_spin_unex(&parent->core.spin);
		KKASSERT(chain == NULL);
		goto done;
	}

	/*
	 * Selected from blockref or in-memory chain.
	 */
	if (chain == NULL) {
		bcopy = *bref;
		hammer2_spin_unex(&parent->core.spin);
		chain = hammer2_chain_get(parent, generation, &bcopy);
		if (chain == NULL) {
			kprintf("retry scan parent %p keys %016jx\n",
				parent, key);
			goto again;
		}
		if (bcmp(&bcopy, bref, sizeof(bcopy))) {
			hammer2_chain_drop(chain);
			chain = NULL;
			goto again;
		}
	} else {
		hammer2_chain_ref(chain);
		hammer2_spin_unex(&parent->core.spin);
	}

	/*
	 * chain is referenced but not locked.  We must lock the chain
	 * to obtain definitive DUPLICATED/DELETED state
	 */
	hammer2_chain_lock(chain, how);

	/*
	 * Skip deleted chains (XXX cache 'i' end-of-block-array? XXX)
	 *
	 * NOTE: chain's key range is not relevant as there might be
	 *	 one-offs within the range that are not deleted.
	 *
	 * NOTE: XXX this could create problems with scans used in
	 *	 situations other than mount-time recovery.
	 *
	 * NOTE: Lookups can race delete-duplicate because
	 *	 delete-duplicate does not lock the parent's core
	 *	 (they just use the spinlock on the core).  We must
	 *	 check for races by comparing the DUPLICATED flag before
	 *	 releasing the spinlock with the flag after locking the
	 *	 chain.
	 */
	if (chain->flags & HAMMER2_CHAIN_DELETED) {
		hammer2_chain_unlock(chain);
		hammer2_chain_drop(chain);
		chain = NULL;

		key = next_key;
		if (key == 0)
			goto done;
		goto again;
	}

done:
	/*
	 * All done, return the chain or NULL
	 */
	return (chain);
}

/*
 * Create and return a new hammer2 system memory structure of the specified
 * key, type and size and insert it under (*parentp).  This is a full
 * insertion, based on the supplied key/keybits, and may involve creating
 * indirect blocks and moving other chains around via delete/duplicate.
 *
 * THE CALLER MUST HAVE ALREADY PROPERLY SEEKED (*parentp) TO THE INSERTION
 * POINT SANS ANY REQUIRED INDIRECT BLOCK CREATIONS DUE TO THE ARRAY BEING
 * FULL.  This typically means that the caller is creating the chain after
 * doing a hammer2_chain_lookup().
 *
 * (*parentp) must be exclusive locked and may be replaced on return
 * depending on how much work the function had to do.
 *
 * (*parentp) must not be errored or this function will assert.
 *
 * (*chainp) usually starts out NULL and returns the newly created chain,
 * but if the caller desires the caller may allocate a disconnected chain
 * and pass it in instead.
 *
 * This function should NOT be used to insert INDIRECT blocks.  It is
 * typically used to create/insert inodes and data blocks.
 *
 * Caller must pass-in an exclusively locked parent the new chain is to
 * be inserted under, and optionally pass-in a disconnected, exclusively
 * locked chain to insert (else we create a new chain).  The function will
 * adjust (*parentp) as necessary, create or connect the chain, and
 * return an exclusively locked chain in *chainp.
 *
 * When creating a PFSROOT inode under the super-root, pmp is typically NULL
 * and will be reassigned.
 */
int
hammer2_chain_create(hammer2_trans_t *trans, hammer2_chain_t **parentp,
		     hammer2_chain_t **chainp, hammer2_pfs_t *pmp,
		     hammer2_key_t key, int keybits, int type, size_t bytes,
		     int flags)
{
	hammer2_dev_t *hmp;
	hammer2_chain_t *chain;
	hammer2_chain_t *parent;
	hammer2_blockref_t *base;
	hammer2_blockref_t dummy;
	int allocated = 0;
	int error = 0;
	int count;
	int maxloops = 300000;

	/*
	 * Topology may be crossing a PFS boundary.
	 */
	parent = *parentp;
	KKASSERT(hammer2_mtx_owned(&parent->core.lock));
	KKASSERT(parent->error == 0);
	hmp = parent->hmp;
	chain = *chainp;

	if (chain == NULL) {
		/*
		 * First allocate media space and construct the dummy bref,
		 * then allocate the in-memory chain structure.  Set the
		 * INITIAL flag for fresh chains which do not have embedded
		 * data.
		 */
		bzero(&dummy, sizeof(dummy));
		dummy.type = type;
		dummy.key = key;
		dummy.keybits = keybits;
		dummy.data_off = hammer2_getradix(bytes);
		dummy.methods = parent->bref.methods;
		chain = hammer2_chain_alloc(hmp, pmp, trans, &dummy);

		/*
		 * Lock the chain manually, chain_lock will load the chain
		 * which we do NOT want to do.  (note: chain->refs is set
		 * to 1 by chain_alloc() for us, but lockcnt is not).
		 */
		chain->lockcnt = 1;
		hammer2_mtx_ex(&chain->core.lock);
		allocated = 1;

		/*
		 * Set INITIAL to optimize I/O.  The flag will generally be
		 * processed when we call hammer2_chain_modify().
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
			/* fall through */
		case HAMMER2_BREF_TYPE_INODE:
		case HAMMER2_BREF_TYPE_DATA:
		default:
			/*
			 * leave chain->data NULL, set INITIAL
			 */
			KKASSERT(chain->data == NULL);
			atomic_set_int(&chain->flags, HAMMER2_CHAIN_INITIAL);
			break;
		}

		/*
		 * Set statistics for pending updates.  These will be
		 * synchronized by the flush code.
		 */
		switch(type) {
		case HAMMER2_BREF_TYPE_INODE:
			chain->inode_count = 1;
			break;
		case HAMMER2_BREF_TYPE_DATA:
		case HAMMER2_BREF_TYPE_INDIRECT:
			chain->data_count = chain->bytes;
			break;
		}
	} else {
		/*
		 * We are reattaching a previously deleted chain, possibly
		 * under a new parent and possibly with a new key/keybits.
		 * The chain does not have to be in a modified state.  The
		 * UPDATE flag will be set later on in this routine.
		 *
		 * Do NOT mess with the current state of the INITIAL flag.
		 */
		chain->bref.key = key;
		chain->bref.keybits = keybits;
		if (chain->flags & HAMMER2_CHAIN_DELETED)
			atomic_clear_int(&chain->flags, HAMMER2_CHAIN_DELETED);
		KKASSERT(chain->parent == NULL);
	}
	if (flags & HAMMER2_INSERT_PFSROOT)
		chain->bref.flags |= HAMMER2_BREF_FLAG_PFSROOT;
	else
		chain->bref.flags &= ~HAMMER2_BREF_FLAG_PFSROOT;

	/*
	 * Calculate how many entries we have in the blockref array and
	 * determine if an indirect block is required.
	 */
again:
	if (--maxloops == 0)
		panic("hammer2_chain_create: maxloops");

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
		if (parent->flags & HAMMER2_CHAIN_INITIAL)
			base = NULL;
		else
			base = &parent->data->npdata[0];
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
		base = NULL;
		count = 0;
		break;
	}

	/*
	 * Make sure we've counted the brefs
	 */
	if ((parent->core.flags & HAMMER2_CORE_COUNTEDBREFS) == 0)
		hammer2_chain_countbrefs(parent, base, count);

	KKASSERT(parent->core.live_count >= 0 &&
		 parent->core.live_count <= count);

	/*
	 * If no free blockref could be found we must create an indirect
	 * block and move a number of blockrefs into it.  With the parent
	 * locked we can safely lock each child in order to delete+duplicate
	 * it without causing a deadlock.
	 *
	 * This may return the new indirect block or the old parent depending
	 * on where the key falls.  NULL is returned on error.
	 */
	if (parent->core.live_count == count) {
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
			hammer2_chain_drop(parent);
			parent = *parentp = nparent;
		}
		goto again;
	}

	/*
	 * Link the chain into its parent.
	 */
	if (chain->parent != NULL)
		panic("hammer2: hammer2_chain_create: chain already connected");
	KKASSERT(chain->parent == NULL);
	hammer2_chain_insert(parent, chain,
			     HAMMER2_CHAIN_INSERT_SPIN |
			     HAMMER2_CHAIN_INSERT_LIVE,
			     0);

	if (allocated) {
		/*
		 * Mark the newly created chain modified.  This will cause
		 * UPDATE to be set and process the INITIAL flag.
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
		case HAMMER2_BREF_TYPE_FREEMAP_LEAF:
		case HAMMER2_BREF_TYPE_INODE:
			hammer2_chain_modify(trans, chain,
					     HAMMER2_MODIFY_OPTDATA);
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
		 * When reconnecting a chain we must set UPDATE and
		 * setflush so the flush recognizes that it must update
		 * the bref in the parent.
		 */
		if ((chain->flags & HAMMER2_CHAIN_UPDATE) == 0) {
			hammer2_chain_ref(chain);
			atomic_set_int(&chain->flags, HAMMER2_CHAIN_UPDATE);
		}
		if (chain->bref.type == HAMMER2_BREF_TYPE_INODE &&
		    (flags & HAMMER2_INSERT_NOSTATS) == 0) {
			KKASSERT(chain->data);
			chain->inode_count_up +=
				chain->data->ipdata.inode_count;
			chain->data_count_up +=
				chain->data->ipdata.data_count;
		}
	}

	/*
	 * We must setflush(parent) to ensure that it recurses through to
	 * chain.  setflush(chain) might not work because ONFLUSH is possibly
	 * already set in the chain (so it won't recurse up to set it in the
	 * parent).
	 */
	hammer2_chain_setflush(trans, parent);

done:
	*chainp = chain;

	return (error);
}

/*
 * Move the chain from its old parent to a new parent.  The chain must have
 * already been deleted or already disconnected (or never associated) with
 * a parent.  The chain is reassociated with the new parent and the deleted
 * flag will be cleared (no longer deleted).  The chain's modification state
 * is not altered.
 *
 * THE CALLER MUST HAVE ALREADY PROPERLY SEEKED (parent) TO THE INSERTION
 * POINT SANS ANY REQUIRED INDIRECT BLOCK CREATIONS DUE TO THE ARRAY BEING
 * FULL.  This typically means that the caller is creating the chain after
 * doing a hammer2_chain_lookup().
 *
 * A non-NULL bref is typically passed when key and keybits must be overridden.
 * Note that hammer2_cluster_duplicate() *ONLY* uses the key and keybits fields
 * from a passed-in bref and uses the old chain's bref for everything else.
 *
 * Neither (parent) or (chain) can be errored.
 *
 * If (parent) is non-NULL then the new duplicated chain is inserted under
 * the parent.
 *
 * If (parent) is NULL then the newly duplicated chain is not inserted
 * anywhere, similar to if it had just been chain_alloc()'d (suitable for
 * passing into hammer2_chain_create() after this function returns).
 *
 * WARNING! This function calls create which means it can insert indirect
 *	    blocks.  This can cause other unrelated chains in the parent to
 *	    be moved to a newly inserted indirect block in addition to the
 *	    specific chain.
 */
void
hammer2_chain_rename(hammer2_trans_t *trans, hammer2_blockref_t *bref,
		     hammer2_chain_t **parentp, hammer2_chain_t *chain,
		     int flags)
{
	hammer2_dev_t *hmp;
	hammer2_chain_t *parent;
	size_t bytes;

	/*
	 * WARNING!  We should never resolve DATA to device buffers
	 *	     (XXX allow it if the caller did?), and since
	 *	     we currently do not have the logical buffer cache
	 *	     buffer in-hand to fix its cached physical offset
	 *	     we also force the modify code to not COW it. XXX
	 */
	hmp = chain->hmp;
	KKASSERT(chain->parent == NULL);
	KKASSERT(chain->error == 0);

	/*
	 * Now create a duplicate of the chain structure, associating
	 * it with the same core, making it the same size, pointing it
	 * to the same bref (the same media block).
	 */
	if (bref == NULL)
		bref = &chain->bref;
	bytes = (hammer2_off_t)1 <<
		(int)(bref->data_off & HAMMER2_OFF_MASK_RADIX);

	/*
	 * If parent is not NULL the duplicated chain will be entered under
	 * the parent and the UPDATE bit set to tell flush to update
	 * the blockref.
	 *
	 * We must setflush(parent) to ensure that it recurses through to
	 * chain.  setflush(chain) might not work because ONFLUSH is possibly
	 * already set in the chain (so it won't recurse up to set it in the
	 * parent).
	 *
	 * Having both chains locked is extremely important for atomicy.
	 */
	if (parentp && (parent = *parentp) != NULL) {
		KKASSERT(hammer2_mtx_owned(&parent->core.lock));
		KKASSERT(parent->refs > 0);
		KKASSERT(parent->error == 0);

		hammer2_chain_create(trans, parentp, &chain, chain->pmp,
				     bref->key, bref->keybits, bref->type,
				     chain->bytes, flags);
		KKASSERT(chain->flags & HAMMER2_CHAIN_UPDATE);
		hammer2_chain_setflush(trans, *parentp);
	}
}

/*
 * Helper function for deleting chains.
 *
 * The chain is removed from the live view (the RBTREE) as well as the parent's
 * blockmap.  Both chain and its parent must be locked.
 *
 * parent may not be errored.  chain can be errored.
 */
static void
_hammer2_chain_delete_helper(hammer2_trans_t *trans,
			     hammer2_chain_t *parent, hammer2_chain_t *chain,
			     int flags)
{
	hammer2_dev_t *hmp;

	KKASSERT((chain->flags & (HAMMER2_CHAIN_DELETED |
				  HAMMER2_CHAIN_FICTITIOUS)) == 0);
	hmp = chain->hmp;

	if (chain->flags & HAMMER2_CHAIN_BMAPPED) {
		/*
		 * Chain is blockmapped, so there must be a parent.
		 * Atomically remove the chain from the parent and remove
		 * the blockmap entry.
		 */
		hammer2_blockref_t *base;
		int count;

		KKASSERT(parent != NULL);
		KKASSERT(parent->error == 0);
		KKASSERT((parent->flags & HAMMER2_CHAIN_INITIAL) == 0);
		hammer2_chain_modify(trans, parent,
				     HAMMER2_MODIFY_OPTDATA);

		/*
		 * Calculate blockmap pointer
		 */
		KKASSERT(chain->flags & HAMMER2_CHAIN_ONRBTREE);
		hammer2_spin_ex(&parent->core.spin);

		atomic_set_int(&chain->flags, HAMMER2_CHAIN_DELETED);
		atomic_add_int(&parent->core.live_count, -1);
		++parent->core.generation;
		RB_REMOVE(hammer2_chain_tree, &parent->core.rbtree, chain);
		atomic_clear_int(&chain->flags, HAMMER2_CHAIN_ONRBTREE);
		--parent->core.chain_count;
		chain->parent = NULL;

		switch(parent->bref.type) {
		case HAMMER2_BREF_TYPE_INODE:
			/*
			 * Access the inode's block array.  However, there
			 * is no block array if the inode is flagged
			 * DIRECTDATA.  The DIRECTDATA case typicaly only
			 * occurs when a hardlink has been shifted up the
			 * tree and the original inode gets replaced with
			 * an OBJTYPE_HARDLINK placeholding inode.
			 */
			if (parent->data &&
			    (parent->data->ipdata.op_flags &
			     HAMMER2_OPFLAG_DIRECTDATA) == 0) {
				base =
				   &parent->data->ipdata.u.blockset.blockref[0];
			} else {
				base = NULL;
			}
			count = HAMMER2_SET_COUNT;
			break;
		case HAMMER2_BREF_TYPE_INDIRECT:
		case HAMMER2_BREF_TYPE_FREEMAP_NODE:
			if (parent->data)
				base = &parent->data->npdata[0];
			else
				base = NULL;
			count = parent->bytes / sizeof(hammer2_blockref_t);
			break;
		case HAMMER2_BREF_TYPE_VOLUME:
			base = &hmp->voldata.sroot_blockset.blockref[0];
			count = HAMMER2_SET_COUNT;
			break;
		case HAMMER2_BREF_TYPE_FREEMAP:
			base = &parent->data->npdata[0];
			count = HAMMER2_SET_COUNT;
			break;
		default:
			base = NULL;
			count = 0;
			panic("hammer2_flush_pass2: "
			      "unrecognized blockref type: %d",
			      parent->bref.type);
		}

		/*
		 * delete blockmapped chain from its parent.
		 *
		 * The parent is not affected by any statistics in chain
		 * which are pending synchronization.  That is, there is
		 * nothing to undo in the parent since they have not yet
		 * been incorporated into the parent.
		 *
		 * The parent is affected by statistics stored in inodes.
		 * Those have already been synchronized, so they must be
		 * undone.  XXX split update possible w/delete in middle?
		 */
		if (base) {
			if (chain->bref.type == HAMMER2_BREF_TYPE_INODE &&
			    (flags & HAMMER2_DELETE_NOSTATS) == 0) {
				KKASSERT(chain->data != NULL);
				parent->data_count -=
					chain->data->ipdata.data_count;
				parent->inode_count -=
					chain->data->ipdata.inode_count;
			}

			int cache_index = -1;
			hammer2_base_delete(trans, parent, base, count,
					    &cache_index, chain);
		}
		hammer2_spin_unex(&parent->core.spin);
	} else if (chain->flags & HAMMER2_CHAIN_ONRBTREE) {
		/*
		 * Chain is not blockmapped but a parent is present.
		 * Atomically remove the chain from the parent.  There is
		 * no blockmap entry to remove.
		 *
		 * Because chain was associated with a parent but not
		 * synchronized, the chain's *_count_up fields contain
		 * inode adjustment statistics which must be undone.
		 */
		hammer2_spin_ex(&parent->core.spin);
		if (chain->bref.type == HAMMER2_BREF_TYPE_INODE &&
		    (flags & HAMMER2_DELETE_NOSTATS) == 0) {
			KKASSERT(chain->data != NULL);
			chain->data_count_up -=
				chain->data->ipdata.data_count;
			chain->inode_count_up -=
				chain->data->ipdata.inode_count;
		}
		atomic_set_int(&chain->flags, HAMMER2_CHAIN_DELETED);
		atomic_add_int(&parent->core.live_count, -1);
		++parent->core.generation;
		RB_REMOVE(hammer2_chain_tree, &parent->core.rbtree, chain);
		atomic_clear_int(&chain->flags, HAMMER2_CHAIN_ONRBTREE);
		--parent->core.chain_count;
		chain->parent = NULL;
		hammer2_spin_unex(&parent->core.spin);
	} else {
		/*
		 * Chain is not blockmapped and has no parent.  This
		 * is a degenerate case.
		 */
		atomic_set_int(&chain->flags, HAMMER2_CHAIN_DELETED);
	}

#if 0
	/*
	 * If the deletion is permanent (i.e. the chain is not simply being
	 * moved within the topology), adjust the freemap to indicate that
	 * the block *might* be freeable.  bulkfree must still determine
	 * that it is actually freeable.
	 *
	 * We no longer do this in the normal filesystem operations path
	 * as it interferes with the bulkfree algorithm.
	 */
	if ((flags & HAMMER2_DELETE_PERMANENT) &&
	    chain->bref.type != HAMMER2_BREF_TYPE_FREEMAP_NODE &&
	    chain->bref.type != HAMMER2_BREF_TYPE_FREEMAP_LEAF &&
	    (chain->bref.data_off & ~HAMMER2_OFF_MASK_RADIX)) {
		hammer2_freemap_adjust(trans, hmp, &chain->bref,
				       HAMMER2_FREEMAP_DOMAYFREE);
	}
#endif
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
	hammer2_dev_t *hmp;
	hammer2_blockref_t *base;
	hammer2_blockref_t *bref;
	hammer2_blockref_t bcopy;
	hammer2_chain_t *chain;
	hammer2_chain_t *ichain;
	hammer2_chain_t dummy;
	hammer2_key_t key = create_key;
	hammer2_key_t key_beg;
	hammer2_key_t key_end;
	hammer2_key_t key_next;
	int keybits = create_bits;
	int count;
	int nbytes;
	int cache_index;
	int loops;
	int reason;
	int generation;
	int maxloops = 300000;

	/*
	 * Calculate the base blockref pointer or NULL if the chain
	 * is known to be empty.  We need to calculate the array count
	 * for RB lookups either way.
	 */
	hmp = parent->hmp;
	*errorp = 0;
	KKASSERT(hammer2_mtx_owned(&parent->core.lock));

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
	hammer2_chain_lock(ichain, HAMMER2_RESOLVE_MAYBE);
	/* ichain has one ref at this point */

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
	key_beg = 0;
	key_end = HAMMER2_KEY_MAX;
	cache_index = 0;
	hammer2_spin_ex(&parent->core.spin);
	loops = 0;
	reason = 0;

	for (;;) {
		if (++loops > 100000) {
		    hammer2_spin_unex(&parent->core.spin);
		    panic("excessive loops r=%d p=%p base/count %p:%d %016jx\n",
			  reason, parent, base, count, key_next);
		}

		/*
		 * NOTE: spinlock stays intact, returned chain (if not NULL)
		 *	 is not referenced or locked which means that we
		 *	 cannot safely check its flagged / deletion status
		 *	 until we lock it.
		 */
		chain = hammer2_combined_find(parent, base, count,
					      &cache_index, &key_next,
					      key_beg, key_end,
					      &bref);
		generation = parent->core.generation;
		if (bref == NULL)
			break;
		key_next = bref->key + ((hammer2_key_t)1 << bref->keybits);

		/*
		 * Skip keys that are not within the key/radix of the new
		 * indirect block.  They stay in the parent.
		 */
		if ((~(((hammer2_key_t)1 << keybits) - 1) &
		    (key ^ bref->key)) != 0) {
			goto next_key_spinlocked;
		}

		/*
		 * Load the new indirect block by acquiring the related
		 * chains (potentially from media as it might not be
		 * in-memory).  Then move it to the new parent (ichain)
		 * via DELETE-DUPLICATE.
		 *
		 * chain is referenced but not locked.  We must lock the
		 * chain to obtain definitive DUPLICATED/DELETED state
		 */
		if (chain) {
			/*
			 * Use chain already present in the RBTREE
			 */
			hammer2_chain_ref(chain);
			hammer2_spin_unex(&parent->core.spin);
			hammer2_chain_lock(chain, HAMMER2_RESOLVE_NEVER);
		} else {
			/*
			 * Get chain for blockref element.  _get returns NULL
			 * on insertion race.
			 */
			bcopy = *bref;
			hammer2_spin_unex(&parent->core.spin);
			chain = hammer2_chain_get(parent, generation, &bcopy);
			if (chain == NULL) {
				reason = 1;
				hammer2_spin_ex(&parent->core.spin);
				continue;
			}
			if (bcmp(&bcopy, bref, sizeof(bcopy))) {
				kprintf("REASON 2\n");
				reason = 2;
				hammer2_chain_drop(chain);
				hammer2_spin_ex(&parent->core.spin);
				continue;
			}
			hammer2_chain_lock(chain, HAMMER2_RESOLVE_NEVER);
		}

		/*
		 * This is always live so if the chain has been deleted
		 * we raced someone and we have to retry.
		 *
		 * NOTE: Lookups can race delete-duplicate because
		 *	 delete-duplicate does not lock the parent's core
		 *	 (they just use the spinlock on the core).  We must
		 *	 check for races by comparing the DUPLICATED flag before
		 *	 releasing the spinlock with the flag after locking the
		 *	 chain.
		 *
		 *	 (note reversed logic for this one)
		 */
		if (chain->flags & HAMMER2_CHAIN_DELETED) {
			hammer2_chain_unlock(chain);
			hammer2_chain_drop(chain);
			goto next_key;
		}

		/*
		 * Shift the chain to the indirect block.
		 *
		 * WARNING! No reason for us to load chain data, pass NOSTATS
		 *	    to prevent delete/insert from trying to access
		 *	    inode stats (and thus asserting if there is no
		 *	    chain->data loaded).
		 */
		hammer2_chain_delete(trans, parent, chain,
				     HAMMER2_DELETE_NOSTATS);
		hammer2_chain_rename(trans, NULL, &ichain, chain,
				     HAMMER2_INSERT_NOSTATS);
		hammer2_chain_unlock(chain);
		hammer2_chain_drop(chain);
		KKASSERT(parent->refs > 0);
		chain = NULL;
next_key:
		hammer2_spin_ex(&parent->core.spin);
next_key_spinlocked:
		if (--maxloops == 0)
			panic("hammer2_chain_create_indirect: maxloops");
		reason = 4;
		if (key_next == 0 || key_next > key_end)
			break;
		key_beg = key_next;
		/* loop */
	}
	hammer2_spin_unex(&parent->core.spin);

	/*
	 * Insert the new indirect block into the parent now that we've
	 * cleared out some entries in the parent.  We calculated a good
	 * insertion index in the loop above (ichain->index).
	 *
	 * We don't have to set UPDATE here because we mark ichain
	 * modified down below (so the normal modified -> flush -> set-moved
	 * sequence applies).
	 *
	 * The insertion shouldn't race as this is a completely new block
	 * and the parent is locked.
	 */
	KKASSERT((ichain->flags & HAMMER2_CHAIN_ONRBTREE) == 0);
	hammer2_chain_insert(parent, ichain,
			     HAMMER2_CHAIN_INSERT_SPIN |
			     HAMMER2_CHAIN_INSERT_LIVE,
			     0);

	/*
	 * Make sure flushes propogate after our manual insertion.
	 */
	hammer2_chain_setflush(trans, ichain);
	hammer2_chain_setflush(trans, parent);

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
		hammer2_chain_drop(ichain);
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
	hammer2_chain_t *chain;
	hammer2_blockref_t *bref;
	hammer2_key_t key;
	hammer2_key_t key_beg;
	hammer2_key_t key_end;
	hammer2_key_t key_next;
	int cache_index;
	int locount;
	int hicount;
	int maxloops = 300000;

	key = *keyp;
	locount = 0;
	hicount = 0;
	keybits = 64;

	/*
	 * Calculate the range of keys in the array being careful to skip
	 * slots which are overridden with a deletion.
	 */
	key_beg = 0;
	key_end = HAMMER2_KEY_MAX;
	cache_index = 0;
	hammer2_spin_ex(&parent->core.spin);

	for (;;) {
		if (--maxloops == 0) {
			panic("indkey_freemap shit %p %p:%d\n",
			      parent, base, count);
		}
		chain = hammer2_combined_find(parent, base, count,
					      &cache_index, &key_next,
					      key_beg, key_end,
					      &bref);

		/*
		 * Exhausted search
		 */
		if (bref == NULL)
			break;

		/*
		 * Skip deleted chains.
		 */
		if (chain && (chain->flags & HAMMER2_CHAIN_DELETED)) {
			if (key_next == 0 || key_next > key_end)
				break;
			key_beg = key_next;
			continue;
		}

		/*
		 * Use the full live (not deleted) element for the scan
		 * iteration.  HAMMER2 does not allow partial replacements.
		 *
		 * XXX should be built into hammer2_combined_find().
		 */
		key_next = bref->key + ((hammer2_key_t)1 << bref->keybits);

		if (keybits > bref->keybits) {
			key = bref->key;
			keybits = bref->keybits;
		} else if (keybits == bref->keybits && bref->key < key) {
			key = bref->key;
		}
		if (key_next == 0)
			break;
		key_beg = key_next;
	}
	hammer2_spin_unex(&parent->core.spin);

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
	hammer2_blockref_t *bref;
	hammer2_chain_t	*chain;
	hammer2_key_t key_beg;
	hammer2_key_t key_end;
	hammer2_key_t key_next;
	hammer2_key_t key;
	int nkeybits;
	int locount;
	int hicount;
	int cache_index;
	int maxloops = 300000;

	key = *keyp;
	locount = 0;
	hicount = 0;

	/*
	 * Calculate the range of keys in the array being careful to skip
	 * slots which are overridden with a deletion.  Once the scan
	 * completes we will cut the key range in half and shift half the
	 * range into the new indirect block.
	 */
	key_beg = 0;
	key_end = HAMMER2_KEY_MAX;
	cache_index = 0;
	hammer2_spin_ex(&parent->core.spin);

	for (;;) {
		if (--maxloops == 0) {
			panic("indkey_freemap shit %p %p:%d\n",
			      parent, base, count);
		}
		chain = hammer2_combined_find(parent, base, count,
					      &cache_index, &key_next,
					      key_beg, key_end,
					      &bref);

		/*
		 * Exhausted search
		 */
		if (bref == NULL)
			break;

		/*
		 * NOTE: No need to check DUPLICATED here because we do
		 *	 not release the spinlock.
		 */
		if (chain && (chain->flags & HAMMER2_CHAIN_DELETED)) {
			if (key_next == 0 || key_next > key_end)
				break;
			key_beg = key_next;
			continue;
		}

		/*
		 * Use the full live (not deleted) element for the scan
		 * iteration.  HAMMER2 does not allow partial replacements.
		 *
		 * XXX should be built into hammer2_combined_find().
		 */
		key_next = bref->key + ((hammer2_key_t)1 << bref->keybits);

		/*
		 * Expand our calculated key range (key, keybits) to fit
		 * the scanned key.  nkeybits represents the full range
		 * that we will later cut in half (two halves @ nkeybits - 1).
		 */
		nkeybits = keybits;
		if (nkeybits < bref->keybits) {
			if (bref->keybits > 64) {
				kprintf("bad bref chain %p bref %p\n",
					chain, bref);
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
		 * upper half of the (new) key range.
		 */
		if (((hammer2_key_t)1 << (nkeybits - 1)) & bref->key)
			++hicount;
		else
			++locount;

		if (key_next == 0)
			break;
		key_beg = key_next;
	}
	hammer2_spin_unex(&parent->core.spin);
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
 * Sets CHAIN_DELETED and remove the chain's blockref from the parent if
 * it exists.
 *
 * Both parent and chain must be locked exclusively.
 *
 * This function will modify the parent if the blockref requires removal
 * from the parent's block table.
 *
 * This function is NOT recursive.  Any entity already pushed into the
 * chain (such as an inode) may still need visibility into its contents,
 * as well as the ability to read and modify the contents.  For example,
 * for an unlinked file which is still open.
 */
void
hammer2_chain_delete(hammer2_trans_t *trans, hammer2_chain_t *parent,
		     hammer2_chain_t *chain, int flags)
{
	KKASSERT(hammer2_mtx_owned(&chain->core.lock));

	/*
	 * Nothing to do if already marked.
	 *
	 * We need the spinlock on the core whos RBTREE contains chain
	 * to protect against races.
	 */
	if ((chain->flags & HAMMER2_CHAIN_DELETED) == 0) {
		KKASSERT((chain->flags & HAMMER2_CHAIN_DELETED) == 0 &&
			 chain->parent == parent);
		_hammer2_chain_delete_helper(trans, parent, chain, flags);
	}

	/*
	 * NOTE: Special case call to hammer2_flush() for permanent deletions
	 *	 to get rid of the in-memory topology.
	 *
	 *	 XXX not the best way to destroy the sub-topology.
	 */
	if (flags & HAMMER2_DELETE_PERMANENT) {
		atomic_set_int(&chain->flags, HAMMER2_CHAIN_DESTROY);
		hammer2_flush(trans, chain, 1);
	} else {
		/* XXX might not be needed */
		hammer2_chain_setflush(trans, chain);
	}
}

/*
 * Returns the index of the nearest element in the blockref array >= elm.
 * Returns (count) if no element could be found.
 *
 * Sets *key_nextp to the next key for loop purposes but does not modify
 * it if the next key would be higher than the current value of *key_nextp.
 * Note that *key_nexp can overflow to 0, which should be tested by the
 * caller.
 *
 * (*cache_indexp) is a heuristic and can be any value without effecting
 * the result.
 *
 * WARNING!  Must be called with parent's spinlock held.  Spinlock remains
 *	     held through the operation.
 */
static int
hammer2_base_find(hammer2_chain_t *parent,
		  hammer2_blockref_t *base, int count,
		  int *cache_indexp, hammer2_key_t *key_nextp,
		  hammer2_key_t key_beg, hammer2_key_t key_end)
{
	hammer2_blockref_t *scan;
	hammer2_key_t scan_end;
	int i;
	int limit;

	/*
	 * Require the live chain's already have their core's counted
	 * so we can optimize operations.
	 */
        KKASSERT(parent->core.flags & HAMMER2_CORE_COUNTEDBREFS);

	/*
	 * Degenerate case
	 */
	if (count == 0 || base == NULL)
		return(count);

	/*
	 * Sequential optimization using *cache_indexp.  This is the most
	 * likely scenario.
	 *
	 * We can avoid trailing empty entries on live chains, otherwise
	 * we might have to check the whole block array.
	 */
	i = *cache_indexp;
	cpu_ccfence();
	limit = parent->core.live_zero;
	if (i >= limit)
		i = limit - 1;
	if (i < 0)
		i = 0;
	KKASSERT(i < count);

	/*
	 * Search backwards
	 */
	scan = &base[i];
	while (i > 0 && (scan->type == 0 || scan->key > key_beg)) {
		--scan;
		--i;
	}
	*cache_indexp = i;

	/*
	 * Search forwards, stop when we find a scan element which
	 * encloses the key or until we know that there are no further
	 * elements.
	 */
	while (i < count) {
		if (scan->type != 0) {
			scan_end = scan->key +
				   ((hammer2_key_t)1 << scan->keybits) - 1;
			if (scan->key > key_beg || scan_end >= key_beg)
				break;
		}
		if (i >= limit)
			return (count);
		++scan;
		++i;
	}
	if (i != count) {
		*cache_indexp = i;
		if (i >= limit) {
			i = count;
		} else {
			scan_end = scan->key +
				   ((hammer2_key_t)1 << scan->keybits);
			if (scan_end && (*key_nextp > scan_end ||
					 *key_nextp == 0)) {
				*key_nextp = scan_end;
			}
		}
	}
	return (i);
}

/*
 * Do a combined search and return the next match either from the blockref
 * array or from the in-memory chain.  Sets *bresp to the returned bref in
 * both cases, or sets it to NULL if the search exhausted.  Only returns
 * a non-NULL chain if the search matched from the in-memory chain.
 *
 * When no in-memory chain has been found and a non-NULL bref is returned
 * in *bresp.
 *
 *
 * The returned chain is not locked or referenced.  Use the returned bref
 * to determine if the search exhausted or not.  Iterate if the base find
 * is chosen but matches a deleted chain.
 *
 * WARNING!  Must be called with parent's spinlock held.  Spinlock remains
 *	     held through the operation.
 */
static hammer2_chain_t *
hammer2_combined_find(hammer2_chain_t *parent,
		      hammer2_blockref_t *base, int count,
		      int *cache_indexp, hammer2_key_t *key_nextp,
		      hammer2_key_t key_beg, hammer2_key_t key_end,
		      hammer2_blockref_t **bresp)
{
	hammer2_blockref_t *bref;
	hammer2_chain_t *chain;
	int i;

	/*
	 * Lookup in block array and in rbtree.
	 */
	*key_nextp = key_end + 1;
	i = hammer2_base_find(parent, base, count, cache_indexp,
			      key_nextp, key_beg, key_end);
	chain = hammer2_chain_find(parent, key_nextp, key_beg, key_end);

	/*
	 * Neither matched
	 */
	if (i == count && chain == NULL) {
		*bresp = NULL;
		return(NULL);
	}

	/*
	 * Only chain matched.
	 */
	if (i == count) {
		bref = &chain->bref;
		goto found;
	}

	/*
	 * Only blockref matched.
	 */
	if (chain == NULL) {
		bref = &base[i];
		goto found;
	}

	/*
	 * Both in-memory and blockref matched, select the nearer element.
	 *
	 * If both are flush with the left-hand side or both are the
	 * same distance away, select the chain.  In this situation the
	 * chain must have been loaded from the matching blockmap.
	 */
	if ((chain->bref.key <= key_beg && base[i].key <= key_beg) ||
	    chain->bref.key == base[i].key) {
		KKASSERT(chain->bref.key == base[i].key);
		bref = &chain->bref;
		goto found;
	}

	/*
	 * Select the nearer key
	 */
	if (chain->bref.key < base[i].key) {
		bref = &chain->bref;
	} else {
		bref = &base[i];
		chain = NULL;
	}

	/*
	 * If the bref is out of bounds we've exhausted our search.
	 */
found:
	if (bref->key > key_end) {
		*bresp = NULL;
		chain = NULL;
	} else {
		*bresp = bref;
	}
	return(chain);
}

/*
 * Locate the specified block array element and delete it.  The element
 * must exist.
 *
 * The spin lock on the related chain must be held.
 *
 * NOTE: live_count was adjusted when the chain was deleted, so it does not
 *	 need to be adjusted when we commit the media change.
 */
void
hammer2_base_delete(hammer2_trans_t *trans, hammer2_chain_t *parent,
		    hammer2_blockref_t *base, int count,
		    int *cache_indexp, hammer2_chain_t *chain)
{
	hammer2_blockref_t *elm = &chain->bref;
	hammer2_key_t key_next;
	int i;

	/*
	 * Delete element.  Expect the element to exist.
	 *
	 * XXX see caller, flush code not yet sophisticated enough to prevent
	 *     re-flushed in some cases.
	 */
	key_next = 0; /* max range */
	i = hammer2_base_find(parent, base, count, cache_indexp,
			      &key_next, elm->key, elm->key);
	if (i == count || base[i].type == 0 ||
	    base[i].key != elm->key ||
	    ((chain->flags & HAMMER2_CHAIN_BMAPUPD) == 0 &&
	     base[i].keybits != elm->keybits)) {
		hammer2_spin_unex(&parent->core.spin);
		panic("delete base %p element not found at %d/%d elm %p\n",
		      base, i, count, elm);
		return;
	}
	bzero(&base[i], sizeof(*base));

	/*
	 * We can only optimize parent->core.live_zero for live chains.
	 */
	if (parent->core.live_zero == i + 1) {
		while (--i >= 0 && base[i].type == 0)
			;
		parent->core.live_zero = i + 1;
	}

	/*
	 * Clear appropriate blockmap flags in chain.
	 */
	atomic_clear_int(&chain->flags, HAMMER2_CHAIN_BMAPPED |
					HAMMER2_CHAIN_BMAPUPD);
}

/*
 * Insert the specified element.  The block array must not already have the
 * element and must have space available for the insertion.
 *
 * The spin lock on the related chain must be held.
 *
 * NOTE: live_count was adjusted when the chain was deleted, so it does not
 *	 need to be adjusted when we commit the media change.
 */
void
hammer2_base_insert(hammer2_trans_t *trans __unused, hammer2_chain_t *parent,
		    hammer2_blockref_t *base, int count,
		    int *cache_indexp, hammer2_chain_t *chain)
{
	hammer2_blockref_t *elm = &chain->bref;
	hammer2_key_t key_next;
	hammer2_key_t xkey;
	int i;
	int j;
	int k;
	int l;
	int u = 1;

	/*
	 * Insert new element.  Expect the element to not already exist
	 * unless we are replacing it.
	 *
	 * XXX see caller, flush code not yet sophisticated enough to prevent
	 *     re-flushed in some cases.
	 */
	key_next = 0; /* max range */
	i = hammer2_base_find(parent, base, count, cache_indexp,
			      &key_next, elm->key, elm->key);

	/*
	 * Shortcut fill optimization, typical ordered insertion(s) may not
	 * require a search.
	 */
	KKASSERT(i >= 0 && i <= count);

	/*
	 * Set appropriate blockmap flags in chain.
	 */
	atomic_set_int(&chain->flags, HAMMER2_CHAIN_BMAPPED);

	/*
	 * We can only optimize parent->core.live_zero for live chains.
	 */
	if (i == count && parent->core.live_zero < count) {
		i = parent->core.live_zero++;
		base[i] = *elm;
		return;
	}

	xkey = elm->key + ((hammer2_key_t)1 << elm->keybits) - 1;
	if (i != count && (base[i].key < elm->key || xkey >= base[i].key)) {
		hammer2_spin_unex(&parent->core.spin);
		panic("insert base %p overlapping elements at %d elm %p\n",
		      base, i, elm);
	}

	/*
	 * Try to find an empty slot before or after.
	 */
	j = i;
	k = i;
	while (j > 0 || k < count) {
		--j;
		if (j >= 0 && base[j].type == 0) {
			if (j == i - 1) {
				base[j] = *elm;
			} else {
				bcopy(&base[j+1], &base[j],
				      (i - j - 1) * sizeof(*base));
				base[i - 1] = *elm;
			}
			goto validate;
		}
		++k;
		if (k < count && base[k].type == 0) {
			bcopy(&base[i], &base[i+1],
			      (k - i) * sizeof(hammer2_blockref_t));
			base[i] = *elm;

			/*
			 * We can only update parent->core.live_zero for live
			 * chains.
			 */
			if (parent->core.live_zero <= k)
				parent->core.live_zero = k + 1;
			u = 2;
			goto validate;
		}
	}
	panic("hammer2_base_insert: no room!");

	/*
	 * Debugging
	 */
validate:
	key_next = 0;
	for (l = 0; l < count; ++l) {
		if (base[l].type) {
			key_next = base[l].key +
				   ((hammer2_key_t)1 << base[l].keybits) - 1;
			break;
		}
	}
	while (++l < count) {
		if (base[l].type) {
			if (base[l].key <= key_next)
				panic("base_insert %d %d,%d,%d fail %p:%d", u, i, j, k, base, l);
			key_next = base[l].key +
				   ((hammer2_key_t)1 << base[l].keybits) - 1;

		}
	}

}

#if 0

/*
 * Sort the blockref array for the chain.  Used by the flush code to
 * sort the blockref[] array.
 *
 * The chain must be exclusively locked AND spin-locked.
 */
typedef hammer2_blockref_t *hammer2_blockref_p;

static
int
hammer2_base_sort_callback(const void *v1, const void *v2)
{
	hammer2_blockref_p bref1 = *(const hammer2_blockref_p *)v1;
	hammer2_blockref_p bref2 = *(const hammer2_blockref_p *)v2;

	/*
	 * Make sure empty elements are placed at the end of the array
	 */
	if (bref1->type == 0) {
		if (bref2->type == 0)
			return(0);
		return(1);
	} else if (bref2->type == 0) {
		return(-1);
	}

	/*
	 * Sort by key
	 */
	if (bref1->key < bref2->key)
		return(-1);
	if (bref1->key > bref2->key)
		return(1);
	return(0);
}

void
hammer2_base_sort(hammer2_chain_t *chain)
{
	hammer2_blockref_t *base;
	int count;

	switch(chain->bref.type) {
	case HAMMER2_BREF_TYPE_INODE:
		/*
		 * Special shortcut for embedded data returns the inode
		 * itself.  Callers must detect this condition and access
		 * the embedded data (the strategy code does this for us).
		 *
		 * This is only applicable to regular files and softlinks.
		 */
		if (chain->data->ipdata.op_flags & HAMMER2_OPFLAG_DIRECTDATA)
			return;
		base = &chain->data->ipdata.u.blockset.blockref[0];
		count = HAMMER2_SET_COUNT;
		break;
	case HAMMER2_BREF_TYPE_FREEMAP_NODE:
	case HAMMER2_BREF_TYPE_INDIRECT:
		/*
		 * Optimize indirect blocks in the INITIAL state to avoid
		 * I/O.
		 */
		KKASSERT((chain->flags & HAMMER2_CHAIN_INITIAL) == 0);
		base = &chain->data->npdata[0];
		count = chain->bytes / sizeof(hammer2_blockref_t);
		break;
	case HAMMER2_BREF_TYPE_VOLUME:
		base = &chain->hmp->voldata.sroot_blockset.blockref[0];
		count = HAMMER2_SET_COUNT;
		break;
	case HAMMER2_BREF_TYPE_FREEMAP:
		base = &chain->hmp->voldata.freemap_blockset.blockref[0];
		count = HAMMER2_SET_COUNT;
		break;
	default:
		kprintf("hammer2_chain_lookup: unrecognized "
			"blockref(A) type: %d",
		        chain->bref.type);
		while (1)
			tsleep(&base, 0, "dead", 0);
		panic("hammer2_chain_lookup: unrecognized "
		      "blockref(A) type: %d",
		      chain->bref.type);
		base = NULL;	/* safety */
		count = 0;	/* safety */
	}
	kqsort(base, count, sizeof(*base), hammer2_base_sort_callback);
}

#endif

/*
 * Chain memory management
 */
void
hammer2_chain_wait(hammer2_chain_t *chain)
{
	tsleep(chain, 0, "chnflw", 1);
}

const hammer2_media_data_t *
hammer2_chain_rdata(hammer2_chain_t *chain)
{
	KKASSERT(chain->data != NULL);
	return (chain->data);
}

hammer2_media_data_t *
hammer2_chain_wdata(hammer2_chain_t *chain)
{
	KKASSERT(chain->data != NULL);
	return (chain->data);
}

/*
 * Set the check data for a chain.  This can be a heavy-weight operation
 * and typically only runs on-flush.  For file data check data is calculated
 * when the logical buffers are flushed.
 */
void
hammer2_chain_setcheck(hammer2_chain_t *chain, void *bdata)
{
	chain->bref.flags &= ~HAMMER2_BREF_FLAG_ZERO;

	switch(HAMMER2_DEC_CHECK(chain->bref.methods)) {
	case HAMMER2_CHECK_NONE:
		break;
	case HAMMER2_CHECK_DISABLED:
		break;
	case HAMMER2_CHECK_ISCSI32:
		chain->bref.check.iscsi32.value =
			hammer2_icrc32(bdata, chain->bytes);
		break;
	case HAMMER2_CHECK_CRC64:
		chain->bref.check.crc64.value = 0;
		/* XXX */
		break;
	case HAMMER2_CHECK_SHA192:
		{
			SHA256_CTX hash_ctx;
			union {
				uint8_t digest[SHA256_DIGEST_LENGTH];
				uint64_t digest64[SHA256_DIGEST_LENGTH/8];
			} u;

			SHA256_Init(&hash_ctx);
			SHA256_Update(&hash_ctx, bdata, chain->bytes);
			SHA256_Final(u.digest, &hash_ctx);
			u.digest64[2] ^= u.digest64[3];
			bcopy(u.digest,
			      chain->bref.check.sha192.data,
			      sizeof(chain->bref.check.sha192.data));
		}
		break;
	case HAMMER2_CHECK_FREEMAP:
		chain->bref.check.freemap.icrc32 =
			hammer2_icrc32(bdata, chain->bytes);
		break;
	default:
		kprintf("hammer2_chain_setcheck: unknown check type %02x\n",
			chain->bref.methods);
		break;
	}
}

int
hammer2_chain_testcheck(hammer2_chain_t *chain, void *bdata)
{
	int r;

	if (chain->bref.flags & HAMMER2_BREF_FLAG_ZERO)
		return 1;

	switch(HAMMER2_DEC_CHECK(chain->bref.methods)) {
	case HAMMER2_CHECK_NONE:
		r = 1;
		break;
	case HAMMER2_CHECK_DISABLED:
		r = 1;
		break;
	case HAMMER2_CHECK_ISCSI32:
		r = (chain->bref.check.iscsi32.value ==
		     hammer2_icrc32(bdata, chain->bytes));
		break;
	case HAMMER2_CHECK_CRC64:
		r = (chain->bref.check.crc64.value == 0);
		/* XXX */
		break;
	case HAMMER2_CHECK_SHA192:
		{
			SHA256_CTX hash_ctx;
			union {
				uint8_t digest[SHA256_DIGEST_LENGTH];
				uint64_t digest64[SHA256_DIGEST_LENGTH/8];
			} u;

			SHA256_Init(&hash_ctx);
			SHA256_Update(&hash_ctx, bdata, chain->bytes);
			SHA256_Final(u.digest, &hash_ctx);
			u.digest64[2] ^= u.digest64[3];
			if (bcmp(u.digest,
				 chain->bref.check.sha192.data,
			         sizeof(chain->bref.check.sha192.data)) == 0) {
				r = 1;
			} else {
				r = 0;
			}
		}
		break;
	case HAMMER2_CHECK_FREEMAP:
		r = (chain->bref.check.freemap.icrc32 ==
		     hammer2_icrc32(bdata, chain->bytes));
		if (r == 0) {
			kprintf("freemap.icrc %08x icrc32 %08x (%d)\n",
				chain->bref.check.freemap.icrc32,
				hammer2_icrc32(bdata, chain->bytes), chain->bytes);
			if (chain->dio)
				kprintf("dio %p buf %016jx,%d bdata %p/%p\n",
					chain->dio, chain->dio->bp->b_loffset, chain->dio->bp->b_bufsize, bdata, chain->dio->bp->b_data);
		}

		break;
	default:
		kprintf("hammer2_chain_setcheck: unknown check type %02x\n",
			chain->bref.methods);
		r = 1;
		break;
	}
	return r;
}
