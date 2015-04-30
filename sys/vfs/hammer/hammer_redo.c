/*
 * Copyright (c) 2010 The DragonFly Project.  All rights reserved.
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
 * HAMMER redo - REDO record support for the UNDO/REDO FIFO.
 *
 * See also hammer_undo.c
 */

#include "hammer.h"

RB_GENERATE2(hammer_redo_rb_tree, hammer_inode, rb_redonode,
	     hammer_redo_rb_compare, hammer_off_t, redo_fifo_start);

/*
 * HAMMER version 4+ REDO support.
 *
 * REDO records are used to improve fsync() performance.  Instead of having
 * to go through a complete double-flush cycle involving at least two disk
 * synchronizations the fsync need only flush UNDO/REDO FIFO buffers through
 * the related REDO records, which is a single synchronization requiring
 * no track seeking.  If a recovery becomes necessary the recovery code
 * will generate logical data writes based on the REDO records encountered.
 * That is, the recovery code will UNDO any partial meta-data/data writes
 * at the raw disk block level and then REDO the data writes at the logical
 * level.
 */
int
hammer_generate_redo(hammer_transaction_t trans, hammer_inode_t ip,
		     hammer_off_t file_off, u_int32_t flags,
		     void *base, int len)
{
	hammer_mount_t hmp;
	hammer_volume_t root_volume;
	hammer_blockmap_t undomap;
	hammer_buffer_t buffer = NULL;
	hammer_fifo_redo_t redo;
	hammer_fifo_tail_t tail;
	hammer_off_t next_offset;
	int error;
	int bytes;
	int n;

	/*
	 * Setup
	 */
	hmp = trans->hmp;

	root_volume = trans->rootvol;
	undomap = &hmp->blockmap[HAMMER_ZONE_UNDO_INDEX];

	/*
	 * No undo recursion when modifying the root volume
	 */
	hammer_modify_volume_noundo(NULL, root_volume);
	hammer_lock_ex(&hmp->undo_lock);

	/* undo had better not roll over (loose test) */
	if (hammer_undo_space(trans) < len + HAMMER_BUFSIZE*3)
		panic("hammer: insufficient undo FIFO space!");

	/*
	 * Loop until the undo for the entire range has been laid down.
	 * Loop at least once (len might be 0 as a degenerate case).
	 */
	for (;;) {
		/*
		 * Fetch the layout offset in the UNDO FIFO, wrap it as
		 * necessary.
		 */
		if (undomap->next_offset == undomap->alloc_offset) {
			undomap->next_offset =
				HAMMER_ZONE_ENCODE(HAMMER_ZONE_UNDO_INDEX, 0);
		}
		next_offset = undomap->next_offset;

		/*
		 * This is a tail-chasing FIFO, when we hit the start of a new
		 * buffer we don't have to read it in.
		 */
		if ((next_offset & HAMMER_BUFMASK) == 0) {
			redo = hammer_bnew(hmp, next_offset, &error, &buffer);
			hammer_format_undo(redo, hmp->undo_seqno ^ 0x40000000);
		} else {
			redo = hammer_bread(hmp, next_offset, &error, &buffer);
		}
		if (error)
			break;
		hammer_modify_buffer_noundo(NULL, buffer);

		/*
		 * Calculate how big a media structure fits up to the next
		 * alignment point and how large a data payload we can
		 * accomodate.
		 *
		 * If n calculates to 0 or negative there is no room for
		 * anything but a PAD.
		 */
		bytes = HAMMER_UNDO_ALIGN -
			((int)next_offset & HAMMER_UNDO_MASK);
		n = bytes -
		    (int)sizeof(struct hammer_fifo_redo) -
		    (int)sizeof(struct hammer_fifo_tail);

		/*
		 * If available space is insufficient for any payload
		 * we have to lay down a PAD.
		 *
		 * The minimum PAD is 8 bytes and the head and tail will
		 * overlap each other in that case.  PADs do not have
		 * sequence numbers or CRCs.
		 *
		 * A PAD may not start on a boundary.  That is, every
		 * 512-byte block in the UNDO/REDO FIFO must begin with
		 * a record containing a sequence number.
		 */
		if (n <= 0) {
			KKASSERT(bytes >= sizeof(struct hammer_fifo_tail));
			KKASSERT(((int)next_offset & HAMMER_UNDO_MASK) != 0);
			tail = (void *)((char *)redo + bytes - sizeof(*tail));
			if ((void *)redo != (void *)tail) {
				tail->tail_signature = HAMMER_TAIL_SIGNATURE;
				tail->tail_type = HAMMER_HEAD_TYPE_PAD;
				tail->tail_size = bytes;
			}
			redo->head.hdr_signature = HAMMER_HEAD_SIGNATURE;
			redo->head.hdr_type = HAMMER_HEAD_TYPE_PAD;
			redo->head.hdr_size = bytes;
			/* NO CRC OR SEQ NO */
			undomap->next_offset += bytes;
			hammer_modify_buffer_done(buffer);
			hammer_stats_redo += bytes;
			continue;
		}

		/*
		 * When generating an inode-related REDO record we track
		 * the point in the UNDO/REDO FIFO containing the inode's
		 * earliest REDO record.  See hammer_generate_redo_sync().
		 *
		 * redo_fifo_next is cleared when an inode is staged to
		 * the backend and then used to determine how to reassign
		 * redo_fifo_start after the inode flush completes.
		 */
		if (ip) {
			redo->redo_objid = ip->obj_id;
			redo->redo_localization = ip->obj_localization;
			if ((ip->flags & HAMMER_INODE_RDIRTY) == 0) {
				ip->redo_fifo_start = next_offset;
				if (RB_INSERT(hammer_redo_rb_tree,
					      &hmp->rb_redo_root, ip)) {
					panic("hammer_generate_redo: "
					      "cannot insert inode %p on "
					      "redo FIFO", ip);
				}
				ip->flags |= HAMMER_INODE_RDIRTY;
			}
			if (ip->redo_fifo_next == 0)
				ip->redo_fifo_next = next_offset;
		} else {
			redo->redo_objid = 0;
			redo->redo_localization = 0;
		}

		/*
		 * Calculate the actual payload and recalculate the size
		 * of the media structure as necessary.  If no data buffer
		 * is supplied there is no payload.
		 */
		if (base == NULL) {
			n = 0;
		} else if (n > len) {
			n = len;
		}
		bytes = ((n + HAMMER_HEAD_ALIGN_MASK) &
			 ~HAMMER_HEAD_ALIGN_MASK) +
			(int)sizeof(struct hammer_fifo_redo) +
			(int)sizeof(struct hammer_fifo_tail);
		if (hammer_debug_general & 0x0080) {
			kprintf("redo %016llx %d %d\n",
				(long long)next_offset, bytes, n);
		}

		redo->head.hdr_signature = HAMMER_HEAD_SIGNATURE;
		redo->head.hdr_type = HAMMER_HEAD_TYPE_REDO;
		redo->head.hdr_size = bytes;
		redo->head.hdr_seq = hmp->undo_seqno++;
		redo->head.hdr_crc = 0;
		redo->redo_mtime = trans->time;
		redo->redo_offset = file_off;
		redo->redo_flags = flags;

		/*
		 * Incremental payload.  If no payload we throw the entire
		 * len into redo_data_bytes and will not loop.
		 */
		if (base) {
			redo->redo_data_bytes = n;
			bcopy(base, redo + 1, n);
			len -= n;
			base = (char *)base + n;
			file_off += n;
		} else {
			redo->redo_data_bytes = len;
			file_off += len;
			len = 0;
		}

		tail = (void *)((char *)redo + bytes - sizeof(*tail));
		tail->tail_signature = HAMMER_TAIL_SIGNATURE;
		tail->tail_type = HAMMER_HEAD_TYPE_REDO;
		tail->tail_size = bytes;

		KKASSERT(bytes >= sizeof(redo->head));
		redo->head.hdr_crc = crc32(redo, HAMMER_FIFO_HEAD_CRCOFF) ^
			     crc32(&redo->head + 1, bytes - sizeof(redo->head));
		undomap->next_offset += bytes;
		hammer_stats_redo += bytes;

		/*
		 * Before we finish off the buffer we have to deal with any
		 * junk between the end of the media structure we just laid
		 * down and the UNDO alignment boundary.  We do this by laying
		 * down a dummy PAD.  Even though we will probably overwrite
		 * it almost immediately we have to do this so recovery runs
		 * can iterate the UNDO space without having to depend on
		 * the indices in the volume header.
		 *
		 * This dummy PAD will be overwritten on the next undo so
		 * we do not adjust undomap->next_offset.
		 */
		bytes = HAMMER_UNDO_ALIGN -
			((int)undomap->next_offset & HAMMER_UNDO_MASK);
		if (bytes != HAMMER_UNDO_ALIGN) {
			KKASSERT(bytes >= sizeof(struct hammer_fifo_tail));
			redo = (void *)(tail + 1);
			tail = (void *)((char *)redo + bytes - sizeof(*tail));
			if ((void *)redo != (void *)tail) {
				tail->tail_signature = HAMMER_TAIL_SIGNATURE;
				tail->tail_type = HAMMER_HEAD_TYPE_PAD;
				tail->tail_size = bytes;
			}
			redo->head.hdr_signature = HAMMER_HEAD_SIGNATURE;
			redo->head.hdr_type = HAMMER_HEAD_TYPE_PAD;
			redo->head.hdr_size = bytes;
			/* NO CRC OR SEQ NO */
		}
		hammer_modify_buffer_done(buffer);
		if (len == 0)
			break;
	}
	hammer_modify_volume_done(root_volume);
	hammer_unlock(&hmp->undo_lock);

	if (buffer)
		hammer_rel_buffer(buffer, 0);

	/*
	 * Make sure the nominal undo span contains at least one REDO_SYNC,
	 * otherwise the REDO recovery will not be triggered.
	 */
	if ((hmp->flags & HAMMER_MOUNT_REDO_SYNC) == 0 &&
	    flags != HAMMER_REDO_SYNC) {
		hammer_generate_redo_sync(trans);
	}

	return(error);
}

