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
 * $DragonFly: src/sys/vfs/hammer/hammer_blockmap.c,v 1.10 2008/05/03 05:28:55 dillon Exp $
 */

/*
 * HAMMER blockmap
 */
#include "hammer.h"

static hammer_off_t hammer_find_hole(hammer_mount_t hmp,
				   hammer_holes_t holes, int bytes);
static void hammer_add_hole(hammer_mount_t hmp, hammer_holes_t holes,
				    hammer_off_t offset, int bytes);

/*
 * Allocate bytes from a zone
 */
hammer_off_t
hammer_blockmap_alloc(hammer_transaction_t trans, int zone,
		      int bytes, int *errorp)
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
	int skip_amount;
	int used_hole;

	KKASSERT(zone >= HAMMER_ZONE_BTREE_INDEX && zone < HAMMER_MAX_ZONES);
	root_volume = hammer_get_root_volume(trans->hmp, errorp);
	if (*errorp)
		return(0);
	rootmap = &trans->hmp->blockmap[zone];
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
	KKASSERT(bytes > 0 && bytes <= HAMMER_BUFSIZE);

	lockmgr(&trans->hmp->blockmap_lock, LK_EXCLUSIVE|LK_RETRY);

	/*
	 * Try to use a known-free hole, otherwise append.
	 */
	next_offset = hammer_find_hole(trans->hmp, &trans->hmp->holes[zone],
				       bytes);
	if (next_offset == 0) {
		next_offset = rootmap->next_offset;
		used_hole = 0;
	} else {
		used_hole = 1;
	}

