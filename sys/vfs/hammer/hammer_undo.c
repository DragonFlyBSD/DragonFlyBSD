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
 * HAMMER undo - undo buffer/FIFO management.
 */

#include "hammer.h"

static int hammer_und_rb_compare(hammer_undo_t node1, hammer_undo_t node2);

RB_GENERATE2(hammer_und_rb_tree, hammer_undo, rb_node,
             hammer_und_rb_compare, hammer_off_t, offset);

/*
 * Convert a zone-3 undo offset into a zone-2 buffer offset.
 */
hammer_off_t
hammer_undo_lookup(hammer_mount_t hmp, hammer_off_t zone3_off, int *errorp)
{
	hammer_volume_t root_volume;
	hammer_blockmap_t undomap __debugvar;
	hammer_off_t result_offset;
	int i;

	KKASSERT((zone3_off & HAMMER_OFF_ZONE_MASK) == HAMMER_ZONE_UNDO);
	root_volume = hammer_get_root_volume(hmp, errorp);
	if (*errorp)
		return(0);
	undomap = &hmp->blockmap[HAMMER_ZONE_UNDO_INDEX];
	KKASSERT(HAMMER_ZONE_DECODE(undomap->alloc_offset) == HAMMER_ZONE_UNDO_INDEX);
	KKASSERT(zone3_off < undomap->alloc_offset);

	i = (zone3_off & HAMMER_OFF_SHORT_MASK) / HAMMER_BIGBLOCK_SIZE;
	result_offset = root_volume->ondisk->vol0_undo_array[i] +
			(zone3_off & HAMMER_BIGBLOCK_MASK64);

	hammer_rel_volume(root_volume, 0);
	return(result_offset);
}

/*
 * Generate UNDO record(s) for the block of data at the specified zone1
 * or zone2 offset.
 *
 * The recovery code will execute UNDOs in reverse order, allowing overlaps.
 * All the UNDOs are executed together so if we already laid one down we
 * do not have to lay another one down for the same range.
 *
 * For HAMMER version 4+ UNDO a 512 byte boundary is enforced and a PAD
 * will be laid down for any unused space.  UNDO FIFO media structures
 * will implement the hdr_seq field (it used to be reserved01), and
 * both flush and recovery mechanics will be very different.
 *
 * WARNING!  See also hammer_generate_redo() in hammer_redo.c
 */
