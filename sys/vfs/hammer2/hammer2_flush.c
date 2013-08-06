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
	struct flush_deferral_list flush_list;
	hammer2_tid_t	sync_tid;	/* flush synchronization point */
	hammer2_tid_t	mirror_tid;	/* collect mirror TID updates */
};

typedef struct hammer2_flush_info hammer2_flush_info_t;

static void hammer2_chain_flush_core(hammer2_flush_info_t *info,
				hammer2_chain_t *chain);
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
 * WARNING! Modifications to the root volume cannot dup the root volume
 *	    header to handle synchronization points, so alloc_tid can
 *	    wind up (harmlessly) more advanced on flush.
 *
 * WARNING! Operations which might call inode_duplicate()/chain_duplicate()
 *	    depend heavily on having a unique sync_tid to avoid duplication
 *	    collisions (which key off of delete_tid).
 */
void
hammer2_trans_init(hammer2_trans_t *trans, hammer2_pfsmount_t *pmp, int flags)
{
	hammer2_cluster_t *cluster;
	hammer2_mount_t *hmp;
	hammer2_trans_t *scan;

	bzero(trans, sizeof(*trans));
	trans->pmp = pmp;
	cluster = pmp->cluster;
	hmp = cluster->hmp;

	hammer2_voldata_lock(hmp);
	trans->sync_tid = hmp->voldata.alloc_tid++;
	trans->flags = flags;
	trans->td = curthread;
	TAILQ_INSERT_TAIL(&hmp->transq, trans, entry);

	if (flags & HAMMER2_TRANS_ISFLUSH) {
		/*
		 * If we are a flush we have to wait for all transactions
		 * prior to our flush synchronization point to complete
		 * before we can start our flush.
		 */
		++hmp->flushcnt;
		if (hmp->curflush == NULL) {
			hmp->curflush = trans;
			hmp->topo_flush_tid = trans->sync_tid;
		}
		while (TAILQ_FIRST(&hmp->transq) != trans) {
			lksleep(&trans->sync_tid, &hmp->voldatalk,
				0, "h2syncw", hz);
		}

		/*
		 * Once we become the running flush we can wakeup anyone
		 * who blocked on us.
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
	} else {
		/*
		 * If we are not a flush but our sync_tid is after a
		 * stalled flush, we have to wait until that flush unstalls
		 * (that is, all transactions prior to that flush complete),
		 * but then we can run concurrently with that flush.
		 *
		 * (flushcnt check only good as pre-condition, otherwise it
		 *  may represent elements queued after us after we block).
		 */
		if (hmp->flushcnt > 1 ||
		    (hmp->curflush &&
		     TAILQ_FIRST(&hmp->transq) != hmp->curflush)) {
			trans->blocked = 1;
			while (trans->blocked) {
				lksleep(&trans->blocked, &hmp->voldatalk,
					0, "h2trans", hz);
			}
		}
	}
	hammer2_voldata_unlock(hmp, 0);
}

