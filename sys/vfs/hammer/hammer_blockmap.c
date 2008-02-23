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
 * $DragonFly: src/sys/vfs/hammer/hammer_blockmap.c,v 1.4 2008/02/23 03:01:08 dillon Exp $
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
	hammer_buffer_t buffer1 = NULL;
	hammer_buffer_t buffer2 = NULL;
	hammer_buffer_t buffer3 = NULL;
	hammer_off_t tmp_offset;
	hammer_off_t next_offset;
	hammer_off_t layer1_offset;
	hammer_off_t layer2_offset;
	hammer_off_t bigblock_offset;
	int loops = 0;

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
	next_offset = rootmap->next_offset;

again:
	/*
	 * The allocation request may not cross a buffer boundary
	 */
	tmp_offset = next_offset + bytes;
	if ((next_offset ^ (tmp_offset - 1)) & ~HAMMER_BUFMASK64)
		next_offset = (tmp_offset - 1) & ~HAMMER_BUFMASK64;

	/*
	 * Dive layer 1.  If we are starting a new layer 1 entry,
	 * allocate a layer 2 block for it.
	 */
	layer1_offset = rootmap->phys_offset +
			HAMMER_BLOCKMAP_LAYER1_OFFSET(next_offset);
	layer1 = hammer_bread(hmp, layer1_offset, errorp, &buffer1);
	KKASSERT(*errorp == 0);

	/*
	 * Allocate layer2 backing store in layer1 if necessary
	 */
	if ((next_offset == rootmap->alloc_offset &&
	    (next_offset & HAMMER_BLOCKMAP_LAYER2_MASK) == 0) ||
	    layer1->phys_offset == HAMMER_BLOCKMAP_FREE
	) {
		hammer_modify_buffer(buffer1, layer1, sizeof(*layer1));
		bzero(layer1, sizeof(*layer1));
		layer1->phys_offset = hammer_freemap_alloc(hmp, next_offset,
							   errorp);
		layer1->blocks_free = HAMMER_BLOCKMAP_RADIX2;
		KKASSERT(*errorp == 0);
	}
	KKASSERT(layer1->phys_offset);

	/*
	 * If layer1 indicates no free blocks in layer2 and our alloc_offset
	 * is not in layer2, skip layer2 entirely.
	 */
	if (layer1->blocks_free == 0 &&
	    ((next_offset ^ rootmap->alloc_offset) & ~HAMMER_BLOCKMAP_LAYER2_MASK) != 0) {
		kprintf("blockmap skip1 %016llx\n", next_offset);
		next_offset = (next_offset + HAMMER_BLOCKMAP_LAYER2_MASK) &
			      ~HAMMER_BLOCKMAP_LAYER2_MASK;
		if (next_offset >= hmp->zone_limits[zone]) {
			kprintf("blockmap wrap1\n");
			next_offset = HAMMER_ZONE_ENCODE(zone, 0);
			if (++loops == 2) {	/* XXX poor-man's */
				next_offset = 0;
				*errorp = ENOSPC;
				goto done;
			}
		}
		goto again;
	}

	/*
	 * Dive layer 2, each entry represents a large-block.
	 */
	layer2_offset = layer1->phys_offset +
			HAMMER_BLOCKMAP_LAYER2_OFFSET(next_offset);
	layer2 = hammer_bread(hmp, layer2_offset, errorp, &buffer2);
	KKASSERT(*errorp == 0);

	if ((next_offset & HAMMER_LARGEBLOCK_MASK64) == 0) {
		/*
		 * We are at the beginning of a new bigblock
		 */
		if (next_offset == rootmap->alloc_offset ||
		    layer2->u.phys_offset == HAMMER_BLOCKMAP_FREE) {
			/*
			 * Allocate the bigblock in layer2 if diving into
			 * uninitialized space or if the block was previously
			 * freed.
			 */
			hammer_modify_buffer(buffer1, layer1, sizeof(*layer1));
			KKASSERT(layer1->blocks_free);
			--layer1->blocks_free;
			hammer_modify_buffer(buffer2, layer2, sizeof(*layer2));
			bzero(layer2, sizeof(*layer2));
			layer2->u.phys_offset =
				hammer_freemap_alloc(hmp, next_offset, errorp);
			layer2->bytes_free = HAMMER_LARGEBLOCK_SIZE;
			KKASSERT(*errorp == 0);
		} else if (layer2->bytes_free != HAMMER_LARGEBLOCK_SIZE) {
			/*
			 * We have encountered a block that is already
			 * partially allocated.  We must skip this block.
			 */
			kprintf("blockmap skip2 %016llx\n", next_offset);
			next_offset += HAMMER_LARGEBLOCK_SIZE;
			if (next_offset >= hmp->zone_limits[zone]) {
				next_offset = HAMMER_ZONE_ENCODE(zone, 0);
				kprintf("blockmap wrap2\n");
				if (++loops == 2) {	/* XXX poor-man's */
					next_offset = 0;
					*errorp = ENOSPC;
					goto done;
				}
			}
			goto again;
		}
	} else {
		/*
		 * We are appending within a bigblock.
		 */
		KKASSERT(layer2->u.phys_offset != HAMMER_BLOCKMAP_FREE);
	}

	hammer_modify_buffer(buffer2, layer2, sizeof(*layer2));
	layer2->bytes_free -= bytes;

	/*
	 * If the buffer was completely free we do not have to read it from
	 * disk, call hammer_bnew() to instantiate it.
	 */
	if ((next_offset & HAMMER_BUFMASK) == 0) {
		bigblock_offset = layer2->u.phys_offset +
				  (next_offset & HAMMER_LARGEBLOCK_MASK64);
		hammer_bnew(hmp, bigblock_offset, errorp, &buffer3);
	}

	/*
	 * Adjust our iterator
	 */
	hammer_modify_volume(root_volume, rootmap, sizeof(*rootmap));
	rootmap->next_offset = next_offset + bytes;
	if (rootmap->alloc_offset < rootmap->next_offset)
		rootmap->alloc_offset = rootmap->next_offset;
