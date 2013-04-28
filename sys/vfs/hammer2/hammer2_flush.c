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
	hammer2_mount_t	*hmp;
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

/*
 * Transaction support functions for writing to the filesystem.
 *
 * Initializing a new transaction allocates a transaction ID.  We
 * don't bother marking the volume header MODIFIED.  Instead, the volume
 * header will be updated only if the operation actually makes modifications
 * (when then propagate to the root).
 *
 * WARNING! Modifications to the root volume cannot dup the root volume
 *	    header to handle synchronization points, so alloc_tid can
 *	    wind up (harmlessly) more advanced on flush.
 */
void
hammer2_trans_init(hammer2_trans_t *trans, hammer2_mount_t *hmp)
{
	bzero(trans, sizeof(*trans));
	trans->hmp = hmp;
	hammer2_voldata_lock(hmp);
	trans->sync_tid = hmp->voldata.alloc_tid++;
	hammer2_voldata_unlock(hmp, 0);	/* don't immediately mark modified */
}

void
hammer2_trans_done(hammer2_trans_t *trans)
{
	trans->hmp = NULL;
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
 * SUBMODIFIED is not cleared if modified elements with higher modify_tid
 * values (thus not flushed) are still present after the flush.
 *
 * If a chain is unable to completely flush we have to be sure that
 * SUBMODIFIED remains set up the parent chain, and that MOVED is not
 * cleared or our desynchronized bref will not properly update in the
 * parent.  The parent's indirect block is copied-on-write and adjusted
 * as needed so it no longer needs to be placemarked by the subchains,
 * allowing the sub-chains to be cleaned out.
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
	info.hmp = trans->hmp;
	info.trans = trans;
	info.sync_tid = trans->sync_tid;
	info.mirror_tid = 0;

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
		 * Flush pass1 on root.  SUBMODIFIED can remain set after
		 * this call for numerous reasons, including write failures,
		 * but most likely due to only a partial flush being
		 * requested.
		 */
		info.diddeferral = 0;
		hammer2_chain_flush_core(&info, chain);

		/*
		 * Only loop if deep recursions have been deferred.
		 */
		if (TAILQ_EMPTY(&info.flush_list))
			break;
	}

	/*
	 * SUBMODIFIED can be temporarily cleared and then re-set, which
	 * can prevent concurrent setsubmods from reaching all the way to
	 * the root.  If after the flush we find the node is still in need
	 * of flushing (though possibly due to modifications made outside
	 * the requested synchronization zone), we must call setsubmod again
	 * to cover the race.
	 */
	if (chain->flags & (HAMMER2_CHAIN_MOVED |
			    HAMMER2_CHAIN_DELETED |
			    HAMMER2_CHAIN_MODIFIED |
			    HAMMER2_CHAIN_SUBMODIFIED)) {
		hammer2_chain_parent_setsubmod(chain);
	}
}

