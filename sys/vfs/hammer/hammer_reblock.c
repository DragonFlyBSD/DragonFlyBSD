/*
 * Copyright (c) 2008-2012 The DragonFly Project.  All rights reserved.
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
 */
/*
 * HAMMER reblocker - This code frees up fragmented physical space
 *
 * HAMMER only keeps track of free space on a big-block basis.  A big-block
 * containing holes can only be freed by migrating the remaining data in
 * that big-block into a new big-block, then freeing the big-block.
 *
 * This function is called from an ioctl or via the hammer support thread.
 */

#include "hammer.h"

static int hammer_reblock_helper(struct hammer_ioc_reblock *reblock,
				 hammer_cursor_t cursor,
				 hammer_btree_elm_t elm);
static int hammer_reblock_data(struct hammer_ioc_reblock *reblock,
				hammer_cursor_t cursor, hammer_btree_elm_t elm);
static int hammer_reblock_leaf_node(struct hammer_ioc_reblock *reblock,
				hammer_cursor_t cursor, hammer_btree_elm_t elm);
static int hammer_reblock_int_node(struct hammer_ioc_reblock *reblock,
				hammer_cursor_t cursor, hammer_btree_elm_t elm);

int
hammer_ioc_reblock(hammer_transaction_t trans, hammer_inode_t ip,
		   struct hammer_ioc_reblock *reblock)
{
	struct hammer_cursor cursor;
	hammer_btree_elm_t elm;
	int checkspace_count;
	int error;
	int seq;
	int slop;
	u_int32_t key_end_localization;

	if ((reblock->key_beg.localization | reblock->key_end.localization) &
	    HAMMER_LOCALIZE_PSEUDOFS_MASK) {
		return(EINVAL);
	}
	if (reblock->key_beg.obj_id >= reblock->key_end.obj_id)
		return(EINVAL);
	if (reblock->free_level < 0 ||
	    reblock->free_level > HAMMER_BIGBLOCK_SIZE)
		return(EINVAL);

	/*
	 * A fill_percentage <= 20% is considered an emergency.  free_level is
	 * inverted from fill_percentage.
	 */
	if (reblock->free_level >= HAMMER_BIGBLOCK_SIZE * 8 / 10)
		slop = HAMMER_CHKSPC_EMERGENCY;
	else
		slop = HAMMER_CHKSPC_REBLOCK;

	/*
	 * Ioctl caller has only set localization type to reblock.
	 * Initialize cursor key localization with ip localization.
	 */
	reblock->key_cur = reblock->key_beg;
	reblock->key_cur.localization &= HAMMER_LOCALIZE_MASK;
	if (reblock->allpfs == 0)
		reblock->key_cur.localization += ip->obj_localization;

	key_end_localization = reblock->key_end.localization;
	key_end_localization &= HAMMER_LOCALIZE_MASK;
	if (reblock->allpfs == 0)
		key_end_localization += ip->obj_localization;
	else
		key_end_localization += ((HAMMER_MAX_PFS - 1) << 16);

	checkspace_count = 0;
	seq = trans->hmp->flusher.done;
retry:
	error = hammer_init_cursor(trans, &cursor, NULL, NULL);
	if (error) {
		hammer_done_cursor(&cursor);
		goto failed;
	}
	cursor.key_beg.localization = reblock->key_cur.localization;
	cursor.key_beg.obj_id = reblock->key_cur.obj_id;
	cursor.key_beg.key = HAMMER_MIN_KEY;
	cursor.key_beg.create_tid = 1;
	cursor.key_beg.delete_tid = 0;
	cursor.key_beg.rec_type = HAMMER_MIN_RECTYPE;
	cursor.key_beg.obj_type = 0;

	cursor.key_end.localization = key_end_localization;
	cursor.key_end.obj_id = reblock->key_end.obj_id;
	cursor.key_end.key = HAMMER_MAX_KEY;
	cursor.key_end.create_tid = HAMMER_MAX_TID - 1;
	cursor.key_end.delete_tid = 0;
	cursor.key_end.rec_type = HAMMER_MAX_RECTYPE;
	cursor.key_end.obj_type = 0;

	cursor.flags |= HAMMER_CURSOR_END_INCLUSIVE;
	cursor.flags |= HAMMER_CURSOR_BACKEND;
	cursor.flags |= HAMMER_CURSOR_NOSWAPCACHE;

