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

#include <sys/fcntl.h>
#include <sys/nlookup.h>

#include "hammer.h"

static int
hammer_format_volume_header(struct hammer_mount *hmp,
	struct hammer_volume_ondisk *ondisk,
	const char *vol_name, int vol_no, int vol_count,
	int64_t vol_size, int64_t boot_area_size, int64_t mem_area_size);

static int
hammer_do_reblock(hammer_transaction_t trans, hammer_inode_t ip);

struct bigblock_stat {
	int64_t total_bigblocks;
	int64_t total_free_bigblocks;
	int64_t counter;
};

static int
hammer_format_freemap(hammer_transaction_t trans, hammer_volume_t volume,
	struct bigblock_stat *stat);

static int
hammer_free_freemap(hammer_transaction_t trans, hammer_volume_t volume,
	struct bigblock_stat *stat);

static int
hammer_test_free_freemap(hammer_transaction_t trans, hammer_volume_t volume);

int
hammer_ioc_volume_add(hammer_transaction_t trans, hammer_inode_t ip,
		struct hammer_ioc_volume *ioc)
{
	struct hammer_mount *hmp = trans->hmp;
	struct mount *mp = hmp->mp;
	struct hammer_volume_ondisk ondisk;
	struct bigblock_stat stat;
	hammer_volume_t volume;
	int vol_no;
	int error;

	if (mp->mnt_flag & MNT_RDONLY) {
		kprintf("Cannot add volume to read-only HAMMER filesystem\n");
		return (EINVAL);
	}

	if (hmp->nvolumes >= HAMMER_MAX_VOLUMES) {
		kprintf("Max number of HAMMER volumes exceeded\n");
		return (EINVAL);
	}

	if (hammer_lock_ex_try(&hmp->volume_lock) != 0) {
		kprintf("Another volume operation is in progress!\n");
		return (EAGAIN);
	}

	/*
	 * Find an unused volume number.
	 */
	int free_vol_no = 0;
	while (free_vol_no < HAMMER_MAX_VOLUMES &&
		HAMMER_VOLUME_NUMBER_IS_SET(hmp, free_vol_no)) {
		++free_vol_no;
	}
	if (free_vol_no >= HAMMER_MAX_VOLUMES) {
		kprintf("Max number of HAMMER volumes exceeded\n");
		hammer_unlock(&hmp->volume_lock);
		return (EINVAL);
	}

	error = hammer_format_volume_header(
		hmp,
		&ondisk,
		hmp->rootvol->ondisk->vol_name,
		free_vol_no,
		hmp->nvolumes+1,
		ioc->vol_size,
		ioc->boot_area_size,
		ioc->mem_area_size);
	if (error)
		goto end;

	error = hammer_install_volume(hmp, ioc->device_name, NULL, &ondisk);
	if (error)
		goto end;

	hammer_sync_lock_sh(trans);
	hammer_lock_ex(&hmp->blkmap_lock);

	volume = hammer_get_volume(hmp, free_vol_no, &error);
	KKASSERT(volume != NULL && error == 0);

	error =	hammer_format_freemap(trans, volume, &stat);
	KKASSERT(error == 0);
	hammer_rel_volume(volume, 0);

	++hmp->nvolumes;

	/*
	 * Set each volume's new value of the vol_count field.
	 */
	HAMMER_VOLUME_NUMBER_FOREACH(hmp, vol_no) {
		volume = hammer_get_volume(hmp, vol_no, &error);
		KKASSERT(volume != NULL && error == 0);
		hammer_modify_volume_field(trans, volume, vol_count);
		volume->ondisk->vol_count = hmp->nvolumes;
		hammer_modify_volume_done(volume);

		/*
		 * Only changes to the header of the root volume
		 * are automatically flushed to disk. For all
		 * other volumes that we modify we do it here.
		 *
		 * No interlock is needed, volume buffers are not
		 * messed with by bioops.
		 */
		if (volume != trans->rootvol && volume->io.modified) {
			hammer_crc_set_volume(volume->ondisk);
			hammer_io_flush(&volume->io, 0);
		}

		hammer_rel_volume(volume, 0);
	}

