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
 * $DragonFly: src/sys/vfs/hammer/hammer_blockmap.c,v 1.16 2008/06/08 18:16:26 dillon Exp $
 */

/*
 * HAMMER blockmap
 */
#include "hammer.h"

static hammer_off_t hammer_find_hole(hammer_mount_t hmp,
				   hammer_holes_t holes, int bytes);
static void hammer_add_hole(hammer_mount_t hmp, hammer_holes_t holes,
				   hammer_off_t zone_offset, int bytes);
static void hammer_clean_holes(hammer_mount_t hmp, hammer_holes_t holes,
				   hammer_off_t base_offset);
static int hammer_res_rb_compare(hammer_reserve_t res1, hammer_reserve_t res2);

/*
 * Reserved big-blocks red-black tree support
 */
RB_GENERATE2(hammer_res_rb_tree, hammer_reserve, rb_node,
	     hammer_res_rb_compare, hammer_off_t, zone_offset);

static int
hammer_res_rb_compare(hammer_reserve_t res1, hammer_reserve_t res2)
{
	if (res1->zone_offset < res2->zone_offset)
		return(-1);
	if (res1->zone_offset > res2->zone_offset)
		return(1);
	return(0);
}

/*
 * Allocate a big-block from the freemap and stuff it into the blockmap
 * at layer1/layer2.
 */
static void
hammer_blockmap_llalloc(hammer_transaction_t trans,
		hammer_off_t zone_offset, int *errorp,
		hammer_buffer_t buffer1, hammer_blockmap_layer1_t layer1,
		hammer_buffer_t buffer2, hammer_blockmap_layer2_t layer2)
{
	hammer_off_t zone2_offset;

	zone2_offset = hammer_freemap_alloc(trans, zone_offset, errorp);
	if (*errorp)
		return;
	hammer_modify_buffer(trans, buffer1, layer1, sizeof(*layer1));
	KKASSERT(layer1->blocks_free);
	--layer1->blocks_free;
	layer1->layer1_crc = crc32(layer1, HAMMER_LAYER1_CRCSIZE);
	hammer_modify_buffer_done(buffer1);
	hammer_modify_buffer(trans, buffer2, layer2, sizeof(*layer2));
	bzero(layer2, sizeof(*layer2));
	layer2->u.phys_offset = zone2_offset;
	layer2->bytes_free = HAMMER_LARGEBLOCK_SIZE;
	layer2->entry_crc = crc32(layer2, HAMMER_LAYER2_CRCSIZE);
	hammer_modify_buffer_done(buffer2);
}


/*
 * Allocate bytes from a zone
 */
