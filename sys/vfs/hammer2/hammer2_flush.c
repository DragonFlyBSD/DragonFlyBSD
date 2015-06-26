/*
 * Copyright (c) 2011-2015 The DragonFly Project.  All rights reserved.
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

#define HAMMER2_FLUSH_DEPTH_LIMIT       10      /* stack recursion limit */


/*
 * Recursively flush the specified chain.  The chain is locked and
 * referenced by the caller and will remain so on return.  The chain
 * will remain referenced throughout but can temporarily lose its
 * lock during the recursion to avoid unnecessarily stalling user
 * processes.
 */
struct hammer2_flush_info {
	hammer2_chain_t *parent;
	int		depth;
	int		diddeferral;
	int		cache_index;
	struct h2_flush_list flushq;
	hammer2_chain_t	*debug;
};

typedef struct hammer2_flush_info hammer2_flush_info_t;

static void hammer2_flush_core(hammer2_flush_info_t *info,
				hammer2_chain_t *chain, int deleting);
static int hammer2_flush_recurse(hammer2_chain_t *child, void *data);

/*
 * Any per-pfs transaction initialization goes here.
 */
void
hammer2_trans_manage_init(hammer2_pfs_t *pmp)
{
}

/*
 * Transaction support for any modifying operation.  Transactions are used
 * in the pmp layer by the frontend and in the spmp layer by the backend.
 *
 * 0			- Normal transaction, interlocked against flush
 *			  transaction.
 *
 * TRANS_ISFLUSH	- Flush transaction, interlocked against normal
 *			  transaction.
 *
 * TRANS_BUFCACHE	- Buffer cache transaction, no interlock.
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
hammer2_trans_init(hammer2_pfs_t *pmp, uint32_t flags)
{
	uint32_t oflags;
	uint32_t nflags;
	int dowait;

	for (;;) {
		oflags = pmp->trans.flags;
		cpu_ccfence();
		dowait = 0;

		if (flags & HAMMER2_TRANS_ISFLUSH) {
			/*
			 * Requesting flush transaction.  Wait for all
			 * currently running transactions to finish.
			 */
			if (oflags & HAMMER2_TRANS_MASK) {
				nflags = oflags | HAMMER2_TRANS_FPENDING |
						  HAMMER2_TRANS_WAITING;
				dowait = 1;
			} else {
				nflags = (oflags | flags) + 1;
			}
			++pmp->modify_tid;
		} else if (flags & HAMMER2_TRANS_BUFCACHE) {
			/*
			 * Requesting strategy transaction.  Generally
			 * allowed in all situations unless a flush
			 * is running without the preflush flag.
			 */
			if ((oflags & (HAMMER2_TRANS_ISFLUSH |
				       HAMMER2_TRANS_PREFLUSH)) ==
			    HAMMER2_TRANS_ISFLUSH) {
				nflags = oflags | HAMMER2_TRANS_WAITING;
				dowait = 1;
			} else {
				nflags = (oflags | flags) + 1;
			}
		} else {
			/*
			 * Requesting normal transaction.  Wait for any
			 * flush to finish before allowing.
			 */
			if (oflags & HAMMER2_TRANS_ISFLUSH) {
				nflags = oflags | HAMMER2_TRANS_WAITING;
				dowait = 1;
			} else {
				nflags = (oflags | flags) + 1;
			}
		}
		if (dowait)
			tsleep_interlock(&pmp->trans.sync_wait, 0);
		if (atomic_cmpset_int(&pmp->trans.flags, oflags, nflags)) {
			if (dowait == 0)
				break;
			tsleep(&pmp->trans.sync_wait, PINTERLOCKED,
			       "h2trans", hz);
		} else {
			cpu_pause();
		}
		/* retry */
	}
}

