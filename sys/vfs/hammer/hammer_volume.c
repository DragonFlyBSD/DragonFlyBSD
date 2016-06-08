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

static int
hammer_format_volume_header(hammer_mount_t hmp,
	struct hammer_ioc_volume *ioc,
	struct hammer_volume_ondisk *ondisk,
	int vol_no);

static int
hammer_update_volumes_header(hammer_transaction_t trans,
	int64_t total_bigblocks, int64_t empty_bigblocks);

static int
hammer_do_reblock(hammer_transaction_t trans, hammer_inode_t ip);

static int
hammer_format_freemap(hammer_transaction_t trans, hammer_volume_t volume);

static int
hammer_free_freemap(hammer_transaction_t trans, hammer_volume_t volume);

static int
hammer_count_bigblocks(hammer_mount_t hmp, hammer_volume_t volume,
	int64_t *total_bigblocks, int64_t *empty_bigblocks);

int
hammer_ioc_volume_add(hammer_transaction_t trans, hammer_inode_t ip,
		struct hammer_ioc_volume *ioc)
{
	struct hammer_mount *hmp = trans->hmp;
	struct mount *mp = hmp->mp;
	struct hammer_volume_ondisk ondisk;
	hammer_volume_t volume;
	int64_t total_bigblocks, empty_bigblocks;
	int free_vol_no = 0;
	int error;

	if (mp->mnt_flag & MNT_RDONLY) {
		hmkprintf(hmp, "Cannot add volume to read-only HAMMER filesystem\n");
		return (EINVAL);
	}

	if (hammer_lock_ex_try(&hmp->volume_lock) != 0) {
		hmkprintf(hmp, "Another volume operation is in progress!\n");
		return (EAGAIN);
	}

	if (hmp->nvolumes >= HAMMER_MAX_VOLUMES) {
		hammer_unlock(&hmp->volume_lock);
		hmkprintf(hmp, "Max number of HAMMER volumes exceeded\n");
		return (EINVAL);
	}

	/*
	 * Find an unused volume number.
	 */
	while (free_vol_no < HAMMER_MAX_VOLUMES &&
		HAMMER_VOLUME_NUMBER_IS_SET(hmp, free_vol_no)) {
		++free_vol_no;
	}
	if (free_vol_no >= HAMMER_MAX_VOLUMES) {
		hmkprintf(hmp, "Max number of HAMMER volumes exceeded\n");
		error = EINVAL;
		goto end;
	}

	error = hammer_format_volume_header(hmp, ioc, &ondisk, free_vol_no);
	if (error)
		goto end;

	error = hammer_install_volume(hmp, ioc->device_name, NULL, &ondisk);
	if (error)
		goto end;

	hammer_sync_lock_sh(trans);
	hammer_lock_ex(&hmp->blkmap_lock);

	volume = hammer_get_volume(hmp, free_vol_no, &error);
	KKASSERT(volume != NULL && error == 0);

	error =	hammer_format_freemap(trans, volume);
	KKASSERT(error == 0);

	error = hammer_count_bigblocks(hmp, volume,
			&total_bigblocks, &empty_bigblocks);
	KKASSERT(error == 0);
	KKASSERT(total_bigblocks == empty_bigblocks);

	hammer_rel_volume(volume, 0);

	++hmp->nvolumes;
	error = hammer_update_volumes_header(trans,
			total_bigblocks, empty_bigblocks);
	KKASSERT(error == 0);

	hammer_unlock(&hmp->blkmap_lock);
	hammer_sync_unlock(trans);

	KKASSERT(error == 0);
end:
	hammer_unlock(&hmp->volume_lock);
	if (error)
		hmkprintf(hmp, "An error occurred: %d\n", error);
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
	struct hammer_volume_ondisk ondisk;
	hammer_volume_t volume;
	int64_t total_bigblocks, empty_bigblocks;
	int vol_no;
	int error = 0;

	if (mp->mnt_flag & MNT_RDONLY) {
		hmkprintf(hmp, "Cannot del volume from read-only HAMMER filesystem\n");
		return (EINVAL);
	}

