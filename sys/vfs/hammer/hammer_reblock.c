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
 * $DragonFly: src/sys/vfs/hammer/hammer_reblock.c,v 1.3 2008/03/19 20:18:17 dillon Exp $
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
static int hammer_reblock_record(struct hammer_ioc_reblock *reblock,
				hammer_cursor_t cursor, hammer_btree_elm_t elm);
static int hammer_reblock_node(struct hammer_ioc_reblock *reblock,
				hammer_cursor_t cursor, hammer_btree_elm_t elm);

int
hammer_ioc_reblock(hammer_transaction_t trans, hammer_inode_t ip,
	       struct hammer_ioc_reblock *reblock)
{
	struct hammer_cursor cursor;
	hammer_btree_elm_t elm;
	int error;

	if (reblock->beg_obj_id >= reblock->end_obj_id)
		return(EINVAL);
	if (reblock->free_level < 0)
		return(EINVAL);

retry:
	error = hammer_init_cursor(trans, &cursor, NULL);
	if (error) {
		hammer_done_cursor(&cursor);
		return(error);
	}
	cursor.key_beg.obj_id = reblock->cur_obj_id;
	cursor.key_beg.key = HAMMER_MIN_KEY;
	cursor.key_beg.create_tid = 1;
	cursor.key_beg.delete_tid = 0;
	cursor.key_beg.rec_type = HAMMER_MIN_RECTYPE;
	cursor.key_beg.obj_type = 0;

	cursor.key_end.obj_id = reblock->end_obj_id;
	cursor.key_end.key = HAMMER_MAX_KEY;
	cursor.key_end.create_tid = HAMMER_MAX_TID - 1;
	cursor.key_end.delete_tid = 0;
	cursor.key_end.rec_type = HAMMER_MAX_RECTYPE;
	cursor.key_end.obj_type = 0;

	cursor.flags |= HAMMER_CURSOR_END_INCLUSIVE;

	error = hammer_btree_first(&cursor);
	while (error == 0) {
		elm = &cursor.node->ondisk->elms[cursor.index];
		reblock->cur_obj_id = elm->base.obj_id;

		error = hammer_reblock_helper(reblock, &cursor, elm);
		if (error == 0) {
			cursor.flags |= HAMMER_CURSOR_ATEDISK;
			error = hammer_btree_iterate(&cursor);
		}
	}
	if (error == ENOENT)
		error = 0;
	hammer_done_cursor(&cursor);
	if (error == EDEADLK)
		goto retry;
	return(error);
}

/*
 * Reblock the B-Tree (leaf) node, record, and/or data if necessary.
 *
 * XXX We have no visibility into internal B-Tree nodes at the moment.
 */
