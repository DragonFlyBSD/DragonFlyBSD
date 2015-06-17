/*
 * Copyright (c) 2015 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Matthew Dillon <dillon@dragonflybsd.org>
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
 * This module implements various PFS-based helper threads.
 */
#include "hammer2.h"

#define HAMMER2_SYNCTHR_DEBUG 1

static int hammer2_sync_slaves(hammer2_syncthr_t *thr,
			hammer2_cluster_t *cparent, int *errors);
static void hammer2_update_pfs_status(hammer2_syncthr_t *thr,
			hammer2_cluster_t *cparent);
static int hammer2_sync_insert(hammer2_syncthr_t *thr,
			hammer2_cluster_t *cparent, hammer2_cluster_t *cluster,
			hammer2_tid_t modify_tid,
			int i, int *errors);
static int hammer2_sync_destroy(hammer2_syncthr_t *thr,
			hammer2_cluster_t *cparent, hammer2_cluster_t *cluster,
			int i, int *errors);
static int hammer2_sync_replace(hammer2_syncthr_t *thr,
			hammer2_cluster_t *cparent, hammer2_cluster_t *cluster,
			hammer2_tid_t modify_tid,
			int i, int *errors);

/*
 * Initialize the suspplied syncthr structure, starting the specified
 * thread.
 */
void
hammer2_syncthr_create(hammer2_syncthr_t *thr, hammer2_pfs_t *pmp,
		       int clindex, void (*func)(void *arg))
{
	lockinit(&thr->lk, "h2syncthr", 0, 0);
	thr->pmp = pmp;
	thr->clindex = clindex;
	lwkt_create(func, thr, &thr->td, NULL, 0, -1,
		    "h2nod-%s", pmp->pfs_names[clindex]);
}

/*
 * Terminate a syncthr.  This function will silently return if the syncthr
 * was never initialized or has already been deleted.
 *
 * This is accomplished by setting the STOP flag and waiting for the td
 * structure to become NULL.
 */
void
hammer2_syncthr_delete(hammer2_syncthr_t *thr)
{
	if (thr->td == NULL)
		return;
	lockmgr(&thr->lk, LK_EXCLUSIVE);
	atomic_set_int(&thr->flags, HAMMER2_SYNCTHR_STOP);
	wakeup(&thr->flags);
	while (thr->td) {
		lksleep(thr, &thr->lk, 0, "h2thr", hz);
	}
	lockmgr(&thr->lk, LK_RELEASE);
	thr->pmp = NULL;
	lockuninit(&thr->lk);
}

/*
 * Asynchronous remaster request.  Ask the synchronization thread to
 * start over soon (as if it were frozen and unfrozen, but without waiting).
 * The thread always recalculates mastership relationships when restarting.
 */
void
hammer2_syncthr_remaster(hammer2_syncthr_t *thr)
{
	if (thr->td == NULL)
		return;
	lockmgr(&thr->lk, LK_EXCLUSIVE);
	atomic_set_int(&thr->flags, HAMMER2_SYNCTHR_REMASTER);
	wakeup(&thr->flags);
	lockmgr(&thr->lk, LK_RELEASE);
}

void
hammer2_syncthr_freeze(hammer2_syncthr_t *thr)
{
	if (thr->td == NULL)
		return;
	lockmgr(&thr->lk, LK_EXCLUSIVE);
	atomic_set_int(&thr->flags, HAMMER2_SYNCTHR_FREEZE);
	wakeup(&thr->flags);
	while ((thr->flags & HAMMER2_SYNCTHR_FROZEN) == 0) {
		lksleep(thr, &thr->lk, 0, "h2frz", hz);
	}
	lockmgr(&thr->lk, LK_RELEASE);
}

void
hammer2_syncthr_unfreeze(hammer2_syncthr_t *thr)
{
	if (thr->td == NULL)
		return;
	lockmgr(&thr->lk, LK_EXCLUSIVE);
	atomic_clear_int(&thr->flags, HAMMER2_SYNCTHR_FROZEN);
	wakeup(&thr->flags);
	lockmgr(&thr->lk, LK_RELEASE);
}

/*
 * Primary management thread for an element of a node.  A thread will exist
 * for each element requiring management.
 *
 * No management threads are needed for the SPMP or for any PMP with only
 * a single MASTER.
 *
 * On the SPMP - handles bulkfree and dedup operations
 * On a PFS    - handles remastering and synchronization
 */