	if (hammer_lock_ex_try(&hmp->volume_lock) != 0) {
		hmkprintf(hmp, "Another volume operation is in progress!\n");
		return (EAGAIN);
	}

	if (hmp->nvolumes <= 1) {
		hammer_unlock(&hmp->volume_lock);
		hmkprintf(hmp, "No HAMMER volume to delete\n");
		return (EINVAL);
	}

	/*
	 * find volume by volname
	 */
	volume = NULL;
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
		hmkprintf(hmp, "Couldn't find volume\n");
		error = EINVAL;
		goto end;
	}

	if (volume == trans->rootvol) {
		hmkprintf(hmp, "Cannot remove root-volume\n");
		hammer_rel_volume(volume, 0);
		error = EINVAL;
		goto end;
	}

	/*
	 * Reblock filesystem if the volume is not empty
	 */
	hmp->volume_to_remove = volume->vol_no;

	error = hammer_count_bigblocks(hmp, volume,
			&total_bigblocks, &empty_bigblocks);
	KKASSERT(error == 0);

	if (total_bigblocks == empty_bigblocks) {
		hmkprintf(hmp, "%s is already empty\n", volume->vol_name);
	} else if (ioc->flag & HAMMER_IOC_VOLUME_REBLOCK) {
		error = hammer_do_reblock(trans, ip);
		if (error) {
			hmp->volume_to_remove = -1;
			hammer_rel_volume(volume, 0);
			goto end;
		}
	} else {
		hmkprintf(hmp, "%s is not empty\n", volume->vol_name);
		hammer_rel_volume(volume, 0);
		error = ENOTEMPTY;
		goto end;
	}

	hammer_sync_lock_sh(trans);
	hammer_lock_ex(&hmp->blkmap_lock);

	error = hammer_count_bigblocks(hmp, volume,
			&total_bigblocks, &empty_bigblocks);
	KKASSERT(error == 0);

	error = hammer_free_freemap(trans, volume);
	if (error) {
		hmkprintf(hmp, "Failed to free volume: ");
		if (error == EBUSY)
			kprintf("Volume %d not empty\n", volume->vol_no);
		else
			kprintf("%d\n", error);
		hmp->volume_to_remove = -1;
		hammer_rel_volume(volume, 0);
		goto end1;
	}
	hammer_rel_volume(volume, 0);

	/*
	 * XXX: Temporary solution for
	 * http://lists.dragonflybsd.org/pipermail/kernel/2015-August/175027.html
	 */
	hammer_unlock(&hmp->blkmap_lock);
	hammer_sync_unlock(trans);
	hammer_flusher_sync(hmp); /* 1 */
	hammer_flusher_sync(hmp); /* 2 */
	hammer_flusher_sync(hmp); /* 3 */
	hammer_sync_lock_sh(trans);
	hammer_lock_ex(&hmp->blkmap_lock);

	/*
	 * Unload buffers
	 */
        RB_SCAN(hammer_buf_rb_tree, &hmp->rb_bufs_root, NULL,
		hammer_unload_buffer, volume);

	bzero(&ondisk, sizeof(ondisk));
	error = hammer_unload_volume(volume, &ondisk);
	if (error == -1) {
		hmkprintf(hmp, "Failed to unload volume\n");
		goto end1;
	}

	--hmp->nvolumes;
	error = hammer_update_volumes_header(trans,
			-total_bigblocks, -empty_bigblocks);
	KKASSERT(error == 0);
	hmp->volume_to_remove = -1;

end1:
	hammer_unlock(&hmp->blkmap_lock);
	hammer_sync_unlock(trans);

