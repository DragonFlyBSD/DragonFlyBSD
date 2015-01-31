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
 * HAMMER blockmap
 */
#include "hammer.h"

static int hammer_res_rb_compare(hammer_reserve_t res1, hammer_reserve_t res2);
static void hammer_reserve_setdelay_offset(hammer_mount_t hmp,
				    hammer_off_t base_offset, int zone,
				    struct hammer_blockmap_layer2 *layer2);
static void hammer_reserve_setdelay(hammer_mount_t hmp, hammer_reserve_t resv);
static int update_bytes_free(hammer_reserve_t resv, int bytes);

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
 * Allocate bytes from a zone
 */
hammer_off_t
hammer_blockmap_alloc(hammer_transaction_t trans, int zone, int bytes,
		      hammer_off_t hint, int *errorp)
{
	hammer_mount_t hmp;
	hammer_volume_t root_volume;
	hammer_blockmap_t blockmap;
	hammer_blockmap_t freemap;
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
	hammer_off_t base_off;
	int loops = 0;
	int offset;		/* offset within big-block */
	int use_hint;

	hmp = trans->hmp;

	/*
	 * Deal with alignment and buffer-boundary issues.
	 *
	 * Be careful, certain primary alignments are used below to allocate
	 * new blockmap blocks.
	 */
	bytes = (bytes + 15) & ~15;
	KKASSERT(bytes > 0 && bytes <= HAMMER_XBUFSIZE);
	KKASSERT(zone >= HAMMER_ZONE_BTREE_INDEX && zone < HAMMER_MAX_ZONES);

	/*
	 * Setup
	 */
	root_volume = trans->rootvol;
	*errorp = 0;
	blockmap = &hmp->blockmap[zone];
	freemap = &hmp->blockmap[HAMMER_ZONE_FREEMAP_INDEX];
	KKASSERT(HAMMER_ZONE_DECODE(blockmap->next_offset) == zone);

	/*
	 * Use the hint if we have one.
	 */
	if (hint && HAMMER_ZONE_DECODE(hint) == zone) {
		next_offset = (hint + 15) & ~(hammer_off_t)15;
		use_hint = 1;
	} else {
		next_offset = blockmap->next_offset;
		use_hint = 0;
	}
again:

	/*
	 * use_hint is turned off if we leave the hinted big-block.
	 */
	if (use_hint && ((next_offset ^ hint) & ~HAMMER_HINTBLOCK_MASK64)) {
		next_offset = blockmap->next_offset;
		use_hint = 0;
	}

	/*
	 * Check for wrap
	 */
	if (next_offset == HAMMER_ZONE_ENCODE(zone + 1, 0)) {
		if (++loops == 2) {
			result_offset = 0;
			*errorp = ENOSPC;
			goto failed;
		}
		next_offset = HAMMER_ZONE_ENCODE(zone, 0);
	}

	/*
	 * The allocation request may not cross a buffer boundary.  Special
	 * large allocations must not cross a big-block boundary.
	 */
	tmp_offset = next_offset + bytes - 1;
	if (bytes <= HAMMER_BUFSIZE) {
		if ((next_offset ^ tmp_offset) & ~HAMMER_BUFMASK64) {
			next_offset = tmp_offset & ~HAMMER_BUFMASK64;
			goto again;
		}
	} else {
		if ((next_offset ^ tmp_offset) & ~HAMMER_BIGBLOCK_MASK64) {
			next_offset = tmp_offset & ~HAMMER_BIGBLOCK_MASK64;
			goto again;
		}
	}
	offset = (int)next_offset & HAMMER_BIGBLOCK_MASK;

	/*
	 * Dive layer 1.
	 */
	layer1_offset = freemap->phys_offset +
			HAMMER_BLOCKMAP_LAYER1_OFFSET(next_offset);

	layer1 = hammer_bread(hmp, layer1_offset, errorp, &buffer1);
	if (*errorp) {
		result_offset = 0;
		goto failed;
	}

	/*
	 * Check CRC.
	 */
	if (layer1->layer1_crc != crc32(layer1, HAMMER_LAYER1_CRCSIZE)) {
		hammer_lock_ex(&hmp->blkmap_lock);
		if (layer1->layer1_crc != crc32(layer1, HAMMER_LAYER1_CRCSIZE))
			panic("CRC FAILED: LAYER1");
		hammer_unlock(&hmp->blkmap_lock);
	}

	/*
	 * If we are at a big-block boundary and layer1 indicates no 
	 * free big-blocks, then we cannot allocate a new bigblock in
	 * layer2, skip to the next layer1 entry.
	 */
	if (offset == 0 && layer1->blocks_free == 0) {
		next_offset = (next_offset + HAMMER_BLOCKMAP_LAYER2) &
			      ~HAMMER_BLOCKMAP_LAYER2_MASK;
		goto again;
	}
	KKASSERT(layer1->phys_offset != HAMMER_BLOCKMAP_UNAVAIL);

	/*
	 * Skip this layer1 entry if it is pointing to a layer2 big-block
	 * on a volume that we are currently trying to remove from the
	 * file-system. This is used by the volume-del code together with
	 * the reblocker to free up a volume.
	 */
	if ((int)HAMMER_VOL_DECODE(layer1->phys_offset) ==
	    hmp->volume_to_remove) {
		next_offset = (next_offset + HAMMER_BLOCKMAP_LAYER2) &
			      ~HAMMER_BLOCKMAP_LAYER2_MASK;
		goto again;
	}

	/*
	 * Dive layer 2, each entry represents a big-block.
	 */
	layer2_offset = layer1->phys_offset +
			HAMMER_BLOCKMAP_LAYER2_OFFSET(next_offset);
	layer2 = hammer_bread(hmp, layer2_offset, errorp, &buffer2);
	if (*errorp) {
		result_offset = 0;
		goto failed;
	}

	/*
	 * Check CRC.  This can race another thread holding the lock
	 * and in the middle of modifying layer2.
	 */
	if (layer2->entry_crc != crc32(layer2, HAMMER_LAYER2_CRCSIZE)) {
		hammer_lock_ex(&hmp->blkmap_lock);
		if (layer2->entry_crc != crc32(layer2, HAMMER_LAYER2_CRCSIZE))
			panic("CRC FAILED: LAYER2");
		hammer_unlock(&hmp->blkmap_lock);
	}

	/*
	 * Skip the layer if the zone is owned by someone other then us.
	 */
	if (layer2->zone && layer2->zone != zone) {
		next_offset += (HAMMER_BIGBLOCK_SIZE - offset);
		goto again;
	}
	if (offset < layer2->append_off) {
		next_offset += layer2->append_off - offset;
		goto again;
	}

#if 0
	/*
	 * If operating in the current non-hint blockmap block, do not
	 * allow it to get over-full.  Also drop any active hinting so
	 * blockmap->next_offset is updated at the end.
	 *
	 * We do this for B-Tree and meta-data allocations to provide
	 * localization for updates.
	 */
	if ((zone == HAMMER_ZONE_BTREE_INDEX ||
	     zone == HAMMER_ZONE_META_INDEX) &&
	    offset >= HAMMER_BIGBLOCK_OVERFILL &&
	    !((next_offset ^ blockmap->next_offset) & ~HAMMER_BIGBLOCK_MASK64)
	) {
		if (offset >= HAMMER_BIGBLOCK_OVERFILL) {
			next_offset += (HAMMER_BIGBLOCK_SIZE - offset);
			use_hint = 0;
			goto again;
		}
	}
#endif

	/*
	 * We need the lock from this point on.  We have to re-check zone
	 * ownership after acquiring the lock and also check for reservations.
	 */
	hammer_lock_ex(&hmp->blkmap_lock);

	if (layer2->zone && layer2->zone != zone) {
		hammer_unlock(&hmp->blkmap_lock);
		next_offset += (HAMMER_BIGBLOCK_SIZE - offset);
		goto again;
	}
	if (offset < layer2->append_off) {
		hammer_unlock(&hmp->blkmap_lock);
		next_offset += layer2->append_off - offset;
		goto again;
	}

	/*
	 * The bigblock might be reserved by another zone.  If it is reserved
	 * by our zone we may have to move next_offset past the append_off.
	 */
	base_off = hammer_xlate_to_zone2(next_offset &
					~HAMMER_BIGBLOCK_MASK64);
	resv = RB_LOOKUP(hammer_res_rb_tree, &hmp->rb_resv_root, base_off);
	if (resv) {
		if (resv->zone != zone) {
			hammer_unlock(&hmp->blkmap_lock);
			next_offset = (next_offset + HAMMER_BIGBLOCK_SIZE) &
				      ~HAMMER_BIGBLOCK_MASK64;
			goto again;
		}
		if (offset < resv->append_off) {
			hammer_unlock(&hmp->blkmap_lock);
			next_offset += resv->append_off - offset;
			goto again;
		}
		++resv->refs;
	}

	/*
	 * Ok, we can allocate out of this layer2 big-block.  Assume ownership
	 * of the layer for real.  At this point we've validated any
	 * reservation that might exist and can just ignore resv.
	 */
	if (layer2->zone == 0) {
		/*
		 * Assign the bigblock to our zone
		 */
		hammer_modify_buffer(trans, buffer1,
				     layer1, sizeof(*layer1));
		--layer1->blocks_free;
		layer1->layer1_crc = crc32(layer1,
					   HAMMER_LAYER1_CRCSIZE);
		hammer_modify_buffer_done(buffer1);
		hammer_modify_buffer(trans, buffer2,
				     layer2, sizeof(*layer2));
		layer2->zone = zone;
		KKASSERT(layer2->bytes_free == HAMMER_BIGBLOCK_SIZE);
		KKASSERT(layer2->append_off == 0);
		hammer_modify_volume_field(trans, trans->rootvol,
					   vol0_stat_freebigblocks);
		--root_volume->ondisk->vol0_stat_freebigblocks;
		hmp->copy_stat_freebigblocks =
			root_volume->ondisk->vol0_stat_freebigblocks;
		hammer_modify_volume_done(trans->rootvol);
	} else {
		hammer_modify_buffer(trans, buffer2,
				     layer2, sizeof(*layer2));
	}
	KKASSERT(layer2->zone == zone);

	/*
	 * NOTE: bytes_free can legally go negative due to de-dup.
	 */
	layer2->bytes_free -= bytes;
	KKASSERT(layer2->append_off <= offset);
	layer2->append_off = offset + bytes;
	layer2->entry_crc = crc32(layer2, HAMMER_LAYER2_CRCSIZE);
	hammer_modify_buffer_done(buffer2);

	/*
	 * We hold the blockmap lock and should be the only ones
	 * capable of modifying resv->append_off.  Track the allocation
	 * as appropriate.
	 */
	KKASSERT(bytes != 0);
	if (resv) {
		KKASSERT(resv->append_off <= offset);
		resv->append_off = offset + bytes;
		resv->flags &= ~HAMMER_RESF_LAYER2FREE;
		hammer_blockmap_reserve_complete(hmp, resv);
	}

	/*
	 * If we are allocating from the base of a new buffer we can avoid
	 * a disk read by calling hammer_bnew_ext().
	 */
	if ((next_offset & HAMMER_BUFMASK) == 0) {
		hammer_bnew_ext(trans->hmp, next_offset, bytes,
				errorp, &buffer3);
	}
	result_offset = next_offset;

	/*
	 * If we weren't supplied with a hint or could not use the hint
	 * then we wound up using blockmap->next_offset as the hint and
	 * need to save it.
	 */
	if (use_hint == 0) {
		hammer_modify_volume(NULL, root_volume, NULL, 0);
		blockmap->next_offset = next_offset + bytes;
		hammer_modify_volume_done(root_volume);
	}
	hammer_unlock(&hmp->blkmap_lock);
failed:

	/*
	 * Cleanup
	 */
	if (buffer1)
		hammer_rel_buffer(buffer1, 0);
	if (buffer2)
		hammer_rel_buffer(buffer2, 0);
	if (buffer3)
		hammer_rel_buffer(buffer3, 0);

	return(result_offset);
}

