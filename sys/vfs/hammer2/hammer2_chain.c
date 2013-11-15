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
 * the hammer2_chain structure.
 *
 * Chains are the in-memory version on media objects (volume header, inodes,
 * indirect blocks, data blocks, etc).  Chains represent a portion of the
 * HAMMER2 topology.
 *
 * A chain is topologically stable once it has been inserted into the
 * in-memory topology.  Modifications which copy, move, or resize the chain
 * are handled via the DELETE-DUPLICATE mechanic where the original chain
 * stays intact but is marked deleted and a new chain is allocated which
 * shares the old chain's children.
 *
 * This sharing is handled via the hammer2_chain_core structure.
 *
 * The DELETE-DUPLICATE mechanism allows the same topological level to contain
 * many overloadings.  However, our RBTREE mechanics require that there be
 * no overlaps so we accomplish the overloading by moving conflicting chains
 * with smaller or equal radii into a sub-RBTREE under the chain being
 * overloaded.
 *
 * DELETE-DUPLICATE is also used when a modification to a chain crosses a
 * flush synchronization boundary, allowing the flush code to continue flushing
 * the older version of the topology and not be disrupted by new frontend
 * operations.
 *
 *				LIVE VS FLUSH VIEW
 *
 * All lookup and iterate operations and most modifications are done on the
 * live view.  During flushes lookups are not normally done and modifications
 * may be run on the flush view.  However, flushes often needs to allocate
 * blocks and the freemap_alloc/free code issues lookups.  This code is
 * special cased to use the live view when called from a flush.
 *
 * General chain lookup/iteration functions are NOT aware of the flush view,
 * they only know about live views.
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
static hammer2_chain_t *hammer2_combined_find(
		hammer2_chain_t *parent,
		hammer2_blockref_t *base, int count,
		int *cache_indexp, hammer2_key_t *key_nextp,
		hammer2_key_t key_beg, hammer2_key_t key_end,
		hammer2_blockref_t **bresp);

/*
 * Basic RBTree for chains.  Chains cannot overlap within any given
 * core->rbtree without recursing through chain->rbtree.  We effectively
 * guarantee this by checking the full range rather than just the first
 * key element.  By matching on the full range callers can detect when
 * recursrion through chain->rbtree is needed.
 *
 * NOTE: This also means the a delete-duplicate on the same key will
 *	 overload by placing the deleted element in the new element's
 *	 chain->rbtree (when doing a direct replacement).
 */
RB_GENERATE(hammer2_chain_tree, hammer2_chain, rbnode, hammer2_chain_cmp);