end:
	hammer_unlock(&hmp->volume_lock);
	if (error)
		hmkprintf(hmp, "An error occurred: %d\n", error);
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
		hmkprintf(hmp, "Another volume operation is in progress!\n");
		return (EAGAIN);
	}

	HAMMER_VOLUME_NUMBER_FOREACH(hmp, i) {
		if (cnt >= ioc->nvols)
			break;
		volume = hammer_get_volume(hmp, i, &error);
		KKASSERT(volume != NULL && error == 0);

		len = strlen(volume->vol_name) + 1;
		KKASSERT(len <= MAXPATHLEN);

		ioc->vols[cnt].vol_no = volume->vol_no;
		error = copyout(volume->vol_name, ioc->vols[cnt].device_name,
				len);
		hammer_rel_volume(volume, 0);
		if (error)
			goto end;
		cnt++;
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
	struct hammer_mount *hmp = trans->hmp;
	int error;
	int vol_no;

	struct hammer_ioc_reblock reblock;
	bzero(&reblock, sizeof(reblock));

	vol_no = trans->hmp->volume_to_remove;
	KKASSERT(vol_no != -1);

	reblock.key_beg.localization = HAMMER_MIN_LOCALIZATION;
	reblock.key_beg.obj_id = HAMMER_MIN_OBJID;
	reblock.key_end.localization = HAMMER_MAX_LOCALIZATION;
	reblock.key_end.obj_id = HAMMER_MAX_OBJID;
	reblock.head.flags = HAMMER_IOC_DO_FLAGS;
	reblock.free_level = 0;	/* reblock all big-blocks */
	reblock.allpfs = 1;	/* reblock all PFS */
	reblock.vol_no = vol_no;

	hmkprintf(hmp, "reblock started\n");
	error = hammer_ioc_reblock(trans, ip, &reblock);

	if (reblock.head.flags & HAMMER_IOC_HEAD_INTR) {
		error = EINTR;
	}

	if (error) {
		if (error == EINTR) {
			hmkprintf(hmp, "reblock was interrupted\n");
		} else {
			hmkprintf(hmp, "reblock failed: %d\n", error);
		}
		return(error);
	}

	return(0);
}

/*
 * XXX This somehow needs to stop doing hammer_modify_buffer() for
 * layer2 entries.  In theory adding a large block device could
 * blow away UNDO fifo.  The best way is to format layer2 entries
 * in userspace without UNDO getting involved before the device is
 * safely added to the filesystem.  HAMMER has no interest in what
 * has happened to the device before it safely joins the filesystem.
 */