	/*
	 * Update the total number of big-blocks.
	 */
	hammer_modify_volume_field(trans, trans->rootvol, vol0_stat_bigblocks);
	trans->rootvol->ondisk->vol0_stat_bigblocks += stat.total_bigblocks;
	hammer_modify_volume_done(trans->rootvol);

	/*
	 * Big-block count changed so recompute the total number of blocks.
	 */
	mp->mnt_stat.f_blocks = trans->rootvol->ondisk->vol0_stat_bigblocks *
				HAMMER_BUFFERS_PER_BIGBLOCK;
	mp->mnt_vstat.f_blocks = trans->rootvol->ondisk->vol0_stat_bigblocks *
				HAMMER_BUFFERS_PER_BIGBLOCK;

	/*
	 * Update the total number of free big-blocks.
	 */
	hammer_modify_volume_field(trans, trans->rootvol,
		vol0_stat_freebigblocks);
	trans->rootvol->ondisk->vol0_stat_freebigblocks += stat.total_free_bigblocks;
	hammer_modify_volume_done(trans->rootvol);

	/*
	 * Update the copy in hmp.
	 */
	hmp->copy_stat_freebigblocks =
		trans->rootvol->ondisk->vol0_stat_freebigblocks;

	hammer_unlock(&hmp->blkmap_lock);
	hammer_sync_unlock(trans);

	KKASSERT(error == 0);
end:
	hammer_unlock(&hmp->volume_lock);
	if (error)
		kprintf("An error occurred: %d\n", error);
	return (error);
}


/*
 * Remove a volume.
 */
int
hammer_ioc_volume_del(hammer_transaction_t trans, hammer_inode_t ip,
		struct hammer_ioc_volume *ioc)
{
	struct hammer_mount *hmp = trans->hmp;
	struct mount *mp = hmp->mp;
	struct hammer_volume_ondisk *ondisk;
	struct bigblock_stat stat;
	hammer_volume_t volume;
	int vol_no;
	int error = 0;

	if (mp->mnt_flag & MNT_RDONLY) {
		kprintf("Cannot del volume from read-only HAMMER filesystem\n");
		return (EINVAL);
	}

	if (hammer_lock_ex_try(&hmp->volume_lock) != 0) {
		kprintf("Another volume operation is in progress!\n");
		return (EAGAIN);
	}

	volume = NULL;

	/*
	 * find volume by volname
	 */
	HAMMER_VOLUME_NUMBER_FOREACH(hmp, vol_no) {
		volume = hammer_get_volume(hmp, vol_no, &error);
		KKASSERT(volume != NULL && error == 0);
		if (strcmp(volume->vol_name, ioc->device_name) == 0) {
			break;
		}
		hammer_rel_volume(volume, 0);
		volume = NULL;
	}

	if (volume == NULL) {
		kprintf("Couldn't find volume\n");
		error = EINVAL;
		goto end;
	}

	if (volume == trans->rootvol) {
		kprintf("Cannot remove root-volume\n");
		hammer_rel_volume(volume, 0);
		error = EINVAL;
		goto end;
	}

	/*
	 * Reblock filesystem if the volume is not empty
	 */
	hmp->volume_to_remove = volume->vol_no;

	if (hammer_test_free_freemap(trans, volume)) {
		error = hammer_do_reblock(trans, ip);
		if (error) {
			hmp->volume_to_remove = -1;
			hammer_rel_volume(volume, 0);
			goto end;
		}
	}

	/*
	 * Sync filesystem
	 */
	int count = 0;
	while (hammer_flusher_haswork(hmp)) {
		hammer_flusher_sync(hmp);
		++count;
		if (count >= 5) {
			if (count == 5)
				kprintf("HAMMER: flushing.");
			else
				kprintf(".");
			tsleep(&count, 0, "hmrufl", hz);
		}
		if (count == 30) {
			kprintf("giving up");
			break;
		}
	}
	if (count >= 5)
		kprintf("\n");

	hammer_sync_lock_sh(trans);
	hammer_lock_ex(&hmp->blkmap_lock);

