/*
 * Copyright (c) 2008 The DragonFly Project.  All rights reserved.
 * 
 * This code is derived from software contributed to The DragonFly Project
 * by Matthew Dillon <dillon@backplane.com>
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
 * 
 * $DragonFly: src/sys/vfs/hammer/hammer_prune.c,v 1.2 2008/05/18 01:48:50 dillon Exp $
 */

#include "hammer.h"

/*
 * Iterate through the specified range of object ids and remove any
 * deleted records that fall entirely within a prune modulo.
 *
 * A reverse iteration is used to prevent overlapping records from being
 * created during the iteration due to alignments.  This also allows us
 * to adjust alignments without blowing up the B-Tree.
 */
static int check_prune(struct hammer_ioc_prune *prune, hammer_btree_elm_t elm,
			int *realign_cre, int *realign_del);
static int realign_prune(struct hammer_ioc_prune *prune, hammer_cursor_t cursor,
			int realign_cre, int realign_del);

int
hammer_ioc_prune(hammer_transaction_t trans, hammer_inode_t ip,
		 struct hammer_ioc_prune *prune)
{
	struct hammer_cursor cursor;
	hammer_btree_elm_t elm;
	int error;
	int isdir;
	int realign_cre;
	int realign_del;

	if (prune->nelms < 0 || prune->nelms > HAMMER_MAX_PRUNE_ELMS)
		return(EINVAL);
	if (prune->beg_obj_id >= prune->end_obj_id)
		return(EINVAL);
	if ((prune->head.flags & HAMMER_IOC_PRUNE_ALL) && prune->nelms)
		return(EINVAL);

	prune->cur_localization = prune->end_localization;
	prune->cur_obj_id = prune->end_obj_id;
	prune->cur_key = HAMMER_MAX_KEY;

retry:
	error = hammer_init_cursor(trans, &cursor, NULL, NULL);
	if (error) {
		hammer_done_cursor(&cursor);
		return(error);
	}
	cursor.key_beg.localization = prune->beg_localization;
	cursor.key_beg.obj_id = prune->beg_obj_id;
	cursor.key_beg.key = HAMMER_MIN_KEY;
	cursor.key_beg.create_tid = 1;
	cursor.key_beg.delete_tid = 0;
	cursor.key_beg.rec_type = HAMMER_MIN_RECTYPE;
	cursor.key_beg.obj_type = 0;

	cursor.key_end.localization = prune->cur_localization;
	cursor.key_end.obj_id = prune->cur_obj_id;
	cursor.key_end.key = prune->cur_key;
	cursor.key_end.create_tid = HAMMER_MAX_TID - 1;
	cursor.key_end.delete_tid = 0;
	cursor.key_end.rec_type = HAMMER_MAX_RECTYPE;
	cursor.key_end.obj_type = 0;

	cursor.flags |= HAMMER_CURSOR_END_INCLUSIVE;
	cursor.flags |= HAMMER_CURSOR_BACKEND;

	/*
	 * This flag allows the B-Tree code to clean up loose ends.
	 */
	cursor.flags |= HAMMER_CURSOR_PRUNING;

