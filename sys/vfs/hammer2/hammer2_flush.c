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

#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/types.h>
#include <sys/lock.h>
#include <sys/uuid.h>

#include "hammer2.h"

/*
 * Recursively flush the specified chain.  The chain is locked and
 * referenced by the caller and will remain so on return.  The chain
 * will remain referenced throughout but can temporarily lose its
 * lock during the recursion to avoid unnecessarily stalling user
 * processes.
 */
struct hammer2_flush_info {
	hammer2_chain_t *parent;
	hammer2_trans_t	*trans;
	int		depth;
	int		diddeferral;
	int		pass;
	int		cache_index;
	struct h2_flush_deferral_list flush_list;
	hammer2_tid_t	sync_tid;	/* flush synchronization point */
	hammer2_tid_t	mirror_tid;	/* collect mirror TID updates */
};

typedef struct hammer2_flush_info hammer2_flush_info_t;

static void hammer2_chain_flush_core(hammer2_flush_info_t *info,
				hammer2_chain_t **chainp);
static int hammer2_chain_flush_scan1(hammer2_chain_t *child, void *data);
static int hammer2_chain_flush_scan2(hammer2_chain_t *child, void *data);
static void hammer2_rollup_stats(hammer2_chain_t *parent,
				hammer2_chain_t *child, int how);

#if 0
static __inline
void
hammer2_updatestats(hammer2_flush_info_t *info, hammer2_blockref_t *bref,
		    int how)
{
	hammer2_key_t bytes;

	if (bref->type != 0) {
		bytes = 1 << (bref->data_off & HAMMER2_OFF_MASK_RADIX);
		if (bref->type == HAMMER2_BREF_TYPE_INODE)
			info->inode_count += how;
		if (how < 0)
			info->data_count -= bytes;
		else
			info->data_count += bytes;
	}
}
#endif

/*
 * Transaction support functions for writing to the filesystem.
 *
 * Initializing a new transaction allocates a transaction ID.  We
 * don't bother marking the volume header MODIFIED.  Instead, the volume
 * will be synchronized at a later time as part of a larger flush sequence.
 *
 * Non-flush transactions can typically run concurrently.  However if
 * there are non-flush transaction both before AND after a flush trans,
 * the transactions after stall until the ones before finish.
 *
 * Non-flush transactions occuring after a flush pointer can run concurrently
 * with that flush.  They only have to wait for transactions prior to the
 * flush trans to complete before they unstall.
 *
 * WARNING! Transaction ids are only allocated when the transaction becomes
 *	    active, which allows other transactions to insert ahead of us
 *	    if we are forced to block (only bioq transactions do that).
 *
 * WARNING! Modifications to the root volume cannot dup the root volume
 *	    header to handle synchronization points, so alloc_tid can
 *	    wind up (harmlessly) more advanced on flush.
 */
void
hammer2_trans_init(hammer2_trans_t *trans, hammer2_pfsmount_t *pmp, int flags)
{
	hammer2_mount_t *hmp;
	hammer2_trans_t *scan;

	bzero(trans, sizeof(*trans));
	trans->pmp = pmp;
	hmp = pmp->cluster.chains[0]->hmp;	/* XXX */

	hammer2_voldata_lock(hmp);
	trans->flags = flags;
	trans->td = curthread;
	/*trans->delete_gen = 0;*/	/* multiple deletions within trans */

	if (flags & HAMMER2_TRANS_ISFLUSH) {
		/*
		 * If multiple flushes are trying to run we have to
		 * wait until it is our turn, then set curflush to
		 * indicate that a flush is now pending (but not
		 * necessarily active yet).
		 *
		 * NOTE: Do not set trans->blocked here.
		 */
		++hmp->flushcnt;
		while (hmp->curflush != NULL) {
			lksleep(&hmp->curflush, &hmp->voldatalk,
				0, "h2multf", hz);
		}
		hmp->curflush = trans;
		TAILQ_INSERT_TAIL(&hmp->transq, trans, entry);

		/*
		 * If we are a flush we have to wait for all transactions
		 * prior to our flush synchronization point to complete
		 * before we can start our flush.
		 *
		 * Most importantly, this includes bioq flushes.
		 *
		 * NOTE: Do not set trans->blocked here.
		 */
		while (TAILQ_FIRST(&hmp->transq) != trans) {
			lksleep(&trans->sync_tid, &hmp->voldatalk,
				0, "h2syncw", hz);
		}

		/*
		 * don't assign sync_tid until we become the running
		 * flush.  last_flush_tid and topo_flush_tid eare used
		 * to determine when a copy-on-write (aka delete-duplicate)
		 * is required.
		 */
		trans->sync_tid = hmp->voldata.alloc_tid;
		hmp->voldata.alloc_tid += 2;
		hmp->topo_flush_tid = trans->sync_tid;

		/*
		 * Once we become the running flush we can wakeup anyone
		 * who blocked on us, up to the next flush.  That is,
		 * our flush can run concurrent with frontend operations.
		 */
		scan = trans;
		while ((scan = TAILQ_NEXT(scan, entry)) != NULL) {
			if (scan->flags & HAMMER2_TRANS_ISFLUSH)
				break;
			if (scan->blocked == 0)
				break;
			scan->blocked = 0;
			wakeup(&scan->blocked);
		}
	} else if ((flags & HAMMER2_TRANS_BUFCACHE) && hmp->curflush) {
		/*
		 * We cannot block if we are the bioq thread.
		 *
		 * When possible we steal the flush's TID and flush buffers
		 * as part of the larger filesystem flush.  The flush will
		 * interlock against buffer cache transactions when INVFSYNC
		 * is set.
		 *
		 * NOTE: Transactions are not ordered by sync_tid on the
		 *	 transq.  Append to avoid confusion.  Other waiting
		 *       flushes will have not added themselves to transq
		 *	 yet.
		 */
		TAILQ_INSERT_TAIL(&hmp->transq, trans, entry);
		if ((scan = hmp->curflush) != NULL) {
			if (scan->flags & HAMMER2_TRANS_INVFSYNC) {
				trans->sync_tid = scan->sync_tid;
			} else {
				trans->sync_tid = hmp->voldata.alloc_tid++;
			}
		} else {
			trans->sync_tid = hmp->voldata.alloc_tid++;
		}
	} else {
		/*
		 * If this is a normal transaction and not a flush, or
		 * if this is a bioq transaction and no flush is pending,
		 * we can queue normally.
		 *
		 * Normal transactions must block while a pending flush is
		 * waiting for prior transactions to complete.  Once the
		 * pending flush becomes active we can run concurrently
		 * with it.
		 */
		TAILQ_INSERT_TAIL(&hmp->transq, trans, entry);
		scan = TAILQ_FIRST(&hmp->transq);
		if (hmp->curflush && hmp->curflush != scan) {
			trans->blocked = 1;
			while (trans->blocked) {
				lksleep(&trans->blocked, &hmp->voldatalk,
					0, "h2trans", hz);
			}
		}
		trans->sync_tid = hmp->voldata.alloc_tid++;
	}
	hammer2_voldata_unlock(hmp, 0);
}

