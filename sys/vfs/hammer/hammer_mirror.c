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
 * $DragonFly: src/sys/vfs/hammer/hammer_mirror.c,v 1.7 2008/07/07 00:24:31 dillon Exp $
 */
/*
 * HAMMER mirroring ioctls - serialize and deserialize modifications made
 *			     to a filesystem.
 */

#include "hammer.h"

static int hammer_mirror_check(hammer_cursor_t cursor,
				struct hammer_ioc_mrecord *mrec);
static int hammer_mirror_update(hammer_cursor_t cursor,
				struct hammer_ioc_mrecord *mrec);
static int hammer_mirror_write(hammer_cursor_t cursor,
				struct hammer_ioc_mrecord *mrec,
				hammer_inode_t ip, char *udata);
static int hammer_mirror_localize_data(hammer_data_ondisk_t data,
				hammer_btree_leaf_elm_t leaf);

/*
 * All B-Tree records within the specified key range which also conform
 * to the transaction id range are returned.  Mirroring code keeps track
 * of the last transaction id fully scanned and can efficiently pick up
 * where it left off if interrupted.
 */
int
hammer_ioc_mirror_read(hammer_transaction_t trans, hammer_inode_t ip,
		       struct hammer_ioc_mirror_rw *mirror)
{
	struct hammer_cursor cursor;
	struct hammer_ioc_mrecord mrec;
	hammer_btree_leaf_elm_t elm;
	const int head_size = HAMMER_MREC_HEADSIZE;
	const int crc_start = HAMMER_MREC_CRCOFF;
	char *uptr;
	int error;
	int data_len;
	int bytes;

	if ((mirror->key_beg.localization | mirror->key_end.localization) &
	    HAMMER_LOCALIZE_PSEUDOFS_MASK) {
		return(EINVAL);
	}
	if (hammer_btree_cmp(&mirror->key_beg, &mirror->key_end) > 0)
		return(EINVAL);

	mirror->key_cur = mirror->key_beg;
	mirror->key_cur.localization += ip->obj_localization;
	bzero(&mrec, sizeof(mrec));

retry:
	error = hammer_init_cursor(trans, &cursor, NULL, NULL);
	if (error) {
		hammer_done_cursor(&cursor);
		goto failed;
	}
	cursor.key_beg = mirror->key_cur;
	cursor.key_end = mirror->key_end;
	cursor.key_end.localization += ip->obj_localization;

	cursor.flags |= HAMMER_CURSOR_END_INCLUSIVE;
	cursor.flags |= HAMMER_CURSOR_BACKEND;

	/*
	 * This flag filters the search to only return elements whos create
	 * or delete TID is >= mirror_tid.  The B-Tree uses the mirror_tid
	 * field stored with internal and leaf nodes to shortcut the scan.
	 */
	cursor.flags |= HAMMER_CURSOR_MIRROR_FILTERED;
	cursor.mirror_tid = mirror->tid_beg;