/*
 * Generate a REDO SYNC record.  At least one such record must be generated
 * in the nominal recovery span for the recovery code to be able to run
 * REDOs outside of the span.
 *
 * The SYNC record contains the aggregate earliest UNDO/REDO FIFO offset
 * for all inodes with active REDOs.  This changes dynamically as inodes
 * get flushed.
 *
 * During recovery stage2 any new flush cycles must specify the original
 * redo sync offset.  That way a crash will re-run the REDOs, at least
 * up to the point where the UNDO FIFO does not overwrite the area.
 */
void
hammer_generate_redo_sync(hammer_transaction_t trans)
{
	hammer_mount_t hmp = trans->hmp;
	hammer_inode_t ip;
	hammer_off_t redo_fifo_start;

	if (hmp->flags & HAMMER_MOUNT_REDO_RECOVERY_RUN) {
		ip = NULL;
		redo_fifo_start = hmp->recover_stage2_offset;
	} else {
		ip = RB_FIRST(hammer_redo_rb_tree, &hmp->rb_redo_root);
		if (ip)
			redo_fifo_start = ip->redo_fifo_start;
		else
			redo_fifo_start = 0;
	}
	if (redo_fifo_start) {
		if (hammer_debug_io & 0x0004) {
			kprintf("SYNC IP %p %016jx\n",
				ip, (intmax_t)redo_fifo_start);
		}
		hammer_generate_redo(trans, NULL, redo_fifo_start,
				     HAMMER_REDO_SYNC, NULL, 0);
		trans->hmp->flags |= HAMMER_MOUNT_REDO_SYNC;
	}
}