/*
 * Frontend function - Reserve bytes in a zone.
 *
 * This code reserves bytes out of a blockmap without committing to any
 * meta-data modifications, allowing the front-end to directly issue disk
 * write I/O for big blocks of data
 *
 * The backend later finalizes the reservation with hammer_blockmap_finalize()
 * upon committing the related record.
 */
hammer_reserve_t
hammer_blockmap_reserve(hammer_mount_t hmp, int zone, int bytes,
			hammer_off_t *zone_offp, int *errorp)
{
	hammer_volume_t root_volume;
	hammer_blockmap_t blockmap;
	hammer_blockmap_t freemap;
	struct hammer_blockmap_layer1 *layer1;
	struct hammer_blockmap_layer2 *layer2;
	hammer_buffer_t buffer1 = NULL;
	hammer_buffer_t buffer2 = NULL;
	hammer_buffer_t buffer3 = NULL;
	hammer_off_t tmp_offset;
	hammer_off_t next_offset;
	hammer_off_t layer1_offset;
	hammer_off_t layer2_offset;
	hammer_off_t base_off;
	hammer_reserve_t resv;
	hammer_reserve_t resx;
	int loops = 0;
	int offset;

	/*
	 * Setup
	 */
	KKASSERT(zone >= HAMMER_ZONE_BTREE_INDEX && zone < HAMMER_MAX_ZONES);
	root_volume = hammer_get_root_volume(hmp, errorp);
	if (*errorp)
		return(NULL);
	blockmap = &hmp->blockmap[zone];
	freemap = &hmp->blockmap[HAMMER_ZONE_FREEMAP_INDEX];
	KKASSERT(HAMMER_ZONE_DECODE(blockmap->next_offset) == zone);

	/*
	 * Deal with alignment and buffer-boundary issues.
	 *
	 * Be careful, certain primary alignments are used below to allocate
	 * new blockmap blocks.
	 */
	bytes = (bytes + 15) & ~15;
	KKASSERT(bytes > 0 && bytes <= HAMMER_XBUFSIZE);

	next_offset = blockmap->next_offset;
again:
	resv = NULL;
	/*
	 * Check for wrap
	 */
	if (next_offset == HAMMER_ZONE_ENCODE(zone + 1, 0)) {
		if (++loops == 2) {
			*errorp = ENOSPC;
			goto failed;
		}
		next_offset = HAMMER_ZONE_ENCODE(zone, 0);
	}

	/*
	 * The allocation request may not cross a buffer boundary.  Special
	 * large allocations must not cross a big-block boundary.
	 */
	tmp_offset = next_offset + bytes - 1;
	if (bytes <= HAMMER_BUFSIZE) {
		if ((next_offset ^ tmp_offset) & ~HAMMER_BUFMASK64) {
			next_offset = tmp_offset & ~HAMMER_BUFMASK64;
			goto again;
		}
	} else {
		if ((next_offset ^ tmp_offset) & ~HAMMER_BIGBLOCK_MASK64) {
			next_offset = tmp_offset & ~HAMMER_BIGBLOCK_MASK64;
			goto again;
		}
	}
	offset = (int)next_offset & HAMMER_BIGBLOCK_MASK;

	/*
	 * Dive layer 1.
	 */
	layer1_offset = freemap->phys_offset +
			HAMMER_BLOCKMAP_LAYER1_OFFSET(next_offset);
	layer1 = hammer_bread(hmp, layer1_offset, errorp, &buffer1);
	if (*errorp)
		goto failed;

	/*
	 * Check CRC.
	 */
	if (layer1->layer1_crc != crc32(layer1, HAMMER_LAYER1_CRCSIZE)) {
		hammer_lock_ex(&hmp->blkmap_lock);
		if (layer1->layer1_crc != crc32(layer1, HAMMER_LAYER1_CRCSIZE))
			panic("CRC FAILED: LAYER1");
		hammer_unlock(&hmp->blkmap_lock);
	}

	/*
	 * If we are at a big-block boundary and layer1 indicates no 
	 * free big-blocks, then we cannot allocate a new bigblock in
	 * layer2, skip to the next layer1 entry.
	 */
	if ((next_offset & HAMMER_BIGBLOCK_MASK) == 0 &&
	    layer1->blocks_free == 0) {
		next_offset = (next_offset + HAMMER_BLOCKMAP_LAYER2) &
			      ~HAMMER_BLOCKMAP_LAYER2_MASK;
		goto again;
	}
	KKASSERT(layer1->phys_offset != HAMMER_BLOCKMAP_UNAVAIL);

	/*
	 * Dive layer 2, each entry represents a big-block.
	 */
	layer2_offset = layer1->phys_offset +
			HAMMER_BLOCKMAP_LAYER2_OFFSET(next_offset);
	layer2 = hammer_bread(hmp, layer2_offset, errorp, &buffer2);
	if (*errorp)
		goto failed;

	/*
	 * Check CRC if not allocating into uninitialized space (which we
	 * aren't when reserving space).
	 */
	if (layer2->entry_crc != crc32(layer2, HAMMER_LAYER2_CRCSIZE)) {
		hammer_lock_ex(&hmp->blkmap_lock);
		if (layer2->entry_crc != crc32(layer2, HAMMER_LAYER2_CRCSIZE))
			panic("CRC FAILED: LAYER2");
		hammer_unlock(&hmp->blkmap_lock);
	}

	/*
	 * Skip the layer if the zone is owned by someone other then us.
	 */
	if (layer2->zone && layer2->zone != zone) {
		next_offset += (HAMMER_BIGBLOCK_SIZE - offset);
		goto again;
	}
	if (offset < layer2->append_off) {
		next_offset += layer2->append_off - offset;
		goto again;
	}

	/*
	 * We need the lock from this point on.  We have to re-check zone
	 * ownership after acquiring the lock and also check for reservations.
	 */
	hammer_lock_ex(&hmp->blkmap_lock);

	if (layer2->zone && layer2->zone != zone) {
		hammer_unlock(&hmp->blkmap_lock);
		next_offset += (HAMMER_BIGBLOCK_SIZE - offset);
		goto again;
	}
	if (offset < layer2->append_off) {
		hammer_unlock(&hmp->blkmap_lock);
		next_offset += layer2->append_off - offset;
		goto again;
	}

	/*
	 * The bigblock might be reserved by another zone.  If it is reserved
	 * by our zone we may have to move next_offset past the append_off.
	 */
	base_off = hammer_xlate_to_zone2(next_offset &
					~HAMMER_BIGBLOCK_MASK64);
	resv = RB_LOOKUP(hammer_res_rb_tree, &hmp->rb_resv_root, base_off);
	if (resv) {
		if (resv->zone != zone) {
			hammer_unlock(&hmp->blkmap_lock);
			next_offset = (next_offset + HAMMER_BIGBLOCK_SIZE) &
				      ~HAMMER_BIGBLOCK_MASK64;
			goto again;
		}
		if (offset < resv->append_off) {
			hammer_unlock(&hmp->blkmap_lock);
			next_offset += resv->append_off - offset;
			goto again;
		}
		++resv->refs;
		resx = NULL;
	} else {
		resx = kmalloc(sizeof(*resv), hmp->m_misc,
			       M_WAITOK | M_ZERO | M_USE_RESERVE);
		resx->refs = 1;
		resx->zone = zone;
		resx->zone_offset = base_off;
		if (layer2->bytes_free == HAMMER_BIGBLOCK_SIZE)
			resx->flags |= HAMMER_RESF_LAYER2FREE;
		resv = RB_INSERT(hammer_res_rb_tree, &hmp->rb_resv_root, resx);
		KKASSERT(resv == NULL);
		resv = resx;
		++hammer_count_reservations;
	}
	resv->append_off = offset + bytes;

	/*
	 * If we are not reserving a whole buffer but are at the start of
	 * a new block, call hammer_bnew() to avoid a disk read.
	 *
	 * If we are reserving a whole buffer (or more), the caller will
	 * probably use a direct read, so do nothing.
	 *
	 * If we do not have a whole lot of system memory we really can't
	 * afford to block while holding the blkmap_lock!
	 */
	if (bytes < HAMMER_BUFSIZE && (next_offset & HAMMER_BUFMASK) == 0) {
		if (!vm_page_count_min(HAMMER_BUFSIZE / PAGE_SIZE))
			hammer_bnew(hmp, next_offset, errorp, &buffer3);
	}

	/*
	 * Adjust our iterator and alloc_offset.  The layer1 and layer2
	 * space beyond alloc_offset is uninitialized.  alloc_offset must
	 * be big-block aligned.
	 */
	blockmap->next_offset = next_offset + bytes;
	hammer_unlock(&hmp->blkmap_lock);

failed:
	if (buffer1)
		hammer_rel_buffer(buffer1, 0);
	if (buffer2)
		hammer_rel_buffer(buffer2, 0);
	if (buffer3)
		hammer_rel_buffer(buffer3, 0);
	hammer_rel_volume(root_volume, 0);
	*zone_offp = next_offset;

	return(resv);
}