void
hammer2_trans_done(hammer2_pfs_t *pmp)
{
	uint32_t oflags;
	uint32_t nflags;

	for (;;) {
		oflags = pmp->trans.flags;
		cpu_ccfence();
		KKASSERT(oflags & HAMMER2_TRANS_MASK);
		if ((oflags & HAMMER2_TRANS_MASK) == 1) {
			/*
			 * This was the last transaction
			 */
			nflags = (oflags - 1) & ~(HAMMER2_TRANS_ISFLUSH |
						  HAMMER2_TRANS_BUFCACHE |
						  HAMMER2_TRANS_PREFLUSH |
						  HAMMER2_TRANS_FPENDING |
						  HAMMER2_TRANS_WAITING);
		} else {
			/*
			 * Still transactions pending
			 */
			nflags = oflags - 1;
		}
		if (atomic_cmpset_int(&pmp->trans.flags, oflags, nflags)) {
			if ((nflags & HAMMER2_TRANS_MASK) == 0 &&
			    (oflags & HAMMER2_TRANS_WAITING)) {
				wakeup(&pmp->trans.sync_wait);
			}
			break;
		} else {
			cpu_pause();
		}
		/* retry */
	}
}

/*
 * Obtain new, unique inode number (not serialized by caller).
 */
hammer2_tid_t
hammer2_trans_newinum(hammer2_pfs_t *pmp)
{
	hammer2_tid_t tid;

	KKASSERT(sizeof(long) == 8);
	tid = atomic_fetchadd_long(&pmp->inode_tid, 1);

	return tid;
}

/*
 * Assert that a strategy call is ok here.  Strategy calls are legal
 *
 * (1) In a normal transaction.
 * (2) In a flush transaction only if PREFLUSH is also set.
 */
void
hammer2_trans_assert_strategy(hammer2_pfs_t *pmp)
{
	KKASSERT((pmp->trans.flags & HAMMER2_TRANS_ISFLUSH) == 0 ||
		 (pmp->trans.flags & HAMMER2_TRANS_PREFLUSH));
}


/*
 * Chains undergoing destruction are removed from the in-memory topology.
 * To avoid getting lost these chains are placed on the delayed flush
 * queue which will properly dispose of them.
 *
 * We do this instead of issuing an immediate flush in order to give
 * recursive deletions (rm -rf, etc) a chance to remove more of the
 * hierarchy, potentially allowing an enormous amount of write I/O to
 * be avoided.
 */
void
hammer2_delayed_flush(hammer2_chain_t *chain)
{
	if ((chain->flags & HAMMER2_CHAIN_DELAYED) == 0) {
		hammer2_spin_ex(&chain->hmp->list_spin);
		if ((chain->flags & (HAMMER2_CHAIN_DELAYED |
				     HAMMER2_CHAIN_DEFERRED)) == 0) {
			atomic_set_int(&chain->flags, HAMMER2_CHAIN_DELAYED |
						      HAMMER2_CHAIN_DEFERRED);
			TAILQ_INSERT_TAIL(&chain->hmp->flushq,
					  chain, flush_node);
			hammer2_chain_ref(chain);
		}
		hammer2_spin_unex(&chain->hmp->list_spin);
	}
}

/*
 * Flush the chain and all modified sub-chains through the specified
 * synchronization point, propagating parent chain modifications, modify_tid,
 * and mirror_tid updates back up as needed.
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
hammer2_flush(hammer2_chain_t *chain, int istop)
{
	hammer2_chain_t *scan;
	hammer2_flush_info_t info;
	hammer2_dev_t *hmp;
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
	hmp = chain->hmp;
	loops = 0;

	for (;;) {
		/*
		 * Move hmp->flushq to info.flushq if non-empty so it can
		 * be processed.
		 */
		if (TAILQ_FIRST(&hmp->flushq) != NULL) {
			hammer2_spin_ex(&chain->hmp->list_spin);
			TAILQ_CONCAT(&info.flushq, &hmp->flushq, flush_node);
			hammer2_spin_unex(&chain->hmp->list_spin);
		}

		/*
		 * Unwind deep recursions which had been deferred.  This
		 * can leave the FLUSH_* bits set for these chains, which
		 * will be handled when we [re]flush chain after the unwind.
		 */
		while ((scan = TAILQ_FIRST(&info.flushq)) != NULL) {
			KKASSERT(scan->flags & HAMMER2_CHAIN_DEFERRED);
			TAILQ_REMOVE(&info.flushq, scan, flush_node);
			atomic_clear_int(&scan->flags, HAMMER2_CHAIN_DEFERRED |
						       HAMMER2_CHAIN_DELAYED);

			/*
			 * Now that we've popped back up we can do a secondary
			 * recursion on the deferred elements.
			 *
			 * NOTE: hammer2_flush() may replace scan.
			 */
			if (hammer2_debug & 0x0040)
				kprintf("deferred flush %p\n", scan);
			hammer2_chain_lock(scan, HAMMER2_RESOLVE_MAYBE);
			hammer2_flush(scan, 0);
			hammer2_chain_unlock(scan);
			hammer2_chain_drop(scan);	/* ref from deferral */
		}

		/*
		 * [re]flush chain.
		 */
		info.diddeferral = 0;
		hammer2_flush_core(&info, chain, istop);

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
 * blockref.modify_tid is consistent only within a PFS, and will not be
 * consistent during synchronization.  mirror_tid is consistent across the
 * block device regardless of the PFS.
 */