/*
 * This is called when an inode is queued to the backend.
 */
void
hammer_redo_fifo_start_flush(hammer_inode_t ip)
{
	ip->redo_fifo_next = 0;
}

/*
 * This is called when an inode backend flush is finished.  We have to make
 * sure that RDIRTY is not set unless dirty bufs are present.  Dirty bufs
 * can get destroyed through operations such as truncations and leave
 * us with a stale redo_fifo_next.
 */
void
hammer_redo_fifo_end_flush(hammer_inode_t ip)
{
	hammer_mount_t hmp = ip->hmp;

	if (ip->flags & HAMMER_INODE_RDIRTY) {
		RB_REMOVE(hammer_redo_rb_tree, &hmp->rb_redo_root, ip);
		ip->flags &= ~HAMMER_INODE_RDIRTY;
	}
	if ((ip->flags & HAMMER_INODE_BUFS) == 0)
		ip->redo_fifo_next = 0;
	if (ip->redo_fifo_next) {
		ip->redo_fifo_start = ip->redo_fifo_next;
		if (RB_INSERT(hammer_redo_rb_tree, &hmp->rb_redo_root, ip)) {
			panic("hammer_generate_redo: cannot reinsert "
			      "inode %p on redo FIFO",
			      ip);
		}
		ip->flags |= HAMMER_INODE_RDIRTY;
	}
}