void
hammer2_syncthr_primary(void *arg)
{
	hammer2_syncthr_t *thr = arg;
	hammer2_cluster_t *cparent;
	hammer2_chain_t *chain;
	hammer2_pfs_t *pmp;
	int errors[HAMMER2_MAXCLUSTER];
	int error;

	pmp = thr->pmp;

	lockmgr(&thr->lk, LK_EXCLUSIVE);
	while ((thr->flags & HAMMER2_SYNCTHR_STOP) == 0) {
		/*
		 * Handle freeze request
		 */
		if (thr->flags & HAMMER2_SYNCTHR_FREEZE) {
			atomic_set_int(&thr->flags, HAMMER2_SYNCTHR_FROZEN);
			atomic_clear_int(&thr->flags, HAMMER2_SYNCTHR_FREEZE);
		}

		/*
		 * Force idle if frozen until unfrozen or stopped.
		 */
		if (thr->flags & HAMMER2_SYNCTHR_FROZEN) {
			lksleep(&thr->flags, &thr->lk, 0, "frozen", 0);
			continue;
		}

		/*
		 * Reset state on REMASTER request
		 */
		if (thr->flags & HAMMER2_SYNCTHR_REMASTER) {
			atomic_clear_int(&thr->flags, HAMMER2_SYNCTHR_REMASTER);
			/* reset state */
		}

		/*
		 * Synchronization scan.
		 */
		hammer2_trans_init(&thr->trans, pmp, HAMMER2_TRANS_KEEPMODIFY);
		cparent = hammer2_inode_lock(pmp->iroot,
					     HAMMER2_RESOLVE_ALWAYS);
		hammer2_update_pfs_status(thr, cparent);
		hammer2_inode_unlock(pmp->iroot, NULL);
		bzero(errors, sizeof(errors));
		kprintf("sync_slaves clindex %d\n", thr->clindex);

		/*
		 * We are the syncer, not a normal frontend operator,
		 * so force cparent good to prime the scan.
		 */
		hammer2_cluster_forcegood(cparent);
		error = hammer2_sync_slaves(thr, cparent, errors);
		if (error)
			kprintf("hammer2_sync_slaves: error %d\n", error);
		chain = cparent->array[thr->clindex].chain;

		/*
		 * Retain chain for our node and release the cluster.
		 */
		hammer2_chain_ref(chain);
		hammer2_chain_lock(chain, HAMMER2_RESOLVE_ALWAYS);
		hammer2_cluster_unlock(cparent);
		hammer2_cluster_drop(cparent);

		/*
		 * Flush the chain.
		 */
		hammer2_flush(&thr->trans, chain, 1);
		hammer2_chain_unlock(chain);
		hammer2_chain_drop(chain);

		hammer2_trans_done(&thr->trans);

		/*
		 * Wait for event, or 5-second poll.
		 */
		lksleep(&thr->flags, &thr->lk, 0, "h2idle", hz * 5);
	}
	thr->td = NULL;
	wakeup(thr);
	lockmgr(&thr->lk, LK_RELEASE);
	/* thr structure can go invalid after this point */
}

/*
 * Given a locked cluster created from pmp->iroot, update the PFS's
 * reporting status.
 */
static
void
hammer2_update_pfs_status(hammer2_syncthr_t *thr, hammer2_cluster_t *cparent)
{
	hammer2_pfs_t *pmp = thr->pmp;
	uint32_t flags;

	flags = cparent->flags & HAMMER2_CLUSTER_ZFLAGS;
	if (pmp->flags == flags)
		return;
	pmp->flags = flags;

	kprintf("pfs %p", pmp);
	if (flags & HAMMER2_CLUSTER_MSYNCED)
		kprintf(" masters-all-good");
	if (flags & HAMMER2_CLUSTER_SSYNCED)
		kprintf(" slaves-all-good");

	if (flags & HAMMER2_CLUSTER_WRHARD)
		kprintf(" quorum/rw");
	else if (flags & HAMMER2_CLUSTER_RDHARD)
		kprintf(" quorum/ro");

	if (flags & HAMMER2_CLUSTER_UNHARD)
		kprintf(" out-of-sync-masters");
	else if (flags & HAMMER2_CLUSTER_NOHARD)
		kprintf(" no-masters-visible");

	if (flags & HAMMER2_CLUSTER_WRSOFT)
		kprintf(" soft/rw");
	else if (flags & HAMMER2_CLUSTER_RDSOFT)
		kprintf(" soft/ro");

	if (flags & HAMMER2_CLUSTER_UNSOFT)
		kprintf(" out-of-sync-slaves");
	else if (flags & HAMMER2_CLUSTER_NOSOFT)
		kprintf(" no-slaves-visible");
	kprintf("\n");
}

