/*
 * Copyright (c) 2009 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Matthew Dillon <dillon@backplane.com> and
 * Michael Neumann <mneumann@ntecs.de>
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
 */

#include "hammer.h"
#include <sys/fcntl.h>
#include <sys/nlookup.h>
#include <sys/buf.h>

static int
hammer_setup_device(struct vnode **devvpp, const char *dev_path, int ronly);

static void
hammer_close_device(struct vnode **devvpp, int ronly);

static int
hammer_format_volume_header(struct hammer_mount *hmp, struct vnode *devvp,
	const char *vol_name, int vol_no, int vol_count,
	int64_t vol_size, int64_t boot_area_size, int64_t mem_area_size);

static uint64_t
hammer_format_freemap(struct hammer_mount *hmp,
	hammer_transaction_t trans,
	hammer_volume_t volume);

static uint64_t
hammer_format_layer2_chunk(struct hammer_mount *hmp,
	hammer_transaction_t trans,
	hammer_off_t phys_offset,
	hammer_off_t aligned_buf_end_off,
	hammer_buffer_t *bufferp,
	int *errorp);

static void
hammer_set_layer1_entry(struct hammer_mount *hmp,
	hammer_transaction_t trans,
	hammer_off_t phys_offset,
	uint64_t free_bigblocks,
	hammer_blockmap_t freemap,
	hammer_buffer_t *bufferp,
	int *errorp);

int
hammer_ioc_expand(hammer_transaction_t trans, hammer_inode_t ip,
		struct hammer_ioc_expand *expand)
{
	struct hammer_mount *hmp = trans->hmp;
	struct mount *mp = hmp->mp;
	hammer_volume_t volume;
	hammer_volume_t root_volume;
	int error;

	if (mp->mnt_flag & MNT_RDONLY) {
		kprintf("Cannot expand read-only HAMMER filesystem\n");
		return (EINVAL);
	}

	if (hmp->nvolumes + 1 >= HAMMER_MAX_VOLUMES) {
		kprintf("Max number of HAMMER volumes exceeded\n");
		return (EINVAL);
	}

	/*
	 * Find an unused volume number.
	 */
	int free_vol_no = 0;
	while (free_vol_no < HAMMER_MAX_VOLUMES &&
	       RB_LOOKUP(hammer_vol_rb_tree, &hmp->rb_vols_root, free_vol_no)) {
		++free_vol_no;
	}
	if (free_vol_no >= HAMMER_MAX_VOLUMES) {
		kprintf("Max number of HAMMER volumes exceeded\n");
		return (EINVAL);
	}

	struct vnode *devvp = NULL;
	error = hammer_setup_device(&devvp, expand->device_name, 0);
	if (error)
		goto end;
	KKASSERT(devvp);
	error = hammer_format_volume_header(
		hmp,
		devvp,
		hmp->rootvol->ondisk->vol_name,
		free_vol_no,
		hmp->nvolumes+1,
		expand->vol_size,
		expand->boot_area_size,
		expand->mem_area_size);
	hammer_close_device(&devvp, 0);
	if (error)
		goto end;

	error = hammer_install_volume(hmp, expand->device_name, NULL);
	if (error)
		goto end;

	hammer_sync_lock_sh(trans);
	hammer_lock_ex(&hmp->blkmap_lock);

	++hmp->nvolumes;

	/*
	 * Set each volumes new value of the vol_count field.
	 */
	for (int vol_no = 0; vol_no < HAMMER_MAX_VOLUMES; ++vol_no) {
		if (vol_no == free_vol_no)
			continue;

		volume = hammer_get_volume(hmp, vol_no, &error);
		if (volume == NULL && error == ENOENT) {
			/*
			 * Skip unused volume numbers
			 */
			error = 0;
			continue;
		}
		KKASSERT(volume != NULL && error == 0);
		hammer_modify_volume_field(trans, volume, vol_count);
		volume->ondisk->vol_count = hmp->nvolumes;
		hammer_modify_volume_done(volume);
		hammer_rel_volume(volume, 0);
	}