	/*
	 * We use stat later to update rootvol's big-block stats
	 */
	error = hammer_free_freemap(trans, volume, &stat);
	if (error) {
		kprintf("Failed to free volume: ");
		if (error == EBUSY)
			kprintf("Volume %d not empty\n", volume->vol_no);
		else
			kprintf("%d\n", error);
		hmp->volume_to_remove = -1;
		hammer_rel_volume(volume, 0);
		hammer_unlock(&hmp->blkmap_lock);
		hammer_sync_unlock(trans);
		goto end;
	}

	hmp->volume_to_remove = -1;
	hammer_rel_volume(volume, 0);

	/*
	 * Unload buffers
	 */
        RB_SCAN(hammer_buf_rb_tree, &hmp->rb_bufs_root, NULL,
		hammer_unload_buffer, volume);

	bzero(&ondisk, sizeof(ondisk));
	error = hammer_unload_volume(volume, &ondisk);
	if (error == -1) {
		kprintf("Failed to unload volume\n");
		hammer_unlock(&hmp->blkmap_lock);
		hammer_sync_unlock(trans);
		goto end;
	}

	--hmp->nvolumes;

	/*
	 * Set each volume's new value of the vol_count field.
	 */
	HAMMER_VOLUME_NUMBER_FOREACH(hmp, vol_no) {
		volume = hammer_get_volume(hmp, vol_no, &error);
		KKASSERT(volume != NULL && error == 0);
		hammer_modify_volume_field(trans, volume, vol_count);
		volume->ondisk->vol_count = hmp->nvolumes;
		hammer_modify_volume_done(volume);

		/*
		 * Only changes to the header of the root volume
		 * are automatically flushed to disk. For all
		 * other volumes that we modify we do it here.
		 *
		 * No interlock is needed, volume buffers are not
		 * messed with by bioops.
		 */
		if (volume != trans->rootvol && volume->io.modified) {
			hammer_crc_set_volume(volume->ondisk);
			hammer_io_flush(&volume->io, 0);
		}

		hammer_rel_volume(volume, 0);
	}

	/*
	 * Update the total number of big-blocks.
	 */
	hammer_modify_volume_field(trans, trans->rootvol, vol0_stat_bigblocks);
	trans->rootvol->ondisk->vol0_stat_bigblocks += stat.total_bigblocks;
	hammer_modify_volume_done(trans->rootvol);

	/*
	 * Big-block count changed so recompute the total number of blocks.
	 */
	mp->mnt_stat.f_blocks = trans->rootvol->ondisk->vol0_stat_bigblocks *
				HAMMER_BUFFERS_PER_BIGBLOCK;
	mp->mnt_vstat.f_blocks = trans->rootvol->ondisk->vol0_stat_bigblocks *
				HAMMER_BUFFERS_PER_BIGBLOCK;

	/*
	 * Update the total number of free big-blocks.
	 */
	hammer_modify_volume_field(trans, trans->rootvol,
		vol0_stat_freebigblocks);
	trans->rootvol->ondisk->vol0_stat_freebigblocks += stat.total_free_bigblocks;
	hammer_modify_volume_done(trans->rootvol);

	/*
	 * Update the copy in hmp.
	 */
	hmp->copy_stat_freebigblocks =
		trans->rootvol->ondisk->vol0_stat_freebigblocks;

	hammer_unlock(&hmp->blkmap_lock);
	hammer_sync_unlock(trans);

	KKASSERT(error == 0);
end:
	hammer_unlock(&hmp->volume_lock);
	if (error)
		kprintf("An error occurred: %d\n", error);
	return (error);
}


int
hammer_ioc_volume_list(hammer_transaction_t trans, hammer_inode_t ip,
    struct hammer_ioc_volume_list *ioc)
{
	struct hammer_mount *hmp = trans->hmp;
	hammer_volume_t volume;
	int error = 0;
	int i, len, cnt = 0;

	if (hammer_lock_ex_try(&hmp->volume_lock) != 0) {
		kprintf("Another volume operation is in progress!\n");
		return (EAGAIN);
	}

