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
	int		domodify;
	struct h2_flush_deferral_list flush_list;
	hammer2_tid_t	sync_tid;	/* flush synchronization point */
};

typedef struct hammer2_flush_info hammer2_flush_info_t;

static void hammer2_flush_core(hammer2_flush_info_t *info,
				hammer2_chain_t **chainp, int deleting);
static int hammer2_flush_pass1(hammer2_chain_t *child, void *data);
static int hammer2_flush_pass2(hammer2_chain_t *child, void *data);
static int hammer2_flush_pass3(hammer2_chain_t *child, void *data);
static int hammer2_flush_pass4(hammer2_chain_t *child, void *data);
static void hammer2_rollup_stats(hammer2_chain_t *parent,
				hammer2_chain_t *child, int how);


/*
 * Can we ignore a chain for the purposes of flushing modifications
 * to the media?
 *
 * This code is now degenerate.  We used to have to distinguish between
 * deleted chains and deleted chains associated with inodes that were
 * still open.  This mechanic has been fixed so the function is now
 * a simple test.
 */
static __inline
int
h2ignore_deleted(hammer2_flush_info_t *info, hammer2_chain_t *chain)
{
	return (chain->delete_tid <= info->sync_tid);
}

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
 *
 * NOTE: The sync_tid for a flush's freemap allocation will match the
 *	 sync_tid of the following <concurrent_fs_ops> transaction(s).
 *	 The freemap topology will be out-of-step by one transaction id
 *	 in order to give the flusher a stable freemap topology to flush
 *	 out.  This is fixed up at mount-time using a quick incremental
 *	 scan.
 */
void
hammer2_trans_init(hammer2_trans_t *trans, hammer2_pfsmount_t *pmp,
		   hammer2_mount_t *hmp, int flags)
{
	hammer2_trans_t *head;

	bzero(trans, sizeof(*trans));
	if (pmp) {
		trans->pmp = pmp;
		KKASSERT(hmp == NULL);
		hmp = pmp->cluster.chains[0]->hmp;	/* XXX */
	} else {
		trans->hmp_single = hmp;
		KKASSERT(hmp);
	}

	hammer2_voldata_lock(hmp);
	trans->flags = flags;
	trans->td = curthread;
	/*trans->delete_gen = 0;*/	/* multiple deletions within trans */

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
		++hmp->flushcnt;
		++hmp->voldata.alloc_tid;
		trans->sync_tid = hmp->voldata.alloc_tid;
		trans->orig_tid = trans->sync_tid;
		++hmp->voldata.alloc_tid;
		TAILQ_INSERT_TAIL(&hmp->transq, trans, entry);
		if (TAILQ_FIRST(&hmp->transq) != trans) {
			trans->blocked = 1;
			while (trans->blocked) {
				lksleep(&trans->sync_tid, &hmp->voldatalk,
					0, "h2multf", hz);
			}
		}
	} else if (hmp->flushcnt == 0) {
		/*
		 * No flushes are pending, we can go.
		 */
		TAILQ_INSERT_TAIL(&hmp->transq, trans, entry);
		trans->sync_tid = hmp->voldata.alloc_tid;
		trans->orig_tid = trans->sync_tid;

		/* XXX improve/optimize inode allocation */
	} else {
		/*
		 * One or more flushes are pending.  We insert after
		 * the current flush and may block.  We have priority
		 * over any flushes that are not the current flush.
		 *
		 * TRANS_BUFCACHE transactions cannot block.
		 */
		TAILQ_FOREACH(head, &hmp->transq, entry) {
			if (head->flags & HAMMER2_TRANS_ISFLUSH)
				break;
		}
		KKASSERT(head);
		TAILQ_INSERT_AFTER(&hmp->transq, head, trans, entry);
		trans->sync_tid = head->orig_tid + 1;
		trans->orig_tid = trans->sync_tid;
		trans->flags |= HAMMER2_TRANS_CONCURRENT;

		if ((trans->flags & HAMMER2_TRANS_BUFCACHE) == 0) {
			/*
			 * If synchronous flush mode is enabled concurrent
			 * frontend transactions during the flush are not
			 * allowed (except we don't have a choice for buffer
			 * cache ops).
			 */
			if (hammer2_synchronous_flush > 0 ||
			    TAILQ_FIRST(&hmp->transq) != head) {
				trans->blocked = 1;
				while (trans->blocked) {
					lksleep(&trans->sync_tid,
						&hmp->voldatalk, 0,
						"h2multf", hz);
				}
			}
		}
	}
	if (flags & HAMMER2_TRANS_NEWINODE) {
		if (hmp->voldata.inode_tid < HAMMER2_INODE_START)
			hmp->voldata.inode_tid = HAMMER2_INODE_START;
		trans->inode_tid = hmp->voldata.inode_tid++;
	}
	hammer2_voldata_unlock(hmp, 0);
}

