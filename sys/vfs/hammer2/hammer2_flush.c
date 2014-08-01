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
 *			TRANSACTION AND FLUSH HANDLING
 *
 * Deceptively simple but actually fairly difficult to implement properly is
 * how I would describe it.
 *
 * The biggest issue is that each PFS may belong to a cluster so its media
 * modify_tid and mirror_tid fields are in a completely different domain
 * than the topology related to the super-root.
 *
 * Flushing generally occurs bottom-up but requires a top-down scan to
 * locate chains with MODIFIED and/or UPDATE bits set.  The ONFLUSH flag
 * tells how to recurse downward to find these chains.
 */

#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/types.h>
#include <sys/lock.h>
#include <sys/uuid.h>

#include "hammer2.h"

#define FLUSH_DEBUG 0

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
	int		cache_index;
	struct h2_flush_list flushq;
	hammer2_xid_t	sync_xid;	/* memory synchronization point */
	hammer2_chain_t	*debug;
};

typedef struct hammer2_flush_info hammer2_flush_info_t;

static void hammer2_flush_core(hammer2_flush_info_t *info,
				hammer2_chain_t *chain, int deleting);
static int hammer2_flush_recurse(hammer2_chain_t *child, void *data);
#if 0
static void hammer2_rollup_stats(hammer2_chain_t *parent,
				hammer2_chain_t *child, int how);
#endif


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
 * For now use a global transaction manager.  What we ultimately want to do
 * is give each non-overlapping hmp/pmp group its own transaction manager.
 *
 * Transactions govern XID tracking on the physical media (the hmp), but they
 * also govern TID tracking which is per-PFS and thus might cross multiple
 * hmp's.  So we can't just stuff tmanage into hammer2_mount or
 * hammer2_pfsmount.
 */
static hammer2_trans_manage_t	tmanage;

void
hammer2_trans_manage_init(void)
{
	lockinit(&tmanage.translk, "h2trans", 0, 0);
	TAILQ_INIT(&tmanage.transq);
	tmanage.flush_xid = 1;
	tmanage.alloc_xid = tmanage.flush_xid + 1;
}

hammer2_xid_t
hammer2_trans_newxid(hammer2_pfsmount_t *pmp __unused)
{
	hammer2_xid_t xid;

	for (;;) {
		xid = atomic_fetchadd_int(&tmanage.alloc_xid, 1);
		if (xid)
			break;
	}
	return xid;
}

/*
 * Transaction support functions for writing to the filesystem.
 *
 * Initializing a new transaction allocates a transaction ID.  Typically
 * passed a pmp (hmp passed as NULL), indicating a cluster transaction.  Can
 * be passed a NULL pmp and non-NULL hmp to indicate a transaction on a single
 * media target.  The latter mode is used by the recovery code.
 *
 * TWO TRANSACTION IDs can run concurrently, where one is a flush and the
 * other is a set of any number of concurrent filesystem operations.  We
 * can either have <running_fs_ops> + <waiting_flush> + <blocked_fs_ops>
 * or we can have <running_flush> + <concurrent_fs_ops>.
 *
 * During a flush, new fs_ops are only blocked until the fs_ops prior to
 * the flush complete.  The new fs_ops can then run concurrent with the flush.
 *
 * Buffer-cache transactions operate as fs_ops but never block.  A
 * buffer-cache flush will run either before or after the current pending
 * flush depending on its state.
 */
