/*
 * Copyright (c) 2011-2014 The DragonFly Project.  All rights reserved.
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
 * Recursively set update_xhi starting at chain and moving upward.  Stop early
 * if we hit a PFS transition (PFS flush code will have to detect the case
 * and perform an update within its own transaction).  The transaction xid
 * is only good within the current PFS.
 *
 * This controls top-down visibility for flushes.  The child has just one
 * 'above' core, but the core itself can be multi-homed with parents iterated
 * via core->ownerq.  The last parent is the 'live' parent (all others had to
 * have been delete-duplicated).  We always propagate upward through the live
 * parent.
 *
 * This function is not used during a flush (except when the flush is
 * allocating which requires the live tree).  The flush keeps track of its
 * recursion itself.
 *
 * XXX SMP races.  For now we do not allow concurrent transactions with
 *     different transaction ids and there should be no race, but if we do
 *     later on there will be a problem.
 */
void
hammer2_chain_setsubmod(hammer2_trans_t *trans, hammer2_chain_t *chain)
{
	hammer2_chain_core_t *above;

	if (chain->update_xhi < trans->sync_xid)
		chain->update_xhi = trans->sync_xid;

	while ((above = chain->above) != NULL) {
		spin_lock(&above->cst.spin);
		chain = TAILQ_LAST(&above->ownerq, h2_core_list);
		if (chain->update_xhi < trans->sync_xid)
			chain->update_xhi = trans->sync_xid;
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
 * chain->pmp inherits pmp unless the chain is an inode (other than the
 * super-root inode).
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
	 * Initialize the new chain structure.
	 */
	chain->pmp = pmp;
	chain->hmp = hmp;
	chain->bref = *bref;
	chain->bytes = bytes;
	chain->refs = 1;
	chain->flags = HAMMER2_CHAIN_ALLOCATED;
	chain->delete_xid = HAMMER2_XID_MAX;

	/*
	 * Set the PFS boundary flag if this chain represents a PFS root.
	 */
	if (bref->flags & HAMMER2_BREF_FLAG_PFSROOT)
		chain->flags |= HAMMER2_CHAIN_PFSBOUNDARY;

	/*
	 * Set modify_xid if a transaction is creating the inode.
	 * Enforce update_xlo = 0 so nearby transactions do not think
	 * it has been flushed when it hasn't.
	 *
	 * NOTE: When loading a chain from backing store or creating a
	 *	 snapshot, trans will be NULL and the caller is responsible
	 *	 for setting these fields.
	 */
	if (trans) {
		chain->modify_xid = trans->sync_xid;
		chain->update_xlo = 0;
	}

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
		TAILQ_INIT(&core->ownerq);
		TAILQ_INIT(&core->dbq);
		RB_INIT(&core->rbtree);	/* live chains */
		RB_INIT(&core->dbtree);	/* deleted original (bmapped) chains */
		core->sharecnt = 1;
		core->good = 0x1234;
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
		 * operations may have to work on elements with delete_xid's
		 * beyond the flush sync_xid.  In this situation we must
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
			KKASSERT(trans->flags & HAMMER2_TRANS_ISFLUSH);
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

		/*
		 * Maintain ordering for refactor test so we don't skip over
		 * a snapshot.  Also, during flushes, delete-duplications
		 * for block-table updates can occur on ochains already
		 * deleted (delete-duplicated by a later transaction), or
		 * on forward-indexed ochains.  We must properly insert
		 * nchain relative to ochain.
		 */
		if (trans && trans->sync_xid < ochain->modify_xid) {
			TAILQ_INSERT_BEFORE(ochain, nchain, core_entry);
		} else {
			TAILQ_INSERT_AFTER(&core->ownerq, ochain,
					   nchain, core_entry);
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
hammer2_chain_insert(hammer2_chain_core_t *above,
		     hammer2_chain_t *ochain, hammer2_chain_t *nchain,
		     int flags, int generation)
{
	hammer2_chain_t *xchain;
	int error = 0;

	if (flags & HAMMER2_CHAIN_INSERT_SPIN)
		spin_lock(&above->cst.spin);

	/*
	 * Interlocked by spinlock, check for race
	 */
	if ((flags & HAMMER2_CHAIN_INSERT_RACE) &&
	    above->generation != generation) {
		error = EAGAIN;
		goto failed;
	}

	/*
	 * Insert nchain
	 *
	 * XXX BMAPPED might not be handled correctly for ochain/nchain
	 *     ordering in both DELETED cases (flush and non-flush-term),
	 *     so delete-duplicate code.
	 */
	if (nchain->flags & HAMMER2_CHAIN_DELETED) {
		if (ochain && (ochain->flags & HAMMER2_CHAIN_BMAPPED)) {
			if (ochain->flags & HAMMER2_CHAIN_ONDBTREE) {
				RB_REMOVE(hammer2_chain_tree,
					  &above->dbtree, ochain);
				atomic_clear_int(&ochain->flags,
						 HAMMER2_CHAIN_ONDBTREE);
				TAILQ_INSERT_TAIL(&above->dbq,
						  ochain, db_entry);
				atomic_set_int(&ochain->flags,
						HAMMER2_CHAIN_ONDBQ);
			}
			/* clear BMAPPED (DBTREE, sometimes RBTREE) */
			atomic_clear_int(&ochain->flags, HAMMER2_CHAIN_BMAPPED);

			xchain = RB_INSERT(hammer2_chain_tree,
					   &above->dbtree, nchain);
			KKASSERT(xchain == NULL);
			atomic_set_int(&nchain->flags,
				       HAMMER2_CHAIN_ONDBTREE |
				       HAMMER2_CHAIN_BMAPPED);
		} else {
			TAILQ_INSERT_TAIL(&above->dbq, nchain, db_entry);
			atomic_set_int(&nchain->flags, HAMMER2_CHAIN_ONDBQ);
		}
	} else {
		xchain = RB_INSERT(hammer2_chain_tree, &above->rbtree, nchain);
		KASSERT(xchain == NULL,
			("hammer2_chain_insert: collision %p", nchain));
		atomic_set_int(&nchain->flags, HAMMER2_CHAIN_ONRBTREE);
	}

	nchain->above = above;
	++above->chain_count;
	++above->generation;

	/*
	 * We have to keep track of the effective live-view blockref count
	 * so the create code knows when to push an indirect block.
	 */
	if (flags & HAMMER2_CHAIN_INSERT_LIVE)
		atomic_add_int(&above->live_count, 1);
failed:
	if (flags & HAMMER2_CHAIN_INSERT_SPIN)
		spin_unlock(&above->cst.spin);
	return error;
}

/*
 * Drop the caller's reference to the chain.  When the ref count drops to
 * zero this function will try to disassociate the chain from its parent and
 * deallocate it, then recursely drop the parent using the implied ref
 * from the chain's chain->parent.
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

	if (chain->flags & HAMMER2_CHAIN_FLUSH_CREATE)
		++need;
	if (chain->flags & HAMMER2_CHAIN_FLUSH_DELETE)
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
 * The chain cannot be freed if it has a non-empty core (children) or
 * it is not at the head of ownerq.
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
		if (core->chain_count &&
		    (chain->flags & HAMMER2_CHAIN_DUPLICATED) == 0) {
			if (atomic_cmpset_int(&chain->refs, 1, 0))
				chain = NULL;	/* success */
			spin_unlock(&core->cst.spin);
			return(chain);
		}
		/* no chains left under us */

		/*
		 * Various parts of the code might be holding a ref on a
		 * stale chain as a placemarker which must be iterated to
		 * locate a later non-stale (live) chain.  We must be sure
		 * NOT to free the later non-stale chain (which might have
		 * no refs).  Otherwise mass confusion may result.
		 *
		 * The DUPLICATED flag tells us whether the chain is stale
		 * or not, so the rule is that any chain whos DUPLICATED flag
		 * is NOT set must also be at the head of the ownerq.
		 *
		 * Note that the DELETED flag is not involved.  That is, a
		 * live chain can represent a deletion that has not yet been
		 * flushed (or still has refs).
		 */
#if 0
		if (TAILQ_NEXT(chain, core_entry) == NULL &&
		    TAILQ_FIRST(&core->ownerq) != chain) {
#endif
		if ((chain->flags & HAMMER2_CHAIN_DUPLICATED) == 0 &&
		    TAILQ_FIRST(&core->ownerq) != chain) {
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
		 * above core.
		 */
		switch (chain->flags & (HAMMER2_CHAIN_ONRBTREE |
					HAMMER2_CHAIN_ONDBTREE |
					HAMMER2_CHAIN_ONDBQ)) {
		case HAMMER2_CHAIN_ONRBTREE:
			RB_REMOVE(hammer2_chain_tree, &above->rbtree, chain);
			atomic_clear_int(&chain->flags, HAMMER2_CHAIN_ONRBTREE);
			break;
		case HAMMER2_CHAIN_ONDBTREE:
			RB_REMOVE(hammer2_chain_tree, &above->dbtree, chain);
			atomic_clear_int(&chain->flags, HAMMER2_CHAIN_ONDBTREE);
			break;
		case HAMMER2_CHAIN_ONDBQ:
			TAILQ_REMOVE(&above->dbq, chain, db_entry);
			atomic_clear_int(&chain->flags, HAMMER2_CHAIN_ONDBQ);
			break;
		default:
			panic("hammer2_chain_lastdrop: chain %p badflags %08x",
			      chain, chain->flags);
			break;
		}

		--above->chain_count;
		chain->above = NULL;

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
			 * it.
			 */
			KKASSERT(TAILQ_EMPTY(&core->ownerq));
			KKASSERT(RB_EMPTY(&core->rbtree) &&
				 RB_EMPTY(&core->dbtree) &&
				 TAILQ_EMPTY(&core->dbq) &&
				 core->chain_count == 0);
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
	KKASSERT((chain->flags & (HAMMER2_CHAIN_FLUSH_CREATE |
				  HAMMER2_CHAIN_FLUSH_DELETE |
				  HAMMER2_CHAIN_MODIFIED)) == 0);
	hammer2_chain_drop_data(chain, 1);

	KKASSERT(chain->dio == NULL);

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
	/*hammer2_mount_t *hmp = chain->hmp;*/

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
		hammer2_adjreadcounter(&chain->bref, chain->bytes);
	}

	if (error) {
		kprintf("hammer2_chain_lock: I/O error %016jx: %d\n",
			(intmax_t)bref->data_off, error);
		hammer2_io_bqrelse(&chain->dio);
		ccms_thread_lock_downgrade(&core->cst, ostate);
		return (error);
	}

#if 0
	/*
	 * No need for this, always require that hammer2_chain_modify()
	 * be called before any modifying operations.
	 */
	if ((chain->flags & HAMMER2_CHAIN_MODIFIED) &&
	    !hammer2_io_isdirty(chain->dio)) {
		hammer2_io_setdirty(chain->dio);
	}
#endif

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
	ccms_thread_lock_downgrade(&core->cst, ostate);
	return (0);
}

/*
 * This basically calls hammer2_io_breadcb() but does some pre-processing
 * of the chain first to handle certain cases.
 */
void
hammer2_chain_load_async(hammer2_cluster_t *cluster,
			 void (*callback)(hammer2_io_t *dio,
					  hammer2_cluster_t *cluster,
					  hammer2_chain_t *chain,
					  void *arg_p, off_t arg_o),
			 void *arg_p)
{
	hammer2_chain_t *chain;
	hammer2_mount_t *hmp;
	struct hammer2_io *dio;
	hammer2_blockref_t *bref;
	int error;
	int i;

	/*
	 * If no chain specified see if any chain data is available and use
	 * that, otherwise begin an I/O iteration using the first chain.
	 */
	chain = NULL;
	for (i = 0; i < cluster->nchains; ++i) {
		chain = cluster->array[i];
		if (chain->data)
			break;
	}
	if (i == cluster->nchains) {
		chain = cluster->array[0];
		i = 0;
	}

	if (chain->data) {
		callback(NULL, cluster, chain, arg_p, (off_t)i);
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
		callback(dio, cluster, chain, arg_p, (off_t)i);
		return;
	}

	/*
	 * Otherwise issue a read
	 */
	hammer2_adjreadcounter(&chain->bref, chain->bytes);
	hammer2_io_breadcb(hmp, bref->data_off, chain->bytes,
			   callback, cluster, chain, arg_p, (off_t)i);
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
	 *
	 * (data blocks only, we aren't copying the storage here).
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

	*chainp = chain;
}

#if 0

/*
 * REMOVED - see cluster code
 *
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

#endif

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
	if (chain->modify_xid != trans->sync_xid &&	   /* cross boundary */
	    (flags & HAMMER2_MODIFY_INPLACE) == 0) {	   /* from d-d */
		if (chain != &hmp->fchain && chain != &hmp->vchain) {
			KKASSERT((flags & HAMMER2_MODIFY_ASSERTNOCOPY) == 0);
			hammer2_chain_delete_duplicate(trans, chainp, 0);
			chain = *chainp;
		}
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
	 * that the chain has been modified.  Set FLUSH_CREATE to flush
	 * the new blockref (the D-D set FLUSH_DELETE on the old chain to
	 * delete the old blockref).
	 */
	if ((chain->flags & HAMMER2_CHAIN_MODIFIED) == 0) {
		atomic_set_int(&chain->flags, HAMMER2_CHAIN_MODIFIED);
		hammer2_chain_ref(chain);
		hammer2_pfs_memory_inc(chain->pmp);
	}
	if ((chain->flags & HAMMER2_CHAIN_FLUSH_CREATE) == 0) {
		atomic_set_int(&chain->flags, HAMMER2_CHAIN_FLUSH_CREATE);
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
		      chain->modify_xid != trans->sync_xid)
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
	 * Update modify_xid.  XXX special-case vchain/fchain because they
	 * are always modified in-place.  Otherwise the chain being modified
	 * must not be part of a future transaction.
	 */
	if (chain == &hmp->vchain || chain == &hmp->fchain) {
		if (chain->modify_xid <= trans->sync_xid)
			chain->modify_xid = trans->sync_xid;
	} else {
		KKASSERT(chain->modify_xid <= trans->sync_xid);
		chain->modify_xid = trans->sync_xid;
	}

	/*
	 * Do not COW BREF_TYPE_DATA when OPTDATA is set.  This is because
	 * data modifications are done via the logical buffer cache so COWing
	 * it here would result in unnecessary extra copies (and possibly extra
	 * block reallocations).  The INITIAL flag remains unchanged in this
	 * situation.
	 *
	 * (This is a bit of a hack).
	 */
	if (chain->bref.type == HAMMER2_BREF_TYPE_DATA &&
	    (flags & HAMMER2_MODIFY_OPTDATA)) {
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
		KKASSERT(error == 0);

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
		 * Retire the old buffer, replace with the new
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
	hammer2_chain_setsubmod(trans, chain);
}

/*
 * Volume header data locks
 */
void
hammer2_voldata_lock(hammer2_mount_t *hmp)
{
	lockmgr(&hmp->vollk, LK_EXCLUSIVE);
}

void
hammer2_voldata_unlock(hammer2_mount_t *hmp)
{
	lockmgr(&hmp->vollk, LK_RELEASE);
}

void
hammer2_voldata_modify(hammer2_mount_t *hmp)
{
	if ((hmp->vchain.flags & HAMMER2_CHAIN_MODIFIED) == 0) {
		atomic_set_int(&hmp->vchain.flags, HAMMER2_CHAIN_MODIFIED);
		hammer2_chain_ref(&hmp->vchain);
	}
}

/*
 * This function returns the chain at the nearest key within the specified
 * range with the highest delete_xid.  The core spinlock must be held on
 * call and the returned chain will be referenced but not locked.
 *
 * The returned chain may or may not be in a deleted state.  Note that
 * live chains have a delete_xid = XID_MAX.
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

	info.best = NULL;
	info.key_beg = key_beg;
	info.key_end = key_end;
	info.key_next = *key_nextp;

	KKASSERT(parent->core->good == 0x1234);
	RB_SCAN(hammer2_chain_tree, &parent->core->rbtree,
		hammer2_chain_find_cmp, hammer2_chain_find_callback,
		&info);
	*key_nextp = info.key_next;
#if 0
	kprintf("chain_find %p %016jx:%016jx next=%016jx\n",
		parent, key_beg, key_end, *key_nextp);
#endif

	return (info.best);
}

/*
 * Find a deleted chain covering a block table entry.  Be careful to deal
 * with the race condition where the block table has been updated but the
 * chain has not yet been removed from dbtree (due to multiple parents having
 * to be updated).
 */
static
hammer2_chain_t *
hammer2_chain_find_deleted(hammer2_chain_t *parent,
			  hammer2_key_t key_beg, hammer2_key_t key_end)
{
	struct hammer2_chain_find_info info;
	hammer2_chain_t *child;

	info.best = NULL;
	info.key_beg = key_beg;
	info.key_end = key_end;
	info.key_next = 0;

	KKASSERT(parent->core->good == 0x1234);
	RB_SCAN(hammer2_chain_tree, &parent->core->dbtree,
		hammer2_chain_find_cmp, hammer2_chain_find_callback,
		&info);
	if ((child = info.best) != NULL) {
		if (child->delete_xid <= parent->update_xlo)
			child = NULL;
	}
	return child;
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
		 * also flush with key_beg choose based on delete_xid.
		 *
		 * key_next will automatically be limited to the smaller of
		 * the two end-points.
		 */
		if (child->delete_xid > best->delete_xid)
			info->best = child;
	} else if (child->bref.key < best->bref.key) {
		/*
		 * Child has a nearer key and best is not flush with key_beg.
		 * Truncate key_next to the old best key iff it had a better
		 * delete_xid.
		 */
		info->best = child;
		if (best->delete_xid >= child->delete_xid &&
		    (info->key_next > best->bref.key || info->key_next == 0))
			info->key_next = best->bref.key;
	} else if (child->bref.key == best->bref.key) {
		/*
		 * If our current best is flush with the child then choose
		 * based on delete_xid.
		 *
		 * key_next will automatically be limited to the smaller of
		 * the two end-points.
		 */
		if (child->delete_xid > best->delete_xid)
			info->best = child;
	} else {
		/*
		 * Keep the current best but truncate key_next to the child's
		 * base iff the child has a higher delete_xid.
		 *
		 * key_next will also automatically be limited to the smaller
		 * of the two end-points (probably not necessary for this case
		 * but we do it anyway).
		 */
		if (child->delete_xid >= best->delete_xid &&
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
 * in-memory chain structure which reflects it.  modify_xid will be
 * set to the min value which forces any modifications to issue a
 * delete-duplicate.
 *
 * To handle insertion races pass the INSERT_RACE flag along with the
 * generation number of the core.  NULL will be returned if the generation
 * number changes before we have a chance to insert the chain.  Insert
 * races can occur because the parent might be held shared.
 *
 * Caller must hold the parent locked shared or exclusive since we may
 * need the parent's bref array to find our block.
 *
 * WARNING! chain->pmp is left NULL if the bref represents a PFS mount
 *	    point.
 */
hammer2_chain_t *
hammer2_chain_get(hammer2_chain_t *parent, int generation,
		  hammer2_blockref_t *bref)
{
	hammer2_mount_t *hmp = parent->hmp;
	hammer2_chain_core_t *above = parent->core;
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
	hammer2_chain_core_alloc(NULL, chain, NULL);
	/* ref'd chain returned */

	/*
	 * Set modify_xid and update_xlo to the chain's synchronization
	 * point from the media.
	 */
	chain->modify_xid = HAMMER2_XID_MIN;
	chain->update_xlo = HAMMER2_XID_MIN;
	atomic_set_int(&chain->flags, HAMMER2_CHAIN_BMAPPED);

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
		KKASSERT((chain->flags & (HAMMER2_CHAIN_ONRBTREE |
					  HAMMER2_CHAIN_ONDBTREE |
					  HAMMER2_CHAIN_ONDBQ)) == 0);
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
		     int *cache_indexp, int flags, int *ddflagp)
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
	int wasdup;

	*ddflagp = 0;
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
			*ddflagp = 1;
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
				      key_beg, key_end,
				      &bref);
	generation = above->generation;

	/*
	 * Exhausted parent chain, iterate.
	 */
	if (bref == NULL) {
		spin_unlock(&above->cst.spin);
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
		spin_unlock(&above->cst.spin);
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
		wasdup = 0;
	} else {
		hammer2_chain_ref(chain);
		wasdup = ((chain->flags & HAMMER2_CHAIN_DUPLICATED) != 0);
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
	 * NOTE: Lookups can race delete-duplicate because
	 *	 delete-duplicate does not lock the parent's core
	 *	 (they just use the spinlock on the core).  We must
	 *	 check for races by comparing the DUPLICATED flag before
	 *	 releasing the spinlock with the flag after locking the
	 *	 chain.
	 */
	if (chain->flags & HAMMER2_CHAIN_DELETED) {
		hammer2_chain_unlock(chain);
		if ((chain->flags & HAMMER2_CHAIN_DUPLICATED) == 0 || wasdup) {
			key_beg = *key_nextp;
			if (key_beg == 0 || key_beg > key_end)
				return(NULL);
		}
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
	int ddflag;

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
				     cache_indexp, flags, &ddflag));
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
	int wasdup;

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
				      key, HAMMER2_KEY_MAX,
				      &bref);
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
		wasdup = 0;
	} else {
		hammer2_chain_ref(chain);
		wasdup = ((chain->flags & HAMMER2_CHAIN_DUPLICATED) != 0);
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
	 * NOTE: Lookups can race delete-duplicate because
	 *	 delete-duplicate does not lock the parent's core
	 *	 (they just use the spinlock on the core).  We must
	 *	 check for races by comparing the DUPLICATED flag before
	 *	 releasing the spinlock with the flag after locking the
	 *	 chain.
	 */
	if (chain->flags & HAMMER2_CHAIN_DELETED) {
		hammer2_chain_unlock(chain);
		chain = NULL;

		if ((chain->flags & HAMMER2_CHAIN_DUPLICATED) == 0 || wasdup) {
			key = next_key;
			if (key == 0)
				goto done;
		}
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
		     hammer2_chain_t **chainp, hammer2_pfsmount_t *pmp,
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

	/*
	 * Topology may be crossing a PFS boundary.
	 */
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
		chain = hammer2_chain_alloc(hmp, pmp, trans, &dummy);
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
	} else {
		/*
		 * We are reattaching a chain that has been duplicated and
		 * left disconnected under a DIFFERENT parent with potentially
		 * different key/keybits.
		 *
		 * The chain must be modified in the current transaction
		 * (the duplication code should have done that for us),
		 * and it's modify_xid should be greater than the parent's
		 * bref.mirror_tid.  This should cause it to be created under
		 * the new parent.
		 *
		 * If deleted in the same transaction, the create/delete TIDs
		 * will be the same and effective the chain will not have
		 * existed at all from the point of view of the parent.
		 *
		 * Do NOT mess with the current state of the INITIAL flag.
		 */
		KKASSERT(chain->modify_xid == trans->sync_xid);
		chain->bref.key = key;
		chain->bref.keybits = keybits;
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
	 * Link the chain into its parent.
	 */
	if (chain->above != NULL)
		panic("hammer2: hammer2_chain_create: chain already connected");
	KKASSERT(chain->above == NULL);
	hammer2_chain_insert(above, NULL, chain,
			     HAMMER2_CHAIN_INSERT_SPIN |
			     HAMMER2_CHAIN_INSERT_LIVE,
			     0);

	if (allocated) {
		/*
		 * Mark the newly created chain modified.  This will cause
		 * FLUSH_CREATE to be set.
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
			hammer2_chain_modify(trans, &chain,
					     HAMMER2_MODIFY_OPTDATA |
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
		 * When reconnecting a chain we must set FLUSH_CREATE and
		 * setsubmod so the flush recognizes that it must update
		 * the bref in the parent.
		 */
		if ((chain->flags & HAMMER2_CHAIN_FLUSH_CREATE) == 0) {
			hammer2_chain_ref(chain);
			atomic_set_int(&chain->flags,
				       HAMMER2_CHAIN_FLUSH_CREATE);
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
 * unlocked and the replacement will be returned locked.  The duplicated
 * chain is inserted under (*parentp).
 *
 * THE CALLER MUST HAVE ALREADY PROPERLY SEEKED (*parentp) TO THE INSERTION
 * POINT SANS ANY REQUIRED INDIRECT BLOCK CREATIONS DUE TO THE ARRAY BEING
 * FULL.  This typically means that the caller is creating the chain after
 * doing a hammer2_chain_lookup().
 *
 * A non-NULL bref is typically passed when key and keybits must be overridden.
 * Note that hammer2_cluster_duplicate() *ONLY* uses the key and keybits fields
 * from a passed-in bref and uses the old chain's bref for everything else.
 *
 * The old chain must be in a DELETED state unless snapshot is non-zero.
 *
 * The new chain will be live (i.e. not deleted), and modified.
 *
 * If (parent) is non-NULL then the new duplicated chain is inserted under
 * the parent.
 *
 * If (parent) is NULL then the newly duplicated chain is not inserted
 * anywhere, similar to if it had just been chain_alloc()'d (suitable for
 * passing into hammer2_chain_create() after this function returns).
 *
 * WARNING! This function cannot take snapshots all by itself.  The caller
 *	    needs to do other massaging for snapshots.
 *
 * WARNING! This function calls create which means it can insert indirect
 *	    blocks.  Callers may have to refactor locked chains held across
 *	    the call (other than the ones passed into the call).
 */
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
	 * operation even if modify_xid indicates that one is not needed.
	 *
	 * We don't want to set FORCECOW on nchain simply as an optimization,
	 * as many duplication calls simply move chains into ichains and
	 * then delete the original.
	 *
	 * WARNING!  We should never resolve DATA to device buffers
	 *	     (XXX allow it if the caller did?), and since
	 *	     we currently do not have the logical buffer cache
	 *	     buffer in-hand to fix its cached physical offset
	 *	     we also force the modify code to not COW it. XXX
	 */
	ochain = *chainp;
	hmp = ochain->hmp;
	KKASSERT(snapshot == 1 || (ochain->flags & HAMMER2_CHAIN_DELETED));

	/*
	 * Now create a duplicate of the chain structure, associating
	 * it with the same core, making it the same size, pointing it
	 * to the same bref (the same media block).
	 *
	 * Give nchain the same modify_xid that we previously ensured was
	 * sufficiently advanced to trigger a block table insertion on flush.
	 *
	 * nchain copies ochain's data and must inherit ochain->update_xlo.
	 *
	 * NOTE: bref.mirror_tid duplicated by virtue of bref copy in
	 *	 hammer2_chain_alloc()
	 */
	if (bref == NULL)
		bref = &ochain->bref;
	if (snapshot) {
		nchain = hammer2_chain_alloc(hmp, NULL, trans, bref);
		atomic_set_int(&nchain->flags, HAMMER2_CHAIN_SNAPSHOT);
	} else {
		nchain = hammer2_chain_alloc(hmp, ochain->pmp, trans, bref);
	}
	hammer2_chain_core_alloc(trans, nchain, ochain);
	bytes = (hammer2_off_t)1 <<
		(int)(bref->data_off & HAMMER2_OFF_MASK_RADIX);
	nchain->bytes = bytes;
	nchain->modify_xid = ochain->modify_xid;
	nchain->update_xlo = ochain->update_xlo;
	nchain->inode_reason = ochain->inode_reason + 0x100000;
	atomic_set_int(&nchain->flags,
		       ochain->flags & (HAMMER2_CHAIN_INITIAL |
					HAMMER2_CHAIN_FORCECOW |
					HAMMER2_CHAIN_UNLINKED |
					HAMMER2_CHAIN_PFSROOT |
					HAMMER2_CHAIN_PFSBOUNDARY));
	if (ochain->modify_xid == trans->sync_xid)
		atomic_set_int(&ochain->flags, HAMMER2_CHAIN_FORCECOW);

	/*
	 * Switch from ochain to nchain
	 */
	hammer2_chain_lock(nchain, HAMMER2_RESOLVE_NEVER |
				   HAMMER2_RESOLVE_NOREF);
	/* nchain has 1 ref */
	hammer2_chain_unlock(ochain);

	/*
	 * Place nchain in the modified state, instantiate media data
	 * if necessary.  Because modify_xid is already completely
	 * synchronized this should not result in a delete-duplicate.
	 *
	 * We want nchain at the target to look like a new insertion.
	 * Forcing the modification to be INPLACE accomplishes this
	 * because we get the same nchain with an updated modify_xid.
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
	 * the parent and the FLUSH_CREATE bit set to tell flush to update
	 * the blockref.
	 *
	 * Having both chains locked is extremely important for atomicy.
	 */
	if (parentp && (parent = *parentp) != NULL) {
		above = parent->core;
		KKASSERT(ccms_thread_lock_owned(&above->cst));
		KKASSERT((nchain->flags & HAMMER2_CHAIN_DELETED) == 0);
		KKASSERT(parent->refs > 0);

		hammer2_chain_create(trans, parentp, &nchain, nchain->pmp,
				     nchain->bref.key, nchain->bref.keybits,
				     nchain->bref.type, nchain->bytes);
		parent = NULL;

		KKASSERT(nchain->flags & HAMMER2_CHAIN_FLUSH_CREATE);
		hammer2_chain_setsubmod(trans, nchain);
	}

	*chainp = nchain;
}

/*
 * Helper function for deleting chains.
 *
 * The chain is removed from the live view (the RBTREE).
 *
 * If appropriate, the chain is added to the shadow topology and FLUSH_DELETE
 * is set for flusher visbility.  The caller is responsible for calling
 * setsubmod on chain, so we do not adjust update_xhi here.
 */
static void
_hammer2_chain_delete_helper(hammer2_trans_t *trans,
			     hammer2_chain_core_t *above,
			     hammer2_chain_t *chain)
{
	hammer2_mount_t *hmp;
	hammer2_chain_t *xchain;

	KKASSERT(chain->flags & HAMMER2_CHAIN_ONRBTREE);
	KKASSERT(trans->sync_xid >= chain->modify_xid);
	KKASSERT((chain->flags & (HAMMER2_CHAIN_DELETED |
				  HAMMER2_CHAIN_ONDBQ |
				  HAMMER2_CHAIN_ONDBTREE |
				  HAMMER2_CHAIN_FLUSH_DELETE)) == 0);

	/*
	 * Flag as deleted, reduce live_count and bump the above core's
	 * generation.
	 */
	chain->delete_xid = trans->sync_xid;
	atomic_set_int(&chain->flags, HAMMER2_CHAIN_DELETED);
	atomic_add_int(&above->live_count, -1);
	++above->generation;
	hmp = chain->hmp;

	/*
	 * Remove from live tree
	 */
	RB_REMOVE(hammer2_chain_tree, &above->rbtree, chain);
	atomic_clear_int(&chain->flags, HAMMER2_CHAIN_ONRBTREE);

	if (chain->flags & HAMMER2_CHAIN_BMAPPED) {
		/*
		 * If the chain was originally bmapped we must place on the
		 * deleted tree and set FLUSH_DELETE (+ref) to prevent
		 * destruction of the chain until the flush can reconcile
		 * the parent's block table.
		 *
		 * NOTE! DBTREE is only representitive of the live view,
		 *	 the flush must check both DBTREE and DBQ.
		 */
		xchain = RB_INSERT(hammer2_chain_tree, &above->dbtree, chain);
		KKASSERT(xchain == NULL);
		atomic_set_int(&chain->flags, HAMMER2_CHAIN_ONDBTREE);

		atomic_set_int(&chain->flags, HAMMER2_CHAIN_FLUSH_DELETE);
		hammer2_chain_ref(chain);
	} else {
		/*
		 * If the chain no longer (and never had) an actual blockmap
		 * entry we must place it on the dbq list and set FLUSH_DELETE
		 * (+ref) to prevent destruction of the chain until the flush
		 * can reconcile the parent's block table.
		 *
		 * NOTE! DBTREE is only representitive of the live view,
		 *	 the flush must check both DBTREE and DBQ.
		 */
		TAILQ_INSERT_TAIL(&above->dbq, chain, db_entry);
		atomic_set_int(&chain->flags, HAMMER2_CHAIN_ONDBQ);

		atomic_set_int(&chain->flags, HAMMER2_CHAIN_FLUSH_DELETE);
		hammer2_chain_ref(chain);
	}
}

/*
 * Special in-place delete-duplicate sequence which does not require a
 * locked parent.  (*chainp) is marked DELETED and atomically replaced
 * with a duplicate.  Atomicy is at the very-fine spin-lock level in
 * order to ensure that lookups do not race us.
 *
 * The flush code will sometimes call this function with a deleted chain.
 * In this situation the old chain's memory is reallocated without
 * duplicating it.
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
	uint32_t oflags;

	if (hammer2_debug & 0x20000)
		Debugger("dd");

	/*
	 * Note that we do not have to call setsubmod on ochain, calling it
	 * on nchain is sufficient.
	 */
	ochain = *chainp;
	oflags = ochain->flags;		/* flags prior to core_alloc mods */
	hmp = ochain->hmp;

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
	 *
	 * WARNING! Flusher concurrency can create two cases.  The first is
	 *	    that the flusher might be working on a chain that has
	 *	    been deleted in the live view but is live in the flusher's
	 *	    view.  In the second case the flusher may be duplicating
	 *	    a forward-transacted chain.  In both situations nchain
	 *	    must be marked deleted.
	 *
	 * WARNING! hammer2_chain_core_alloc() also acts on these issues.
	 */
	nchain = hammer2_chain_alloc(hmp, ochain->pmp, trans, &ochain->bref);
	if ((ochain->flags & HAMMER2_CHAIN_DELETED) ||
	    (ochain->modify_xid > trans->sync_xid)) {
		atomic_set_int(&nchain->flags, HAMMER2_CHAIN_DELETED);
	}
	if (flags & HAMMER2_DELDUP_RECORE)
		hammer2_chain_core_alloc(trans, nchain, NULL);
	else
		hammer2_chain_core_alloc(trans, nchain, ochain);
	above = ochain->above;

	bytes = (hammer2_off_t)1 <<
		(int)(ochain->bref.data_off & HAMMER2_OFF_MASK_RADIX);
	nchain->bytes = bytes;

	/*
	 * nchain inherits ochain's live state including its modification
	 * state.  This function disposes of the original.  Because we are
	 * doing this in-place under the same parent the block array
	 * inserted/deleted state does not change.
	 *
	 * nchain copies ochain's data and must inherit ochain->update_xlo.
	 *
	 * If ochain was previously marked FORCECOW we also flag nchain
	 * FORCECOW (used during hardlink splits).  FORCECOW forces a
	 * reallocation of the block when we modify the chain a little later,
	 * it does not force another delete-duplicate.
	 *
	 * NOTE: bref.mirror_tid duplicated by virtue of bref copy in
	 *	 hammer2_chain_alloc()
	 */
	nchain->data_count += ochain->data_count;
	nchain->inode_count += ochain->inode_count;
	atomic_set_int(&nchain->flags,
		       ochain->flags & (HAMMER2_CHAIN_INITIAL |
					HAMMER2_CHAIN_FORCECOW |
					HAMMER2_CHAIN_UNLINKED |
					HAMMER2_CHAIN_PFSROOT |
					HAMMER2_CHAIN_PFSBOUNDARY));
	if (ochain->modify_xid == trans->sync_xid)
		atomic_set_int(&ochain->flags, HAMMER2_CHAIN_FORCECOW);
	nchain->inode_reason = ochain->inode_reason + 0x1000;
	nchain->update_xlo = ochain->update_xlo;

	/*
	 * Lock nchain so both chains are now locked (extremely important
	 * for atomicy).  The shared core allows us to unlock ochain without
	 * actually unlocking ochain.
	 */
	hammer2_chain_lock(nchain, HAMMER2_RESOLVE_NEVER);
	/* extra ref still present from original allocation */

	KKASSERT(ochain->flags & (HAMMER2_CHAIN_ONRBTREE |
				  HAMMER2_CHAIN_ONDBTREE |
				  HAMMER2_CHAIN_ONDBQ));
	spin_lock(&above->cst.spin);

	nchain->modify_xid = ochain->modify_xid;
	nchain->delete_xid = HAMMER2_XID_MAX;

	if ((nchain->flags & HAMMER2_CHAIN_DELETED) &&
	    (oflags & HAMMER2_CHAIN_DUPLICATED)) {
		/*
		 * Special case, used by the flush code when a chain which
		 * has been delete-duplicated is visible (effectively 'live')
		 * in the flush code.
		 *
		 * In this situations nchain will be marked deleted and
		 * insert before ochain.  nchain must inherit certain features
		 * of ochain.
		 */
		KKASSERT(trans->flags & HAMMER2_TRANS_ISFLUSH);
		KKASSERT(ochain->modify_xid < trans->sync_xid);
		KKASSERT(ochain->delete_xid > trans->sync_xid);
		atomic_set_int(&nchain->flags, HAMMER2_CHAIN_FLUSH_TEMPORARY);
		hammer2_chain_insert(above, ochain, nchain, 0, 0);

		if ((ochain->flags & HAMMER2_CHAIN_DELETED) &&
		    ochain->modify_xid < trans->sync_xid) {
			nchain->delete_xid = ochain->delete_xid;
			ochain->delete_xid = trans->sync_xid;
		} else if (ochain->modify_xid > trans->sync_xid) {
			nchain->delete_xid = ochain->modify_xid;
		}
	} else if (nchain->flags & HAMMER2_CHAIN_DELETED) {
		/*
		 * ochain is 'live' with respect to not having been D-D'd,
		 * but is flagged DELETED.  Sometimes updates to deleted
		 * chains must be allowed due to references which still exist
		 * on those chains, or due to a flush trying to retire a
		 * logical buffer cache buffer.
		 *
		 * In this situation the D-D operates normally, except
		 * ochain has already been deleted and nchain is also
		 * marked deleted.
		 */
		hammer2_chain_insert(above, ochain, nchain, 0, 0);
		nchain->delete_xid = trans->sync_xid;
	} else {
		/*
		 * Normal case, delete-duplicate deletes ochain and nchain
		 * is the new live chain.
		 */
		_hammer2_chain_delete_helper(trans, above, ochain);
		hammer2_chain_insert(above, ochain, nchain,
				     HAMMER2_CHAIN_INSERT_LIVE, 0);
	}
	spin_unlock(&above->cst.spin);

	/*
	 * ochain must be unlocked because ochain and nchain might share
	 * a buffer cache buffer, so we need to release it so nchain can
	 * potentially obtain it.
	 */
	hammer2_chain_setsubmod(trans, ochain);
	hammer2_chain_unlock(ochain);

	/*
	 * Finishing fixing up nchain.  A new block will be allocated if
	 * crossing a synchronization point (meta-data only).
	 *
	 * Calling hammer2_chain_modify() will update modify_xid to
	 * (typically) trans->sync_xid.
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
	 * Unconditionally set FLUSH_CREATE to force the parent blockrefs to
	 * update as the chain_modify() above won't necessarily do it.
	 */
	if ((nchain->flags & HAMMER2_CHAIN_FLUSH_CREATE) == 0) {
		atomic_set_int(&nchain->flags, HAMMER2_CHAIN_FLUSH_CREATE);
		hammer2_chain_ref(nchain);
	}

	/*
	 * If nchain is in a DELETED state we must set FLUSH_DELETE
	 */
	if (nchain->flags & HAMMER2_CHAIN_DELETED)
		KKASSERT((nchain->flags & HAMMER2_CHAIN_FLUSH_DELETE) == 0);
#if 1
	if ((nchain->flags & HAMMER2_CHAIN_FLUSH_DELETE) == 0 &&
	    (nchain->flags & HAMMER2_CHAIN_DELETED)) {
		atomic_set_int(&nchain->flags, HAMMER2_CHAIN_FLUSH_DELETE);
		hammer2_chain_ref(nchain);
	}
#endif
	hammer2_chain_setsubmod(trans, nchain);
	*chainp = nchain;
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
	int retry_same;
	int wasdup;

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
	dummy.delete_xid = HAMMER2_XID_MAX;

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
	hammer2_chain_core_alloc(trans, ichain, NULL);
	icore = ichain->core;
	hammer2_chain_lock(ichain, HAMMER2_RESOLVE_MAYBE);
	hammer2_chain_drop(ichain);	/* excess ref from alloc */

	/*
	 * We have to mark it modified to allocate its block, but use
	 * OPTDATA to allow it to remain in the INITIAL state.  Otherwise
	 * it won't be acted upon by the flush code.
	 */
	hammer2_chain_modify(trans, &ichain, HAMMER2_MODIFY_OPTDATA);

	/*
	 * Iterate the original parent and move the matching brefs into
	 * the new indirect block.
	 *
	 * XXX handle flushes.
	 */
	key_beg = 0;
	key_end = HAMMER2_KEY_MAX;
	cache_index = 0;
	spin_lock(&above->cst.spin);
	loops = 0;
	reason = 0;
	retry_same = 0;

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
			wasdup = ((chain->flags &
				   HAMMER2_CHAIN_DUPLICATED) != 0);
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
			chain = hammer2_chain_get(parent, generation, &bcopy);
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
			wasdup = 0;
		}

		/*
		 * This is always live so if the chain has been delete-
		 * duplicated we raced someone and we have to retry.
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
			if ((chain->flags & HAMMER2_CHAIN_DUPLICATED) &&
			    wasdup == 0) {
				retry_same = 1;
			}
			goto next_key;
		}

		/*
		 * Shift the chain to the indirect block.
		 *
		 * WARNING! Can cause held-over chains to require a refactor.
		 *	    Fortunately we have none (our locked chains are
		 *	    passed into and modified by the call).
		 */
		hammer2_chain_delete(trans, chain, 0);
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
		if (retry_same == 0) {
			if (key_next == 0 || key_next > key_end)
				break;
			key_beg = key_next;
		}
		/* loop */
	}
	spin_unlock(&above->cst.spin);

	/*
	 * Insert the new indirect block into the parent now that we've
	 * cleared out some entries in the parent.  We calculated a good
	 * insertion index in the loop above (ichain->index).
	 *
	 * We don't have to set FLUSH_CREATE here because we mark ichain
	 * modified down below (so the normal modified -> flush -> set-moved
	 * sequence applies).
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
	 */
	/*hammer2_chain_modify(trans, &ichain, HAMMER2_MODIFY_OPTDATA);*/
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
	key_end = HAMMER2_KEY_MAX;
	cache_index = 0;
	spin_lock(&above->cst.spin);

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
	key_end = HAMMER2_KEY_MAX;
	cache_index = 0;
	spin_lock(&above->cst.spin);

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
 * Sets CHAIN_DELETED and CHAIN_FLUSH_DELETE in the chain being deleted and
 * set chain->delete_xid.  The chain is not actually marked possibly-free
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
	spin_lock(&chain->above->cst.spin);
	_hammer2_chain_delete_helper(trans, chain->above, chain);
	spin_unlock(&chain->above->cst.spin);

	hammer2_chain_setsubmod(trans, chain);
}

/*
 * Returns the index of the nearest element in the blockref array >= elm.
 * Returns (count) if no element could be found.  If delete_filter is non-zero
 * the scan filters out any blockrefs which match deleted chains on dbtree.
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
hammer2_base_find(hammer2_chain_t *parent,
		  hammer2_blockref_t *base, int count,
		  int *cache_indexp, hammer2_key_t *key_nextp,
		  hammer2_key_t key_beg, hammer2_key_t key_end,
		  int delete_filter)
{
	hammer2_chain_core_t *core = parent->core;
	hammer2_blockref_t *scan;
	hammer2_key_t scan_end;
	int i;
	int limit;

	/*
	 * Require the live chain's already have their core's counted
	 * so we can optimize operations.
	 */
        KKASSERT((parent->flags & HAMMER2_CHAIN_DUPLICATED) ||
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
	if (parent->flags & HAMMER2_CHAIN_DUPLICATED)
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
			scan_end = scan->key +
				   ((hammer2_key_t)1 << scan->keybits) - 1;
			if (scan->key > key_beg || scan_end >= key_beg) {
				/*
				 * Check to see if the entry is covered by
				 * a deleted chain and ignore the entry if
				 * it is and delete_filter != 0.
				 */
				if (delete_filter == 0)
					break;
				if (hammer2_chain_find_deleted(
					parent, scan->key, scan_end) == NULL) {
					break;
				}
			}
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
 * Must be called with above's spinlock held.  Spinlock remains held
 * through the operation.
 *
 * The returned chain is not locked or referenced.  Use the returned bref
 * to determine if the search exhausted or not.  Iterate if the base find
 * is chosen but matches a deleted chain.
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
			      key_nextp, key_beg, key_end, 1);
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
		if ((chain->flags & HAMMER2_CHAIN_BMAPPED) == 0) {
			kprintf("chain not bmapped %p.%d %08x\n",
				chain, chain->bref.type, chain->flags);
			kprintf("in chain mod/del %08x %08x\n",
				chain->modify_xid, chain->delete_xid);
			kprintf("and updlo/hi %08x %08x\n",
				chain->update_xlo, chain->update_xhi);
		}
		KKASSERT(chain->flags & HAMMER2_CHAIN_BMAPPED);
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
			      &key_next, elm->key, elm->key, 0);
	if (i == count || base[i].type == 0 ||
	    base[i].key != elm->key || base[i].keybits != elm->keybits) {
		spin_unlock(&core->cst.spin);
		panic("delete base %p element not found at %d/%d elm %p\n"
		      "child ino_reason=%08x\n",
		      base, i, count, elm,
		      child->inode_reason);
		return;
	}
	bzero(&base[i], sizeof(*base));

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
			      &key_next, elm->key, elm->key, 0);

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
		if (child->flags & HAMMER2_CHAIN_FLUSH_TEMPORARY) {
			kprintf("child %p special replace\n", child);
			base[i] = *elm;
			return;
		} else {
			spin_unlock(&core->cst.spin);
			panic("insert base %p overlapping "
			      "elements at %d elm %p\n",
			      base, i, elm);
		}
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
 * chain may have been moved around by the create.
 */
void
hammer2_chain_refactor(hammer2_chain_t **chainp)
{
	hammer2_chain_t *chain = *chainp;
	hammer2_chain_core_t *core;

	core = chain->core;
	while (chain->flags & HAMMER2_CHAIN_DUPLICATED) {
		spin_lock(&core->cst.spin);
		chain = TAILQ_NEXT(chain, core_entry);
		while (chain->flags & HAMMER2_CHAIN_DUPLICATED)
			chain = TAILQ_NEXT(chain, core_entry);
		hammer2_chain_ref(chain);
		spin_unlock(&core->cst.spin);
		KKASSERT(chain->core == core);

		hammer2_chain_unlock(*chainp);
		hammer2_chain_lock(chain, HAMMER2_RESOLVE_ALWAYS |
					  HAMMER2_RESOLVE_NOREF); /* eat ref */
		*chainp = chain;
	}
}