void
hammer2_trans_done(hammer2_trans_t *trans)
{
	hammer2_mount_t *hmp;
	hammer2_trans_t *head;
	hammer2_trans_t *scan;

	if (trans->pmp)
		hmp = trans->pmp->cluster.chains[0]->hmp;
	else
		hmp = trans->hmp_single;

	/*
	 * Remove.
	 */
	hammer2_voldata_lock(hmp);
	TAILQ_REMOVE(&hmp->transq, trans, entry);
	head = TAILQ_FIRST(&hmp->transq);

	/*
	 * Adjust flushcnt if this was a flush, clear TRANS_CONCURRENT
	 * up through the next flush.  (If the head is a flush then we
	 * stop there, unlike the unblock code following this section).
	 */
	if (trans->flags & HAMMER2_TRANS_ISFLUSH) {
		--hmp->flushcnt;
		scan = head;
		while (scan && (scan->flags & HAMMER2_TRANS_ISFLUSH) == 0) {
			atomic_clear_int(&scan->flags,
					 HAMMER2_TRANS_CONCURRENT);
			scan = TAILQ_NEXT(head, entry);
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
		wakeup(&head->sync_tid);

		if (hammer2_synchronous_flush > 0)
			scan = head;
		else
			scan = TAILQ_NEXT(head, entry);
		while (scan && (scan->flags & HAMMER2_TRANS_ISFLUSH) == 0) {
			if (scan->blocked) {
				scan->blocked = 0;
				wakeup(&scan->sync_tid);
			}
			scan = TAILQ_NEXT(scan, entry);
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
 * is from VFS_SYNC.
 *
 * chain is locked on call and will remain locked on return.  If a flush
 * occured, the chain's FLUSH_CREATE and/or FLUSH_DELETE bit will be set
 * indicating that its parent (which is not part of the flush) should be
 * updated.  The chain may be replaced by the call if it was modified.
 */
void
hammer2_flush(hammer2_trans_t *trans, hammer2_chain_t **chainp)
{
	hammer2_chain_t *chain = *chainp;
	hammer2_chain_t *scan;
	hammer2_chain_core_t *core;
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
	TAILQ_INIT(&info.flush_list);
	info.trans = trans;
	info.sync_tid = trans->sync_tid;
	info.cache_index = -1;

	core = chain->core;

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
		while ((scan = TAILQ_FIRST(&info.flush_list)) != NULL) {
			KKASSERT(scan->flags & HAMMER2_CHAIN_DEFERRED);
			TAILQ_REMOVE(&info.flush_list, scan, flush_node);
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
			hammer2_flush(trans, &scan);
			hammer2_chain_unlock(scan);
		}

		/*
		 * [re]flush chain.
		 */
		info.diddeferral = 0;
		hammer2_flush_core(&info, &chain, 0);

		/*
		 * Only loop if deep recursions have been deferred.
		 */
		if (TAILQ_EMPTY(&info.flush_list))
			break;

		if (++loops % 1000 == 0) {
			kprintf("hammer2_flush: excessive loops on %p\n",
				chain);
			if (hammer2_debug & 0x100000)
				Debugger("hell4");
		}
	}
	hammer2_chain_drop(chain);
	*chainp = chain;
}

/*
 * This is the core of the chain flushing code.  The chain is locked by the
 * caller and must also have an extra ref on it by the caller, and remains
 * locked and will have an extra ref on return.  Upon return, the caller can
 * test the FLUSH_CREATE and FLUSH_DELETE bits to determine what action must
 * be taken on the parent.
 *
 * (1) Determine if this node is a candidate for the flush, return if it is
 *     not.  fchain and vchain are always candidates for the flush.
 *
 * (2) If we recurse too deep the chain is entered onto the deferral list and
 *     the current flush stack is aborted until after the deferral list is
 *     run.
 *
 * (3) Recursively flush live children (rbtree).  This can create deferrals.
 *     A successful flush clears the MODIFIED bit in the children.
 *
 * (4) Recursively flush deleted children (dbtree).  Deletions may be
 *     considered 'live' if the delete_tid is beyond the flush_tid.  If
 *     considered 'dead' the recursion is still needed in order to clean
 *     up the chain.  This can create deferrals.
 *
 *     A successful flush clears the MODIFIED bit in the children.
 *
 * (5) Calculate block table updates on chain based on the children scans
 *     in (3) and (4) by testing the FLUSH_CREATE and FLUSH_DELETE bits,
 *     modifying chain if necessary to perform the block table updates.
 *     Deletions must be removed from dbtree when removed from the
 *     chain's block table.
 *
 *     If 'chain' itself is marked DELETED but treated as live, the block
 *     table update(s) must be propagated to all contemporary chains.  In
 *     fact, all contemporary chains must be locked and updated uninterrupted
 *     to avoid lookup races.  Once MODIFIED and FLUSH_CREATE is cleared,
 *     a chain can be unloaded from memory with the expectation that it can
 *     be reloaded later via the block table at any time.
 *
 * NOTE: chain->bref.modify_tid is different from chain->modify_tid.  COW
 *	 propagations for block updates do not update chain->bref.modify_tid,
 *	 only chain->bref.mirror_tid.  The MODIFIED bit is set on any
 *	 modified chain, including COW propagations, but the flusher normally
 *	 just keys off of the FLUSH_* bits.  FLUSH_CREATE will also be set
 *	 in this situation.
 *
 * NOTE: We are responsible for updating chain->bref.mirror_tid and
 *	 chain->update_lo.  The caller is responsible for processing us into
 *	 our parent (if any).
 */
static void
hammer2_flush_core(hammer2_flush_info_t *info, hammer2_chain_t **chainp,
		   int deleting)
{
	hammer2_chain_t *chain = *chainp;
	hammer2_chain_t *saved_parent;
	hammer2_mount_t *hmp;
	hammer2_chain_core_t *core;
	int diddeferral;
	int saved_domodify;

	hmp = chain->hmp;
	core = chain->core;
	diddeferral = info->diddeferral;

	/*
	 * (1) Check if we even have any work to do.
	 *
	 * This bit of code is capable of short-cutting entire sub-trees
	 * if they have not been touched or if they have already been
	 * flushed.
	 */
	if (/*(chain->flags & HAMMER2_CHAIN_MODIFIED) == 0 &&*/
	    (chain->update_lo >= info->sync_tid ||	/* already synced */
	     chain->update_lo >= chain->update_hi)) {	/* old/unchanged */
		/* update_lo/_hi already filters chain out, do not update */
		/* don't update bref.mirror_tid, pass2 is not called */
		return;
	}

	/*
	 * mirror_tid should not be forward-indexed
	 */
	KKASSERT(chain->bref.mirror_tid <= info->sync_tid);

	/*
	 * Ignore chains modified beyond the current flush point.  These
	 * will be treated as if they did not exist.  Subchains with lower
	 * modify_tid's will still be accessible via other parents.
	 *
	 * Do not update bref.mirror_tid here, it will interfere with
	 * synchronization.  e.g. inode flush tid 1, concurrent D-D tid 2,
	 * then later on inode flush tid 2.  If we were to set mirror_tid
	 * to 1 during inode flush tid 1 the blockrefs would only be partially
	 * updated (and likely panic).
	 *
	 * We must update chain->update_lo here to prevent re-entry in this
	 * flush transaction.
	 *
	 * (vchain and fchain are exceptions since they cannot be duplicated)
	 */
	if (chain->modify_tid > info->sync_tid &&
	    chain != &hmp->fchain && chain != &hmp->vchain) {
		/* do not update bref.mirror_tid, pass2 ignores chain */
		/* chain->update_lo = info->sync_tid; */
		return;
	}

	/*
	 * (2) Recurse downward and check recursion depth.
	 * (3) Flush live children
	 * (4) Flush deleted children
	 *
	 * We adjust update_lo if not deferring chain to prevent re-entry
	 * in this flush cycle, but it must be set AFTER the flush in case
	 * a deeper flush hits the chain.  Otherwise the deeper flush cannot
	 * complete.  We re-check the condition after finishing the flushes.
	 *
	 * update_hi was already checked and prevents initial recursions on
	 * subtrees which have not been modified.
	 */
	saved_parent = info->parent;
	saved_domodify = info->domodify;
	info->parent = chain;
	info->domodify = 0;

	if (chain->flags & HAMMER2_CHAIN_DEFERRED) {
		++info->diddeferral;
	} else if (info->depth == HAMMER2_FLUSH_DEPTH_LIMIT) {
		if ((chain->flags & HAMMER2_CHAIN_DEFERRED) == 0) {
			hammer2_chain_ref(chain);
			TAILQ_INSERT_TAIL(&info->flush_list,
					  chain, flush_node);
			atomic_set_int(&chain->flags,
				       HAMMER2_CHAIN_DEFERRED);
		}
		++info->diddeferral;
	} else {
		hammer2_chain_t *scan;

		/*
		 * The flush is queue-agnostic when running pass1, but order
		 * is important to catch any races where an existing
		 * flush-visible child is moved from rbtree->dbtree/dbq.
		 *
		 * New children added by concurrent operations are not visible
		 * to the flush anyway so we don't care about those races.
		 * However, the flush itself can move a child from dbq to
		 * dbtree (rare in pass1 but it is possible).
		 *
		 * pass1 can handle re-execution of a child.
		 */
		spin_lock(&core->cst.spin);
		KKASSERT(core->good == 0x1234 && core->sharecnt > 0);
		RB_SCAN(hammer2_chain_tree, &core->rbtree,
			NULL, hammer2_flush_pass1, info);
		RB_SCAN(hammer2_chain_tree, &core->dbtree,
			NULL, hammer2_flush_pass1, info);
		scan = TAILQ_FIRST(&core->dbq);
		while (scan) {
			KKASSERT(scan->flags & HAMMER2_CHAIN_ONDBQ);
			hammer2_flush_pass1(scan, info);
			if (scan->flags & HAMMER2_CHAIN_ONDBQ)
				scan = TAILQ_NEXT(scan, db_entry);
			else
				scan = TAILQ_FIRST(&core->dbq);
		}
		spin_unlock(&core->cst.spin);
	}

	/*
	 * Stop if deferred, do not update update_lo.
	 */
	if (info->diddeferral)
		goto done;

	/*
	 * If a block table update is needed place the parent in a modified
	 * state, which might delete-duplicate it.
	 *
	 * - To prevent loops and other confusion, we synchronize update_lo
	 *   for the original chain.
	 *
	 * - The original parent will not be used by the flush so we can
	 *   clear its MODIFIED bit.
	 */
	if (info->domodify) {
		hammer2_chain_modify(info->trans, &info->parent,
				     HAMMER2_MODIFY_NO_MODIFY_TID);
		if (info->parent != chain) {
			/*
			 * chain	- old
			 * info->parent - new
			 *
			 * NOTE: bref.mirror_tid cannot be updated
			 *	 unless MODIFIED is cleared or already
			 *	 clear.
			 */
			chain->inode_reason += 0x10000000;
			info->parent->inode_reason += 0x100;
			KKASSERT(info->parent->core == chain->core);
			if (chain->flags & HAMMER2_CHAIN_MODIFIED) {
				atomic_clear_int(&chain->flags,
						HAMMER2_CHAIN_MODIFIED);
				hammer2_chain_memory_wakeup(chain->pmp);
				hammer2_chain_drop(chain);
			}
#if 0
			if (chain->flags & HAMMER2_CHAIN_FLUSH_CREATE) {
				atomic_clear_int(&chain->flags,
						HAMMER2_CHAIN_FLUSH_CREATE);
				hammer2_chain_drop(chain);
			}
			if (info->parent->flags & HAMMER2_CHAIN_FLUSH_DELETE) {
				atomic_clear_int(&info->parent->flags,
						HAMMER2_CHAIN_FLUSH_DELETE);
				hammer2_chain_drop(info->parent);
			}
#endif
#if 0
			if (chain->bref.mirror_tid < info->sync_tid)
				chain->bref.mirror_tid = info->sync_tid;
#endif
			if (chain->update_lo < info->sync_tid)
				chain->update_lo = info->sync_tid;
			KKASSERT(info->parent->update_lo < info->sync_tid);
			hammer2_chain_drop(chain);
			hammer2_chain_ref(info->parent);
		}
		chain = info->parent;
	}

	/*
	 * If a blocktable update is needed determine if this is the last
	 * parent requiring modification (check all parents using the core).
	 *
	 * Set bit 1 (0x02) of domodify if this is the last parent,
	 * which will cause scan2 to clear FLUSH_CREATE and FLUSH_DELETE.
	 */
	if (1) {
		hammer2_chain_t *scan;

		spin_lock(&core->cst.spin);
		TAILQ_FOREACH(scan, &core->ownerq, core_entry) {
			/*
			 * Ignore chains which have already been updated
			 * Ignore unmodified chains
			 */
			if ((scan->flags & HAMMER2_CHAIN_MODIFIED) == 0 &&
			    (scan->update_lo >= info->sync_tid ||
			     scan->update_lo >= scan->update_hi)) {
				continue;
			}

			/*
			 * Ignore the current parent being processed (we do
			 * not adjust update_lo until after the fixup).
			 */
			if (scan == chain)
				continue;

			/*
			 * Cannot exhaust all parents if one is not visible
			 * to the flush.  The root chains are special-cased
			 * because they cannot really be delete-duplicated.
			 */
			if (scan != &scan->hmp->fchain &&
			    scan != &scan->hmp->vchain &&
			    scan->modify_tid > info->sync_tid) {
				break;
			}

			/*
			 * Fail if update_lo has not been synchronized to
			 * at least our sync_tid on any modified parent chain.
			 */
			if (scan->update_lo < info->sync_tid)
				break;
		}
		spin_unlock(&core->cst.spin);
		if (scan == NULL)
			info->domodify |= 2;
	}

	/*
	 * (5) Calculate block table updates or child cleanups.
	 *     (this whole operation has to be atomic)
	 *
	 * domodify 0x01 - block table updates
	 * 	    0x02 - child cleanups
	 *
	 *	pass2 - Process deletions from dbtree and dbq.
	 *	pass3 - Process insertions from rbtree, dbtree, and dbq.
	 *	pass4 - Cleanup child flags on the last parent and
	 *		Adjust queues on the live parent.
	 */
	if (info->domodify) {
		hammer2_chain_t *scan;

		spin_lock(&core->cst.spin);

		while ((info->domodify & 1) && info->parent) {
			/* PASS2 - Deletions */
			RB_SCAN(hammer2_chain_tree, &core->rbtree,
				NULL, hammer2_flush_pass2, info);
			RB_SCAN(hammer2_chain_tree, &core->dbtree,
				NULL, hammer2_flush_pass2, info);
			scan = TAILQ_FIRST(&core->dbq);
			TAILQ_FOREACH(scan, &core->dbq, db_entry) {
				KKASSERT(scan->flags & HAMMER2_CHAIN_ONDBQ);
				hammer2_flush_pass2(scan, info);
			}

			/* PASS3 - Insertions */
			RB_SCAN(hammer2_chain_tree, &core->rbtree,
				NULL, hammer2_flush_pass3, info);
			RB_SCAN(hammer2_chain_tree, &core->dbtree,
				NULL, hammer2_flush_pass3, info);
			TAILQ_FOREACH(scan, &core->dbq, db_entry) {
				KKASSERT(scan->flags & HAMMER2_CHAIN_ONDBQ);
				hammer2_flush_pass3(scan, info);
			}
			info->parent = TAILQ_NEXT(info->parent, core_entry);
			if (info->parent)
				kprintf("FLUSH SPECIAL UPDATE (%p) %p.%d %08x\n",
					chain, info->parent,
					info->parent->bref.type,
					info->parent->flags);
		}
		info->parent = chain;

		/* PASS4 - Cleanup */
		RB_SCAN(hammer2_chain_tree, &core->rbtree,
			NULL, hammer2_flush_pass4, info);
		scan = TAILQ_FIRST(&core->dbq);
		while (scan) {
			KKASSERT(scan->flags & HAMMER2_CHAIN_ONDBQ);
			hammer2_flush_pass4(scan, info);
			if (scan->flags & HAMMER2_CHAIN_ONDBQ)
				scan = TAILQ_NEXT(scan, db_entry);
			else
				scan = TAILQ_FIRST(&core->dbq);
		}
		RB_SCAN(hammer2_chain_tree, &core->dbtree,
			NULL, hammer2_flush_pass4, info);

		spin_unlock(&core->cst.spin);
	}

	/*
	 * Synchronize update_lo to prevent reentrant block updates of this
	 * parent.
	 */
	chain->update_lo = info->sync_tid;

	/*
	 * Skip the flush if the chain was not placed in a modified state
	 * or was not already in a modified state.
	 */
	if ((chain->flags & HAMMER2_CHAIN_MODIFIED) == 0)
		goto done;

	/*
	 * FLUSH THE CHAIN (on the way back up the recursion)
	 *
	 * Chain is now deterministically being flushed and not being deferred.
	 * We've finished running the recursion and the blockref update.
	 *
	 * update bref.mirror_tid.  update_lo has already been updated.
	 */
	if (chain->bref.mirror_tid < info->sync_tid)
		chain->bref.mirror_tid = info->sync_tid;

	/*
	 * Dispose of the modified bit.  FLUSH_CREATE should already be
	 * set.
	 */
	KKASSERT(chain->flags & HAMMER2_CHAIN_FLUSH_CREATE);
	atomic_clear_int(&chain->flags, HAMMER2_CHAIN_MODIFIED);
	hammer2_chain_memory_wakeup(chain->pmp);

	if ((chain->flags & HAMMER2_CHAIN_FLUSH_CREATE) ||
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
		 * set a ref for the FLUSH_CREATE bit we are setting.
		 * Net 0 refs.
		 */
		atomic_set_int(&chain->flags, HAMMER2_CHAIN_FLUSH_CREATE);
	}

	/*
	 * Skip the actual flush operation if the chain has been deleted
	 * in our flus hview.  There will be no block table entry that
	 * references it.
	 */
	if (h2ignore_deleted(info, chain))
		goto done;

	/*
	 * Issue flush.
	 *
	 * A DELETED node that reaches this point must be flushed for
	 * synchronization point consistency.
	 *
	 * Update bref.mirror_tid, clear MODIFIED, and set MOVED.
	 *
	 * The caller will update the parent's reference to this chain
	 * by testing MOVED as long as the modification was in-bounds.
	 *
	 * MOVED is never set on the volume root as there is no parent
	 * to adjust.
	 */
	if (hammer2_debug & 0x1000) {
		kprintf("Flush %p.%d %016jx/%d sync_tid=%016jx data=%016jx\n",
			chain, chain->bref.type,
			chain->bref.key, chain->bref.keybits,
			info->sync_tid, chain->bref.data_off);
	}
	if (hammer2_debug & 0x2000) {
		Debugger("Flush hell");
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
		hmp->voldata.freemap_tid = hmp->fchain.bref.mirror_tid;
		break;
	case HAMMER2_BREF_TYPE_VOLUME:
		/*
		 * The free block table is flushed by hammer2_vfs_sync()
		 * before it flushes vchain.  We must still hold fchain
		 * locked while copying voldata to volsync, however.
		 */
		hammer2_chain_lock(&hmp->fchain, HAMMER2_RESOLVE_ALWAYS);
#if 0
		if ((hmp->fchain.flags & HAMMER2_CHAIN_MODIFIED) ||
		    hmp->voldata.freemap_tid < info->trans->sync_tid) {
			/* this will modify vchain as a side effect */
			hammer2_chain_t *tmp = &hmp->fchain;
			hammer2_chain_flush(info->trans, &tmp);
			KKASSERT(tmp == &hmp->fchain);
		}
#endif

		/*
		 * There is no parent to our root vchain and fchain to
		 * synchronize the bref to, their updated mirror_tid's
		 * must be synchronized to the volume header.
		 */
		hmp->voldata.mirror_tid = chain->bref.mirror_tid;
		/*hmp->voldata.freemap_tid = hmp->fchain.bref.mirror_tid;*/

		/*
		 * The volume header is flushed manually by the syncer, not
		 * here.  All we do here is adjust the crc's.
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
		KKASSERT(chain->dio != NULL);

		chain->data = NULL;
		hammer2_io_bqrelse(&chain->dio);
		hammer2_chain_unlock(chain);
		break;
#endif
	case HAMMER2_BREF_TYPE_INDIRECT:
	case HAMMER2_BREF_TYPE_FREEMAP_NODE:
	case HAMMER2_BREF_TYPE_FREEMAP_LEAF:
	case HAMMER2_BREF_TYPE_INODE:
		/*
		 * Device-backed.  Buffer will be flushed by the sync
		 * code XXX.
		 */
		KKASSERT((chain->flags & HAMMER2_CHAIN_EMBEDDED) == 0);
		break;
	default:
		KKASSERT(chain->flags & HAMMER2_CHAIN_EMBEDDED);
		panic("hammer2_chain_flush_core: unsupported embedded bref %d",
		      chain->bref.type);
		/* NOT REACHED */
	}

	/*
	 * Final cleanup after flush
	 */
done:
	KKASSERT(chain->refs > 1);
	info->domodify = saved_domodify;
	info->parent = saved_parent;
	*chainp = chain;

	KKASSERT(chain->bref.mirror_tid <= info->sync_tid);
}

/*
 * Flush helper pass1 (recursive)
 *
 * Flushes the children of the caller's chain (info->parent), restricted
 * by sync_tid.  Set info->domodify if the child's blockref must propagate
 * back up to the parent.
 *
 * Ripouts can move child from rbtree to dbtree or dbq but the caller's
 * flush scan order prevents any chains from being lost.  A child can be
 * executes more than once (update_lo is used to prevent infinite recursions).
 *
 * WARNING! If we do not call hammer2_flush_core() we must update
 *	    bref.mirror_tid ourselves to indicate that the flush has
 *	    processed the child.
 *
 * WARNING! parent->core spinlock is held on entry and return.
 */
static int
hammer2_flush_pass1(hammer2_chain_t *child, void *data)
{
	hammer2_flush_info_t *info = data;
	hammer2_trans_t *trans = info->trans;
	hammer2_chain_t *parent = info->parent;

	/*
	 * Child modified in a later transactions, nothing to flush in this
	 * transaction.
	 *
	 * Remember that modifications generally delete-duplicate so if the
	 * sub-tree is dirty another child will get us there.  But not this
	 * one.
	 *
	 * (child can never be fchain or vchain so a special check isn't
	 *  needed).
	 */
	if (child->modify_tid > trans->sync_tid) {
		KKASSERT(child->delete_tid >= child->modify_tid);
		/*child->update_lo = info->sync_tid;*/
		/* do not update mirror_tid, pass2 will ignore chain */
		return (0);
	}

	/*
	 * We must ref the child before unlocking the spinlock.
	 *
	 * The caller has added a ref to the parent so we can temporarily
	 * unlock it in order to lock the child.
	 */
	hammer2_chain_ref(child);
	spin_unlock(&parent->core->cst.spin);

	hammer2_chain_unlock(parent);
	hammer2_chain_lock(child, HAMMER2_RESOLVE_MAYBE);

	/*
	 * Recurse and collect deferral data.  We only recursively sync
	 * (basically) if update_lo has not been updated, indicating that
	 * the child has not already been processed.
	 */
	if ((child->flags & HAMMER2_CHAIN_MODIFIED) ||
	    (child->update_lo < info->sync_tid &&
	     child->update_lo < child->update_hi)) {
		++info->depth;
		hammer2_flush_core(info, &child, 0); /* XXX deleting */
		--info->depth;
	}

	/*
	 * Determine if domodify should be set.  Do not otherwise adjust
	 * the child or pass2 will get confused.
	 *
	 * Insertion:
	 *	- child is flagged as possibly needing block table insertion.
	 *	- child not deleted or deletion is beyond transaction id
	 *	- child created beyond parent synchronization point
	 *	- parent not deleted as-of this transaction
	 */
	if ((child->flags & HAMMER2_CHAIN_FLUSH_CREATE) &&
	    child->delete_tid > trans->sync_tid &&
	    child->modify_tid > parent->update_lo &&
	    parent->delete_tid > trans->sync_tid) {
		info->domodify = 1;
	}

	/*
	 * Removal:
	 *	- child is flagged as possibly needing block table removal.
	 *	- child deleted before or during this transaction
	 *	- child created prior or during parent synchronization point
	 *	- parent not yet synchronized to child deletion
	 *	- parent not deleted as-of this transaction
	 */
	if ((child->flags & HAMMER2_CHAIN_FLUSH_DELETE) &&
	    child->delete_tid <= trans->sync_tid &&
	    child->modify_tid <= parent->update_lo &&
	    child->delete_tid > parent->update_lo &&
	    parent->delete_tid > trans->sync_tid) {
		info->domodify = 1;
	}

	/*
	 * Relock to continue the loop
	 */
	hammer2_chain_unlock(child);
	hammer2_chain_lock(parent, HAMMER2_RESOLVE_MAYBE);
	hammer2_chain_drop(child);
	KKASSERT(info->parent == parent);

	spin_lock(&parent->core->cst.spin);
	return (0);
}

/*
 * PASS2 - BLOCKTABLE DELETIONS
 */
static int
hammer2_flush_pass2(hammer2_chain_t *child, void *data)
{
	hammer2_flush_info_t *info = data;
	hammer2_chain_t *parent = info->parent;
	hammer2_mount_t *hmp = child->hmp;
	hammer2_trans_t *trans = info->trans;
	hammer2_blockref_t *base;
	int count;

	/*
	 * Prefilter - Ignore children not flagged as needing a parent
	 *	       blocktable update.
	 */
	if ((child->flags & HAMMER2_CHAIN_FLUSH_DELETE) == 0)
		return (0);

	/*
	 * Prefilter - Ignore children created after our flush_tid (not
	 *	       visible to our flush).
	 */
	if (child->modify_tid > trans->sync_tid) {
		KKASSERT(child->delete_tid >= child->modify_tid);
		return 0;
	}

	/*
	 * Prefilter - Don't bother updating the blockrefs for a deleted
	 *	       parent (from the flush's perspective).  Otherwise,
	 *	       we need to be COUNTEDBREFS synchronized for the
	 *	       hammer2_base_*() functions.
	 *
	 * NOTE: This test must match the similar one in flush_core.
	 */
	if (h2ignore_deleted(info, parent))
		return 0;

	/*
	 * Calculate blockmap pointer
	 */
	switch(parent->bref.type) {
	case HAMMER2_BREF_TYPE_INODE:
		/*
		 * Access the inode's block array.  However, there is no
		 * block array if the inode is flagged DIRECTDATA.  The
		 * DIRECTDATA case typicaly only occurs when a hardlink has
		 * been shifted up the tree and the original inode gets
		 * replaced with an OBJTYPE_HARDLINK placeholding inode.
		 */
		if (parent->data &&
		    (parent->data->ipdata.op_flags &
		     HAMMER2_OPFLAG_DIRECTDATA) == 0) {
			base = &parent->data->ipdata.u.blockset.blockref[0];
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
		panic("hammer2_chain_flush_pass2: "
		      "unrecognized blockref type: %d",
		      parent->bref.type);
	}

	/*
	 * Removal
	 *	- child is flagged for removal
	 *	- child deleted before or during this transaction
	 *	- child created prior or during parent synchronization point
	 *	- parent not yet synchronized to child's deletion
	 */
	if (child->delete_tid <= trans->sync_tid &&
	    child->modify_tid <= parent->update_lo &&
	    child->delete_tid > parent->update_lo) {
		/* can't assert BMAPPED because state adjustment may occur
		 * before we are done, and BMAPPED only applies to the live
		 * parent.
		 *KKASSERT(child->flags & HAMMER2_CHAIN_BMAPPED);*/
		if (base) {
			hammer2_rollup_stats(parent, child, -1);
			hammer2_base_delete(trans, parent, base, count,
					    &info->cache_index, child);
		}
	}

	return 0;
}

/*
 * PASS3 - BLOCKTABLE INSERTIONS
 */
static int
hammer2_flush_pass3(hammer2_chain_t *child, void *data)
{
	hammer2_flush_info_t *info = data;
	hammer2_chain_t *parent = info->parent;
	hammer2_mount_t *hmp = child->hmp;
	hammer2_trans_t *trans = info->trans;
	hammer2_blockref_t *base;
	int count;

	/*
	 * Prefilter - Ignore children not flagged as needing a parent
	 *	       blocktable update.
	 */
	if ((child->flags & HAMMER2_CHAIN_FLUSH_CREATE) == 0)
		return (0);

	/*
	 * Prefilter - Ignore children created after our flush_tid (not
	 *	       visible to our flush).
	 */
	if (child->modify_tid > trans->sync_tid) {
		KKASSERT(child->delete_tid >= child->modify_tid);
		return 0;
	}

	/*
	 * Prefilter - Don't bother updating the blockrefs for a deleted
	 *	       parent (from the flush's perspective).  Otherwise,
	 *	       we need to be COUNTEDBREFS synchronized for the
	 *	       hammer2_base_*() functions.
	 *
	 * NOTE: This test must match the similar one in flush_core.
	 */
	if (h2ignore_deleted(info, parent))
		return 0;

	/*
	 * Calculate blockmap pointer
	 */
	switch(parent->bref.type) {
	case HAMMER2_BREF_TYPE_INODE:
		/*
		 * Access the inode's block array.  However, there is no
		 * block array if the inode is flagged DIRECTDATA.  The
		 * DIRECTDATA case typicaly only occurs when a hardlink has
		 * been shifted up the tree and the original inode gets
		 * replaced with an OBJTYPE_HARDLINK placeholding inode.
		 */
		if (parent->data &&
		    (parent->data->ipdata.op_flags &
		     HAMMER2_OPFLAG_DIRECTDATA) == 0) {
			base = &parent->data->ipdata.u.blockset.blockref[0];
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
		panic("hammer2_chain_flush_pass2: "
		      "unrecognized blockref type: %d",
		      parent->bref.type);
	}

	/*
	 * Insertion
	 *	- child is flagged as possibly needing block table insertion.
	 *	- child not deleted or deletion is beyond transaction id
	 *	- child created beyond parent synchronization point
	 */
	if (child->delete_tid > trans->sync_tid &&
	    child->modify_tid > parent->update_lo) {
		if (base) {
			hammer2_rollup_stats(parent, child, 1);
			hammer2_base_insert(trans, parent, base, count,
					    &info->cache_index, child);
		}
	}

	return 0;
}

/*
 * PASS4 - CLEANUP CHILDREN (non-recursive, but CAN be re-entrant)
 *
 * Adjust queues and set or clear BMAPPED appropriately if processing
 * the live parent.
 *
 * Cleanup FLUSH_CREATE/FLUSH_DELETE on the last parent.
 */
static int
hammer2_flush_pass4(hammer2_chain_t *child, void *data)
{
	hammer2_flush_info_t *info = data;
	hammer2_chain_t *parent = info->parent;
	hammer2_chain_t *xchain;
	hammer2_chain_core_t *above = child->above;
	hammer2_trans_t *trans = info->trans;

	/*
	 * Prefilter - Ignore children created after our flush_tid (not
	 *	       visible to our flush).
	 */
	if (child->modify_tid > trans->sync_tid) {
		KKASSERT(child->delete_tid >= child->modify_tid);
		return 0;
	}

	/*
	 * Ref and lock child for operation, spinlock must be temporarily
	 * Make sure child is referenced before we unlock.
	 */
	hammer2_chain_ref(child);
	spin_unlock(&above->cst.spin);
	hammer2_chain_lock(child, HAMMER2_RESOLVE_NEVER);
	KKASSERT(child->above == above);
	KKASSERT(parent->core == above);

	/*
	 * Adjust BMAPPED state and rbtree/queue only when we hit the
	 * actual live parent.
	 */
	if ((parent->flags & HAMMER2_CHAIN_DELETED) == 0) {
		/*
		 * Deleting from blockmap, move child out of dbtree
		 * and clear BMAPPED.  Child should not be on RBTREE.
		 */
		spin_lock(&above->cst.spin);
		if (child->delete_tid <= trans->sync_tid &&
		    child->modify_tid <= parent->update_lo &&
		    child->delete_tid > parent->update_lo &&
		    (child->flags & HAMMER2_CHAIN_BMAPPED)) {
			KKASSERT(child->flags & HAMMER2_CHAIN_ONDBTREE);
			RB_REMOVE(hammer2_chain_tree, &above->dbtree, child);
			atomic_clear_int(&child->flags, HAMMER2_CHAIN_ONDBTREE);
			atomic_clear_int(&child->flags, HAMMER2_CHAIN_BMAPPED);
		}

		/*
		 * Inserting into blockmap, place child in rbtree or dbtree.
		 */
		if (child->delete_tid > trans->sync_tid &&
		    child->modify_tid > parent->update_lo &&
		    (child->flags & HAMMER2_CHAIN_BMAPPED) == 0) {
			if (child->flags & HAMMER2_CHAIN_ONDBQ) {
				TAILQ_REMOVE(&above->dbq, child, db_entry);
				atomic_clear_int(&child->flags,
						 HAMMER2_CHAIN_ONDBQ);
			}
			if ((child->flags & HAMMER2_CHAIN_DELETED) == 0 &&
			    (child->flags & HAMMER2_CHAIN_ONRBTREE) == 0) {
				KKASSERT((child->flags &
					  (HAMMER2_CHAIN_ONDBTREE |
					   HAMMER2_CHAIN_ONDBQ)) == 0);
				xchain = RB_INSERT(hammer2_chain_tree,
						   &above->rbtree, child);
				KKASSERT(xchain == NULL);
				atomic_set_int(&child->flags,
					       HAMMER2_CHAIN_ONRBTREE);
			} else
			if ((child->flags & HAMMER2_CHAIN_DELETED) &&
			    (child->flags & HAMMER2_CHAIN_ONDBTREE) == 0) {
				KKASSERT((child->flags &
					  (HAMMER2_CHAIN_ONRBTREE |
					   HAMMER2_CHAIN_ONDBQ)) == 0);
				xchain = RB_INSERT(hammer2_chain_tree,
						   &above->dbtree, child);
				KKASSERT(xchain == NULL);
				atomic_set_int(&child->flags,
					       HAMMER2_CHAIN_ONDBTREE);
			}
			atomic_set_int(&child->flags, HAMMER2_CHAIN_BMAPPED);
			KKASSERT(child->flags &
				 (HAMMER2_CHAIN_ONRBTREE |
				  HAMMER2_CHAIN_ONDBTREE |
				  HAMMER2_CHAIN_ONDBQ));
		}

		/*
		 * Not on any list, place child on DBQ
		 */
		if ((child->flags & (HAMMER2_CHAIN_ONRBTREE |
				     HAMMER2_CHAIN_ONDBTREE |
				     HAMMER2_CHAIN_ONDBQ)) == 0) {
			KKASSERT((child->flags & HAMMER2_CHAIN_BMAPPED) == 0);
			TAILQ_INSERT_TAIL(&above->dbq, child, db_entry);
			atomic_set_int(&child->flags, HAMMER2_CHAIN_ONDBQ);
		}
		spin_unlock(&above->cst.spin);
	}

#if 0
	/*
	 * Adjust queues and adjust BMAPPED on the live parent only
	 * (there should be only one).
	 *
	 * First handle deletions
	 */
	if (parent->delete_tid > info->sync_tid &&
	    child->delete_tid <= trans->sync_tid &&
	    child->modify_tid <= parent->update_lo &&
	    child->delete_tid > parent->update_lo) {
		KKASSERT(child->flags & HAMMER2_CHAIN_FLUSH_DELETE);
		if (child->flags & HAMMER2_CHAIN_ONDBTREE) {
			KKASSERT(child->flags & HAMMER2_CHAIN_FLUSH_DELETE);
			KKASSERT(child->flags & HAMMER2_CHAIN_BMAPPED);
			spin_lock(&above->cst.spin);
			RB_REMOVE(hammer2_chain_tree, &above->dbtree, child);
			atomic_clear_int(&child->flags, HAMMER2_CHAIN_ONDBTREE);
			atomic_clear_int(&child->flags, HAMMER2_CHAIN_BMAPPED);
			TAILQ_INSERT_TAIL(&above->dbq, child, db_entry);
			atomic_set_int(&child->flags, HAMMER2_CHAIN_ONDBQ);
			spin_unlock(&above->cst.spin);
		} else {
			KKASSERT(child->flags & HAMMER2_CHAIN_ONDBQ);
		}
	}

	/*
	 * Adjust child state when updating a live parent.
	 * Adjust child state on the last parent.
	 */
	if (parent->delete_tid > info->sync_tid &&
	    child->delete_tid > trans->sync_tid &&
	    child->modify_tid > parent->update_lo) {
		/*
		 * NOTE: FLUSH_CREATE may have already been cleared and
		 *	 BMAPPED may have already been set due to
		 *	 reentrancy due to the queue movement below.
		 */
		atomic_set_int(&child->flags, HAMMER2_CHAIN_BMAPPED);

		if ((child->flags & HAMMER2_CHAIN_DELETED) &&
		    (child->flags & HAMMER2_CHAIN_ONDBQ)) {
			/*
			 * A deleted child that is on DBQ which has been
			 * inserted must be moved to DBTREE.  However,
			 * temporary deleted children are only applicable to
			 * the current flush and must not have any visibility
			 * beyond it, so temporary children are left on DBQ.
			 *
			 * We run DBQ before DBTREE so this can cause pass4
			 * to be called twice on this child.
			 */
			KKASSERT((child->flags & HAMMER2_CHAIN_ONRBTREE) == 0);
			if ((child->flags & (HAMMER2_CHAIN_ONDBQ |
					     HAMMER2_CHAIN_FLUSH_TEMPORARY)) ==
			    HAMMER2_CHAIN_ONDBQ) {
				spin_lock(&above->cst.spin);

				KKASSERT(child->flags &
					 HAMMER2_CHAIN_FLUSH_CREATE);
				TAILQ_REMOVE(&above->dbq, child, db_entry);
				atomic_clear_int(&child->flags,
						 HAMMER2_CHAIN_ONDBQ);
				xchain = RB_INSERT(hammer2_chain_tree,
						   &above->dbtree, child);
				KASSERT(xchain == NULL, ("pass4: collision with %p moving %p to dbtree", xchain, child));
				atomic_set_int(&child->flags,
					       HAMMER2_CHAIN_ONDBTREE);
				spin_unlock(&above->cst.spin);
			}
		} else if (child->flags & HAMMER2_CHAIN_DELETED) {
			KKASSERT(child->flags & HAMMER2_CHAIN_ONDBTREE);
		} else {
			/*
			 * If the child is not deleted it must be on
			 * the rbtree.
			 */
			KKASSERT(child->flags & HAMMER2_CHAIN_ONRBTREE);
		}
	}
#endif

	/*
	 * Cleanup flags on last parent iterated for flush.
	 */
	if (info->domodify & 2) {
		if (child->flags & HAMMER2_CHAIN_FLUSH_CREATE) {
			atomic_clear_int(&child->flags,
					 HAMMER2_CHAIN_FLUSH_CREATE);
			hammer2_chain_drop(child);
		}
		if ((child->flags & HAMMER2_CHAIN_FLUSH_DELETE) &&
		    child->delete_tid <= trans->sync_tid) {
			KKASSERT((child->flags & HAMMER2_CHAIN_ONDBTREE) == 0);
			KKASSERT((child->flags & HAMMER2_CHAIN_BMAPPED) == 0);
			atomic_clear_int(&child->flags,
					 HAMMER2_CHAIN_FLUSH_DELETE);
			hammer2_chain_drop(child);
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
	return (0);
}

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