	hammer_sync_lock_sh(trans);
	error = hammer_btree_last(&cursor);
	while (error == 0) {
		/*
		 * Yield to more important tasks
		 */
		if (trans->hmp->sync_lock.wanted) {
			hammer_sync_unlock(trans);
			tsleep(trans, 0, "hmrslo", hz / 10);
			hammer_sync_lock_sh(trans);
		}

		/*
		 * Check for work
		 */
		elm = &cursor.node->ondisk->elms[cursor.index];
		prune->cur_localization = elm->base.localization;
		prune->cur_obj_id = elm->base.obj_id;
		prune->cur_key = elm->base.key;

		if (prune->stat_oldest_tid > elm->leaf.base.create_tid)
			prune->stat_oldest_tid = elm->leaf.base.create_tid;

		if (check_prune(prune, elm, &realign_cre, &realign_del) == 0) {
			if (hammer_debug_general & 0x0200) {
				kprintf("check %016llx %016llx: DELETE\n",
					elm->base.obj_id, elm->base.key);
			}

			/*
			 * NOTE: This can return EDEADLK
			 *
			 * Acquiring the sync lock guarantees that the
			 * operation will not cross a synchronization
			 * boundary (see the flusher).
			 */
			isdir = (elm->base.rec_type == HAMMER_RECTYPE_DIRENTRY);

			error = hammer_delete_at_cursor(&cursor,
							&prune->stat_bytes);
			if (error)
				break;

			if (isdir)
				++prune->stat_dirrecords;
			else
				++prune->stat_rawrecords;

			/*
			 * The current record might now be the one after
			 * the one we deleted, set ATEDISK to force us
			 * to skip it (since we are iterating backwards).
			 */
			cursor.flags |= HAMMER_CURSOR_ATEDISK;
		} else if (realign_cre >= 0 || realign_del >= 0) {
			error = realign_prune(prune, &cursor,
					      realign_cre, realign_del);
			if (error == 0) {
				cursor.flags |= HAMMER_CURSOR_ATEDISK;
				if (hammer_debug_general & 0x0200) {
					kprintf("check %016llx %016llx: "
						"REALIGN\n",
						elm->base.obj_id,
						elm->base.key);
				}
			}
		} else {
			cursor.flags |= HAMMER_CURSOR_ATEDISK;
			if (hammer_debug_general & 0x0100) {
				kprintf("check %016llx %016llx: SKIP\n",
					elm->base.obj_id, elm->base.key);
			}
		}
		++prune->stat_scanrecords;

		/*
		 * Bad hack for now, don't blow out the kernel's buffer
		 * cache.  NOTE: We still hold locks on the cursor, we
		 * cannot call the flusher synchronously.
		 */
		if (trans->hmp->locked_dirty_count +
		    trans->hmp->io_running_count > hammer_limit_dirtybufs) {
			hammer_flusher_async(trans->hmp);
			tsleep(trans, 0, "hmrslo", hz / 10);
		}
		error = hammer_signal_check(trans->hmp);
		if (error == 0)
			error = hammer_btree_iterate_reverse(&cursor);
	}
	hammer_sync_unlock(trans);
	if (error == ENOENT)
		error = 0;
	hammer_done_cursor(&cursor);
	if (error == EDEADLK)
		goto retry;
	if (error == EINTR) {
		prune->head.flags |= HAMMER_IOC_HEAD_INTR;
		error = 0;
	}
	return(error);
}

/*
 * Check pruning list.  The list must be sorted in descending order.
 */
static int
check_prune(struct hammer_ioc_prune *prune, hammer_btree_elm_t elm,
	    int *realign_cre, int *realign_del)
{
	struct hammer_ioc_prune_elm *scan;
	int i;

	*realign_cre = -1;
	*realign_del = -1;

	/*
	 * If pruning everything remove all records with a non-zero
	 * delete_tid.
	 */
	if (prune->head.flags & HAMMER_IOC_PRUNE_ALL) {
		if (elm->base.delete_tid != 0)
			return(0);
		return(-1);
	}

	for (i = 0; i < prune->nelms; ++i) {
		scan = &prune->elms[i];

		/*
		 * Locate the scan index covering the create and delete TIDs.
		 */
		if (*realign_cre < 0 &&
		    elm->base.create_tid >= scan->beg_tid &&
		    elm->base.create_tid < scan->end_tid) {
			*realign_cre = i;
		}
		if (*realign_del < 0 && elm->base.delete_tid &&
		    elm->base.delete_tid > scan->beg_tid &&
		    elm->base.delete_tid <= scan->end_tid) {
			*realign_del = i;
		}

		/*
		 * Now check for loop termination.
		 */
		if (elm->base.create_tid >= scan->end_tid ||
		    elm->base.delete_tid > scan->end_tid) {
			break;
		}

		/*
		 * Now determine if we can delete the record.
		 */
		if (elm->base.delete_tid &&
		    elm->base.create_tid >= scan->beg_tid &&
		    elm->base.delete_tid <= scan->end_tid &&
		    elm->base.create_tid / scan->mod_tid ==
		    elm->base.delete_tid / scan->mod_tid) {
			return(0);
		}
	}
	return(-1);
}