	error = hammer_btree_first(&cursor);
	while (error == 0) {
		/*
		 * Leaf node.  Only return elements modified in the range
		 * requested by userland.
		 */
		KKASSERT(cursor.node->ondisk->type == HAMMER_BTREE_TYPE_LEAF);
		elm = &cursor.node->ondisk->elms[cursor.index].leaf;

		if (elm->base.create_tid < mirror->tid_beg ||
		    elm->base.create_tid >= mirror->tid_end) {
			if (elm->base.delete_tid < mirror->tid_beg ||
			    elm->base.delete_tid >= mirror->tid_end) {
				goto skip;
			}
		}

		mirror->key_cur = elm->base;

		/*
		 * Yield to more important tasks
		 */
		if ((error = hammer_signal_check(trans->hmp)) != 0)
			break;
		if (trans->hmp->sync_lock.wanted) {
			tsleep(trans, 0, "hmrslo", hz / 10);
		}
		if (trans->hmp->locked_dirty_space +
		    trans->hmp->io_running_space > hammer_limit_dirtybufspace) {
			hammer_flusher_async(trans->hmp);
			tsleep(trans, 0, "hmrslo", hz / 10);
		}

		/*
		 * The core code exports the data to userland.
		 */
		data_len = (elm->data_offset) ? elm->data_len : 0;
		if (data_len) {
			error = hammer_btree_extract(&cursor,
						     HAMMER_CURSOR_GET_DATA);
			if (error)
				break;
		}
		bytes = sizeof(struct hammer_ioc_mrecord) + data_len;
		bytes = (bytes + HAMMER_HEAD_ALIGN_MASK) &
			~HAMMER_HEAD_ALIGN_MASK;
		if (mirror->count + bytes > mirror->size)
			break;

		/*
		 * Construct the record for userland and copyout.
		 *
		 * The user is asking for a snapshot, if the record was
		 * deleted beyond the user-requested ending tid, the record
		 * is not considered deleted from the point of view of
		 * userland and delete_tid is cleared.
		 */
		mrec.signature = HAMMER_IOC_MIRROR_SIGNATURE;
		mrec.type = HAMMER_MREC_TYPE_REC;
		mrec.rec_size = bytes;
		mrec.leaf = *elm;
		if (elm->base.delete_tid >= mirror->tid_end)
			mrec.leaf.base.delete_tid = 0;
		mrec.rec_crc = crc32(&mrec.rec_size, head_size - crc_start);
		uptr = (char *)mirror->ubuf + mirror->count;
		error = copyout(&mrec, uptr, head_size);
		if (data_len && error == 0) {
			error = copyout(cursor.data, uptr + head_size,
					data_len);
		}
		if (error == 0)
			mirror->count += bytes;
skip:
		if (error == 0) {
			cursor.flags |= HAMMER_CURSOR_ATEDISK;
			error = hammer_btree_iterate(&cursor);
		}
	}
	if (error == ENOENT) {
		mirror->key_cur = mirror->key_end;
		error = 0;
	}
	hammer_done_cursor(&cursor);
	if (error == EDEADLK)
		goto retry;
	if (error == EINTR) {
		mirror->head.flags |= HAMMER_IOC_HEAD_INTR;
		error = 0;
	}
failed:
	mirror->key_cur.localization &= HAMMER_LOCALIZE_MASK;
	return(error);
}

/*
 * Copy records from userland to the target mirror.  Records which already
 * exist may only have their delete_tid updated.
 *
 * The passed ip is the root ip of the pseudofs
 */
int
hammer_ioc_mirror_write(hammer_transaction_t trans, hammer_inode_t ip,
		       struct hammer_ioc_mirror_rw *mirror)
{
	struct hammer_cursor cursor;
	struct hammer_ioc_mrecord mrec;
	const int head_size = HAMMER_MREC_HEADSIZE;
	const int crc_start = HAMMER_MREC_CRCOFF;
	u_int32_t rec_crc;
	int error;
	char *uptr;

	if (mirror->size < 0 || mirror->size > 0x70000000)
		return(EINVAL);

	error = hammer_init_cursor(trans, &cursor, NULL, NULL);
retry:
	hammer_normalize_cursor(&cursor);

	while (error == 0 && mirror->count + head_size <= mirror->size) {
		/*
		 * Acquire and validate header
		 */
		uptr = (char *)mirror->ubuf + mirror->count;
		error = copyin(uptr, &mrec, head_size);
		if (error)
			break;
		rec_crc = crc32(&mrec.rec_size, head_size - crc_start);
		if (mrec.signature != HAMMER_IOC_MIRROR_SIGNATURE) {
			error = EINVAL;
			break;
		}
		if (mrec.type != HAMMER_MREC_TYPE_REC) {
			error = EINVAL;
			break;
		}
		if (rec_crc != mrec.rec_crc) {
			error = EINVAL;
			break;
		}
		if (mrec.rec_size < head_size ||
		    mrec.rec_size > head_size + HAMMER_XBUFSIZE + 16 ||
		    mirror->count + mrec.rec_size > mirror->size) {
			error = EINVAL;
			break;
		}
		if (mrec.leaf.data_len < 0 || 
		    mrec.leaf.data_len > HAMMER_XBUFSIZE ||
		    sizeof(struct hammer_ioc_mrecord) + mrec.leaf.data_len > mrec.rec_size) {
			error = EINVAL;
		}

		/*
		 * Re-localize for target.  relocalization of data is handled
		 * by hammer_mirror_write().
		 */
		mrec.leaf.base.localization &= HAMMER_LOCALIZE_MASK;
		mrec.leaf.base.localization += ip->obj_localization;

		/*
		 * Locate the record.
		 *
		 * If the record exists only the delete_tid may be updated.
		 *
		 * If the record does not exist we create it.  For now we
		 * ignore records with a non-zero delete_tid.  Note that
		 * mirror operations are effective an as-of operation and
		 * delete_tid can be 0 for mirroring purposes even if it is
		 * not actually 0 at the originator.
		 */
		hammer_normalize_cursor(&cursor);
		cursor.key_beg = mrec.leaf.base;
		cursor.flags |= HAMMER_CURSOR_BACKEND;
		cursor.flags &= ~HAMMER_CURSOR_INSERT;
		error = hammer_btree_lookup(&cursor);

		if (error == 0 && hammer_mirror_check(&cursor, &mrec)) {
			hammer_sync_lock_sh(trans);
			error = hammer_mirror_update(&cursor, &mrec);
			hammer_sync_unlock(trans);
		} else if (error == ENOENT && mrec.leaf.base.delete_tid == 0) {
			hammer_sync_lock_sh(trans);
			error = hammer_mirror_write(&cursor, &mrec, ip,
						    uptr + head_size);
			hammer_sync_unlock(trans);
		} else if (error == ENOENT) {
			error = 0;
		}

		/*
		 * Clean for loop.  It is ok if the record already exists
		 * on the target.
		 */
		if (error == EDEADLK) {
			hammer_done_cursor(&cursor);
			error = hammer_init_cursor(trans, &cursor, NULL, NULL);
			goto retry;
		}

		if (error == EALREADY)
			error = 0;
		if (error == 0)
			mirror->count += mrec.rec_size;
	}
	hammer_done_cursor(&cursor);
	return(0);
}