int
hammer_generate_undo(hammer_transaction_t trans,
		     hammer_off_t zone_off, void *base, int len)
{
	hammer_mount_t hmp;
	hammer_volume_t root_volume;
	hammer_blockmap_t undomap;
	hammer_buffer_t buffer = NULL;
	hammer_fifo_undo_t undo;
	hammer_fifo_tail_t tail;
	hammer_off_t next_offset;
	int error;
	int bytes;
	int n;

	hmp = trans->hmp;

	/*
	 * A SYNC record may be required before we can lay down a general
	 * UNDO.  This ensures that the nominal recovery span contains
	 * at least one SYNC record telling the recovery code how far
	 * out-of-span it must go to run the REDOs.
	 */
	if ((hmp->flags & HAMMER_MOUNT_REDO_SYNC) == 0 &&
	    hmp->version >= HAMMER_VOL_VERSION_FOUR) {
		hammer_generate_redo_sync(trans);
	}

	/*
	 * Enter the offset into our undo history.  If there is an existing
	 * undo we do not have to generate a new one.
	 */
	if (hammer_enter_undo_history(hmp, zone_off, len) == EALREADY)
		return(0);

	root_volume = trans->rootvol;
	undomap = &hmp->blockmap[HAMMER_ZONE_UNDO_INDEX];

	/* no undo recursion */
	hammer_modify_volume(NULL, root_volume, NULL, 0);
	hammer_lock_ex(&hmp->undo_lock);

	/* undo had better not roll over (loose test) */
	if (hammer_undo_space(trans) < len + HAMMER_BUFSIZE*3)
		panic("hammer: insufficient undo FIFO space!");

	/*
	 * Loop until the undo for the entire range has been laid down.
	 */
	while (len) {
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
			undo = hammer_bnew(hmp, next_offset, &error, &buffer);
			hammer_format_undo(undo, hmp->undo_seqno ^ 0x40000000);
		} else {
			undo = hammer_bread(hmp, next_offset, &error, &buffer);
		}
		if (error)
			break;
		hammer_modify_buffer(NULL, buffer, NULL, 0);

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
		    (int)sizeof(struct hammer_fifo_undo) -
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
			tail = (void *)((char *)undo + bytes - sizeof(*tail));
			if ((void *)undo != (void *)tail) {
				tail->tail_signature = HAMMER_TAIL_SIGNATURE;
				tail->tail_type = HAMMER_HEAD_TYPE_PAD;
				tail->tail_size = bytes;
			}
			undo->head.hdr_signature = HAMMER_HEAD_SIGNATURE;
			undo->head.hdr_type = HAMMER_HEAD_TYPE_PAD;
			undo->head.hdr_size = bytes;
			/* NO CRC OR SEQ NO */
			undomap->next_offset += bytes;
			hammer_modify_buffer_done(buffer);
			hammer_stats_undo += bytes;
			continue;
		}

		/*
		 * Calculate the actual payload and recalculate the size
		 * of the media structure as necessary.
		 */
		if (n > len) {
			n = len;
			bytes = ((n + HAMMER_HEAD_ALIGN_MASK) &
				 ~HAMMER_HEAD_ALIGN_MASK) +
				(int)sizeof(struct hammer_fifo_undo) +
				(int)sizeof(struct hammer_fifo_tail);
		}
		if (hammer_debug_general & 0x0080) {
			kprintf("undo %016llx %d %d\n",
				(long long)next_offset, bytes, n);
		}

		undo->head.hdr_signature = HAMMER_HEAD_SIGNATURE;
		undo->head.hdr_type = HAMMER_HEAD_TYPE_UNDO;
		undo->head.hdr_size = bytes;
		undo->head.hdr_seq = hmp->undo_seqno++;
		undo->head.hdr_crc = 0;
		undo->undo_offset = zone_off;
		undo->undo_data_bytes = n;
		bcopy(base, undo + 1, n);

		tail = (void *)((char *)undo + bytes - sizeof(*tail));
		tail->tail_signature = HAMMER_TAIL_SIGNATURE;
		tail->tail_type = HAMMER_HEAD_TYPE_UNDO;
		tail->tail_size = bytes;

		KKASSERT(bytes >= sizeof(undo->head));
		undo->head.hdr_crc = crc32(undo, HAMMER_FIFO_HEAD_CRCOFF) ^
			     crc32(&undo->head + 1, bytes - sizeof(undo->head));
		undomap->next_offset += bytes;
		hammer_stats_undo += bytes;

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
			undo = (void *)(tail + 1);
			tail = (void *)((char *)undo + bytes - sizeof(*tail));
			if ((void *)undo != (void *)tail) {
				tail->tail_signature = HAMMER_TAIL_SIGNATURE;
				tail->tail_type = HAMMER_HEAD_TYPE_PAD;
				tail->tail_size = bytes;
			}
			undo->head.hdr_signature = HAMMER_HEAD_SIGNATURE;
			undo->head.hdr_type = HAMMER_HEAD_TYPE_PAD;
			undo->head.hdr_size = bytes;
			/* NO CRC OR SEQ NO */
		}
		hammer_modify_buffer_done(buffer);

		/*
		 * Adjust for loop
		 */
		len -= n;
		base = (char *)base + n;
		zone_off += n;
	}
	hammer_modify_volume_done(root_volume);
	hammer_unlock(&hmp->undo_lock);

	if (buffer)
		hammer_rel_buffer(buffer, 0);
	return(error);
}

/*
 * Preformat a new UNDO block.  We could read the old one in but we get
 * better performance if we just pre-format a new one.
 *
 * The recovery code always works forwards so the caller just makes sure the
 * seqno is not contiguous with prior UNDOs or ancient UNDOs now being
 * overwritten.
 *
 * The preformatted UNDO headers use the smallest possible sector size
 * (512) to ensure that any missed media writes are caught.
 *
 * NOTE: Also used by the REDO code.
 */