/*
 * Frontend function - Dedup bytes in a zone.
 *
 * Dedup reservations work exactly the same as normal write reservations
 * except we only adjust bytes_free field and don't touch append offset.
 * Finalization mechanic for dedup reservations is also the same as for
 * normal write ones - the backend finalizes the reservation with
 * hammer_blockmap_finalize().
 */
hammer_reserve_t
hammer_blockmap_reserve_dedup(hammer_mount_t hmp, int zone, int bytes,
			      hammer_off_t zone_offset, int *errorp)
{
	hammer_volume_t root_volume;
	hammer_blockmap_t freemap;
	struct hammer_blockmap_layer1 *layer1;
	struct hammer_blockmap_layer2 *layer2;
	hammer_buffer_t buffer1 = NULL;
	hammer_buffer_t buffer2 = NULL;
	hammer_off_t layer1_offset;
	hammer_off_t layer2_offset;
	hammer_off_t base_off;
	hammer_reserve_t resv = NULL;
	hammer_reserve_t resx = NULL;

	/*
	 * Setup
	 */
	KKASSERT(zone >= HAMMER_ZONE_BTREE_INDEX && zone < HAMMER_MAX_ZONES);
	root_volume = hammer_get_root_volume(hmp, errorp);
	if (*errorp)
		return (NULL);
	freemap = &hmp->blockmap[HAMMER_ZONE_FREEMAP_INDEX];
	KKASSERT(freemap->phys_offset != 0);

	bytes = (bytes + 15) & ~15;
	KKASSERT(bytes > 0 && bytes <= HAMMER_XBUFSIZE);

	/*
	 * Dive layer 1.
	 */
	layer1_offset = freemap->phys_offset +
			HAMMER_BLOCKMAP_LAYER1_OFFSET(zone_offset);
	layer1 = hammer_bread(hmp, layer1_offset, errorp, &buffer1);
	if (*errorp)
		goto failed;

	/*
	 * Check CRC.
	 */
	if (layer1->layer1_crc != crc32(layer1, HAMMER_LAYER1_CRCSIZE)) {
		hammer_lock_ex(&hmp->blkmap_lock);
		if (layer1->layer1_crc != crc32(layer1, HAMMER_LAYER1_CRCSIZE))
			panic("CRC FAILED: LAYER1");
		hammer_unlock(&hmp->blkmap_lock);
	}
	KKASSERT(layer1->phys_offset != HAMMER_BLOCKMAP_UNAVAIL);

	/*
	 * Dive layer 2, each entry represents a big-block.
	 */
	layer2_offset = layer1->phys_offset +
			HAMMER_BLOCKMAP_LAYER2_OFFSET(zone_offset);
	layer2 = hammer_bread(hmp, layer2_offset, errorp, &buffer2);
	if (*errorp)
		goto failed;

	/*
	 * Check CRC.
	 */
	if (layer2->entry_crc != crc32(layer2, HAMMER_LAYER2_CRCSIZE)) {
		hammer_lock_ex(&hmp->blkmap_lock);
		if (layer2->entry_crc != crc32(layer2, HAMMER_LAYER2_CRCSIZE))
			panic("CRC FAILED: LAYER2");
		hammer_unlock(&hmp->blkmap_lock);
	}

	/*
	 * Fail if the zone is owned by someone other than us.
	 */
	if (layer2->zone && layer2->zone != zone)
		goto failed;

	/*
	 * We need the lock from this point on.  We have to re-check zone
	 * ownership after acquiring the lock and also check for reservations.
	 */
	hammer_lock_ex(&hmp->blkmap_lock);

	if (layer2->zone && layer2->zone != zone) {
		hammer_unlock(&hmp->blkmap_lock);
		goto failed;
	}

	base_off = hammer_xlate_to_zone2(zone_offset &
					~HAMMER_BIGBLOCK_MASK64);
	resv = RB_LOOKUP(hammer_res_rb_tree, &hmp->rb_resv_root, base_off);
	if (resv) {
		if (resv->zone != zone) {
			hammer_unlock(&hmp->blkmap_lock);
			resv = NULL;
			goto failed;
		}
		/*
		 * Due to possible big block underflow we can't simply
		 * subtract bytes from bytes_free.
		 */
		if (update_bytes_free(resv, bytes) == 0) {
			hammer_unlock(&hmp->blkmap_lock);
			resv = NULL;
			goto failed;
		}
		++resv->refs;
		resx = NULL;
	} else {
		resx = kmalloc(sizeof(*resv), hmp->m_misc,
			       M_WAITOK | M_ZERO | M_USE_RESERVE);
		resx->refs = 1;
		resx->zone = zone;
		resx->bytes_free = layer2->bytes_free;
		/*
		 * Due to possible big block underflow we can't simply
		 * subtract bytes from bytes_free.
		 */
		if (update_bytes_free(resx, bytes) == 0) {
			hammer_unlock(&hmp->blkmap_lock);
			kfree(resx, hmp->m_misc);
			goto failed;
		}
		resx->zone_offset = base_off;
		resv = RB_INSERT(hammer_res_rb_tree, &hmp->rb_resv_root, resx);
		KKASSERT(resv == NULL);
		resv = resx;
		++hammer_count_reservations;
	}

	hammer_unlock(&hmp->blkmap_lock);

failed:
	if (buffer1)
		hammer_rel_buffer(buffer1, 0);
	if (buffer2)
		hammer_rel_buffer(buffer2, 0);
	hammer_rel_volume(root_volume, 0);

	return(resv);
}