/*
 * Check whether an update is needed in the case where a match already
 * exists on the target.  The only type of update allowed in this case
 * is an update of the delete_tid.
 *
 * Return non-zero if the update should proceed.
 */
static
int
hammer_mirror_check(hammer_cursor_t cursor, struct hammer_ioc_mrecord *mrec)
{
	hammer_btree_leaf_elm_t leaf = cursor->leaf;

	if (leaf->base.delete_tid != mrec->leaf.base.delete_tid) {
		if (leaf->base.delete_tid != 0)
			return(1);
	}
	return(0);
}

/*
 * Update a record in-place.  Only the delete_tid can change.
 */
static
int
hammer_mirror_update(hammer_cursor_t cursor, struct hammer_ioc_mrecord *mrec)
{
	hammer_transaction_t trans;
	hammer_btree_leaf_elm_t elm;

	elm = cursor->leaf;
	trans = cursor->trans;

	if (mrec->leaf.base.delete_tid == 0) {
		kprintf("mirror_write: object %016llx:%016llx deleted on "
			"target, not deleted on source\n",
			elm->base.obj_id, elm->base.key);
		return(0);
	}

	KKASSERT(elm->base.create_tid < mrec->leaf.base.delete_tid);
	hammer_modify_node(trans, cursor->node, elm, sizeof(*elm));
	elm->base.delete_tid = mrec->leaf.base.delete_tid;
	elm->delete_ts = mrec->leaf.delete_ts;
	hammer_modify_node_done(cursor->node);

	/*
	 * Track a count of active inodes.
	 */
	if (elm->base.obj_type == HAMMER_RECTYPE_INODE) {
		hammer_modify_volume_field(trans,
					   trans->rootvol,
					   vol0_stat_inodes);
		--trans->hmp->rootvol->ondisk->vol0_stat_inodes;
		hammer_modify_volume_done(trans->rootvol);
	}

	return(0);
}

/*
 * Write out a new record.
 */
static
int
hammer_mirror_write(hammer_cursor_t cursor, struct hammer_ioc_mrecord *mrec,
		    hammer_inode_t ip, char *udata)
{
	hammer_transaction_t trans;
	hammer_buffer_t data_buffer;
	hammer_off_t ndata_offset;
	void *ndata;
	int error;
	int doprop;

	/*
	 * Skip records related to the root inode other then
	 * directory entries.
	 */
	if (mrec->leaf.base.obj_id == HAMMER_OBJID_ROOT) {
		if (mrec->leaf.base.rec_type == HAMMER_RECTYPE_INODE ||
		    mrec->leaf.base.rec_type == HAMMER_RECTYPE_FIX) {
			return(0);
		}
	}

	trans = cursor->trans;
	data_buffer = NULL;

	/*
	 * Allocate and adjust data
	 */
	if (mrec->leaf.data_len && mrec->leaf.data_offset) {
		ndata = hammer_alloc_data(trans, mrec->leaf.data_len,
					  mrec->leaf.base.rec_type,
					  &ndata_offset, &data_buffer, &error);
		if (ndata == NULL)
			return(error);
		mrec->leaf.data_offset = ndata_offset;
		hammer_modify_buffer(trans, data_buffer, NULL, 0);
		error = copyin(udata, ndata, mrec->leaf.data_len);
		if (error == 0) {
			if (hammer_crc_test_leaf(ndata, &mrec->leaf) == 0) {
				kprintf("data crc mismatch on pipe\n");
				error = EINVAL;
			} else {
				error = hammer_mirror_localize_data(
							ndata, &mrec->leaf);
			}
		}
		hammer_modify_buffer_done(data_buffer);
	} else {
		mrec->leaf.data_offset = 0;
		error = 0;
		ndata = NULL;
	}
	if (error)
		goto failed;

	/*
	 * Do the insertion
	 */
	cursor->flags |= HAMMER_CURSOR_INSERT;
	error = hammer_btree_lookup(cursor);
	if (error != ENOENT) {
		if (error == 0)
			error = EALREADY;
		goto failed;
	}
	error = 0;

	error = hammer_btree_insert(cursor, &mrec->leaf, &doprop);

	/*
	 * Track a count of active inodes.
	 */
	if (error == 0 && mrec->leaf.base.delete_tid == 0 &&
	    mrec->leaf.base.obj_type == HAMMER_RECTYPE_INODE) {
		hammer_modify_volume_field(trans,
					   trans->rootvol,
					   vol0_stat_inodes);
		++trans->hmp->rootvol->ondisk->vol0_stat_inodes;
		hammer_modify_volume_done(trans->rootvol);
	}
	if (error == 0 && doprop)
		hammer_btree_do_propagation(cursor, ip, &mrec->leaf);

failed:
	/*
	 * Cleanup
	 */
	if (error && mrec->leaf.data_offset) {
		hammer_blockmap_free(cursor->trans,
				     mrec->leaf.data_offset,
				     mrec->leaf.data_len);
	}
	if (data_buffer)
		hammer_rel_buffer(data_buffer, 0);
	return(error);
}