void
hammer2_trans_init(hammer2_trans_t *trans, hammer2_pfsmount_t *pmp, int flags)
{
	hammer2_trans_manage_t *tman;
	hammer2_trans_t *head;

	tman = &tmanage;

	bzero(trans, sizeof(*trans));
	trans->pmp = pmp;
	trans->flags = flags;
	trans->td = curthread;

	lockmgr(&tman->translk, LK_EXCLUSIVE);

	if (flags & HAMMER2_TRANS_ISFLUSH) {
		/*
		 * If multiple flushes are trying to run we have to
		 * wait until it is our turn.  All flushes are serialized.
		 *
		 * We queue ourselves and then wait to become the head
		 * of the queue, allowing all prior flushes to complete.
		 *
		 * Multiple normal transactions can share the current
		 * transaction id but a flush transaction needs its own
		 * unique TID for proper block table update accounting.
		 */
		++tman->flushcnt;
		++pmp->alloc_tid;
		pmp->flush_tid = pmp->alloc_tid;
		tman->flush_xid = hammer2_trans_newxid(pmp);
		trans->sync_xid = tman->flush_xid;
		++pmp->alloc_tid;
		TAILQ_INSERT_TAIL(&tman->transq, trans, entry);
		if (TAILQ_FIRST(&tman->transq) != trans) {
			trans->blocked = 1;
			while (trans->blocked) {
				lksleep(&trans->sync_xid, &tman->translk,
					0, "h2multf", hz);
			}
		}
	} else if (tman->flushcnt == 0) {
		/*
		 * No flushes are pending, we can go.  Use prior flush_xid + 1.
		 *
		 * WARNING!  Also see hammer2_chain_setflush()
		 */
		TAILQ_INSERT_TAIL(&tman->transq, trans, entry);
		trans->sync_xid = tman->flush_xid + 1;

		/* XXX improve/optimize inode allocation */
	} else if (trans->flags & HAMMER2_TRANS_BUFCACHE) {
		/*
		 * A buffer cache transaction is requested while a flush
		 * is in progress.  The flush's PREFLUSH flag must be set
		 * in this situation.
		 *
		 * The buffer cache flush takes on the main flush's
		 * transaction id.
		 */
		TAILQ_FOREACH(head, &tman->transq, entry) {
			if (head->flags & HAMMER2_TRANS_ISFLUSH)
				break;
		}
		KKASSERT(head);
		KKASSERT(head->flags & HAMMER2_TRANS_PREFLUSH);
		trans->flags |= HAMMER2_TRANS_PREFLUSH;
		TAILQ_INSERT_AFTER(&tman->transq, head, trans, entry);
		trans->sync_xid = head->sync_xid;
		trans->flags |= HAMMER2_TRANS_CONCURRENT;
		/* not allowed to block */
	} else {
		/*
		 * A normal transaction is requested while a flush is in
		 * progress.  We insert after the current flush and may
		 * block.
		 *
		 * WARNING!  Also see hammer2_chain_setflush()
		 */
		TAILQ_FOREACH(head, &tman->transq, entry) {
			if (head->flags & HAMMER2_TRANS_ISFLUSH)
				break;
		}
		KKASSERT(head);
		TAILQ_INSERT_AFTER(&tman->transq, head, trans, entry);
		trans->sync_xid = head->sync_xid + 1;
		trans->flags |= HAMMER2_TRANS_CONCURRENT;

		/*
		 * XXX for now we must block new transactions, synchronous
		 * flush mode is on by default.
		 *
		 * If synchronous flush mode is enabled concurrent
		 * frontend transactions during the flush are not
		 * allowed (except we don't have a choice for buffer
		 * cache ops).
		 */
		if (hammer2_synchronous_flush > 0 ||
		    TAILQ_FIRST(&tman->transq) != head) {
			trans->blocked = 1;
			while (trans->blocked) {
				lksleep(&trans->sync_xid,
					&tman->translk, 0,
					"h2multf", hz);
			}
		}
	}
	if (flags & HAMMER2_TRANS_NEWINODE) {
		if (pmp->spmp_hmp) {
			/*
			 * Super-root transaction, all new inodes have an
			 * inode number of 1.  Normal pfs inode cache
			 * semantics are not used.
			 */
			trans->inode_tid = 1;
		} else {
			/*
			 * Normal transaction
			 */
			if (pmp->inode_tid < HAMMER2_INODE_START)
				pmp->inode_tid = HAMMER2_INODE_START;
			trans->inode_tid = pmp->inode_tid++;
		}
	}

	lockmgr(&tman->translk, LK_RELEASE);
}

/*
 * This may only be called while in a flush transaction.  It's a bit of a
 * hack but after flushing a PFS we need to flush each volume root as part
 * of the same transaction.
 */
