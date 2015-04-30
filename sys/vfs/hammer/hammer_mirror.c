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
 */
/*
 * HAMMER mirroring ioctls - serialize and deserialize modifications made
 *			     to a filesystem.
 */

#include "hammer.h"

static int hammer_mirror_check(hammer_cursor_t cursor,
				struct hammer_ioc_mrecord_rec *mrec);
static int hammer_mirror_update(hammer_cursor_t cursor,
				struct hammer_ioc_mrecord_rec *mrec);
static int hammer_ioc_mirror_write_rec(hammer_cursor_t cursor,
				struct hammer_ioc_mrecord_rec *mrec,
				struct hammer_ioc_mirror_rw *mirror,
				u_int32_t localization,
				char *uptr);
static int hammer_ioc_mirror_write_pass(hammer_cursor_t cursor,
				struct hammer_ioc_mrecord_rec *mrec,
				struct hammer_ioc_mirror_rw *mirror,
				u_int32_t localization);
static int hammer_ioc_mirror_write_skip(hammer_cursor_t cursor,
				struct hammer_ioc_mrecord_skip *mrec,
				struct hammer_ioc_mirror_rw *mirror,
				u_int32_t localization);
static int hammer_mirror_delete_to(hammer_cursor_t cursor,
			        struct hammer_ioc_mirror_rw *mirror);
static int hammer_mirror_nomirror(struct hammer_base_elm *base);

/*
 * All B-Tree records within the specified key range which also conform
 * to the transaction id range are returned.  Mirroring code keeps track
 * of the last transaction id fully scanned and can efficiently pick up
 * where it left off if interrupted.
 *
 * The PFS is identified in the mirror structure.  The passed ip is just
 * some directory in the overall HAMMER filesystem and has nothing to
 * do with the PFS.
 */
int
hammer_ioc_mirror_read(hammer_transaction_t trans, hammer_inode_t ip,
		       struct hammer_ioc_mirror_rw *mirror)
{
	struct hammer_cmirror cmirror;
	struct hammer_cursor cursor;
	union hammer_ioc_mrecord_any mrec;
	hammer_btree_leaf_elm_t elm;
	const int crc_start = HAMMER_MREC_CRCOFF;
	char *uptr;
	int error;
	int data_len;
	int bytes;
	int eatdisk;
	int mrec_flags;
	u_int32_t localization;
	u_int32_t rec_crc;

	localization = (u_int32_t)mirror->pfs_id << 16;

	if ((mirror->key_beg.localization | mirror->key_end.localization) &
	    HAMMER_LOCALIZE_PSEUDOFS_MASK) {
		return(EINVAL);
	}
	if (hammer_btree_cmp(&mirror->key_beg, &mirror->key_end) > 0)
		return(EINVAL);

	mirror->key_cur = mirror->key_beg;
	mirror->key_cur.localization &= HAMMER_LOCALIZE_MASK;
	mirror->key_cur.localization += localization;
	bzero(&mrec, sizeof(mrec));
	bzero(&cmirror, sizeof(cmirror));

	/*
	 * Make CRC errors non-fatal (at least on data), causing an EDOM
	 * error instead of EIO.
	 */
	trans->flags |= HAMMER_TRANSF_CRCDOM;

retry:
	error = hammer_init_cursor(trans, &cursor, NULL, NULL);
	if (error) {
		hammer_done_cursor(&cursor);
		goto failed;
	}
	cursor.key_beg = mirror->key_cur;
	cursor.key_end = mirror->key_end;
	cursor.key_end.localization &= HAMMER_LOCALIZE_MASK;
	cursor.key_end.localization += localization;

	cursor.flags |= HAMMER_CURSOR_END_INCLUSIVE;
	cursor.flags |= HAMMER_CURSOR_BACKEND;