hammer_off_t
hammer_blockmap_alloc(hammer_transaction_t trans, int zone,
		      int bytes, int *errorp)
{
	hammer_mount_t hmp;
	hammer_volume_t root_volume;
	hammer_blockmap_t rootmap;
	hammer_reserve_t resv;
	struct hammer_blockmap_layer1 *layer1;
	struct hammer_blockmap_layer2 *layer2;
	hammer_buffer_t buffer1 = NULL;
	hammer_buffer_t buffer2 = NULL;
	hammer_buffer_t buffer3 = NULL;
	hammer_off_t tmp_offset;
	hammer_off_t next_offset;
	hammer_off_t result_offset;
	hammer_off_t layer1_offset;
	hammer_off_t layer2_offset;
	int loops = 0;
	int skip_amount;

	hmp = trans->hmp;

	/*
	 * Deal with alignment and buffer-boundary issues.
	 *
	 * Be careful, certain primary alignments are used below to allocate
	 * new blockmap blocks.
	 */
	bytes = (bytes + 15) & ~15;
	KKASSERT(bytes > 0 && bytes <= HAMMER_BUFSIZE);
	KKASSERT(zone >= HAMMER_ZONE_BTREE_INDEX && zone < HAMMER_MAX_ZONES);

	/*
	 * Try to use a known-free hole.
	 */
	result_offset = hammer_find_hole(hmp, &trans->hmp->holes[zone], bytes);
	if (result_offset) {
		*errorp = 0;
		hammer_blockmap_free(trans, result_offset, -bytes);
		return(result_offset);
	}

	/*
	 * Otherwise scan for space
	 */
	root_volume = hammer_get_root_volume(hmp, errorp);
	if (*errorp)
		return(0);
	rootmap = &hmp->blockmap[zone];
	KKASSERT(rootmap->phys_offset != 0);
	KKASSERT(HAMMER_ZONE_DECODE(rootmap->phys_offset) ==
		 HAMMER_ZONE_RAW_BUFFER_INDEX);
	KKASSERT(HAMMER_ZONE_DECODE(rootmap->alloc_offset) == zone);
	KKASSERT(HAMMER_ZONE_DECODE(rootmap->next_offset) == zone);

	lockmgr(&hmp->blockmap_lock, LK_EXCLUSIVE|LK_RETRY);
	next_offset = rootmap->next_offset;
again:
	/*
	 * The allocation request may not cross a buffer boundary.
	 */
	tmp_offset = next_offset + bytes - 1;
	if ((next_offset ^ tmp_offset) & ~HAMMER_BUFMASK64) {
		skip_amount = HAMMER_BUFSIZE - 
			      ((int)next_offset & HAMMER_BUFMASK);
		hammer_add_hole(hmp, &hmp->holes[zone],
				next_offset, skip_amount);
		next_offset = tmp_offset & ~HAMMER_BUFMASK64;
	}

	/*
	 * Dive layer 1.  If we are starting a new layer 1 entry,
	 * allocate a layer 2 block for it.
	 */
	layer1_offset = rootmap->phys_offset +
			HAMMER_BLOCKMAP_LAYER1_OFFSET(next_offset);
	layer1 = hammer_bread(hmp, layer1_offset, errorp, &buffer1);
	KKASSERT(*errorp == 0);
	KKASSERT(next_offset <= rootmap->alloc_offset);

	/*
	 * Check CRC if not allocating into uninitialized space
	 */
	if ((next_offset != rootmap->alloc_offset) ||
	    (next_offset & HAMMER_BLOCKMAP_LAYER2_MASK)) {
		if (layer1->layer1_crc != crc32(layer1,
						HAMMER_LAYER1_CRCSIZE)) {
			Debugger("CRC FAILED: LAYER1");
		}
	}

	/*
	 * Allocate layer2 backing store in layer1 if necessary.  next_offset
	 * can skip to a bigblock boundary but alloc_offset is at least
	 * bigblock-aligned so that's ok.
	 */
	if ((next_offset == rootmap->alloc_offset &&
	    (next_offset & HAMMER_BLOCKMAP_LAYER2_MASK) == 0) ||
	    layer1->phys_offset == HAMMER_BLOCKMAP_FREE
	) {
		KKASSERT((next_offset & HAMMER_BLOCKMAP_LAYER2_MASK) == 0);
		hammer_modify_buffer(trans, buffer1, layer1, sizeof(*layer1));
		bzero(layer1, sizeof(*layer1));
		layer1->phys_offset =
			hammer_freemap_alloc(trans, next_offset, errorp);
		layer1->blocks_free = HAMMER_BLOCKMAP_RADIX2;
		layer1->layer1_crc = crc32(layer1, HAMMER_LAYER1_CRCSIZE);
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
		next_offset = (next_offset + HAMMER_BLOCKMAP_LAYER2_MASK) &
			      ~HAMMER_BLOCKMAP_LAYER2_MASK;
		if (next_offset >= hmp->zone_limits[zone]) {
			hkprintf("blockmap wrap1\n");
			next_offset = HAMMER_ZONE_ENCODE(zone, 0);
			if (++loops == 2) {	/* XXX poor-man's */
				result_offset = 0;
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

	/*
	 * Check CRC if not allocating into uninitialized space
	 */
	if (next_offset != rootmap->alloc_offset ||
	    (next_offset & HAMMER_LARGEBLOCK_MASK64)) {
		if (layer2->entry_crc != crc32(layer2, HAMMER_LAYER2_CRCSIZE)) {
			Debugger("CRC FAILED: LAYER2");
		}
	}

	if ((next_offset & HAMMER_LARGEBLOCK_MASK64) == 0) {
		/*
		 * We are at the beginning of a new bigblock
		 */
		resv = RB_LOOKUP(hammer_res_rb_tree, &hmp->rb_resv_root,
				 next_offset & ~HAMMER_LARGEBLOCK_MASK64);

		if (resv) {
			goto skip;
		} else if (next_offset == rootmap->alloc_offset ||
			   layer2->u.phys_offset == HAMMER_BLOCKMAP_FREE) {
			/*
			 * Allocate the bigblock in layer2 if diving into
			 * uninitialized space or if the block was previously
			 * freed.
			 */
			hammer_blockmap_llalloc(trans,
						next_offset, errorp,
						buffer1, layer1,
						buffer2, layer2);
			KKASSERT(layer2->u.phys_offset != HAMMER_BLOCKMAP_FREE);
		} else if (layer2->bytes_free != HAMMER_LARGEBLOCK_SIZE) {
			/*
			 * We have encountered a block that is already
			 * partially allocated.  We must skip this block.
			 */
skip:
			next_offset += HAMMER_LARGEBLOCK_SIZE;
			if (next_offset >= trans->hmp->zone_limits[zone]) {
				next_offset = HAMMER_ZONE_ENCODE(zone, 0);
				hkprintf("blockmap wrap2\n");
				if (++loops == 2) {	/* XXX poor-man's */
					result_offset = 0;
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
	layer2->entry_crc = crc32(layer2, HAMMER_LAYER2_CRCSIZE);
	hammer_modify_buffer_done(buffer2);
	KKASSERT(layer2->bytes_free >= 0);

	/*
	 * If we are allocating from the base of a new buffer we can avoid
	 * a disk read by calling hammer_bnew().
	 */
	if ((next_offset & HAMMER_BUFMASK) == 0) {
		hammer_bnew(trans->hmp, next_offset, errorp, &buffer3);
	}
	result_offset = next_offset;

	/*
	 * Process allocated result_offset
	 */
done:
	hammer_modify_volume(NULL, root_volume, NULL, 0);
	if (result_offset) {
		if (result_offset == next_offset) {
			rootmap->next_offset = next_offset + bytes;
		} else {
			rootmap->next_offset = next_offset;
		}
	} else {
		rootmap->next_offset = next_offset;
	}
	if (rootmap->alloc_offset < rootmap->next_offset) {
		rootmap->alloc_offset =
		    (rootmap->next_offset + HAMMER_LARGEBLOCK_MASK) &
		    ~HAMMER_LARGEBLOCK_MASK64;
	}
	hammer_modify_volume_done(root_volume);
	lockmgr(&trans->hmp->blockmap_lock, LK_RELEASE);

	/*
	 * Cleanup
	 */
	if (buffer1)
		hammer_rel_buffer(buffer1, 0);
	if (buffer2)
		hammer_rel_buffer(buffer2, 0);
	if (buffer3)
		hammer_rel_buffer(buffer3, 0);
	hammer_rel_volume(root_volume, 0);

	return(result_offset);
}

/*
 * Front-end blockmap reservation
 *
 * This code reserves bytes out of a blockmap without committing to any
 * meta-data modifications, allowing the front-end to issue disk write I/O
 * for large blocks of data without having to queue the BIOs to the back-end.
 * If the reservation winds up not being used, for example due to a crash,
 * the reblocker should eventually come along and clean it up.
 *
 * This code will attempt to assign free big-blocks to the blockmap to
 * accomodate the request.
 *
 * If we return 0 a reservation was not possible and the caller must queue
 * the I/O to the backend.
 */
hammer_reserve_t
hammer_blockmap_reserve(hammer_mount_t hmp, int zone, int bytes,
			hammer_off_t *zone_offp, int *errorp)
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
	hammer_reserve_t resv;
	int loops = 0;
	int skip_amount;

	/*
	 * Setup
	 */
	KKASSERT(zone >= HAMMER_ZONE_BTREE_INDEX && zone < HAMMER_MAX_ZONES);
	root_volume = hammer_get_root_volume(hmp, errorp);
	if (*errorp)
		return(NULL);
	rootmap = &hmp->blockmap[zone];
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
	bytes = (bytes + 15) & ~15;
	KKASSERT(bytes > 0 && bytes <= HAMMER_BUFSIZE);

	lockmgr(&hmp->blockmap_lock, LK_EXCLUSIVE|LK_RETRY);

	/*
	 * Starting zoneX offset.  The reservation code always wraps at the
	 * alloc_offset (the allocation code is allowed to go through to the
	 * limit).
	 */
	next_offset = rootmap->next_offset;
again:
	resv = NULL;
	if (next_offset >= rootmap->alloc_offset) {
		if (++loops == 2) {	/* XXX poor-man's */
			*errorp = ENOSPC;
			goto done;
		}
		next_offset = HAMMER_ZONE_ENCODE(zone, 0);
	}

	/*
	 * The allocation request may not cross a buffer boundary.
	 */
	tmp_offset = next_offset + bytes - 1;
	if ((next_offset ^ tmp_offset) & ~HAMMER_BUFMASK64) {
		skip_amount = HAMMER_BUFSIZE - 
			      ((int)next_offset & HAMMER_BUFMASK);
		hammer_add_hole(hmp, &hmp->holes[zone],
				next_offset, skip_amount);
		next_offset = tmp_offset & ~HAMMER_BUFMASK64;
	}

	/*
	 * Dive layer 1.
	 */
	layer1_offset = rootmap->phys_offset +
			HAMMER_BLOCKMAP_LAYER1_OFFSET(next_offset);
	layer1 = hammer_bread(hmp, layer1_offset, errorp, &buffer1);
	KKASSERT(*errorp == 0);
	KKASSERT(next_offset <= rootmap->alloc_offset);

	/*
	 * Check CRC if not allocating into uninitialized space
	 */
	if (layer1->layer1_crc != crc32(layer1, HAMMER_LAYER1_CRCSIZE)) {
		Debugger("CRC FAILED: LAYER1");
	}
	KKASSERT(layer1->phys_offset);

	/*
	 * If layer1 indicates no free blocks in layer2 and our alloc_offset
	 * is not in layer2, skip layer2 entirely.
	 */
	if (layer1->blocks_free == 0 &&
	    ((next_offset ^ rootmap->alloc_offset) & ~HAMMER_BLOCKMAP_LAYER2_MASK) != 0) {
		next_offset = (next_offset + HAMMER_BLOCKMAP_LAYER2_MASK) &
			      ~HAMMER_BLOCKMAP_LAYER2_MASK;
		goto again;
	}

	/*
	 * Dive layer 2, each entry represents a large-block.
	 */
	layer2_offset = layer1->phys_offset +
			HAMMER_BLOCKMAP_LAYER2_OFFSET(next_offset);
	layer2 = hammer_bread(hmp, layer2_offset, errorp, &buffer2);
	KKASSERT(*errorp == 0);

	/*
	 * Check CRC if not allocating into uninitialized space (which we
	 * aren't when reserving space).
	 */
	if (layer2->entry_crc != crc32(layer2, HAMMER_LAYER2_CRCSIZE)) {
		Debugger("CRC FAILED: LAYER2");
	}

	/*
	 * Acquire the related reservation structure.  If it exists we can
	 * only use the bigblock if our current next_offset is already in
	 * it.
	 */
	resv = RB_LOOKUP(hammer_res_rb_tree, &hmp->rb_resv_root,
			 next_offset & ~HAMMER_LARGEBLOCK_MASK64);

	if ((next_offset & HAMMER_LARGEBLOCK_MASK64) == 0) {
		/*
		 * We are at the beginning of a new bigblock.
		 *
		 * (1) If the bigblock has already been reserved do not
		 *     try to use it, skip it.
		 *
		 * (2) If the bigblock has not been allocated then allocate
		 *     it.
		 *
		 * (3) If the bigblock is not completely free we have no
		 *     visibility into what portions may have been allocated,
		 *     so skip it.
		 */

		if (resv) {
			next_offset += HAMMER_LARGEBLOCK_SIZE;
			goto again;
		} else if (layer2->u.phys_offset == HAMMER_BLOCKMAP_FREE) {
			struct hammer_transaction trans;

			hammer_start_transaction(&trans, hmp);
			if (hammer_sync_lock_sh_try(&trans) == 0) {
				hammer_blockmap_llalloc(&trans,
							next_offset, errorp,
							buffer1, layer1,
							buffer2, layer2);
				hammer_sync_unlock(&trans);
			} else {
				hammer_sync_lock_sh(&trans);
				hammer_blockmap_llalloc(&trans,
							next_offset, errorp,
							buffer1, layer1,
							buffer2, layer2);
				hammer_sync_unlock(&trans);
				/* *errorp = EDEADLK; */
			}
			hammer_done_transaction(&trans);
			if (layer2->u.phys_offset == HAMMER_BLOCKMAP_FREE) {
				resv = NULL;
				goto done;
			}
		} else if (layer2->bytes_free != HAMMER_LARGEBLOCK_SIZE) {
			/*
			 * We have encountered a block that is already
			 * partially allocated.  We must skip this block.
			 */
			next_offset += HAMMER_LARGEBLOCK_SIZE;
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
		KKASSERT(layer2->u.phys_offset != HAMMER_BLOCKMAP_FREE);
		KKASSERT(layer2->bytes_free >= HAMMER_LARGEBLOCK_SIZE - (int)(next_offset & HAMMER_LARGEBLOCK_MASK64));
	}

	/*
	 * The reservation code does not modify layer2->bytes_free, it
	 * simply adjusts next_offset.
	 */
	KKASSERT(layer2->bytes_free >= 0);

	/*
	 * If we are not reserving a whole buffer but are at the start of
	 * a new block, call hammer_bnew() to avoid a disk read.
	 *
	 * If we are reserving a whole buffer the caller will probably use
	 * a direct read, so do nothing.
	 */
	if (bytes < HAMMER_BUFSIZE && (next_offset & HAMMER_BUFMASK) == 0) {
		hammer_bnew(hmp, next_offset, errorp, &buffer3);
	}

	/*
	 * Make the reservation
	 */
	if (resv) {
		++resv->refs;
	} else {
		resv = kmalloc(sizeof(*resv), M_HAMMER, M_WAITOK|M_ZERO);
		resv->refs = 1;
		resv->zone_offset = next_offset & ~HAMMER_LARGEBLOCK_MASK64;
		RB_INSERT(hammer_res_rb_tree, &hmp->rb_resv_root, resv);
		++hammer_count_reservations;
	}

	/*
	 * Adjust our iterator and alloc_offset.  The layer1 and layer2
	 * space beyond alloc_offset is uninitialized.  alloc_offset must
	 * be big-block aligned.
	 */
done:
	if (resv) {
		hammer_modify_volume(NULL, root_volume, NULL, 0);
		rootmap->next_offset = next_offset + bytes;
		hammer_modify_volume_done(root_volume);
	} else if (rootmap->next_offset != next_offset) {
		hammer_modify_volume(NULL, root_volume, NULL, 0);
		rootmap->next_offset = next_offset;
		hammer_modify_volume_done(root_volume);
	}

	if (buffer1)
		hammer_rel_buffer(buffer1, 0);
	if (buffer2)
		hammer_rel_buffer(buffer2, 0);
	if (buffer3)
		hammer_rel_buffer(buffer3, 0);
	hammer_rel_volume(root_volume, 0);
	lockmgr(&hmp->blockmap_lock, LK_RELEASE);
	*zone_offp = next_offset;

	return(resv);
}

/*
 * A record with a storage resolution calls this function when it is
 * being freed.  The storage may or may not have actually been allocated.
 */
void
hammer_blockmap_reserve_complete(hammer_mount_t hmp, hammer_reserve_t resv)
{
	KKASSERT(resv->refs > 0);
	if (--resv->refs == 0) {
		RB_REMOVE(hammer_res_rb_tree, &hmp->rb_resv_root, resv);
		kfree(resv, M_HAMMER);
		--hammer_count_reservations;
	}
}

/*
 * Free (offset,bytes) in a zone.
 *
 * If bytes is negative we are actually allocating previously reserved
 * space in the zone.
 */
void
hammer_blockmap_free(hammer_transaction_t trans,
		     hammer_off_t bmap_off, int bytes)
{
	hammer_mount_t hmp;
	hammer_volume_t root_volume;
	hammer_reserve_t resv;
	hammer_blockmap_t rootmap;
	struct hammer_blockmap_layer1 *layer1;
	struct hammer_blockmap_layer2 *layer2;
	hammer_buffer_t buffer1 = NULL;
	hammer_buffer_t buffer2 = NULL;
	hammer_off_t layer1_offset;
	hammer_off_t layer2_offset;
	int error;
	int zone;

	hmp = trans->hmp;

	if (bytes >= 0) {
		bytes = (bytes + 15) & ~15;
		KKASSERT(bytes <= HAMMER_BUFSIZE);
		KKASSERT(((bmap_off ^ (bmap_off + (bytes - 1))) & 
			  ~HAMMER_LARGEBLOCK_MASK64) == 0);
	} else {
		KKASSERT(bytes >= -HAMMER_BUFSIZE);
	}
	zone = HAMMER_ZONE_DECODE(bmap_off);
	KKASSERT(zone >= HAMMER_ZONE_BTREE_INDEX && zone < HAMMER_MAX_ZONES);
	root_volume = hammer_get_root_volume(hmp, &error);
	if (error)
		return;

	lockmgr(&hmp->blockmap_lock, LK_EXCLUSIVE|LK_RETRY);

	rootmap = &hmp->blockmap[zone];
	KKASSERT(rootmap->phys_offset != 0);
	KKASSERT(HAMMER_ZONE_DECODE(rootmap->phys_offset) ==
		 HAMMER_ZONE_RAW_BUFFER_INDEX);
	KKASSERT(HAMMER_ZONE_DECODE(rootmap->alloc_offset) == zone);

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
	if (layer1->layer1_crc != crc32(layer1, HAMMER_LAYER1_CRCSIZE)) {
		Debugger("CRC FAILED: LAYER1");
	}

	/*
	 * Dive layer 2, each entry represents a large-block.
	 */
	layer2_offset = layer1->phys_offset +
			HAMMER_BLOCKMAP_LAYER2_OFFSET(bmap_off);
	layer2 = hammer_bread(hmp, layer2_offset, &error, &buffer2);
	KKASSERT(error == 0);
	KKASSERT(layer2->u.phys_offset);
	if (layer2->entry_crc != crc32(layer2, HAMMER_LAYER2_CRCSIZE)) {
		Debugger("CRC FAILED: LAYER2");
	}

	hammer_modify_buffer(trans, buffer2, layer2, sizeof(*layer2));
	layer2->bytes_free += bytes;
	KKASSERT(layer2->bytes_free <= HAMMER_LARGEBLOCK_SIZE);

	/*
	 * If the big-block is free, return it to the free pool.  The layer2
	 * infrastructure is left intact even if the entire layer2 becomes
	 * free.
	 *
	 * At the moment if our iterator is in a bigblock that becomes
	 * wholely free, we have to leave the block allocated and we cannot
	 * reset the iterator because there may be UNDOs on-disk that
	 * reference areas of that block and we cannot overwrite those areas.
	 */
	if (layer2->bytes_free == HAMMER_LARGEBLOCK_SIZE) {
		hammer_off_t base_off;

		base_off = bmap_off & ~HAMMER_LARGEBLOCK_MASK64;
		resv = RB_LOOKUP(hammer_res_rb_tree, &hmp->rb_resv_root,
				 base_off);

		if (resv) {
			/*
			 * Portions of this block have been reserved, do
			 * not free it.
			 */
		} else if ((rootmap->next_offset ^ bmap_off) &
			    ~HAMMER_LARGEBLOCK_MASK64) {
			/*
			 * Our iterator is not in the now-free big-block
			 * and we can release it.
			 */
			hammer_clean_holes(hmp, &trans->hmp->holes[zone],
					   base_off);
			hammer_del_buffers(hmp, base_off,
					   layer2->u.phys_offset,
					   HAMMER_LARGEBLOCK_SIZE);
			hammer_freemap_free(trans, layer2->u.phys_offset,
					    bmap_off, &error);
			layer2->u.phys_offset = HAMMER_BLOCKMAP_FREE;

			hammer_modify_buffer(trans, buffer1,
					     layer1, sizeof(*layer1));
			++layer1->blocks_free;
#if 0
			/*
			 * This commented out code would release the layer2
			 * bigblock.  We do not want to do this, at least
			 * not right now.
			 *
			 * This also may be incomplete.
			 */
			if (layer1->blocks_free == HAMMER_BLOCKMAP_RADIX2) {
				hammer_freemap_free(
					trans, layer1->phys_offset,
					bmap_off & ~HAMMER_BLOCKMAP_LAYER2_MASK,
					&error);
				layer1->phys_offset = HAMMER_BLOCKMAP_FREE;
			}
#endif
			layer1->layer1_crc = crc32(layer1,
						   HAMMER_LAYER1_CRCSIZE);
			hammer_modify_buffer_done(buffer1);
		} else {
#if 0
			/*
			 * This commented out code would reset the iterator,
			 * which we cannot do at the moment as it could cause
			 * new allocations to overwrite deleted data still
			 * subject to undo on reboot.
			 */
			hammer_modify_volume(trans, root_volume,
					     NULL, 0);
			rootmap->next_offset &= ~HAMMER_LARGEBLOCK_MASK64;
			hammer_modify_volume_done(root_volume);
#endif
		}
	}
	layer2->entry_crc = crc32(layer2, HAMMER_LAYER2_CRCSIZE);
	hammer_modify_buffer_done(buffer2);
done:
	lockmgr(&hmp->blockmap_lock, LK_RELEASE);

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
	if (layer1->layer1_crc != crc32(layer1, HAMMER_LAYER1_CRCSIZE)) {
		Debugger("CRC FAILED: LAYER1");
	}

	/*
	 * Dive layer 2, each entry represents a large-block.
	 */
	layer2_offset = layer1->phys_offset +
			HAMMER_BLOCKMAP_LAYER2_OFFSET(bmap_off);
	layer2 = hammer_bread(hmp, layer2_offset, errorp, &buffer);
	KKASSERT(*errorp == 0);
	KKASSERT(layer2->u.phys_offset);
	if (layer2->entry_crc != crc32(layer2, HAMMER_LAYER2_CRCSIZE)) {
		Debugger("CRC FAILED: LAYER2");
	}

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
	if (layer1->layer1_crc != crc32(layer1, HAMMER_LAYER1_CRCSIZE)) {
		Debugger("CRC FAILED: LAYER1");
	}

	/*
	 * Dive layer 2, each entry represents a large-block.
	 */
	layer2_offset = layer1->phys_offset +
			HAMMER_BLOCKMAP_LAYER2_OFFSET(bmap_off);
	layer2 = hammer_bread(hmp, layer2_offset, errorp, &buffer);

	KKASSERT(*errorp == 0);
	KKASSERT(layer2->u.phys_offset);
	if (layer2->entry_crc != crc32(layer2, HAMMER_LAYER2_CRCSIZE)) {
		Debugger("CRC FAILED: LAYER2");
	}

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
		if (hole->resv) {
			hammer_blockmap_reserve_complete(hmp, hole->resv);
			hole->resv = NULL;
		}
		kfree(hole, M_HAMMER);
		--holes->count;
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
			result_off = hole->zone_offset;
			hole->zone_offset += bytes;
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
 *
 * offset is a zone-N offset.
 */
static void
hammer_add_hole(hammer_mount_t hmp, hammer_holes_t holes,
		hammer_off_t zone_offset, int bytes)
{
	hammer_hole_t hole;
	hammer_reserve_t resv;

	if (bytes <= 128)
		return;

	/*
	 * Allocate or reuse a hole structure
	 */
	if (holes->count < HAMMER_MAX_HOLES) {
		hole = kmalloc(sizeof(*hole), M_HAMMER, M_WAITOK);
		++holes->count;
	} else {
		hole = TAILQ_FIRST(&holes->list);
		TAILQ_REMOVE(&holes->list, hole, entry);
		if (hole->resv) {
			hammer_blockmap_reserve_complete(hmp, hole->resv);
			hole->resv = NULL;
		}
	}

	/*
	 * Associate the structure with the appropriate reservation so the
	 * bigblock does not get freed or reused while we have cached holes,
	 * and install.
	 */
	hole->zone_offset = zone_offset;
	hole->bytes = bytes;

	zone_offset &= ~HAMMER_LARGEBLOCK_MASK64;

	resv = RB_LOOKUP(hammer_res_rb_tree, &hmp->rb_resv_root, zone_offset);
	if (resv == NULL) {
		resv = kmalloc(sizeof(*resv), M_HAMMER, M_WAITOK|M_ZERO);
		resv->zone_offset = zone_offset;
		resv->refs = 1;
		RB_INSERT(hammer_res_rb_tree, &hmp->rb_resv_root, resv);
		++hammer_count_reservations;
	} else {
		++resv->refs;
	}
	hole->resv = resv;
	TAILQ_INSERT_TAIL(&holes->list, hole, entry);
}

/*
 * Clean out any holes cached for the bigblock we are about to release back
 * to the free pool.
 */
static void
hammer_clean_holes(hammer_mount_t hmp, hammer_holes_t holes,
		   hammer_off_t base_offset)
{
	hammer_hole_t hole;

restart:
	TAILQ_FOREACH(hole, &holes->list, entry) {
		if ((hole->zone_offset & ~HAMMER_LARGEBLOCK_MASK64) == 
		    base_offset) {
			TAILQ_REMOVE(&holes->list, hole, entry);
			if (hole->resv) {
				hammer_blockmap_reserve_complete(hmp,
								 hole->resv);
				hole->resv = NULL;
			}
			--holes->count;
			kfree(hole, M_HAMMER);
			goto restart;
		}
	}
}