static int
update_bytes_free(hammer_reserve_t resv, int bytes)
{
	int32_t temp;

	/*
	 * Big-block underflow check
	 */
	temp = resv->bytes_free - HAMMER_BIGBLOCK_SIZE * 2;
	cpu_ccfence(); /* XXX do we really need it ? */
	if (temp > resv->bytes_free) {
		kprintf("BIGBLOCK UNDERFLOW\n");
		return (0);
	}

	resv->bytes_free -= bytes;
	return (1);
}

/*
 * Dereference a reservation structure.  Upon the final release the
 * underlying big-block is checked and if it is entirely free we delete
 * any related HAMMER buffers to avoid potential conflicts with future
 * reuse of the big-block.
 */
void
hammer_blockmap_reserve_complete(hammer_mount_t hmp, hammer_reserve_t resv)
{
	hammer_off_t base_offset;
	int error;

	KKASSERT(resv->refs > 0);
	KKASSERT((resv->zone_offset & HAMMER_OFF_ZONE_MASK) ==
		 HAMMER_ZONE_RAW_BUFFER);

	/*
	 * Setting append_off to the max prevents any new allocations
	 * from occuring while we are trying to dispose of the reservation,
	 * allowing us to safely delete any related HAMMER buffers.
	 *
	 * If we are unable to clean out all related HAMMER buffers we
	 * requeue the delay.
	 */
	if (resv->refs == 1 && (resv->flags & HAMMER_RESF_LAYER2FREE)) {
		resv->append_off = HAMMER_BIGBLOCK_SIZE;
		base_offset = resv->zone_offset & ~HAMMER_OFF_ZONE_MASK;
		base_offset = HAMMER_ZONE_ENCODE(resv->zone, base_offset);
		if (!TAILQ_EMPTY(&hmp->dedup_lru_list))
			hammer_dedup_cache_inval(hmp, base_offset);
		error = hammer_del_buffers(hmp, base_offset,
					   resv->zone_offset,
					   HAMMER_BIGBLOCK_SIZE,
					   1);
		if (hammer_debug_general & 0x20000) {
			kprintf("hammer: delbgblk %016jx error %d\n",
				(intmax_t)base_offset, error);
		}
		if (error)
			hammer_reserve_setdelay(hmp, resv);
	}
	if (--resv->refs == 0) {
		if (hammer_debug_general & 0x20000) {
			kprintf("hammer: delresvr %016jx zone %02x\n",
				(intmax_t)resv->zone_offset, resv->zone);
		}
		KKASSERT((resv->flags & HAMMER_RESF_ONDELAY) == 0);
		RB_REMOVE(hammer_res_rb_tree, &hmp->rb_resv_root, resv);
		kfree(resv, hmp->m_misc);
		--hammer_count_reservations;
	}
}