	/*
	 * This flag filters the search to only return elements whos create
	 * or delete TID is >= mirror_tid.  The B-Tree uses the mirror_tid
	 * field stored with internal and leaf nodes to shortcut the scan.
	 */
	cursor.flags |= HAMMER_CURSOR_MIRROR_FILTERED;
	cursor.cmirror = &cmirror;
	cmirror.mirror_tid = mirror->tid_beg;

	error = hammer_btree_first(&cursor);
	while (error == 0) {
		/*
		 * Yield to more important tasks
		 */
		if (error == 0) {
			error = hammer_signal_check(trans->hmp);
			if (error)
				break;
		}

		/*
		 * An internal node can be returned in mirror-filtered
		 * mode and indicates that the scan is returning a skip
		 * range in the cursor->cmirror structure.
		 */
		uptr = (char *)mirror->ubuf + mirror->count;
		if (cursor.node->ondisk->type == HAMMER_BTREE_TYPE_INTERNAL) {
			/*
			 * Check space
			 */
			mirror->key_cur = cmirror.skip_beg;
			bytes = sizeof(mrec.skip);
			if (mirror->count + HAMMER_HEAD_DOALIGN(bytes) >
			    mirror->size) {
				break;
			}

			/*
			 * Fill mrec
			 */
			mrec.head.signature = HAMMER_IOC_MIRROR_SIGNATURE;
			mrec.head.type = HAMMER_MREC_TYPE_SKIP;
			mrec.head.rec_size = bytes;
			mrec.skip.skip_beg = cmirror.skip_beg;
			mrec.skip.skip_end = cmirror.skip_end;
			mrec.head.rec_crc = crc32(&mrec.head.rec_size,
						 bytes - crc_start);
			error = copyout(&mrec, uptr, bytes);
			eatdisk = 0;
			goto didwrite;
		}

		/*
		 * Leaf node.  In full-history mode we could filter out
		 * elements modified outside the user-requested TID range.
		 *
		 * However, such elements must be returned so the writer
		 * can compare them against the target to determine what
		 * needs to be deleted on the target, particular for
		 * no-history mirrors.
		 */
		KKASSERT(cursor.node->ondisk->type == HAMMER_BTREE_TYPE_LEAF);
		elm = &cursor.node->ondisk->elms[cursor.index].leaf;
		mirror->key_cur = elm->base;

		/*
		 * If the record was created after our end point we just
		 * ignore it.
		 */
		if (elm->base.create_tid > mirror->tid_end) {
			error = 0;
			bytes = 0;
			eatdisk = 1;
			goto didwrite;
		}

		/*
		 * Determine if we should generate a PASS or a REC.  PASS
		 * records are records without any data payload.  Such
		 * records will be generated if the target is already expected
		 * to have the record, allowing it to delete the gaps.
		 *
		 * A PASS record is also used to perform deletions on the
		 * target.
		 *
		 * Such deletions are needed if the master or files on the
		 * master are no-history, or if the slave is so far behind
		 * the master has already been pruned.
		 */
		if (elm->base.create_tid < mirror->tid_beg) {
			bytes = sizeof(mrec.rec);
			if (mirror->count + HAMMER_HEAD_DOALIGN(bytes) >
			    mirror->size) {
				break;
			}

			/*
			 * Fill mrec.
			 */
			mrec.head.signature = HAMMER_IOC_MIRROR_SIGNATURE;
			mrec.head.type = HAMMER_MREC_TYPE_PASS;
			mrec.head.rec_size = bytes;
			mrec.rec.leaf = *elm;
			mrec.head.rec_crc = crc32(&mrec.head.rec_size,
						 bytes - crc_start);
			error = copyout(&mrec, uptr, bytes);
			eatdisk = 1;
			goto didwrite;
			
		}

		/*
		 * The core code exports the data to userland.
		 *
		 * CRC errors on data are reported but passed through,
		 * but the data must be washed by the user program.
		 *
		 * If userland just wants the btree records it can
		 * request that bulk data not be returned.  This is
		 * use during mirror-stream histogram generation.
		 */
		mrec_flags = 0;
		data_len = (elm->data_offset) ? elm->data_len : 0;
		if (data_len &&
		    (mirror->head.flags & HAMMER_IOC_MIRROR_NODATA)) {
			data_len = 0;
			mrec_flags |= HAMMER_MRECF_NODATA;
		}
		if (data_len) {
			error = hammer_btree_extract(&cursor,
						     HAMMER_CURSOR_GET_DATA);
			if (error) {
				if (error != EDOM)
					break;
				mrec_flags |= HAMMER_MRECF_CRC_ERROR |
					      HAMMER_MRECF_DATA_CRC_BAD;
			}
		}

		bytes = sizeof(mrec.rec) + data_len;
		if (mirror->count + HAMMER_HEAD_DOALIGN(bytes) > mirror->size)
			break;

		/*
		 * Construct the record for userland and copyout.
		 *
		 * The user is asking for a snapshot, if the record was
		 * deleted beyond the user-requested ending tid, the record
		 * is not considered deleted from the point of view of
		 * userland and delete_tid is cleared.
		 */
		mrec.head.signature = HAMMER_IOC_MIRROR_SIGNATURE;
		mrec.head.type = HAMMER_MREC_TYPE_REC | mrec_flags;
		mrec.head.rec_size = bytes;
		mrec.rec.leaf = *elm;

		if (elm->base.delete_tid > mirror->tid_end)
			mrec.rec.leaf.base.delete_tid = 0;
		rec_crc = crc32(&mrec.head.rec_size,
				sizeof(mrec.rec) - crc_start);
		if (data_len)
			rec_crc = crc32_ext(cursor.data, data_len, rec_crc);
		mrec.head.rec_crc = rec_crc;
		error = copyout(&mrec, uptr, sizeof(mrec.rec));
		if (data_len && error == 0) {
			error = copyout(cursor.data, uptr + sizeof(mrec.rec),
					data_len);
		}
		eatdisk = 1;

		/*
		 * eatdisk controls whether we skip the current cursor
		 * position on the next scan or not.  If doing a SKIP
		 * the cursor is already positioned properly for the next
		 * scan and eatdisk will be 0.
		 */
didwrite:
		if (error == 0) {
			mirror->count += HAMMER_HEAD_DOALIGN(bytes);
			if (eatdisk)
				cursor.flags |= HAMMER_CURSOR_ATEDISK;
			else
				cursor.flags &= ~HAMMER_CURSOR_ATEDISK;
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
 * Copy records from userland to the target mirror.
 *
 * The PFS is identified in the mirror structure.  The passed ip is just
 * some directory in the overall HAMMER filesystem and has nothing to
 * do with the PFS.  In fact, there might not even be a root directory for
 * the PFS yet!
 */
int
hammer_ioc_mirror_write(hammer_transaction_t trans, hammer_inode_t ip,
		       struct hammer_ioc_mirror_rw *mirror)
{
	union hammer_ioc_mrecord_any mrec;
	struct hammer_cursor cursor;
	u_int32_t localization;
	int checkspace_count = 0;
	int error;
	int bytes;
	char *uptr;
	int seq;

	localization = (u_int32_t)mirror->pfs_id << 16;
	seq = trans->hmp->flusher.done;

	/*
	 * Validate the mirror structure and relocalize the tracking keys.
	 */
	if (mirror->size < 0 || mirror->size > 0x70000000)
		return(EINVAL);
	mirror->key_beg.localization &= HAMMER_LOCALIZE_MASK;
	mirror->key_beg.localization += localization;
	mirror->key_end.localization &= HAMMER_LOCALIZE_MASK;
	mirror->key_end.localization += localization;
	mirror->key_cur.localization &= HAMMER_LOCALIZE_MASK;
	mirror->key_cur.localization += localization;

	/*
	 * Set up our tracking cursor for the loop.  The tracking cursor
	 * is used to delete records that are no longer present on the
	 * master.  The last handled record at key_cur must be skipped.
	 */
	error = hammer_init_cursor(trans, &cursor, NULL, NULL);

	cursor.key_beg = mirror->key_cur;
	cursor.key_end = mirror->key_end;
	cursor.flags |= HAMMER_CURSOR_BACKEND;
	error = hammer_btree_first(&cursor);
	if (error == 0)
		cursor.flags |= HAMMER_CURSOR_ATEDISK;
	if (error == ENOENT)
		error = 0;

	/*
	 * Loop until our input buffer has been exhausted.
	 */
	while (error == 0 &&
		mirror->count + sizeof(mrec.head) <= mirror->size) {

	        /*
		 * Don't blow out the buffer cache.  Leave room for frontend
		 * cache as well.
		 *
		 * WARNING: See warnings in hammer_unlock_cursor() function.
		 */
		while (hammer_flusher_meta_halflimit(trans->hmp) ||
		       hammer_flusher_undo_exhausted(trans, 2)) {
			hammer_unlock_cursor(&cursor);
			hammer_flusher_wait(trans->hmp, seq);
			hammer_lock_cursor(&cursor);
			seq = hammer_flusher_async_one(trans->hmp);
		}

		/*
		 * If there is insufficient free space it may be due to
		 * reserved bigblocks, which flushing might fix.
		 */
		if (hammer_checkspace(trans->hmp, HAMMER_CHKSPC_MIRROR)) {
			if (++checkspace_count == 10) {
				error = ENOSPC;
				break;
			}
			hammer_unlock_cursor(&cursor);
			hammer_flusher_wait(trans->hmp, seq);
			hammer_lock_cursor(&cursor);
			seq = hammer_flusher_async(trans->hmp, NULL);
		}


		/*
		 * Acquire and validate header
		 */
		if ((bytes = mirror->size - mirror->count) > sizeof(mrec))
			bytes = sizeof(mrec);
		uptr = (char *)mirror->ubuf + mirror->count;
		error = copyin(uptr, &mrec, bytes);
		if (error)
			break;
		if (mrec.head.signature != HAMMER_IOC_MIRROR_SIGNATURE) {
			error = EINVAL;
			break;
		}
		if (mrec.head.rec_size < sizeof(mrec.head) ||
		    mrec.head.rec_size > sizeof(mrec) + HAMMER_XBUFSIZE ||
		    mirror->count + mrec.head.rec_size > mirror->size) {
			error = EINVAL;
			break;
		}

		switch(mrec.head.type & HAMMER_MRECF_TYPE_MASK) {
		case HAMMER_MREC_TYPE_SKIP:
			if (mrec.head.rec_size != sizeof(mrec.skip))
				error = EINVAL;
			if (error == 0)
				error = hammer_ioc_mirror_write_skip(&cursor, &mrec.skip, mirror, localization);
			break;
		case HAMMER_MREC_TYPE_REC:
			if (mrec.head.rec_size < sizeof(mrec.rec))
				error = EINVAL;
			if (error == 0)
				error = hammer_ioc_mirror_write_rec(&cursor, &mrec.rec, mirror, localization, uptr + sizeof(mrec.rec));
			break;
		case HAMMER_MREC_TYPE_REC_NODATA:
		case HAMMER_MREC_TYPE_REC_BADCRC:
			/*
			 * Records with bad data payloads are ignored XXX.
			 * Records with no data payload have to be skipped
			 * (they shouldn't have been written in the first
			 * place).
			 */
			if (mrec.head.rec_size < sizeof(mrec.rec))
				error = EINVAL;
			break;
		case HAMMER_MREC_TYPE_PASS:
			if (mrec.head.rec_size != sizeof(mrec.rec))
				error = EINVAL;
			if (error == 0)
				error = hammer_ioc_mirror_write_pass(&cursor, &mrec.rec, mirror, localization);
			break;
		default:
			error = EINVAL;
			break;
		}

		/*
		 * Retry the current record on deadlock, otherwise setup
		 * for the next loop.
		 */
		if (error == EDEADLK) {
			while (error == EDEADLK) {
				hammer_sync_lock_sh(trans);
				hammer_recover_cursor(&cursor);
				error = hammer_cursor_upgrade(&cursor);
				hammer_sync_unlock(trans);
			}
		} else {
			if (error == EALREADY)
				error = 0;
			if (error == 0) {
				mirror->count += 
					HAMMER_HEAD_DOALIGN(mrec.head.rec_size);
			}
		}
	}
	hammer_done_cursor(&cursor);

	/*
	 * cumulative error 
	 */
	if (error) {
		mirror->head.flags |= HAMMER_IOC_HEAD_ERROR;
		mirror->head.error = error;
	}

	/*
	 * ioctls don't update the RW data structure if an error is returned,
	 * always return 0.
	 */
	return(0);
}

/*
 * Handle skip records.
 *
 * We must iterate from the last resolved record position at mirror->key_cur
 * to skip_beg non-inclusive and delete any records encountered.
 *
 * mirror->key_cur must be carefully set when we succeed in processing
 * this mrec.
 */
static int
hammer_ioc_mirror_write_skip(hammer_cursor_t cursor,
			     struct hammer_ioc_mrecord_skip *mrec,
			     struct hammer_ioc_mirror_rw *mirror,
			     u_int32_t localization)
{
	int error;

	/*
	 * Relocalize the skip range
	 */
	mrec->skip_beg.localization &= HAMMER_LOCALIZE_MASK;
	mrec->skip_beg.localization += localization;
	mrec->skip_end.localization &= HAMMER_LOCALIZE_MASK;
	mrec->skip_end.localization += localization;

	/*
	 * Iterate from current position to skip_beg, deleting any records
	 * we encounter.  The record at skip_beg is not included (it is
	 * skipped).
	 */
	cursor->key_end = mrec->skip_beg;
	cursor->flags &= ~HAMMER_CURSOR_END_INCLUSIVE;
	cursor->flags |= HAMMER_CURSOR_BACKEND;
	error = hammer_mirror_delete_to(cursor, mirror);

	/*
	 * Now skip past the skip (which is the whole point point of
	 * having a skip record).  The sender has not sent us any records
	 * for the skip area so we wouldn't know what to keep and what
	 * to delete anyway.
	 *
	 * Clear ATEDISK because skip_end is non-inclusive, so we can't
	 * count an exact match if we happened to get one.
	 */
	if (error == 0) {
		mirror->key_cur = mrec->skip_end;
		cursor->key_beg = mrec->skip_end;
		error = hammer_btree_lookup(cursor);
		cursor->flags &= ~HAMMER_CURSOR_ATEDISK;
		if (error == ENOENT)
			error = 0;
	}
	return(error);
}

/*
 * Handle B-Tree records.
 *
 * We must iterate to mrec->base.key (non-inclusively), and then process
 * the record.  We are allowed to write a new record or delete an existing
 * record, but cannot replace an existing record.
 *
 * mirror->key_cur must be carefully set when we succeed in processing
 * this mrec.
 */
static int
hammer_ioc_mirror_write_rec(hammer_cursor_t cursor,
			    struct hammer_ioc_mrecord_rec *mrec,
			    struct hammer_ioc_mirror_rw *mirror,
			    u_int32_t localization,
			    char *uptr)
{
	int error;

	if (mrec->leaf.data_len < 0 || 
	    mrec->leaf.data_len > HAMMER_XBUFSIZE ||
	    mrec->leaf.data_len + sizeof(*mrec) > mrec->head.rec_size) {
		return(EINVAL);
	}

	/*
	 * Re-localize for target.  relocalization of data is handled
	 * by hammer_mirror_write().
	 */
	mrec->leaf.base.localization &= HAMMER_LOCALIZE_MASK;
	mrec->leaf.base.localization += localization;

	/*
	 * Delete records through until we reach (non-inclusively) the
	 * target record.
	 */
	cursor->key_end = mrec->leaf.base;
	cursor->flags &= ~HAMMER_CURSOR_END_INCLUSIVE;
	cursor->flags |= HAMMER_CURSOR_BACKEND;
	error = hammer_mirror_delete_to(cursor, mirror);

	/*
	 * Certain records are not part of the mirroring operation
	 */
	if (error == 0 && hammer_mirror_nomirror(&mrec->leaf.base))
		return(0);

	/*
	 * Locate the record.
	 *
	 * If the record exists only the delete_tid may be updated.
	 *
	 * If the record does not exist we can create it only if the
	 * create_tid is not too old.  If the create_tid is too old
	 * it may have already been destroyed on the slave from pruning.
	 *
	 * Note that mirror operations are effectively as-of operations
	 * and delete_tid can be 0 for mirroring purposes even if it is
	 * not actually 0 at the originator.
	 *
	 * These functions can return EDEADLK
	 */
	if (error == 0) {
		cursor->key_beg = mrec->leaf.base;
		cursor->flags |= HAMMER_CURSOR_BACKEND;
		cursor->flags &= ~HAMMER_CURSOR_INSERT;
		error = hammer_btree_lookup(cursor);
	}

	if (error == 0 && hammer_mirror_check(cursor, mrec)) {
		error = hammer_mirror_update(cursor, mrec);
	} else if (error == ENOENT) {
		if (mrec->leaf.base.create_tid >= mirror->tid_beg) {
			error = hammer_create_at_cursor(
					cursor, &mrec->leaf,
					uptr, HAMMER_CREATE_MODE_UMIRROR);
		} else {
			error = 0;
		}
	}
	if (error == 0 || error == EALREADY)
		mirror->key_cur = mrec->leaf.base;
	return(error);
}

/*
 * This works like write_rec but no write or update is necessary,
 * and no data payload is included so we couldn't do a write even
 * if we wanted to.
 *
 * We must still iterate for deletions, and we can validate the
 * record header which is a good way to test for corrupted mirror
 * targets XXX.
 *
 * mirror->key_cur must be carefully set when we succeed in processing
 * this mrec.
 */
static
int
hammer_ioc_mirror_write_pass(hammer_cursor_t cursor,
			     struct hammer_ioc_mrecord_rec *mrec,
			     struct hammer_ioc_mirror_rw *mirror,
			     u_int32_t localization)
{
	int error;

	/*
	 * Re-localize for target.  Relocalization of data is handled
	 * by hammer_mirror_write().
	 */
	mrec->leaf.base.localization &= HAMMER_LOCALIZE_MASK;
	mrec->leaf.base.localization += localization;

	/*
	 * Delete records through until we reach (non-inclusively) the
	 * target record.
	 */
	cursor->key_end = mrec->leaf.base;
	cursor->flags &= ~HAMMER_CURSOR_END_INCLUSIVE;
	cursor->flags |= HAMMER_CURSOR_BACKEND;
	error = hammer_mirror_delete_to(cursor, mirror);

	/*
	 * Certain records are not part of the mirroring operation
	 */
	if (hammer_mirror_nomirror(&mrec->leaf.base))
		return(0);

	/*
	 * Locate the record and get past it by setting ATEDISK.  Perform
	 * any necessary deletions.  We have no data payload and cannot
	 * create a new record.
	 */
	if (error == 0) {
		mirror->key_cur = mrec->leaf.base;
		cursor->key_beg = mrec->leaf.base;
		cursor->flags |= HAMMER_CURSOR_BACKEND;
		cursor->flags &= ~HAMMER_CURSOR_INSERT;
		error = hammer_btree_lookup(cursor);
		if (error == 0) {
			if (hammer_mirror_check(cursor, mrec))
				error = hammer_mirror_update(cursor, mrec);
			cursor->flags |= HAMMER_CURSOR_ATEDISK;
		} else {
			cursor->flags &= ~HAMMER_CURSOR_ATEDISK;
		}
		if (error == ENOENT)
			error = 0;
	}
	return(error);
}

/*
 * As part of the mirror write we iterate across swaths of records
 * on the target which no longer exist on the source, and mark them
 * deleted.
 *
 * The caller has indexed the cursor and set up key_end.  We iterate
 * through to key_end.
 *
 * There is an edge case where the master has deleted a record whos
 * create_tid exactly matches our end_tid.  We cannot delete this
 * record on the slave yet because we cannot assign delete_tid == create_tid.
 * The deletion should be picked up on the next sequence since in order
 * to have been deleted on the master a transaction must have occured with
 * a TID greater then the create_tid of the record.
 *
 * To support incremental re-mirroring, just for robustness, we do not
 * touch any records created beyond (or equal to) mirror->tid_end.
 */
static
int
hammer_mirror_delete_to(hammer_cursor_t cursor,
		       struct hammer_ioc_mirror_rw *mirror)
{
	hammer_btree_leaf_elm_t elm;
	int error;

	error = hammer_btree_iterate(cursor);
	while (error == 0) {
		elm = &cursor->node->ondisk->elms[cursor->index].leaf;
		KKASSERT(elm->base.btype == HAMMER_BTREE_TYPE_RECORD);
		cursor->flags |= HAMMER_CURSOR_ATEDISK;

		/*
		 * Certain records are not part of the mirroring operation
		 */
		if (hammer_mirror_nomirror(&elm->base)) {
			error = hammer_btree_iterate(cursor);
			continue;
		}

		/*
		 * Note: Must still delete records with create_tid < tid_beg,
		 *	 as record may have been pruned-away on source.
		 */
		if (elm->base.delete_tid == 0 &&
		    elm->base.create_tid < mirror->tid_end) {
			error = hammer_delete_at_cursor(cursor,
							HAMMER_DELETE_ADJUST,
							mirror->tid_end,
							time_second,
							1, NULL);
		}
		if (error == 0)
			error = hammer_btree_iterate(cursor);
	}
	if (error == ENOENT)
		error = 0;
	return(error);
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
hammer_mirror_check(hammer_cursor_t cursor, struct hammer_ioc_mrecord_rec *mrec)
{
	hammer_btree_leaf_elm_t leaf = cursor->leaf;

	if (leaf->base.delete_tid != mrec->leaf.base.delete_tid) {
		if (mrec->leaf.base.delete_tid != 0)
			return(1);
	}
	return(0);
}

/*
 * Filter out records which are never mirrored, such as configuration space
 * records (for hammer cleanup).
 *
 * NOTE: We currently allow HAMMER_RECTYPE_SNAPSHOT records to be mirrored.
 */
static
int
hammer_mirror_nomirror(struct hammer_base_elm *base)
{
	/*
	 * Certain types of records are never updated when mirroring.
	 * Slaves have their own configuration space.
	 */
	if (base->rec_type == HAMMER_RECTYPE_CONFIG)
		return(1);
	return(0);
}


/*
 * Update a record in-place.  Only the delete_tid can change, and
 * only from zero to non-zero.
 */
static
int
hammer_mirror_update(hammer_cursor_t cursor,
		     struct hammer_ioc_mrecord_rec *mrec)
{
	int error;

	/*
	 * This case shouldn't occur.
	 */
	if (mrec->leaf.base.delete_tid == 0)
		return(0);

	/*
	 * Mark the record deleted on the mirror target.
	 */
	error = hammer_delete_at_cursor(cursor, HAMMER_DELETE_ADJUST,
					mrec->leaf.base.delete_tid,
					mrec->leaf.delete_ts,
					1, NULL);
	cursor->flags |= HAMMER_CURSOR_ATEDISK;
	return(error);
}

#if 0
/*
 * MOVED TO HAMMER_OBJECT.C: hammer_create_at_cursor()
 */

static int hammer_mirror_localize_data(hammer_data_ondisk_t data,
				hammer_btree_leaf_elm_t leaf);

/*
 * Write out a new record.
 */
static
int
hammer_mirror_write(hammer_cursor_t cursor,
		    struct hammer_ioc_mrecord_rec *mrec,
		    char *udata)
{
	hammer_transaction_t trans;
	hammer_buffer_t data_buffer;
	hammer_off_t ndata_offset;
	hammer_tid_t high_tid;
	void *ndata;
	int error;
	int doprop;

	trans = cursor->trans;
	data_buffer = NULL;

	/*
	 * Get the sync lock so the whole mess is atomic
	 */
	hammer_sync_lock_sh(trans);

	/*
	 * Allocate and adjust data
	 */
	if (mrec->leaf.data_len && mrec->leaf.data_offset) {
		ndata = hammer_alloc_data(trans, mrec->leaf.data_len,
					  mrec->leaf.base.rec_type,
					  &ndata_offset, &data_buffer,
					  0, &error);
		if (ndata == NULL)
			return(error);
		mrec->leaf.data_offset = ndata_offset;
		hammer_modify_buffer_noundo(trans, data_buffer);
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
	 * Do the insertion.  This can fail with a EDEADLK or EALREADY
	 */
	cursor->flags |= HAMMER_CURSOR_INSERT;
	error = hammer_btree_lookup(cursor);
	if (error != ENOENT) {
		if (error == 0)
			error = EALREADY;
		goto failed;
	}

	error = hammer_btree_insert(cursor, &mrec->leaf, &doprop);

	/*
	 * Cursor is left on the current element, we want to skip it now.
	 */
	cursor->flags |= HAMMER_CURSOR_ATEDISK;
	cursor->flags &= ~HAMMER_CURSOR_INSERT;

	/*
	 * Track a count of active inodes.
	 */
	if (error == 0 &&
	    mrec->leaf.base.rec_type == HAMMER_RECTYPE_INODE &&
	    mrec->leaf.base.delete_tid == 0) {
		hammer_modify_volume_field(trans,
					   trans->rootvol,
					   vol0_stat_inodes);
		++trans->hmp->rootvol->ondisk->vol0_stat_inodes;
		hammer_modify_volume_done(trans->rootvol);
	}

	/*
	 * vol0_next_tid must track the highest TID stored in the filesystem.
	 * We do not need to generate undo for this update.
	 */
	high_tid = mrec->leaf.base.create_tid;
	if (high_tid < mrec->leaf.base.delete_tid)
		high_tid = mrec->leaf.base.delete_tid;
	if (trans->rootvol->ondisk->vol0_next_tid < high_tid) {
		hammer_modify_volume_noundo(trans, trans->rootvol);
		trans->rootvol->ondisk->vol0_next_tid = high_tid;
		hammer_modify_volume_done(trans->rootvol);
	}

	/*
	 * WARNING!  cursor's leaf pointer may have changed after
	 *	     do_propagation returns.
	 */
	if (error == 0 && doprop)
		hammer_btree_do_propagation(cursor, NULL, &mrec->leaf);

failed:
	/*
	 * Cleanup
	 */
	if (error && mrec->leaf.data_offset) {
		hammer_blockmap_free(cursor->trans,
				     mrec->leaf.data_offset,
				     mrec->leaf.data_len);
	}
	hammer_sync_unlock(trans);
	if (data_buffer)
		hammer_rel_buffer(data_buffer, 0);
	return(error);
}

/*
 * Localize the data payload.  Directory entries may need their
 * localization adjusted.
 */
static
int
hammer_mirror_localize_data(hammer_data_ondisk_t data,
			    hammer_btree_leaf_elm_t leaf)
{
	u_int32_t localization;

	if (leaf->base.rec_type == HAMMER_RECTYPE_DIRENTRY) {
		localization = leaf->base.localization &
			       HAMMER_LOCALIZE_PSEUDOFS_MASK;
		if (data->entry.localization != localization) {
			data->entry.localization = localization;
			hammer_crc_set_leaf(data, leaf);
		}
	}
	return(0);
}

#endif