done:
	if (buffer1)
		hammer_rel_buffer(buffer1, 0);
	if (buffer2)
		hammer_rel_buffer(buffer2, 0);
	if (buffer3)
		hammer_rel_buffer(buffer3, 0);
	hammer_rel_volume(root_volume, 0);
	lockmgr(&hmp->blockmap_lock, LK_RELEASE);
	return(next_offset);
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
	hammer_buffer_t buffer1 = NULL;
	hammer_buffer_t buffer2 = NULL;
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

	lockmgr(&hmp->blockmap_lock, LK_EXCLUSIVE|LK_RETRY);

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
	layer1 = hammer_bread(hmp, layer1_offset, &error, &buffer1);
	KKASSERT(error == 0);
	KKASSERT(layer1->phys_offset);

	/*
	 * Dive layer 2, each entry represents a large-block.
	 */
	layer2_offset = layer1->phys_offset +
			HAMMER_BLOCKMAP_LAYER2_OFFSET(bmap_off);
	layer2 = hammer_bread(hmp, layer2_offset, &error, &buffer2);

	KKASSERT(error == 0);
	KKASSERT(layer2->u.phys_offset);
	hammer_modify_buffer(buffer2, layer2, sizeof(*layer2));
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

			hammer_modify_buffer(buffer1, layer1, sizeof(*layer1));
			++layer1->blocks_free;
			if (layer1->blocks_free == HAMMER_BLOCKMAP_RADIX2) {
				hammer_freemap_free(
					hmp, layer1->phys_offset,
					bmap_off & ~HAMMER_BLOCKMAP_LAYER2_MASK,
					&error);
				layer1->phys_offset = HAMMER_BLOCKMAP_FREE;
			}
		} else {
			hammer_modify_volume(root_volume, rootmap,
					     sizeof(*rootmap));
			rootmap->next_offset &= ~HAMMER_LARGEBLOCK_MASK64;
		}
	}
done:
	lockmgr(&hmp->blockmap_lock, LK_RELEASE);

	if (buffer1)
		hammer_rel_buffer(buffer1, 0);
	if (buffer2)
		hammer_rel_buffer(buffer2, 0);
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