static int
hammer_format_freemap(hammer_transaction_t trans, hammer_volume_t volume)
{
	struct hammer_mount *hmp = trans->hmp;
	struct hammer_volume_ondisk *ondisk;
	hammer_blockmap_t freemap;
	hammer_off_t alloc_offset;
	hammer_off_t phys_offset;
	hammer_off_t block_offset;
	hammer_off_t layer1_offset;
	hammer_off_t layer2_offset;
	hammer_off_t vol_free_end;
	hammer_off_t aligned_vol_free_end;
	struct hammer_blockmap_layer1 *layer1;
	struct hammer_blockmap_layer2 *layer2;
	hammer_buffer_t buffer1 = NULL;
	hammer_buffer_t buffer2 = NULL;
	int64_t vol_buf_size;
	int64_t layer1_count = 0;
	int error = 0;

	KKASSERT(volume->vol_no != HAMMER_ROOT_VOLNO);

	ondisk = volume->ondisk;
	vol_buf_size = ondisk->vol_buf_end - ondisk->vol_buf_beg;
	KKASSERT((vol_buf_size & ~HAMMER_OFF_SHORT_MASK) == 0);
	vol_free_end = HAMMER_ENCODE_RAW_BUFFER(ondisk->vol_no,
			vol_buf_size & ~HAMMER_BIGBLOCK_MASK64);
	aligned_vol_free_end = (vol_free_end + HAMMER_BLOCKMAP_LAYER2_MASK)
			& ~HAMMER_BLOCKMAP_LAYER2_MASK;

	freemap = &hmp->blockmap[HAMMER_ZONE_FREEMAP_INDEX];
	alloc_offset = HAMMER_ENCODE_RAW_BUFFER(volume->vol_no, 0);

	hmkprintf(hmp, "Initialize freemap volume %d\n", volume->vol_no);

	for (phys_offset = HAMMER_ENCODE_RAW_BUFFER(volume->vol_no, 0);
	     phys_offset < aligned_vol_free_end;
	     phys_offset += HAMMER_BLOCKMAP_LAYER2) {
		layer1_offset = freemap->phys_offset +
				HAMMER_BLOCKMAP_LAYER1_OFFSET(phys_offset);
		layer1 = hammer_bread(hmp, layer1_offset, &error, &buffer1);
		if (error)
			goto end;
		if (layer1->phys_offset == HAMMER_BLOCKMAP_UNAVAIL) {
			hammer_modify_buffer(trans, buffer1, layer1, sizeof(*layer1));
			bzero(layer1, sizeof(*layer1));
			layer1->phys_offset = alloc_offset;
			layer1->blocks_free = 0;
			layer1->layer1_crc = crc32(layer1, HAMMER_LAYER1_CRCSIZE);
			hammer_modify_buffer_done(buffer1);
			alloc_offset += HAMMER_BIGBLOCK_SIZE;
		}
	}

	for (phys_offset = HAMMER_ENCODE_RAW_BUFFER(volume->vol_no, 0);
	     phys_offset < aligned_vol_free_end;
	     phys_offset += HAMMER_BLOCKMAP_LAYER2) {
		layer1_count = 0;
		layer1_offset = freemap->phys_offset +
				HAMMER_BLOCKMAP_LAYER1_OFFSET(phys_offset);
		layer1 = hammer_bread(hmp, layer1_offset, &error, &buffer1);
		if (error)
			goto end;
		KKASSERT(layer1->phys_offset != HAMMER_BLOCKMAP_UNAVAIL);

		for (block_offset = 0;
		     block_offset < HAMMER_BLOCKMAP_LAYER2;
		     block_offset += HAMMER_BIGBLOCK_SIZE) {
			layer2_offset = layer1->phys_offset +
				        HAMMER_BLOCKMAP_LAYER2_OFFSET(block_offset);
			layer2 = hammer_bread(hmp, layer2_offset, &error, &buffer2);
			if (error)
				goto end;

			hammer_modify_buffer(trans, buffer2, layer2, sizeof(*layer2));
			bzero(layer2, sizeof(*layer2));

			if (phys_offset + block_offset < alloc_offset) {
				layer2->zone = HAMMER_ZONE_FREEMAP_INDEX;
				layer2->append_off = HAMMER_BIGBLOCK_SIZE;
				layer2->bytes_free = 0;
			} else if (phys_offset + block_offset < vol_free_end) {
				layer2->zone = 0;
				layer2->append_off = 0;
				layer2->bytes_free = HAMMER_BIGBLOCK_SIZE;
				++layer1_count;
			} else {
				layer2->zone = HAMMER_ZONE_UNAVAIL_INDEX;
				layer2->append_off = HAMMER_BIGBLOCK_SIZE;
				layer2->bytes_free = 0;
			}

			layer2->entry_crc = crc32(layer2, HAMMER_LAYER2_CRCSIZE);
			hammer_modify_buffer_done(buffer2);
		}

		hammer_modify_buffer(trans, buffer1, layer1, sizeof(*layer1));
		layer1->blocks_free += layer1_count;
		layer1->layer1_crc = crc32(layer1, HAMMER_LAYER1_CRCSIZE);
		hammer_modify_buffer_done(buffer1);
	}

end:
	if (buffer1)
		hammer_rel_buffer(buffer1, 0);
	if (buffer2)
		hammer_rel_buffer(buffer2, 0);

	return error;
}

/*
 * XXX This somehow needs to stop doing hammer_modify_buffer() for
 * layer2 entries.  In theory removing a large block device could
 * blow away UNDO fifo.  The best way is to erase layer2 entries
 * in userspace without UNDO getting involved after the device has
 * been safely removed from the filesystem.  HAMMER has no interest
 * in what happens to the device once it's safely removed.
 */
