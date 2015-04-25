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
 * $DragonFly: src/sys/vfs/hammer/hammer_prune.c,v 1.19 2008/09/23 21:03:52 dillon Exp $
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
static int prune_should_delete(struct hammer_ioc_prune *prune,
			       hammer_btree_leaf_elm_t elm);
static void prune_check_nlinks(hammer_cursor_t cursor,
			       hammer_btree_leaf_elm_t elm);

int
hammer_ioc_prune(hammer_transaction_t trans, hammer_inode_t ip,
		 struct hammer_ioc_prune *prune)
{
	struct hammer_cursor cursor;
	hammer_btree_leaf_elm_t elm;
	struct hammer_ioc_prune_elm *copy_elms;
	struct hammer_ioc_prune_elm *user_elms;
	int error;
	int isdir;
	int elm_array_size;
	int seq;
	int64_t bytes;

	if (prune->nelms < 0 || prune->nelms > HAMMER_MAX_PRUNE_ELMS)
		return(EINVAL);
	if ((prune->key_beg.localization | prune->key_end.localization) &
	    HAMMER_LOCALIZE_PSEUDOFS_MASK) {
		return(EINVAL);
	}
	if (prune->key_beg.localization > prune->key_end.localization)
		return(EINVAL);
	if (prune->key_beg.localization == prune->key_end.localization) {
		if (prune->key_beg.obj_id > prune->key_end.obj_id)
			return(EINVAL);
		/* key-space limitations - no check needed */
	}
	if ((prune->head.flags & HAMMER_IOC_PRUNE_ALL) && prune->nelms)
		return(EINVAL);

	prune->key_cur.localization = (prune->key_end.localization &
					HAMMER_LOCALIZE_MASK) +
				      ip->obj_localization;
	prune->key_cur.obj_id = prune->key_end.obj_id;
	prune->key_cur.key = HAMMER_MAX_KEY;

	/*
	 * Copy element array from userland
	 */
	elm_array_size = sizeof(*copy_elms) * prune->nelms;
	user_elms = prune->elms;
	copy_elms = kmalloc(elm_array_size, M_TEMP, M_WAITOK);
	if ((error = copyin(user_elms, copy_elms, elm_array_size)) != 0)
		goto failed;
	prune->elms = copy_elms;

	seq = trans->hmp->flusher.done;

	/*
	 * Scan backwards.  Retries typically occur if a deadlock is detected.
	 */
retry:
	error = hammer_init_cursor(trans, &cursor, NULL, NULL);
	if (error) {
		hammer_done_cursor(&cursor);
		goto failed;
	}
	cursor.key_beg.localization = (prune->key_beg.localization &
					HAMMER_LOCALIZE_MASK) +
				      ip->obj_localization;
	cursor.key_beg.obj_id = prune->key_beg.obj_id;
	cursor.key_beg.key = HAMMER_MIN_KEY;
	cursor.key_beg.create_tid = 1;
	cursor.key_beg.delete_tid = 0;
	cursor.key_beg.rec_type = HAMMER_MIN_RECTYPE;
	cursor.key_beg.obj_type = 0;

	cursor.key_end.localization = prune->key_cur.localization;
	cursor.key_end.obj_id = prune->key_cur.obj_id;
	cursor.key_end.key = prune->key_cur.key;
	cursor.key_end.create_tid = HAMMER_MAX_TID - 1;
	cursor.key_end.delete_tid = 0;
	cursor.key_end.rec_type = HAMMER_MAX_RECTYPE;
	cursor.key_end.obj_type = 0;

	cursor.flags |= HAMMER_CURSOR_END_INCLUSIVE;
	cursor.flags |= HAMMER_CURSOR_BACKEND;

	/*
	 * This flag allows the B-Tree code to clean up loose ends.  At
	 * the moment (XXX) it also means we have to hold the sync lock
	 * through the iteration.
	 */
	cursor.flags |= HAMMER_CURSOR_PRUNING;

	hammer_sync_lock_sh(trans);
	error = hammer_btree_last(&cursor);
	hammer_sync_unlock(trans);