/*
 * Prevent a potentially free big-block from being reused until after
 * the related flushes have completely cycled, otherwise crash recovery
 * could resurrect a data block that was already reused and overwritten.
 *
 * The caller might reset the underlying layer2 entry's append_off to 0, so
 * our covering append_off must be set to max to prevent any reallocation
 * until after the flush delays complete, not to mention proper invalidation
 * of any underlying cached blocks.
 */
static void
hammer_reserve_setdelay_offset(hammer_mount_t hmp, hammer_off_t base_offset,
			int zone, struct hammer_blockmap_layer2 *layer2)
{
	hammer_reserve_t resv;

	/*
	 * Allocate the reservation if necessary.
	 *
	 * NOTE: need lock in future around resv lookup/allocation and
	 * the setdelay call, currently refs is not bumped until the call.
	 */
again:
	resv = RB_LOOKUP(hammer_res_rb_tree, &hmp->rb_resv_root, base_offset);
	if (resv == NULL) {
		resv = kmalloc(sizeof(*resv), hmp->m_misc,
			       M_WAITOK | M_ZERO | M_USE_RESERVE);
		resv->zone = zone;
		resv->zone_offset = base_offset;
		resv->refs = 0;
		resv->append_off = HAMMER_BIGBLOCK_SIZE;

		if (layer2->bytes_free == HAMMER_BIGBLOCK_SIZE)
			resv->flags |= HAMMER_RESF_LAYER2FREE;
		if (RB_INSERT(hammer_res_rb_tree, &hmp->rb_resv_root, resv)) {
			kfree(resv, hmp->m_misc);
			goto again;
		}
		++hammer_count_reservations;
	} else {
		if (layer2->bytes_free == HAMMER_BIGBLOCK_SIZE)
			resv->flags |= HAMMER_RESF_LAYER2FREE;
	}
	hammer_reserve_setdelay(hmp, resv);
}

/*
 * Enter the reservation on the on-delay list, or move it if it
 * is already on the list.
 */
static void
hammer_reserve_setdelay(hammer_mount_t hmp, hammer_reserve_t resv)
{
	if (resv->flags & HAMMER_RESF_ONDELAY) {
		TAILQ_REMOVE(&hmp->delay_list, resv, delay_entry);
		resv->flush_group = hmp->flusher.next + 1;
		TAILQ_INSERT_TAIL(&hmp->delay_list, resv, delay_entry);
	} else {
		++resv->refs;
		++hmp->rsv_fromdelay;
		resv->flags |= HAMMER_RESF_ONDELAY;
		resv->flush_group = hmp->flusher.next + 1;
		TAILQ_INSERT_TAIL(&hmp->delay_list, resv, delay_entry);
	}
}

/*
 * Reserve has reached its flush point, remove it from the delay list
 * and finish it off.  hammer_blockmap_reserve_complete() inherits
 * the ondelay reference.
 */
void
hammer_reserve_clrdelay(hammer_mount_t hmp, hammer_reserve_t resv)
{
	KKASSERT(resv->flags & HAMMER_RESF_ONDELAY);
	resv->flags &= ~HAMMER_RESF_ONDELAY;
	TAILQ_REMOVE(&hmp->delay_list, resv, delay_entry);
	--hmp->rsv_fromdelay;
	hammer_blockmap_reserve_complete(hmp, resv);
}

/*
 * Backend function - free (offset, bytes) in a zone.
 *
 * XXX error return
 */