	HAMMER_VOLUME_NUMBER_FOREACH(hmp, i) {
		if (cnt >= ioc->nvols)
			break;
		volume = hammer_get_volume(hmp, i, &error);
		KKASSERT(volume != NULL && error == 0);

		len = strlen(volume->vol_name) + 1;
		KKASSERT(len <= MAXPATHLEN);

		error = copyout(volume->vol_name, ioc->vols[cnt].device_name,
				len);
		if (error) {
			hammer_rel_volume(volume, 0);
			goto end;
		}
		cnt++;
		hammer_rel_volume(volume, 0);
	}
	ioc->nvols = cnt;

end:
	hammer_unlock(&hmp->volume_lock);
	return (error);
}

static
int
hammer_do_reblock(hammer_transaction_t trans, hammer_inode_t ip)
{
	int error;

	struct hammer_ioc_reblock reblock;
	bzero(&reblock, sizeof(reblock));

	reblock.key_beg.localization = HAMMER_MIN_LOCALIZATION;
	reblock.key_beg.obj_id = HAMMER_MIN_OBJID;
	reblock.key_end.localization = HAMMER_MAX_LOCALIZATION;
	reblock.key_end.obj_id = HAMMER_MAX_OBJID;
	reblock.head.flags = HAMMER_IOC_DO_FLAGS;
	reblock.free_level = 0;
	reblock.allpfs = 1;

	kprintf("reblock started\n");
	error = hammer_ioc_reblock(trans, ip, &reblock);

	if (reblock.head.flags & HAMMER_IOC_HEAD_INTR) {
		error = EINTR;
	}

	if (error) {
		if (error == EINTR) {
			kprintf("reblock was interrupted\n");
		} else {
			kprintf("reblock failed: %d\n", error);
		}
		return(error);
	}

	return(0);
}

/*
 * Iterate over all usable L1 entries of the volume and
 * the corresponding L2 entries.
 */
static int
hammer_iterate_l1l2_entries(hammer_transaction_t trans, hammer_volume_t volume,
	int (*callback)(hammer_transaction_t, hammer_volume_t, hammer_buffer_t*,
		struct hammer_blockmap_layer1*, struct hammer_blockmap_layer2*,
		hammer_off_t, hammer_off_t, void*),
	void *data)
{
	struct hammer_mount *hmp = trans->hmp;
	hammer_blockmap_t freemap = &hmp->blockmap[HAMMER_ZONE_FREEMAP_INDEX];
	int error = 0;
	hammer_off_t phys_off;
	hammer_off_t block_off;
	hammer_off_t layer1_off;
	hammer_off_t layer2_off;
	hammer_off_t aligned_buf_end_off;
	hammer_off_t aligned_vol_end_off;
	struct hammer_blockmap_layer1 *layer1;
	struct hammer_blockmap_layer2 *layer2;
	hammer_buffer_t buffer1 = NULL;
	hammer_buffer_t buffer2 = NULL;

	/*
	 * Calculate the usable size of the volume, which
	 * must be aligned at a big-block (8 MB) boundary.
	 */
	aligned_buf_end_off = HAMMER_ENCODE_RAW_BUFFER(volume->ondisk->vol_no,
		(volume->ondisk->vol_buf_end - volume->ondisk->vol_buf_beg)
		& ~HAMMER_BIGBLOCK_MASK64);
	aligned_vol_end_off = (aligned_buf_end_off + HAMMER_BLOCKMAP_LAYER2_MASK)
		& ~HAMMER_BLOCKMAP_LAYER2_MASK;

	/*
	 * Iterate the volume's address space in chunks of 4 TB, where each
	 * chunk consists of at least one physically available 8 MB big-block.
	 *
	 * For each chunk we need one L1 entry and one L2 big-block.
	 * We use the first big-block of each chunk as L2 block.
	 */
	for (phys_off = HAMMER_ENCODE_RAW_BUFFER(volume->ondisk->vol_no, 0);
	     phys_off < aligned_vol_end_off;
	     phys_off += HAMMER_BLOCKMAP_LAYER2) {
		for (block_off = 0;
		     block_off < HAMMER_BLOCKMAP_LAYER2;
		     block_off += HAMMER_BIGBLOCK_SIZE) {
			layer2_off = phys_off +
				HAMMER_BLOCKMAP_LAYER2_OFFSET(block_off);
			layer2 = hammer_bread(hmp, layer2_off, &error, &buffer2);
			if (error)
				goto end;

			error = callback(trans, volume, &buffer2, NULL,
					 layer2, phys_off, block_off, data);
			if (error)
				goto end;
		}

		layer1_off = freemap->phys_offset +
				HAMMER_BLOCKMAP_LAYER1_OFFSET(phys_off);
		layer1 = hammer_bread(hmp, layer1_off, &error, &buffer1);
		if (error)
			goto end;

		error = callback(trans, volume, &buffer1, layer1, NULL,
				 phys_off, 0, data);
		if (error)
			goto end;
	}

end:
	if (buffer1)
		hammer_rel_buffer(buffer1, 0);
	if (buffer2)
		hammer_rel_buffer(buffer2, 0);

	return error;
}