/*
 * Clear the flag that allowed buffer cache flushes to steal the
 * main flush's transaction id and wait for any in-progress BC flushes
 * to finish.
 */
void
hammer2_trans_clear_invfsync(hammer2_trans_t *trans)
{
	hammer2_mount_t *hmp = trans->pmp->cluster.chains[0]->hmp;

        hammer2_bioq_sync(trans->pmp);
	atomic_clear_int(&trans->flags, HAMMER2_TRANS_INVFSYNC);
	if (TAILQ_FIRST(&hmp->transq) != trans) {
		hammer2_voldata_lock(hmp);
		while (TAILQ_FIRST(&hmp->transq) != trans) {
			tsleep(&trans->sync_tid, 0, "h2flbw", 0);
		}
		hammer2_voldata_unlock(hmp, 0);
	}
	hammer2_bioq_sync(trans->pmp);
	++trans->sync_tid;
	hmp->topo_flush_tid = trans->sync_tid;
}

void
hammer2_trans_done(hammer2_trans_t *trans)
{
	hammer2_mount_t *hmp;
	hammer2_trans_t *scan;
	int wasathead;

	hmp = trans->pmp->cluster.chains[0]->hmp;

	hammer2_voldata_lock(hmp);
	wasathead = (TAILQ_FIRST(&hmp->transq) == trans);
	TAILQ_REMOVE(&hmp->transq, trans, entry);

	if (trans->flags & HAMMER2_TRANS_ISFLUSH) {
		--hmp->flushcnt;
		if (hmp->flushcnt) {
			/*
			 * If we were a flush then wakeup anyone waiting on
			 * curflush (i.e. other flushes that want to run).
			 */
			hmp->curflush = NULL;
			wakeup(&hmp->curflush);
		} else {
			/*
			 * Cycle the flush_tid.
			 */
			hmp->curflush = NULL;
		}
		hmp->last_flush_tid = hmp->topo_flush_tid;
		hmp->topo_flush_tid = HAMMER2_MAX_TID;
	} else {
		/*
		 * If we are not a flush but a flush is now at the head
		 * of the queue and we were previously blocking it,
		 * we can now unblock it.
		 *
		 * Special case where sync_tid == scan->sync_tid occurs
		 * when buffer flush is issued while a normal flush is
		 * running (and in the correct stager), which is typically
		 * semi-synchronous but not always.
		 */
		if (hmp->flushcnt &&
		    (scan = TAILQ_FIRST(&hmp->transq)) != NULL &&
		    wasathead &&
		    (scan->flags & HAMMER2_TRANS_ISFLUSH)) {
			wakeup(&scan->sync_tid);
		}
	}
	hammer2_voldata_unlock(hmp, 0);
}

/*
 * Flush the chain and all modified sub-chains through the specified
 * synchronization point (sync_tid), propagating parent chain modifications
 * and mirror_tid updates back up as needed.  Since we are recursing downward
 * we do not have to deal with the complexities of multi-homed chains (chains
 * with multiple parents).
 *
 * Caller must have interlocked against any non-flush-related modifying
 * operations in progress whos modify_tid values are less than or equal
 * to the passed sync_tid.
 *
 * Caller must have already vetted synchronization points to ensure they
 * are properly flushed.  Only snapshots and cluster flushes can create
 * these sorts of synchronization points.
 *
 * This routine can be called from several places but the most important
 * is from the hammer2_vop_reclaim() function.  We want to try to completely
 * clean out the inode structure to prevent disconnected inodes from
 * building up and blowing out the kmalloc pool.  However, it is not actually
 * necessary to flush reclaimed inodes to maintain HAMMER2's crash recovery
 * capability.
 *
 * chain is locked on call and will remain locked on return.  If a flush
 * occured, the chain's MOVED bit will be set indicating that its parent
 * (which is not part of the flush) should be updated.
 */
void
hammer2_chain_flush(hammer2_trans_t *trans, hammer2_chain_t **chainp)
{
	hammer2_chain_t *chain = *chainp;
	hammer2_chain_t *scan;
	hammer2_chain_core_t *core;
	hammer2_flush_info_t info;

	/*
	 * Execute the recursive flush and handle deferrals.
	 *
	 * Chains can be ridiculously long (thousands deep), so to
	 * avoid blowing out the kernel stack the recursive flush has a
	 * depth limit.  Elements at the limit are placed on a list
	 * for re-execution after the stack has been popped.
	 */
	bzero(&info, sizeof(info));
	TAILQ_INIT(&info.flush_list);
	info.trans = trans;
	info.sync_tid = trans->sync_tid;
	info.mirror_tid = 0;
	info.cache_index = -1;

	core = chain->core;

	/*
	 * Extra ref needed because flush_core expects it when replacing
	 * chain.
	 */
	hammer2_chain_ref(chain);

	for (;;) {
		/*
		 * Unwind deep recursions which had been deferred.  This
		 * can leave MOVED set for these chains, which will be
		 * handled when we [re]flush chain after the unwind.
		 */
		while ((scan = TAILQ_FIRST(&info.flush_list)) != NULL) {
			KKASSERT(scan->flags & HAMMER2_CHAIN_DEFERRED);
			TAILQ_REMOVE(&info.flush_list, scan, flush_node);
			atomic_clear_int(&scan->flags, HAMMER2_CHAIN_DEFERRED);

			/*
			 * Now that we've popped back up we can do a secondary
			 * recursion on the deferred elements.
			 */
			if (hammer2_debug & 0x0040)
				kprintf("defered flush %p\n", scan);
			hammer2_chain_lock(scan, HAMMER2_RESOLVE_MAYBE);
			hammer2_chain_flush(trans, &scan);
			hammer2_chain_unlock(scan);
			hammer2_chain_drop(scan);	/* ref from deferral */
		}

		/*
		 * Flush pass1 on root.
		 */
		info.diddeferral = 0;
		hammer2_chain_flush_core(&info, &chain);
#if FLUSH_DEBUG
		kprintf("flush_core_done parent=<base> chain=%p.%d %08x\n",
			chain, chain->bref.type, chain->flags);
#endif

		/*
		 * Only loop if deep recursions have been deferred.
		 */
		if (TAILQ_EMPTY(&info.flush_list))
			break;
	}
	hammer2_chain_drop(chain);
	*chainp = chain;
}