	/*
	 * This flag allows the btree scan code to return internal nodes,
	 * so we can reblock them in addition to the leafs.  Only specify it
	 * if we intend to reblock B-Tree nodes.
	 */
	if (reblock->head.flags & HAMMER_IOC_DO_BTREE)
		cursor.flags |= HAMMER_CURSOR_REBLOCKING;

	error = hammer_btree_first(&cursor);
	while (error == 0) {
		/*
		 * Internal or Leaf node
		 */
		KKASSERT(cursor.index < cursor.node->ondisk->count);
		elm = &cursor.node->ondisk->elms[cursor.index];
		reblock->key_cur.obj_id = elm->base.obj_id;
		reblock->key_cur.localization = elm->base.localization;

		/*
		 * Yield to more important tasks
		 */
		if ((error = hammer_signal_check(trans->hmp)) != 0)
			break;

		/*
		 * If there is insufficient free space it may be due to
		 * reserved bigblocks, which flushing might fix.
		 *
		 * We must force a retest in case the unlocked cursor is
		 * moved to the end of the leaf, or moved to an internal
		 * node.
		 *
		 * WARNING: See warnings in hammer_unlock_cursor() function.
		 */
		if (hammer_checkspace(trans->hmp, slop)) {
			if (++checkspace_count == 10) {
				error = ENOSPC;
				break;
			}
			hammer_unlock_cursor(&cursor);
			cursor.flags |= HAMMER_CURSOR_RETEST;
			hammer_flusher_wait(trans->hmp, seq);
			hammer_lock_cursor(&cursor);
			seq = hammer_flusher_async(trans->hmp, NULL);
			goto skip;
		}

		/*
		 * Acquiring the sync_lock prevents the operation from
		 * crossing a synchronization boundary.
		 *
		 * NOTE: cursor.node may have changed on return.
		 *
		 * WARNING: See warnings in hammer_unlock_cursor() function.
		 */
		hammer_sync_lock_sh(trans);
		error = hammer_reblock_helper(reblock, &cursor, elm);
		hammer_sync_unlock(trans);

		while (hammer_flusher_meta_halflimit(trans->hmp) ||
		       hammer_flusher_undo_exhausted(trans, 2)) {
			hammer_unlock_cursor(&cursor);
			hammer_flusher_wait(trans->hmp, seq);
			hammer_lock_cursor(&cursor);
			seq = hammer_flusher_async_one(trans->hmp);
		}

		/*
		 * Setup for iteration, our cursor flags may be modified by
		 * other threads while we are unlocked.
		 */
		cursor.flags |= HAMMER_CURSOR_ATEDISK;

		/*
		 * We allocate data buffers, which atm we don't track
		 * dirty levels for because we allow the kernel to write
		 * them.  But if we allocate too many we can still deadlock
		 * the buffer cache.
		 *
		 * WARNING: See warnings in hammer_unlock_cursor() function.
		 *	    (The cursor's node and element may change!)
		 */
		if (bd_heatup()) {
			hammer_unlock_cursor(&cursor);
			bwillwrite(HAMMER_XBUFSIZE);
			hammer_lock_cursor(&cursor);
		}
		vm_wait_nominal();
skip:
		if (error == 0) {
			error = hammer_btree_iterate(&cursor);
		}
	}
	if (error == ENOENT)
		error = 0;
	hammer_done_cursor(&cursor);
	if (error == EWOULDBLOCK) {
		hammer_flusher_sync(trans->hmp);
		goto retry;
	}
	if (error == EDEADLK)
		goto retry;
	if (error == EINTR) {
		reblock->head.flags |= HAMMER_IOC_HEAD_INTR;
		error = 0;
	}
failed:
	reblock->key_cur.localization &= HAMMER_LOCALIZE_MASK;
	return(error);
}

/*
 * Reblock the B-Tree (leaf) node, record, and/or data if necessary.
 *
 * XXX We have no visibility into internal B-Tree nodes at the moment,
 * only leaf nodes.
 */
