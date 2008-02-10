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
 * $DragonFly: src/sys/vfs/hammer/hammer_blockmap.c,v 1.2 2008/02/10 18:58:22 dillon Exp $
 */

/*
 * HAMMER blockmap
 */
#include "hammer.h"

/*
 * Allocate bytes from a zone
 */
hammer_off_t
hammer_blockmap_alloc(hammer_mount_t hmp, int zone, int bytes, int *errorp)
{
	hammer_volume_t root_volume;
	hammer_blockmap_entry_t rootmap;
	hammer_blockmap_entry_t blockmap;
	hammer_buffer_t buffer = NULL;
	hammer_off_t alloc_offset;
	hammer_off_t result_offset;
	int32_t i;

	KKASSERT(zone >= HAMMER_ZONE_BTREE_INDEX && zone < HAMMER_MAX_ZONES);
	root_volume = hammer_get_root_volume(hmp, errorp);
	if (*errorp)
		return(0);
	rootmap = &root_volume->ondisk->vol0_blockmap[zone];
	KKASSERT(rootmap->phys_offset != 0);
	KKASSERT(HAMMER_ZONE_DECODE(rootmap->phys_offset) ==
		 HAMMER_ZONE_RAW_BUFFER_INDEX);
	KKASSERT(HAMMER_ZONE_DECODE(rootmap->alloc_offset) == zone);

	/*
	 * Deal with alignment and buffer-boundary issues.
	 *
	 * Be careful, certain primary alignments are used below to allocate
	 * new blockmap blocks.
	 */
	bytes = (bytes + 7) & ~7;
	KKASSERT(bytes <= HAMMER_BUFSIZE);

	lockmgr(&hmp->blockmap_lock, LK_EXCLUSIVE|LK_RETRY);
	alloc_offset = rootmap->alloc_offset;
	result_offset = alloc_offset + bytes;
	if ((alloc_offset ^ (result_offset - 1)) & ~HAMMER_BUFMASK64) {
		alloc_offset = (result_offset - 1) & ~HAMMER_BUFMASK64;
	}

	/*
	 * Dive layer 2, each entry is a layer-1 entry.  If we are at the
	 * start of a new entry, allocate a layer 1 large-block
	 */
	i = (alloc_offset >> (HAMMER_LARGEBLOCK_BITS +
	     HAMMER_BLOCKMAP_BITS)) & HAMMER_BLOCKMAP_RADIX_MASK;

	blockmap = hammer_bread(hmp, rootmap->phys_offset + i * sizeof(*blockmap), errorp, &buffer);
	KKASSERT(*errorp == 0);

	if ((alloc_offset & HAMMER_LARGEBLOCK_LAYER1_MASK) == 0) {
		hammer_modify_buffer(buffer, blockmap, sizeof(*blockmap));
		bzero(blockmap, sizeof(*blockmap));
		blockmap->phys_offset = hammer_freemap_alloc(hmp, errorp);
		KKASSERT(*errorp == 0);
	}
	KKASSERT(blockmap->phys_offset);

	/*
	 * Dive layer 1, each entry is a large-block.  If we are at the
	 * start of a new entry, allocate a large-block.
	 */
	i = (alloc_offset >> HAMMER_LARGEBLOCK_BITS) &
	    HAMMER_BLOCKMAP_RADIX_MASK;

	blockmap = hammer_bread(hmp, blockmap->phys_offset + i * sizeof(*blockmap), errorp, &buffer);
	KKASSERT(*errorp == 0);

	if ((alloc_offset & HAMMER_LARGEBLOCK_MASK64) == 0) {
		hammer_modify_buffer(buffer, blockmap, sizeof(*blockmap));
		/* XXX rootmap changed */
		bzero(blockmap, sizeof(*blockmap));
		blockmap->phys_offset = hammer_freemap_alloc(hmp, errorp);
		blockmap->bytes_free = HAMMER_LARGEBLOCK_SIZE;
		KKASSERT(*errorp == 0);
	}

	hammer_modify_buffer(buffer, blockmap, sizeof(*blockmap));
	blockmap->bytes_free -= bytes;

	hammer_modify_volume(root_volume, &rootmap->alloc_offset,
			     sizeof(rootmap->alloc_offset));
	result_offset = alloc_offset;
	rootmap->alloc_offset = alloc_offset + bytes;

	/*
	 * Calling bnew on the buffer backing the allocation gets it into
	 * the system without a disk read.
	 *
	 * XXX This can only be done when appending into a new buffer.
	 */
	if (((int32_t)result_offset & HAMMER_BUFMASK) == 0) {
		hammer_bnew(hmp, blockmap->phys_offset + (result_offset & HAMMER_LARGEBLOCK_MASK64), errorp, &buffer);
	}

	if (buffer)
		hammer_rel_buffer(buffer, 0);
	hammer_rel_volume(root_volume, 0);
	lockmgr(&hmp->blockmap_lock, LK_RELEASE);
	return(result_offset);
}

