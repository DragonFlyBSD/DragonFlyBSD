/*
 * Copyright (c) 2011-2012 The DragonFly Project.  All rights reserved.
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
	struct flush_deferral_list flush_list;
	int		depth;
	hammer2_tid_t	modify_tid;
};

typedef struct hammer2_flush_info hammer2_flush_info_t;

static void hammer2_chain_flush_pass1(hammer2_mount_t *hmp,
			hammer2_chain_t *chain, hammer2_flush_info_t *info);
static void hammer2_saved_child_cleanup(hammer2_mount_t *hmp,
			hammer2_chain_t *parent, hammer2_chain_t *child);

/*
 * Stand-alone flush.  If the chain is unable to completely flush we have
 * to be sure that SUBMODIFIED propagates up the parent chain.  We must not
 * clear the MOVED bit after flushing in this situation or our desynchronized
 * bref will not properly update in the parent.
 *
 * This routine can be called from several places but the most important
 * is from the hammer2_vop_reclaim() function.  We want to try to completely
 * clean out the inode structure to prevent disconnected inodes from
 * building up and blowing out the kmalloc pool.
 *
 * If modify_tid is 0 (usual case), a new modify_tid is allocated and
 * applied to the flush.  The depth-limit handling code is the only
 * code which passes a non-zero modify_tid to hammer2_chain_flush().
 *
 * chain is locked on call and will remain locked on return.
 */