void
hammer_format_undo(void *base, u_int32_t seqno)
{
	hammer_fifo_head_t head;
	hammer_fifo_tail_t tail;
	int i;
	int bytes = HAMMER_UNDO_ALIGN;

	bzero(base, HAMMER_BUFSIZE);

	for (i = 0; i < HAMMER_BUFSIZE; i += bytes) {
		head = (void *)((char *)base + i);
		tail = (void *)((char *)head + bytes - sizeof(*tail));

		head->hdr_signature = HAMMER_HEAD_SIGNATURE;
		head->hdr_type = HAMMER_HEAD_TYPE_DUMMY;
		head->hdr_size = bytes;
		head->hdr_seq = seqno++;
		head->hdr_crc = 0;

		tail->tail_signature = HAMMER_TAIL_SIGNATURE;
		tail->tail_type = HAMMER_HEAD_TYPE_DUMMY;
		tail->tail_size = bytes;

		head->hdr_crc = crc32(head, HAMMER_FIFO_HEAD_CRCOFF) ^
			     crc32(head + 1, bytes - sizeof(*head));
	}
}

/*
 * HAMMER version 4+ conversion support.
 *
 * Convert a HAMMER version < 4 UNDO FIFO area to a 4+ UNDO FIFO area.
 * The 4+ UNDO FIFO area is backwards compatible.  The conversion is
 * needed to initialize the sequence space and place headers on the
 * new 512-byte undo boundary.
 */
int
hammer_upgrade_undo_4(hammer_transaction_t trans)
{
	hammer_mount_t hmp;
	hammer_volume_t root_volume;
	hammer_blockmap_t undomap;
	hammer_buffer_t buffer = NULL;
	hammer_fifo_head_t head;
	hammer_fifo_tail_t tail;
	hammer_off_t next_offset;
	u_int32_t seqno;
	int error;
	int bytes;

	hmp = trans->hmp;

	root_volume = trans->rootvol;

	/* no undo recursion */
	hammer_lock_ex(&hmp->undo_lock);
	hammer_modify_volume(NULL, root_volume, NULL, 0);

	/*
	 * Adjust the in-core undomap and the on-disk undomap.
	 */
	next_offset = HAMMER_ZONE_ENCODE(HAMMER_ZONE_UNDO_INDEX, 0);
	undomap = &hmp->blockmap[HAMMER_ZONE_UNDO_INDEX];
	undomap->next_offset = next_offset;
	undomap->first_offset = next_offset;

	undomap = &root_volume->ondisk->vol0_blockmap[HAMMER_ZONE_UNDO_INDEX];
	undomap->next_offset = next_offset;
	undomap->first_offset = next_offset;

	/*
	 * Loop over the entire UNDO space creating DUMMY entries.  Sequence
	 * numbers are assigned.
	 */
	seqno = 0;
	bytes = HAMMER_UNDO_ALIGN;

	while (next_offset != undomap->alloc_offset) {
		head = hammer_bnew(hmp, next_offset, &error, &buffer);
		if (error)
			break;
		hammer_modify_buffer(NULL, buffer, NULL, 0);
		tail = (void *)((char *)head + bytes - sizeof(*tail));

		head->hdr_signature = HAMMER_HEAD_SIGNATURE;
		head->hdr_type = HAMMER_HEAD_TYPE_DUMMY;
		head->hdr_size = bytes;
		head->hdr_seq = seqno;
		head->hdr_crc = 0;

		tail = (void *)((char *)head + bytes - sizeof(*tail));
		tail->tail_signature = HAMMER_TAIL_SIGNATURE;
		tail->tail_type = HAMMER_HEAD_TYPE_DUMMY;
		tail->tail_size = bytes;

		head->hdr_crc = crc32(head, HAMMER_FIFO_HEAD_CRCOFF) ^
			     crc32(head + 1, bytes - sizeof(*head));
		hammer_modify_buffer_done(buffer);

		hammer_stats_undo += bytes;
		next_offset += HAMMER_UNDO_ALIGN;
		++seqno;
	}

	/*
	 * The sequence number will be the next sequence number to lay down.
	 */
	hmp->undo_seqno = seqno;
	kprintf("version upgrade seqno start %08x\n", seqno);

	hammer_modify_volume_done(root_volume);
	hammer_unlock(&hmp->undo_lock);

	if (buffer)
		hammer_rel_buffer(buffer, 0);
	return (error);
}

/*
 * UNDO HISTORY API
 *
 * It is not necessary to layout an undo record for the same address space
 * multiple times.  Maintain a cache of recent undo's.
 */

/*
 * Enter an undo into the history.  Return EALREADY if the request completely
 * covers a previous request.
 */