void
hammer_blockmap_free(hammer_transaction_t trans,
		     hammer_off_t zone_offset, int bytes)
{
	hammer_mount_t hmp;
	hammer_volume_t root_volume;
	hammer_blockmap_t freemap;
	struct hammer_blockmap_layer1 *layer1;
	struct hammer_blockmap_layer2 *layer2;
	hammer_buffer_t buffer1 = NULL;
	hammer_buffer_t buffer2 = NULL;
	hammer_off_t layer1_offset;
	hammer_off_t layer2_offset;
	hammer_off_t base_off;
	int error;
	int zone;

	if (bytes == 0)
		return;
	hmp = trans->hmp;

	/*
	 * Alignment
	 */
	bytes = (bytes + 15) & ~15;
	KKASSERT(bytes <= HAMMER_XBUFSIZE);
	KKASSERT(((zone_offset ^ (zone_offset + (bytes - 1))) & 
		  ~HAMMER_BIGBLOCK_MASK64) == 0);

	/*
	 * Basic zone validation & locking
	 */
	zone = HAMMER_ZONE_DECODE(zone_offset);
	KKASSERT(zone >= HAMMER_ZONE_BTREE_INDEX && zone < HAMMER_MAX_ZONES);
	root_volume = trans->rootvol;
	error = 0;

	freemap = &hmp->blockmap[HAMMER_ZONE_FREEMAP_INDEX];

	/*
	 * Dive layer 1.
	 */
	layer1_offset = freemap->phys_offset +
			HAMMER_BLOCKMAP_LAYER1_OFFSET(zone_offset);
	layer1 = hammer_bread(hmp, layer1_offset, &error, &buffer1);
	if (error)
		goto failed;
	KKASSERT(layer1->phys_offset &&
		 layer1->phys_offset != HAMMER_BLOCKMAP_UNAVAIL);
	if (layer1->layer1_crc != crc32(layer1, HAMMER_LAYER1_CRCSIZE)) {
		hammer_lock_ex(&hmp->blkmap_lock);
		if (layer1->layer1_crc != crc32(layer1, HAMMER_LAYER1_CRCSIZE))
			panic("CRC FAILED: LAYER1");
		hammer_unlock(&hmp->blkmap_lock);
	}

	/*
	 * Dive layer 2, each entry represents a big-block.
	 */
	layer2_offset = layer1->phys_offset +
			HAMMER_BLOCKMAP_LAYER2_OFFSET(zone_offset);
	layer2 = hammer_bread(hmp, layer2_offset, &error, &buffer2);
	if (error)
		goto failed;
	if (layer2->entry_crc != crc32(layer2, HAMMER_LAYER2_CRCSIZE)) {
		hammer_lock_ex(&hmp->blkmap_lock);
		if (layer2->entry_crc != crc32(layer2, HAMMER_LAYER2_CRCSIZE))
			panic("CRC FAILED: LAYER2");
		hammer_unlock(&hmp->blkmap_lock);
	}

	hammer_lock_ex(&hmp->blkmap_lock);

	hammer_modify_buffer(trans, buffer2, layer2, sizeof(*layer2));

	/*
	 * Free space previously allocated via blockmap_alloc().
	 *
	 * NOTE: bytes_free can be and remain negative due to de-dup ops
	 *	 but can never become larger than HAMMER_BIGBLOCK_SIZE.
	 */
	KKASSERT(layer2->zone == zone);
	layer2->bytes_free += bytes;
	KKASSERT(layer2->bytes_free <= HAMMER_BIGBLOCK_SIZE);

	/*
	 * If a big-block becomes entirely free we must create a covering
	 * reservation to prevent premature reuse.  Note, however, that
	 * the big-block and/or reservation may still have an append_off
	 * that allows further (non-reused) allocations.
	 *
	 * Once the reservation has been made we re-check layer2 and if
	 * the big-block is still entirely free we reset the layer2 entry.
	 * The reservation will prevent premature reuse.
	 *
	 * NOTE: hammer_buffer's are only invalidated when the reservation
	 * is completed, if the layer2 entry is still completely free at
	 * that time.  Any allocations from the reservation that may have
	 * occured in the mean time, or active references on the reservation
	 * from new pending allocations, will prevent the invalidation from
	 * occuring.
	 */
	if (layer2->bytes_free == HAMMER_BIGBLOCK_SIZE) {
		base_off = hammer_xlate_to_zone2(zone_offset &
						~HAMMER_BIGBLOCK_MASK64);

		hammer_reserve_setdelay_offset(hmp, base_off, zone, layer2);
		if (layer2->bytes_free == HAMMER_BIGBLOCK_SIZE) {
			layer2->zone = 0;
			layer2->append_off = 0;
			hammer_modify_buffer(trans, buffer1,
					     layer1, sizeof(*layer1));
			++layer1->blocks_free;
			layer1->layer1_crc = crc32(layer1,
						   HAMMER_LAYER1_CRCSIZE);
			hammer_modify_buffer_done(buffer1);
			hammer_modify_volume_field(trans,
					trans->rootvol,
					vol0_stat_freebigblocks);
			++root_volume->ondisk->vol0_stat_freebigblocks;
			hmp->copy_stat_freebigblocks =
			   root_volume->ondisk->vol0_stat_freebigblocks;
			hammer_modify_volume_done(trans->rootvol);
		}
	}
	layer2->entry_crc = crc32(layer2, HAMMER_LAYER2_CRCSIZE);
	hammer_modify_buffer_done(buffer2);
	hammer_unlock(&hmp->blkmap_lock);

failed:
	if (buffer1)
		hammer_rel_buffer(buffer1, 0);
	if (buffer2)
		hammer_rel_buffer(buffer2, 0);
}

int
hammer_blockmap_dedup(hammer_transaction_t trans,
		     hammer_off_t zone_offset, int bytes)
{
	hammer_mount_t hmp;
	hammer_blockmap_t freemap;
	struct hammer_blockmap_layer1 *layer1;
	struct hammer_blockmap_layer2 *layer2;
	hammer_buffer_t buffer1 = NULL;
	hammer_buffer_t buffer2 = NULL;
	hammer_off_t layer1_offset;
	hammer_off_t layer2_offset;
	int32_t temp;
	int error;
	int zone __debugvar;

	if (bytes == 0)
		return (0);
	hmp = trans->hmp;

	/*
	 * Alignment
	 */
	bytes = (bytes + 15) & ~15;
	KKASSERT(bytes <= HAMMER_BIGBLOCK_SIZE);
	KKASSERT(((zone_offset ^ (zone_offset + (bytes - 1))) &
		  ~HAMMER_BIGBLOCK_MASK64) == 0);

	/*
	 * Basic zone validation & locking
	 */
	zone = HAMMER_ZONE_DECODE(zone_offset);
	KKASSERT(zone >= HAMMER_ZONE_BTREE_INDEX && zone < HAMMER_MAX_ZONES);
	error = 0;

	freemap = &hmp->blockmap[HAMMER_ZONE_FREEMAP_INDEX];

	/*
	 * Dive layer 1.
	 */
	layer1_offset = freemap->phys_offset +
			HAMMER_BLOCKMAP_LAYER1_OFFSET(zone_offset);
	layer1 = hammer_bread(hmp, layer1_offset, &error, &buffer1);
	if (error)
		goto failed;
	KKASSERT(layer1->phys_offset &&
		 layer1->phys_offset != HAMMER_BLOCKMAP_UNAVAIL);
	if (layer1->layer1_crc != crc32(layer1, HAMMER_LAYER1_CRCSIZE)) {
		hammer_lock_ex(&hmp->blkmap_lock);
		if (layer1->layer1_crc != crc32(layer1, HAMMER_LAYER1_CRCSIZE))
			panic("CRC FAILED: LAYER1");
		hammer_unlock(&hmp->blkmap_lock);
	}

	/*
	 * Dive layer 2, each entry represents a big-block.
	 */
	layer2_offset = layer1->phys_offset +
			HAMMER_BLOCKMAP_LAYER2_OFFSET(zone_offset);
	layer2 = hammer_bread(hmp, layer2_offset, &error, &buffer2);
	if (error)
		goto failed;
	if (layer2->entry_crc != crc32(layer2, HAMMER_LAYER2_CRCSIZE)) {
		hammer_lock_ex(&hmp->blkmap_lock);
		if (layer2->entry_crc != crc32(layer2, HAMMER_LAYER2_CRCSIZE))
			panic("CRC FAILED: LAYER2");
		hammer_unlock(&hmp->blkmap_lock);
	}

	hammer_lock_ex(&hmp->blkmap_lock);

	hammer_modify_buffer(trans, buffer2, layer2, sizeof(*layer2));

	/*
	 * Free space previously allocated via blockmap_alloc().
	 *
	 * NOTE: bytes_free can be and remain negative due to de-dup ops
	 *	 but can never become larger than HAMMER_BIGBLOCK_SIZE.
	 */
	KKASSERT(layer2->zone == zone);
	temp = layer2->bytes_free - HAMMER_BIGBLOCK_SIZE * 2;
	cpu_ccfence(); /* prevent gcc from optimizing temp out */
	if (temp > layer2->bytes_free) {
		error = ERANGE;
		goto underflow;
	}
	layer2->bytes_free -= bytes;

	KKASSERT(layer2->bytes_free <= HAMMER_BIGBLOCK_SIZE);

	layer2->entry_crc = crc32(layer2, HAMMER_LAYER2_CRCSIZE);
underflow:
	hammer_modify_buffer_done(buffer2);
	hammer_unlock(&hmp->blkmap_lock);

failed:
	if (buffer1)
		hammer_rel_buffer(buffer1, 0);
	if (buffer2)
		hammer_rel_buffer(buffer2, 0);
	return (error);
}