	volume = hammer_get_volume(hmp, free_vol_no, &error);
	KKASSERT(volume != NULL && error == 0);
	root_volume = hammer_get_root_volume(hmp, &error);
	KKASSERT(root_volume != NULL && error == 0);

	uint64_t total_free_bigblocks =
		hammer_format_freemap(hmp, trans, volume);

	/*
	 * Increase the total number of bigblocks
	 */
	hammer_modify_volume_field(trans, root_volume,
		vol0_stat_bigblocks);
	root_volume->ondisk->vol0_stat_bigblocks += total_free_bigblocks;
	hammer_modify_volume_done(root_volume);

	/*
	 * Increase the number of free bigblocks
	 * (including the copy in hmp)
	 */
	hammer_modify_volume_field(trans, root_volume,
		vol0_stat_freebigblocks);
	root_volume->ondisk->vol0_stat_freebigblocks += total_free_bigblocks;
	hmp->copy_stat_freebigblocks =
		root_volume->ondisk->vol0_stat_freebigblocks;
	hammer_modify_volume_done(root_volume);

	hammer_rel_volume(root_volume, 0);
	hammer_rel_volume(volume, 0);

	hammer_unlock(&hmp->blkmap_lock);
	hammer_sync_unlock(trans);

end:
	if (error)
		kprintf("An error occurred: %d\n", error);
	return (error);
}

static uint64_t
hammer_format_freemap(struct hammer_mount *hmp,
	hammer_transaction_t trans,
	hammer_volume_t volume)
{
	hammer_off_t phys_offset;
	hammer_buffer_t buffer = NULL;
	hammer_blockmap_t freemap;
	hammer_off_t aligned_buf_end_off;
	uint64_t free_bigblocks;
	uint64_t total_free_bigblocks;
	int error = 0;

	total_free_bigblocks = 0;

	/*
	 * Calculate the usable size of the new volume, which
	 * must be aligned at a bigblock (8 MB) boundary.
	 */
	aligned_buf_end_off = HAMMER_ENCODE_RAW_BUFFER(volume->ondisk->vol_no,
		(volume->ondisk->vol_buf_end - volume->ondisk->vol_buf_beg)
		& ~HAMMER_LARGEBLOCK_MASK64);

	freemap = &hmp->blockmap[HAMMER_ZONE_FREEMAP_INDEX];

	/*
	 * Iterate the volume's address space in chunks of 4 TB,
	 * where each chunk consists of at least one physically
	 * available 8 MB bigblock.
	 *
	 * For each chunk we need one L1 entry and one L2 bigblock.
	 * We use the first bigblock of each chunk as L2 block.
	 */
	for (phys_offset = HAMMER_ENCODE_RAW_BUFFER(volume->ondisk->vol_no, 0);
	     phys_offset < aligned_buf_end_off;
	     phys_offset += HAMMER_BLOCKMAP_LAYER2) {

		free_bigblocks = hammer_format_layer2_chunk(hmp, trans,
			phys_offset, aligned_buf_end_off, &buffer, &error);
		KKASSERT(error == 0);

		hammer_set_layer1_entry(hmp, trans, phys_offset,
			free_bigblocks, freemap, &buffer, &error);
		KKASSERT(error == 0);

		total_free_bigblocks += free_bigblocks;
	}

	if (buffer) {
		hammer_rel_buffer(buffer, 0);
		buffer = NULL;
	}

	return total_free_bigblocks;
}

/*
 * Format the L2 bigblock representing a 4 TB chunk.
 *
 * Returns the number of free bigblocks.
 */