void
hammer2_trans_spmp(hammer2_trans_t *trans, hammer2_pfsmount_t *spmp)
{
	++spmp->alloc_tid;
	spmp->flush_tid = spmp->alloc_tid;
	++spmp->alloc_tid;
	trans->pmp = spmp;
}


void
hammer2_trans_done(hammer2_trans_t *trans)
{
	hammer2_trans_manage_t *tman;
	hammer2_trans_t *head;
	hammer2_trans_t *scan;

	tman = &tmanage;

	/*
	 * Remove.
	 */
	lockmgr(&tman->translk, LK_EXCLUSIVE);
	TAILQ_REMOVE(&tman->transq, trans, entry);
	head = TAILQ_FIRST(&tman->transq);

	/*
	 * Adjust flushcnt if this was a flush, clear TRANS_CONCURRENT
	 * up through the next flush.  (If the head is a flush then we
	 * stop there, unlike the unblock code following this section).
	 */
	if (trans->flags & HAMMER2_TRANS_ISFLUSH) {
		--tman->flushcnt;
		scan = head;
		while (scan && (scan->flags & HAMMER2_TRANS_ISFLUSH) == 0) {
			atomic_clear_int(&scan->flags,
					 HAMMER2_TRANS_CONCURRENT);
			scan = TAILQ_NEXT(scan, entry);
		}
	}

	/*
	 * Unblock the head of the queue and any additional transactions
	 * up to the next flush.  The head can be a flush and it will be
	 * unblocked along with the non-flush transactions following it
	 * (which are allowed to run concurrently with it).
	 *
	 * In synchronous flush mode we stop if the head transaction is
	 * a flush.
	 */
	if (head && head->blocked) {
		head->blocked = 0;
		wakeup(&head->sync_xid);

		if (hammer2_synchronous_flush > 0)
			scan = head;
		else
			scan = TAILQ_NEXT(head, entry);
		while (scan && (scan->flags & HAMMER2_TRANS_ISFLUSH) == 0) {
			if (scan->blocked) {
				scan->blocked = 0;
				wakeup(&scan->sync_xid);
			}
			scan = TAILQ_NEXT(scan, entry);
		}
	}
	lockmgr(&tman->translk, LK_RELEASE);
}

/*
 * Flush the chain and all modified sub-chains through the specified
 * synchronization point, propagating parent chain modifications and
 * mirror_tid updates back up as needed.
 *
 * Caller must have interlocked against any non-flush-related modifying
 * operations in progress whos XXX values are less than or equal
 * to the passed sync_xid.
 *
 * Caller must have already vetted synchronization points to ensure they
 * are properly flushed.  Only snapshots and cluster flushes can create
 * these sorts of synchronization points.
 *
 * This routine can be called from several places but the most important
 * is from VFS_SYNC.
 *
 * chain is locked on call and will remain locked on return.  The chain's
 * UPDATE flag indicates that its parent's block table (which is not yet
 * part of the flush) should be updated.  The chain may be replaced by
 * the call if it was modified.
 */