	while (error == 0) {
		/*
		 * Check for work
		 */
		elm = &cursor.node->ondisk->elms[cursor.index].leaf;
		prune->key_cur = elm->base;

		/*
		 * Yield to more important tasks
		 */
		if ((error = hammer_signal_check(trans->hmp)) != 0)
			break;

		if (prune->stat_oldest_tid > elm->base.create_tid)
			prune->stat_oldest_tid = elm->base.create_tid;

		if (hammer_debug_general & 0x0200) {
			kprintf("check %016llx %016llx cre=%016llx del=%016llx\n",
					(long long)elm->base.obj_id,
					(long long)elm->base.key,
					(long long)elm->base.create_tid,
					(long long)elm->base.delete_tid);
		}
				
		if (prune_should_delete(prune, elm)) {
			if (hammer_debug_general & 0x0200) {
				kprintf("check %016llx %016llx: DELETE\n",
					(long long)elm->base.obj_id,
					(long long)elm->base.key);
			}

			/*
			 * NOTE: This can return EDEADLK
			 *
			 * Acquiring the sync lock guarantees that the
			 * operation will not cross a synchronization
			 * boundary (see the flusher).
			 *
			 * We dont need to track inodes or next_tid when
			 * we are destroying deleted records.
			 */
			isdir = (elm->base.rec_type == HAMMER_RECTYPE_DIRENTRY);

			hammer_sync_lock_sh(trans);
			error = hammer_delete_at_cursor(&cursor,
							HAMMER_DELETE_DESTROY,
							cursor.trans->tid,
							cursor.trans->time32,
							0, &bytes);
			hammer_sync_unlock(trans);
			if (error)
				break;

			if (isdir)
				++prune->stat_dirrecords;
			else
				++prune->stat_rawrecords;
			prune->stat_bytes += bytes;

			/*
			 * The current record might now be the one after
			 * the one we deleted, set ATEDISK to force us
			 * to skip it (since we are iterating backwards).
			 */
			cursor.flags |= HAMMER_CURSOR_ATEDISK;
		} else {
			/*
			 * Nothing to delete, but we may have to check other
			 * things.
			 */
			prune_check_nlinks(&cursor, elm);
			cursor.flags |= HAMMER_CURSOR_ATEDISK;
			if (hammer_debug_general & 0x0100) {
				kprintf("check %016llx %016llx: SKIP\n",
					(long long)elm->base.obj_id,
					(long long)elm->base.key);
			}
		}
		++prune->stat_scanrecords;

		/*
		 * WARNING: See warnings in hammer_unlock_cursor() function.
		 */
		while (hammer_flusher_meta_halflimit(trans->hmp) ||
		       hammer_flusher_undo_exhausted(trans, 2)) {
			hammer_unlock_cursor(&cursor);
			hammer_flusher_wait(trans->hmp, seq);
			hammer_lock_cursor(&cursor);
			seq = hammer_flusher_async_one(trans->hmp);
		}
		hammer_sync_lock_sh(trans);
		error = hammer_btree_iterate_reverse(&cursor);
		hammer_sync_unlock(trans);
	}
	if (error == ENOENT)
		error = 0;
	hammer_done_cursor(&cursor);
	if (error == EDEADLK)
		goto retry;
	if (error == EINTR) {
		prune->head.flags |= HAMMER_IOC_HEAD_INTR;
		error = 0;
	}
failed:
	prune->key_cur.localization &= HAMMER_LOCALIZE_MASK;
	prune->elms = user_elms;
	kfree(copy_elms, M_TEMP);
	return(error);
}

/*
 * Check pruning list.  The list must be sorted in descending order.
 *
 * Return non-zero if the record should be deleted.
 */
static int
prune_should_delete(struct hammer_ioc_prune *prune, hammer_btree_leaf_elm_t elm)
{
	struct hammer_ioc_prune_elm *scan;
	int i;

	/*
	 * If pruning everything remove all records with a non-zero
	 * delete_tid.
	 */
	if (prune->head.flags & HAMMER_IOC_PRUNE_ALL) {
		if (elm->base.delete_tid != 0)
			return(1);
		return(0);
	}

	for (i = 0; i < prune->nelms; ++i) {
		scan = &prune->elms[i];

		/*
		 * Check for loop termination.
		 */
		if (elm->base.create_tid >= scan->end_tid ||
		    elm->base.delete_tid > scan->end_tid) {
			break;
		}

		/*
		 * Determine if we can delete the record.
		 */
		if (elm->base.delete_tid &&
		    elm->base.create_tid >= scan->beg_tid &&
		    elm->base.delete_tid <= scan->end_tid &&
		    (elm->base.create_tid - scan->beg_tid) / scan->mod_tid ==
		    (elm->base.delete_tid - scan->beg_tid) / scan->mod_tid) {
			return(1);
		}
	}
	return(0);
}