static int
hammer_reblock_helper(struct hammer_ioc_reblock *reblock,
		      hammer_cursor_t cursor, hammer_btree_elm_t elm)
{
	hammer_off_t tmp_offset;
	int error;
	int zone;
	int bytes;
	int cur;

	if (elm->leaf.base.btype != HAMMER_BTREE_TYPE_RECORD)
		return(0);
	error = 0;

	/*
	 * Reblock data.  Note that data embedded in a record is reblocked
	 * by the record reblock code.
	 */
	tmp_offset = elm->leaf.data_offset;
	zone = HAMMER_ZONE_DECODE(tmp_offset);		/* can be 0 */
	if ((zone == HAMMER_ZONE_SMALL_DATA_INDEX ||
	     zone == HAMMER_ZONE_LARGE_DATA_INDEX) && error == 0) {
		++reblock->data_count;
		reblock->data_byte_count += elm->leaf.data_len;
		bytes = hammer_blockmap_getfree(cursor->trans->hmp, tmp_offset,
						&cur, &error);
		if (error == 0 && cur == 0 && bytes > reblock->free_level) {
			kprintf("%6d ", bytes);
			error = hammer_cursor_upgrade(cursor);
			if (error == 0) {
				error = hammer_reblock_data(reblock,
							    cursor, elm);
			}
			if (error == 0) {
				++reblock->data_moves;
				reblock->data_byte_moves += elm->leaf.data_len;
			}
		}
	}

	/*
	 * Reblock a record
	 */
	tmp_offset = elm->leaf.rec_offset;
	zone = HAMMER_ZONE_DECODE(tmp_offset);
	if (zone == HAMMER_ZONE_RECORD_INDEX && error == 0) {
		++reblock->record_count;
		bytes = hammer_blockmap_getfree(cursor->trans->hmp, tmp_offset,
						&cur, &error);
		if (error == 0 && cur == 0 && bytes > reblock->free_level) {
			kprintf("%6d ", bytes);
			error = hammer_cursor_upgrade(cursor);
			if (error == 0) {
				error = hammer_reblock_record(reblock,
							      cursor, elm);
			}
			if (error == 0) {
				++reblock->record_moves;
			}
		}
	}

	/*
	 * Reblock a B-Tree node.  Adjust elm to point at the parent's
	 * leaf entry.
	 */
	tmp_offset = cursor->node->node_offset;
	zone = HAMMER_ZONE_DECODE(tmp_offset);
	if (zone == HAMMER_ZONE_BTREE_INDEX && error == 0 &&
	    cursor->index == 0) {
		++reblock->btree_count;
		bytes = hammer_blockmap_getfree(cursor->trans->hmp, tmp_offset,
						&cur, &error);
		if (error == 0 && cur == 0 && bytes > reblock->free_level) {
			kprintf("%6d ", bytes);
			error = hammer_cursor_upgrade(cursor);
			if (error == 0) {
				if (cursor->parent)
					elm = &cursor->parent->ondisk->elms[cursor->parent_index];
				else
					elm = NULL;
				error = hammer_reblock_node(reblock,
							    cursor, elm);
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
					     HAMMER_CURSOR_GET_RECORD);
	if (error)
		return (error);
	ndata = hammer_alloc_data(cursor->trans, elm->leaf.data_len,
				  &ndata_offset, &data_buffer, &error);
	if (error)
		goto done;

	/*
	 * Move the data
	 */
	bcopy(cursor->data, ndata, elm->leaf.data_len);
	hammer_modify_node(cursor->trans, cursor->node,
			   &elm->leaf.data_offset, sizeof(hammer_off_t));
	hammer_modify_record(cursor->trans, cursor->record_buffer,
			     &cursor->record->base.data_off,
			     sizeof(hammer_off_t));

	hammer_blockmap_free(cursor->trans,
			     elm->leaf.data_offset, elm->leaf.data_len);

	cursor->record->base.data_off = ndata_offset;
	elm->leaf.data_offset = ndata_offset;

done:
	if (data_buffer)
		hammer_rel_buffer(data_buffer, 0);
	return (error);
}

/*
 * Reblock a record.  The B-Tree must be adjusted to point to the new record
 * and the existing record must be physically destroyed so a FS rebuild
 * does not see two versions of the same record.
 */
static int
hammer_reblock_record(struct hammer_ioc_reblock *reblock,
		      hammer_cursor_t cursor, hammer_btree_elm_t elm)
{
	struct hammer_buffer *rec_buffer = NULL;
	hammer_off_t nrec_offset;
	hammer_off_t ndata_offset;
	hammer_record_ondisk_t orec;
	hammer_record_ondisk_t nrec;
	int error;
	int inline_data;

	error = hammer_btree_extract(cursor, HAMMER_CURSOR_GET_RECORD);
	if (error)
		return (error);

	nrec = hammer_alloc_record(cursor->trans, &nrec_offset,
				   elm->leaf.base.rec_type, &rec_buffer,
				   0, NULL, NULL, &error);
	if (error)
		goto done;

	/*
	 * Move the record.  Check for an inline data reference and move that
	 * too if necessary.
	 */
	orec = cursor->record;
	bcopy(orec, nrec, sizeof(*nrec));

	if ((orec->base.data_off & HAMMER_OFF_ZONE_MASK) == HAMMER_ZONE_RECORD) {
		ndata_offset = orec->base.data_off - elm->leaf.rec_offset;
		KKASSERT(ndata_offset < sizeof(*nrec));
		ndata_offset += nrec_offset;
		inline_data = 1;
	} else {
		ndata_offset = 0;
		inline_data = 0;
	}

	hammer_modify_record(cursor->trans, cursor->record_buffer,
			     &orec->base.base.rec_type,
			     sizeof(orec->base.base.rec_type));
	orec->base.base.rec_type |= HAMMER_RECTYPE_MOVED;

	hammer_blockmap_free(cursor->trans,
			     elm->leaf.rec_offset, sizeof(*nrec));

	kprintf("REBLOCK RECD %016llx -> %016llx\n",
		elm->leaf.rec_offset, nrec_offset);

	hammer_modify_node(cursor->trans, cursor->node,
			   &elm->leaf.rec_offset, sizeof(hammer_off_t));
	elm->leaf.rec_offset = nrec_offset;
	if (inline_data) {
		hammer_modify_node(cursor->trans, cursor->node,
				 &elm->leaf.data_offset, sizeof(hammer_off_t));
		elm->leaf.data_offset = ndata_offset;
	}

done:
	if (rec_buffer)
		hammer_rel_buffer(rec_buffer, 0);
	return (error);
}

/*
 * Reblock a B-Tree (leaf) node.  The parent must be adjusted to point to
 * the new copy of the leaf node.  elm is a pointer to the parent element
 * pointing at cursor.node.
 *
 * XXX reblock internal nodes too.
 */
static int
hammer_reblock_node(struct hammer_ioc_reblock *reblock,
		    hammer_cursor_t cursor, hammer_btree_elm_t elm)
{
	hammer_node_t onode;
	hammer_node_t nnode;
	int error;

	onode = cursor->node;
	nnode = hammer_alloc_btree(cursor->trans, &error);
	hammer_lock_ex(&nnode->lock);

	if (nnode == NULL)
		return (error);

	/*
	 * Move the node
	 */
	bcopy(onode->ondisk, nnode->ondisk, sizeof(*nnode->ondisk));

	if (elm) {
		/*
		 * We are not the root of the B-Tree 
		 */
		hammer_modify_node(cursor->trans, cursor->parent,
				   &elm->internal.subtree_offset,
				   sizeof(elm->internal.subtree_offset));
		elm->internal.subtree_offset = nnode->node_offset;
	} else {
		/*
		 * We are the root of the B-Tree
		 */
                hammer_volume_t volume;
                        
                volume = hammer_get_root_volume(cursor->trans->hmp, &error);
                KKASSERT(error == 0);

                hammer_modify_volume(cursor->trans, volume,
				     &volume->ondisk->vol0_btree_root,
                                     sizeof(hammer_off_t));
                volume->ondisk->vol0_btree_root = nnode->node_offset;
                hammer_rel_volume(volume, 0);
        }

	hammer_delete_node(cursor->trans, onode);

	kprintf("REBLOCK NODE %016llx -> %016llx\n",
		onode->node_offset, nnode->node_offset);

	cursor->node = nnode;
	hammer_rel_node(onode);

	return (error);
}