static int
hammer_reblock_helper(struct hammer_ioc_reblock *reblock,
		      hammer_cursor_t cursor, hammer_btree_elm_t elm)
{
	hammer_mount_t hmp;
	hammer_off_t tmp_offset;
	hammer_node_ondisk_t ondisk;
	struct hammer_btree_leaf_elm leaf;
	int error;
	int bytes;
	int cur;
	int iocflags;

	error = 0;
	hmp = cursor->trans->hmp;

	/*
	 * Reblock data.  Note that data embedded in a record is reblocked
	 * by the record reblock code.  Data processing only occurs at leaf
	 * nodes and for RECORD element types.
	 */
	if (cursor->node->ondisk->type != HAMMER_BTREE_TYPE_LEAF)
		goto skip;
	if (elm->leaf.base.btype != HAMMER_BTREE_TYPE_RECORD)
		return(0);
	tmp_offset = elm->leaf.data_offset;
	if (tmp_offset == 0)
		goto skip;
	if (error)
		goto skip;

	/*
	 * NOTE: Localization restrictions may also have been set-up, we can't
	 *	 just set the match flags willy-nilly here.
	 */
	switch(elm->leaf.base.rec_type) {
	case HAMMER_RECTYPE_INODE:
	case HAMMER_RECTYPE_SNAPSHOT:
	case HAMMER_RECTYPE_CONFIG:
		iocflags = HAMMER_IOC_DO_INODES;
		break;
	case HAMMER_RECTYPE_EXT:
	case HAMMER_RECTYPE_FIX:
	case HAMMER_RECTYPE_PFS:
	case HAMMER_RECTYPE_DIRENTRY:
		iocflags = HAMMER_IOC_DO_DIRS;
		break;
	case HAMMER_RECTYPE_DATA:
	case HAMMER_RECTYPE_DB:
		iocflags = HAMMER_IOC_DO_DATA;
		break;
	default:
		iocflags = 0;
		break;
	}
	if (reblock->head.flags & iocflags) {
		++reblock->data_count;
		reblock->data_byte_count += elm->leaf.data_len;
		bytes = hammer_blockmap_getfree(hmp, tmp_offset, &cur, &error);
		if (hammer_debug_general & 0x4000)
			kprintf("D %6d/%d\n", bytes, reblock->free_level);
		if (error == 0 && (cur == 0 || reblock->free_level == 0) &&
		    bytes >= reblock->free_level) {
			/*
			 * This is nasty, the uncache code may have to get
			 * vnode locks and because of that we can't hold
			 * the cursor locked.
			 *
			 * WARNING: See warnings in hammer_unlock_cursor()
			 *	    function.
			 */
			leaf = elm->leaf;
			hammer_unlock_cursor(cursor);
			hammer_io_direct_uncache(hmp, &leaf);
			hammer_lock_cursor(cursor);

			/*
			 * elm may have become stale or invalid, reload it.
			 * ondisk variable is temporary only.  Note that
			 * cursor->node and thus cursor->node->ondisk may
			 * also changed.
			 */
			ondisk = cursor->node->ondisk;
			elm = &ondisk->elms[cursor->index];
			if (cursor->flags & HAMMER_CURSOR_RETEST) {
				kprintf("hammer: debug: retest on "
					"reblocker uncache\n");
				error = EDEADLK;
			} else if (ondisk->type != HAMMER_BTREE_TYPE_LEAF ||
				   cursor->index >= ondisk->count) {
				kprintf("hammer: debug: shifted on "
					"reblocker uncache\n");
				error = EDEADLK;
			} else if (bcmp(&elm->leaf, &leaf, sizeof(leaf))) {
				kprintf("hammer: debug: changed on "
					"reblocker uncache\n");
				error = EDEADLK;
			}
			if (error == 0)
				error = hammer_cursor_upgrade(cursor);
			if (error == 0) {
				KKASSERT(cursor->index < ondisk->count);
				error = hammer_reblock_data(reblock,
							    cursor, elm);
			}
			if (error == 0) {
				++reblock->data_moves;
				reblock->data_byte_moves += elm->leaf.data_len;
			}
		}
	}

skip:
	/*
	 * Reblock a B-Tree internal or leaf node.  A leaf node is reblocked
	 * on initial entry only (element 0).  An internal node is reblocked
	 * when entered upward from its first leaf node only (also element 0).
	 * Further revisits of the internal node (index > 0) are ignored.
	 */
	tmp_offset = cursor->node->node_offset;
	if (cursor->index == 0 &&
	    error == 0 && (reblock->head.flags & HAMMER_IOC_DO_BTREE)) {
		++reblock->btree_count;
		bytes = hammer_blockmap_getfree(hmp, tmp_offset, &cur, &error);
		if (hammer_debug_general & 0x4000)
			kprintf("B %6d/%d\n", bytes, reblock->free_level);
		if (error == 0 && (cur == 0 || reblock->free_level == 0) &&
		    bytes >= reblock->free_level) {
			error = hammer_cursor_upgrade(cursor);
			if (error == 0) {
				if (cursor->parent) {
					KKASSERT(cursor->parent_index <
						 cursor->parent->ondisk->count);
					elm = &cursor->parent->ondisk->elms[cursor->parent_index];
				} else {
					elm = NULL;
				}
				switch(cursor->node->ondisk->type) {
				case HAMMER_BTREE_TYPE_LEAF:
					error = hammer_reblock_leaf_node(
							reblock, cursor, elm);
					break;
				case HAMMER_BTREE_TYPE_INTERNAL:
					error = hammer_reblock_int_node(
							reblock, cursor, elm);
					break;
				default:
					panic("Illegal B-Tree node type");
				}
			}
			if (error == 0) {
				++reblock->btree_moves;
			}
		}
	}

	hammer_cursor_downgrade(cursor);
	return(error);
}