again:
	/*
	 * The allocation request may not cross a buffer boundary.
	 */
	tmp_offset = next_offset + bytes - 1;
	if ((next_offset ^ tmp_offset) & ~HAMMER_BUFMASK64) {
		skip_amount = HAMMER_BUFSIZE - 
			      ((int)next_offset & HAMMER_BUFMASK);
		hammer_add_hole(trans->hmp, &trans->hmp->holes[zone],
				next_offset, skip_amount);
		next_offset = tmp_offset & ~HAMMER_BUFMASK64;
	}

	/*
	 * Dive layer 1.  If we are starting a new layer 1 entry,
	 * allocate a layer 2 block for it.
	 */
	layer1_offset = rootmap->phys_offset +
			HAMMER_BLOCKMAP_LAYER1_OFFSET(next_offset);
	layer1 = hammer_bread(trans->hmp, layer1_offset, errorp, &buffer1);
	KKASSERT(*errorp == 0);
	KKASSERT(next_offset <= rootmap->alloc_offset);

	/*
	 * Allocate layer2 backing store in layer1 if necessary.  next_offset
	 * can skip to a bigblock boundary but alloc_offset is at least
	 * bigblock=aligned so that's ok.
	 */
	if (next_offset == rootmap->alloc_offset &&
	    ((next_offset & HAMMER_BLOCKMAP_LAYER2_MASK) == 0 ||
	    layer1->phys_offset == HAMMER_BLOCKMAP_FREE)
	) {
		KKASSERT((next_offset & HAMMER_BLOCKMAP_LAYER2_MASK) == 0);
		hammer_modify_buffer(trans, buffer1, layer1, sizeof(*layer1));
		bzero(layer1, sizeof(*layer1));
		layer1->phys_offset =
			hammer_freemap_alloc(trans, next_offset, errorp);
		layer1->blocks_free = HAMMER_BLOCKMAP_RADIX2;
		hammer_modify_buffer_done(buffer1);
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
		if (next_offset >= trans->hmp->zone_limits[zone]) {
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
	layer2 = hammer_bread(trans->hmp, layer2_offset, errorp, &buffer2);
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
			hammer_modify_buffer(trans, buffer1,
					     layer1, sizeof(*layer1));
			KKASSERT(layer1->blocks_free);
			--layer1->blocks_free;
			hammer_modify_buffer_done(buffer1);
			hammer_modify_buffer(trans, buffer2,
					     layer2, sizeof(*layer2));
			bzero(layer2, sizeof(*layer2));
			layer2->u.phys_offset =
				hammer_freemap_alloc(trans, next_offset,
						     errorp);
			layer2->bytes_free = HAMMER_LARGEBLOCK_SIZE;
			hammer_modify_buffer_done(buffer2);
			KKASSERT(*errorp == 0);
		} else if (layer2->bytes_free != HAMMER_LARGEBLOCK_SIZE) {
			/*
			 * We have encountered a block that is already
			 * partially allocated.  We must skip this block.
			 */
			kprintf("blockmap skip2 %016llx %d\n",
				next_offset, layer2->bytes_free);
			next_offset += HAMMER_LARGEBLOCK_SIZE;
			if (next_offset >= trans->hmp->zone_limits[zone]) {
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
		 * We are appending within a bigblock.  It is possible that
		 * the blockmap has been marked completely free via a prior
		 * pruning operation.  We no longer reset the append index
		 * for that case because it compromises the UNDO by allowing
		 * data overwrites.
		 */
		/*
		KKASSERT(layer2->u.phys_offset != HAMMER_BLOCKMAP_FREE);
		*/
	}

	hammer_modify_buffer(trans, buffer2, layer2, sizeof(*layer2));
	layer2->bytes_free -= bytes;
	hammer_modify_buffer_done(buffer2);
	KKASSERT(layer2->bytes_free >= 0);

	/*
	 * If the buffer was completely free we do not have to read it from
	 * disk, call hammer_bnew() to instantiate it.
	 */
	if ((next_offset & HAMMER_BUFMASK) == 0) {
		bigblock_offset = layer2->u.phys_offset +
				  (next_offset & HAMMER_LARGEBLOCK_MASK64);
		hammer_bnew(trans->hmp, bigblock_offset, errorp, &buffer3);
	}

	/*
	 * Adjust our iterator and alloc_offset.  The layer1 and layer2
	 * space beyond alloc_offset is uninitialized.  alloc_offset must
	 * be big-block aligned.
	 */
	if (used_hole == 0) {
		hammer_modify_volume(trans, root_volume, NULL, 0);
		rootmap->next_offset = next_offset + bytes;
		if (rootmap->alloc_offset < rootmap->next_offset) {
			rootmap->alloc_offset =
			    (rootmap->next_offset + HAMMER_LARGEBLOCK_MASK) &
			    ~HAMMER_LARGEBLOCK_MASK64;
		}
		hammer_modify_volume_done(root_volume);
	}
done:
	if (buffer1)
		hammer_rel_buffer(buffer1, 0);
	if (buffer2)
		hammer_rel_buffer(buffer2, 0);
	if (buffer3)
		hammer_rel_buffer(buffer3, 0);
	hammer_rel_volume(root_volume, 0);
	lockmgr(&trans->hmp->blockmap_lock, LK_RELEASE);
	return(next_offset);
}

/*
 * Free (offset,bytes) in a zone
 */
void
hammer_blockmap_free(hammer_transaction_t trans,
		     hammer_off_t bmap_off, int bytes)
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
	root_volume = hammer_get_root_volume(trans->hmp, &error);
	if (error)
		return;

	lockmgr(&trans->hmp->blockmap_lock, LK_EXCLUSIVE|LK_RETRY);

	rootmap = &trans->hmp->blockmap[zone];
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
	layer1 = hammer_bread(trans->hmp, layer1_offset, &error, &buffer1);
	KKASSERT(error == 0);
	KKASSERT(layer1->phys_offset);

	/*
	 * Dive layer 2, each entry represents a large-block.
	 */
	layer2_offset = layer1->phys_offset +
			HAMMER_BLOCKMAP_LAYER2_OFFSET(bmap_off);
	layer2 = hammer_bread(trans->hmp, layer2_offset, &error, &buffer2);

	KKASSERT(error == 0);
	KKASSERT(layer2->u.phys_offset);
	hammer_modify_buffer(trans, buffer2, layer2, sizeof(*layer2));
	layer2->bytes_free += bytes;
	KKASSERT(layer2->bytes_free <= HAMMER_LARGEBLOCK_SIZE);

	/*
	 * If the big-block is free, return it to the free pool.  If our
	 * iterator is in the wholely free block, leave the block intact
	 * and reset the iterator.
	 */
	if (layer2->bytes_free == HAMMER_LARGEBLOCK_SIZE) {
		if ((rootmap->next_offset ^ bmap_off) &
		    ~HAMMER_LARGEBLOCK_MASK64) {
			hammer_freemap_free(trans, layer2->u.phys_offset,
					    bmap_off, &error);
			layer2->u.phys_offset = HAMMER_BLOCKMAP_FREE;

			hammer_modify_buffer(trans, buffer1,
					     layer1, sizeof(*layer1));
			++layer1->blocks_free;
#if 0
			/* 
			 * XXX Not working yet - we aren't clearing it when
			 * reallocating the block later on.
			 */
			if (layer1->blocks_free == HAMMER_BLOCKMAP_RADIX2) {
				hammer_freemap_free(
					trans, layer1->phys_offset,
					bmap_off & ~HAMMER_BLOCKMAP_LAYER2_MASK,
					&error);
				layer1->phys_offset = HAMMER_BLOCKMAP_FREE;
			}
#endif
			hammer_modify_buffer_done(buffer1);
		} else {
			/*
			 * Leave block intact and reset the iterator. 
			 *
			 * XXX can't do this yet because if we allow data 
			 * allocations they could overwrite deleted data
			 * that is still subject to an undo on reboot.
			 */
#if 0
			hammer_modify_volume(trans, root_volume,
					     NULL, 0);
			rootmap->next_offset &= ~HAMMER_LARGEBLOCK_MASK64;
			hammer_modify_volume_done(root_volume);
#endif
		}
	}
	hammer_modify_buffer_done(buffer2);
done:
	lockmgr(&trans->hmp->blockmap_lock, LK_RELEASE);

	if (buffer1)
		hammer_rel_buffer(buffer1, 0);
	if (buffer2)
		hammer_rel_buffer(buffer2, 0);
	hammer_rel_volume(root_volume, 0);
}