static
void
dumpcluster(const char *label,
	    hammer2_cluster_t *cparent, hammer2_cluster_t *cluster)
{
	hammer2_chain_t *chain;
	int i;

	if ((hammer2_debug & 1) == 0)
		return;

	kprintf("%s\t", label);
	KKASSERT(cparent->nchains == cluster->nchains);
	for (i = 0; i < cparent->nchains; ++i) {
		if (i)
			kprintf("\t");
		kprintf("%d ", i);
		if ((chain = cparent->array[i].chain) != NULL) {
			kprintf("%016jx%s ",
				chain->bref.key,
				((cparent->array[i].flags &
				  HAMMER2_CITEM_INVALID) ? "(I)" : "   ")
			);
		} else {
			kprintf("      NULL      %s ", "   ");
		}
		if ((chain = cluster->array[i].chain) != NULL) {
			kprintf("%016jx%s ",
				chain->bref.key,
				((cluster->array[i].flags &
				  HAMMER2_CITEM_INVALID) ? "(I)" : "   ")
			);
		} else {
			kprintf("      NULL      %s ", "   ");
		}
		kprintf("\n");
	}
}

/*
 * TODO - have cparent use a shared lock normally instead of exclusive,
 *	  (needs to be upgraded for slave adjustments).
 */