int
hammer2_chain_cmp(hammer2_chain_t *chain1, hammer2_chain_t *chain2)
{
	hammer2_key_t c1_beg;
	hammer2_key_t c1_end;
	hammer2_key_t c2_beg;
	hammer2_key_t c2_end;

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
 * Recursively set the update_hi flag up to the root starting at chain's
 * parent->core.  update_hi is not set in chain's core.
 *
 * This controls top-down visibility for flushes.  The child has just one
 * 'above' core, but the core itself can be multi-homed with parents iterated
 * via core->ownerq.
 *
 * This function is not used during a flush (except when the flush is
 * allocating which requires the live tree).  The flush keeps track of its
 * recursion itself.
 *
 * XXX needs to be optimized to use roll-up TIDs.  update_hi is only really
 * compared against bref.mirror_tid which itself is only updated by a flush.
 */
void
hammer2_chain_setsubmod(hammer2_trans_t *trans, hammer2_chain_t *chain)
{
	hammer2_chain_core_t *above;

	while ((above = chain->above) != NULL) {
		spin_lock(&above->cst.spin);
		/* XXX optimize */
		if (above->update_hi < trans->sync_tid)
			above->update_hi = trans->sync_tid;
		chain = TAILQ_LAST(&above->ownerq, h2_core_list);
#if 0
		TAILQ_FOREACH_REVERSE(chain, &above->ownerq,
				      h2_core_list, core_entry) {
			if (trans->sync_tid >= chain->modify_tid &&
			    trans->sync_tid <= chain->delete_tid) {
				break;
			}
		}
#endif
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
		 * Chain's are really only associated with the hmp but we
		 * maintain a pmp association for per-mount memory tracking
		 * purposes.  The pmp can be NULL.
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
	chain->bytes = bytes;
	chain->refs = 1;
	chain->flags = HAMMER2_CHAIN_ALLOCATED;
	chain->delete_tid = HAMMER2_MAX_TID;

	/*
	 * Set modify_tid if a transaction is creating the chain.  When
	 * loading a chain from backing store trans is passed as NULL and
	 * modify_tid is left set to 0.
	 */
	if (trans)
		chain->modify_tid = trans->sync_tid;

	return (chain);
}

/*
 * Associate an existing core with the chain or allocate a new core.
 *
 * The core is not locked.  No additional refs on the chain are made.
 * (trans) must not be NULL if (core) is not NULL.
 *
 * When chains are delete-duplicated during flushes we insert nchain on
 * the ownerq after ochain instead of at the end in order to give the
 * drop code visibility in the correct order, otherwise drops can be missed.
 */
void
hammer2_chain_core_alloc(hammer2_trans_t *trans,
			 hammer2_chain_t *nchain, hammer2_chain_t *ochain)
{
	hammer2_chain_core_t *core;

	KKASSERT(nchain->core == NULL);

	if (ochain == NULL) {
		/*
		 * Fresh core under nchain (no multi-homing of ochain's
		 * sub-tree).
		 */
		core = kmalloc(sizeof(*core), nchain->hmp->mchain,
			       M_WAITOK | M_ZERO);
		TAILQ_INIT(&core->layerq);
		TAILQ_INIT(&core->ownerq);
		core->sharecnt = 1;
		core->good = 0x1234;
		if (trans)
			core->update_hi = trans->sync_tid;
		else
			core->update_hi = nchain->bref.mirror_tid;
		nchain->core = core;
		ccms_cst_init(&core->cst, nchain);
		TAILQ_INSERT_TAIL(&core->ownerq, nchain, core_entry);
	} else {
		/*
		 * Propagate the PFSROOT flag which we set on all subdirs
		 * under the super-root.
		 */
		atomic_set_int(&nchain->flags,
			       ochain->flags & HAMMER2_CHAIN_PFSROOT);

		/*
		 * Duplicating ochain -> nchain.  Set the DUPLICATED flag on
		 * ochain if nchain is not a snapshot.
		 *
		 * It is possible for the DUPLICATED flag to already be
		 * set when called via a flush operation because flush
		 * operations may have to work on elements with delete_tid's
		 * beyond the flush sync_tid.  In this situation we must
		 * ensure that nchain is placed just after ochain in the
		 * ownerq and that the DUPLICATED flag is set on nchain so
		 * 'live' operations skip past it to the correct chain.
		 *
		 * The flusher understands the blockref synchronization state
		 * for any stale chains by observing bref.mirror_tid, which
		 * delete-duplicate replicates.
		 *
		 * WARNING! However, the case is disallowed when the flusher
		 *	    is allocating freemap space because this entails
		 *	    more than just adjusting a block table.
		 */
		if (ochain->flags & HAMMER2_CHAIN_DUPLICATED) {
			KKASSERT((trans->flags &
				  (HAMMER2_TRANS_ISFLUSH |
				   HAMMER2_TRANS_ISALLOCATING)) ==
			         HAMMER2_TRANS_ISFLUSH);
			atomic_set_int(&nchain->flags,
				       HAMMER2_CHAIN_DUPLICATED);
		}
		if ((nchain->flags & HAMMER2_CHAIN_SNAPSHOT) == 0) {
			atomic_set_int(&ochain->flags,
				       HAMMER2_CHAIN_DUPLICATED);
		}
		core = ochain->core;
		atomic_add_int(&core->sharecnt, 1);

		spin_lock(&core->cst.spin);
		nchain->core = core;

#if 0
		if (core->update_hi < trans->sync_tid)
			core->update_hi = trans->sync_tid;
#endif

		/*
		 * Maintain ordering for refactor test so we don't skip over
		 * a snapshot.  Also, during flushes, delete-duplications
		 * for block-table updates can occur on blocks already
		 * deleted (delete-duplicated by a later transaction).  We
		 * must insert nchain after ochain but before the later
		 * transaction's copy.
		 */
		TAILQ_INSERT_AFTER(&core->ownerq, ochain, nchain, core_entry);

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
 * Insert the chain in the core rbtree at the first layer
 * which accepts it (for now we don't sort layers by the transaction tid)
 */
#define HAMMER2_CHAIN_INSERT_SPIN	0x0001
#define HAMMER2_CHAIN_INSERT_LIVE	0x0002
#define HAMMER2_CHAIN_INSERT_RACE	0x0004

static
int
hammer2_chain_insert(hammer2_chain_core_t *above, hammer2_chain_layer_t *layer,
		     hammer2_chain_t *chain, int flags, int generation)
{
	hammer2_chain_t *xchain;
	hammer2_chain_layer_t *nlayer;
	int error = 0;

	if (flags & HAMMER2_CHAIN_INSERT_SPIN)
		spin_lock(&above->cst.spin);
        chain->above = above;

	/*
	 * Special case, place the chain in the next most-recent layer as the
	 * specified layer, inserting a layer inbetween if necessary.
	 */
	if (layer) {
		KKASSERT((flags & HAMMER2_CHAIN_INSERT_RACE) == 0);
		nlayer = TAILQ_PREV(layer, h2_layer_list, entry);
		if (nlayer && RB_INSERT(hammer2_chain_tree,
					&nlayer->rbtree, chain) == NULL) {
			layer = nlayer;
			goto done;
		}

		spin_unlock(&above->cst.spin);
		KKASSERT((flags & HAMMER2_CHAIN_INSERT_LIVE) == 0);
		nlayer = kmalloc(sizeof(*nlayer), chain->hmp->mchain,
				 M_WAITOK | M_ZERO);
		RB_INIT(&nlayer->rbtree);
		nlayer->good = 0xABCD;
		spin_lock(&above->cst.spin);

		TAILQ_INSERT_BEFORE(layer, nlayer, entry);
		RB_INSERT(hammer2_chain_tree, &nlayer->rbtree, chain);
		layer = nlayer;
		goto done;
	}

	/*
	 * Interlocked by spinlock, check for race
	 */
	if ((flags & HAMMER2_CHAIN_INSERT_RACE) &&
	    above->generation != generation) {
		error = EAGAIN;
		goto failed;
	}

	/*
	 * Try to insert, allocate a new layer if a nominal collision
	 * occurs (a collision is different from a SMP race).
	 */
	layer = TAILQ_FIRST(&above->layerq);
	xchain = NULL;

	if (layer == NULL ||
	    (xchain = RB_INSERT(hammer2_chain_tree,
				&layer->rbtree, chain)) != NULL) {

		/*
		 * Allocate a new layer to resolve the issue.
		 */
		spin_unlock(&above->cst.spin);
		layer = kmalloc(sizeof(*layer), chain->hmp->mchain,
				M_WAITOK | M_ZERO);
		RB_INIT(&layer->rbtree);
		layer->good = 0xABCD;
		spin_lock(&above->cst.spin);

		if ((flags & HAMMER2_CHAIN_INSERT_RACE) &&
		    above->generation != generation) {
			spin_unlock(&above->cst.spin);
			kfree(layer, chain->hmp->mchain);
			spin_lock(&above->cst.spin);
			error = EAGAIN;
			goto failed;
		}

		TAILQ_INSERT_HEAD(&above->layerq, layer, entry);
		RB_INSERT(hammer2_chain_tree, &layer->rbtree, chain);
	}
done:
	chain->inlayer = layer;
	++above->chain_count;
	++above->generation;

	if ((flags & HAMMER2_CHAIN_INSERT_LIVE) &&
	    (chain->flags & HAMMER2_CHAIN_DELETED) == 0) {
		atomic_add_int(&above->live_count, 1);
	}
	atomic_set_int(&chain->flags, HAMMER2_CHAIN_ONRBTREE);
failed:
	if (flags & HAMMER2_CHAIN_INSERT_SPIN)
		spin_unlock(&above->cst.spin);
	return error;
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
static hammer2_chain_t *hammer2_chain_lastdrop(hammer2_chain_t *chain,
					       struct h2_core_list *delayq);

void
hammer2_chain_drop(hammer2_chain_t *chain)
{
	struct h2_core_list delayq;
	hammer2_chain_t *scan;
	u_int refs;
	u_int need = 0;

	if (hammer2_debug & 0x200000)
		Debugger("drop");

	if (chain->flags & HAMMER2_CHAIN_MOVED)
		++need;
	if (chain->flags & HAMMER2_CHAIN_MODIFIED)
		++need;
	KKASSERT(chain->refs > need);

	TAILQ_INIT(&delayq);

	while (chain) {
		refs = chain->refs;
		cpu_ccfence();
		KKASSERT(refs > 0);

		if (refs == 1) {
			chain = hammer2_chain_lastdrop(chain, &delayq);
		} else {
			if (atomic_cmpset_int(&chain->refs, refs, refs - 1))
				break;
			/* retry the same chain */
		}

		/*
		 * When we've exhausted lastdrop chaining pull off of delayq.
		 * chains on delayq are dead but are used to placehold other
		 * chains which we added a ref to for the purpose of dropping.
		 */
		if (chain == NULL) {
			hammer2_mount_t *hmp;

			if ((scan = TAILQ_FIRST(&delayq)) != NULL) {
				chain = (void *)scan->data;
				TAILQ_REMOVE(&delayq, scan, core_entry);
				scan->flags &= ~HAMMER2_CHAIN_ALLOCATED;
				hmp = scan->hmp;
				scan->hmp = NULL;
				kfree(scan, hmp->mchain);
			}
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
 * The cst spinlock is allowed nest child-to-parent (not parent-to-child).
 */
static
hammer2_chain_t *
hammer2_chain_lastdrop(hammer2_chain_t *chain, struct h2_core_list *delayq)
{
	hammer2_pfsmount_t *pmp;
	hammer2_mount_t *hmp;
	hammer2_chain_core_t *above;
	hammer2_chain_core_t *core;
	hammer2_chain_layer_t *layer;
	hammer2_chain_t *rdrop1;
	hammer2_chain_t *rdrop2;

	/*
	 * Spinlock the core and check to see if it is empty.  If it is
	 * not empty we leave chain intact with refs == 0.  The elements
	 * in core->rbtree are associated with other chains contemporary
	 * with ours but not with our chain directly.
	 */
	if ((core = chain->core) != NULL) {
		spin_lock(&core->cst.spin);

		/*
		 * We can't free chains with children because there might
		 * be a flush dependency.
		 *
		 * NOTE: We return (chain) on failure to retry.
		 */
		if (core->chain_count) {
			if (atomic_cmpset_int(&chain->refs, 1, 0))
				chain = NULL;	/* success */
			spin_unlock(&core->cst.spin);
			return(chain);
		}
		/* no chains left under us */

		/*
		 * Because various parts of the code, including the inode
		 * structure, might be holding a stale chain and need to
		 * iterate to a non-stale sibling, we cannot remove siblings
		 * unless they are at the head of chain.
		 *
		 * We can't free a live chain unless it is a the head
		 * of its ownerq.  If we were to then the go-to chain
		 * would revert to the prior deleted chain.
		 */
		if (TAILQ_FIRST(&core->ownerq) != chain) {
			if (atomic_cmpset_int(&chain->refs, 1, 0))
				chain = NULL;	/* success */
			spin_unlock(&core->cst.spin);
			return(chain);
		}
	}

	/*
	 * chain->core has no children left so no accessors can get to our
	 * chain from there.  Now we have to lock the above core to interlock
	 * remaining possible accessors that might bump chain's refs before
	 * we can safely drop chain's refs with intent to free the chain.
	 */
	hmp = chain->hmp;
	pmp = chain->pmp;	/* can be NULL */
	rdrop1 = NULL;
	rdrop2 = NULL;
	layer = NULL;

	/*
	 * Spinlock the parent and try to drop the last ref on chain.
	 * On success remove chain from its parent, otherwise return NULL.
	 *
	 * (normal core locks are top-down recursive but we define core
	 *  spinlocks as bottom-up recursive, so this is safe).
	 */
	if ((above = chain->above) != NULL) {
		spin_lock(&above->cst.spin);
		if (atomic_cmpset_int(&chain->refs, 1, 0) == 0) {
			/* 1->0 transition failed */
			spin_unlock(&above->cst.spin);
			if (core)
				spin_unlock(&core->cst.spin);
			return(chain);	/* retry */
		}

		/*
		 * 1->0 transition successful, remove chain from its
		 * above core.  Track layer for removal/freeing.
		 */
		KKASSERT(chain->flags & HAMMER2_CHAIN_ONRBTREE);
		layer = chain->inlayer;
		RB_REMOVE(hammer2_chain_tree, &layer->rbtree, chain);
		--above->chain_count;
		atomic_clear_int(&chain->flags, HAMMER2_CHAIN_ONRBTREE);
		chain->above = NULL;
		chain->inlayer = NULL;

		if (RB_EMPTY(&layer->rbtree) && layer->refs == 0) {
			TAILQ_REMOVE(&above->layerq, layer, entry);
		} else {
			layer = NULL;
		}

		/*
		 * If our chain was the last chain in the parent's core the
		 * core is now empty and its parents might now be droppable.
		 * Try to drop the first multi-homed parent by gaining a
		 * ref on it here and then dropping it below.
		 */
		if (above->chain_count == 0) {
			rdrop1 = TAILQ_FIRST(&above->ownerq);
			if (rdrop1 &&
			    atomic_cmpset_int(&rdrop1->refs, 0, 1) == 0) {
				rdrop1 = NULL;
			}
		}
		spin_unlock(&above->cst.spin);
		above = NULL;	/* safety */
	}

	/*
	 * Successful 1->0 transition and the chain can be destroyed now.
	 *
	 * We still have the core spinlock (if core is non-NULL), and core's
	 * chain_count is 0.  The above spinlock is gone.
	 *
	 * Remove chain from ownerq.  Once core has no more owners (and no
	 * children which is already the case) we can destroy core.
	 *
	 * If core has more owners we may be able to continue a bottom-up
	 * drop with our next sibling.
	 */
	if (core) {
		chain->core = NULL;

		TAILQ_REMOVE(&core->ownerq, chain, core_entry);
		rdrop2 = TAILQ_FIRST(&core->ownerq);
		if (rdrop2 && atomic_cmpset_int(&rdrop2->refs, 0, 1) == 0)
			rdrop2 = NULL;
		spin_unlock(&core->cst.spin);

		/*
		 * We can do the final 1->0 transition with an atomic op
		 * after releasing core's spinlock.
		 */
		if (atomic_fetchadd_int(&core->sharecnt, -1) == 1) {
			/*
			 * On the 1->0 transition of core we can destroy
			 * it.  Any remaining layers should no longer be
			 * referenced or visibile to other threads.
			 */
			KKASSERT(TAILQ_EMPTY(&core->ownerq));
			if (layer) {
				layer->good = 0xEF00;
				kfree(layer, hmp->mchain);
			}
			while ((layer = TAILQ_FIRST(&core->layerq)) != NULL) {
				KKASSERT(layer->refs == 0 &&
					 RB_EMPTY(&layer->rbtree));
				TAILQ_REMOVE(&core->layerq, layer, entry);
				layer->good = 0xEF01;
				kfree(layer, hmp->mchain);
			}
			/* layer now NULL */
			KKASSERT(core->cst.count == 0);
			KKASSERT(core->cst.upgrade == 0);
			core->good = 0x5678;
			kfree(core, hmp->mchain);
		}
		core = NULL;	/* safety */
	}

	/*
	 * All spin locks are gone, finish freeing stuff.
	 */
	KKASSERT((chain->flags & (HAMMER2_CHAIN_MOVED |
				  HAMMER2_CHAIN_MODIFIED)) == 0);
	hammer2_chain_drop_data(chain, 1);

	KKASSERT(chain->dio == NULL);

	/*
	 * Free saved empty layer and return chained drop.
	 */
	if (layer) {
		layer->good = 0xEF02;
		kfree(layer, hmp->mchain);
	}

	/*
	 * Once chain resources are gone we can use the now dead chain
	 * structure to placehold what might otherwise require a recursive
	 * drop, because we have potentially two things to drop and can only
	 * return one directly.
	 */
	if (rdrop1 && rdrop2) {
		KKASSERT(chain->flags & HAMMER2_CHAIN_ALLOCATED);
		chain->data = (void *)rdrop1;
		TAILQ_INSERT_TAIL(delayq, chain, core_entry);
		rdrop1 = NULL;
	} else if (chain->flags & HAMMER2_CHAIN_ALLOCATED) {
		chain->flags &= ~HAMMER2_CHAIN_ALLOCATED;
		chain->hmp = NULL;
		kfree(chain, hmp->mchain);
	}
	if (pmp) {
		atomic_add_long(&pmp->inmem_chains, -1);
		hammer2_chain_memory_wakeup(pmp);
	}

	/*
	 * Either or both can be NULL.  We already handled the case where
	 * both might not have been NULL.
	 */
	if (rdrop1)
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
	ccms_state_t ostate;
	char *bdata;
	int error;

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
		adjreadcounter(&chain->bref, chain->bytes);
	}

	if (error) {
		kprintf("hammer2_chain_lock: I/O error %016jx: %d\n",
			(intmax_t)bref->data_off, error);
		hammer2_io_bqrelse(&chain->dio);
		ccms_thread_lock_downgrade(&core->cst, ostate);
		return (error);
	}

	/*
	 * We can clear the INITIAL state now, we've resolved the buffer
	 * to zeros and marked it dirty with hammer2_io_new().
	 */
	bdata = hammer2_io_data(chain->dio, chain->bref.data_off);
	if (chain->flags & HAMMER2_CHAIN_INITIAL) {
		atomic_clear_int(&chain->flags, HAMMER2_CHAIN_INITIAL);
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
		/*
		 * Copy data from dio to embedded buffer, do not retain the
		 * device buffer.
		 */
		KKASSERT(chain->bytes == sizeof(chain->data->ipdata));
		atomic_set_int(&chain->flags, HAMMER2_CHAIN_EMBEDDED);
		chain->data = kmalloc(sizeof(chain->data->ipdata),
				      hmp->mchain, M_WAITOK | M_ZERO);
		bcopy(bdata, &chain->data->ipdata, chain->bytes);
		hammer2_io_bqrelse(&chain->dio);
		break;
	case HAMMER2_BREF_TYPE_FREEMAP_LEAF:
		KKASSERT(chain->bytes == sizeof(chain->data->bmdata));
		atomic_set_int(&chain->flags, HAMMER2_CHAIN_EMBEDDED);
		chain->data = kmalloc(sizeof(chain->data->bmdata),
				      hmp->mchain, M_WAITOK | M_ZERO);
		bcopy(bdata, &chain->data->bmdata, chain->bytes);
		hammer2_io_bqrelse(&chain->dio);
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
	ccms_thread_lock_downgrade(&core->cst, ostate);
	return (0);
}

/*
 * This basically calls hammer2_io_breadcb() but does some pre-processing
 * of the chain first to handle certain cases.
 */
void
hammer2_chain_load_async(hammer2_chain_t *chain,
			 void (*callback)(hammer2_io_t *dio,
					  hammer2_chain_t *chain,
					  void *arg_p, off_t arg_o),
			 void *arg_p, off_t arg_o)
{
	hammer2_mount_t *hmp;
	struct hammer2_io *dio;
	hammer2_blockref_t *bref;
	int error;

	if (chain->data) {
		callback(NULL, chain, arg_p, arg_o);
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
	hmp = chain->hmp;

	/*
	 * The getblk() optimization can only be used on newly created
	 * elements if the physical block size matches the request.
	 */
	if ((chain->flags & HAMMER2_CHAIN_INITIAL) &&
	    chain->bytes == hammer2_devblksize(chain->bytes)) {
		error = hammer2_io_new(hmp, bref->data_off, chain->bytes, &dio);
		KKASSERT(error == 0);
		callback(dio, chain, arg_p, arg_o);
		return;
	}

	/*
	 * Otherwise issue a read
	 */
	adjreadcounter(&chain->bref, chain->bytes);
	hammer2_io_breadcb(hmp, bref->data_off, chain->bytes,
			   callback, chain, arg_p, arg_o);
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
	 * leaving the data/io intact
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
	 */
	if (chain->dio == NULL) {
		if ((chain->flags & HAMMER2_CHAIN_MODIFIED) == 0)
			hammer2_chain_drop_data(chain, 0);
		ccms_thread_unlock_upgraded(&core->cst, ostate);
		hammer2_chain_drop(chain);
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
	ccms_thread_unlock_upgraded(&core->cst, ostate);
	hammer2_chain_drop(chain);
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
	hammer2_chain_core_t *core = chain->core;

	KKASSERT((chain->flags & HAMMER2_CHAIN_DUPLICATED) == 0);

	spin_lock(&core->cst.spin);
        if ((core->flags & HAMMER2_CORE_COUNTEDBREFS) == 0) {
		if (base) {
			while (--count >= 0) {
				if (base[count].type)
					break;
			}
			core->live_zero = count + 1;
			while (count >= 0) {
				if (base[count].type)
					atomic_add_int(&core->live_count, 1);
				--count;
			}
		} else {
			core->live_zero = 0;
		}
		/* else do not modify live_count */
		atomic_set_int(&core->flags, HAMMER2_CORE_COUNTEDBREFS);
	}
	spin_unlock(&core->cst.spin);
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
 * XXX return error if cannot resize.
 */
void
hammer2_chain_resize(hammer2_trans_t *trans, hammer2_inode_t *ip,
		     hammer2_chain_t *parent, hammer2_chain_t **chainp,
		     int nradix, int flags)
{
	hammer2_mount_t *hmp;
	hammer2_chain_t *chain;
	size_t obytes;
	size_t nbytes;

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
	 * used by the flush code.  The new chain will be returned in a
	 * modified state.
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
	 * Relocate the block, even if making it smaller (because different
	 * block sizes may be in different regions).
	 */
	hammer2_freemap_alloc(trans, chain, nbytes);
	chain->bytes = nbytes;
	atomic_clear_int(&chain->flags, HAMMER2_CHAIN_FORCECOW);
	/*ip->delta_dcount += (ssize_t)(nbytes - obytes);*/ /* XXX atomic */

	/*
	 * For now just support it on DATA chains (and not on indirect
	 * blocks).
	 */
	KKASSERT(chain->dio == NULL);

#if 0
	/*
	 * Make sure the chain is marked MOVED and propagate the update
	 * to the root for flush.
	 */
	if ((chain->flags & HAMMER2_CHAIN_MOVED) == 0) {
		hammer2_chain_ref(chain);
		atomic_set_int(&chain->flags, HAMMER2_CHAIN_MOVED);
	}
	hammer2_chain_setsubmod(trans, chain);
#endif
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
	if (ip->vp)
		vsetisdirty(ip->vp);
	return(&ip->chain->data->ipdata);
}

void
hammer2_chain_modify(hammer2_trans_t *trans, hammer2_chain_t **chainp,
		     int flags)
{
	hammer2_mount_t *hmp;
	hammer2_chain_t *chain;
	hammer2_io_t *dio;
	int error;
	int wasinitial;
	char *bdata;

	chain = *chainp;
	hmp = chain->hmp;

#if 0
	if (chain->bref.type == HAMMER2_BREF_TYPE_FREEMAP ||
	    chain->bref.type == HAMMER2_BREF_TYPE_FREEMAP_NODE ||
	    chain->bref.type == HAMMER2_BREF_TYPE_FREEMAP_LEAF) {
		kprintf("trans %04jx/%08x MODIFY1 %p.%d [%08x] %016jx/%d %016jx C/D %016jx/%016jx\n",
			trans->sync_tid, trans->flags,
			chain, chain->bref.type, chain->flags,
			chain->bref.key, chain->bref.keybits,
			chain->bref.data_off,
			chain->modify_tid, chain->delete_tid);
	}
#endif
#if 0
	kprintf("MODIFY %p.%d flags %08x mod=%016jx del=%016jx\n", chain, chain->bref.type, chain->flags, chain->modify_tid, chain->delete_tid);
#endif
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
	 * data is not optional for freemap chains (we must always be sure
	 * to copy the data on COW storage allocations).
	 */
	if (chain->bref.type == HAMMER2_BREF_TYPE_FREEMAP_NODE ||
	    chain->bref.type == HAMMER2_BREF_TYPE_FREEMAP_LEAF) {
		KKASSERT((chain->flags & HAMMER2_CHAIN_INITIAL) ||
			 (flags & HAMMER2_MODIFY_OPTDATA) == 0);
	}

	/*
	 * Determine if a delete-duplicate is needed.
	 *
	 * (a) Modify_tid is part of a prior flush
	 * (b) Transaction is concurrent with a flush (has higher tid)
	 * (c) and chain is not in the initial state (freshly created)
	 * (d) and caller didn't request an in-place modification.
	 *
	 * The freemap and volume header special chains are never D-Dd.
	 */
	if (chain->modify_tid != trans->sync_tid &&	   /* cross boundary */
	    (flags & HAMMER2_MODIFY_INPLACE) == 0) {	   /* from d-d */
		if (chain != &hmp->fchain && chain != &hmp->vchain) {
			KKASSERT((flags & HAMMER2_MODIFY_ASSERTNOCOPY) == 0);
			hammer2_chain_delete_duplicate(trans, chainp, 0);
			chain = *chainp;
#if 0
	if (chain->bref.type == HAMMER2_BREF_TYPE_FREEMAP ||
	    chain->bref.type == HAMMER2_BREF_TYPE_FREEMAP_NODE ||
	    chain->bref.type == HAMMER2_BREF_TYPE_FREEMAP_LEAF) {
		kprintf("trans %04jx/%08x MODIFY2 %p.%d [%08x] %016jx/%d %016jx\n",
			trans->sync_tid, trans->flags,
			chain, chain->bref.type, chain->flags,
			chain->bref.key, chain->bref.keybits,
			chain->bref.data_off);
			return;
		}
#endif
		}

		/*
		 * Fall through if fchain or vchain, clearing the CHAIN_FLUSHED
		 * flag.  Basically other chains are delete-duplicated and so
		 * the duplicated chains of course will not have the FLUSHED
		 * flag set, but fchain and vchain are special-cased and the
		 * flag must be cleared when changing modify_tid.
		 */
		atomic_clear_int(&chain->flags, HAMMER2_CHAIN_FLUSHED);
	}

	/*
	 * Otherwise do initial-chain handling
	 */
	if ((chain->flags & HAMMER2_CHAIN_MODIFIED) == 0) {
		atomic_set_int(&chain->flags, HAMMER2_CHAIN_MODIFIED);
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
		     ((flags & HAMMER2_MODIFY_NOREALLOC) == 0 &&
		      chain->modify_tid != trans->sync_tid)
		) {
			hammer2_freemap_alloc(trans, chain, chain->bytes);
			/* XXX failed allocation */
		} else if (chain->flags & HAMMER2_CHAIN_FORCECOW) {
			hammer2_freemap_alloc(trans, chain, chain->bytes);
			/* XXX failed allocation */
		}
		atomic_clear_int(&chain->flags, HAMMER2_CHAIN_FORCECOW);
	}

	/*
	 * Update modify_tid.  XXX special-case vchain/fchain because they
	 * are always modified in-place.  Otherwise the chain being modified
	 * must not be part of a future transaction.
	 */
	if (chain == &hmp->vchain || chain == &hmp->fchain) {
		if (chain->modify_tid <= trans->sync_tid)
			chain->modify_tid = trans->sync_tid;
	} else {
		KKASSERT(chain->modify_tid <= trans->sync_tid);
		chain->modify_tid = trans->sync_tid;
	}

	if ((flags & HAMMER2_MODIFY_NO_MODIFY_TID) == 0)
		chain->bref.modify_tid = trans->sync_tid;

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
		KKASSERT(chain->dio == NULL);
		break;
	case HAMMER2_BREF_TYPE_DATA:
	case HAMMER2_BREF_TYPE_INDIRECT:
	case HAMMER2_BREF_TYPE_FREEMAP_NODE:
		/*
		 * Perform the copy-on-write operation
		 */
		KKASSERT(chain != &hmp->vchain && chain != &hmp->fchain);

		if (wasinitial) {
			error = hammer2_io_new(hmp, chain->bref.data_off,
					       chain->bytes, &dio);
		} else {
			error = hammer2_io_bread(hmp, chain->bref.data_off,
						 chain->bytes, &dio);
		}
		adjreadcounter(&chain->bref, chain->bytes);
		KKASSERT(error == 0);

		bdata = hammer2_io_data(dio, chain->bref.data_off);

		/*
		 * Copy or zero-fill on write depending on whether
		 * chain->data exists or not and set the dirty state for
		 * the new buffer.  Retire the existing buffer.
		 */
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
#if 0
	kprintf("RET2 %p.%d flags %08x mod=%016jx del=%016jx\n", chain, chain->bref.type, chain->flags, chain->modify_tid, chain->delete_tid);
#endif
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
 * This function returns the chain at the nearest key within the specified
 * range with the highest delete_tid.  The core spinlock must be held on
 * call and the returned chain will be referenced but not locked.
 *
 * The returned chain may or may not be in a deleted state.  Note that
 * live chains have a delete_tid = MAX_TID.
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
	hammer2_chain_layer_t *layer;

	info.best = NULL;
	info.key_beg = key_beg;
	info.key_end = key_end;
	info.key_next = *key_nextp;

	KKASSERT(parent->core->good == 0x1234);
	TAILQ_FOREACH(layer, &parent->core->layerq, entry) {
		KKASSERT(layer->good == 0xABCD);
		RB_SCAN(hammer2_chain_tree, &layer->rbtree,
			hammer2_chain_find_cmp, hammer2_chain_find_callback,
			&info);
	}
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
		 * If our current best is flush with key_beg and child is
		 * also flush with key_beg choose based on delete_tid.
		 *
		 * key_next will automatically be limited to the smaller of
		 * the two end-points.
		 */
		if (child->delete_tid > best->delete_tid)
			info->best = child;
	} else if (child->bref.key < best->bref.key) {
		/*
		 * Child has a nearer key and best is not flush with key_beg.
		 * Truncate key_next to the old best key iff it had a better
		 * delete_tid.
		 */
		info->best = child;
		if (best->delete_tid >= child->delete_tid &&
		    (info->key_next > best->bref.key || info->key_next == 0))
			info->key_next = best->bref.key;
	} else if (child->bref.key == best->bref.key) {
		/*
		 * If our current best is flush with the child then choose
		 * based on delete_tid.
		 *
		 * key_next will automatically be limited to the smaller of
		 * the two end-points.
		 */
		if (child->delete_tid > best->delete_tid)
			info->best = child;
	} else {
		/*
		 * Keep the current best but truncate key_next to the child's
		 * base iff the child has a higher delete_tid.
		 *
		 * key_next will also automatically be limited to the smaller
		 * of the two end-points (probably not necessary for this case
		 * but we do it anyway).
		 */
		if (child->delete_tid >= best->delete_tid &&
		    (info->key_next > child->bref.key || info->key_next == 0))
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
 * in-memory chain structure which reflects it.  modify_tid will be
 * left 0 which forces any modifications to issue a delete-duplicate.
 *
 * To handle insertion races pass the INSERT_RACE flag along with the
 * generation number of the core.  NULL will be returned if the generation
 * number changes before we have a chance to insert the chain.  Insert
 * races can occur because the parent might be held shared.
 *
 * Caller must hold the parent locked shared or exclusive since we may
 * need the parent's bref array to find our block.
 */
hammer2_chain_t *
hammer2_chain_get(hammer2_chain_t *parent, hammer2_blockref_t *bref,
		  int generation)
{
	hammer2_mount_t *hmp = parent->hmp;
	hammer2_chain_core_t *above = parent->core;
	hammer2_chain_t *chain;
	int error;

	/*
	 * Allocate a chain structure representing the existing media
	 * entry.  Resulting chain has one ref and is not locked.
	 */
	chain = hammer2_chain_alloc(hmp, parent->pmp, NULL, bref);
	chain->dst_reason = 100;
	hammer2_chain_core_alloc(NULL, chain, NULL);
	/* ref'd chain returned */
	chain->modify_tid = chain->bref.mirror_tid;

	/*
	 * Link the chain into its parent.  A spinlock is required to safely
	 * access the RBTREE, and it is possible to collide with another
	 * hammer2_chain_get() operation because the caller might only hold
	 * a shared lock on the parent.
	 */
	KKASSERT(parent->refs > 0);
	error = hammer2_chain_insert(above, NULL, chain,
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
	hammer2_chain_t *bparent;
	hammer2_chain_t *nparent;
	hammer2_chain_core_t *above;

	oparent = *parentp;
	above = oparent->above;

	spin_lock(&above->cst.spin);
	bparent = TAILQ_FIRST(&above->ownerq);
	hammer2_chain_ref(bparent);

	/*
	 * Be careful of order, oparent must be unlocked before nparent
	 * is locked below to avoid a deadlock.  We might as well delay its
	 * unlocking until we conveniently no longer have the spinlock (instead
	 * of cycling the spinlock).
	 *
	 * Theoretically our ref on bparent should prevent elements of the
	 * following chain from going away and prevent above from going away,
	 * but we still need the spinlock to safely scan the list.
	 */
	for (;;) {
		nparent = bparent;
		while (nparent->flags & HAMMER2_CHAIN_DUPLICATED)
			nparent = TAILQ_NEXT(nparent, core_entry);
		hammer2_chain_ref(nparent);
		spin_unlock(&above->cst.spin);

		if (oparent) {
			hammer2_chain_unlock(oparent);
			oparent = NULL;
		}
		hammer2_chain_lock(nparent, how | HAMMER2_RESOLVE_NOREF);
		hammer2_chain_drop(bparent);

		/*
		 * We might have raced a delete-duplicate.
		 */
		if ((nparent->flags & HAMMER2_CHAIN_DUPLICATED) == 0)
			break;
		bparent = nparent;
		hammer2_chain_ref(bparent);
		hammer2_chain_unlock(nparent);
		spin_lock(&above->cst.spin);
		/* retry */
	}
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
 * requested the chain will be returned only referenced.
 *
 * NULL is returned if no match was found, but (*parentp) will still
 * potentially be adjusted.
 *
 * On return (*key_nextp) will point to an iterative value for key_beg.
 * (If NULL is returned (*key_nextp) is set to key_end).
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
	hammer2_mount_t *hmp;
	hammer2_chain_t *parent;
	hammer2_chain_t *chain;
	hammer2_blockref_t *base;
	hammer2_blockref_t *bref;
	hammer2_blockref_t bcopy;
	hammer2_key_t scan_beg;
	hammer2_key_t scan_end;
	hammer2_chain_core_t *above;
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
	if (flags & (HAMMER2_LOOKUP_SHARED | HAMMER2_LOOKUP_NOLOCK)) {
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
			if (flags & HAMMER2_LOOKUP_NOLOCK)
				hammer2_chain_ref(parent);
			else
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
		panic("hammer2_chain_lookup: unrecognized blockref type: %d",
		      parent->bref.type);
		base = NULL;	/* safety */
		count = 0;	/* safety */
	}

	/*
	 * Merged scan to find next candidate.
	 *
	 * hammer2_base_*() functions require the above->live_* fields
	 * to be synchronized.
	 *
	 * We need to hold the spinlock to access the block array and RB tree
	 * and to interlock chain creation.
	 */
	above = parent->core;
	if ((parent->core->flags & HAMMER2_CORE_COUNTEDBREFS) == 0)
		hammer2_chain_countbrefs(parent, base, count);

	/*
	 * Combined search
	 */
	spin_lock(&above->cst.spin);
	chain = hammer2_combined_find(parent, base, count,
				      cache_indexp, key_nextp,
				      key_beg, key_end, &bref);
	generation = above->generation;

	/*
	 * Exhausted parent chain, iterate.
	 */
	if (bref == NULL) {
		spin_unlock(&above->cst.spin);
		if (key_beg == key_end)	/* short cut single-key case */
			return (NULL);
		return (hammer2_chain_next(parentp, NULL, key_nextp,
					   key_beg, key_end,
					   cache_indexp, flags));
	}

	/*
	 * Selected from blockref or in-memory chain.
	 */
	if (chain == NULL) {
		bcopy = *bref;
		spin_unlock(&above->cst.spin);
		chain = hammer2_chain_get(parent, &bcopy, generation);
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
		spin_unlock(&above->cst.spin);
	}

	/*
	 * chain is referenced but not locked.  We must lock the chain
	 * to obtain definitive DUPLICATED/DELETED state
	 */
	if (chain->bref.type == HAMMER2_BREF_TYPE_INDIRECT ||
	    chain->bref.type == HAMMER2_BREF_TYPE_FREEMAP_NODE) {
		hammer2_chain_lock(chain, how_maybe | HAMMER2_RESOLVE_NOREF);
	} else {
		hammer2_chain_lock(chain, how | HAMMER2_RESOLVE_NOREF);
	}

	/*
	 * Skip deleted chains (XXX cache 'i' end-of-block-array? XXX)
	 *
	 * NOTE: Chain's key range is not relevant as there might be
	 *	 one-offs within the range that are not deleted.
	 *
	 * NOTE: Ignore the DUPLICATED flag, the lock above resolves
	 *	 the chain's terminal state so if it is duplicated it
	 *	 is virtually certain to be either deleted or live.
	 */
	if (chain->flags & HAMMER2_CHAIN_DELETED) {
		hammer2_chain_unlock(chain);
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
		*parentp = parent = chain;
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
	if (flags & (HAMMER2_LOOKUP_SHARED | HAMMER2_LOOKUP_NOLOCK))
		how_maybe |= HAMMER2_RESOLVE_SHARED;

	parent = *parentp;

	/*
	 * Calculate the next index and recalculate the parent if necessary.
	 */
	if (chain) {
		key_beg = chain->bref.key +
			  ((hammer2_key_t)1 << chain->bref.keybits);
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
 * Raw scan functions are similar to lookup/next but do not seek the parent
 * chain and do not skip stale chains.  These functions are primarily used
 * by the recovery code.
 *
 * Parent and chain are locked, parent's data must be resolved.  To acquire
 * the first sub-chain under parent pass chain == NULL.
 */
hammer2_chain_t *
hammer2_chain_scan(hammer2_chain_t *parent, hammer2_chain_t *chain,
		   int *cache_indexp, int flags)
{
	hammer2_mount_t *hmp;
	hammer2_blockref_t *base;
	hammer2_blockref_t *bref;
	hammer2_blockref_t bcopy;
	hammer2_chain_core_t *above;
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
	 * Scan flags borrowed from lookup
	 */
	if (flags & HAMMER2_LOOKUP_ALWAYS) {
		how_maybe = how_always;
		how = HAMMER2_RESOLVE_ALWAYS;
	} else if (flags & (HAMMER2_LOOKUP_NODATA | HAMMER2_LOOKUP_NOLOCK)) {
		how = HAMMER2_RESOLVE_NEVER;
	} else {
		how = HAMMER2_RESOLVE_MAYBE;
	}
	if (flags & (HAMMER2_LOOKUP_SHARED | HAMMER2_LOOKUP_NOLOCK)) {
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
		chain = NULL;
		if (key == 0)
			goto done;
	} else {
		key = 0;
	}

again:
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
	 * hammer2_base_*() functions require the above->live_* fields
	 * to be synchronized.
	 *
	 * We need to hold the spinlock to access the block array and RB tree
	 * and to interlock chain creation.
	 */
	if ((parent->core->flags & HAMMER2_CORE_COUNTEDBREFS) == 0)
		hammer2_chain_countbrefs(parent, base, count);

	above = parent->core;
	next_key = 0;
	spin_lock(&above->cst.spin);
	chain = hammer2_combined_find(parent, base, count,
				      cache_indexp, &next_key,
				      key, HAMMER2_MAX_KEY, &bref);
	generation = above->generation;

	/*
	 * Exhausted parent chain, we're done.
	 */
	if (bref == NULL) {
		spin_unlock(&above->cst.spin);
		KKASSERT(chain == NULL);
		goto done;
	}

	/*
	 * Selected from blockref or in-memory chain.
	 */
	if (chain == NULL) {
		bcopy = *bref;
		spin_unlock(&above->cst.spin);
		chain = hammer2_chain_get(parent, &bcopy, generation);
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
		spin_unlock(&above->cst.spin);
	}

	/*
	 * chain is referenced but not locked.  We must lock the chain
	 * to obtain definitive DUPLICATED/DELETED state
	 */
	hammer2_chain_lock(chain, how | HAMMER2_RESOLVE_NOREF);

	/*
	 * Skip deleted chains (XXX cache 'i' end-of-block-array? XXX)
	 *
	 * NOTE: chain's key range is not relevant as there might be
	 *	 one-offs within the range that are not deleted.
	 *
	 * NOTE: XXX this could create problems with scans used in
	 *	 situations other than mount-time recovery.
	 *
	 * NOTE: Ignore the DUPLICATED flag, the lock above resolves
	 *	 the chain's terminal state so if it is duplicated it
	 *	 is virtually certain to be either deleted or live.
	 */
	if (chain->flags & HAMMER2_CHAIN_DELETED) {
		hammer2_chain_unlock(chain);
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
	hammer2_chain_t *parent = *parentp;
	hammer2_chain_core_t *above;
	hammer2_blockref_t *base;
	hammer2_blockref_t dummy;
	int allocated = 0;
	int error = 0;
	int count;
	int maxloops = 300000;

	above = parent->core;
	KKASSERT(ccms_thread_lock_owned(&above->cst));
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
		chain = hammer2_chain_alloc(hmp, parent->pmp, trans, &dummy);
		chain->dst_reason = 101;
		hammer2_chain_core_alloc(trans, chain, NULL);

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
			/*
			 * leave chain->data NULL, set INITIAL
			 */
			KKASSERT(chain->data == NULL);
			atomic_set_int(&chain->flags, HAMMER2_CHAIN_INITIAL);
			break;
		}
	} else {
		/*
		 * We are reattaching a chain that has been duplicated and
		 * left disconnected under a DIFFERENT parent with potentially
		 * different key/keybits.
		 *
		 * The chain must be modified in the current transaction
		 * (the duplication code should have done that for us),
		 * and it's modify_tid should be greater than the parent's
		 * bref.mirror_tid.  This should cause it to be created under
		 * the new parent.
		 *
		 * If deleted in the same transaction, the create/delete TIDs
		 * will be the same and effective the chain will not have
		 * existed at all from the point of view of the parent.
		 *
		 * Do NOT mess with the current state of the INITIAL flag.
		 */
		KKASSERT(chain->modify_tid > parent->bref.mirror_tid);
		KKASSERT(chain->modify_tid == trans->sync_tid);
		chain->bref.key = key;
		chain->bref.keybits = keybits;
		/* chain->modify_tid = chain->bref.mirror_tid; */
		KKASSERT(chain->above == NULL);
	}

	/*
	 * Calculate how many entries we have in the blockref array and
	 * determine if an indirect block is required.
	 */
again:
	if (--maxloops == 0)
		panic("hammer2_chain_create: maxloops");
	above = parent->core;

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
	if ((parent->core->flags & HAMMER2_CORE_COUNTEDBREFS) == 0)
		hammer2_chain_countbrefs(parent, base, count);

	KKASSERT(above->live_count >= 0 && above->live_count <= count);

	/*
	 * If no free blockref could be found we must create an indirect
	 * block and move a number of blockrefs into it.  With the parent
	 * locked we can safely lock each child in order to delete+duplicate
	 * it without causing a deadlock.
	 *
	 * This may return the new indirect block or the old parent depending
	 * on where the key falls.  NULL is returned on error.
	 */
	if (above->live_count == count) {
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
	hammer2_chain_insert(above, NULL, chain,
			     HAMMER2_CHAIN_INSERT_SPIN |
			     HAMMER2_CHAIN_INSERT_LIVE,
			     0);

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
	}
	hammer2_chain_setsubmod(trans, chain);

done:
	*chainp = chain;

	return (error);
}

/*
 * Replace (*chainp) with a duplicate in-memory chain structure which shares
 * the same core and media state as the orignal.  The original *chainp is
 * unlocked and the replacement will be returned locked.
 *
 * The old chain may or may not be in a DELETED state.  This new chain will
 * be live (not deleted).
 *
 * The new chain will be marked modified for the current transaction.
 *
 * If (parent) is non-NULL then the new duplicated chain is inserted under
 * the parent.
 *
 * If (parent) is NULL then the new duplicated chain is not inserted anywhere,
 * similar to if it had just been chain_alloc()'d (suitable for passing into
 * hammer2_chain_create() after this function returns).
 *
 * WARNING! This is not a snapshot.  Changes made underneath either the old
 *	    or new chain will affect both.
 */
static void hammer2_chain_dup_fixup(hammer2_chain_t *ochain,
				    hammer2_chain_t *nchain);

void
hammer2_chain_duplicate(hammer2_trans_t *trans, hammer2_chain_t **parentp,
			hammer2_chain_t **chainp, hammer2_blockref_t *bref,
			int snapshot, int duplicate_reason)
{
	hammer2_mount_t *hmp;
	hammer2_chain_t *parent;
	hammer2_chain_t *ochain;
	hammer2_chain_t *nchain;
	hammer2_chain_core_t *above;
	size_t bytes;

	/*
	 * We want nchain to be our go-to live chain, but ochain may be in
	 * a MODIFIED state within the current flush synchronization segment.
	 * Force any further modifications of ochain to do another COW
	 * operation even if modify_tid indicates that one is not needed.
	 *
	 * WARNING!  We should never resolve DATA to device buffers
	 *	     (XXX allow it if the caller did?), and since
	 *	     we currently do not have the logical buffer cache
	 *	     buffer in-hand to fix its cached physical offset
	 *	     we also force the modify code to not COW it. XXX
	 */
	ochain = *chainp;
	hmp = ochain->hmp;
	if (parentp)
		ochain->debug_reason += 0x10000;
	else
		ochain->debug_reason += 0x100000;
	ochain->src_reason = duplicate_reason;

	atomic_set_int(&ochain->flags, HAMMER2_CHAIN_FORCECOW);

	/*
	 * Now create a duplicate of the chain structure, associating
	 * it with the same core, making it the same size, pointing it
	 * to the same bref (the same media block).
	 *
	 * Give the duplicate the same modify_tid that we previously
	 * ensured was sufficiently advanced to trigger a block table
	 * insertion on flush.
	 *
	 * NOTE: bref.mirror_tid duplicated by virtue of bref copy in
	 *	 hammer2_chain_alloc()
	 */
	if (bref == NULL)
		bref = &ochain->bref;
	if (snapshot) {
		nchain = hammer2_chain_alloc(hmp, NULL, trans, bref);
		nchain->dst_reason = 102;
		atomic_set_int(&nchain->flags, HAMMER2_CHAIN_SNAPSHOT);
	} else {
		nchain = hammer2_chain_alloc(hmp, ochain->pmp, trans, bref);
		nchain->dst_reason = 103;
	}
	nchain->debug_previous = ochain;
	hammer2_chain_core_alloc(trans, nchain, ochain);
	bytes = (hammer2_off_t)1 <<
		(int)(bref->data_off & HAMMER2_OFF_MASK_RADIX);
	nchain->bytes = bytes;
	nchain->modify_tid = ochain->modify_tid;
	if (ochain->flags & HAMMER2_CHAIN_INITIAL)
		atomic_set_int(&nchain->flags, HAMMER2_CHAIN_INITIAL);

	/*
	 * Fixup (copy) any embedded data.  Non-embedded data relies on the
	 * media block.  We must unlock ochain before we can access nchain's
	 * media block because they might share the same bp and deadlock if
	 * we don't.
	 */
	hammer2_chain_lock(nchain, HAMMER2_RESOLVE_NEVER |
				   HAMMER2_RESOLVE_NOREF);
	hammer2_chain_dup_fixup(ochain, nchain);
	/* nchain has 1 ref */
	hammer2_chain_unlock(ochain);
	KKASSERT((ochain->flags & HAMMER2_CHAIN_EMBEDDED) ||
		 ochain->data == NULL);

	/*
	 * Place nchain in the modified state, instantiate media data
	 * if necessary.  Because modify_tid is already completely
	 * synchronized this should not result in a delete-duplicate.
	 *
	 * We want nchain at the target to look like a new insertion.
	 * Forcing the modification to be INPLACE accomplishes this
	 * because we get the same nchain with an updated modify_tid.
	 */
	if (nchain->bref.type == HAMMER2_BREF_TYPE_DATA) {
		hammer2_chain_modify(trans, &nchain,
				     HAMMER2_MODIFY_OPTDATA |
				     HAMMER2_MODIFY_NOREALLOC |
				     HAMMER2_MODIFY_INPLACE);
	} else if (nchain->flags & HAMMER2_CHAIN_INITIAL) {
		hammer2_chain_modify(trans, &nchain,
				     HAMMER2_MODIFY_OPTDATA |
				     HAMMER2_MODIFY_INPLACE);
	} else {
		hammer2_chain_modify(trans, &nchain,
				     HAMMER2_MODIFY_INPLACE);
	}

	/*
	 * If parent is not NULL the duplicated chain will be entered under
	 * the parent and the MOVED bit set.
	 *
	 * Having both chains locked is extremely important for atomicy.
	 */
	if (parentp && (parent = *parentp) != NULL) {
		above = parent->core;
		KKASSERT(ccms_thread_lock_owned(&above->cst));
		KKASSERT((nchain->flags & HAMMER2_CHAIN_DELETED) == 0);
		KKASSERT(parent->refs > 0);

		hammer2_chain_create(trans, parentp, &nchain,
				     nchain->bref.key, nchain->bref.keybits,
				     nchain->bref.type, nchain->bytes);
		parent = NULL;

		if ((nchain->flags & HAMMER2_CHAIN_MOVED) == 0) {
			hammer2_chain_ref(nchain);
			atomic_set_int(&nchain->flags, HAMMER2_CHAIN_MOVED);
		}
		hammer2_chain_setsubmod(trans, nchain);
	}

#if 0
	/*
	 * Unconditionally set MOVED to force the parent blockrefs to
	 * update, and adjust update_hi below nchain so nchain's
	 * blockrefs are updated with the new attachment.
	 */
	if (nchain->core->update_hi < trans->sync_tid) {
		spin_lock(&nchain->core->cst.spin);
		if (nchain->core->update_hi < trans->sync_tid)
			nchain->core->update_hi = trans->sync_tid;
		spin_unlock(&nchain->core->cst.spin);
	}
#endif

	*chainp = nchain;
}

/*
 * Special in-place delete-duplicate sequence which does not require a
 * locked parent.  (*chainp) is marked DELETED and atomically replaced
 * with a duplicate.  Atomicy is at the very-fine spin-lock level in
 * order to ensure that lookups do not race us.
 *
 * If the old chain is already marked deleted the new chain will also be
 * marked deleted.  This case can occur when an inode is removed from the
 * filesystem but programs still have an open descriptor to it, and during
 * flushes when the flush needs to operate on a chain that is deleted in
 * the live view but still alive in the flush view.
 *
 * The new chain will be marked modified for the current transaction.
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

	if (hammer2_debug & 0x20000)
		Debugger("dd");

	/*
	 * Note that we do not have to call setsubmod on ochain, calling it
	 * on nchain is sufficient.
	 */
	ochain = *chainp;
	hmp = ochain->hmp;

	ochain->debug_reason += 0x1000;
	ochain->src_reason = 99;
	if ((ochain->debug_reason & 0xF000) > 0x4000) {
		kprintf("ochain %p\n", ochain);
		Debugger("shit2");
	}
	if (ochain->bref.type == HAMMER2_BREF_TYPE_INODE) {
		KKASSERT(ochain->data);
	}

	/*
	 * First create a duplicate of the chain structure.
	 * (nchain is allocated with one ref).
	 *
	 * In the case where nchain inherits ochains core, nchain is
	 * effectively locked due to ochain being locked (and sharing the
	 * core), until we can give nchain its own official ock.
	 */
	nchain = hammer2_chain_alloc(hmp, ochain->pmp, trans, &ochain->bref);
	nchain->dst_reason = 104;
	nchain->debug_previous = ochain;
	if (flags & HAMMER2_DELDUP_RECORE)
		hammer2_chain_core_alloc(trans, nchain, NULL);
	else
		hammer2_chain_core_alloc(trans, nchain, ochain);
	above = ochain->above;

	bytes = (hammer2_off_t)1 <<
		(int)(ochain->bref.data_off & HAMMER2_OFF_MASK_RADIX);
	nchain->bytes = bytes;

	/*
	 * Duplicate inherits ochain's live state including its modification
	 * state.  This function disposes of the original.  Because we are
	 * doing this in-place under the same parent the block array
	 * inserted/deleted state does not change.
	 *
	 * The caller isn't expected to make further modifications of ochain
	 * but set the FORCECOW bit anyway, just in case it does.  If ochain
	 * was previously marked FORCECOW we also flag nchain FORCECOW
	 * (used during hardlink splits).
	 *
	 * NOTE: bref.mirror_tid duplicated by virtue of bref copy in
	 *	 hammer2_chain_alloc()
	 */
	nchain->data_count += ochain->data_count;
	nchain->inode_count += ochain->inode_count;
	atomic_set_int(&nchain->flags,
		       ochain->flags & (HAMMER2_CHAIN_INITIAL |
					HAMMER2_CHAIN_FORCECOW));
	atomic_set_int(&ochain->flags, HAMMER2_CHAIN_FORCECOW);

	/*
	 * Lock nchain so both chains are now locked (extremely important
	 * for atomicy).  Mark ochain deleted and reinsert into the topology
	 * and insert nchain all in one go.
	 *
	 * If the ochain is already deleted it is left alone and nchain
	 * is inserted into the topology as a deleted chain.  This is
	 * important because it allows ongoing operations to be executed
	 * on a deleted inode which still has open descriptors.
	 *
	 * The deleted case can also occur when a flush delete-duplicates
	 * a node which is being concurrently modified by ongoing operations
	 * in a later transaction.  This creates a problem because the flush
	 * is intended to update blockrefs which then propagate, allowing
	 * the original covering in-memory chains to be freed up.  In this
	 * situation the flush code does NOT free the original covering
	 * chains and will re-apply them to successive copies.
	 */
	hammer2_chain_lock(nchain, HAMMER2_RESOLVE_NEVER);
	hammer2_chain_dup_fixup(ochain, nchain);
	/* extra ref still present from original allocation */

	KKASSERT(ochain->flags & HAMMER2_CHAIN_ONRBTREE);
	spin_lock(&above->cst.spin);
	KKASSERT(ochain->flags & HAMMER2_CHAIN_ONRBTREE);

	/*
	 * Ultimately nchain->modify_tid will be set to trans->sync_tid,
	 * but we can't do that here because we want to call
	 * hammer2_chain_modify() to reallocate the block (if necessary).
	 */
	nchain->modify_tid = ochain->modify_tid;

	if (ochain->flags & HAMMER2_CHAIN_DELETED) {
		/*
		 * ochain was deleted
		 */
		atomic_set_int(&nchain->flags, HAMMER2_CHAIN_DELETED);
		if (ochain->delete_tid > trans->sync_tid) {
			/*
			 * delete-duplicate a chain deleted in a later
			 * transaction.  Only allowed on chains created
			 * before or during the current transaction (flush
			 * code should filter out chains created after the
			 * current transaction).
			 *
			 * To make this work is a bit of a hack.  We convert
			 * ochain's delete_tid to the current sync_tid and
			 * create a nchain which sets up ochains original
			 * delete_tid.
			 *
			 * This effectively forces ochain to flush as a
			 * deletion and nchain as a creation.  Thus MOVED
			 * must be set in ochain (it should already be
			 * set since it's original delete_tid could not
			 * have been flushed yet).  Since ochain's delete_tid
			 * has been moved down to sync_tid, a re-flush at
			 * sync_tid won't try to delete-duplicate ochain
			 * again.
			 */
			KKASSERT(ochain->modify_tid <= trans->sync_tid);
			nchain->delete_tid = ochain->delete_tid;
			ochain->delete_tid = trans->sync_tid;
			KKASSERT(ochain->flags & HAMMER2_CHAIN_MOVED);
		} else if (ochain->delete_tid == trans->sync_tid) {
			/*
			 * ochain was deleted in the current transaction
			 */
			nchain->delete_tid = trans->sync_tid;
		} else {
			/*
			 * ochain was deleted in a prior transaction.
			 * create and delete nchain in the current
			 * transaction.
			 *
			 * (delete_tid might represent a deleted inode
			 *  which still has an open descriptor).
			 */
			nchain->delete_tid = trans->sync_tid;
		}
		hammer2_chain_insert(above, ochain->inlayer, nchain, 0, 0);
	} else {
		/*
		 * ochain was not deleted, delete it in the current
		 * transaction.
		 */
		KKASSERT(trans->sync_tid >= ochain->modify_tid);
		ochain->delete_tid = trans->sync_tid;
		atomic_set_int(&ochain->flags, HAMMER2_CHAIN_DELETED);
		atomic_add_int(&above->live_count, -1);
		hammer2_chain_insert(above, NULL, nchain,
				     HAMMER2_CHAIN_INSERT_LIVE, 0);
	}

	if ((ochain->flags & HAMMER2_CHAIN_MOVED) == 0) {
		hammer2_chain_ref(ochain);
		atomic_set_int(&ochain->flags, HAMMER2_CHAIN_MOVED);
	}
	spin_unlock(&above->cst.spin);

	/*
	 * ochain must be unlocked because ochain and nchain might share
	 * a buffer cache buffer, so we need to release it so nchain can
	 * potentially obtain it.
	 */
	hammer2_chain_unlock(ochain);

	/*
	 * Finishing fixing up nchain.  A new block will be allocated if
	 * crossing a synchronization point (meta-data only).
	 *
	 * Calling hammer2_chain_modify() will update modify_tid to
	 * (typically) trans->sync_tid.
	 */
	if (nchain->bref.type == HAMMER2_BREF_TYPE_DATA) {
		hammer2_chain_modify(trans, &nchain,
				     HAMMER2_MODIFY_OPTDATA |
				     HAMMER2_MODIFY_NOREALLOC |
				     HAMMER2_MODIFY_INPLACE);
	} else if (nchain->flags & HAMMER2_CHAIN_INITIAL) {
		hammer2_chain_modify(trans, &nchain,
				     HAMMER2_MODIFY_OPTDATA |
				     HAMMER2_MODIFY_INPLACE);
	} else {
		hammer2_chain_modify(trans, &nchain,
				     HAMMER2_MODIFY_INPLACE);
	}
	hammer2_chain_drop(nchain);

	/*
	 * Unconditionally set MOVED to force the parent blockrefs to
	 * update as the chain_modify() above won't necessarily do it.
	 *
	 * Adjust update_hi below nchain so nchain's blockrefs are updated
	 * with the new attachment.
	 */
	if ((nchain->flags & HAMMER2_CHAIN_MOVED) == 0) {
		atomic_set_int(&nchain->flags, HAMMER2_CHAIN_MOVED);
		hammer2_chain_ref(nchain);
	}
#if 0
	if (nchain->core->update_hi < trans->sync_tid) {
		spin_lock(&nchain->core->cst.spin);
		if (nchain->core->update_hi < trans->sync_tid)
			nchain->core->update_hi = trans->sync_tid;
		spin_unlock(&nchain->core->cst.spin);
	}
#endif
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
 * Create a snapshot of the specified {parent, ochain} with the specified
 * label.  The originating hammer2_inode must be exclusively locked for
 * safety.
 *
 * The ioctl code has already synced the filesystem.
 */
int
hammer2_chain_snapshot(hammer2_trans_t *trans, hammer2_chain_t **ochainp,
		       hammer2_ioc_pfs_t *pfs)
{
	hammer2_mount_t *hmp;
	hammer2_chain_t *ochain = *ochainp;
	hammer2_chain_t *nchain;
	hammer2_inode_data_t *ipdata;
	hammer2_inode_t *nip;
	size_t name_len;
	hammer2_key_t lhc;
	struct vattr vat;
	uuid_t opfs_clid;
	int error;

	kprintf("snapshot %s ochain->refs %d ochain->flags %08x\n",
		pfs->name, ochain->refs, ochain->flags);

	name_len = strlen(pfs->name);
	lhc = hammer2_dirhash(pfs->name, name_len);

	hmp = ochain->hmp;
	opfs_clid = ochain->data->ipdata.pfs_clid;

	*ochainp = ochain;

	/*
	 * Create the snapshot directory under the super-root
	 *
	 * Set PFS type, generate a unique filesystem id, and generate
	 * a cluster id.  Use the same clid when snapshotting a PFS root,
	 * which theoretically allows the snapshot to be used as part of
	 * the same cluster (perhaps as a cache).
	 *
	 * Copy the (flushed) ochain's blockref array.  Theoretically we
	 * could use chain_duplicate() but it becomes difficult to disentangle
	 * the shared core so for now just brute-force it.
	 */
	VATTR_NULL(&vat);
	vat.va_type = VDIR;
	vat.va_mode = 0755;
	nchain = NULL;
	nip = hammer2_inode_create(trans, hmp->sroot, &vat, proc0.p_ucred,
				   pfs->name, name_len, &nchain, &error);

	if (nip) {
		ipdata = hammer2_chain_modify_ip(trans, nip, &nchain, 0);
		ipdata->pfs_type = HAMMER2_PFSTYPE_SNAPSHOT;
		kern_uuidgen(&ipdata->pfs_fsid, 1);
		if (ochain->flags & HAMMER2_CHAIN_PFSROOT)
			ipdata->pfs_clid = opfs_clid;
		else
			kern_uuidgen(&ipdata->pfs_clid, 1);
		atomic_set_int(&nchain->flags, HAMMER2_CHAIN_PFSROOT);
		ipdata->u.blockset = ochain->data->ipdata.u.blockset;

		hammer2_inode_unlock_ex(nip, nchain);
	}
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
	ichain->dst_reason = 105;
	atomic_set_int(&ichain->flags, HAMMER2_CHAIN_INITIAL);
	hammer2_chain_core_alloc(trans, ichain, NULL);
	icore = ichain->core;
	hammer2_chain_lock(ichain, HAMMER2_RESOLVE_MAYBE);
	hammer2_chain_drop(ichain);	/* excess ref from alloc */

	/*
	 * We have to mark it modified to allocate its block, but use
	 * OPTDATA to allow it to remain in the INITIAL state.  Otherwise
	 * it won't be acted upon by the flush code.
	 *
	 * XXX leave the node unmodified, depend on the update_hi
	 * flush to assign and modify parent blocks.
	 */
	hammer2_chain_modify(trans, &ichain, HAMMER2_MODIFY_OPTDATA);

	/*
	 * Iterate the original parent and move the matching brefs into
	 * the new indirect block.
	 *
	 * XXX handle flushes.
	 */
	key_beg = 0;
	key_end = HAMMER2_MAX_KEY;
	cache_index = 0;
	spin_lock(&above->cst.spin);
	loops = 0;
	reason = 0;

	for (;;) {
		if (++loops > 100000) {
		    spin_unlock(&above->cst.spin);
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
		generation = above->generation;
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
			spin_unlock(&above->cst.spin);
			hammer2_chain_lock(chain, HAMMER2_RESOLVE_NEVER |
						  HAMMER2_RESOLVE_NOREF);
		} else {
			/*
			 * Get chain for blockref element.  _get returns NULL
			 * on insertion race.
			 */
			bcopy = *bref;
			spin_unlock(&above->cst.spin);
			chain = hammer2_chain_get(parent, &bcopy, generation);
			if (chain == NULL) {
				reason = 1;
				spin_lock(&above->cst.spin);
				continue;
			}
			if (bcmp(&bcopy, bref, sizeof(bcopy))) {
				reason = 2;
				hammer2_chain_drop(chain);
				spin_lock(&above->cst.spin);
				continue;
			}
			hammer2_chain_lock(chain, HAMMER2_RESOLVE_NEVER |
						  HAMMER2_RESOLVE_NOREF);
		}

		/*
		 * This is always live so if the chain has been delete-
		 * duplicated we raced someone and we have to retry.
		 *
		 * NOTE: Ignore the DUPLICATED flag, the lock above resolves
		 *	 the chain's terminal state so if it is duplicated it
		 *	 is virtually certain to be either deleted or live.
		 */
		if (chain->flags & HAMMER2_CHAIN_DELETED) {
			hammer2_chain_unlock(chain);
			goto next_key;
		}

		/*
		 * Shift the chain to the indirect block.
		 */
		hammer2_chain_delete(trans, chain, HAMMER2_DELETE_WILLDUP);
		hammer2_chain_duplicate(trans, &ichain, &chain, NULL, 0, 1);
		hammer2_chain_unlock(chain);
		KKASSERT(parent->refs > 0);
		chain = NULL;
next_key:
		spin_lock(&above->cst.spin);
next_key_spinlocked:
		if (--maxloops == 0)
			panic("hammer2_chain_create_indirect: maxloops");
		reason = 4;
		if (key_next == 0 || key_next > key_end)
			break;
		key_beg = key_next;
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
	KKASSERT((ichain->flags & HAMMER2_CHAIN_ONRBTREE) == 0);
	hammer2_chain_insert(above, NULL, ichain,
			     HAMMER2_CHAIN_INSERT_SPIN |
			     HAMMER2_CHAIN_INSERT_LIVE,
			     0);

	/*
	 * Mark the new indirect block modified after insertion, which
	 * will propagate up through parent all the way to the root and
	 * also allocate the physical block in ichain for our caller,
	 * and assign ichain->data to a pre-zero'd space (because there
	 * is not prior data to copy into it).
	 *
	 * We have to set update_hi in ichain's flags manually so the
	 * flusher knows it has to recurse through it to get to all of
	 * our moved blocks, then call setsubmod() to set the bit
	 * recursively.
	 */
	/*hammer2_chain_modify(trans, &ichain, HAMMER2_MODIFY_OPTDATA);*/
	if (ichain->core->update_hi < trans->sync_tid) {
		spin_lock(&ichain->core->cst.spin);
		if (ichain->core->update_hi < trans->sync_tid)
			ichain->core->update_hi = trans->sync_tid;
		spin_unlock(&ichain->core->cst.spin);
	}
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
	above = parent->core;
	locount = 0;
	hicount = 0;
	keybits = 64;

	/*
	 * Calculate the range of keys in the array being careful to skip
	 * slots which are overridden with a deletion.
	 */
	key_beg = 0;
	key_end = HAMMER2_MAX_KEY;
	cache_index = 0;
	spin_lock(&above->cst.spin);

	for (;;) {
		if (--maxloops == 0) {
			panic("indkey_freemap shit %p %p:%d\n",
			      parent, base, count);
		}
		chain = hammer2_combined_find(parent, base, count,
					      &cache_index, &key_next,
					      key_beg, key_end, &bref);

		/*
		 * Exhausted search
		 */
		if (bref == NULL)
			break;
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
	above = parent->core;
	locount = 0;
	hicount = 0;

	/*
	 * Calculate the range of keys in the array being careful to skip
	 * slots which are overridden with a deletion.  Once the scan
	 * completes we will cut the key range in half and shift half the
	 * range into the new indirect block.
	 */
	key_beg = 0;
	key_end = HAMMER2_MAX_KEY;
	cache_index = 0;
	spin_lock(&above->cst.spin);

	for (;;) {
		if (--maxloops == 0) {
			panic("indkey_freemap shit %p %p:%d\n",
			      parent, base, count);
		}
		chain = hammer2_combined_find(parent, base, count,
					      &cache_index, &key_next,
					      key_beg, key_end, &bref);

		/*
		 * Exhausted search
		 */
		if (bref == NULL)
			break;
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
 * set chain->delete_tid.  The chain is not actually marked possibly-free
 * in the freemap until the deletion is completely flushed out (because
 * a flush which doesn't cover the entire deletion is flushing the deleted
 * chain as if it were live).
 *
 * This function does NOT generate a modification to the parent.  It
 * would be nearly impossible to figure out which parent to modify anyway.
 * Such modifications are handled top-down by the flush code and are
 * properly merged using the flush synchronization point.
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
 *	 trans->sync_tid vs chain->modify_tid and chain->delete_tid.
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
	 * The setting of DELETED causes finds, lookups, and _next iterations
	 * to no longer recognize the chain.  RB_SCAN()s will still have
	 * visibility (needed for flush serialization points).
	 *
	 * We need the spinlock on the core whos RBTREE contains chain
	 * to protect against races.
	 */
	KKASSERT(chain->flags & HAMMER2_CHAIN_ONRBTREE);
	spin_lock(&chain->above->cst.spin);

	KKASSERT(trans->sync_tid >= chain->modify_tid);
	chain->delete_tid = trans->sync_tid;
	atomic_set_int(&chain->flags, HAMMER2_CHAIN_DELETED);
	atomic_add_int(&chain->above->live_count, -1);
	++chain->above->generation;

	/*
	 * We must set MOVED along with DELETED for the flush code to
	 * recognize the operation and properly disconnect the chain
	 * in-memory.
	 */
	if ((chain->flags & HAMMER2_CHAIN_MOVED) == 0) {
		hammer2_chain_ref(chain);
		atomic_set_int(&chain->flags, HAMMER2_CHAIN_MOVED);
	}
	spin_unlock(&chain->above->cst.spin);

	if (flags & HAMMER2_DELETE_WILLDUP)
		atomic_set_int(&chain->flags, HAMMER2_CHAIN_FORCECOW);

	hammer2_chain_setsubmod(trans, chain);
}

/*
 * Called with the core spinlock held to check for freeable layers.
 * Used by the flush code.  Layers can wind up not being freed due
 * to the temporary layer->refs count.  This function frees up any
 * layers that were missed.
 */
void
hammer2_chain_layer_check_locked(hammer2_mount_t *hmp,
				 hammer2_chain_core_t *core)
{
	hammer2_chain_layer_t *layer;
	hammer2_chain_layer_t *tmp;

	tmp = TAILQ_FIRST(&core->layerq);
	while ((layer = tmp) != NULL) {
		tmp = TAILQ_NEXT(tmp, entry);
		if (layer->refs == 0 && RB_EMPTY(&layer->rbtree)) {
			TAILQ_REMOVE(&core->layerq, layer, entry);
			if (tmp)
				++tmp->refs;
			spin_unlock(&core->cst.spin);
			kfree(layer, hmp->mchain);
			spin_lock(&core->cst.spin);
			if (tmp)
				--tmp->refs;
		}
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
 * The spin lock on the related chain must be held.
 */
int
hammer2_base_find(hammer2_chain_t *chain,
		  hammer2_blockref_t *base, int count,
		  int *cache_indexp, hammer2_key_t *key_nextp,
		  hammer2_key_t key_beg, hammer2_key_t key_end)
{
	hammer2_chain_core_t *core = chain->core;
	hammer2_blockref_t *scan;
	hammer2_key_t scan_end;
	int i;
	int limit;

	/*
	 * Require the live chain's already have their core's counted
	 * so we can optimize operations.
	 */
        KKASSERT((chain->flags & HAMMER2_CHAIN_DUPLICATED) ||
		 core->flags & HAMMER2_CORE_COUNTEDBREFS);

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
	if (chain->flags & HAMMER2_CHAIN_DUPLICATED)
		limit = count;
	else
		limit = core->live_zero;
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
			if (scan->key > key_beg)
				break;
			scan_end = scan->key +
				   ((hammer2_key_t)1 << scan->keybits) - 1;
			if (scan_end >= key_beg)
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
 * Must be called with above's spinlock held.  Spinlock remains held
 * through the operation.
 *
 * The returned chain is not locked or referenced.  Use the returned bref
 * to determine if the search exhausted or not.
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

	*key_nextp = key_end + 1;
	i = hammer2_base_find(parent, base, count, cache_indexp,
			      key_nextp, key_beg, key_end);
	chain = hammer2_chain_find(parent, key_nextp, key_beg, key_end);

	/*
	 * Neither matched
	 */
	if (i == count && chain == NULL) {
		*bresp = NULL;
		return(chain);	/* NULL */
	}

	/*
	 * Only chain matched
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
	 * Both in-memory and blockref match.
	 *
	 * If they are both flush with the left hand side select the chain.
	 * If their starts match select the chain.
	 * Otherwise the nearer element wins.
	 */
	if (chain->bref.key <= key_beg && base[i].key <= key_beg) {
		bref = &chain->bref;
		goto found;
	}
	if (chain->bref.key <= base[i].key) {
		bref = &chain->bref;
		goto found;
		return(chain);
	}
	bref = &base[i];
	chain = NULL;

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
		    int *cache_indexp, hammer2_chain_t *child)
{
	hammer2_blockref_t *elm = &child->bref;
	hammer2_chain_core_t *core = parent->core;
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
	    base[i].key != elm->key || base[i].keybits != elm->keybits) {
		panic("delete base %p element not found at %d/%d elm %p\n",
		      base, i, count, elm);
		return;
	}
	bzero(&base[i], sizeof(*base));
	base[i].mirror_tid = (intptr_t)parent;
	base[i].modify_tid = (intptr_t)child;
	base[i].check.debug.sync_tid = trans->sync_tid;

	/*
	 * We can only optimize core->live_zero for live chains.
	 */
	if ((parent->flags & HAMMER2_CHAIN_DUPLICATED) == 0) {
		if (core->live_zero == i + 1) {
			while (--i >= 0 && base[i].type == 0)
				;
			core->live_zero = i + 1;
		}
	}
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
		    int *cache_indexp, hammer2_chain_t *child)
{
	hammer2_blockref_t *elm = &child->bref;
	hammer2_chain_core_t *core = parent->core;
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
	 * We can only optimize core->live_zero for live chains.
	 */
	if (i == count && core->live_zero < count) {
		if ((parent->flags & HAMMER2_CHAIN_DUPLICATED) == 0) {
			i = core->live_zero++;
			base[i] = *elm;
			return;
		}
	}

	xkey = elm->key + ((hammer2_key_t)1 << elm->keybits) - 1;
	if (i != count && (base[i].key < elm->key || xkey >= base[i].key)) {
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
			 * We can only update core->live_zero for live
			 * chains.
			 */
			if ((parent->flags & HAMMER2_CHAIN_DUPLICATED) == 0) {
				if (core->live_zero <= k)
					core->live_zero = k + 1;
			}
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
		panic("hammer2_chain_lookup: unrecognized blockref type: %d",
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
		speedup_syncer(pmp->mp);
		pmp->inmem_waiting = 1;
		tsleep(&pmp->inmem_waiting, 0, "chnmem", hz);
	}
#endif
#if 0
	if (pmp->inmem_chains > desiredvnodes / 10 &&
	    pmp->inmem_chains > pmp->mp->mnt_nvnodelistsize * 7 / 4) {
		speedup_syncer(pmp->mp);
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
