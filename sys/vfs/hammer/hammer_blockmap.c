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
 * $DragonFly: src/sys/vfs/hammer/hammer_blockmap.c,v 1.27 2008/07/31 22:30:33 dillon Exp $
 */

/*
 * HAMMER blockmap
 */
#include "hammer.h"

static int hammer_res_rb_compare(hammer_reserve_t res1, hammer_reserve_t res2);
static int hammer_reserve_setdelay(hammer_mount_t hmp, hammer_reserve_t resv,
                        hammer_off_t zone2_offset);


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
hammer_blockmap_alloc(hammer_transaction_t trans, int zone,
		      int bytes, int *errorp)
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

	next_offset = blockmap->next_offset;
again:
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
	 * large allocations must not cross a large-block boundary.
	 */
	tmp_offset = next_offset + bytes - 1;
	if (bytes <= HAMMER_BUFSIZE) {
		if ((next_offset ^ tmp_offset) & ~HAMMER_BUFMASK64) {
			next_offset = tmp_offset & ~HAMMER_BUFMASK64;
			goto again;
		}
	} else {
		if ((next_offset ^ tmp_offset) & ~HAMMER_LARGEBLOCK_MASK64) {
			next_offset = tmp_offset & ~HAMMER_LARGEBLOCK_MASK64;
			goto again;
		}
	}
	offset = (int)next_offset & HAMMER_LARGEBLOCK_MASK;

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
		Debugger("CRC FAILED: LAYER1");
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
	 * Dive layer 2, each entry represents a large-block.
	 */
	layer2_offset = layer1->phys_offset +
			HAMMER_BLOCKMAP_LAYER2_OFFSET(next_offset);
	layer2 = hammer_bread(hmp, layer2_offset, errorp, &buffer2);
	if (*errorp) {
		result_offset = 0;
		goto failed;
	}

	/*
	 * Check CRC.
	 */
	if (layer2->entry_crc != crc32(layer2, HAMMER_LAYER2_CRCSIZE)) {
		Debugger("CRC FAILED: LAYER2");
	}

	/*
	 * Skip the layer if the zone is owned by someone other then us.
	 */
	if (layer2->zone && layer2->zone != zone) {
		next_offset += (HAMMER_LARGEBLOCK_SIZE - offset);
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
		next_offset += (HAMMER_LARGEBLOCK_SIZE - offset);
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
	base_off = (next_offset &
		    (~HAMMER_LARGEBLOCK_MASK64 & ~HAMMER_OFF_ZONE_MASK)) | 
		    HAMMER_ZONE_RAW_BUFFER;
	resv = RB_LOOKUP(hammer_res_rb_tree, &hmp->rb_resv_root, base_off);
	if (resv) {
		if (resv->zone != zone) {
			hammer_unlock(&hmp->blkmap_lock);
			next_offset = (next_offset + HAMMER_LARGEBLOCK_SIZE) &
				      ~HAMMER_LARGEBLOCK_MASK64;
			goto again;
		}
		if (offset < resv->append_off) {
			hammer_unlock(&hmp->blkmap_lock);
			next_offset += resv->append_off - offset;
			goto again;
		}
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
		KKASSERT(layer2->bytes_free == HAMMER_LARGEBLOCK_SIZE);
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

	layer2->bytes_free -= bytes;
	KKASSERT(layer2->append_off <= offset);
	layer2->append_off = offset + bytes;
	layer2->entry_crc = crc32(layer2, HAMMER_LAYER2_CRCSIZE);
	hammer_modify_buffer_done(buffer2);
	KKASSERT(layer2->bytes_free >= 0);

	if (resv) {
		KKASSERT(resv->append_off <= offset);
		resv->append_off = offset + bytes;
	}

	/*
	 * If we are allocating from the base of a new buffer we can avoid
	 * a disk read by calling hammer_bnew().
	 */
	if ((next_offset & HAMMER_BUFMASK) == 0) {
		hammer_bnew_ext(trans->hmp, next_offset, bytes,
				errorp, &buffer3);
	}
	result_offset = next_offset;

	/*
	 * Process allocated result_offset
	 */
	hammer_modify_volume(NULL, root_volume, NULL, 0);
	blockmap->next_offset = next_offset + bytes;
	hammer_modify_volume_done(root_volume);
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
 * write I/O for large blocks of data
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
	 * large allocations must not cross a large-block boundary.
	 */
	tmp_offset = next_offset + bytes - 1;
	if (bytes <= HAMMER_BUFSIZE) {
		if ((next_offset ^ tmp_offset) & ~HAMMER_BUFMASK64) {
			next_offset = tmp_offset & ~HAMMER_BUFMASK64;
			goto again;
		}
	} else {
		if ((next_offset ^ tmp_offset) & ~HAMMER_LARGEBLOCK_MASK64) {
			next_offset = tmp_offset & ~HAMMER_LARGEBLOCK_MASK64;
			goto again;
		}
	}
	offset = (int)next_offset & HAMMER_LARGEBLOCK_MASK;

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
		Debugger("CRC FAILED: LAYER1");
	}

	/*
	 * If we are at a big-block boundary and layer1 indicates no 
	 * free big-blocks, then we cannot allocate a new bigblock in
	 * layer2, skip to the next layer1 entry.
	 */
	if ((next_offset & HAMMER_LARGEBLOCK_MASK) == 0 &&
	    layer1->blocks_free == 0) {
		next_offset = (next_offset + HAMMER_BLOCKMAP_LAYER2) &
			      ~HAMMER_BLOCKMAP_LAYER2_MASK;
		goto again;
	}
	KKASSERT(layer1->phys_offset != HAMMER_BLOCKMAP_UNAVAIL);

	/*
	 * Dive layer 2, each entry represents a large-block.
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
		Debugger("CRC FAILED: LAYER2");
	}

	/*
	 * Skip the layer if the zone is owned by someone other then us.
	 */
	if (layer2->zone && layer2->zone != zone) {
		next_offset += (HAMMER_LARGEBLOCK_SIZE - offset);
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
		next_offset += (HAMMER_LARGEBLOCK_SIZE - offset);
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
	base_off = (next_offset &
		    (~HAMMER_LARGEBLOCK_MASK64 & ~HAMMER_OFF_ZONE_MASK)) |
		    HAMMER_ZONE_RAW_BUFFER;
	resv = RB_LOOKUP(hammer_res_rb_tree, &hmp->rb_resv_root, base_off);
	if (resv) {
		if (resv->zone != zone) {
			hammer_unlock(&hmp->blkmap_lock);
			next_offset = (next_offset + HAMMER_LARGEBLOCK_SIZE) &
				      ~HAMMER_LARGEBLOCK_MASK64;
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
		resx = kmalloc(sizeof(*resv), M_HAMMER,
			       M_WAITOK | M_ZERO | M_USE_RESERVE);
		resx->refs = 1;
		resx->zone = zone;
		resx->zone_offset = base_off;
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
	 */
	if (bytes < HAMMER_BUFSIZE && (next_offset & HAMMER_BUFMASK) == 0) {
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
 * Backend function - undo a portion of a reservation.
 */
void
hammer_blockmap_reserve_undo(hammer_reserve_t resv,
			 hammer_off_t zone_offset, int bytes)
{
	resv->bytes_freed += bytes;
}


/*
 * A record with a storage reservation calls this function when it is
 * being freed.  The storage may or may not have actually been allocated.
 *
 * This function removes the lock that prevented other entities from
 * allocating out of the storage or removing the zone assignment.
 */
void
hammer_blockmap_reserve_complete(hammer_mount_t hmp, hammer_reserve_t resv)
{
	hammer_off_t zone2_offset;

	KKASSERT(resv->refs > 0);
	if (--resv->refs == 0) {
		KKASSERT((resv->flags & HAMMER_RESF_ONDELAY) == 0);

		zone2_offset = (resv->zone_offset & ~HAMMER_OFF_ZONE_MASK) |
				HAMMER_ZONE_RAW_BUFFER;

		/*
		 * If we are releasing a zone and all of its reservations
		 * were undone we have to clean out all hammer and device
		 * buffers associated with the big block.  We do this
		 * primarily because the large-block may be reallocated
		 * from non-large-data to large-data or vise-versa, resulting
		 * in a different mix of 16K and 64K buffer cache buffers.
		 * XXX - this isn't fun and needs to be redone.
		 *
		 * Any direct allocations will cause this test to fail
		 * (bytes_freed will never reach append_off), which is
		 * the behavior we desire.  Once the zone has been assigned
		 * to the big-block the only way to allocate from it in the
		 * future is if the reblocker can completely clean it out,
		 * and that will also properly call hammer_del_buffers().
		 *
		 * If we don't we risk all sorts of buffer cache aliasing
		 * effects, including overlapping buffers with different
		 * sizes.
		 */
		if (resv->bytes_freed == resv->append_off) {
			hammer_del_buffers(hmp, resv->zone_offset,
					   zone2_offset,
					   HAMMER_LARGEBLOCK_SIZE);
		}
		RB_REMOVE(hammer_res_rb_tree, &hmp->rb_resv_root, resv);
		kfree(resv, M_HAMMER);
		--hammer_count_reservations;
	}
}

/*
 * This ensures that no data reallocations will take place at the specified
 * zone2_offset (pointing to the base of a bigblock) for 2 flush cycles,
 * preventing deleted data space, which has no UNDO, from being reallocated 
 * too quickly.
 */
static int
hammer_reserve_setdelay(hammer_mount_t hmp, hammer_reserve_t resv,
			hammer_off_t zone2_offset)
{
	int error;

	if (resv == NULL) {
		resv = kmalloc(sizeof(*resv), M_HAMMER,
			       M_WAITOK | M_ZERO | M_USE_RESERVE);
		resv->refs = 1;	/* ref for on-delay list */
		resv->zone_offset = zone2_offset;
		resv->append_off = HAMMER_LARGEBLOCK_SIZE;
		if (RB_INSERT(hammer_res_rb_tree, &hmp->rb_resv_root, resv)) {
			error = EAGAIN;
			kfree(resv, M_HAMMER);
		} else {
			error = 0;
			++hammer_count_reservations;
		}
	} else if (resv->flags & HAMMER_RESF_ONDELAY) {
		--hmp->rsv_fromdelay;
		resv->flags &= ~HAMMER_RESF_ONDELAY;
		TAILQ_REMOVE(&hmp->delay_list, resv, delay_entry);
		resv->flush_group = hmp->flusher.next + 1;
		error = 0;
	} else {
		++resv->refs;	/* ref for on-delay list */
		error = 0;
	}
	if (error == 0) {
		++hmp->rsv_fromdelay;
		resv->flags |= HAMMER_RESF_ONDELAY;
		resv->flush_group = hmp->flusher.next + 1;
		TAILQ_INSERT_TAIL(&hmp->delay_list, resv, delay_entry);
	}
	return(error);
}

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
	hammer_reserve_t resv;
	hammer_blockmap_t blockmap;
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
		  ~HAMMER_LARGEBLOCK_MASK64) == 0);

	/*
	 * Basic zone validation & locking
	 */
	zone = HAMMER_ZONE_DECODE(zone_offset);
	KKASSERT(zone >= HAMMER_ZONE_BTREE_INDEX && zone < HAMMER_MAX_ZONES);
	root_volume = trans->rootvol;
	error = 0;

	blockmap = &hmp->blockmap[zone];
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
		Debugger("CRC FAILED: LAYER1");
	}

	/*
	 * Dive layer 2, each entry represents a large-block.
	 */
	layer2_offset = layer1->phys_offset +
			HAMMER_BLOCKMAP_LAYER2_OFFSET(zone_offset);
	layer2 = hammer_bread(hmp, layer2_offset, &error, &buffer2);
	if (error)
		goto failed;
	if (layer2->entry_crc != crc32(layer2, HAMMER_LAYER2_CRCSIZE)) {
		Debugger("CRC FAILED: LAYER2");
	}

	hammer_lock_ex(&hmp->blkmap_lock);

	hammer_modify_buffer(trans, buffer2, layer2, sizeof(*layer2));

	/*
	 * Freeing previously allocated space
	 */
	KKASSERT(layer2->zone == zone);
	layer2->bytes_free += bytes;
	KKASSERT(layer2->bytes_free <= HAMMER_LARGEBLOCK_SIZE);
	if (layer2->bytes_free == HAMMER_LARGEBLOCK_SIZE) {
		base_off = (zone_offset & (~HAMMER_LARGEBLOCK_MASK64 & ~HAMMER_OFF_ZONE_MASK)) | HAMMER_ZONE_RAW_BUFFER;
again:
		resv = RB_LOOKUP(hammer_res_rb_tree, &hmp->rb_resv_root,
				 base_off);
		if (resv) {
			/*
			 * Portions of this block have been reserved, do
			 * not free it.
			 *
			 * Make sure the reservation remains through
			 * the next flush cycle so potentially undoable
			 * data is not overwritten.
			 */
			KKASSERT(resv->zone == zone);
			hammer_reserve_setdelay(hmp, resv, base_off);
		} else if ((blockmap->next_offset ^ zone_offset) &
			    ~HAMMER_LARGEBLOCK_MASK64) {
			/*
			 * Our iterator is not in the now-free big-block
			 * and we can release it.
			 *
			 * Make sure the reservation remains through
			 * the next flush cycle so potentially undoable
			 * data is not overwritten.
			 */
			if (hammer_reserve_setdelay(hmp, NULL, base_off))
				goto again;
			KKASSERT(layer2->zone == zone);
			/*
			 * XXX maybe incorporate this del call in the
			 * release code by setting base_offset, bytes_freed,
			 * etc.
			 */
			hammer_del_buffers(hmp,
					   zone_offset &
					      ~HAMMER_LARGEBLOCK_MASK64,
					   base_off,
					   HAMMER_LARGEBLOCK_SIZE);
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

/*
 * Backend function - finalize (offset, bytes) in a zone.
 *
 * Allocate space that was previously reserved by the frontend.
 */
int
hammer_blockmap_finalize(hammer_transaction_t trans,
			 hammer_off_t zone_offset, int bytes)
{
	hammer_mount_t hmp;
	hammer_volume_t root_volume;
	hammer_blockmap_t blockmap;
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

	blockmap = &hmp->blockmap[zone];
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
		Debugger("CRC FAILED: LAYER1");
	}

	/*
	 * Dive layer 2, each entry represents a large-block.
	 */
	layer2_offset = layer1->phys_offset +
			HAMMER_BLOCKMAP_LAYER2_OFFSET(zone_offset);
	layer2 = hammer_bread(hmp, layer2_offset, &error, &buffer2);
	if (error)
		goto failed;
	if (layer2->entry_crc != crc32(layer2, HAMMER_LAYER2_CRCSIZE)) {
		Debugger("CRC FAILED: LAYER2");
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
		KKASSERT(layer2->bytes_free == HAMMER_LARGEBLOCK_SIZE);
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
	layer2->bytes_free -= bytes;

	/*
	 * Finalizations can occur out of order, or combined with allocations.
	 * append_off must be set to the highest allocated offset.
	 */
	offset = ((int)zone_offset & HAMMER_LARGEBLOCK_MASK) + bytes;
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
 * Return the number of free bytes in the big-block containing the
 * specified blockmap offset.
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
	int bytes;
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
		Debugger("CRC FAILED: LAYER1");
	}

	/*
	 * Dive layer 2, each entry represents a large-block.
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
		Debugger("CRC FAILED: LAYER2");
	}
	KKASSERT(layer2->zone == zone);

	bytes = layer2->bytes_free;

	if ((blockmap->next_offset ^ zone_offset) & ~HAMMER_LARGEBLOCK_MASK64)
		*curp = 0;
	else
		*curp = 1;
failed:
	if (buffer)
		hammer_rel_buffer(buffer, 0);
	hammer_rel_volume(root_volume, 0);
	if (hammer_debug_general & 0x0800) {
		kprintf("hammer_blockmap_getfree: %016llx -> %d\n",
			zone_offset, bytes);
	}
	return(bytes);
}


/*
 * Lookup a blockmap offset.
 */
hammer_off_t
hammer_blockmap_lookup(hammer_mount_t hmp, hammer_off_t zone_offset,
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
	hammer_reserve_t resv;
	int zone;

	/*
	 * Calculate the zone-2 offset.
	 */
	zone = HAMMER_ZONE_DECODE(zone_offset);
	KKASSERT(zone >= HAMMER_ZONE_BTREE_INDEX && zone < HAMMER_MAX_ZONES);

	result_offset = (zone_offset & ~HAMMER_OFF_ZONE_MASK) |
			HAMMER_ZONE_RAW_BUFFER;

	/*
	 * We can actually stop here, normal blockmaps are now direct-mapped
	 * onto the freemap and so represent zone-2 addresses.
	 */
	if (hammer_verify_zone == 0) {
		*errorp = 0;
		return(result_offset);
	}

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
		Debugger("CRC FAILED: LAYER1");
	}

	/*
	 * Dive layer 2, each entry represents a large-block.
	 */
	layer2_offset = layer1->phys_offset +
			HAMMER_BLOCKMAP_LAYER2_OFFSET(zone_offset);
	layer2 = hammer_bread(hmp, layer2_offset, errorp, &buffer);

	if (*errorp)
		goto failed;
	if (layer2->zone == 0) {
		base_off = (zone_offset & (~HAMMER_LARGEBLOCK_MASK64 & ~HAMMER_OFF_ZONE_MASK)) | HAMMER_ZONE_RAW_BUFFER;
		resv = RB_LOOKUP(hammer_res_rb_tree, &hmp->rb_resv_root,
				 base_off);
		KKASSERT(resv && resv->zone == zone);

	} else if (layer2->zone != zone) {
		panic("hammer_blockmap_lookup: bad zone %d/%d\n",
			layer2->zone, zone);
	}
	if (layer2->entry_crc != crc32(layer2, HAMMER_LAYER2_CRCSIZE)) {
		Debugger("CRC FAILED: LAYER2");
	}

failed:
	if (buffer)
		hammer_rel_buffer(buffer, 0);
	hammer_rel_volume(root_volume, 0);
	if (hammer_debug_general & 0x0800) {
		kprintf("hammer_blockmap_lookup: %016llx -> %016llx\n",
			zone_offset, result_offset);
	}
	return(result_offset);
}


/*
 * Check space availability
 */
int
hammer_checkspace(hammer_mount_t hmp, int slop)
{
	const int in_size = sizeof(struct hammer_inode_data) +
			    sizeof(union hammer_btree_elm);
	const int rec_size = (sizeof(union hammer_btree_elm) * 2);
	int64_t usedbytes;

	usedbytes = hmp->rsv_inodes * in_size +
		    hmp->rsv_recs * rec_size +
		    hmp->rsv_databytes +
		    ((int64_t)hmp->rsv_fromdelay << HAMMER_LARGEBLOCK_BITS) +
		    ((int64_t)hidirtybufspace << 2) +
		    (slop << HAMMER_LARGEBLOCK_BITS);

	hammer_count_extra_space_used = usedbytes;	/* debugging */

	if (hmp->copy_stat_freebigblocks >=
	    (usedbytes >> HAMMER_LARGEBLOCK_BITS)) {
		return(0);
	}
	return (ENOSPC);
}