static
int
hammer2_sync_slaves(hammer2_syncthr_t *thr, hammer2_cluster_t *cparent,
		    int *errors)
{
	hammer2_pfs_t *pmp;
	hammer2_cluster_t *cluster;
	hammer2_cluster_t *scluster;
	hammer2_chain_t *focus;
	hammer2_chain_t *chain;
	hammer2_key_t key_next;
	int error;
	int nerror;
	int idx;
	int n;
	int nowork;
	int dorecursion;

	pmp = thr->pmp;
	idx = thr->clindex;	/* cluster node we are responsible for */

	/*
	 * Nothing to do if all slaves are synchronized.
	 * Nothing to do if cluster not authoritatively readable.
	 */
	if (pmp->flags & HAMMER2_CLUSTER_SSYNCED)
		return(0);
	if ((pmp->flags & HAMMER2_CLUSTER_RDHARD) == 0)
		return(HAMMER2_ERROR_INCOMPLETE);

	error = 0;

	/*
	 * XXX snapshot the source to provide a stable source to copy.
	 */

	/*
	 * Update all local slaves (remote slaves are handled by the sync
	 * threads on their respective hosts).
	 *
	 * Do a full topology scan, insert/delete elements on slaves as
	 * needed.  cparent must be ref'd so we can unlock and relock it
	 * on the recursion.
	 *
	 * ALLNODES - Allows clusters with a NULL focus to be returned if
	 *	      elements remain on other nodes.
	 */
	hammer2_cluster_ref(cparent);
	cluster = hammer2_cluster_lookup(cparent, &key_next,
					 HAMMER2_KEY_MIN, HAMMER2_KEY_MAX,
					 HAMMER2_LOOKUP_NODATA |
					 HAMMER2_LOOKUP_NOLOCK |
					 HAMMER2_LOOKUP_NODIRECT |
					 HAMMER2_LOOKUP_ALLNODES);
	dumpcluster("lookup", cparent, cluster);

	/*
	 * Scan elements
	 */
	while (cluster) {
		/*
		 * nowork is adjusted during the loop,
		 * dorecursion is calculated here.
		 */
		nowork = 1;
		focus = cluster->focus;
		if (focus && focus->bref.type == HAMMER2_BREF_TYPE_INODE)
			dorecursion = 1;
		else
			dorecursion = 0;

		if (idx == 3 && (hammer2_debug & 1) && focus)
			kprintf("scan3 focus %d.%016jx %d.%016jx\n",
			    (cparent ? cparent->focus->bref.type : 0xFF),
			    (cparent ? cparent->focus->bref.key : (uintmax_t)-1LLU),
			    focus->bref.type, focus->bref.key);
repeat1:
		/*
		 * Synchronize chains to focus
		 */
		if (idx >= cluster->nchains)
			goto skip1;
		chain = cluster->array[idx].chain;
		if (idx == 3 && (hammer2_debug & 1) && chain)
			kprintf("scan3 slave %d.%016jx %d.%016jx\n",
			    ((cparent && cparent->array[idx].chain) ? cparent->array[idx].chain->bref.type : 0xFF),
			    ((cparent && cparent->array[idx].chain) ? cparent->array[idx].chain->bref.key : (uintmax_t)-1LLU),
			    cluster->array[idx].chain->bref.type,
			    cluster->array[idx].chain->bref.key);
		if (idx == 3 && (hammer2_debug & 1) && chain == NULL)
			kprintf("scan3 slave %d.%16jx NULL\n",
			    ((cparent && cparent->array[idx].chain) ? cparent->array[idx].chain->bref.type : 0xFF),
			    ((cparent && cparent->array[idx].chain) ? cparent->array[idx].chain->bref.key : (uintmax_t)-1LLU)
			);

		/*
		 * Disable recursion for this index and loop up
		 * if a chain error is detected.
		 *
		 * A NULL chain is ok, it simply indicates that
		 * the slave reached the end of its scan, but we
		 * might have stuff from the master that still
		 * needs to be copied in.
		 */
		if (chain && chain->error) {
			kprintf("chain error index %d: %d\n",
				idx, chain->error);
			errors[idx] = chain->error;
			error = chain->error;
			cluster->array[idx].flags |= HAMMER2_CITEM_INVALID;
			goto skip1;
		}

		/*
		 * Skip if the slave already has the record (everything
		 * matches including the modify_tid).  Note that the
		 * mirror_tid does not have to match, mirror_tid is
		 * a per-block-device entity.
		 */
		if (chain &&
		    (cluster->array[idx].flags & HAMMER2_CITEM_INVALID) == 0) {
			goto skip1;
		}

		/*
		 * Invalid element needs to be updated.
		 */
		nowork = 0;

		/*
		 * Otherwise adjust the slave.  Compare the focus to
		 * the chain.  Note that focus and chain can
		 * independently be NULL.
		 */
		KKASSERT(cluster->focus == focus);
		if (focus) {
			if (chain)
				n = hammer2_chain_cmp(focus, chain);
			else
				n = -1;	/* end-of-scan on slave */
		} else {
			if (chain)
				n = 1;	/* end-of-scan on focus */
			else
				n = 0;	/* end-of-scan on both */
		}

		if (n < 0) {
			/*
			 * slave chain missing, create missing chain.
			 *
			 * If we are going to recurse we have to set
			 * the initial modify_tid to 0 until the
			 * sub-tree is completely synchronized.
			 * Setting (n = 0) in this situation forces
			 * the replacement call to run on the way
			 * back up after the sub-tree has
			 * synchronized.
			 */
			if (dorecursion) {
				nerror = hammer2_sync_insert(
						thr, cparent, cluster,
						0,
						idx, errors);
				if (nerror == 0)
					n = 0;
			} else {
				nerror = hammer2_sync_insert(
						thr, cparent, cluster,
						focus->bref.modify_tid,
						idx, errors);
			}
		} else if (n > 0) {
			/*
			 * excess slave chain, destroy
			 */
			nerror = hammer2_sync_destroy(thr,
						      cparent, cluster,
						      idx, errors);
			hammer2_cluster_next_single_chain(
				cparent, cluster,
				&key_next,
				HAMMER2_KEY_MIN,
				HAMMER2_KEY_MAX,
				idx,
				HAMMER2_LOOKUP_NODATA |
				HAMMER2_LOOKUP_NOLOCK |
				HAMMER2_LOOKUP_NODIRECT |
				HAMMER2_LOOKUP_ALLNODES);
			/*
			 * Re-execute same index, there might be more
			 * items to delete before this slave catches
			 * up to the focus.
			 */
			goto repeat1;
		} else {
			/*
			 * Key matched but INVALID was set which likely
			 * means that modify_tid is out of sync.
			 *
			 * If we are going to recurse we have to do
			 * a partial replacement of the parent to
			 * ensure that the block array is compatible.
			 * For example, the current slave inode might
			 * be flagged DIRECTDATA when the focus is not.
			 * We must set modify_tid to 0 for now and
			 * will fix it when recursion is complete.
			 *
			 * If we are not going to recurse we can do
			 * a normal replacement.
			 *
			 * focus && chain can both be NULL on a match.
			 */
			if (dorecursion) {
				nerror = hammer2_sync_replace(
						thr, cparent, cluster,
						0,
						idx, errors);
			} else if (focus) {
				nerror = hammer2_sync_replace(
						thr, cparent, cluster,
						focus->bref.modify_tid,
						idx, errors);
			} else {
				nerror = 0;
			}
		}
		if (nerror)
			error = nerror;
		/* finished primary synchronization of chains */

skip1:
#if 0
		/*
		 * Operation may have modified cparent, we must replace
		 * iroot->cluster if we are at the top level.
		 */
		if (thr->depth == 0)
			hammer2_inode_repoint_one(pmp->iroot, cparent, idx);
#endif
		KKASSERT(cluster->focus == focus);

		/*
		 * If no work to do this iteration, skip any recursion.
		 */
		if (nowork)
			goto skip2;

		/*
		 * EXECUTE RECURSION (skip if no recursion)
		 *
		 * Indirect blocks are absorbed by the iteration so we only
		 * have to recurse on inodes.
		 *
		 * Do not resolve scluster, it represents the iteration
		 * parent and while it is logically in-sync the physical
		 * elements might not match due to the presence of indirect
		 * blocks and such.
		 */
		if (dorecursion == 0)
			goto skip2;
		if (thr->depth > 20) {
			kprintf("depth limit reached\n");
			nerror = HAMMER2_ERROR_DEPTH;
		} else {
			hammer2_cluster_unlock(cparent);
			scluster = hammer2_cluster_copy(cluster);
			hammer2_cluster_lock(scluster, HAMMER2_RESOLVE_ALWAYS);
			++thr->depth;
			nerror = hammer2_sync_slaves(thr, scluster, errors);
			--thr->depth;
			hammer2_cluster_unlock(scluster);
			hammer2_cluster_drop(scluster);
			/* XXX modify_tid on scluster */
			/* flush needs to not update modify_tid */
			hammer2_cluster_lock(cparent, HAMMER2_RESOLVE_ALWAYS);
		}
		if (nerror)
			goto skip2;

		/*
		 * Fixup parent nodes on the way back up from the recursion
		 * if no error occurred.  The modify_tid for these nodes
		 * would have been set to 0 and must be set to their final
		 * value.
		 */
		chain = cluster->array[idx].chain;
		if (chain == NULL || chain->error)
			goto skip2;
		/*
		 * should not be set but must fixup parents.
		if ((cluster->array[idx].flags & HAMMER2_CITEM_INVALID) == 0)
			goto skip2;
		*/

		/*
		 * At this point we have to have key-matched non-NULL
		 * elements.
		 */
		n = hammer2_chain_cmp(focus, chain);
		if (n != 0) {
			kprintf("hammer2_sync_slaves: illegal "
				"post-recursion state %d\n", n);
			goto skip2;
		}

		/*
		 * Update modify_tid on the way back up.
		 */
		nerror = hammer2_sync_replace(
				thr, cparent, cluster,
				focus->bref.modify_tid,
				idx, errors);
		if (nerror)
			error = nerror;

#if 0
		/*
		 * Operation may modify cparent, must replace
		 * iroot->cluster if we are at the top level.
		 */
		if (thr->depth == 0)
			hammer2_inode_repoint_one(pmp->iroot, cparent, idx);
#endif

skip2:
		/*
		 * Iterate.
		 */
		dumpcluster("adjust", cparent, cluster);
		cluster = hammer2_cluster_next(cparent, cluster,
					       &key_next,
					       HAMMER2_KEY_MIN,
					       HAMMER2_KEY_MAX,
					       HAMMER2_LOOKUP_NODATA |
					       HAMMER2_LOOKUP_NOLOCK |
					       HAMMER2_LOOKUP_NODIRECT |
					       HAMMER2_LOOKUP_ALLNODES);
		dumpcluster("nextcl", cparent, cluster);
	}
	hammer2_cluster_drop(cparent);
	if (cluster)
		hammer2_cluster_drop(cluster);

	return error;
}