static int
hammer_free_freemap(hammer_transaction_t trans, hammer_volume_t volume)
{
	struct hammer_mount *hmp = trans->hmp;
	struct hammer_volume_ondisk *ondisk;
	hammer_blockmap_t freemap;
	hammer_off_t phys_offset;
	hammer_off_t block_offset;
	hammer_off_t layer1_offset;
	hammer_off_t layer2_offset;
	hammer_off_t vol_free_end;
	hammer_off_t aligned_vol_free_end;
	struct hammer_blockmap_layer1 *layer1;
	struct hammer_blockmap_layer2 *layer2;
	hammer_buffer_t buffer1 = NULL;
	hammer_buffer_t buffer2 = NULL;
	int64_t vol_buf_size;
	int error = 0;

	KKASSERT(volume->vol_no != HAMMER_ROOT_VOLNO);

	ondisk = volume->ondisk;
	vol_buf_size = ondisk->vol_buf_end - ondisk->vol_buf_beg;
	KKASSERT((vol_buf_size & ~HAMMER_OFF_SHORT_MASK) == 0);
	vol_free_end = HAMMER_ENCODE_RAW_BUFFER(ondisk->vol_no,
			vol_buf_size & ~HAMMER_BIGBLOCK_MASK64);
	aligned_vol_free_end = (vol_free_end + HAMMER_BLOCKMAP_LAYER2_MASK)
			& ~HAMMER_BLOCKMAP_LAYER2_MASK;

	freemap = &hmp->blockmap[HAMMER_ZONE_FREEMAP_INDEX];

	hmkprintf(hmp, "Free freemap volume %d\n", volume->vol_no);

	for (phys_offset = HAMMER_ENCODE_RAW_BUFFER(volume->vol_no, 0);
	     phys_offset < aligned_vol_free_end;
	     phys_offset += HAMMER_BLOCKMAP_LAYER2) {
		layer1_offset = freemap->phys_offset +
				HAMMER_BLOCKMAP_LAYER1_OFFSET(phys_offset);
		layer1 = hammer_bread(hmp, layer1_offset, &error, &buffer1);
		if (error)
			goto end;
		KKASSERT(layer1->phys_offset != HAMMER_BLOCKMAP_UNAVAIL);

		for (block_offset = 0;
		     block_offset < HAMMER_BLOCKMAP_LAYER2;
		     block_offset += HAMMER_BIGBLOCK_SIZE) {
			layer2_offset = layer1->phys_offset +
				        HAMMER_BLOCKMAP_LAYER2_OFFSET(block_offset);
			layer2 = hammer_bread(hmp, layer2_offset, &error, &buffer2);
			if (error)
				goto end;

			switch (layer2->zone) {
			case HAMMER_ZONE_UNDO_INDEX:
				KKASSERT(0);
			case HAMMER_ZONE_FREEMAP_INDEX:
			case HAMMER_ZONE_UNAVAIL_INDEX:
				continue;
			default:
				KKASSERT(phys_offset + block_offset < aligned_vol_free_end);
				if (layer2->append_off == 0 &&
				    layer2->bytes_free == HAMMER_BIGBLOCK_SIZE)
					continue;
				break;
			}
			return EBUSY;  /* Not empty */
		}
	}

	for (phys_offset = HAMMER_ENCODE_RAW_BUFFER(volume->vol_no, 0);
	     phys_offset < aligned_vol_free_end;
	     phys_offset += HAMMER_BLOCKMAP_LAYER2) {
		layer1_offset = freemap->phys_offset +
				HAMMER_BLOCKMAP_LAYER1_OFFSET(phys_offset);
		layer1 = hammer_bread(hmp, layer1_offset, &error, &buffer1);
		if (error)
			goto end;
		KKASSERT(layer1->phys_offset != HAMMER_BLOCKMAP_UNAVAIL);

		for (block_offset = 0;
		     block_offset < HAMMER_BLOCKMAP_LAYER2;
		     block_offset += HAMMER_BIGBLOCK_SIZE) {
			layer2_offset = layer1->phys_offset +
				        HAMMER_BLOCKMAP_LAYER2_OFFSET(block_offset);
			layer2 = hammer_bread(hmp, layer2_offset, &error, &buffer2);
			if (error)
				goto end;

			switch (layer2->zone) {
			case HAMMER_ZONE_UNDO_INDEX:
				KKASSERT(0);
			default:
				KKASSERT(phys_offset + block_offset < aligned_vol_free_end);
				hammer_modify_buffer(trans, buffer2, layer2, sizeof(*layer2));
				bzero(layer2, sizeof(*layer2));
				hammer_modify_buffer_done(buffer2);
				break;
			}
		}

		hammer_modify_buffer(trans, buffer1, layer1, sizeof(*layer1));
		bzero(layer1, sizeof(*layer1));
		layer1->phys_offset = HAMMER_BLOCKMAP_UNAVAIL;
		layer1->layer1_crc = crc32(layer1, HAMMER_LAYER1_CRCSIZE);
		hammer_modify_buffer_done(buffer1);
	}

end:
	if (buffer1)
		hammer_rel_buffer(buffer1, 0);
	if (buffer2)
		hammer_rel_buffer(buffer2, 0);

	return error;
}