static int
format_callback(hammer_transaction_t trans, hammer_volume_t volume,
	hammer_buffer_t *bufferp,
	struct hammer_blockmap_layer1 *layer1,
	struct hammer_blockmap_layer2 *layer2,
	hammer_off_t phys_off,
	hammer_off_t block_off,
	void *data)
{
	struct bigblock_stat *stat = (struct bigblock_stat*)data;

	/*
	 * Calculate the usable size of the volume, which must be aligned
	 * at a big-block (8 MB) boundary.
	 */
	hammer_off_t aligned_buf_end_off;
	aligned_buf_end_off = HAMMER_ENCODE_RAW_BUFFER(volume->ondisk->vol_no,
		(volume->ondisk->vol_buf_end - volume->ondisk->vol_buf_beg)
		& ~HAMMER_BIGBLOCK_MASK64);

	if (layer1) {
		KKASSERT(layer1->phys_offset == HAMMER_BLOCKMAP_UNAVAIL);

		hammer_modify_buffer(trans, *bufferp, layer1, sizeof(*layer1));
		bzero(layer1, sizeof(*layer1));
		layer1->phys_offset = phys_off;
		layer1->blocks_free = stat->counter;
		layer1->layer1_crc = crc32(layer1, HAMMER_LAYER1_CRCSIZE);
		hammer_modify_buffer_done(*bufferp);
		stat->counter = 0; /* reset */
	} else if (layer2) {
		hammer_modify_buffer(trans, *bufferp, layer2, sizeof(*layer2));
		bzero(layer2, sizeof(*layer2));

		if (block_off == 0) {
			/*
			 * The first entry represents the L2 big-block itself.
			 * Note that the first entry represents the L1 big-block
			 * and the second entry represents the L2 big-block for
			 * root volume, but this function assumes the volume is
			 * non-root given that we can't add a new root volume.
			 */
			KKASSERT(trans->rootvol && trans->rootvol != volume);
			layer2->zone = HAMMER_ZONE_FREEMAP_INDEX;
			layer2->append_off = HAMMER_BIGBLOCK_SIZE;
			layer2->bytes_free = 0;
		} else if (phys_off + block_off < aligned_buf_end_off) {
			/*
			 * Available big-block
			 */
			layer2->zone = 0;
			layer2->append_off = 0;
			layer2->bytes_free = HAMMER_BIGBLOCK_SIZE;
			++stat->total_bigblocks;
			++stat->total_free_bigblocks;
			++stat->counter;
		} else {
			/*
			 * Big-block outside of physically available
			 * space
			 */
			layer2->zone = HAMMER_ZONE_UNAVAIL_INDEX;
			layer2->append_off = HAMMER_BIGBLOCK_SIZE;
			layer2->bytes_free = 0;
		}

		layer2->entry_crc = crc32(layer2, HAMMER_LAYER2_CRCSIZE);
		hammer_modify_buffer_done(*bufferp);
	} else {
		KKASSERT(0);
	}

	return 0;
}

static int
hammer_format_freemap(hammer_transaction_t trans, hammer_volume_t volume,
	struct bigblock_stat *stat)
{
	stat->total_bigblocks = 0;
	stat->total_free_bigblocks = 0;
	stat->counter = 0;
	return hammer_iterate_l1l2_entries(trans, volume, format_callback, stat);
}