/*
 * cparent is locked exclusively, with an extra ref, cluster is not locked.
 */
static
int
hammer2_sync_insert(hammer2_syncthr_t *thr,
		    hammer2_cluster_t *cparent, hammer2_cluster_t *cluster,
		    hammer2_tid_t modify_tid, int i, int *errors)
{
	hammer2_chain_t *focus;
	hammer2_chain_t *chain;
	hammer2_key_t dummy;

	focus = cluster->focus;
#if HAMMER2_SYNCTHR_DEBUG
	if (hammer2_debug & 1)
	kprintf("insert rec par=%p/%d.%016jx slave %d %d.%016jx mod=%016jx\n",
		cparent->array[i].chain, 
		cparent->array[i].chain->bref.type,
		cparent->array[i].chain->bref.key,
		i, focus->bref.type, focus->bref.key, modify_tid);
#endif

	/*
	 * We have to do a lookup to position ourselves at the correct
	 * parent when inserting a record into a new slave because the
	 * cluster iteration for this slave might not be pointing to the
	 * right place.  Our expectation is that the record will not be
	 * found.
	 */
	hammer2_cluster_unlock_except(cparent, i);
	chain = hammer2_chain_lookup(&cparent->array[i].chain, &dummy,
				     focus->bref.key, focus->bref.key,
				     &cparent->array[i].cache_index,
				     HAMMER2_LOOKUP_NODIRECT);
	if (cparent->focus_index == i)
		cparent->focus = cparent->array[i].chain;
	KKASSERT(chain == NULL);

	/*
	 * Create the missing chain.
	 *
	 * Have to be careful to avoid deadlocks.
	 */
	chain = NULL;
	if (cluster->focus_index < i)
		hammer2_chain_lock(focus, HAMMER2_RESOLVE_ALWAYS);
	hammer2_chain_create(&thr->trans, &cparent->array[i].chain,
			     &chain, thr->pmp,
			     focus->bref.key, focus->bref.keybits,
			     focus->bref.type, focus->bytes,
			     0);
	if (cluster->focus_index > i)
		hammer2_chain_lock(focus, HAMMER2_RESOLVE_ALWAYS);
	if (cparent->focus_index == i)
		cparent->focus = cparent->array[i].chain;
	hammer2_chain_modify(&thr->trans, chain, 0);

	/*
	 * Copy focus to new chain
	 */

	/* type already set */
	chain->bref.methods = focus->bref.methods;
	/* keybits already set */
	chain->bref.vradix = focus->bref.vradix;
	/* mirror_tid set by flush */
	chain->bref.modify_tid = modify_tid;
	chain->bref.flags = focus->bref.flags;
	/* key already present */
	/* check code will be recalculated */

	/*
	 * Copy data body.
	 */
	switch(chain->bref.type) {
	case HAMMER2_BREF_TYPE_INODE:
		if ((focus->data->ipdata.meta.op_flags &
		     HAMMER2_OPFLAG_DIRECTDATA) == 0) {
			bcopy(focus->data, chain->data,
			      offsetof(hammer2_inode_data_t, u));
			break;
		}
		/* fall through */
	case HAMMER2_BREF_TYPE_DATA:
		bcopy(focus->data, chain->data, chain->bytes);
		hammer2_chain_setcheck(chain, chain->data);
		break;
	default:
		KKASSERT(0);
		break;
	}

	hammer2_chain_unlock(focus);
	hammer2_chain_unlock(chain);		/* unlock, leave ref */

	/*
	 * Avoid ordering deadlock when relocking cparent.
	 */
	if (i == 0) {
		hammer2_cluster_lock_except(cparent, i, HAMMER2_RESOLVE_ALWAYS);
	} else {
		hammer2_chain_unlock(cparent->array[i].chain);
		hammer2_cluster_lock(cparent, HAMMER2_RESOLVE_ALWAYS);
	}

	/*
	 * Enter item into (unlocked) cluster.
	 *
	 * Must clear invalid for iteration to work properly.
	 */
	if (cluster->array[i].chain)
		hammer2_chain_drop(cluster->array[i].chain);
	cluster->array[i].chain = chain;
	cluster->array[i].flags &= ~HAMMER2_CITEM_INVALID;

	return 0;
}