static int
hammer_format_volume_header(hammer_mount_t hmp,
	struct hammer_ioc_volume *ioc,
	struct hammer_volume_ondisk *ondisk,
	int vol_no)
{
	struct hammer_volume_ondisk *root_ondisk;
	int64_t vol_alloc;

	KKASSERT(HAMMER_BUFSIZE >= sizeof(struct hammer_volume_ondisk));

	/*
	 * Just copy from the root volume header.
	 */
	root_ondisk = hmp->rootvol->ondisk;
	bzero(ondisk, sizeof(struct hammer_volume_ondisk));
	ondisk->vol_fsid = root_ondisk->vol_fsid;
	ondisk->vol_fstype = root_ondisk->vol_fstype;
	ksnprintf(ondisk->vol_label, sizeof(ondisk->vol_label), "%s",
		root_ondisk->vol_label);
	ondisk->vol_version = root_ondisk->vol_version;
	ondisk->vol_rootvol = root_ondisk->vol_no;
	ondisk->vol_signature = root_ondisk->vol_signature;

	KKASSERT(ondisk->vol_rootvol == HAMMER_ROOT_VOLNO);
	KKASSERT(ondisk->vol_signature == HAMMER_FSBUF_VOLUME);

	/*
	 * Assign the new vol_no and vol_count.
	 */
	ondisk->vol_no = vol_no;
	ondisk->vol_count = root_ondisk->vol_count + 1;

	/*
	 * Reserve space for (future) header junk.
	 */
	vol_alloc = root_ondisk->vol_bot_beg;
	KKASSERT(vol_alloc == HAMMER_VOL_ALLOC);
	ondisk->vol_bot_beg = vol_alloc;
	vol_alloc += ioc->boot_area_size;
	ondisk->vol_mem_beg = vol_alloc;
	vol_alloc += ioc->mem_area_size;

	/*
	 * The remaining area is the zone 2 buffer allocation area.
	 */
	ondisk->vol_buf_beg = vol_alloc;
	ondisk->vol_buf_end = ioc->vol_size & ~(int64_t)HAMMER_BUFMASK;

	if (ondisk->vol_buf_end < ondisk->vol_buf_beg) {
		hmkprintf(hmp, "volume %d is too small to hold the volume header\n",
			ondisk->vol_no);
		return(EFTYPE);
	}

	return(0);
}

static int
hammer_update_volumes_header(hammer_transaction_t trans,
	int64_t total_bigblocks, int64_t empty_bigblocks)
{
	struct hammer_mount *hmp = trans->hmp;
	struct mount *mp = hmp->mp;
	hammer_volume_t volume;
	int vol_no;
	int error = 0;

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
	trans->rootvol->ondisk->vol0_stat_bigblocks += total_bigblocks;
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
	trans->rootvol->ondisk->vol0_stat_freebigblocks += empty_bigblocks;
	hammer_modify_volume_done(trans->rootvol);

	/*
	 * Update the copy in hmp.
	 */
	hmp->copy_stat_freebigblocks =
		trans->rootvol->ondisk->vol0_stat_freebigblocks;

	return(error);
}