/*
 * Backend function - finalize (offset, bytes) in a zone.
 *
 * Allocate space that was previously reserved by the frontend.
 */
int
hammer_blockmap_finalize(hammer_transaction_t trans,
			 hammer_reserve_t resv,
			 hammer_off_t zone_offset, int bytes)
{
	hammer_mount_t hmp;
	hammer_volume_t root_volume;
	hammer_blockmap_t freemap;
	struct hammer_blockmap_layer1 *layer1;
	struct hammer_blockmap_layer2 *layer2;
	hammer_buffer_t buffer1 = NULL;
	hammer_buffer_t buffer2 = NULL;
	hammer_off_t layer1_offset;
	hammer_off_t layer2_offset;
	int error;
	int zone;
	int offset;

	if (bytes == 0)
		return(0);
	hmp = trans->hmp;

	/*
	 * Alignment
	 */
	bytes = (bytes + 15) & ~15;
	KKASSERT(bytes <= HAMMER_XBUFSIZE);

	/*
	 * Basic zone validation & locking
	 */
	zone = HAMMER_ZONE_DECODE(zone_offset);
	KKASSERT(zone >= HAMMER_ZONE_BTREE_INDEX && zone < HAMMER_MAX_ZONES);
	root_volume = trans->rootvol;
	error = 0;

	freemap = &hmp->blockmap[HAMMER_ZONE_FREEMAP_INDEX];

	/*
	 * Dive layer 1.
	 */
	layer1_offset = freemap->phys_offset +
			HAMMER_BLOCKMAP_LAYER1_OFFSET(zone_offset);
	layer1 = hammer_bread(hmp, layer1_offset, &error, &buffer1);
	if (error)
		goto failed;
	KKASSERT(layer1->phys_offset &&
		 layer1->phys_offset != HAMMER_BLOCKMAP_UNAVAIL);
	if (layer1->layer1_crc != crc32(layer1, HAMMER_LAYER1_CRCSIZE)) {
		hammer_lock_ex(&hmp->blkmap_lock);
		if (layer1->layer1_crc != crc32(layer1, HAMMER_LAYER1_CRCSIZE))
			panic("CRC FAILED: LAYER1");
		hammer_unlock(&hmp->blkmap_lock);
	}

	/*
	 * Dive layer 2, each entry represents a big-block.
	 */
	layer2_offset = layer1->phys_offset +
			HAMMER_BLOCKMAP_LAYER2_OFFSET(zone_offset);
	layer2 = hammer_bread(hmp, layer2_offset, &error, &buffer2);
	if (error)
		goto failed;
	if (layer2->entry_crc != crc32(layer2, HAMMER_LAYER2_CRCSIZE)) {
		hammer_lock_ex(&hmp->blkmap_lock);
		if (layer2->entry_crc != crc32(layer2, HAMMER_LAYER2_CRCSIZE))
			panic("CRC FAILED: LAYER2");
		hammer_unlock(&hmp->blkmap_lock);
	}

	hammer_lock_ex(&hmp->blkmap_lock);

	hammer_modify_buffer(trans, buffer2, layer2, sizeof(*layer2));

	/*
	 * Finalize some or all of the space covered by a current
	 * reservation.  An allocation in the same layer may have
	 * already assigned ownership.
	 */
	if (layer2->zone == 0) {
		hammer_modify_buffer(trans, buffer1,
				     layer1, sizeof(*layer1));
		--layer1->blocks_free;
		layer1->layer1_crc = crc32(layer1,
					   HAMMER_LAYER1_CRCSIZE);
		hammer_modify_buffer_done(buffer1);
		layer2->zone = zone;
		KKASSERT(layer2->bytes_free == HAMMER_BIGBLOCK_SIZE);
		KKASSERT(layer2->append_off == 0);
		hammer_modify_volume_field(trans,
				trans->rootvol,
				vol0_stat_freebigblocks);
		--root_volume->ondisk->vol0_stat_freebigblocks;
		hmp->copy_stat_freebigblocks =
		   root_volume->ondisk->vol0_stat_freebigblocks;
		hammer_modify_volume_done(trans->rootvol);
	}
	if (layer2->zone != zone)
		kprintf("layer2 zone mismatch %d %d\n", layer2->zone, zone);
	KKASSERT(layer2->zone == zone);
	KKASSERT(bytes != 0);
	layer2->bytes_free -= bytes;

	if (resv) {
		resv->flags &= ~HAMMER_RESF_LAYER2FREE;
	}

	/*
	 * Finalizations can occur out of order, or combined with allocations.
	 * append_off must be set to the highest allocated offset.
	 */
	offset = ((int)zone_offset & HAMMER_BIGBLOCK_MASK) + bytes;
	if (layer2->append_off < offset)
		layer2->append_off = offset;

	layer2->entry_crc = crc32(layer2, HAMMER_LAYER2_CRCSIZE);
	hammer_modify_buffer_done(buffer2);
	hammer_unlock(&hmp->blkmap_lock);

failed:
	if (buffer1)
		hammer_rel_buffer(buffer1, 0);
	if (buffer2)
		hammer_rel_buffer(buffer2, 0);
	return(error);
}

/*
 * Return the approximate number of free bytes in the big-block
 * containing the specified blockmap offset.
 *
 * WARNING: A negative number can be returned if data de-dup exists,
 *	    and the result will also not represent he actual number
 *	    of free bytes in this case.
 *
 *	    This code is used only by the reblocker.
 */