/*
 * cparent is locked exclusively, with an extra ref, cluster is not locked.
 */
static
int
hammer2_sync_destroy(hammer2_syncthr_t *thr,
		     hammer2_cluster_t *cparent, hammer2_cluster_t *cluster,
		     int i, int *errors)
{
	hammer2_chain_t *chain;

	chain = cluster->array[i].chain;
#if HAMMER2_SYNCTHR_DEBUG
	if (hammer2_debug & 1)
	kprintf("destroy rec %p/%p slave %d %d.%016jx\n",
		cparent, cluster,
		i, chain->bref.type, chain->bref.key);
#endif
	/*
	 * Try to avoid unnecessary I/O.
	 *
	 * XXX accounting not propagated up properly.  We might have to do
	 *     a RESOLVE_MAYBE here and pass 0 for the flags.
	 */
	hammer2_chain_lock(chain, HAMMER2_RESOLVE_NEVER);
	hammer2_chain_delete(&thr->trans, cparent->array[i].chain, chain,
			     HAMMER2_DELETE_NOSTATS |
			     HAMMER2_DELETE_PERMANENT);
	hammer2_chain_unlock(chain);

	/*
	 * The element is not valid in that it doesn't match the other
	 * elements, but we have to mark it valid here to allow the
	 * cluster_next() call to advance this index to the next element.
	 */
	cluster->array[i].flags &= ~HAMMER2_CITEM_INVALID;

	return 0;
}