void
hammer2_chain_flush(hammer2_mount_t *hmp, hammer2_chain_t *chain,
		    hammer2_tid_t modify_tid)
{
	hammer2_chain_t *parent;
	hammer2_chain_t *scan;
	hammer2_blockref_t *base;
	hammer2_flush_info_t info;
	int count;
	int reflush;

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

	if (modify_tid == 0) {
		hammer2_voldata_lock(hmp);
		info.modify_tid = hmp->voldata.alloc_tid++;
		atomic_set_int(&hmp->vchain.flags, HAMMER2_CHAIN_MODIFIED_AUX);
		hammer2_voldata_unlock(hmp);
	} else {
		info.modify_tid = modify_tid;
	}
	reflush = 1;

	while (reflush) {
		/*
		 * Primary recursion
		 */
		hammer2_chain_flush_pass1(hmp, chain, &info);
		reflush = 0;

		while ((scan = TAILQ_FIRST(&info.flush_list)) != NULL) {
			/*
			 * Secondary recursion.  Note that a reference is
			 * retained from the element's presence on the
			 * deferral list.
			 */
			KKASSERT(scan->flags & HAMMER2_CHAIN_DEFERRED);
			TAILQ_REMOVE(&info.flush_list, scan, flush_node);
			atomic_clear_int(&scan->flags, HAMMER2_CHAIN_DEFERRED);

			/*
			 * Now that we've popped back up we can do a secondary
			 * recursion on the deferred elements.
			 */
			if (hammer2_debug & 0x0040)
				kprintf("defered flush %p\n", scan);
			hammer2_chain_lock(hmp, scan, HAMMER2_RESOLVE_MAYBE);
			hammer2_chain_flush(hmp, scan, info.modify_tid);
			hammer2_chain_unlock(hmp, scan);

			/*
			 * Only flag a reflush if SUBMODIFIED is no longer
			 * set.  If SUBMODIFIED is set the element will just
			 * wind up on our flush_list again.
			 */
			if ((scan->flags & (HAMMER2_CHAIN_SUBMODIFIED |
					    HAMMER2_CHAIN_MODIFIED |
					    HAMMER2_CHAIN_MODIFIED_AUX)) == 0) {
				reflush = 1;
			}
			hammer2_chain_drop(hmp, scan);
		}
		if ((hammer2_debug & 0x0040) && reflush)
			kprintf("reflush %p\n", chain);
	}

	/*
	 * The SUBMODIFIED bit must propagate upward if the chain could not
	 * be completely flushed.
	 */
	if (chain->flags & (HAMMER2_CHAIN_SUBMODIFIED |
			    HAMMER2_CHAIN_MODIFIED |
			    HAMMER2_CHAIN_MODIFIED_AUX |
			    HAMMER2_CHAIN_MOVED)) {
		hammer2_chain_parent_setsubmod(hmp, chain);
	}

	/*
	 * If the only thing left is a simple bref update try to
	 * pro-actively update the parent, otherwise return early.
	 */
	parent = chain->parent;
	if (parent == NULL) {
		return;
	}
	if (chain->bref.type != HAMMER2_BREF_TYPE_INODE ||
	    (chain->flags & (HAMMER2_CHAIN_SUBMODIFIED |
			     HAMMER2_CHAIN_MODIFIED |
			     HAMMER2_CHAIN_MODIFIED_AUX |
			     HAMMER2_CHAIN_MOVED)) != HAMMER2_CHAIN_MOVED) {
		return;
	}

	/*
	 * We are locking backwards so allow the lock to fail.
	 */
	if (ccms_thread_lock_nonblock(&parent->cst, CCMS_STATE_EXCLUSIVE))
		return;

	/*
	 * We are updating brefs but we have to call chain_modify()
	 * because our caller is not being run from a recursive flush.
	 *
	 * This will also chain up the parent list and set the SUBMODIFIED
	 * flag.
	 *
	 * We do not want to set HAMMER2_CHAIN_MODIFY_TID here because the
	 * modification is only related to updating a bref in the parent.
	 *
	 * When updating the blockset embedded in the volume header we must
	 * also update voldata.mirror_tid.
	 */
	hammer2_chain_lock(hmp, parent, HAMMER2_RESOLVE_MAYBE);
	hammer2_chain_modify(hmp, parent, HAMMER2_MODIFY_NO_MODIFY_TID);

	switch(parent->bref.type) {
	case HAMMER2_BREF_TYPE_INODE:
		base = &parent->data->ipdata.u.blockset.
			blockref[0];
		count = HAMMER2_SET_COUNT;
		break;
	case HAMMER2_BREF_TYPE_INDIRECT:
		base = &parent->data->npdata.blockref[0];
		count = parent->bytes /
			sizeof(hammer2_blockref_t);
		break;
	case HAMMER2_BREF_TYPE_VOLUME:
		base = &hmp->voldata.sroot_blockset.blockref[0];
		count = HAMMER2_SET_COUNT;
		if (chain->flags & HAMMER2_CHAIN_MOVED) {
			if (hmp->voldata.mirror_tid < chain->bref.mirror_tid) {
				hmp->voldata.mirror_tid =
					chain->bref.mirror_tid;
			}
		}
		break;
	default:
		base = NULL;
		panic("hammer2_chain_flush: "
		      "unrecognized blockref type: %d",
		      parent->bref.type);
	}

	/*
	 * Update the blockref in the parent.  We do not have to set
	 * MOVED in the parent because the parent has been marked modified,
	 * so the flush sequence will pick up the bref change.
	 *
	 * We do have to propagate mirror_tid upward.
	 */
	KKASSERT(chain->index >= 0 &&
		 chain->index < count);
	KKASSERT(chain->parent == parent);
	if (chain->flags & HAMMER2_CHAIN_MOVED) {
		base[chain->index] = chain->bref_flush;
		if (parent->bref.mirror_tid < chain->bref_flush.mirror_tid)
			parent->bref.mirror_tid = chain->bref_flush.mirror_tid;
		atomic_clear_int(&chain->flags, HAMMER2_CHAIN_MOVED);
		hammer2_chain_drop(hmp, chain);
	} else if (bcmp(&base[chain->index], &chain->bref_flush,
		   sizeof(chain->bref)) != 0) {
		panic("hammer2: unflagged bref update(2)");
	}
	ccms_thread_unlock(&parent->cst);		/* release manual op */
	hammer2_chain_unlock(hmp, parent);
}

/*
 * chain is locked by the caller and remains locked on return.
 */