/*
 * (chain) is locked by the caller and remains locked on return.
 * This function is keyed off of SUBMODIFIED but must make fine-grained
 * choices based on the synchronization point we are flushing to.
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
	size_t bbytes;
	size_t boff;
	char *bdata;
	struct buf *bp;
	int error;
	int wasmodified;
	int diddeferral = 0;

	hmp = info->hmp;

	/*
	 * If SUBMODIFIED is set we recurse the flush and adjust the
	 * blockrefs accordingly.
	 *
	 * NOTE: Looping on SUBMODIFIED can prevent a flush from ever
	 *	 finishing in the face of filesystem activity.
	 */
	if (chain->flags & HAMMER2_CHAIN_SUBMODIFIED) {
		hammer2_chain_t *saved_parent;

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
		info->parent = chain;

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
			spin_lock(&chain->core->cst.spin);
			RB_SCAN(hammer2_chain_tree, &chain->core->rbtree,
				NULL, hammer2_chain_flush_scan1, info);
			spin_unlock(&chain->core->cst.spin);
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
		spin_lock(&chain->core->cst.spin);
		RB_SCAN(hammer2_chain_tree, &chain->core->rbtree,
			NULL, hammer2_chain_flush_scan2, info);
		spin_unlock(&chain->core->cst.spin);
		info->parent = saved_parent;
		hammer2_chain_drop(chain);
	}

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
	 * Chain objects flagged for complete destruction recurse down from
	 * their inode.  The inode will have already been removed from
	 * its parent.  We have no need to disconnect the children from
	 * their parents or the inode in this situation (it would just
	 * waste time and storage with copy-on-write operations), so
	 * we can clear both the MODIFIED bit and the MOVED bit.
	 *
	 * However, delete_tid must be within the synchronization zone
	 * for us to act on this bit.  Open-but-deleted files have to
	 * be managed by the cluster such that they are not subjected to
	 * reclamation.
	 *
	 * DESTROYED chains stop processing here.
	 */
	if ((chain->flags & HAMMER2_CHAIN_DESTROYED) &&
	    (chain->delete_tid <= info->sync_tid)) {
		if (chain->flags & HAMMER2_CHAIN_MODIFIED) {
			if (chain->bp)
				chain->bp->b_flags |= B_INVAL|B_RELBUF;
			atomic_clear_int(&chain->flags, HAMMER2_CHAIN_MODIFIED);
			hammer2_chain_drop(chain);
		}
		if (chain->flags & HAMMER2_CHAIN_MOVED) {
			atomic_clear_int(&chain->flags, HAMMER2_CHAIN_MOVED);
			hammer2_chain_drop(chain);
		}
		if (hammer2_debug & 0x0008) {
			kprintf("%*.*s} %p/%d %04x (destroyed)",
				info->depth, info->depth, "",
				chain, chain->refs, chain->flags);
		}
		return;
	}

	/*
	 * If MODIFIED is not set or modify_tid is > sync_tid we have
	 * nothing to do.
	 *
	 * Note that MOVED can be set without MODIFIED being set due to
	 * a deletion, in which case it is handled by Scan2 later on.
	 *
	 * Both bits can be set along with DELETED due to a deletion if
	 * modified data within the synchronization zone and the chain
	 * was then deleted beyond the zone, in which case we still have
	 * to flush for synchronization point consistency.
	 */
	if ((chain->flags & HAMMER2_CHAIN_MODIFIED) == 0)
		return;
	if (chain->bref.modify_tid > info->sync_tid) {
		if (hammer2_debug & 0x0008) {
			kprintf("%*.*s} %p/%d %04x (skip - beyond sync_tid)",
				info->depth, info->depth, "",
				chain, chain->refs, chain->flags);
		}
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
	if (chain->bref.mirror_tid < info->sync_tid)
		chain->bref.mirror_tid = info->sync_tid;
	wasmodified = (chain->flags & HAMMER2_CHAIN_MODIFIED) != 0;
	atomic_clear_int(&chain->flags, HAMMER2_CHAIN_MODIFIED);
	if (chain == &hmp->vchain)
		kprintf("(FLUSHED VOLUME HEADER)\n");

	if ((chain->flags & HAMMER2_CHAIN_MOVED) ||
	    chain == &hmp->vchain) {
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
	 * out the buffer cache buffer and pass a new bref back up the chain.
	 *
	 * This will never be a volume header.
	 */
	switch(chain->bref.type) {
	case HAMMER2_BREF_TYPE_VOLUME:
		/*
		 * The volume header is flushed manually by the syncer, not
		 * here.
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
		break;
	case HAMMER2_BREF_TYPE_DATA:
		/*
		 * Data elements have already been flushed via the logical
		 * file buffer cache.  Their hash was set in the bref by
		 * the vop_write code.
		 *
		 * Make sure the buffer(s) have been flushed out here.
		 */
		bbytes = chain->bytes;
		pbase = chain->bref.data_off & ~(hammer2_off_t)(bbytes - 1);
		boff = chain->bref.data_off & HAMMER2_OFF_MASK & (bbytes - 1);

		bp = getblk(hmp->devvp, pbase, bbytes, GETBLK_NOWAIT, 0);
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
	default:
		/*
		 * Embedded elements have to be flushed out.
		 */
		KKASSERT(chain->data != NULL);
		KKASSERT(chain->bp == NULL);
		bref = &chain->bref;

		KKASSERT((bref->data_off & HAMMER2_OFF_MASK) != 0);
		KKASSERT(HAMMER2_DEC_CHECK(chain->bref.methods) ==
			 HAMMER2_CHECK_ISCSI32);

		if (chain->bp == NULL) {
			/*
			 * The data is embedded, we have to acquire the
			 * buffer cache buffer and copy the data into it.
			 */
			if ((bbytes = chain->bytes) < HAMMER2_MINIOSIZE)
				bbytes = HAMMER2_MINIOSIZE;
			pbase = bref->data_off & ~(hammer2_off_t)(bbytes - 1);
			boff = bref->data_off & HAMMER2_OFF_MASK & (bbytes - 1);

			/*
			 * The getblk() optimization can only be used if the
			 * physical block size matches the request.
			 */
			if (chain->bytes == bbytes) {
				bp = getblk(hmp->devvp, pbase, bbytes, 0, 0);
				error = 0;
			} else {
				error = bread(hmp->devvp, pbase, bbytes, &bp);
				KKASSERT(error == 0);
			}
			bdata = (char *)bp->b_data + boff;

			/*
			 * Copy the data to the buffer, mark the buffer
			 * dirty, and convert the chain to unmodified.
			 */
			bcopy(chain->data, bdata, chain->bytes);
			bp->b_flags |= B_CLUSTEROK;
			bdwrite(bp);
			bp = NULL;
			chain->bref.check.iscsi32.value =
				hammer2_icrc32(chain->data, chain->bytes);
			if (chain->bref.type == HAMMER2_BREF_TYPE_INODE)
				++hammer2_iod_meta_write;
			else
				++hammer2_iod_indr_write;
		} else {
			chain->bref.check.iscsi32.value =
				hammer2_icrc32(chain->data, chain->bytes);
		}
	}
	if (hammer2_debug & 0x0008) {
		kprintf("%*.*s} %p/%d %04x (flushed)",
			info->depth, info->depth, "",
			chain, chain->refs, chain->flags);
	}
}

/*
 * Flush helper scan1 (recursive)
 *
 * Flushes the children of the caller's chain (parent) and updates
 * the blockref.
 *
 * Ripouts during the loop should not cause any problems.  Because we are
 * flushing to a synchronization point, modification races will occur after
 * sync_tid and do not have to be flushed anyway.
 */
static int
hammer2_chain_flush_scan1(hammer2_chain_t *child, void *data)
{
	hammer2_flush_info_t *info = data;
	hammer2_chain_t *parent = info->parent;
	/*hammer2_mount_t *hmp = info->hmp;*/
	int diddeferral;

	/*
	 * We should only need to recurse if SUBMODIFIED is set, but as
	 * a safety also recursive if MODIFIED is also set.  Return early
	 * if neither bit is set.
	 */
	if ((child->flags & (HAMMER2_CHAIN_SUBMODIFIED |
			     HAMMER2_CHAIN_MODIFIED)) == 0) {
		return (0);
	}
	spin_unlock(&parent->core->cst.spin);

	/*
	 * The caller has added a ref to the parent so we can temporarily
	 * unlock it in order to lock the child.  Re-check the flags before
	 * continuing.
	 */
	hammer2_chain_unlock(parent);
	hammer2_chain_lock(child, HAMMER2_RESOLVE_MAYBE);

	if ((child->flags & (HAMMER2_CHAIN_SUBMODIFIED |
			     HAMMER2_CHAIN_MODIFIED)) == 0) {
		hammer2_chain_unlock(child);
		hammer2_chain_lock(parent, HAMMER2_RESOLVE_ALWAYS);
		spin_lock(&parent->core->cst.spin);
		return (0);
	}

	/*
	 * Propagate the DESTROYED flag if found set as well as deal with
	 * delete_tid.  This also causes SUBMODIFIED to be propagated
	 * downward to keep the recursion going.
	 *
	 * In the case of delete_tid, nothing need be done.  Destruction
	 * occurs after any deletions and destruction of internal chains
	 * where delete_tid may be 0 (since we don't bother to copy-on-write
	 * the propagation of a deletion) will pass the conditional just
	 * fine.
	 *
	 * This optimization allows the inode reclaim (destroy unlinked file
	 * on vnode reclamation after last close) to be flagged by just
	 * setting HAMMER2_CHAIN_DESTROYED at the top level.
	 */
	if ((parent->flags & HAMMER2_CHAIN_DESTROYED) &&
	    (child->flags & HAMMER2_CHAIN_DESTROYED) == 0) {
		atomic_set_int(&child->flags,
			       HAMMER2_CHAIN_DESTROYED |
			       HAMMER2_CHAIN_SUBMODIFIED);
	}

	/*
	 * Recurse and collect deferral data.
	 */
	diddeferral = info->diddeferral;
	++info->depth;
	hammer2_chain_flush_core(info, child);
	--info->depth;
	info->diddeferral += diddeferral;

	hammer2_chain_unlock(child);

	/*
	 * Always resolve when relocking the parent meta-data so Scan2
	 * has the indirect block data in-hand to handle the MOVED bit.
	 */
	hammer2_chain_lock(parent, HAMMER2_RESOLVE_ALWAYS);

	spin_lock(&parent->core->cst.spin);
	return (0);
}

/*
 * Flush helper scan2 (non-recursive)
 *
 * This pass on a chain's children propagates any MOVED or DELETED
 * elements back up the chain towards the root.  The bref's modify_tid
 * must be within the synchronization zone for MOVED to be recognized
 * and delete_tid must be within the synchronization zone for DELETED
 * to be recognized.
 *
 * We must re-set SUBMODIFIED if appropriate.
 */
static int
hammer2_chain_flush_scan2(hammer2_chain_t *child, void *data)
{
	enum { HC_NONE, HC_DELETE, HC_UPDATE } action = HC_NONE;
	hammer2_flush_info_t *info = data;
	hammer2_chain_t *parent = info->parent;
	hammer2_mount_t *hmp = info->hmp;
	hammer2_blockref_t *base;
	int count;

	/*
	 * Check update conditions prior to locking child.
	 * We may not be able to safely test the 64-bit TIDs
	 * but we can certainly test the flags.
	 */
	if ((child->flags & (HAMMER2_CHAIN_DELETED |
			     HAMMER2_CHAIN_MOVED)) == 0) {
		goto finalize;
	}
	spin_unlock(&parent->core->cst.spin);

	/*
	 * The MOVED bit implies an additional reference which prevents
	 * the child from being destroyed out from under our operation
	 * so we can lock the child safely without worrying about it
	 * getting ripped up (?).
	 */
	hammer2_chain_lock(child, HAMMER2_RESOLVE_NEVER);

	/*
	 * Full condition check.  We can only update and clear MOVED
	 * if the child is deleted or updated within our synchronization
	 * zone.
	 */
	if ((child->flags & HAMMER2_CHAIN_DELETED) &&
	    child->delete_tid <= info->sync_tid) {
		action = HC_DELETE;
	} else if ((child->flags & HAMMER2_CHAIN_MOVED) &&
		   child->bref.modify_tid <= info->sync_tid) {
		action = HC_UPDATE;
	} else {
		hammer2_chain_unlock(child);
		spin_lock(&parent->core->cst.spin);
		goto finalize;
	}

	/*
	 * If the parent is to be deleted then we can clear MOVED
	 * in the child without updating the parent.  That is, it
	 * doesn't matter that the parent->child blockref is left intact
	 * because the parent is going to be deleted too.  This little
	 * bit of code will result in major optimizations of recursive
	 * file tree deletions and truncations.
	 */
	if ((parent->flags & HAMMER2_CHAIN_DELETED) &&
	    parent->delete_tid <= info->sync_tid) {
		goto cleanup;
	}

	/*
	 * The parent's blockref to the child must be deleted or updated.
	 *
	 * This point is not reached on successful DESTROYED optimizations
	 * but can be reached on recursive deletions.  We can optimize
	 */
	hammer2_chain_modify(info->trans, parent, HAMMER2_MODIFY_NO_MODIFY_TID);

	switch(parent->bref.type) {
	case HAMMER2_BREF_TYPE_INODE:
		KKASSERT((parent->data->ipdata.op_flags &
			  HAMMER2_OPFLAG_DIRECTDATA) == 0);
		base = &parent->data->ipdata.u.blockset.blockref[0];
		count = HAMMER2_SET_COUNT;
		break;
	case HAMMER2_BREF_TYPE_INDIRECT:
		if (parent->data) {
			base = &parent->data->npdata.blockref[0];
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
	default:
		base = NULL;
		count = 0;
		panic("hammer2_chain_get: "
		      "unrecognized blockref type: %d",
		      parent->bref.type);
	}

	/*
	 * Update the parent's blockref table and propagate mirror_tid.
	 * blockref updates do not touch modify_tid.  Instead, mirroring
	 * operations always reconcile the entire array during their
	 * mirror_tid based recursion.
	 */
	if (action == HC_DELETE) {
		if (base) {
			KKASSERT(child->index < count);
			bzero(&base[child->index], sizeof(child->bref));
		}
	} else {
		if (base) {
			KKASSERT(child->index < count);
			base[child->index] = child->bref;
		}
	}
	KKASSERT(child->index >= 0);

	if (parent->bref.mirror_tid < child->bref.mirror_tid) {
		parent->bref.mirror_tid = child->bref.mirror_tid;
	}
	if (parent->bref.type == HAMMER2_BREF_TYPE_VOLUME &&
	    hmp->voldata.mirror_tid < child->bref.mirror_tid) {
		hmp->voldata.mirror_tid = child->bref.mirror_tid;
	}

cleanup:
	/*
	 * Deletions should also zero-out the child's bref for safety.
	 */
	if (action == HC_DELETE)
		bzero(&child->bref, sizeof(child->bref));

	/*
	 * Cleanup the child's MOVED flag and unlock the child.
	 */
	if (child->flags & HAMMER2_CHAIN_MOVED) {
		atomic_clear_int(&child->flags, HAMMER2_CHAIN_MOVED);
		hammer2_chain_drop(child);	/* flag */
	}

	/*
	 * Unlock the child.  This can wind up dropping the child's
	 * last ref, removing it from the parent's RB tree, and deallocating
	 * the structure.  The RB_SCAN() our caller is doing handles the
	 * situation.
	 */
	hammer2_chain_unlock(child);
	spin_lock(&parent->core->cst.spin);

	/*
	 * The parent cleared SUBMODIFIED prior to the scan.  If the child
	 * still requires a flush (possibly due to being outside the current
	 * synchronization zone), we must re-set SUBMODIFIED on the way back
	 * up.
	 */
finalize:
	if (child->flags & (HAMMER2_CHAIN_MOVED |
			    HAMMER2_CHAIN_DELETED |
			    HAMMER2_CHAIN_MODIFIED |
			    HAMMER2_CHAIN_SUBMODIFIED)) {
		atomic_set_int(&parent->flags, HAMMER2_CHAIN_SUBMODIFIED);
	}

	return (0);
}