/*
 * This is the core of the chain flushing code.  The chain is locked by the
 * caller and must also have an extra ref on it by the caller, and remains
 * locked and will have an extra ref on return.
 *
 * This function is keyed off of the update_tid bit but must make
 * fine-grained choices based on the synchronization point we are flushing to.
 *
 * If the flush accomplished any work chain will be flagged MOVED
 * indicating a copy-on-write propagation back up is required.
 * Deep sub-nodes may also have been entered onto the deferral list.
 * MOVED is never set on the volume root.
 *
 * NOTE: modify_tid is different from MODIFIED.  modify_tid is updated
 *	 only when a chain is specifically modified, and not updated
 *	 for copy-on-write propagations.  MODIFIED is set on any modification
 *	 including copy-on-write propagations.
 */
static void
hammer2_chain_flush_core(hammer2_flush_info_t *info, hammer2_chain_t **chainp)
{
	hammer2_chain_t *chain = *chainp;
	hammer2_mount_t *hmp;
	hammer2_blockref_t *bref;
	hammer2_off_t pbase;
	hammer2_off_t pmask;
#if 0
	hammer2_trans_t *trans = info->trans;
#endif
	hammer2_chain_core_t *core;
	size_t psize;
	size_t boff;
	char *bdata;
	struct buf *bp;
	int error;
	int wasmodified;
	int diddeferral = 0;

	hmp = chain->hmp;

#if FLUSH_DEBUG
	if (info->parent)
		kprintf("flush_core %p->%p.%d %08x (%s)\n",
			info->parent, chain, chain->bref.type,
			chain->flags,
			((chain->bref.type == HAMMER2_BREF_TYPE_INODE) ?
				chain->data->ipdata.filename : "?"));
	else
		kprintf("flush_core NULL->%p.%d %08x (%s)\n",
			chain, chain->bref.type,
			chain->flags,
			((chain->bref.type == HAMMER2_BREF_TYPE_INODE) ?
				chain->data->ipdata.filename : "?"));
#endif
	/*
	 * Ignore chains modified beyond the current flush point.  These
	 * will be treated as if they did not exist.
	 */
	if (chain->modify_tid > info->sync_tid)
		return;

	core = chain->core;

#if 0
	kprintf("PUSH   %p.%d %08x mirror=%016jx\n", chain, chain->bref.type, chain->flags, chain->bref.mirror_tid);
#endif

	/*
	 * If update_tid triggers we recurse the flush and adjust the
	 * blockrefs accordingly.
	 *
	 * NOTE: Looping on update_tid can prevent a flush from ever
	 *	 finishing in the face of filesystem activity.
	 *
	 * NOTE: We must recurse whether chain is flagged DELETED or not.
	 *	 However, if it is flagged DELETED we limit sync_tid to
	 *	 delete_tid to ensure that the chain's bref.mirror_tid is
	 *	 not fully updated and causes it to miss the non-DELETED
	 *	 path.
	 */
	if (chain->bref.mirror_tid < core->update_tid) {
		hammer2_chain_t *saved_parent;
		hammer2_tid_t saved_mirror;
		hammer2_chain_layer_t *layer;

		/*
		 * Races will bump update_tid above trans->sync_tid causing
		 * us to catch the issue in a later flush.  We do not update
		 * update_tid if a deferral (or error XXX) occurs.
		 *
		 * We don't want to set our chain to MODIFIED gratuitously.
		 *
		 * We need an extra ref on chain because we are going to
		 * release its lock temporarily in our child loop.
		 */

		/*
		 * Run two passes.  The first pass handles MODIFIED and
		 * update_tid recursions while the second pass handles
		 * MOVED chains on the way back up.
		 *
		 * If the stack gets too deep we defer scan1, but must
		 * be sure to still run scan2 if on the next loop the
		 * deferred chain has been flushed and now needs MOVED
		 * handling on the way back up.
		 *
		 * Scan1 is recursive.
		 *
		 * NOTE: The act of handling a modified/submodified chain can
		 *	 cause the MOVED Flag to be set.  It can also be set
		 *	 via hammer2_chain_delete() and in other situations.
		 *
		 * NOTE: RB_SCAN() must be used instead of RB_FOREACH()
		 *	 because children can be physically removed during
		 *	 the scan.
		 */
		saved_parent = info->parent;
		saved_mirror = info->mirror_tid;
		info->parent = chain;
		info->mirror_tid = chain->bref.mirror_tid;

		if (info->depth == HAMMER2_FLUSH_DEPTH_LIMIT) {
			if ((chain->flags & HAMMER2_CHAIN_DEFERRED) == 0) {
				hammer2_chain_ref(chain);
				TAILQ_INSERT_TAIL(&info->flush_list,
						  chain, flush_node);
				atomic_set_int(&chain->flags,
					       HAMMER2_CHAIN_DEFERRED);
			}
			diddeferral = 1;
		} else {
			info->diddeferral = 0;
			spin_lock(&core->cst.spin);
			KKASSERT(core->good == 0x1234 && core->sharecnt > 0);
			TAILQ_FOREACH_REVERSE(layer, &core->layerq,
					      h2_layer_list, entry) {
				++layer->refs;
				KKASSERT(layer->good == 0xABCD);
				RB_SCAN(hammer2_chain_tree, &layer->rbtree,
					NULL, hammer2_chain_flush_scan1, info);
				--layer->refs;
				diddeferral += info->diddeferral;
			}
			spin_unlock(&core->cst.spin);
		}

		KKASSERT(info->parent == chain);

		/*
		 * Handle successfully flushed children who are in the MOVED
		 * state on the way back up the recursion.  This can have
		 * the side-effect of clearing MOVED.
		 *
		 * Scan2 may replace info->parent.  If it does it will also
		 * replace the extra ref we made.
		 *
		 * Scan2 is non-recursive.
		 */
		if (diddeferral) {
			spin_lock(&core->cst.spin);
		} else {
			spin_lock(&core->cst.spin);
			KKASSERT(core->good == 0x1234 && core->sharecnt > 0);
			TAILQ_FOREACH_REVERSE(layer, &core->layerq,
					      h2_layer_list, entry) {
				info->pass = 1;
				++layer->refs;
				KKASSERT(layer->good == 0xABCD);
				RB_SCAN(hammer2_chain_tree, &layer->rbtree,
					NULL, hammer2_chain_flush_scan2, info);
				info->pass = 2;
				RB_SCAN(hammer2_chain_tree, &layer->rbtree,
					NULL, hammer2_chain_flush_scan2, info);
				--layer->refs;
				KKASSERT(info->parent->core == core);
			}

			/*
			 * Mirror_tid propagates all changes.  It is also used
			 * in scan2 to determine when a chain must be applied
			 * to the related block table.
			 */
#if 0
			kprintf("chainA %p.%d set parent bref mirror_tid %016jx -> %016jx\n",
				info->parent, info->parent->bref.type,
				info->mirror_tid, info->parent->bref.mirror_tid);
#endif
			KKASSERT(info->parent->bref.mirror_tid <=
				 info->mirror_tid);
			info->parent->bref.mirror_tid = info->mirror_tid;
		}

		/*
		 * chain may have been replaced.
		 */
#if 0
		if (info->parent != *chainp)
			kprintf("SWITCH PARENT %p->%p\n",
				*chainp, info->parent);
#endif
		chain = info->parent;
		*chainp = chain;

		hammer2_chain_layer_check_locked(chain->hmp, core);
		spin_unlock(&core->cst.spin);

		info->mirror_tid = saved_mirror;
		info->parent = saved_parent;
		KKASSERT(chain->refs > 1);
	}

#if 0
	kprintf("POP    %p.%d\n", chain, chain->bref.type);
#endif

	/*
	 * Rollup diddeferral for caller.  Note direct assignment, not +=.
	 */
	info->diddeferral = diddeferral;

	/*
	 * Do not flush chain if there were any deferrals.  It will be
	 * retried later after the deferrals are independently handled.
	 */
	if (diddeferral) {
		if (hammer2_debug & 0x0008) {
			kprintf("%*.*s} %p/%d %04x (deferred)",
				info->depth, info->depth, "",
				chain, chain->refs, chain->flags);
		}
		return;
	}

	/*
	 * If we encounter a deleted chain within our flush we can clear
	 * the MODIFIED bit and avoid flushing it whether it has been
	 * destroyed or not.  We must make sure that the chain is flagged
	 * MOVED in this situation so the parent picks up the deletion.
	 *
	 * Since this chain will now never be written to disk we need to
	 * adjust bref.mirror_tid such that it does not prevent sub-chains
	 * from clearing their MOVED bits.
	 *
	 * NOTE:  scan2 has already executed above so statistics have
	 *	  already been rolled up.
	 *
	 * NOTE:  Deletions do not prevent flush recursion as a deleted
	 *	  inode (removed file) which is still open may still require
	 *	  on-media storage to be able to clean related pages out from
	 *	  the system caches.
	 *
	 * NOTE:  Even though this chain will not issue write I/O, we must
	 *	  still update chain->bref.mirror_tid for flush management
	 *	  purposes.
	 */
	if (chain->delete_tid <= info->sync_tid) {
		if (chain->flags & HAMMER2_CHAIN_MODIFIED) {
			if (chain->bp) {
				if (chain->bytes == chain->bp->b_bufsize)
					chain->bp->b_flags |= B_INVAL|B_RELBUF;
			}
			if ((chain->flags & HAMMER2_CHAIN_MOVED) == 0) {
				hammer2_chain_ref(chain);
				atomic_set_int(&chain->flags,
					       HAMMER2_CHAIN_MOVED);
			}
			atomic_clear_int(&chain->flags, HAMMER2_CHAIN_MODIFIED);
			if (chain->bref.mirror_tid < info->sync_tid)
				chain->bref.mirror_tid = info->sync_tid;
			hammer2_chain_drop(chain);
		}
		if (chain->bref.mirror_tid < info->sync_tid)
			chain->bref.mirror_tid = info->sync_tid;
		return;
	}
#if 0
	if ((chain->flags & HAMMER2_CHAIN_DESTROYED) &&
	    (chain->flags & HAMMER2_CHAIN_DELETED) &&
	    (trans->flags & HAMMER2_TRANS_RESTRICTED) == 0) {
		/*
		 * Throw-away the MODIFIED flag
		 */
		if (chain->flags & HAMMER2_CHAIN_MODIFIED) {
			if (chain->bp) {
				if (chain->bytes == chain->bp->b_bufsize)
					chain->bp->b_flags |= B_INVAL|B_RELBUF;
			}
			atomic_clear_int(&chain->flags, HAMMER2_CHAIN_MODIFIED);
			hammer2_chain_drop(chain);
		}
		return;
	}
#endif

	/*
	 * A degenerate flush might not have flushed anything and thus not
	 * processed modified blocks on the way back up.  Detect the case.
	 *
	 * Note that MOVED can be set without MODIFIED being set due to
	 * a deletion, in which case it is handled by Scan2 later on.
	 *
	 * Both bits can be set along with DELETED due to a deletion if
	 * modified data within the synchronization zone and the chain
	 * was then deleted beyond the zone, in which case we still have
	 * to flush for synchronization point consistency.  Otherwise though
	 * DELETED and MODIFIED are treated as separate flags.
	 */
	if ((chain->flags & HAMMER2_CHAIN_MODIFIED) == 0) {
		if (chain->bref.mirror_tid < info->sync_tid)
			chain->bref.mirror_tid = info->sync_tid;
		return;
	}

	/*
	 * Issue flush.
	 *
	 * A DESTROYED node that reaches this point must be flushed for
	 * synchronization point consistency.
	 */

	/*
	 * Update mirror_tid, clear MODIFIED, and set MOVED.
	 *
	 * The caller will update the parent's reference to this chain
	 * by testing MOVED as long as the modification was in-bounds.
	 *
	 * MOVED is never set on the volume root as there is no parent
	 * to adjust.
	 */
	if (hammer2_debug & 0x1000) {
		kprintf("Flush %p.%d %016jx/%d sync_tid %016jx\n",
			chain, chain->bref.type,
			chain->bref.key, chain->bref.keybits,
			info->sync_tid);
	}
	if (hammer2_debug & 0x2000) {
		Debugger("Flush hell");
	}
	if (chain->bref.mirror_tid < info->sync_tid)
		chain->bref.mirror_tid = info->sync_tid;
	wasmodified = (chain->flags & HAMMER2_CHAIN_MODIFIED) != 0;
	atomic_clear_int(&chain->flags, HAMMER2_CHAIN_MODIFIED);
	if (chain == &hmp->vchain)
		kprintf("(FLUSHED VOLUME HEADER)\n");
	if (chain == &hmp->fchain)
		kprintf("(FLUSHED FREEMAP HEADER)\n");

	if ((chain->flags & HAMMER2_CHAIN_MOVED) ||
	    chain == &hmp->vchain ||
	    chain == &hmp->fchain) {
		/*
		 * Drop the ref from the MODIFIED bit we cleared.
		 * Net is -0 or -1 ref depending.
		 */
		if (wasmodified)
			hammer2_chain_drop(chain);
	} else {
		/*
		 * Drop the ref from the MODIFIED bit we cleared and
		 * set a ref for the MOVED bit we are setting.  Net
		 * is +0 or +1 ref depending.
		 */
		if (wasmodified == 0)
			hammer2_chain_ref(chain);
		atomic_set_int(&chain->flags, HAMMER2_CHAIN_MOVED);
	}

	/*
	 * If this is part of a recursive flush we can go ahead and write
	 * out the buffer cache buffer and pass a new bref back up the chain
	 * via the MOVED bit.
	 *
	 * Volume headers are NOT flushed here as they require special
	 * processing.
	 */
	switch(chain->bref.type) {
	case HAMMER2_BREF_TYPE_FREEMAP:
		hammer2_modify_volume(hmp);
		break;
	case HAMMER2_BREF_TYPE_VOLUME:
		/*
		 * We should flush the free block table before we calculate
		 * CRCs and copy voldata -> volsync.
		 *
		 * To prevent SMP races, fchain must remain locked until
		 * voldata is copied to volsync.
		 */
		hammer2_chain_lock(&hmp->fchain, HAMMER2_RESOLVE_ALWAYS);
		if ((hmp->fchain.flags & HAMMER2_CHAIN_MODIFIED) ||
		    hmp->voldata.mirror_tid < hmp->fchain.core->update_tid) {
			/* this will modify vchain as a side effect */
			hammer2_chain_t *tmp = &hmp->fchain;
			hammer2_chain_flush(info->trans, &tmp);
			KKASSERT(tmp == &hmp->fchain);
		}

		/*
		 * The volume header is flushed manually by the syncer, not
		 * here.  All we do is adjust the crc's.
		 */
		KKASSERT(chain->data != NULL);
		KKASSERT(chain->bp == NULL);
		kprintf("volume header mirror_tid %jd\n",
			hmp->voldata.mirror_tid);

		hmp->voldata.icrc_sects[HAMMER2_VOL_ICRC_SECT1]=
			hammer2_icrc32(
				(char *)&hmp->voldata +
				 HAMMER2_VOLUME_ICRC1_OFF,
				HAMMER2_VOLUME_ICRC1_SIZE);
		hmp->voldata.icrc_sects[HAMMER2_VOL_ICRC_SECT0]=
			hammer2_icrc32(
				(char *)&hmp->voldata +
				 HAMMER2_VOLUME_ICRC0_OFF,
				HAMMER2_VOLUME_ICRC0_SIZE);
		hmp->voldata.icrc_volheader =
			hammer2_icrc32(
				(char *)&hmp->voldata +
				 HAMMER2_VOLUME_ICRCVH_OFF,
				HAMMER2_VOLUME_ICRCVH_SIZE);
		hmp->volsync = hmp->voldata;
		atomic_set_int(&chain->flags, HAMMER2_CHAIN_VOLUMESYNC);
		hammer2_chain_unlock(&hmp->fchain);
		break;
	case HAMMER2_BREF_TYPE_DATA:
		/*
		 * Data elements have already been flushed via the logical
		 * file buffer cache.  Their hash was set in the bref by
		 * the vop_write code.
		 *
		 * Make sure any device buffer(s) have been flushed out here.
		 * (there aren't usually any to flush).
		 */
		psize = hammer2_devblksize(chain->bytes);
		pmask = (hammer2_off_t)psize - 1;
		pbase = chain->bref.data_off & ~pmask;
		boff = chain->bref.data_off & (HAMMER2_OFF_MASK & pmask);

		bp = getblk(hmp->devvp, pbase, psize, GETBLK_NOWAIT, 0);
		if (bp) {
			if ((bp->b_flags & (B_CACHE | B_DIRTY)) ==
			    (B_CACHE | B_DIRTY)) {
				cluster_awrite(bp);
			} else {
				bp->b_flags |= B_RELBUF;
				brelse(bp);
			}
		}
		break;
#if 0
	case HAMMER2_BREF_TYPE_INDIRECT:
		/*
		 * Indirect blocks may be in an INITIAL state.  Use the
		 * chain_lock() call to ensure that the buffer has been
		 * instantiated (even though it is already locked the buffer
		 * might not have been instantiated).
		 *
		 * Only write the buffer out if it is dirty, it is possible
		 * the operating system had already written out the buffer.
		 */
		hammer2_chain_lock(chain, HAMMER2_RESOLVE_ALWAYS);
		KKASSERT(chain->bp != NULL);

		bp = chain->bp;
		if ((chain->flags & HAMMER2_CHAIN_DIRTYBP) ||
		    (bp->b_flags & B_DIRTY)) {
			bdwrite(chain->bp);
		} else {
			brelse(chain->bp);
		}
		chain->bp = NULL;
		chain->data = NULL;
		hammer2_chain_unlock(chain);
		break;
#endif
	case HAMMER2_BREF_TYPE_INDIRECT:
	case HAMMER2_BREF_TYPE_FREEMAP_NODE:
		/*
		 * Device-backed.  Buffer will be flushed by the sync
		 * code XXX.
		 */
		KKASSERT((chain->flags & HAMMER2_CHAIN_EMBEDDED) == 0);
		break;
	case HAMMER2_BREF_TYPE_FREEMAP_LEAF:
	default:
		/*
		 * Embedded elements have to be flushed out.
		 * (Basically just BREF_TYPE_INODE).
		 */
		KKASSERT(chain->flags & HAMMER2_CHAIN_EMBEDDED);
		KKASSERT(chain->data != NULL);
		KKASSERT(chain->bp == NULL);
		bref = &chain->bref;

		KKASSERT((bref->data_off & HAMMER2_OFF_MASK) != 0);
		KKASSERT(HAMMER2_DEC_CHECK(chain->bref.methods) ==
			 HAMMER2_CHECK_ISCSI32 ||
			 HAMMER2_DEC_CHECK(chain->bref.methods) ==
			 HAMMER2_CHECK_FREEMAP);

		/*
		 * The data is embedded, we have to acquire the
		 * buffer cache buffer and copy the data into it.
		 */
		psize = hammer2_devblksize(chain->bytes);
		pmask = (hammer2_off_t)psize - 1;
		pbase = bref->data_off & ~pmask;
		boff = bref->data_off & (HAMMER2_OFF_MASK & pmask);

		/*
		 * The getblk() optimization can only be used if the
		 * physical block size matches the request.
		 */
		error = bread(hmp->devvp, pbase, psize, &bp);
		KKASSERT(error == 0);

		bdata = (char *)bp->b_data + boff;

		/*
		 * Copy the data to the buffer, mark the buffer
		 * dirty, and convert the chain to unmodified.
		 */
		bcopy(chain->data, bdata, chain->bytes);
		bp->b_flags |= B_CLUSTEROK;
		bdwrite(bp);
		bp = NULL;

		switch(HAMMER2_DEC_CHECK(chain->bref.methods)) {
		case HAMMER2_CHECK_FREEMAP:
			chain->bref.check.freemap.icrc32 =
				hammer2_icrc32(chain->data, chain->bytes);
			break;
		case HAMMER2_CHECK_ISCSI32:
			chain->bref.check.iscsi32.value =
				hammer2_icrc32(chain->data, chain->bytes);
			break;
		default:
			panic("hammer2_flush_core: bad crc type");
			break; /* NOT REACHED */
		}
		if (chain->bref.type == HAMMER2_BREF_TYPE_INODE)
			++hammer2_iod_meta_write;
		else
			++hammer2_iod_indr_write;
	}
}