void
hammer2_trans_done(hammer2_trans_t *trans)
{
	hammer2_cluster_t *cluster;
	hammer2_mount_t *hmp;
	hammer2_trans_t *scan;

	cluster = trans->pmp->cluster;
	hmp = cluster->hmp;

	hammer2_voldata_lock(hmp);
	TAILQ_REMOVE(&hmp->transq, trans, entry);
	if (trans->flags & HAMMER2_TRANS_ISFLUSH) {
		/*
		 * If we were a flush we have to adjust curflush to the
		 * next flush.
		 *
		 * flush_tid is used to partition copy-on-write operations
		 * (mostly duplicate-on-modify ops), which is what allows
		 * us to execute a flush concurrent with modifying operations
		 * with higher TIDs.
		 */
		--hmp->flushcnt;
		if (hmp->flushcnt) {
			TAILQ_FOREACH(scan, &hmp->transq, entry) {
				if (scan->flags & HAMMER2_TRANS_ISFLUSH)
					break;
			}
			KKASSERT(scan);
			hmp->curflush = scan;
			hmp->topo_flush_tid = scan->sync_tid;
		} else {
			/*
			 * Theoretically we don't have to clear flush_tid
			 * here since the flush will have synchronized
			 * all operations <= flush_tid already.  But for
			 * now zero-it.
			 */
			hmp->curflush = NULL;
			hmp->topo_flush_tid = 0;
		}
	} else {
		/*
		 * If we are not a flush but a flush is now at the head
		 * of the queue and we were previously blocking it,
		 * we can now unblock it.
		 */
		if (hmp->flushcnt &&
		    (scan = TAILQ_FIRST(&hmp->transq)) != NULL &&
		    trans->sync_tid < scan->sync_tid &&
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
hammer2_chain_flush(hammer2_trans_t *trans, hammer2_chain_t *chain)
{
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

	core = chain->core;

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
			hammer2_chain_flush(trans, scan);
			hammer2_chain_unlock(scan);
			hammer2_chain_drop(scan);	/* ref from deferral */
		}

		/*
		 * Flush pass1 on root.
		 */
		info.diddeferral = 0;
		hammer2_chain_flush_core(&info, chain);
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
}

/*
 * This is the core of the chain flushing code.  The chain is locked by the
 * caller and remains locked on return.  This function is keyed off of
 * the SUBMODIFIED bit but must make fine-grained choices based on the
 * synchronization point we are flushing to.
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
hammer2_chain_flush_core(hammer2_flush_info_t *info, hammer2_chain_t *chain)
{
	hammer2_mount_t *hmp;
	hammer2_blockref_t *bref;
	hammer2_off_t pbase;
	hammer2_off_t pmask;
	hammer2_tid_t saved_sync;
	hammer2_trans_t *trans = info->trans;
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

	/*
	 * Deleted chains which have not been destroyed must be retained,
	 * and we probably have to recurse to clean-up any sub-trees.
	 * However, restricted flushes can stop processing here because
	 * the chain cleanup will be handled by a later normal flush.
	 *
	 * The MODIFIED bit can likely be cleared in this situation and we
	 * will do so later on in this procedure.
	 */
	if (chain->delete_tid <= info->sync_tid) {
		if (trans->flags & HAMMER2_TRANS_RESTRICTED)
			return;
	}

	saved_sync = info->sync_tid;
	core = chain->core;

	/*
	 * If SUBMODIFIED is set we recurse the flush and adjust the
	 * blockrefs accordingly.
	 *
	 * NOTE: Looping on SUBMODIFIED can prevent a flush from ever
	 *	 finishing in the face of filesystem activity.
	 */
	if (chain->flags & HAMMER2_CHAIN_SUBMODIFIED) {
		hammer2_chain_t *saved_parent;
		hammer2_tid_t saved_mirror;

		/*
		 * Clear SUBMODIFIED to catch races.  Note that any child
		 * with MODIFIED, DELETED, or MOVED set during Scan2, after
		 * it processes the child, will cause SUBMODIFIED to be
		 * re-set.
		 * child has to be flushed SUBMODIFIED will wind up being
		 * set again (for next time), but this does not stop us from
		 * synchronizing block updates which occurred.
		 *
		 * We don't want to set our chain to MODIFIED gratuitously.
		 *
		 * We need an extra ref on chain because we are going to
		 * release its lock temporarily in our child loop.
		 */
		atomic_clear_int(&chain->flags, HAMMER2_CHAIN_SUBMODIFIED);
		hammer2_chain_ref(chain);

		/*
		 * Run two passes.  The first pass handles MODIFIED and
		 * SUBMODIFIED chains and recurses while the second pass
		 * handles MOVED chains on the way back up.
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
			RB_SCAN(hammer2_chain_tree, &chain->core->rbtree,
				NULL, hammer2_chain_flush_scan1, info);
			spin_unlock(&core->cst.spin);
			diddeferral += info->diddeferral;
		}

		/*
		 * Handle successfully flushed children who are in the MOVED
		 * state on the way back up the recursion.  This can have
		 * the side-effect of clearing MOVED.
		 *
		 * We execute this even if there were deferrals to try to
		 * keep the chain topology cleaner.
		 *
		 * Scan2 is non-recursive.
		 */
		if (diddeferral) {
			atomic_set_int(&chain->flags,
				       HAMMER2_CHAIN_SUBMODIFIED);
		} else {
#if FLUSH_DEBUG
			kprintf("scan2_start parent %p %08x\n",
				chain, chain->flags);
#endif
			spin_lock(&core->cst.spin);
			RB_SCAN(hammer2_chain_tree, &core->rbtree,
				NULL, hammer2_chain_flush_scan2, info);
			spin_unlock(&core->cst.spin);
#if FLUSH_DEBUG
			kprintf("scan2_stop  parent %p %08x\n",
				chain, chain->flags);
#endif
		}
		chain->bref.mirror_tid = info->mirror_tid;
		info->mirror_tid = saved_mirror;
		info->parent = saved_parent;
		hammer2_chain_drop(chain);
	}

	/*
	 * Restore sync_tid in case it was restricted by a delete/duplicate.
	 */
	info->sync_tid = saved_sync;

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
	 * Note that scan2 has already executed above so statistics have
	 * already been rolled up.
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
			hammer2_chain_drop(chain);
		}
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
	if ((chain->flags & HAMMER2_CHAIN_MODIFIED) == 0)
		return;

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
		 */
		if (wasmodified)
			hammer2_chain_drop(chain);
	} else {
		/*
		 * If we were MODIFIED we inherit the ref from clearing
		 * that bit, otherwise we need another ref.
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
		if (hmp->fchain.flags & (HAMMER2_CHAIN_MODIFIED |
					 HAMMER2_CHAIN_SUBMODIFIED)) {
			/* this will modify vchain as a side effect */
			hammer2_chain_flush(info->trans, &hmp->fchain);
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
	 * We should only need to recurse if SUBMODIFIED is set, but as
	 * a safety also recurse if MODIFIED is also set.
	 *
	 * Return early if neither bit is set.  We must re-assert the
	 * SUBMODIFIED flag in the parent if any child covered by the
	 * parent (via delete_tid) is skipped.
	 */
	if ((child->flags & (HAMMER2_CHAIN_MODIFIED |
			     HAMMER2_CHAIN_SUBMODIFIED)) == 0) {
		return (0);
	}
	if (child->modify_tid > trans->sync_tid) {
		if (parent->delete_tid > trans->sync_tid) {
			atomic_set_int(&parent->flags,
				       HAMMER2_CHAIN_SUBMODIFIED);
		}
		return (0);
	}

	hammer2_chain_ref(child);
	spin_unlock(&parent->core->cst.spin);

	/*
	 * The caller has added a ref to the parent so we can temporarily
	 * unlock it in order to lock the child.  Re-check the flags before
	 * continuing.
	 */
	hammer2_chain_unlock(parent);
	hammer2_chain_lock(child, HAMMER2_RESOLVE_MAYBE);

	if ((child->flags & (HAMMER2_CHAIN_MODIFIED |
			     HAMMER2_CHAIN_SUBMODIFIED)) == 0) {
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
		if (parent->delete_tid > trans->sync_tid) {
			atomic_set_int(&parent->flags,
				       HAMMER2_CHAIN_SUBMODIFIED);
		}
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
		atomic_set_int(&child->flags, HAMMER2_CHAIN_DESTROYED |
					      HAMMER2_CHAIN_SUBMODIFIED);
	}

	/*
	 * Recurse and collect deferral data.
	 */
	diddeferral = info->diddeferral;
	++info->depth;
	hammer2_chain_flush_core(info, child);
#if FLUSH_DEBUG
	kprintf("flush_core_done parent=%p flags=%08x child=%p.%d %08x\n",
		parent, parent->flags, child, child->bref.type, child->flags);
#endif
	--info->depth;
	info->diddeferral += diddeferral;

	if (child->flags & HAMMER2_CHAIN_SUBMODIFIED)
		atomic_set_int(&parent->flags, HAMMER2_CHAIN_SUBMODIFIED);

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
 * This function also rolls up storage statistics.
 *
 * NOTE!  We must re-set SUBMODIFIED on the parent(s) as appropriate, and
 *	  due to the above conditions it is possible to do this and still
 *	  have some children flagged MOVED depending on the synchronization.
 *
 * NOTE!  A deletion is a visbility issue, there can still be referenced to
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

	/*
	 * Inodes with stale children that have been converted to DIRECTDATA
	 * mode (file extension or hardlink conversion typically) need to
	 * skipped right now before we start messing with a non-existant
	 * block table.
	 */
#if 0
	if (parent->bref.type == HAMMER2_BREF_TYPE_INODE &&
	    (parent->data->ipdata.op_flags & HAMMER2_OPFLAG_DIRECTDATA)) {
#if FLUSH_DEBUG
		kprintf("B");
#endif
		goto finalize;
	}
#endif

	/*
	 * Ignore children created after our flush point, treating them as
	 * if they did not exist).  These children will not cause the parent
	 * to be updated.
	 *
	 * When we encounter such children and the parent chain has not been
	 * deleted, delete/duplicated, or delete/duplicated-for-move, then
	 * the parent may be used to funnel through several flush points.
	 * We must re-set the SUBMODIFIED flag in the parent to ensure that
	 * those flushes have visbility.  A simple test of delete_tid suffices
	 * to determine if the parent spans beyond our current flush.
	 */
	if (child->modify_tid > trans->sync_tid) {
#if FLUSH_DEBUG
		kprintf("E");
#endif
		goto finalize;
	}

	/*
	 * Ignore children which have not changed.  The parent's block table
	 * is already correct.
	 */
	if ((child->flags & HAMMER2_CHAIN_MOVED) == 0) {
#if FLUSH_DEBUG
		kprintf("D");
#endif
		goto finalize;
	}


	hammer2_chain_ref(child);
	spin_unlock(&above->cst.spin);

	/*
	 * The MOVED bit implies an additional reference which prevents
	 * the child from being destroyed out from under our operation
	 * so we can lock the child safely without worrying about it
	 * getting ripped up (?).
	 *
	 * We can only update parents where child->parent matches.  The
	 * child->parent link will migrate along the chain but the flush
	 * order must be enforced absolutely.  Parent reflushed after the
	 * child has passed them by should skip due to the modify_tid test.
	 */
	hammer2_chain_lock(child, HAMMER2_RESOLVE_NEVER);

	/*
	 * The parent's blockref to the child must be deleted or updated.
	 *
	 * This point is not reached on successful DESTROYED optimizations
	 * but can be reached on recursive deletions and restricted flushes.
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
	hammer2_chain_modify(trans, &parent,
			     HAMMER2_MODIFY_NO_MODIFY_TID |
			     HAMMER2_MODIFY_ASSERTNOCOPY);

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
		if (parent->data->ipdata.op_flags &
		    HAMMER2_OPFLAG_DIRECTDATA) {
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
		panic("hammer2_chain_get: "
		      "unrecognized blockref type: %d",
		      parent->bref.type);
	}

	/*
	 * Update the parent's blockref table and propagate mirror_tid.
	 *
	 * NOTE! Children with modify_tid's beyond our flush point are
	 *	 considered to not exist for the purposes of updating the
	 *	 parent's blockref array.
	 *
	 * NOTE! Updates to a parent's blockref table do not adjust the
	 *	 parent's bref.modify_tid, only its bref.mirror_tid.
	 */
	KKASSERT(child->index >= 0);
	if (child->delete_tid <= trans->sync_tid) {
		if (base) {
			hammer2_rollup_stats(parent, child, -1);
			KKASSERT(child->index < count);
			bzero(&base[child->index], sizeof(child->bref));
		}
		if (info->mirror_tid < child->delete_tid)
			info->mirror_tid = child->delete_tid;
	} else {
		if (base) {
			KKASSERT(child->index < count);
			if (base[child->index].type == 0)
				hammer2_rollup_stats(parent, child, 1);
			else
				hammer2_rollup_stats(parent, child, 0);
			base[child->index] = child->bref;
		}
		if (info->mirror_tid < child->modify_tid)
			info->mirror_tid = child->modify_tid;
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
	 * When can we safely clear the MOVED flag?  Flushes down duplicate
	 * paths can occur out of order, for example if an inode is moved
	 * as part of a hardlink consolidation or if an inode is moved into
	 * an indirect block indexed before the inode.
	 *
	 * Only clear MOVED once all possible parents have been flushed.
	 */
	if (child->flags & HAMMER2_CHAIN_MOVED) {
		hammer2_chain_t *scan;
		int ok = 1;

		spin_lock(&above->cst.spin);
		for (scan = above->first_parent;
		     scan;
		     scan = scan->next_parent) {
			/*
			 * XXX weird code also checked at the top of scan2,
			 *     I would like to fix this by detaching the core
			 *     on initial hardlink consolidation (1->2 nlinks).
			 */
#if 0
			if (scan->bref.type == HAMMER2_BREF_TYPE_INODE &&
			    (scan->data->ipdata.op_flags &
			     HAMMER2_OPFLAG_DIRECTDATA)) {
				continue;
			}
#endif
			if (scan->flags & HAMMER2_CHAIN_SUBMODIFIED) {
				ok = 0;
				break;
			}
		}
		spin_unlock(&above->cst.spin);
		if (ok) {
			atomic_clear_int(&child->flags, HAMMER2_CHAIN_MOVED);
			hammer2_chain_drop(child);	/* flag */
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
#if FLUSH_DEBUG
	kprintf("F");
#endif

	/*
	 * The parent cleared SUBMODIFIED prior to the scan.  If the child
	 * still requires a flush (possibly due to being outside the current
	 * synchronization zone), we must re-set SUBMODIFIED on the way back
	 * up.
	 */
finalize:
#if FLUSH_DEBUG
	kprintf("G child %p 08x\n", child, child->flags);
#endif
	return (0);
}

static
void
hammer2_rollup_stats(hammer2_chain_t *parent, hammer2_chain_t *child, int how)
{
	hammer2_chain_t *grandp;

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
		for (grandp = parent->above->first_parent;
		     grandp;
		     grandp = grandp->next_parent) {
			grandp->data_count += parent->data_count;
			grandp->inode_count += parent->inode_count;
		}
		parent->data_count = 0;
		parent->inode_count = 0;
	}
}