static uint64_t
hammer_format_layer2_chunk(struct hammer_mount *hmp,
	hammer_transaction_t trans,
	hammer_off_t phys_offset,
	hammer_off_t aligned_buf_end_off,
	hammer_buffer_t *bufferp,
	int *errorp)
{
	uint64_t free_bigblocks = 0;
	hammer_off_t block_off;
	hammer_off_t layer2_offset;
	struct hammer_blockmap_layer2 *layer2;

	for (block_off = 0;
	     block_off < HAMMER_BLOCKMAP_LAYER2;
	     block_off += HAMMER_LARGEBLOCK_SIZE) {
		layer2_offset = phys_offset +
				HAMMER_BLOCKMAP_LAYER2_OFFSET(block_off);
		layer2 = hammer_bread(hmp, layer2_offset, errorp, bufferp);
		if (*errorp)
			return free_bigblocks;

		KKASSERT(layer2);

		hammer_modify_buffer(trans, *bufferp, layer2, sizeof(*layer2));
		bzero(layer2, sizeof(*layer2));

		if (block_off == 0) {
			/*
			 * The first entry represents the L2 bigblock itself.
			 */
			layer2->zone = HAMMER_ZONE_FREEMAP_INDEX;
			layer2->append_off = HAMMER_LARGEBLOCK_SIZE;
			layer2->bytes_free = 0;
		} else if (phys_offset + block_off < aligned_buf_end_off) {
			layer2->zone = 0;
			layer2->append_off = 0;
			layer2->bytes_free = HAMMER_LARGEBLOCK_SIZE;
			++free_bigblocks;
		} else {
			/*
			 * Bigblock outside of physically available space
			 */
			layer2->zone = HAMMER_ZONE_UNAVAIL_INDEX;
			layer2->append_off = HAMMER_LARGEBLOCK_SIZE;
			layer2->bytes_free = 0;
		}
		layer2->entry_crc = crc32(layer2, HAMMER_LAYER2_CRCSIZE);

		hammer_modify_buffer_done(*bufferp);
	}

	return free_bigblocks;
}

static void
hammer_set_layer1_entry(struct hammer_mount *hmp,
	hammer_transaction_t trans,
	hammer_off_t phys_offset,
	uint64_t free_bigblocks,
	hammer_blockmap_t freemap,
	hammer_buffer_t *bufferp,
	int *errorp)
{
	struct hammer_blockmap_layer1 *layer1;
	hammer_off_t layer1_offset;

	layer1_offset =	freemap->phys_offset +
			HAMMER_BLOCKMAP_LAYER1_OFFSET(phys_offset);
	layer1 = hammer_bread(hmp, layer1_offset, errorp, bufferp);
	if (*errorp)
		return;
	KKASSERT(layer1);
	KKASSERT(layer1->phys_offset == HAMMER_BLOCKMAP_UNAVAIL);

	hammer_modify_buffer(trans, *bufferp, layer1, sizeof(*layer1));
	bzero(layer1, sizeof(*layer1));
	layer1->phys_offset = phys_offset;
	layer1->blocks_free = free_bigblocks;
	layer1->layer1_crc = crc32(layer1, HAMMER_LAYER1_CRCSIZE);

	hammer_modify_buffer_done(*bufferp);
}

static int
hammer_setup_device(struct vnode **devvpp, const char *dev_path, int ronly)
{
	int error;
	struct nlookupdata nd;

	/*
	 * Get the device vnode
	 */
	if (*devvpp == NULL) {
		error = nlookup_init(&nd, dev_path, UIO_SYSSPACE, NLC_FOLLOW);
		if (error == 0)
			error = nlookup(&nd);
		if (error == 0)
			error = cache_vref(&nd.nl_nch, nd.nl_cred, devvpp);
		nlookup_done(&nd);
	} else {
		error = 0;
	}

	if (error == 0) {
		if (vn_isdisk(*devvpp, &error)) {
			error = vfs_mountedon(*devvpp);
		}
	}
	if (error == 0 && vcount(*devvpp) > 0)
		error = EBUSY;
	if (error == 0) {
		vn_lock(*devvpp, LK_EXCLUSIVE | LK_RETRY);
		error = vinvalbuf(*devvpp, V_SAVE, 0, 0);
		if (error == 0) {
			error = VOP_OPEN(*devvpp,
					 (ronly ? FREAD : FREAD|FWRITE),
					 FSCRED, NULL);
		}
		vn_unlock(*devvpp);
	}
	if (error && *devvpp) {
		vrele(*devvpp);
		*devvpp = NULL;
	}
	return (error);
}