/*
 * Localize the data payload.  Directory entries may need their
 * localization adjusted.
 *
 * PFS directory entries must be skipped entirely (return EALREADY).
 */
static
int
hammer_mirror_localize_data(hammer_data_ondisk_t data,
			    hammer_btree_leaf_elm_t leaf)
{
	u_int32_t localization;

	if (leaf->base.rec_type == HAMMER_RECTYPE_DIRENTRY) {
		if (data->entry.obj_id == HAMMER_OBJID_ROOT)
			return(EALREADY);
		localization = leaf->base.localization &
			       HAMMER_LOCALIZE_PSEUDOFS_MASK;
		if (data->entry.localization != localization) {
			data->entry.localization = localization;
			hammer_crc_set_leaf(data, leaf);
		}
	}
	return(0);
}

/*
 * Set mirroring/pseudo-fs information
 */
int
hammer_ioc_set_pseudofs(hammer_transaction_t trans, hammer_inode_t ip,
			struct hammer_ioc_pseudofs_rw *pfs)
{
	hammer_pseudofs_inmem_t pfsm;
	int error;

	pfsm = ip->pfsm;
	error = 0;

	if (pfs->pseudoid != ip->obj_localization)
		error = EINVAL;
	if (pfs->bytes != sizeof(pfsm->pfsd))
		error = EINVAL;
	if (pfs->version != HAMMER_IOC_PSEUDOFS_VERSION)
		error = EINVAL;
	if (error == 0 && pfs->ondisk) {
		if (ip->obj_id != HAMMER_OBJID_ROOT)
			error = EINVAL;
		if (error == 0) {
			error = copyin(pfs->ondisk, &ip->pfsm->pfsd,
				       sizeof(ip->pfsm->pfsd));
		}
		if (error == 0)
			error = hammer_save_pseudofs(trans, ip);
	}
	return(error);
}

/*
 * Get mirroring/pseudo-fs information
 */
int
hammer_ioc_get_pseudofs(hammer_transaction_t trans, hammer_inode_t ip,
			struct hammer_ioc_pseudofs_rw *pfs)
{
	hammer_pseudofs_inmem_t pfsm;
	int error;

	pfs->pseudoid = ip->obj_localization;
	pfs->bytes = sizeof(struct hammer_pseudofs_data);
	pfs->version = HAMMER_IOC_PSEUDOFS_VERSION;

	/*
	 * Update pfsm->sync_end_tid if a master
	 */
	pfsm = ip->pfsm;
	if (pfsm->pfsd.master_id >= 0)
		pfsm->pfsd.sync_end_tid = trans->rootvol->ondisk->vol0_next_tid;

	/*
	 * Return PFS information for root inodes only.
	 */
	error = 0;
	if (pfs->ondisk) {
		if (ip->obj_id != HAMMER_OBJID_ROOT)
			error = EINVAL;
		if (error == 0) {
			error = copyout(&ip->pfsm->pfsd, pfs->ondisk,
					sizeof(ip->pfsm->pfsd));
		}
	}
	return(error);
}