/*
 * Flush helper scan1 (recursive)
 *
 * Flushes the children of the caller's chain (parent) and updates
 * the blockref, restricted by sync_tid.
 *
 * Ripouts during the loop should not cause any problems.  Because we are
 * flushing to a synchronization point, modification races will occur after
 * sync_tid and do not have to be flushed anyway.
 *
 * It is also ok if the parent is chain_duplicate()'d while unlocked because
 * the delete/duplication will install a delete_tid that is still larger than
 * our current sync_tid.
 */
static int
hammer2_chain_flush_scan1(hammer2_chain_t *child, void *data)
{
	hammer2_flush_info_t *info = data;
	hammer2_trans_t *trans = info->trans;
	hammer2_chain_t *parent = info->parent;
	int diddeferral;

	/*
	 * We only need to recurse if MODIFIED is set or
	 * child->bref.mirror_tid has not caught up to update_tid.
	 */
	if ((child->flags & HAMMER2_CHAIN_MODIFIED) == 0 &&
	    child->bref.mirror_tid >= child->core->update_tid) {
		return (0);
	}
	if (child->modify_tid > trans->sync_tid)
		return (0);

	hammer2_chain_ref(child);
	spin_unlock(&parent->core->cst.spin);

	/*
	 * The caller has added a ref to the parent so we can temporarily
	 * unlock it in order to lock the child.  Re-check the flags before
	 * continuing.
	 */
	hammer2_chain_unlock(parent);
	hammer2_chain_lock(child, HAMMER2_RESOLVE_MAYBE);

	if ((child->flags & HAMMER2_CHAIN_MODIFIED) == 0 &&
	    child->bref.mirror_tid >= child->core->update_tid) {
		hammer2_chain_unlock(child);
		hammer2_chain_drop(child);
		hammer2_chain_lock(parent, HAMMER2_RESOLVE_MAYBE);
		spin_lock(&parent->core->cst.spin);
		return (0);
	}
	if (child->modify_tid > trans->sync_tid) {
		hammer2_chain_unlock(child);
		hammer2_chain_drop(child);
		hammer2_chain_lock(parent, HAMMER2_RESOLVE_MAYBE);
		spin_lock(&parent->core->cst.spin);
		return (0);
	}

	/*
	 * The DESTROYED flag can only be initially set on an unreferenced
	 * deleted inode and will propagate downward via the mechanic below.
	 * Such inode chains have been deleted for good and should no longer
	 * be subject to delete/duplication.
	 *
	 * This optimization allows the inode reclaim (destroy unlinked file
	 * on vnode reclamation after last close) to be flagged by just
	 * setting HAMMER2_CHAIN_DESTROYED at the top level and then will
	 * cause the chains to be terminated and related buffers to be
	 * invalidated and not flushed out.
	 *
	 * We have to be careful not to propagate the DESTROYED flag if
	 * the destruction occurred after our flush sync_tid.
	 */
	if ((parent->flags & HAMMER2_CHAIN_DESTROYED) &&
	    (child->flags & HAMMER2_CHAIN_DELETED) &&
	    (child->flags & HAMMER2_CHAIN_DESTROYED) == 0) {
		atomic_set_int(&child->flags, HAMMER2_CHAIN_DESTROYED);
		/*
		 * Force downward recursion by bringing update_tid up to
		 * at least sync_tid.  Parent's mirror_tid has not yet
		 * been updated.
		 *
		 * Vnode reclamation may have forced update_tid to MAX_TID.
		 * In this situation bring it down to something reasonable
		 * so the elements being destroyed can be retired.
		 */
		spin_lock(&child->core->cst.spin);
		if (child->core->update_tid < trans->sync_tid ||
		    child->core->update_tid == HAMMER2_MAX_TID) {
			child->core->update_tid = trans->sync_tid;
		}
		spin_unlock(&child->core->cst.spin);
	}

	/*
	 * Recurse and collect deferral data.
	 */
	diddeferral = info->diddeferral;
	++info->depth;
	hammer2_chain_flush_core(info, &child);
#if FLUSH_DEBUG
	kprintf("flush_core_done parent=%p flags=%08x child=%p.%d %08x\n",
		parent, parent->flags, child, child->bref.type, child->flags);
#endif
	/*
	 * NOTE: If child failed to fully synchronize, child's bref.mirror_tid
	 *	 will not have been updated.  Bumping diddeferral prevents
	 *	 the parent chain from updating bref.mirror_tid on the way
	 *	 back up in order to force a retry later.
	 */
	if (child->bref.mirror_tid < child->core->update_tid)
		++diddeferral;

	--info->depth;
	info->diddeferral += diddeferral;
	hammer2_chain_unlock(child);
	hammer2_chain_drop(child);

	hammer2_chain_lock(parent, HAMMER2_RESOLVE_MAYBE);

	spin_lock(&parent->core->cst.spin);

	return (0);
}

