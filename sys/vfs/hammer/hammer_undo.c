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
 * $DragonFly: src/sys/vfs/hammer/hammer_undo.c,v 1.20 2008/07/18 00:19:53 dillon Exp $
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
	hammer_blockmap_t undomap;
	hammer_off_t result_offset;
	int i;

	KKASSERT((zone3_off & HAMMER_OFF_ZONE_MASK) == HAMMER_ZONE_UNDO);
	root_volume = hammer_get_root_volume(hmp, errorp);
	if (*errorp)
		return(0);
	undomap = &hmp->blockmap[HAMMER_ZONE_UNDO_INDEX];
	KKASSERT(HAMMER_ZONE_DECODE(undomap->alloc_offset) == HAMMER_ZONE_UNDO_INDEX);
	KKASSERT (zone3_off < undomap->alloc_offset);

	i = (zone3_off & HAMMER_OFF_SHORT_MASK) / HAMMER_LARGEBLOCK_SIZE;
	result_offset = root_volume->ondisk->vol0_undo_array[i] +
			(zone3_off & HAMMER_LARGEBLOCK_MASK64);

	hammer_rel_volume(root_volume, 0);
	return(result_offset);
}

/*
 * Generate an UNDO record for the block of data at the specified zone1
 * or zone2 offset.
 *
 * The recovery code will execute UNDOs in reverse order, allowing overlaps.
 * All the UNDOs are executed together so if we already laid one down we
 * do not have to lay another one down for the same range.
 */
int
hammer_generate_undo(hammer_transaction_t trans, hammer_io_t io,
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

	hmp = trans->hmp;

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
again:
	/*
	 * Allocate space in the FIFO
	 */
	bytes = ((len + HAMMER_HEAD_ALIGN_MASK) & ~HAMMER_HEAD_ALIGN_MASK) +
		sizeof(struct hammer_fifo_undo) +
		sizeof(struct hammer_fifo_tail);
	if (hammer_undo_space(trans) < bytes + HAMMER_BUFSIZE*2)
		panic("hammer: insufficient undo FIFO space!");

	next_offset = undomap->next_offset;

	/*
	 * Wrap next_offset
	 */
	if (undomap->next_offset == undomap->alloc_offset) {
		next_offset = HAMMER_ZONE_ENCODE(HAMMER_ZONE_UNDO_INDEX, 0);
		undomap->next_offset = next_offset;
	}

	/*
	 * This is a tail-chasing FIFO, when we hit the start of a new
	 * buffer we don't have to read it in.
	 */
	if ((next_offset & HAMMER_BUFMASK) == 0)
		undo = hammer_bnew(hmp, next_offset, &error, &buffer);
	else
		undo = hammer_bread(hmp, next_offset, &error, &buffer);
	if (error)
		goto done;

	hammer_modify_buffer(NULL, buffer, NULL, 0);

	KKASSERT(undomap->next_offset == next_offset);

	/*
	 * The FIFO entry would cross a buffer boundary, PAD to the end
	 * of the buffer and try again.  Due to our data alignment, the
	 * worst case (smallest) PAD record is 8 bytes.  PAD records only
	 * populate the first 8 bytes of hammer_fifo_head and the tail may
	 * be at the same offset as the head.
	 */
	if ((next_offset ^ (next_offset + bytes)) & ~HAMMER_BUFMASK64) {
		bytes = HAMMER_BUFSIZE - ((int)next_offset & HAMMER_BUFMASK);
		tail = (void *)((char *)undo + bytes - sizeof(*tail));
		if ((void *)undo != (void *)tail) {
			tail->tail_signature = HAMMER_TAIL_SIGNATURE;
			tail->tail_type = HAMMER_HEAD_TYPE_PAD;
			tail->tail_size = bytes;
		}
		undo->head.hdr_signature = HAMMER_HEAD_SIGNATURE;
		undo->head.hdr_type = HAMMER_HEAD_TYPE_PAD;
		undo->head.hdr_size = bytes;
		/* NO CRC */
		undomap->next_offset += bytes;
		hammer_modify_buffer_done(buffer);
		goto again;
	}
	if (hammer_debug_general & 0x0080)
		kprintf("undo %016llx %d %d\n", next_offset, bytes, len);

	/*
	 * We're good, create the entry.
	 */
	undo->head.hdr_signature = HAMMER_HEAD_SIGNATURE;
	undo->head.hdr_type = HAMMER_HEAD_TYPE_UNDO;
	undo->head.hdr_size = bytes;
	undo->head.reserved01 = 0;
	undo->head.hdr_crc = 0;
	undo->undo_offset = zone_off;
	undo->undo_data_bytes = len;
	bcopy(base, undo + 1, len);

	tail = (void *)((char *)undo + bytes - sizeof(*tail));
	tail->tail_signature = HAMMER_TAIL_SIGNATURE;
	tail->tail_type = HAMMER_HEAD_TYPE_UNDO;
	tail->tail_size = bytes;

	KKASSERT(bytes >= sizeof(undo->head));
	undo->head.hdr_crc = crc32(undo, HAMMER_FIFO_HEAD_CRCOFF) ^
			     crc32(&undo->head + 1, bytes - sizeof(undo->head));
	undomap->next_offset += bytes;

	hammer_modify_buffer_done(buffer);
done:
	hammer_modify_volume_done(root_volume);
	hammer_unlock(&hmp->undo_lock);

	if (buffer)
		hammer_rel_buffer(buffer, 0);
	return(error);
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
	hammer_undo_t onode;

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
	int64_t max_bytes;
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

static int
hammer_und_rb_compare(hammer_undo_t node1, hammer_undo_t node2)
{
        if (node1->offset < node2->offset)
                return(-1);
        if (node1->offset > node2->offset)
                return(1);
        return(0);
}