void
hammer2_flush(hammer2_trans_t *trans, hammer2_chain_t *chain)
{
	hammer2_chain_t *scan;
	hammer2_flush_info_t info;
	int loops;

	/*
	 * Execute the recursive flush and handle deferrals.
	 *
	 * Chains can be ridiculously long (thousands deep), so to
	 * avoid blowing out the kernel stack the recursive flush has a
	 * depth limit.  Elements at the limit are placed on a list
	 * for re-execution after the stack has been popped.
	 */
	bzero(&info, sizeof(info));
	TAILQ_INIT(&info.flushq);
	info.trans = trans;
	info.sync_xid = trans->sync_xid;
	info.cache_index = -1;

	/*
	 * Calculate parent (can be NULL), if not NULL the flush core
	 * expects the parent to be referenced so it can easily lock/unlock
	 * it without it getting ripped up.
	 */
	if ((info.parent = chain->parent) != NULL)
		hammer2_chain_ref(info.parent);

	/*
	 * Extra ref needed because flush_core expects it when replacing
	 * chain.
	 */
	hammer2_chain_ref(chain);
	loops = 0;

	for (;;) {
		/*
		 * Unwind deep recursions which had been deferred.  This
		 * can leave the FLUSH_* bits set for these chains, which
		 * will be handled when we [re]flush chain after the unwind.
		 */
		while ((scan = TAILQ_FIRST(&info.flushq)) != NULL) {
			KKASSERT(scan->flags & HAMMER2_CHAIN_DEFERRED);
			TAILQ_REMOVE(&info.flushq, scan, flush_node);
			atomic_clear_int(&scan->flags, HAMMER2_CHAIN_DEFERRED);

			/*
			 * Now that we've popped back up we can do a secondary
			 * recursion on the deferred elements.
			 *
			 * NOTE: hammer2_flush() may replace scan.
			 */
			if (hammer2_debug & 0x0040)
				kprintf("deferred flush %p\n", scan);
			hammer2_chain_lock(scan, HAMMER2_RESOLVE_MAYBE);
			hammer2_chain_drop(scan);	/* ref from deferral */
			hammer2_flush(trans, scan);
			hammer2_chain_unlock(scan);
		}

		/*
		 * [re]flush chain.
		 */
		info.diddeferral = 0;
		hammer2_flush_core(&info, chain, 0);

		/*
		 * Only loop if deep recursions have been deferred.
		 */
		if (TAILQ_EMPTY(&info.flushq))
			break;

		if (++loops % 1000 == 0) {
			kprintf("hammer2_flush: excessive loops on %p\n",
				chain);
			if (hammer2_debug & 0x100000)
				Debugger("hell4");
		}
	}
	hammer2_chain_drop(chain);
	if (info.parent)
		hammer2_chain_drop(info.parent);
}

/*
 * This is the core of the chain flushing code.  The chain is locked by the
 * caller and must also have an extra ref on it by the caller, and remains
 * locked and will have an extra ref on return.  Upon return, the caller can
 * test the UPDATE bit on the child to determine if the parent needs updating.
 *
 * (1) Determine if this node is a candidate for the flush, return if it is
 *     not.  fchain and vchain are always candidates for the flush.
 *
 * (2) If we recurse too deep the chain is entered onto the deferral list and
 *     the current flush stack is aborted until after the deferral list is
 *     run.
 *
 * (3) Recursively flush live children (rbtree).  This can create deferrals.
 *     A successful flush clears the MODIFIED and UPDATE bits on the children
 *     and typically causes the parent to be marked MODIFIED as the children
 *     update the parent's block table.  A parent might already be marked
 *     MODIFIED due to a deletion (whos blocktable update in the parent is
 *     handled by the frontend), or if the parent itself is modified by the
 *     frontend for other reasons.
 *
 * (4) Permanently disconnected sub-trees are cleaned up by the front-end.
 *     Deleted-but-open inodes can still be individually flushed via the
 *     filesystem syncer.
 *
 * (5) Note that an unmodified child may still need the block table in its
 *     parent updated (e.g. rename/move).  The child will have UPDATE set
 *     in this case.
 *
 *			WARNING ON BREF MODIFY_TID/MIRROR_TID
 *
 * blockref.modify_tid and blockref.mirror_tid are consistent only within a
 * PFS.  This is why we cannot cache sync_tid in the transaction structure.
 * Instead we access it from the pmp.
 */