/*
 * Flush helper scan2 (non-recursive)
 *
 * This pass on a chain's children propagates any MOVED or DELETED
 * elements back up the chain towards the root after those elements have
 * been fully flushed.  Unlike scan1, this function is NOT recursive and
 * the parent remains locked across the entire scan.
 *
 * SCAN2 is called twice, once with pass set to 1 and once with it set to 2.
 * We have to do this so base[] elements can be deleted in pass 1 to make
 * room for adding new elements in pass 2.
 *
 * This function also rolls up storage statistics.
 *
 * NOTE!  A deletion is a visbility issue, there can still be references to
 *	  deleted elements (for example, to an unlinked file which is still
 *	  open), and there can also be multiple chains pointing to the same
 *	  bref where some are deleted and some are not (for example due to
 *	  a rename).   So a chain marked for deletion is basically considered
 *	  to be live until it is explicitly destroyed or until its ref-count
 *	  reaches zero (also implying that MOVED and MODIFIED are clear).
 */
static int
hammer2_chain_flush_scan2(hammer2_chain_t *child, void *data)
{
	hammer2_flush_info_t *info = data;
	hammer2_chain_t *parent = info->parent;
	hammer2_chain_core_t *above = child->above;
	hammer2_mount_t *hmp = child->hmp;
	hammer2_trans_t *trans = info->trans;
	hammer2_blockref_t *base;
	int count;
	int ok;

	/*
	 * Inodes with stale children that have been converted to DIRECTDATA
	 * mode (file extension or hardlink conversion typically) need to
	 * skipped right now before we start messing with a non-existant
	 * block table.
	 */
#if 0
	if (parent->bref.type == HAMMER2_BREF_TYPE_INODE &&
	    (parent->data->ipdata.op_flags & HAMMER2_OPFLAG_DIRECTDATA)) {
		goto finalize;
	}
#endif

	/*
	 * Ignore children created after our flush point, treating them as
	 * if they did not exist).  These children will not cause the parent
	 * to be updated.
	 *
	 * Children deleted after our flush point are treated as having been
	 * created for the purposes of the flush.  The parent's update_tid
	 * will already be higher than our trans->sync_tid so the flush path
	 * is left intact.
	 *
	 * When we encounter such children and the parent chain has not been
	 * deleted, delete/duplicated, or delete/duplicated-for-move, then
	 * the parent may be used to funnel through several flush points.
	 * These chains will still be visible to later flushes due to having
	 * a higher update_tid than we can set in the current flush.
	 */
	if (child->modify_tid > trans->sync_tid) {
		goto finalize;
	}

	/*
	 * Ignore children which have not changed.  The parent's block table
	 * is already correct.
	 *
	 * XXX The MOVED bit is only cleared when all multi-homed parents
	 *     have flushed, creating a situation where a re-flush can occur
	 *     via a parent which has already flushed.  The hammer2_base_*()
	 *     functions currently have a hack to deal with this case but
	 *     we need something better.
	 */
	if ((child->flags & HAMMER2_CHAIN_MOVED) == 0) {
		goto finalize;
	}

	/*
	 * Make sure child is referenced before we unlock.
	 */
	hammer2_chain_ref(child);
	spin_unlock(&above->cst.spin);

	/*
	 * Parent reflushed after the child has passed them by should skip
	 * due to the modify_tid test. XXX
	 */
	hammer2_chain_lock(child, HAMMER2_RESOLVE_NEVER);
	KKASSERT(child->above == above);
	KKASSERT(parent->core == above);

	/*
	 * The parent's blockref to the child must be deleted or updated.
	 *
	 * This point is not reached on successful DESTROYED optimizations
	 * but can be reached on recursive deletions and restricted flushes.
	 *
	 * The chain_modify here may delete-duplicate the block.  This can
	 * cause a multitude of issues if the block was already modified
	 * by a later (post-flush) transaction.  Primarily blockrefs in
	 * the later block can be out-of-date, so if the situation occurs
	 * we can't throw away the MOVED bit on the current blocks until
	 * the later blocks are flushed (so as to be able to regenerate all
	 * the changes that were made).
	 *
	 * Because flushes are ordered we do not have to make a
	 * modify/duplicate of indirect blocks.  That is, the flush
	 * code does not have to kmalloc or duplicate anything.  We
	 * can adjust the indirect block table in-place and reuse the
	 * chain.  It IS possible that the chain has already been duplicated
	 * or may wind up being duplicated on-the-fly by modifying code
	 * on the frontend.  We simply use the original and ignore such
	 * chains.  However, it does mean we can't clear the MOVED bit.
	 *
	 * XXX recursive deletions not optimized.
	 */
	hammer2_chain_modify(trans, &parent, HAMMER2_MODIFY_NO_MODIFY_TID);
	if (info->parent != parent) {
		/* extra ref from flush_core */
		hammer2_chain_drop(info->parent);
		info->parent = parent;
		hammer2_chain_ref(info->parent);
	}

	switch(parent->bref.type) {
	case HAMMER2_BREF_TYPE_INODE:
		/*
		 * XXX Should assert that OPFLAG_DIRECTDATA is 0 once we
		 * properly duplicate the inode headers and do proper flush
		 * range checks (all the children should be beyond the flush
		 * point).  For now just don't sync the non-applicable
		 * children.
		 *
		 * XXX Can also occur due to hardlink consolidation.  We
		 * set OPFLAG_DIRECTDATA to prevent the indirect and data
		 * blocks from syncing ot the hardlink pointer.
		 */
#if 0
		KKASSERT((parent->data->ipdata.op_flags &
			  HAMMER2_OPFLAG_DIRECTDATA) == 0);
#endif
#if 0
		if (parent->data->ipdata.op_flags & HAMMER2_OPFLAG_DIRECTDATA) {
			base = NULL;
		} else
#endif
		{
			base = &parent->data->ipdata.u.blockset.blockref[0];
			count = HAMMER2_SET_COUNT;
		}
		break;
	case HAMMER2_BREF_TYPE_INDIRECT:
	case HAMMER2_BREF_TYPE_FREEMAP_NODE:
		if (parent->data) {
			base = &parent->data->npdata[0];
		} else {
			base = NULL;
			KKASSERT(child->flags & HAMMER2_CHAIN_DELETED);
		}
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
		panic("hammer2_chain_flush_scan2: "
		      "unrecognized blockref type: %d",
		      parent->bref.type);
	}

	/*
	 * Don't bother updating a deleted parent's blockrefs (caller will
	 * optimize-out the disk write).  Note that this is not optional,
	 * a deleted parent's blockref array might not be synchronized at
	 * all so calling hammer2_base*() functions could result in a panic.
	 *
	 * Otherwise, we need to be COUNTEDBREFS synchronized for the
	 * hammer2_base_*() functions.
	 */
	if (parent->delete_tid <= trans->sync_tid)
		base = NULL;
	else if ((parent->core->flags & HAMMER2_CORE_COUNTEDBREFS) == 0)
		hammer2_chain_countbrefs(parent, base, count);

	/*
	 * Update the parent's blockref table and propagate mirror_tid.
	 *
	 * NOTE! Children with modify_tid's beyond our flush point are
	 *	 considered to not exist for the purposes of updating the
	 *	 parent's blockref array.
	 *
	 * NOTE! Updates to a parent's blockref table do not adjust the
	 *	 parent's bref.modify_tid, only its bref.mirror_tid.
	 *
	 * NOTE! chain->modify_tid vs chain->bref.modify_tid.  The chain's
	 *	 internal modify_tid is always updated based on creation
	 *	 or delete-duplicate.  However, the bref.modify_tid is NOT
	 *	 updated due to simple blockref updates.
	 */
#if 0
	kprintf("chain %p->%p pass %d trans %016jx sync %p.%d %016jx/%d C=%016jx D=%016jx PMIRROR %016jx\n",
		parent, child,
		info->pass, trans->sync_tid,
		child, child->bref.type,
		child->bref.key, child->bref.keybits,
		child->modify_tid, child->delete_tid, parent->bref.mirror_tid);
#endif

	if (info->pass == 1 && child->delete_tid <= trans->sync_tid) {
		/*
		 * Deleting.  The block array is expected to contain the
		 * child's entry if:
		 *
		 * (1) The deletion occurred after the parent's block table
		 *     was last synchronized (delete_tid), and
		 *
		 * (2) The creation occurred before or during the parent's
		 *     last block table synchronization.
		 */
		ok = 1;
		if (base &&
		    child->delete_tid > parent->bref.mirror_tid &&
		    child->modify_tid <= parent->bref.mirror_tid) {
			hammer2_rollup_stats(parent, child, -1);
			spin_lock(&above->cst.spin);
#if 0
			kprintf("trans %jx parent %p.%d child %p.%d m/d %016jx/%016jx "
				"flg=%08x %016jx/%d delete\n",
				trans->sync_tid,
				parent, parent->bref.type,
				child, child->bref.type,
				child->modify_tid, child->delete_tid,
				child->flags,
				child->bref.key, child->bref.keybits);
#endif
			hammer2_base_delete(parent, base, count,
					    &info->cache_index, child);
			spin_unlock(&above->cst.spin);
		}
		if (info->mirror_tid < child->delete_tid)
			info->mirror_tid = child->delete_tid;
	} else if (info->pass == 2 && child->delete_tid > trans->sync_tid) {
		/*
		 * Inserting.  The block array is expected to NOT contain
		 * the child's entry if:
		 *
		 * (1) The creation occurred after the parent's block table
		 *     was last synchronized (modify_tid), and
		 *
		 * (2) The child is not being deleted in the same
		 *     transaction.
		 */
		ok = 1;
		if (base &&
		    child->modify_tid > parent->bref.mirror_tid &&
		    child->delete_tid > trans->sync_tid) {
			hammer2_rollup_stats(parent, child, 1);
			spin_lock(&above->cst.spin);
#if 0
			kprintf("trans %jx parent %p.%d child %p.%d m/d %016jx/%016jx "
				"flg=%08x %016jx/%d insert\n",
				trans->sync_tid,
				parent, parent->bref.type,
				child, child->bref.type,
				child->modify_tid, child->delete_tid,
				child->flags,
				child->bref.key, child->bref.keybits);
#endif
			hammer2_base_insert(parent, base, count,
					    &info->cache_index, child);
			spin_unlock(&above->cst.spin);
		}
		if (info->mirror_tid < child->modify_tid)
			info->mirror_tid = child->modify_tid;
	} else {
		ok = 0;
	}

	if (info->mirror_tid < child->bref.mirror_tid) {
		info->mirror_tid = child->bref.mirror_tid;
	}
	if ((parent->bref.type == HAMMER2_BREF_TYPE_VOLUME ||
	     parent->bref.type == HAMMER2_BREF_TYPE_FREEMAP) &&
	    hmp->voldata.mirror_tid < child->bref.mirror_tid) {
		hmp->voldata.mirror_tid = child->bref.mirror_tid;
	}

	/*
	 * Only clear MOVED once all possible parents have been flushed.
	 *
	 * When can we safely clear the MOVED flag?  Flushes down duplicate
	 * paths can occur out of order, for example if an inode is moved
	 * as part of a hardlink consolidation or if an inode is moved into
	 * an indirect block indexed before the inode.
	 */
	if (ok && (child->flags & HAMMER2_CHAIN_MOVED)) {
		hammer2_chain_t *scan;

		if (hammer2_debug & 0x4000)
			kprintf("CHECKMOVED %p (parent=%p)", child, parent);

		spin_lock(&above->cst.spin);
		TAILQ_FOREACH(scan, &above->ownerq, core_entry) {
			/*
			 * Can't destroy the child until all parent's have
			 * synchronized with its move.
			 *
			 * NOTE: A deleted parent will synchronize with a
			 *	 child's move without bothering to update
			 *	 its brefs.
			 */
			if (scan == parent ||
			    scan->delete_tid <= trans->sync_tid)
				continue;
			if (scan->bref.mirror_tid < child->modify_tid) {
				if (hammer2_debug & 0x4000)
					kprintf("(fail scan %p %016jx/%016jx)",
						scan, scan->bref.mirror_tid,
						child->modify_tid);
				ok = 0;
			}
		}
		if (hammer2_debug & 0x4000)
			kprintf("\n");
		spin_unlock(&above->cst.spin);
		if (ok) {
			if (hammer2_debug & 0x4000)
				kprintf("clear moved %p.%d %016jx/%d\n",
					child, child->bref.type,
					child->bref.key, child->bref.keybits);
			atomic_clear_int(&child->flags, HAMMER2_CHAIN_MOVED);
			hammer2_chain_drop(child);	/* flag */
		} else {
			if (hammer2_debug & 0x4000)
				kprintf("keep  moved %p.%d %016jx/%d\n",
					child, child->bref.type,
					child->bref.key, child->bref.keybits);
		}
	}

	/*
	 * Unlock the child.  This can wind up dropping the child's
	 * last ref, removing it from the parent's RB tree, and deallocating
	 * the structure.  The RB_SCAN() our caller is doing handles the
	 * situation.
	 */
	hammer2_chain_unlock(child);
	hammer2_chain_drop(child);
	spin_lock(&above->cst.spin);

	/*
	 * The parent may have been delete-duplicated.
	 */
	info->parent = parent;
finalize:
	return (0);
}