int
hammer_enter_undo_history(hammer_mount_t hmp, hammer_off_t offset, int bytes)
{
	hammer_undo_t node;
	hammer_undo_t onode __debugvar;

	node = RB_LOOKUP(hammer_und_rb_tree, &hmp->rb_undo_root, offset);
	if (node) {
		TAILQ_REMOVE(&hmp->undo_lru_list, node, lru_entry);
		TAILQ_INSERT_TAIL(&hmp->undo_lru_list, node, lru_entry);
		if (bytes <= node->bytes)
			return(EALREADY);
		node->bytes = bytes;
		return(0);
	}
	if (hmp->undo_alloc != HAMMER_MAX_UNDOS) {
		node = &hmp->undos[hmp->undo_alloc++];
	} else {
		node = TAILQ_FIRST(&hmp->undo_lru_list);
		TAILQ_REMOVE(&hmp->undo_lru_list, node, lru_entry);
		RB_REMOVE(hammer_und_rb_tree, &hmp->rb_undo_root, node);
	}
	node->offset = offset;
	node->bytes = bytes;
	TAILQ_INSERT_TAIL(&hmp->undo_lru_list, node, lru_entry);
	onode = RB_INSERT(hammer_und_rb_tree, &hmp->rb_undo_root, node);
	KKASSERT(onode == NULL);
	return(0);
}

void
hammer_clear_undo_history(hammer_mount_t hmp)
{
	RB_INIT(&hmp->rb_undo_root);
	TAILQ_INIT(&hmp->undo_lru_list);
	hmp->undo_alloc = 0;
}

/*
 * Return how much of the undo FIFO has been used
 *
 * The calculation includes undo FIFO space still reserved from a previous
 * flush (because it will still be run on recovery if a crash occurs and
 * we can't overwrite it yet).
 */
int64_t
hammer_undo_used(hammer_transaction_t trans)
{
	hammer_blockmap_t cundomap;
	hammer_blockmap_t dundomap;
	int64_t max_bytes __debugvar;
	int64_t bytes;

	cundomap = &trans->hmp->blockmap[HAMMER_ZONE_UNDO_INDEX];
	dundomap = &trans->rootvol->ondisk->
				vol0_blockmap[HAMMER_ZONE_UNDO_INDEX];

	if (dundomap->first_offset <= cundomap->next_offset) {
		bytes = cundomap->next_offset - dundomap->first_offset;
	} else {
		bytes = cundomap->alloc_offset - dundomap->first_offset +
		        (cundomap->next_offset & HAMMER_OFF_LONG_MASK);
	}
	max_bytes = cundomap->alloc_offset & HAMMER_OFF_SHORT_MASK;
	KKASSERT(bytes <= max_bytes);
	return(bytes);
}

/*
 * Return how much of the undo FIFO is available for new records.
 */
int64_t
hammer_undo_space(hammer_transaction_t trans)
{
	hammer_blockmap_t rootmap;
	int64_t max_bytes;

	rootmap = &trans->hmp->blockmap[HAMMER_ZONE_UNDO_INDEX];
	max_bytes = rootmap->alloc_offset & HAMMER_OFF_SHORT_MASK;
	return(max_bytes - hammer_undo_used(trans));
}

int64_t
hammer_undo_max(hammer_mount_t hmp)
{
	hammer_blockmap_t rootmap;
	int64_t max_bytes;

	rootmap = &hmp->blockmap[HAMMER_ZONE_UNDO_INDEX];
	max_bytes = rootmap->alloc_offset & HAMMER_OFF_SHORT_MASK;

	return(max_bytes);
}

/*
 * Returns 1 if the undo buffer should be reclaimed on release.  The
 * only undo buffer we do NOT want to reclaim is the one at the current
 * append offset.
 */
int
hammer_undo_reclaim(hammer_io_t io)
{
	hammer_blockmap_t undomap;
	hammer_off_t next_offset;

	undomap = &io->hmp->blockmap[HAMMER_ZONE_UNDO_INDEX];
	next_offset = undomap->next_offset & ~HAMMER_BUFMASK64;
	if (((struct hammer_buffer *)io)->zoneX_offset == next_offset)
		return(0);
	return(1);
}

static int
hammer_und_rb_compare(hammer_undo_t node1, hammer_undo_t node2)
{
        if (node1->offset < node2->offset)
                return(-1);
        if (node1->offset > node2->offset)
                return(1);
        return(0);
}

