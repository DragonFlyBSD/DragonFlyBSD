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
 * $DragonFly: src/sys/vfs/hammer/hammer_undo.c,v 1.1 2008/03/18 05:19:16 dillon Exp $
 */

/*
 * HAMMER undo - undo buffer/FIFO management.
 */

#include "hammer.h"

/*
 * Convert a zone-3 undo offset into a zone-2 buffer offset.
 */
hammer_off_t
hammer_undo_lookup(hammer_mount_t hmp, hammer_off_t zone3_off, int *errorp)
{
	hammer_volume_t root_volume;
	hammer_blockmap_t undomap;
	struct hammer_blockmap_layer2 *layer2;
	hammer_off_t result_offset;
	int i;

	KKASSERT((zone3_off & HAMMER_OFF_ZONE_MASK) == HAMMER_ZONE_UNDO);
	root_volume = hammer_get_root_volume(hmp, errorp);
	if (*errorp)
		return(0);
	undomap = &root_volume->ondisk->vol0_blockmap[HAMMER_ZONE_UNDO_INDEX];
	KKASSERT(HAMMER_ZONE_DECODE(undomap->alloc_offset) == HAMMER_ZONE_UNDO_INDEX);
	KKASSERT (zone3_off < undomap->alloc_offset);

	i = (zone3_off & HAMMER_OFF_SHORT_MASK) / HAMMER_LARGEBLOCK_SIZE;
	layer2 = &root_volume->ondisk->vol0_undo_array[i];
	result_offset = layer2->u.phys_offset +
			(zone3_off & HAMMER_LARGEBLOCK_MASK64);

	hammer_rel_volume(root_volume, 0);
	return(result_offset);
}

/*
 * Generate an UNDO record for the block of data at the specified zone1
 * offset.
 */
int
hammer_generate_undo(hammer_mount_t hmp, hammer_off_t zone1_off,
		     void *base, int len)
{
	hammer_volume_t root_volume;
	hammer_volume_ondisk_t ondisk;
	hammer_blockmap_t undomap;
	hammer_buffer_t buffer = NULL;
	struct hammer_blockmap_layer2 *layer2;
	hammer_fifo_undo_t undo;
	hammer_fifo_tail_t tail;
	hammer_off_t next_offset;
	hammer_off_t result_offset;
	int i;
	int error;
	int bytes;

	bytes = ((len + 7) & ~7) + sizeof(struct hammer_fifo_undo) +
		sizeof(struct hammer_fifo_tail);

	root_volume = hammer_get_root_volume(hmp, &error);
	if (error)
		return(error);
	ondisk = root_volume->ondisk;
	undomap = &ondisk->vol0_blockmap[HAMMER_ZONE_UNDO_INDEX];
	hammer_modify_volume(root_volume, NULL, 0); /* no undo recursion */

	/*
	 * Allocate space in the FIFO
	 */
again:
	next_offset = undomap->next_offset;

	/*
	 * Wrap next_offset
	 */
	if (undomap->next_offset == undomap->alloc_offset) {
		next_offset = HAMMER_ZONE_ENCODE(HAMMER_ZONE_UNDO_INDEX, 0);
		undomap->next_offset = next_offset;
		kprintf("undo zone's next_offset wrapped\n");
	}

	i = (next_offset & HAMMER_OFF_SHORT_MASK) / HAMMER_LARGEBLOCK_SIZE;
	layer2 = &root_volume->ondisk->vol0_undo_array[i];
	result_offset = layer2->u.phys_offset +
			(next_offset & HAMMER_LARGEBLOCK_MASK64);

	undo = hammer_bread(hmp, result_offset, &error, &buffer);

	/*
	 * We raced another thread, try again.
	 */
	if (undomap->next_offset != next_offset)
		goto again;

	hammer_modify_buffer(buffer, NULL, 0);

	/*
	 * The FIFO entry would cross a buffer boundary, PAD to the end
	 * of the buffer and try again.  Due to our data alignment, the
	 * worst case (smallest) PAD record is 8 bytes.  PAD records only
	 * populate the first 8 bytes of hammer_fifo_head and the tail may
	 * be at the same offset as the head.
	 */
	if ((result_offset ^ (result_offset + bytes)) & ~HAMMER_BUFMASK64) {
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
		undomap->next_offset += bytes;
		goto again;
	}
	if (hammer_debug_general & 0x0080)
		kprintf("undo %016llx %d %d\n", result_offset, bytes, len);

	/*
	 * We're good, create the entry.
	 */
	undo->head.hdr_signature = HAMMER_HEAD_SIGNATURE;
	undo->head.hdr_type = HAMMER_HEAD_TYPE_PAD;
	undo->head.hdr_size = bytes;
	undo->undo_offset = zone1_off;
	undo->undo_data_bytes = len;
	bcopy(base, undo + 1, len);
	undo->head.hdr_crc = crc32(undo, bytes);

	undomap->next_offset += bytes;

	if (buffer)
		hammer_rel_buffer(buffer, 0);
	hammer_rel_volume(root_volume, 0);
	return(error);
}