static void
hammer_close_device(struct vnode **devvpp, int ronly)
{
	VOP_CLOSE(*devvpp, (ronly ? FREAD : FREAD|FWRITE));
	if (*devvpp) {
		vinvalbuf(*devvpp, ronly ? 0 : V_SAVE, 0, 0);
		vrele(*devvpp);
		*devvpp = NULL;
	}
}

static int
hammer_format_volume_header(struct hammer_mount *hmp, struct vnode *devvp,
	const char *vol_name, int vol_no, int vol_count,
	int64_t vol_size, int64_t boot_area_size, int64_t mem_area_size)
{
	struct buf *bp = NULL;
	struct hammer_volume_ondisk *ondisk;
	int error;

	/*
	 * Extract the volume number from the volume header and do various
	 * sanity checks.
	 */
	KKASSERT(HAMMER_BUFSIZE >= sizeof(struct hammer_volume_ondisk));
	error = bread(devvp, 0LL, HAMMER_BUFSIZE, &bp);
	if (error || bp->b_bcount < sizeof(struct hammer_volume_ondisk))
		goto late_failure;

	ondisk = (struct hammer_volume_ondisk*) bp->b_data;

	/*
	 * Note that we do NOT allow to use a device that contains
	 * a valid HAMMER signature. It has to be cleaned up with dd
	 * before.
	 */
	if (ondisk->vol_signature == HAMMER_FSBUF_VOLUME) {
		kprintf("hammer_expand: Formatting of valid HAMMER volume "
			"%s denied. Erase with dd!\n", vol_name);
		error = EFTYPE;
		goto late_failure;
	}

	bzero(ondisk, sizeof(struct hammer_volume_ondisk));
	ksnprintf(ondisk->vol_name, sizeof(ondisk->vol_name), "%s", vol_name);
	ondisk->vol_fstype = hmp->rootvol->ondisk->vol_fstype;
	ondisk->vol_signature = HAMMER_FSBUF_VOLUME;
	ondisk->vol_fsid = hmp->fsid;
	ondisk->vol_rootvol = hmp->rootvol->vol_no;
	ondisk->vol_no = vol_no;
	ondisk->vol_count = vol_count;
	ondisk->vol_version = hmp->version;

	/*
	 * Reserve space for (future) header junk, setup our poor-man's
	 * bigblock allocator.
	 */
	int64_t vol_alloc = HAMMER_BUFSIZE * 16;

	ondisk->vol_bot_beg = vol_alloc;
	vol_alloc += boot_area_size;
	ondisk->vol_mem_beg = vol_alloc;
	vol_alloc += mem_area_size;

	/*
	 * The remaining area is the zone 2 buffer allocation area.  These
	 * buffers
	 */
	ondisk->vol_buf_beg = vol_alloc;
	ondisk->vol_buf_end = vol_size & ~(int64_t)HAMMER_BUFMASK;

	if (ondisk->vol_buf_end < ondisk->vol_buf_beg) {
		kprintf("volume %d %s is too small to hold the volume header",
		     ondisk->vol_no, ondisk->vol_name);
		error = EFTYPE;
		goto late_failure;
	}

	ondisk->vol_nblocks = (ondisk->vol_buf_end - ondisk->vol_buf_beg) /
			      HAMMER_BUFSIZE;
	ondisk->vol_blocksize = HAMMER_BUFSIZE;

	/*
	 * Write volume header to disk
	 */
	error = bwrite(bp);
	bp = NULL;

late_failure:
	if (bp)
		brelse(bp);
	return (error);
}