/*
 * Dangling inodes can occur if processes are holding open descriptors on
 * deleted files as-of when a machine crashes.  When we find one simply
 * acquire the inode and release it.  The inode handling code will then
 * do the right thing.
 */
static
void
prune_check_nlinks(hammer_cursor_t cursor, hammer_btree_leaf_elm_t elm)
{
	hammer_inode_t ip;
	int error;

	if (elm->base.rec_type != HAMMER_RECTYPE_INODE)
		return;
	if (elm->base.delete_tid != 0)
		return;
	if (hammer_btree_extract(cursor, HAMMER_CURSOR_GET_DATA))
		return;
	if (cursor->data->inode.nlinks)
		return;
	hammer_cursor_downgrade(cursor);
	ip = hammer_get_inode(cursor->trans, NULL, elm->base.obj_id,
		      HAMMER_MAX_TID,
		      elm->base.localization & HAMMER_LOCALIZE_PSEUDOFS_MASK,
		      0, &error);
	if (ip) {
		if (hammer_debug_general & 0x0001) {
			kprintf("pruning disconnected inode %016llx\n",
				(long long)elm->base.obj_id);
		}
		hammer_rel_inode(ip, 0);
		hammer_inode_waitreclaims(cursor->trans);
	} else {
		kprintf("unable to prune disconnected inode %016llx\n",
			(long long)elm->base.obj_id);
	}
}

#if 0

/*
 * NOTE: THIS CODE HAS BEEN REMOVED!  Pruning no longer attempts to realign
 *	 adjacent records because it seriously interferes with every 
 *	 mirroring algorithm I could come up with.
 *
 *	 This means that historical accesses beyond the first snapshot
 *	 softlink should be on snapshot boundaries only.  Historical
 *	 accesses from "now" to the first snapshot softlink continue to
 *	 be fine-grained.
 *
 * NOTE: It also looks like there's a bug in the removed code.  It is believed
 *	 that create_tid can sometimes get set to 0xffffffffffffffff.  Just as
 *	 well we no longer try to do this fancy shit.  Probably the attempt to
 *	 correct the rhb is blowing up the cursor's indexing or addressing mapping.
 *
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
	struct hammer_ioc_prune_elm *scan;
	hammer_btree_elm_t elm;
	hammer_tid_t delta;
	hammer_tid_t tid;
	int error;

	hammer_cursor_downgrade(cursor);

	elm = &cursor->node->ondisk->elms[cursor->index];
	++prune->stat_realignments;

	/*
	 * Align the create_tid.  By doing a reverse iteration we guarantee
	 * that all records after our current record have already been
	 * aligned, allowing us to safely correct the right-hand-boundary
	 * (because no record to our right is otherwise exactly matching
	 * will have a create_tid to the left of our aligned create_tid).
	 */
	error = 0;
	if (realign_cre >= 0) {
		scan = &prune->elms[realign_cre];

		delta = (elm->leaf.base.create_tid - scan->beg_tid) % 
			scan->mod_tid;
		if (delta) {
			tid = elm->leaf.base.create_tid - delta + scan->mod_tid;

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
		scan = &prune->elms[realign_del];

		delta = (elm->leaf.base.delete_tid - scan->beg_tid) % 
			scan->mod_tid;
		if (delta) {
			error = hammer_btree_extract(cursor,
						     HAMMER_CURSOR_GET_LEAF);
			if (error == 0) {
				hammer_modify_node(cursor->trans, cursor->node,
					    &elm->leaf.base.delete_tid,
					    sizeof(elm->leaf.base.delete_tid));
				elm->leaf.base.delete_tid =
					    elm->leaf.base.delete_tid -
					    delta + scan->mod_tid;
				hammer_modify_node_done(cursor->node);
			}
		}
	}
	return (error);
}

#endif