static void
hammer2_flush_core(hammer2_flush_info_t *info, hammer2_chain_t *chain,
		   int deleting)
{
	hammer2_chain_t *parent;
	hammer2_mount_t *hmp;
	hammer2_pfsmount_t *pmp;
	int diddeferral;

	/*
	 * (1) Optimize downward recursion to locate nodes needing action.
	 *     Nothing to do if none of these flags are set.
	 */
	if ((chain->flags & HAMMER2_CHAIN_FLUSH_MASK) == 0) {
		if (hammer2_debug & 0x200) {
			if (info->debug == NULL)
				info->debug = chain;
		} else {
			return;
		}
	}

	hmp = chain->hmp;
	pmp = chain->pmp;
	diddeferral = info->diddeferral;
	parent = info->parent;		/* can be NULL */

	/*
	 * mirror_tid should not be forward-indexed
	 */
	KKASSERT(chain->bref.mirror_tid <= pmp->flush_tid);

	/*
	 * Downward search recursion
	 */
	if (chain->flags & HAMMER2_CHAIN_DEFERRED) {
		/*
		 * Already deferred.
		 */
		++info->diddeferral;
	} else if (info->depth == HAMMER2_FLUSH_DEPTH_LIMIT) {
		/*
		 * Recursion depth reached.
		 */
		hammer2_chain_ref(chain);
		TAILQ_INSERT_TAIL(&info->flushq, chain, flush_node);
		atomic_set_int(&chain->flags, HAMMER2_CHAIN_DEFERRED);
		++info->diddeferral;
	} else if (chain->flags & HAMMER2_CHAIN_ONFLUSH) {
		/*
		 * Downward recursion search (actual flush occurs bottom-up).
		 * pre-clear ONFLUSH.  It can get set again due to races,
		 * which we want so the scan finds us again in the next flush.
		 */
		atomic_clear_int(&chain->flags, HAMMER2_CHAIN_ONFLUSH);
		info->parent = chain;
		spin_lock(&chain->core.cst.spin);
		RB_SCAN(hammer2_chain_tree, &chain->core.rbtree,
			NULL, hammer2_flush_recurse, info);
		spin_unlock(&chain->core.cst.spin);
		info->parent = parent;
		if (info->diddeferral)
			hammer2_chain_setflush(info->trans, chain);
	}

	/*
	 * Now we are in the bottom-up part of the recursion.
	 *
	 * Do not update chain if lower layers were deferred.
	 */
	if (info->diddeferral)
		goto done;

	/*
	 * Propagate the DESTROY flag downwards.  This dummies up the flush
	 * code and tries to invalidate related buffer cache buffers to
	 * avoid the disk write.
	 */
	if (parent && (parent->flags & HAMMER2_CHAIN_DESTROY))
		atomic_set_int(&chain->flags, HAMMER2_CHAIN_DESTROY);

	/*
	 * Chain was already modified or has become modified, flush it out.
	 */
again:
	if ((hammer2_debug & 0x200) &&
	    info->debug &&
	    (chain->flags & (HAMMER2_CHAIN_MODIFIED | HAMMER2_CHAIN_UPDATE))) {
		hammer2_chain_t *scan = chain;

		kprintf("DISCONNECTED FLUSH %p->%p\n", info->debug, chain);
		while (scan) {
			kprintf("    chain %p [%08x] bref=%016jx:%02x\n",
				scan, scan->flags,
				scan->bref.key, scan->bref.type);
			if (scan == info->debug)
				break;
			scan = scan->parent;
		}
	}

	if (chain->flags & HAMMER2_CHAIN_MODIFIED) {
		/*
		 * Dispose of the modified bit.  UPDATE should already be
		 * set.
		 */
		KKASSERT((chain->flags & HAMMER2_CHAIN_UPDATE) ||
			 chain == &hmp->vchain);
		atomic_clear_int(&chain->flags, HAMMER2_CHAIN_MODIFIED);
		hammer2_pfs_memory_wakeup(pmp);
		chain->bref.mirror_tid = pmp->flush_tid;

		if ((chain->flags & HAMMER2_CHAIN_UPDATE) ||
		    chain == &hmp->vchain ||
		    chain == &hmp->fchain) {
			/*
			 * Drop the ref from the MODIFIED bit we cleared,
			 * net -1 ref.
			 */
			hammer2_chain_drop(chain);
		} else {
			/*
			 * Drop the ref from the MODIFIED bit we cleared and
			 * set a ref for the UPDATE bit we are setting.  Net
			 * 0 refs.
			 */
			atomic_set_int(&chain->flags, HAMMER2_CHAIN_UPDATE);
		}

		/*
		 * Issue flush.
		 *
		 * A DELETED node that reaches this point must be flushed for
		 * synchronization point consistency.
		 *
		 * Update bref.mirror_tid, clear MODIFIED, and set UPDATE.
		 */
		if (hammer2_debug & 0x1000) {
			kprintf("Flush %p.%d %016jx/%d sync_xid=%08x "
				"data=%016jx\n",
				chain, chain->bref.type,
				chain->bref.key, chain->bref.keybits,
				info->sync_xid,
				chain->bref.data_off);
		}
		if (hammer2_debug & 0x2000) {
			Debugger("Flush hell");
		}

		/*
		 * Update chain CRCs for flush.
		 *
		 * NOTE: Volume headers are NOT flushed here as they require
		 *	 special processing.
		 */
		switch(chain->bref.type) {
		case HAMMER2_BREF_TYPE_FREEMAP:
			KKASSERT(hmp->vchain.flags & HAMMER2_CHAIN_MODIFIED);
			hmp->voldata.freemap_tid = hmp->fchain.bref.mirror_tid;
			break;
		case HAMMER2_BREF_TYPE_VOLUME:
			/*
			 * The free block table is flushed by hammer2_vfs_sync()
			 * before it flushes vchain.  We must still hold fchain
			 * locked while copying voldata to volsync, however.
			 */
			hammer2_voldata_lock(hmp);
			hammer2_chain_lock(&hmp->fchain,
					   HAMMER2_RESOLVE_ALWAYS);
			/*
			 * There is no parent to our root vchain and fchain to
			 * synchronize the bref to, their updated mirror_tid's
			 * must be synchronized to the volume header.
			 */
			hmp->voldata.mirror_tid = chain->bref.mirror_tid;
			hmp->voldata.freemap_tid = hmp->fchain.bref.mirror_tid;
			kprintf("mirror_tid %08jx\n",
				(intmax_t)chain->bref.mirror_tid);

			/*
			 * The volume header is flushed manually by the
			 * syncer, not here.  All we do here is adjust the
			 * crc's.
			 */
			KKASSERT(chain->data != NULL);
			KKASSERT(chain->dio == NULL);

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
			hammer2_voldata_unlock(hmp);
			break;
		case HAMMER2_BREF_TYPE_DATA:
			/*
			 * Data elements have already been flushed via the
			 * logical file buffer cache.  Their hash was set in
			 * the bref by the vop_write code.
			 *
			 * Make sure any device buffer(s) have been flushed
			 * out here (there aren't usually any to flush) XXX.
			 */
			break;
		case HAMMER2_BREF_TYPE_INDIRECT:
		case HAMMER2_BREF_TYPE_FREEMAP_NODE:
		case HAMMER2_BREF_TYPE_FREEMAP_LEAF:
			/*
			 * Buffer I/O will be cleaned up when the volume is
			 * flushed (but the kernel is free to flush it before
			 * then, as well).
			 */
			KKASSERT((chain->flags & HAMMER2_CHAIN_EMBEDDED) == 0);
			break;
		case HAMMER2_BREF_TYPE_INODE:
			if (chain->data->ipdata.op_flags &
			    HAMMER2_OPFLAG_PFSROOT) {
				/*
				 * non-NULL pmp if mounted as a PFS.  We must
				 * sync fields cached in the pmp.
				 */
				hammer2_inode_data_t *ipdata;

				ipdata = &chain->data->ipdata;
				ipdata->pfs_inum = pmp->inode_tid;
			} else {
				/* can't be mounted as a PFS */
				KKASSERT((chain->flags &
					  HAMMER2_CHAIN_PFSROOT) == 0);
			}

			/*
			 * Update inode statistics.  Pending stats in chain
			 * are cleared out on UPDATE so expect that bit to
			 * be set here too or the statistics will not be
			 * rolled-up properly.
			 */
			{
				hammer2_inode_data_t *ipdata;

				KKASSERT(chain->flags & HAMMER2_CHAIN_UPDATE);
				ipdata = &chain->data->ipdata;
				ipdata->data_count += chain->data_count;
				ipdata->inode_count += chain->inode_count;
			}
			KKASSERT((chain->flags & HAMMER2_CHAIN_EMBEDDED) == 0);
			break;
		default:
			KKASSERT(chain->flags & HAMMER2_CHAIN_EMBEDDED);
			panic("hammer2_flush_core: unsupported "
			      "embedded bref %d",
			      chain->bref.type);
			/* NOT REACHED */
		}

		/*
		 * If the chain was destroyed try to avoid unnecessary I/O.
		 * (this only really works if the DIO system buffer is the
		 * same size as chain->bytes).
		 */
		if ((chain->flags & HAMMER2_CHAIN_DESTROY) && chain->dio) {
			hammer2_io_setinval(chain->dio, chain->bytes);
		}
	}

	/*
	 * If UPDATE is set the parent block table may need to be updated.
	 *
	 * NOTE: UPDATE may be set on vchain or fchain in which case
	 *	 parent could be NULL.  It's easiest to allow the case
	 *	 and test for NULL.  parent can also wind up being NULL
	 *	 due to a deletion so we need to handle the case anyway.
	 *
	 * If no parent exists we can just clear the UPDATE bit.  If the
	 * chain gets reattached later on the bit will simply get set
	 * again.
	 */
	if ((chain->flags & HAMMER2_CHAIN_UPDATE) && parent == NULL) {
		atomic_clear_int(&chain->flags, HAMMER2_CHAIN_UPDATE);
		hammer2_chain_drop(chain);
	}

	/*
	 * The chain may need its blockrefs updated in the parent.  This
	 * requires some fancy footwork.
	 */
	if (chain->flags & HAMMER2_CHAIN_UPDATE) {
		hammer2_blockref_t *base;
		int count;

		/*
		 * Both parent and chain must be locked.  This requires
		 * temporarily unlocking the chain.  We have to deal with
		 * the case where the chain might be reparented or modified
		 * while it was unlocked.
		 */
		hammer2_chain_unlock(chain);
		hammer2_chain_lock(parent, HAMMER2_RESOLVE_ALWAYS);
		hammer2_chain_lock(chain, HAMMER2_RESOLVE_MAYBE);
		if (chain->parent != parent) {
			kprintf("PARENT MISMATCH ch=%p p=%p/%p\n", chain, chain->parent, parent);
			hammer2_chain_unlock(parent);
			goto done;
		}

		/*
		 * Check race condition.  If someone got in and modified
		 * it again while it was unlocked, we have to loop up.
		 */
		if (chain->flags & HAMMER2_CHAIN_MODIFIED) {
			hammer2_chain_unlock(parent);
			kprintf("hammer2_flush: chain %p flush-mod race\n",
				chain);
			goto again;
		}

		/*
		 * Clear UPDATE flag
		 */
		if (chain->flags & HAMMER2_CHAIN_UPDATE) {
			atomic_clear_int(&chain->flags, HAMMER2_CHAIN_UPDATE);
			hammer2_chain_drop(chain);
		}
		hammer2_chain_modify(info->trans, parent, 0);

		/*
		 * Calculate blockmap pointer
		 */
		switch(parent->bref.type) {
		case HAMMER2_BREF_TYPE_INODE:
			/*
			 * Access the inode's block array.  However, there is
			 * no block array if the inode is flagged DIRECTDATA.
			 */
			if (parent->data &&
			    (parent->data->ipdata.op_flags &
			     HAMMER2_OPFLAG_DIRECTDATA) == 0) {
				base = &parent->data->
					ipdata.u.blockset.blockref[0];
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
			base = &chain->hmp->voldata.sroot_blockset.blockref[0];
			count = HAMMER2_SET_COUNT;
			break;
		case HAMMER2_BREF_TYPE_FREEMAP:
			base = &parent->data->npdata[0];
			count = HAMMER2_SET_COUNT;
			break;
		default:
			base = NULL;
			count = 0;
			panic("hammer2_flush_core: "
			      "unrecognized blockref type: %d",
			      parent->bref.type);
		}

		/*
		 * Blocktable updates
		 *
		 * We synchronize pending statistics at this time.  Delta
		 * adjustments designated for the current and upper level
		 * are synchronized.
		 */
		if (base && (chain->flags & HAMMER2_CHAIN_BMAPUPD)) {
			if (chain->flags & HAMMER2_CHAIN_BMAPPED) {
				hammer2_base_delete(info->trans, parent,
						    base, count,
						    &info->cache_index, chain);
				/* base_delete clears both bits */
			} else {
				atomic_clear_int(&chain->flags,
						 HAMMER2_CHAIN_BMAPUPD);
			}
		}
		if (base && (chain->flags & HAMMER2_CHAIN_BMAPPED) == 0) {
			parent->data_count += chain->data_count +
					      chain->data_count_up;
			parent->inode_count += chain->inode_count +
					       chain->inode_count_up;
			chain->data_count = 0;
			chain->inode_count = 0;
			chain->data_count_up = 0;
			chain->inode_count_up = 0;
			hammer2_base_insert(info->trans, parent,
					    base, count,
					    &info->cache_index, chain);
			/* base_insert sets BMAPPED */
		}
		hammer2_chain_unlock(parent);
	}

	/*
	 * Final cleanup after flush
	 */
done:
	KKASSERT(chain->refs > 1);
	KKASSERT(chain->bref.mirror_tid <= chain->pmp->flush_tid);
	if (hammer2_debug & 0x200) {
		if (info->debug == chain)
			info->debug = NULL;
	}
}

/*
 * Flush recursion helper, called from flush_core, calls flush_core.
 *
 * Flushes the children of the caller's chain (info->parent), restricted
 * by sync_tid.  Set info->domodify if the child's blockref must propagate
 * back up to the parent.
 *
 * Ripouts can move child from rbtree to dbtree or dbq but the caller's
 * flush scan order prevents any chains from being lost.  A child can be
 * executes more than once.
 *
 * WARNING! If we do not call hammer2_flush_core() we must update
 *	    bref.mirror_tid ourselves to indicate that the flush has
 *	    processed the child.
 *
 * WARNING! parent->core spinlock is held on entry and return.
 *
 * WARNING! Flushes do not cross PFS boundaries.  Specifically, a flush must
 *	    not cross a pfs-root boundary.
 */
static int
hammer2_flush_recurse(hammer2_chain_t *child, void *data)
{
	hammer2_flush_info_t *info = data;
	/*hammer2_trans_t *trans = info->trans;*/
	hammer2_chain_t *parent = info->parent;

	/*
	 * (child can never be fchain or vchain so a special check isn't
	 *  needed).
	 *
	 * We must ref the child before unlocking the spinlock.
	 *
	 * The caller has added a ref to the parent so we can temporarily
	 * unlock it in order to lock the child.
	 */
	hammer2_chain_ref(child);
	spin_unlock(&parent->core.cst.spin);

	hammer2_chain_unlock(parent);
	hammer2_chain_lock(child, HAMMER2_RESOLVE_MAYBE);

	/*
	 * Never recurse across a mounted PFS boundary.
	 *
	 * Recurse and collect deferral data.
	 */
	if ((child->flags & HAMMER2_CHAIN_PFSBOUNDARY) == 0 ||
	    child->pmp == NULL) {
		if (child->flags & HAMMER2_CHAIN_FLUSH_MASK) {
			++info->depth;
			hammer2_flush_core(info, child, 0); /* XXX deleting */
			--info->depth;
		} else if (hammer2_debug & 0x200) {
			if (info->debug == NULL)
				info->debug = child;
			++info->depth;
			hammer2_flush_core(info, child, 0); /* XXX deleting */
			--info->depth;
			if (info->debug == child)
				info->debug = NULL;
		}
	}

	/*
	 * Relock to continue the loop
	 */
	hammer2_chain_unlock(child);
	hammer2_chain_lock(parent, HAMMER2_RESOLVE_MAYBE);
	hammer2_chain_drop(child);
	KKASSERT(info->parent == parent);
	spin_lock(&parent->core.cst.spin);

	return (0);
}


#if 0
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
#endif