static int
free_callback(hammer_transaction_t trans, hammer_volume_t volume __unused,
	hammer_buffer_t *bufferp,
	struct hammer_blockmap_layer1 *layer1,
	struct hammer_blockmap_layer2 *layer2,
	hammer_off_t phys_off,
	hammer_off_t block_off __unused,
	void *data)
{
	struct bigblock_stat *stat = (struct bigblock_stat*)data;

	if (layer1) {
		if (layer1->phys_offset == HAMMER_BLOCKMAP_UNAVAIL) {
			/*
			 * This layer1 entry is already free.
			 */
			return 0;
		}

		KKASSERT((int)HAMMER_VOL_DECODE(layer1->phys_offset) ==
			trans->hmp->volume_to_remove);

		/*
		 * Free the L1 entry
		 */
		hammer_modify_buffer(trans, *bufferp, layer1, sizeof(*layer1));
		bzero(layer1, sizeof(*layer1));
		layer1->phys_offset = HAMMER_BLOCKMAP_UNAVAIL;
		layer1->layer1_crc = crc32(layer1, HAMMER_LAYER1_CRCSIZE);
		hammer_modify_buffer_done(*bufferp);

		return 0;
	} else if (layer2) {
		if (layer2->zone == HAMMER_ZONE_UNAVAIL_INDEX) {
			return 0;
		}

		if (layer2->zone == HAMMER_ZONE_FREEMAP_INDEX) {
			return 0;
		}

		if (layer2->append_off == 0 &&
		    layer2->bytes_free == HAMMER_BIGBLOCK_SIZE) {
			--stat->total_bigblocks;
			--stat->total_free_bigblocks;
			return 0;
		}

		/*
		 * We found a layer2 entry that is not empty!
		 */
		return EBUSY;
	} else {
		KKASSERT(0);
	}

	return EINVAL;
}

/*
 * Non-zero return value means we can't free the volume.
 */
static int
test_free_callback(hammer_transaction_t trans, hammer_volume_t volume __unused,
	hammer_buffer_t *bufferp,
	struct hammer_blockmap_layer1 *layer1,
	struct hammer_blockmap_layer2 *layer2,
	hammer_off_t phys_off,
	hammer_off_t block_off __unused,
	void *data)
{
	if (layer2 == NULL) {
		return(0);  /* only layer2 needs to be tested */
	}

	if (layer2->zone == HAMMER_ZONE_UNAVAIL_INDEX) {
		return(0);  /* beyond physically available space */
	}
	if (layer2->zone == HAMMER_ZONE_FREEMAP_INDEX) {
		return(0);  /* big-block for layer1/2 */
	}
	if (layer2->append_off == 0 &&
	    layer2->bytes_free == HAMMER_BIGBLOCK_SIZE) {
		return(0);  /* big-block is 0% used */
	}

	return(EBUSY);  /* big-block has data */
}

static int
hammer_free_freemap(hammer_transaction_t trans, hammer_volume_t volume,
	struct bigblock_stat *stat)
{
	int error;

	error = hammer_test_free_freemap(trans, volume);
	if (error)
		return error;  /* not ready to free */

	stat->total_bigblocks = 0;
	stat->total_free_bigblocks = 0;
	stat->counter = 0;
	return hammer_iterate_l1l2_entries(trans, volume, free_callback, stat);
}

static int
hammer_test_free_freemap(hammer_transaction_t trans, hammer_volume_t volume)
{
	return hammer_iterate_l1l2_entries(trans, volume, test_free_callback, NULL);
}

static int
hammer_format_volume_header(struct hammer_mount *hmp,
	struct hammer_volume_ondisk *ondisk,
	const char *vol_name, int vol_no, int vol_count,
	int64_t vol_size, int64_t boot_area_size, int64_t mem_area_size)
{
	int64_t vol_alloc;

	KKASSERT(HAMMER_BUFSIZE >= sizeof(struct hammer_volume_ondisk));

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
	 * big-block allocator.
	 */
	vol_alloc = HAMMER_BUFSIZE * 16;
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
		kprintf("volume %d %s is too small to hold the volume header\n",
		     ondisk->vol_no, ondisk->vol_name);
		return(EFTYPE);
	}

	ondisk->vol_nblocks = (ondisk->vol_buf_end - ondisk->vol_buf_beg) /
			      HAMMER_BUFSIZE;
	ondisk->vol_blocksize = HAMMER_BUFSIZE;
	return(0);
}
