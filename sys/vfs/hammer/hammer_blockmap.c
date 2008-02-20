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
 * $DragonFly: src/sys/vfs/hammer/hammer_blockmap.c,v 1.3 2008/02/20 00:55:51 dillon Exp $
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
	hammer_blockmap_t rootmap;
	struct hammer_blockmap_layer1 *layer1;
	struct hammer_blockmap_layer2 *layer2;
	hammer_buffer_t buffer = NULL;
	hammer_off_t tmp_offset;
	hammer_off_t alloc_offset;
	hammer_off_t layer1_offset;
	hammer_off_t layer2_offset;
	hammer_off_t bigblock_offset;

	KKASSERT(zone >= HAMMER_ZONE_BTREE_INDEX && zone < HAMMER_MAX_ZONES);
	root_volume = hammer_get_root_volume(hmp, errorp);
	if (*errorp)
		return(0);
	rootmap = &root_volume->ondisk->vol0_blockmap[zone];
	KKASSERT(rootmap->phys_offset != 0);
	KKASSERT(HAMMER_ZONE_DECODE(rootmap->phys_offset) ==
		 HAMMER_ZONE_RAW_BUFFER_INDEX);
	KKASSERT(HAMMER_ZONE_DECODE(rootmap->alloc_offset) == zone);
	KKASSERT(HAMMER_ZONE_DECODE(rootmap->next_offset) == zone);

	/*
	 * Deal with alignment and buffer-boundary issues.
	 *
	 * Be careful, certain primary alignments are used below to allocate
	 * new blockmap blocks.
	 */
	bytes = (bytes + 7) & ~7;
	KKASSERT(bytes <= HAMMER_BUFSIZE);
	KKASSERT(rootmap->next_offset <= rootmap->alloc_offset);

	lockmgr(&hmp->blockmap_lock, LK_EXCLUSIVE|LK_RETRY);
	alloc_offset = rootmap->next_offset;
	tmp_offset = alloc_offset + bytes;
	if ((alloc_offset ^ (tmp_offset - 1)) & ~HAMMER_BUFMASK64) {
		alloc_offset = (tmp_offset - 1) & ~HAMMER_BUFMASK64;
	}

	/*
	 * Dive layer 1.  If we are starting a new layer 1 entry,
	 * allocate a layer 2 block for it.
	 */
	layer1_offset = rootmap->phys_offset +
			HAMMER_BLOCKMAP_LAYER1_OFFSET(alloc_offset);
	layer1 = hammer_bread(hmp, layer1_offset, errorp, &buffer);
	KKASSERT(*errorp == 0);

	/*
	 * Allocate layer2 backing store if necessary
	 */
	if ((alloc_offset == rootmap->alloc_offset &&
	    (alloc_offset & HAMMER_BLOCKMAP_LAYER2_MASK) == 0) ||
	    layer1->phys_offset == HAMMER_BLOCKMAP_FREE
	) {
		hammer_modify_buffer(buffer, layer1, sizeof(*layer1));
		bzero(layer1, sizeof(*layer1));
		layer1->phys_offset = hammer_freemap_alloc(hmp, alloc_offset,
							   errorp);
		KKASSERT(*errorp == 0);
	}
	KKASSERT(layer1->phys_offset);

	/*
	 * Dive layer 2, each entry represents a large-block.  If we are at
	 * the start of a new entry, allocate a large-block.
	 */
	layer2_offset = layer1->phys_offset +
			HAMMER_BLOCKMAP_LAYER2_OFFSET(alloc_offset);
	layer2 = hammer_bread(hmp, layer2_offset, errorp, &buffer);
	KKASSERT(*errorp == 0);

	/*
	 * Allocate the bigblock in layer2 if necesasry.
	 */
	if ((alloc_offset == rootmap->alloc_offset &&
	    (alloc_offset & HAMMER_LARGEBLOCK_MASK64) == 0) ||
	    layer2->u.phys_offset == HAMMER_BLOCKMAP_FREE
	) {
		hammer_modify_buffer(buffer, layer2, sizeof(*layer2));
		/* XXX rootmap changed */
		bzero(layer2, sizeof(*layer2));
		layer2->u.phys_offset = hammer_freemap_alloc(hmp, alloc_offset,
							     errorp);
		layer2->bytes_free = HAMMER_LARGEBLOCK_SIZE;
		KKASSERT(*errorp == 0);
	}

	hammer_modify_buffer(buffer, layer2, sizeof(*layer2));
	layer2->bytes_free -= bytes;

	/*
	 * Calling bnew on the buffer backing the allocation gets it into
	 * the system without a disk read.
	 *
	 * XXX This can only be done when appending into a new buffer.
	 */
	if (alloc_offset == rootmap->alloc_offset &&
	    (alloc_offset & HAMMER_BUFMASK) == 0) {
		bigblock_offset = layer2->u.phys_offset +
				  (alloc_offset & HAMMER_LARGEBLOCK_MASK64);
		hammer_bnew(hmp, bigblock_offset, errorp, &buffer);
	}

	/*
	 * Adjust our iterator
	 */
	hammer_modify_volume(root_volume, rootmap, sizeof(*rootmap));
	rootmap->next_offset = alloc_offset + bytes;
	if (rootmap->alloc_offset < rootmap->next_offset)
		rootmap->alloc_offset = rootmap->next_offset;

	if (buffer)
		hammer_rel_buffer(buffer, 0);
	hammer_rel_volume(root_volume, 0);
	lockmgr(&hmp->blockmap_lock, LK_RELEASE);
	return(alloc_offset);
}