/*
 * Count total big-blocks and empty big-blocks within the volume.
 * The volume must be a non-root volume.
 *
 * Note that total big-blocks doesn't include big-blocks for layer2
 * (and obviously layer1 and undomap).  This is requirement of the
 * volume header and this function is to retrieve that information.
 */
static int
hammer_count_bigblocks(hammer_mount_t hmp, hammer_volume_t volume,
	int64_t *total_bigblocks, int64_t *empty_bigblocks)
{
	struct hammer_volume_ondisk *ondisk;
	hammer_blockmap_t freemap;
	hammer_off_t phys_offset;
	hammer_off_t block_offset;
	hammer_off_t layer1_offset;
	hammer_off_t layer2_offset;
	hammer_off_t vol_free_end;
	hammer_off_t aligned_vol_free_end;
	struct hammer_blockmap_layer1 *layer1;
	struct hammer_blockmap_layer2 *layer2;
	hammer_buffer_t buffer1 = NULL;
	hammer_buffer_t buffer2 = NULL;
	int64_t vol_buf_size;
	int64_t total = 0;
	int64_t empty = 0;
	int error = 0;

	KKASSERT(volume->vol_no != HAMMER_ROOT_VOLNO);

	ondisk = volume->ondisk;
	vol_buf_size = ondisk->vol_buf_end - ondisk->vol_buf_beg;
	KKASSERT((vol_buf_size & ~HAMMER_OFF_SHORT_MASK) == 0);
	vol_free_end = HAMMER_ENCODE_RAW_BUFFER(ondisk->vol_no,
			vol_buf_size & ~HAMMER_BIGBLOCK_MASK64);
	aligned_vol_free_end = (vol_free_end + HAMMER_BLOCKMAP_LAYER2_MASK)
			& ~HAMMER_BLOCKMAP_LAYER2_MASK;

	freemap = &hmp->blockmap[HAMMER_ZONE_FREEMAP_INDEX];

	for (phys_offset = HAMMER_ENCODE_RAW_BUFFER(volume->ondisk->vol_no, 0);
	     phys_offset < aligned_vol_free_end;
	     phys_offset += HAMMER_BLOCKMAP_LAYER2) {
		layer1_offset = freemap->phys_offset +
				HAMMER_BLOCKMAP_LAYER1_OFFSET(phys_offset);
		layer1 = hammer_bread(hmp, layer1_offset, &error, &buffer1);
		if (error)
			goto end;

		for (block_offset = 0;
		     block_offset < HAMMER_BLOCKMAP_LAYER2;
		     block_offset += HAMMER_BIGBLOCK_SIZE) {
			layer2_offset = layer1->phys_offset +
					HAMMER_BLOCKMAP_LAYER2_OFFSET(block_offset);
			layer2 = hammer_bread(hmp, layer2_offset, &error, &buffer2);
			if (error)
				goto end;

			switch (layer2->zone) {
			case HAMMER_ZONE_UNDO_INDEX:
				KKASSERT(0);
			case HAMMER_ZONE_FREEMAP_INDEX:
			case HAMMER_ZONE_UNAVAIL_INDEX:
				continue;
			default:
				KKASSERT(phys_offset + block_offset < aligned_vol_free_end);
				total++;
				if (layer2->append_off == 0 &&
				    layer2->bytes_free == HAMMER_BIGBLOCK_SIZE)
					empty++;
				break;
			}
		}
	}

	hmkprintf(hmp, "big-blocks total=%jd empty=%jd\n", total, empty);
	*total_bigblocks = total;
	*empty_bigblocks = empty;
end:
	if (buffer1)
		hammer_rel_buffer(buffer1, 0);
	if (buffer2)
		hammer_rel_buffer(buffer2, 0);

	return error;
}