static void
hammer2_chain_flush_pass1(hammer2_mount_t *hmp, hammer2_chain_t *chain,
			  hammer2_flush_info_t *info)
{
	hammer2_blockref_t *bref;
	hammer2_off_t pbase;
	size_t bbytes;
	size_t boff;
	char *bdata;
	struct buf *bp;
	int error;
	int wasmodified;

	/*
	 * If we hit the stack recursion depth limit defer the operation.
	 * The controller of the info structure will execute the deferral
	 * list and then retry.
	 *
	 * This is only applicable if SUBMODIFIED is set.  After a reflush
	 * SUBMODIFIED will probably be cleared and we want to drop through
	 * to finish processing the current element so our direct parent
	 * can process the results.
	 */
	if (info->depth == HAMMER2_FLUSH_DEPTH_LIMIT &&
	    (chain->flags & HAMMER2_CHAIN_SUBMODIFIED)) {
		if ((chain->flags & HAMMER2_CHAIN_DEFERRED) == 0) {
			hammer2_chain_ref(hmp, chain);
			TAILQ_INSERT_TAIL(&info->flush_list,
					  chain, flush_node);
			atomic_set_int(&chain->flags, HAMMER2_CHAIN_DEFERRED);
		}
		return;
	}

	if (hammer2_debug & 0x0008)
		kprintf("%*.*sCHAIN type=%d@%08jx %p/%d %04x {\n",
			info->depth, info->depth, "",
			chain->bref.type, chain->bref.data_off,
			chain, chain->refs, chain->flags);

	/*
	 * If SUBMODIFIED is set we recurse the flush and adjust the
	 * blockrefs accordingly.
	 *
	 * NOTE: Looping on SUBMODIFIED can prevent a flush from ever
	 *	 finishing in the face of filesystem activity.
	 */
	if (chain->flags & HAMMER2_CHAIN_SUBMODIFIED) {
		hammer2_chain_t *child;
		hammer2_chain_t *saved;
		hammer2_blockref_t *base;
		int count;

		/*
		 * Clear SUBMODIFIED to catch races.  Note that if any
		 * child has to be flushed SUBMODIFIED will wind up being
		 * set again (for next time), but this does not stop us from
		 * synchronizing block updates which occurred.
		 *
		 * We don't want to set our chain to MODIFIED gratuitously.
		 *
		 * We need an extra ref on chain because we are going to
		 * release its lock temporarily in our child loop.
		 */
		/* XXX SUBMODIFIED not interlocked, can race */
		atomic_clear_int(&chain->flags, HAMMER2_CHAIN_SUBMODIFIED);
		hammer2_chain_ref(hmp, chain);

		/*
		 * Flush the children and update the blockrefs in the chain.
		 * Be careful of ripouts during the loop.
		 *
		 * The flushing counter prevents ripouts on lastdrop and
		 * also prevents moves (causes renames to sleep/retry).
		 * Be very careful with it.
		 */
		RB_FOREACH(child, hammer2_chain_tree, &chain->rbhead) {
			KASSERT(child->parent == chain,
				("hammer2_flush: child->parent mismatch %p/%p",
				 child->parent, chain));

			/*
			 * We only recurse if SUBMODIFIED (internal node)
			 * or MODIFIED (internal node or leaf) is set.
			 * However, we must still track whether any MOVED
			 * entries are present to determine if the chain's
			 * blockref's need updating or not.
			 */
			if ((child->flags & (HAMMER2_CHAIN_SUBMODIFIED |
					     HAMMER2_CHAIN_MODIFIED |
					    HAMMER2_CHAIN_MODIFIED_AUX)) == 0) {
				continue;
			}

			/*
			 * flushing can only be adjusted while its parent
			 * is locked, and prevent the destruction/removal
			 * of the child from the parent's B-Tree.  This allows
			 * us to temporarily unlock the parent.
			 *
			 * To unwind, we must hold the parent locked before
			 * decrementing flushing to prevent child corruption
			 * during our loop.
			 */
			atomic_add_int(&child->flushing, 1);
			hammer2_chain_unlock(hmp, chain);
			hammer2_chain_lock(hmp, child, HAMMER2_RESOLVE_MAYBE);
			KASSERT(child->parent == chain,
				("hammer2_flush: child->parent mismatch %p/%p",
				 child->parent, chain));
			if ((child->flags & (HAMMER2_CHAIN_SUBMODIFIED |
					     HAMMER2_CHAIN_MODIFIED |
					    HAMMER2_CHAIN_MODIFIED_AUX)) == 0) {
				hammer2_chain_unlock(hmp, child);
				hammer2_chain_lock(hmp, chain,
						   HAMMER2_RESOLVE_ALWAYS);
				KKASSERT(child->parent == chain);
				atomic_add_int(&child->flushing, -1);
				continue;
			}

			/*
			 * Propagate the DESTROYED flag if found set, then
			 * recurse the flush.
			 */
			if ((chain->flags & HAMMER2_CHAIN_DESTROYED) &&
			    (child->flags & HAMMER2_CHAIN_DESTROYED) == 0) {
				atomic_set_int(&child->flags,
					       HAMMER2_CHAIN_DESTROYED |
					       HAMMER2_CHAIN_SUBMODIFIED);
			}
			++info->depth;
			hammer2_chain_flush_pass1(hmp, child, info);
			--info->depth;
			hammer2_chain_unlock(hmp, child);

			/*
			 * Always resolve when relocking the parent.
			 */
			hammer2_chain_lock(hmp, chain, HAMMER2_RESOLVE_ALWAYS);
			KASSERT(child->parent == chain,
				("hammer2_flush: child->parent mismatch %p/%p",
				 child->parent, chain));
			atomic_add_int(&child->flushing, -1);
		}

		/*
		 * Now synchronize any block updates and handle any
		 * chains marked DELETED.
		 *
		 * The flushing counter prevents ripouts on lastdrop and
		 * also prevents moves (causes renames to sleep/retry).
		 * Be very careful with it.
		 */
		saved = NULL;
		RB_FOREACH(child, hammer2_chain_tree, &chain->rbhead) {
			if ((child->flags & (HAMMER2_CHAIN_MOVED |
					     HAMMER2_CHAIN_DELETED)) == 0) {
				continue;
			}
			atomic_add_int(&child->flushing, 1);
			if (saved) {
				hammer2_saved_child_cleanup(hmp, chain, saved);
				saved = NULL;
			}
			saved = child;
			hammer2_chain_lock(hmp, child, HAMMER2_RESOLVE_NEVER);
			KKASSERT(child->parent == chain);
			if ((child->flags & (HAMMER2_CHAIN_MOVED |
					     HAMMER2_CHAIN_DELETED)) == 0) {
				hammer2_chain_unlock(hmp, child);
				continue;
			}
			if (child->flags & HAMMER2_CHAIN_MOVED) {
				hammer2_chain_modify(hmp, chain,
					     HAMMER2_MODIFY_NO_MODIFY_TID);
			}

			switch(chain->bref.type) {
			case HAMMER2_BREF_TYPE_INODE:
				KKASSERT((chain->data->ipdata.op_flags &
					  HAMMER2_OPFLAG_DIRECTDATA) == 0);
				base = &chain->data->ipdata.u.blockset.
					blockref[0];
				count = HAMMER2_SET_COUNT;
				break;
			case HAMMER2_BREF_TYPE_INDIRECT:
				if (chain->data) {
					base = &chain->data->npdata.blockref[0];
				} else {
					base = NULL;
					KKASSERT(child->flags &
						 HAMMER2_CHAIN_DELETED);
				}
				count = chain->bytes /
					sizeof(hammer2_blockref_t);
				break;
			case HAMMER2_BREF_TYPE_VOLUME:
				base = &hmp->voldata.sroot_blockset.blockref[0];
				count = HAMMER2_SET_COUNT;
				break;
			default:
				base = NULL;
				panic("hammer2_chain_get: "
				      "unrecognized blockref type: %d",
				      chain->bref.type);
			}

			KKASSERT(child->index >= 0);

			if (chain->bref.mirror_tid <
			    child->bref_flush.mirror_tid) {
				chain->bref.mirror_tid =
					child->bref_flush.mirror_tid;
			}
			if (chain->bref.type == HAMMER2_BREF_TYPE_VOLUME &&
			    hmp->voldata.mirror_tid <
			    child->bref_flush.mirror_tid) {
				hmp->voldata.mirror_tid =
					child->bref_flush.mirror_tid;
			}
			if (child->flags & HAMMER2_CHAIN_DELETED) {
				bzero(&child->bref_flush,
				      sizeof(child->bref_flush));
			}
			if (base)
				base[child->index] = child->bref_flush;
			if (child->flags & HAMMER2_CHAIN_MOVED) {
				atomic_clear_int(&child->flags,
						 HAMMER2_CHAIN_MOVED);
				hammer2_chain_drop(hmp, child); /* flag */
			}
			hammer2_chain_unlock(hmp, child);
		}
		if (saved) {
			hammer2_saved_child_cleanup(hmp, chain, saved);
			saved = NULL;
		}
		hammer2_chain_drop(hmp, chain);
	}

	/*
	 * If destroying the object we unconditonally clear the MODIFIED
	 * and MOVED bits, and we destroy the buffer without writing it
	 * out.
	 *
	 * We don't bother updating the hash/crc or the chain bref.
	 *
	 * NOTE: The destroy'd object's bref has already been updated.
	 *	 so we can clear MOVED without propagating mirror_tid
	 *	 or modify_tid upward.
	 *
	 * XXX allocations for unflushed data can be returned to the
	 *     free pool.
	 */
	if (chain->flags & HAMMER2_CHAIN_DESTROYED) {
		if (chain->flags & HAMMER2_CHAIN_MODIFIED) {
			if (chain->bp) {
				chain->bp->b_flags |= B_INVAL|B_RELBUF;
			}
			atomic_clear_int(&chain->flags,
					 HAMMER2_CHAIN_MODIFIED |
					 HAMMER2_CHAIN_MODIFY_TID);
			hammer2_chain_drop(hmp, chain);
		}
		if (chain->flags & HAMMER2_CHAIN_MODIFIED_AUX) {
			atomic_clear_int(&chain->flags,
					 HAMMER2_CHAIN_MODIFIED_AUX);
		}
		if (chain->flags & HAMMER2_CHAIN_MOVED) {
			atomic_clear_int(&chain->flags,
					 HAMMER2_CHAIN_MOVED);
			hammer2_chain_drop(hmp, chain);
		}
		return;
	}

	/*
	 * Flush this chain entry only if it is marked modified.
	 */
	if ((chain->flags & (HAMMER2_CHAIN_MODIFIED |
			     HAMMER2_CHAIN_MODIFIED_AUX)) == 0) {
		goto done;
	}

#if 0
	/*
	 * Synchronize cumulative data and inode count adjustments to
	 * the inode and propagate the deltas upward to the parent.
	 *
	 * XXX removed atm
	 */
	if (chain->bref.type == HAMMER2_BREF_TYPE_INODE) {
		hammer2_inode_t *ip;

		ip = chain->u.ip;
		ip->ip_data.inode_count += ip->delta_icount;
		ip->ip_data.data_count += ip->delta_dcount;
		if (ip->pip) {
			ip->pip->delta_icount += ip->delta_icount;
			ip->pip->delta_dcount += ip->delta_dcount;
		}
		ip->delta_icount = 0;
		ip->delta_dcount = 0;
	}
#endif

	/*
	 * Flush if MODIFIED or MODIFIED_AUX is set.  MODIFIED_AUX is only
	 * used by the volume header (&hmp->vchain).
	 */
	if ((chain->flags & (HAMMER2_CHAIN_MODIFIED |
			     HAMMER2_CHAIN_MODIFIED_AUX)) == 0) {
		goto done;
	}
	atomic_clear_int(&chain->flags, HAMMER2_CHAIN_MODIFIED_AUX);

	/*
	 * Clear MODIFIED and set HAMMER2_CHAIN_MOVED.  The caller
	 * will re-test the MOVED bit.  We must also update the mirror_tid
	 * and modify_tid fields as appropriate.
	 *
	 * bits own a single chain ref and the MOVED bit owns its own
	 * chain ref.
	 */
	chain->bref.mirror_tid = info->modify_tid;
	if (chain->flags & HAMMER2_CHAIN_MODIFY_TID)
		chain->bref.modify_tid = info->modify_tid;
	wasmodified = (chain->flags & HAMMER2_CHAIN_MODIFIED) != 0;
	atomic_clear_int(&chain->flags, HAMMER2_CHAIN_MODIFIED |
					HAMMER2_CHAIN_MODIFY_TID);

	if (chain->flags & HAMMER2_CHAIN_MOVED) {
		/*
		 * Drop the ref from the MODIFIED bit we cleared.
		 */
		if (wasmodified)
			hammer2_chain_drop(hmp, chain);
	} else {
		/*
		 * If we were MODIFIED we inherit the ref from clearing
		 * that bit, otherwise we need another ref.
		 */
		if (wasmodified == 0)
			hammer2_chain_ref(hmp, chain);
		atomic_set_int(&chain->flags, HAMMER2_CHAIN_MOVED);
	}
	chain->bref_flush = chain->bref;

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
				kprintf("x");
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
		hammer2_chain_lock(hmp, chain, HAMMER2_RESOLVE_ALWAYS);
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
		hammer2_chain_unlock(hmp, chain);
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
done:
	if (hammer2_debug & 0x0008) {
		kprintf("%*.*s} %p/%d %04x ",
			info->depth, info->depth, "",
			chain, chain->refs, chain->flags);
	}
}

#if 0
/*
 * PASS2 - not yet implemented (should be called only with the root chain?)
 */
static void
hammer2_chain_flush_pass2(hammer2_mount_t *hmp, hammer2_chain_t *chain)
{
}
#endif

static
void
hammer2_saved_child_cleanup(hammer2_mount_t *hmp,
			    hammer2_chain_t *parent, hammer2_chain_t *child)
{
	atomic_add_int(&child->flushing, -1);
	if (child->flushing == 0 && (child->flags & HAMMER2_CHAIN_DELETED)) {
		kprintf("hammer2: fixup deferred deleted child\n");
		hammer2_chain_lock(hmp, child, HAMMER2_RESOLVE_MAYBE);
		hammer2_chain_delete(hmp, parent, child, 0);
		hammer2_chain_unlock(hmp, child);
	}
}