/*
 * Free (offset,bytes) in a zone
 */
void
hammer_blockmap_free(hammer_mount_t hmp, hammer_off_t bmap_off, int bytes)
{
	hammer_volume_t root_volume;
	hammer_blockmap_t rootmap;
	struct hammer_blockmap_layer1 *layer1;
	struct hammer_blockmap_layer2 *layer2;
	hammer_buffer_t buffer = NULL;
	hammer_off_t layer1_offset;
	hammer_off_t layer2_offset;
	int error;
	int zone;

	bytes = (bytes + 7) & ~7;
	KKASSERT(bytes <= HAMMER_BUFSIZE);
	zone = HAMMER_ZONE_DECODE(bmap_off);
	KKASSERT(zone >= HAMMER_ZONE_BTREE_INDEX && zone < HAMMER_MAX_ZONES);
	root_volume = hammer_get_root_volume(hmp, &error);
	if (error)
		return;
	rootmap = &root_volume->ondisk->vol0_blockmap[zone];
	KKASSERT(rootmap->phys_offset != 0);
	KKASSERT(HAMMER_ZONE_DECODE(rootmap->phys_offset) ==
		 HAMMER_ZONE_RAW_BUFFER_INDEX);
	KKASSERT(HAMMER_ZONE_DECODE(rootmap->alloc_offset) == zone);
	KKASSERT(((bmap_off ^ (bmap_off + (bytes - 1))) & 
		  ~HAMMER_LARGEBLOCK_MASK64) == 0);

	if (bmap_off >= rootmap->alloc_offset) {
		panic("hammer_blockmap_lookup: %016llx beyond EOF %016llx",
		      bmap_off, rootmap->alloc_offset);
		goto done;
	}

	/*
	 * Dive layer 1.
	 */
	layer1_offset = rootmap->phys_offset +
			HAMMER_BLOCKMAP_LAYER1_OFFSET(bmap_off);
	layer1 = hammer_bread(hmp, layer1_offset, &error, &buffer);
	KKASSERT(error == 0);
	KKASSERT(layer1->phys_offset);

	/*
	 * Dive layer 2, each entry represents a large-block.
	 */
	layer2_offset = layer1->phys_offset +
			HAMMER_BLOCKMAP_LAYER2_OFFSET(bmap_off);
	layer2 = hammer_bread(hmp, layer2_offset, &error, &buffer);

	KKASSERT(error == 0);
	KKASSERT(layer2->u.phys_offset);
	hammer_modify_buffer(buffer, layer2, sizeof(*layer2));
	layer2->bytes_free += bytes;

	/*
	 * If the big-block is free, return it to the free pool.  If our
	 * iterator is in the wholely free block, leave the block intact
	 * and reset the iterator.
	 */
	if (layer2->bytes_free == HAMMER_LARGEBLOCK_SIZE) {
		if ((rootmap->next_offset ^ bmap_off) &
		    ~HAMMER_LARGEBLOCK_MASK64) {
			hammer_freemap_free(hmp, layer2->u.phys_offset,
					    bmap_off, &error);
			layer2->u.phys_offset = HAMMER_BLOCKMAP_FREE;
		} else {
			hammer_modify_volume(root_volume, rootmap,
					     sizeof(*rootmap));
			rootmap->next_offset &= ~HAMMER_LARGEBLOCK_MASK64;
		}
	}
done:
	if (buffer)
		hammer_rel_buffer(buffer, 0);
	hammer_rel_volume(root_volume, 0);
}

/*
 * Lookup a blockmap offset.
 */
hammer_off_t
hammer_blockmap_lookup(hammer_mount_t hmp, hammer_off_t bmap_off, int *errorp)
{
	hammer_volume_t root_volume;
	hammer_blockmap_t rootmap;
	struct hammer_blockmap_layer1 *layer1;
	struct hammer_blockmap_layer2 *layer2;
	hammer_buffer_t buffer = NULL;
	hammer_off_t layer1_offset;
	hammer_off_t layer2_offset;
	hammer_off_t result_offset;
	int zone;

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
	 * Dive layer 1.
	 */
	layer1_offset = rootmap->phys_offset +
			HAMMER_BLOCKMAP_LAYER1_OFFSET(bmap_off);
	layer1 = hammer_bread(hmp, layer1_offset, errorp, &buffer);
	KKASSERT(*errorp == 0);
	KKASSERT(layer1->phys_offset);

	/*
	 * Dive layer 2, each entry represents a large-block.
	 */
	layer2_offset = layer1->phys_offset +
			HAMMER_BLOCKMAP_LAYER2_OFFSET(bmap_off);
	layer2 = hammer_bread(hmp, layer2_offset, errorp, &buffer);

	KKASSERT(*errorp == 0);
	KKASSERT(layer2->u.phys_offset);

	result_offset = layer2->u.phys_offset +
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