/*
 * Reblock a record's data.  Both the B-Tree element and record pointers
 * to the data must be adjusted.
 */
static int
hammer_reblock_data(struct hammer_ioc_reblock *reblock,
		    hammer_cursor_t cursor, hammer_btree_elm_t elm)
{
	struct hammer_buffer *data_buffer = NULL;
	hammer_off_t ndata_offset;
	int error;
	void *ndata;

	error = hammer_btree_extract(cursor, HAMMER_CURSOR_GET_DATA |
					     HAMMER_CURSOR_GET_LEAF);
	if (error)
		return (error);
	ndata = hammer_alloc_data(cursor->trans, elm->leaf.data_len,
				  elm->leaf.base.rec_type,
				  &ndata_offset, &data_buffer,
				  0, &error);
	if (error)
		goto done;
	hammer_io_notmeta(data_buffer);

	/*
	 * Move the data.  Note that we must invalidate any cached
	 * data buffer in the cursor before calling blockmap_free.
	 * The blockmap_free may free up the entire big-block and
	 * will not be able to invalidate it if the cursor is holding
	 * a data buffer cached in that big block.
	 */
	hammer_modify_buffer(cursor->trans, data_buffer, NULL, 0);
	bcopy(cursor->data, ndata, elm->leaf.data_len);
	hammer_modify_buffer_done(data_buffer);
	hammer_cursor_invalidate_cache(cursor);

	hammer_blockmap_free(cursor->trans,
			     elm->leaf.data_offset, elm->leaf.data_len);

	hammer_modify_node(cursor->trans, cursor->node,
			   &elm->leaf.data_offset, sizeof(hammer_off_t));
	elm->leaf.data_offset = ndata_offset;
	hammer_modify_node_done(cursor->node);

done:
	if (data_buffer)
		hammer_rel_buffer(data_buffer, 0);
	return (error);
}

/*
 * Reblock a B-Tree leaf node.  The parent must be adjusted to point to
 * the new copy of the leaf node.
 *
 * elm is a pointer to the parent element pointing at cursor.node.
 */