static
void
hammer2_rollup_stats(hammer2_chain_t *parent, hammer2_chain_t *child, int how)
{
#if 0
	hammer2_chain_t *grandp;
#endif

	parent->data_count += child->data_count;
	parent->inode_count += child->inode_count;
	child->data_count = 0;
	child->inode_count = 0;
	if (how < 0) {
		parent->data_count -= child->bytes;
		if (child->bref.type == HAMMER2_BREF_TYPE_INODE) {
			parent->inode_count -= 1;
#if 0
			/* XXX child->data may be NULL atm */
			parent->data_count -= child->data->ipdata.data_count;
			parent->inode_count -= child->data->ipdata.inode_count;
#endif
		}
	} else if (how > 0) {
		parent->data_count += child->bytes;
		if (child->bref.type == HAMMER2_BREF_TYPE_INODE) {
			parent->inode_count += 1;
#if 0
			/* XXX child->data may be NULL atm */
			parent->data_count += child->data->ipdata.data_count;
			parent->inode_count += child->data->ipdata.inode_count;
#endif
		}
	}
	if (parent->bref.type == HAMMER2_BREF_TYPE_INODE) {
		parent->data->ipdata.data_count += parent->data_count;
		parent->data->ipdata.inode_count += parent->inode_count;
#if 0
		for (grandp = parent->above->first_parent;
		     grandp;
		     grandp = grandp->next_parent) {
			grandp->data_count += parent->data_count;
			grandp->inode_count += parent->inode_count;
		}
#endif
		parent->data_count = 0;
		parent->inode_count = 0;
	}
}