/*
 * Free (offset,bytes) in a zone
 */
int
hammer_blockmap_free(hammer_mount_t hmp, hammer_off_t bmap_off, int bytes)
{
	kprintf("hammer_blockmap_free %016llx %d\n", bmap_off, bytes);
	return(0);
}

/*
 * Lookup a blockmap offset.
 */
hammer_off_t
hammer_blockmap_lookup(hammer_mount_t hmp, hammer_off_t bmap_off, int *errorp)
{
	hammer_volume_t root_volume;
	hammer_blockmap_entry_t rootmap;
	hammer_blockmap_entry_t blockmap;
	hammer_buffer_t buffer = NULL;
	hammer_off_t result_offset;
	int zone;
	int i;

	zone = HAMMER_ZONE_DECODE(bmap_off);
	KKASSERT(zone >= HAMMER_ZONE_BTREE_INDEX && zone < HAMMER_MAX_ZONES);
	root_volume = hammer_get_root_volume(hmp, errorp);
	if (*errorp)
		return(0);
	rootmap = &root_volume->ondisk->vol0_blockmap[zone];
	KKASSERT(rootmap->phys_offset != 0);
	KKASSERT(HAMMER_ZONE_DECODE(rootmap->phys_offset) ==
		 HAMMER_ZONE_RAW_BUFFER_INDEX);
	KKASSERT(HAMMER_ZONE_DECODE(rootmap->alloc_offset) == zone);

	if (bmap_off >= rootmap->alloc_offset) {
		panic("hammer_blockmap_lookup: %016llx beyond EOF %016llx",
		      bmap_off, rootmap->alloc_offset);
		result_offset = 0;
		goto done;
	}

	/*
	 * Dive layer 2, each entry is a layer-1 entry.  If we are at the
	 * start of a new entry, allocate a layer 1 large-block
	 */
	i = (bmap_off >> (HAMMER_LARGEBLOCK_BITS +
	     HAMMER_BLOCKMAP_BITS)) & HAMMER_BLOCKMAP_RADIX_MASK;

	blockmap = hammer_bread(hmp, rootmap->phys_offset + i * sizeof(*blockmap), errorp, &buffer);
	KKASSERT(*errorp == 0);
	KKASSERT(blockmap->phys_offset);

	/*
	 * Dive layer 1, entry entry is a large-block.  If we are at the
	 * start of a new entry, allocate a large-block.
	 */
	i = (bmap_off >> HAMMER_LARGEBLOCK_BITS) & HAMMER_BLOCKMAP_RADIX_MASK;

	blockmap = hammer_bread(hmp, blockmap->phys_offset + i * sizeof(*blockmap), errorp, &buffer);
	KKASSERT(*errorp == 0);
	KKASSERT(blockmap->phys_offset);
	result_offset = blockmap->phys_offset +
			(bmap_off & HAMMER_LARGEBLOCK_MASK64);
done:
	if (buffer)
		hammer_rel_buffer(buffer, 0);
	hammer_rel_volume(root_volume, 0);
	if (hammer_debug_general & 0x0800) {
		kprintf("hammer_blockmap_lookup: %016llx -> %016llx\n",
			bmap_off, result_offset);
	}
	return(result_offset);
}