static void
hammer2_flush_core(hammer2_flush_info_t *info, hammer2_chain_t *chain,
		   int istop)
{
	hammer2_chain_t *parent;
	hammer2_dev_t *hmp;
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
	diddeferral = info->diddeferral;
	parent = info->parent;		/* can be NULL */

	/*
	 * Downward search recursion
	 */
	if (chain->flags & (HAMMER2_CHAIN_DEFERRED | HAMMER2_CHAIN_DELAYED)) {
		/*
		 * Already deferred.
		 */
		++info->diddeferral;
	} else if (info->depth == HAMMER2_FLUSH_DEPTH_LIMIT) {
		/*
		 * Recursion depth reached.
		 */
		KKASSERT((chain->flags & HAMMER2_CHAIN_DELAYED) == 0);
		hammer2_chain_ref(chain);
		TAILQ_INSERT_TAIL(&info->flushq, chain, flush_node);
		atomic_set_int(&chain->flags, HAMMER2_CHAIN_DEFERRED);
		++info->diddeferral;
	} else if ((chain->flags & HAMMER2_CHAIN_PFSBOUNDARY) && istop == 0) {
		/*
		 * We do not recurse through PFSROOTs.  PFSROOT flushes are
		 * handled by the related pmp's (whether mounted or not,
		 * including during recovery).
		 *
		 * But we must still process the PFSROOT chains for block
		 * table updates in their parent (which IS part of our flush).
		 *
		 * Note that the volume root, vchain, does not set this flag.
		 */
		;
	} else if (chain->flags & HAMMER2_CHAIN_ONFLUSH) {
		/*
		 * Downward recursion search (actual flush occurs bottom-up).
		 * pre-clear ONFLUSH.  It can get set again due to races,
		 * which we want so the scan finds us again in the next flush.
		 * These races can also include 
		 *
		 * Flush recursions stop at PFSROOT boundaries.  Each PFS
		 * must be individually flushed and then the root must
		 * be flushed.
		 */
		atomic_clear_int(&chain->flags, HAMMER2_CHAIN_ONFLUSH);
		info->parent = chain;
		hammer2_spin_ex(&chain->core.spin);
		RB_SCAN(hammer2_chain_tree, &chain->core.rbtree,
			NULL, hammer2_flush_recurse, info);
		hammer2_spin_unex(&chain->core.spin);
		info->parent = parent;
		if (info->diddeferral)
			hammer2_chain_setflush(chain);
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
		 * Dispose of the modified bit.
		 *
		 * UPDATE should already be set.
		 * bref.mirror_tid should already be set.
		 */
		KKASSERT((chain->flags & HAMMER2_CHAIN_UPDATE) ||
			 chain == &hmp->vchain);
		atomic_clear_int(&chain->flags, HAMMER2_CHAIN_MODIFIED);

		/*
		 * Manage threads waiting for excessive dirty memory to
		 * be retired.
		 */
		if (chain->pmp)
			hammer2_pfs_memory_wakeup(chain->pmp);

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
		 * Issue the flush.  This is indirect via the DIO.
		 *
		 * NOTE: A DELETED node that reaches this point must be
		 *	 flushed for synchronization point consistency.
		 *
		 * NOTE: Even though MODIFIED was already set, the related DIO
		 *	 might not be dirty due to a system buffer cache
		 *	 flush and must be set dirty if we are going to make
		 *	 further modifications to the buffer.  Chains with
		 *	 embedded data don't need this.
		 */
		if (hammer2_debug & 0x1000) {
			kprintf("Flush %p.%d %016jx/%d data=%016jx",
				chain, chain->bref.type,
				(uintmax_t)chain->bref.key,
				chain->bref.keybits,
				(uintmax_t)chain->bref.data_off);
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
			/*
			 * Update the volume header's freemap_tid to the
			 * freemap's flushing mirror_tid.
			 *
			 * (note: embedded data, do not call setdirty)
			 */
			KKASSERT(hmp->vchain.flags & HAMMER2_CHAIN_MODIFIED);
			KKASSERT(chain == &hmp->fchain);
			hmp->voldata.freemap_tid = chain->bref.mirror_tid;
			kprintf("sync freemap mirror_tid %08jx\n",
				(intmax_t)chain->bref.mirror_tid);

			/*
			 * The freemap can be flushed independently of the
			 * main topology, but for the case where it is
			 * flushed in the same transaction, and flushed
			 * before vchain (a case we want to allow for
			 * performance reasons), make sure modifications
			 * made during the flush under vchain use a new
			 * transaction id.
			 *
			 * Otherwise the mount recovery code will get confused.
			 */
			++hmp->voldata.mirror_tid;
			break;
		case HAMMER2_BREF_TYPE_VOLUME:
			/*
			 * The free block table is flushed by
			 * hammer2_vfs_sync() before it flushes vchain.
			 * We must still hold fchain locked while copying
			 * voldata to volsync, however.
			 *
			 * (note: embedded data, do not call setdirty)
			 */
			hammer2_chain_lock(&hmp->fchain,
					   HAMMER2_RESOLVE_ALWAYS);
			hammer2_voldata_lock(hmp);
			kprintf("sync volume  mirror_tid %08jx\n",
				(intmax_t)chain->bref.mirror_tid);

			/*
			 * Update the volume header's mirror_tid to the
			 * main topology's flushing mirror_tid.  It is
			 * possible that voldata.mirror_tid is already
			 * beyond bref.mirror_tid due to the bump we made
			 * above in BREF_TYPE_FREEMAP.
			 */
			if (hmp->voldata.mirror_tid < chain->bref.mirror_tid) {
				hmp->voldata.mirror_tid =
					chain->bref.mirror_tid;
			}

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

			kprintf("syncvolhdr %016jx %016jx\n",
				hmp->voldata.mirror_tid,
				hmp->vchain.bref.mirror_tid);
			hmp->volsync = hmp->voldata;
			atomic_set_int(&chain->flags, HAMMER2_CHAIN_VOLUMESYNC);
			hammer2_voldata_unlock(hmp);
			hammer2_chain_unlock(&hmp->fchain);
			break;
		case HAMMER2_BREF_TYPE_DATA:
			/*
			 * Data elements have already been flushed via the
			 * logical file buffer cache.  Their hash was set in
			 * the bref by the vop_write code.  Do not re-dirty.
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
			hammer2_chain_setcheck(chain, chain->data);
			break;
		case HAMMER2_BREF_TYPE_INODE:
			/*
			 * NOTE: We must call io_setdirty() to make any late
			 *	 changes to the inode data, the system might
			 *	 have already flushed the buffer.
			 */
			if (chain->data->ipdata.meta.op_flags &
			    HAMMER2_OPFLAG_PFSROOT) {
				/*
				 * non-NULL pmp if mounted as a PFS.  We must
				 * sync fields cached in the pmp? XXX
				 */
				hammer2_inode_data_t *ipdata;

				hammer2_io_setdirty(chain->dio);
				ipdata = &chain->data->ipdata;
				if (chain->pmp) {
					ipdata->meta.pfs_inum =
						chain->pmp->inode_tid;
				}
			} else {
				/* can't be mounted as a PFS */
			}

			KKASSERT((chain->flags & HAMMER2_CHAIN_EMBEDDED) == 0);
			hammer2_chain_setcheck(chain, chain->data);
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
			kprintf("PARENT MISMATCH ch=%p p=%p/%p\n",
				chain, chain->parent, parent);
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
		 * Clear UPDATE flag, mark parent modified, update its
		 * modify_tid if necessary, and adjust the parent blockmap.
		 */
		if (chain->flags & HAMMER2_CHAIN_UPDATE) {
			atomic_clear_int(&chain->flags, HAMMER2_CHAIN_UPDATE);
			hammer2_chain_drop(chain);
		}

		/*
		 * (optional code)
		 *
		 * Avoid actually modifying and updating the parent if it
		 * was flagged for destruction.  This can greatly reduce
		 * disk I/O in large tree removals because the
		 * hammer2_io_setinval() call in the upward recursion
		 * (see MODIFIED code above) can only handle a few cases.
		 */
		if (parent->flags & HAMMER2_CHAIN_DESTROY) {
			if (parent->bref.modify_tid < chain->bref.modify_tid) {
				parent->bref.modify_tid =
					chain->bref.modify_tid;
			}
			atomic_clear_int(&chain->flags, HAMMER2_CHAIN_BMAPPED |
							HAMMER2_CHAIN_BMAPUPD);
			hammer2_chain_unlock(parent);
			goto skipupdate;
		}

		/*
		 * We are updating the parent's blockmap, the parent must
		 * be set modified.
		 */
		hammer2_chain_modify(parent, HAMMER2_MODIFY_KEEPMODIFY);
		if (parent->bref.modify_tid < chain->bref.modify_tid)
			parent->bref.modify_tid = chain->bref.modify_tid;

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
			    (parent->data->ipdata.meta.op_flags &
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
				hammer2_spin_ex(&parent->core.spin);
				hammer2_base_delete(parent, base, count,
						    &info->cache_index, chain);
				hammer2_spin_unex(&parent->core.spin);
				/* base_delete clears both bits */
			} else {
				atomic_clear_int(&chain->flags,
						 HAMMER2_CHAIN_BMAPUPD);
			}
		}
		if (base && (chain->flags & HAMMER2_CHAIN_BMAPPED) == 0) {
			hammer2_spin_ex(&parent->core.spin);
			hammer2_base_insert(parent, base, count,
					    &info->cache_index, chain);
			hammer2_spin_unex(&parent->core.spin);
			/* base_insert sets BMAPPED */
		}
		hammer2_chain_unlock(parent);
	}
skipupdate:
	;

	/*
	 * Final cleanup after flush
	 */
done:
	KKASSERT(chain->refs > 0);
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
 */
static int
hammer2_flush_recurse(hammer2_chain_t *child, void *data)
{
	hammer2_flush_info_t *info = data;
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
	hammer2_spin_unex(&parent->core.spin);

	hammer2_chain_unlock(parent);
	hammer2_chain_lock(child, HAMMER2_RESOLVE_MAYBE);

	/*
	 * Recurse and collect deferral data.  We're in the media flush,
	 * this can cross PFS boundaries.
	 */
	if (child->flags & HAMMER2_CHAIN_FLUSH_MASK) {
		++info->depth;
		hammer2_flush_core(info, child, 0);
		--info->depth;
	} else if (hammer2_debug & 0x200) {
		if (info->debug == NULL)
			info->debug = child;
		++info->depth;
		hammer2_flush_core(info, child, 0);
		--info->depth;
		if (info->debug == child)
			info->debug = NULL;
	}

	/*
	 * Relock to continue the loop
	 */
	hammer2_chain_unlock(child);
	hammer2_chain_lock(parent, HAMMER2_RESOLVE_MAYBE);
	hammer2_chain_drop(child);
	KKASSERT(info->parent == parent);
	hammer2_spin_ex(&parent->core.spin);

	return (0);
}