int
hammer_blockmap_getfree(hammer_mount_t hmp, hammer_off_t zone_offset,
			int *curp, int *errorp)
{
	hammer_volume_t root_volume;
	hammer_blockmap_t blockmap;
	hammer_blockmap_t freemap;
	struct hammer_blockmap_layer1 *layer1;
	struct hammer_blockmap_layer2 *layer2;
	hammer_buffer_t buffer = NULL;
	hammer_off_t layer1_offset;
	hammer_off_t layer2_offset;
	int32_t bytes;
	int zone;

	zone = HAMMER_ZONE_DECODE(zone_offset);
	KKASSERT(zone >= HAMMER_ZONE_BTREE_INDEX && zone < HAMMER_MAX_ZONES);
	root_volume = hammer_get_root_volume(hmp, errorp);
	if (*errorp) {
		*curp = 0;
		return(0);
	}
	blockmap = &hmp->blockmap[zone];
	freemap = &hmp->blockmap[HAMMER_ZONE_FREEMAP_INDEX];

	/*
	 * Dive layer 1.
	 */
	layer1_offset = freemap->phys_offset +
			HAMMER_BLOCKMAP_LAYER1_OFFSET(zone_offset);
	layer1 = hammer_bread(hmp, layer1_offset, errorp, &buffer);
	if (*errorp) {
		bytes = 0;
		goto failed;
	}
	KKASSERT(layer1->phys_offset);
	if (layer1->layer1_crc != crc32(layer1, HAMMER_LAYER1_CRCSIZE)) {
		hammer_lock_ex(&hmp->blkmap_lock);
		if (layer1->layer1_crc != crc32(layer1, HAMMER_LAYER1_CRCSIZE))
			panic("CRC FAILED: LAYER1");
		hammer_unlock(&hmp->blkmap_lock);
	}

	/*
	 * Dive layer 2, each entry represents a big-block.
	 *
	 * (reuse buffer, layer1 pointer becomes invalid)
	 */
	layer2_offset = layer1->phys_offset +
			HAMMER_BLOCKMAP_LAYER2_OFFSET(zone_offset);
	layer2 = hammer_bread(hmp, layer2_offset, errorp, &buffer);
	if (*errorp) {
		bytes = 0;
		goto failed;
	}
	if (layer2->entry_crc != crc32(layer2, HAMMER_LAYER2_CRCSIZE)) {
		hammer_lock_ex(&hmp->blkmap_lock);
		if (layer2->entry_crc != crc32(layer2, HAMMER_LAYER2_CRCSIZE))
			panic("CRC FAILED: LAYER2");
		hammer_unlock(&hmp->blkmap_lock);
	}
	KKASSERT(layer2->zone == zone);

	bytes = layer2->bytes_free;

	if ((blockmap->next_offset ^ zone_offset) & ~HAMMER_BIGBLOCK_MASK64)
		*curp = 0;
	else
		*curp = 1;
failed:
	if (buffer)
		hammer_rel_buffer(buffer, 0);
	hammer_rel_volume(root_volume, 0);
	if (hammer_debug_general & 0x0800) {
		kprintf("hammer_blockmap_getfree: %016llx -> %d\n",
			(long long)zone_offset, bytes);
	}
	return(bytes);
}


/*
 * Lookup a blockmap offset and verify blockmap layers.
 */
hammer_off_t
hammer_blockmap_lookup_verify(hammer_mount_t hmp, hammer_off_t zone_offset,
			int *errorp)
{
	hammer_volume_t root_volume;
	hammer_blockmap_t freemap;
	struct hammer_blockmap_layer1 *layer1;
	struct hammer_blockmap_layer2 *layer2;
	hammer_buffer_t buffer = NULL;
	hammer_off_t layer1_offset;
	hammer_off_t layer2_offset;
	hammer_off_t result_offset;
	hammer_off_t base_off;
	hammer_reserve_t resv __debugvar;
	int zone;

	/*
	 * Calculate the zone-2 offset.
	 */
	zone = HAMMER_ZONE_DECODE(zone_offset);
	result_offset = hammer_xlate_to_zone2(zone_offset);

	/*
	 * Validate the allocation zone
	 */
	root_volume = hammer_get_root_volume(hmp, errorp);
	if (*errorp)
		return(0);
	freemap = &hmp->blockmap[HAMMER_ZONE_FREEMAP_INDEX];
	KKASSERT(freemap->phys_offset != 0);

	/*
	 * Dive layer 1.
	 */
	layer1_offset = freemap->phys_offset +
			HAMMER_BLOCKMAP_LAYER1_OFFSET(zone_offset);
	layer1 = hammer_bread(hmp, layer1_offset, errorp, &buffer);
	if (*errorp)
		goto failed;
	KKASSERT(layer1->phys_offset != HAMMER_BLOCKMAP_UNAVAIL);
	if (layer1->layer1_crc != crc32(layer1, HAMMER_LAYER1_CRCSIZE)) {
		hammer_lock_ex(&hmp->blkmap_lock);
		if (layer1->layer1_crc != crc32(layer1, HAMMER_LAYER1_CRCSIZE))
			panic("CRC FAILED: LAYER1");
		hammer_unlock(&hmp->blkmap_lock);
	}

	/*
	 * Dive layer 2, each entry represents a big-block.
	 */
	layer2_offset = layer1->phys_offset +
			HAMMER_BLOCKMAP_LAYER2_OFFSET(zone_offset);
	layer2 = hammer_bread(hmp, layer2_offset, errorp, &buffer);

	if (*errorp)
		goto failed;
	if (layer2->zone == 0) {
		base_off = hammer_xlate_to_zone2(zone_offset &
						~HAMMER_BIGBLOCK_MASK64);
		resv = RB_LOOKUP(hammer_res_rb_tree, &hmp->rb_resv_root,
				 base_off);
		KKASSERT(resv && resv->zone == zone);

	} else if (layer2->zone != zone) {
		panic("hammer_blockmap_lookup_verify: bad zone %d/%d",
			layer2->zone, zone);
	}
	if (layer2->entry_crc != crc32(layer2, HAMMER_LAYER2_CRCSIZE)) {
		hammer_lock_ex(&hmp->blkmap_lock);
		if (layer2->entry_crc != crc32(layer2, HAMMER_LAYER2_CRCSIZE))
			panic("CRC FAILED: LAYER2");
		hammer_unlock(&hmp->blkmap_lock);
	}

failed:
	if (buffer)
		hammer_rel_buffer(buffer, 0);
	hammer_rel_volume(root_volume, 0);
	if (hammer_debug_general & 0x0800) {
		kprintf("hammer_blockmap_lookup_verify: %016llx -> %016llx\n",
			(long long)zone_offset, (long long)result_offset);
	}
	return(result_offset);
}


/*
 * Check space availability
 *
 * MPSAFE - does not require fs_token
 */
int
_hammer_checkspace(hammer_mount_t hmp, int slop, int64_t *resp)
{
	const int in_size = sizeof(struct hammer_inode_data) +
			    sizeof(union hammer_btree_elm);
	const int rec_size = (sizeof(union hammer_btree_elm) * 2);
	int64_t usedbytes;

	usedbytes = hmp->rsv_inodes * in_size +
		    hmp->rsv_recs * rec_size +
		    hmp->rsv_databytes +
		    ((int64_t)hmp->rsv_fromdelay << HAMMER_BIGBLOCK_BITS) +
		    ((int64_t)hammer_limit_dirtybufspace) +
		    (slop << HAMMER_BIGBLOCK_BITS);

	hammer_count_extra_space_used = usedbytes;	/* debugging */
	if (resp)
		*resp = usedbytes;

	if (hmp->copy_stat_freebigblocks >=
	    (usedbytes >> HAMMER_BIGBLOCK_BITS)) {
		return(0);
	}
	return (ENOSPC);
}