static int
hammer_reblock_leaf_node(struct hammer_ioc_reblock *reblock,
			 hammer_cursor_t cursor, hammer_btree_elm_t elm)
{
	hammer_node_t onode;
	hammer_node_t nnode;
	int error;

	/*
	 * Don't supply a hint when allocating the leaf.  Fills are done
	 * from the leaf upwards.
	 */
	onode = cursor->node;
	nnode = hammer_alloc_btree(cursor->trans, 0, &error);

	if (nnode == NULL)
		return (error);

	/*
	 * Move the node
	 */
	hammer_lock_ex(&nnode->lock);
	hammer_modify_node_noundo(cursor->trans, nnode);
	bcopy(onode->ondisk, nnode->ondisk, sizeof(*nnode->ondisk));

	if (elm) {
		/*
		 * We are not the root of the B-Tree 
		 */
		hammer_modify_node(cursor->trans, cursor->parent,
				   &elm->internal.subtree_offset,
				   sizeof(elm->internal.subtree_offset));
		elm->internal.subtree_offset = nnode->node_offset;
		hammer_modify_node_done(cursor->parent);
	} else {
		/*
		 * We are the root of the B-Tree
		 */
                hammer_volume_t volume;
                        
                volume = hammer_get_root_volume(cursor->trans->hmp, &error);
                KKASSERT(error == 0);

                hammer_modify_volume_field(cursor->trans, volume,
					   vol0_btree_root);
                volume->ondisk->vol0_btree_root = nnode->node_offset;
                hammer_modify_volume_done(volume);
                hammer_rel_volume(volume, 0);
        }

	hammer_cursor_replaced_node(onode, nnode);
	hammer_delete_node(cursor->trans, onode);

	if (hammer_debug_general & 0x4000) {
		kprintf("REBLOCK LNODE %016llx -> %016llx\n",
			(long long)onode->node_offset,
			(long long)nnode->node_offset);
	}
	hammer_modify_node_done(nnode);
	cursor->node = nnode;

	hammer_unlock(&onode->lock);
	hammer_rel_node(onode);

	return (error);
}

/*
 * Reblock a B-Tree internal node.  The parent must be adjusted to point to
 * the new copy of the internal node, and the node's children's parent
 * pointers must also be adjusted to point to the new copy.
 *
 * elm is a pointer to the parent element pointing at cursor.node.
 */
static int
hammer_reblock_int_node(struct hammer_ioc_reblock *reblock,
			 hammer_cursor_t cursor, hammer_btree_elm_t elm)
{
	struct hammer_node_lock lockroot;
	hammer_node_t onode;
	hammer_node_t nnode;
	int error;
	int i;

	hammer_node_lock_init(&lockroot, cursor->node);
	error = hammer_btree_lock_children(cursor, 1, &lockroot, NULL);
	if (error)
		goto done;

	onode = cursor->node;
	nnode = hammer_alloc_btree(cursor->trans, 0, &error);

	if (nnode == NULL)
		goto done;

	/*
	 * Move the node.  Adjust the parent's pointer to us first.
	 */
	hammer_lock_ex(&nnode->lock);
	hammer_modify_node_noundo(cursor->trans, nnode);
	bcopy(onode->ondisk, nnode->ondisk, sizeof(*nnode->ondisk));

	if (elm) {
		/*
		 * We are not the root of the B-Tree 
		 */
		hammer_modify_node(cursor->trans, cursor->parent,
				   &elm->internal.subtree_offset,
				   sizeof(elm->internal.subtree_offset));
		elm->internal.subtree_offset = nnode->node_offset;
		hammer_modify_node_done(cursor->parent);
	} else {
		/*
		 * We are the root of the B-Tree
		 */
                hammer_volume_t volume;
                        
                volume = hammer_get_root_volume(cursor->trans->hmp, &error);
                KKASSERT(error == 0);

                hammer_modify_volume_field(cursor->trans, volume,
					   vol0_btree_root);
                volume->ondisk->vol0_btree_root = nnode->node_offset;
                hammer_modify_volume_done(volume);
                hammer_rel_volume(volume, 0);
        }

	/*
	 * Now adjust our children's pointers to us.
	 */
	for (i = 0; i < nnode->ondisk->count; ++i) {
		elm = &nnode->ondisk->elms[i];
		error = btree_set_parent(cursor->trans, nnode, elm);
		if (error)
			panic("reblock internal node: fixup problem");
	}

	/*
	 * Clean up.
	 *
	 * The new node replaces the current node in the cursor.  The cursor
	 * expects it to be locked so leave it locked.  Discard onode.
	 */
	hammer_cursor_replaced_node(onode, nnode);
	hammer_delete_node(cursor->trans, onode);

	if (hammer_debug_general & 0x4000) {
		kprintf("REBLOCK INODE %016llx -> %016llx\n",
			(long long)onode->node_offset,
			(long long)nnode->node_offset);
	}
	hammer_modify_node_done(nnode);
	cursor->node = nnode;

	hammer_unlock(&onode->lock);
	hammer_rel_node(onode);

done:
	hammer_btree_unlock_children(cursor->trans->hmp, &lockroot, NULL);
	return (error);
}