/*
 * Return the number of free bytes in the big-block containing the
 * specified blockmap offset.
 */
int
hammer_blockmap_getfree(hammer_mount_t hmp, hammer_off_t bmap_off,
			int *curp, int *errorp)
{
	hammer_volume_t root_volume;
	hammer_blockmap_t rootmap;
	struct hammer_blockmap_layer1 *layer1;
	struct hammer_blockmap_layer2 *layer2;
	hammer_buffer_t buffer = NULL;
	hammer_off_t layer1_offset;
	hammer_off_t layer2_offset;
	int bytes;
	int zone;

	zone = HAMMER_ZONE_DECODE(bmap_off);
	KKASSERT(zone >= HAMMER_ZONE_BTREE_INDEX && zone < HAMMER_MAX_ZONES);
	root_volume = hammer_get_root_volume(hmp, errorp);
	if (*errorp) {
		*curp = 0;
		return(0);
	}
	rootmap = &hmp->blockmap[zone];
	KKASSERT(rootmap->phys_offset != 0);
	KKASSERT(HAMMER_ZONE_DECODE(rootmap->phys_offset) ==
		 HAMMER_ZONE_RAW_BUFFER_INDEX);
	KKASSERT(HAMMER_ZONE_DECODE(rootmap->alloc_offset) == zone);

	if (bmap_off >= rootmap->alloc_offset) {
		panic("hammer_blockmap_lookup: %016llx beyond EOF %016llx",
		      bmap_off, rootmap->alloc_offset);
		bytes = 0;
		*curp = 0;
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

	bytes = layer2->bytes_free;

	if ((rootmap->next_offset ^ bmap_off) & ~HAMMER_LARGEBLOCK_MASK64)
		*curp = 0;
	else
		*curp = 1;
done:
	if (buffer)
		hammer_rel_buffer(buffer, 0);
	hammer_rel_volume(root_volume, 0);
	if (hammer_debug_general & 0x0800) {
		kprintf("hammer_blockmap_getfree: %016llx -> %d\n",
			bmap_off, bytes);
	}
	return(bytes);
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
	rootmap = &hmp->blockmap[zone];
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

/************************************************************************
 *		    IN-CORE TRACKING OF ALLOCATION HOLES		*
 ************************************************************************
 *
 * This is a temporary shim in need of a more permanent solution.
 *
 * As we allocate space holes are created due to having to align to a new
 * 16K buffer when an allocation would otherwise cross the buffer boundary.
 * These holes are recorded here and used to fullfill smaller requests as
 * much as possible.  Only a limited number of holes are recorded and these
 * functions operate somewhat like a heuristic, where information is allowed
 * to be thrown away.
 */

void
hammer_init_holes(hammer_mount_t hmp, hammer_holes_t holes)
{
	TAILQ_INIT(&holes->list);
	holes->count = 0;
}

void
hammer_free_holes(hammer_mount_t hmp, hammer_holes_t holes)
{
	hammer_hole_t hole;

	while ((hole = TAILQ_FIRST(&holes->list)) != NULL) {
		TAILQ_REMOVE(&holes->list, hole, entry);
		kfree(hole, M_HAMMER);
	}
}

/*
 * Attempt to locate a hole with sufficient free space to accomodate the
 * requested allocation.  Return the offset or 0 if no hole could be found.
 */
static hammer_off_t
hammer_find_hole(hammer_mount_t hmp, hammer_holes_t holes, int bytes)
{
	hammer_hole_t hole;
	hammer_off_t result_off = 0;

	TAILQ_FOREACH(hole, &holes->list, entry) {
		if (bytes <= hole->bytes) {
			result_off = hole->offset;
			hole->offset += bytes;
			hole->bytes -= bytes;
			break;
		}
	}
	return(result_off);
}

/*
 * If a newly created hole is reasonably sized then record it.  We only
 * keep track of a limited number of holes.  Lost holes are recovered by
 * reblocking.
 */
static void
hammer_add_hole(hammer_mount_t hmp, hammer_holes_t holes,
		hammer_off_t offset, int bytes)
{
	hammer_hole_t hole;

	if (bytes <= 128)
		return;

	if (holes->count < HAMMER_MAX_HOLES) {
		hole = kmalloc(sizeof(*hole), M_HAMMER, M_WAITOK);
		++holes->count;
	} else {
		hole = TAILQ_FIRST(&holes->list);
		TAILQ_REMOVE(&holes->list, hole, entry);
	}
	TAILQ_INSERT_TAIL(&holes->list, hole, entry);
	hole->offset = offset;
	hole->bytes = bytes;
}