/*
 * cparent is locked exclusively, with an extra ref, cluster is not locked.
 * Replace element [i] in the cluster.
 */
static
int
hammer2_sync_replace(hammer2_syncthr_t *thr,
		     hammer2_cluster_t *cparent, hammer2_cluster_t *cluster,
		     hammer2_tid_t modify_tid, int i, int *errors)
{
	hammer2_chain_t *focus;
	hammer2_chain_t *chain;
	int nradix;
	uint8_t otype;

	focus = cluster->focus;
	chain = cluster->array[i].chain;
#if HAMMER2_SYNCTHR_DEBUG
	if (hammer2_debug & 1)
	kprintf("replace rec %p/%p slave %d %d.%016jx mod=%016jx\n",
		cparent, cluster,
		i, focus->bref.type, focus->bref.key, modify_tid);
#endif
	if (cluster->focus_index < i)
		hammer2_chain_lock(focus, HAMMER2_RESOLVE_ALWAYS);
	hammer2_chain_lock(chain, HAMMER2_RESOLVE_ALWAYS);
	if (cluster->focus_index >= i)
		hammer2_chain_lock(focus, HAMMER2_RESOLVE_ALWAYS);
	if (chain->bytes != focus->bytes) {
		/* XXX what if compressed? */
		nradix = hammer2_getradix(chain->bytes);
		hammer2_chain_resize(&thr->trans, NULL,
				     cparent->array[i].chain, chain,
				     nradix, 0);
	}
	hammer2_chain_modify(&thr->trans, chain, 0);
	otype = chain->bref.type;
	chain->bref.type = focus->bref.type;
	chain->bref.methods = focus->bref.methods;
	chain->bref.keybits = focus->bref.keybits;
	chain->bref.vradix = focus->bref.vradix;
	/* mirror_tid updated by flush */
	chain->bref.modify_tid = modify_tid;
	chain->bref.flags = focus->bref.flags;
	/* key already present */
	/* check code will be recalculated */
	chain->error = 0;

	/*
	 * Copy data body.
	 */
	switch(chain->bref.type) {
	case HAMMER2_BREF_TYPE_INODE:
		if ((focus->data->ipdata.meta.op_flags &
		     HAMMER2_OPFLAG_DIRECTDATA) == 0) {
			/*
			 * If DIRECTDATA is transitioning to 0 or the old
			 * chain is not an inode we have to initialize
			 * the block table.
			 */
			if (otype != HAMMER2_BREF_TYPE_INODE ||
			    (chain->data->ipdata.meta.op_flags &
			     HAMMER2_OPFLAG_DIRECTDATA)) {
				kprintf("chain inode transiiton away from dd\n");
				bzero(&chain->data->ipdata.u,
				      sizeof(chain->data->ipdata.u));
			}
			bcopy(focus->data, chain->data,
			      offsetof(hammer2_inode_data_t, u));
			/* XXX setcheck on inode should not be needed */
			hammer2_chain_setcheck(chain, chain->data);
			break;
		}
		/* fall through */
	case HAMMER2_BREF_TYPE_DATA:
		bcopy(focus->data, chain->data, chain->bytes);
		hammer2_chain_setcheck(chain, chain->data);
		break;
	default:
		KKASSERT(0);
		break;
	}

	hammer2_chain_unlock(focus);
	hammer2_chain_unlock(chain);

	/*
	 * Must clear invalid for iteration to work properly.
	 */
	cluster->array[i].flags &= ~HAMMER2_CITEM_INVALID;

	return 0;
}