/*
 * Align the record to cover any gaps created through the deletion of
 * records within the pruning space.  If we were to just delete the records
 * there would be gaps which in turn would cause a snapshot that is NOT on
 * a pruning boundary to appear corrupt to the user.  Forcing alignment
 * of the create_tid and delete_tid for retained records 'reconnects'
 * the previously contiguous space, making it contiguous again after the
 * deletions.
 *
 * The use of a reverse iteration allows us to safely align the records and
 * related elements without creating temporary overlaps.  XXX we should
 * add ordering dependancies for record buffers to guarantee consistency
 * during recovery.
 */
static int
realign_prune(struct hammer_ioc_prune *prune,
	      hammer_cursor_t cursor, int realign_cre, int realign_del)
{
	hammer_btree_elm_t elm;
	hammer_tid_t delta;
	hammer_tid_t mod;
	hammer_tid_t tid;
	int error;

	hammer_cursor_downgrade(cursor);

	elm = &cursor->node->ondisk->elms[cursor->index];
	++prune->stat_realignments;

	/*
	 * Align the create_tid.  By doing a reverse iteration we guarantee
	 * that all records after our current record have already been
	 * aligned, allowing us to safely correct the right-hand-boundary
	 * (because no record to our right if otherwise exactly matching
	 * will have a create_tid to the left of our aligned create_tid).
	 *
	 * Ordering is important here XXX but disk write ordering for
	 * inter-cluster corrections is not currently guaranteed.
	 */
	error = 0;
	if (realign_cre >= 0) {
		mod = prune->elms[realign_cre].mod_tid;
		delta = elm->leaf.base.create_tid % mod;
		if (delta) {
			tid = elm->leaf.base.create_tid - delta + mod;

			/* can EDEADLK */
			error = hammer_btree_correct_rhb(cursor, tid + 1);
			if (error == 0) {
				error = hammer_btree_extract(cursor,
						     HAMMER_CURSOR_GET_LEAF);
			}
			if (error == 0) {
				/* can EDEADLK */
				error = hammer_cursor_upgrade(cursor);
			}
			if (error == 0) {
				hammer_modify_node(cursor->trans, cursor->node,
					    &elm->leaf.base.create_tid,
					    sizeof(elm->leaf.base.create_tid));
				elm->leaf.base.create_tid = tid;
				hammer_modify_node_done(cursor->node);
			}
		}
	}

	/*
	 * Align the delete_tid.  This only occurs if the record is historical
	 * was deleted at some point.  Realigning the delete_tid does not
	 * move the record within the B-Tree but may cause it to temporarily
	 * overlap a record that has not yet been pruned.
	 */
	if (error == 0 && realign_del >= 0) {
		mod = prune->elms[realign_del].mod_tid;
		delta = elm->leaf.base.delete_tid % mod;
		if (delta) {
			error = hammer_btree_extract(cursor,
						     HAMMER_CURSOR_GET_LEAF);
			if (error == 0) {
				hammer_modify_node(cursor->trans, cursor->node,
					    &elm->leaf.base.delete_tid,
					    sizeof(elm->leaf.base.delete_tid));
				elm->leaf.base.delete_tid =
					    elm->leaf.base.delete_tid -
					    delta + mod;
				hammer_modify_node_done(cursor->node);
			}
		}
	}
	return (error);
}

