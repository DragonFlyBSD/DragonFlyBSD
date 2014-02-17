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

#include <sys/buf2.h>

static int
hammer_setup_device(struct vnode **devvpp, const char *dev_path, int ronly);

static void
hammer_close_device(struct vnode **devvpp, int ronly);

static int
hammer_format_volume_header(struct hammer_mount *hmp, struct vnode *devvp,
	const char *vol_name, int vol_no, int vol_count,
	int64_t vol_size, int64_t boot_area_size, int64_t mem_area_size);

static int
hammer_clear_volume_header(struct vnode *devvp);

struct bigblock_stat {
	uint64_t total_bigblocks;
	uint64_t total_free_bigblocks;
	uint64_t counter;
};

static int
hammer_format_freemap(hammer_transaction_t trans, hammer_volume_t volume,
	struct bigblock_stat *stat);

static int
hammer_free_freemap(hammer_transaction_t trans, hammer_volume_t volume,
	struct bigblock_stat *stat);

int
hammer_ioc_volume_add(hammer_transaction_t trans, hammer_inode_t ip,
		struct hammer_ioc_volume *ioc)
{
	struct hammer_mount *hmp = trans->hmp;
	struct mount *mp = hmp->mp;
	hammer_volume_t volume;
	int error;

	if (mp->mnt_flag & MNT_RDONLY) {
		kprintf("Cannot add volume to read-only HAMMER filesystem\n");
		return (EINVAL);
	}

	if (hmp->nvolumes + 1 >= HAMMER_MAX_VOLUMES) {
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
	       RB_LOOKUP(hammer_vol_rb_tree, &hmp->rb_vols_root, free_vol_no)) {
		++free_vol_no;
	}
	if (free_vol_no >= HAMMER_MAX_VOLUMES) {
		kprintf("Max number of HAMMER volumes exceeded\n");
		hammer_unlock(&hmp->volume_lock);
		return (EINVAL);
	}

	struct vnode *devvp = NULL;
	error = hammer_setup_device(&devvp, ioc->device_name, 0);
	if (error)
		goto end;
	KKASSERT(devvp);
	error = hammer_format_volume_header(
		hmp,
		devvp,
		hmp->rootvol->ondisk->vol_name,
		free_vol_no,
		hmp->nvolumes+1,
		ioc->vol_size,
		ioc->boot_area_size,
		ioc->mem_area_size);
	hammer_close_device(&devvp, 0);
	if (error)
		goto end;

	error = hammer_install_volume(hmp, ioc->device_name, NULL);
	if (error)
		goto end;

	hammer_sync_lock_sh(trans);
	hammer_lock_ex(&hmp->blkmap_lock);

	++hmp->nvolumes;

	/*
	 * Set each volumes new value of the vol_count field.
	 */
	for (int vol_no = 0; vol_no < HAMMER_MAX_VOLUMES; ++vol_no) {
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

	volume = hammer_get_volume(hmp, free_vol_no, &error);
	KKASSERT(volume != NULL && error == 0);

	struct bigblock_stat stat;
	error =	hammer_format_freemap(trans, volume, &stat);
	KKASSERT(error == 0);

	/*
	 * Increase the total number of bigblocks and update stat/vstat totals.
	 */
	hammer_modify_volume_field(trans, trans->rootvol,
		vol0_stat_bigblocks);
	trans->rootvol->ondisk->vol0_stat_bigblocks += stat.total_bigblocks;
	hammer_modify_volume_done(trans->rootvol);
	/*
	 * Bigblock count changed so recompute the total number of blocks.
	 */
	mp->mnt_stat.f_blocks = trans->rootvol->ondisk->vol0_stat_bigblocks *
	    (HAMMER_LARGEBLOCK_SIZE / HAMMER_BUFSIZE);
	mp->mnt_vstat.f_blocks = trans->rootvol->ondisk->vol0_stat_bigblocks *
	    (HAMMER_LARGEBLOCK_SIZE / HAMMER_BUFSIZE);

	/*
	 * Increase the number of free bigblocks
	 * (including the copy in hmp)
	 */
	hammer_modify_volume_field(trans, trans->rootvol,
		vol0_stat_freebigblocks);
	trans->rootvol->ondisk->vol0_stat_freebigblocks += stat.total_free_bigblocks;
	hmp->copy_stat_freebigblocks =
		trans->rootvol->ondisk->vol0_stat_freebigblocks;
	hammer_modify_volume_done(trans->rootvol);

	hammer_rel_volume(volume, 0);

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
	hammer_volume_t volume;
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
	for (int vol_no = 0; vol_no < HAMMER_MAX_VOLUMES; ++vol_no) {
		volume = hammer_get_volume(hmp, vol_no, &error);
		if (volume == NULL && error == ENOENT) {
			/*
			 * Skip unused volume numbers
			 */
			error = 0;
			continue;
		}
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
	 *
	 */

	hmp->volume_to_remove = volume->vol_no;

	struct hammer_ioc_reblock reblock;
	bzero(&reblock, sizeof(reblock));

	reblock.key_beg.localization = HAMMER_MIN_LOCALIZATION;
	reblock.key_beg.obj_id = HAMMER_MIN_OBJID;
	reblock.key_end.localization = HAMMER_MAX_LOCALIZATION;
	reblock.key_end.obj_id = HAMMER_MAX_OBJID;
	reblock.head.flags = HAMMER_IOC_DO_FLAGS;
	reblock.free_level = 0;

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
		hmp->volume_to_remove = -1;
		hammer_rel_volume(volume, 0);
		goto end;
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
	kprintf("\n");

	hammer_sync_lock_sh(trans);
	hammer_lock_ex(&hmp->blkmap_lock);

	/*
	 * We use stat later to update rootvol's bigblock stats
	 */
	struct bigblock_stat stat;
	error = hammer_free_freemap(trans, volume, &stat);
	if (error) {
		kprintf("Failed to free volume. Volume not empty!\n");
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

	error = hammer_unload_volume(volume, NULL);
	if (error == -1) {
		kprintf("Failed to unload volume\n");
		hammer_unlock(&hmp->blkmap_lock);
		hammer_sync_unlock(trans);
		goto end;
	}

	volume = NULL;
	--hmp->nvolumes;

	/*
	 * Set each volume's new value of the vol_count field.
	 */
	for (int vol_no = 0; vol_no < HAMMER_MAX_VOLUMES; ++vol_no) {
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
	 * Update the total number of bigblocks
	 */
	hammer_modify_volume_field(trans, trans->rootvol,
		vol0_stat_bigblocks);
	trans->rootvol->ondisk->vol0_stat_bigblocks -= stat.total_bigblocks;
	hammer_modify_volume_done(trans->rootvol);

	/*
	 * Update the number of free bigblocks
	 * (including the copy in hmp)
	 */
	hammer_modify_volume_field(trans, trans->rootvol,
		vol0_stat_freebigblocks);
	trans->rootvol->ondisk->vol0_stat_freebigblocks -= stat.total_free_bigblocks;
	hmp->copy_stat_freebigblocks =
		trans->rootvol->ondisk->vol0_stat_freebigblocks;
	hammer_modify_volume_done(trans->rootvol);
	/*
	 * Bigblock count changed so recompute the total number of blocks.
	 */
	mp->mnt_stat.f_blocks = trans->rootvol->ondisk->vol0_stat_bigblocks *
	    (HAMMER_LARGEBLOCK_SIZE / HAMMER_BUFSIZE);
	mp->mnt_vstat.f_blocks = trans->rootvol->ondisk->vol0_stat_bigblocks *
	    (HAMMER_LARGEBLOCK_SIZE / HAMMER_BUFSIZE);

	hammer_unlock(&hmp->blkmap_lock);
	hammer_sync_unlock(trans);

	/*
	 * Erase the volume header of the removed device.
	 *
	 * This is to not accidentally mount the volume again.
	 */
	struct vnode *devvp = NULL;
	error = hammer_setup_device(&devvp, ioc->device_name, 0);
	if (error) {
		kprintf("Failed to open device: %s\n", ioc->device_name);
		goto end;
	}
	KKASSERT(devvp);
	error = hammer_clear_volume_header(devvp);
	if (error) {
		kprintf("Failed to clear volume header of device: %s\n",
			ioc->device_name);
		goto end;
	}
	hammer_close_device(&devvp, 0);

	KKASSERT(error == 0);
end:
	hammer_unlock(&hmp->volume_lock);
	return (error);
}


int
hammer_ioc_volume_list(hammer_transaction_t trans, hammer_inode_t ip,
    struct hammer_ioc_volume_list *ioc)
{
	struct hammer_mount *hmp = trans->hmp;
	hammer_volume_t volume;
	int error = 0;
	int i, cnt, len;

	for (i = 0, cnt = 0; i < HAMMER_MAX_VOLUMES && cnt < ioc->nvols; i++) {
		volume = hammer_get_volume(hmp, i, &error);
		if (volume == NULL && error == ENOENT) {
			error = 0;
			continue;
		}
		KKASSERT(volume != NULL && error == 0);

		len = strlen(volume->vol_name) + 1;
		KKASSERT(len <= MAXPATHLEN);

		error = copyout(volume->vol_name, ioc->vols[cnt].device_name,
				len);
		if (error) {
			hammer_rel_volume(volume, 0);
			return (error);
		}
		cnt++;
		hammer_rel_volume(volume, 0);
	}
	ioc->nvols = cnt;

	return (error);
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
	hammer_buffer_t buffer = NULL;
	int error = 0;

	hammer_off_t phys_off;
	hammer_off_t block_off;
	hammer_off_t layer1_off;
	hammer_off_t layer2_off;
	hammer_off_t aligned_buf_end_off;
	struct hammer_blockmap_layer1 *layer1;
	struct hammer_blockmap_layer2 *layer2;

	/*
	 * Calculate the usable size of the volume, which
	 * must be aligned at a bigblock (8 MB) boundary.
	 */
	aligned_buf_end_off = (HAMMER_ENCODE_RAW_BUFFER(volume->ondisk->vol_no,
		(volume->ondisk->vol_buf_end - volume->ondisk->vol_buf_beg)
		& ~HAMMER_LARGEBLOCK_MASK64));

	/*
	 * Iterate the volume's address space in chunks of 4 TB, where each
	 * chunk consists of at least one physically available 8 MB bigblock.
	 *
	 * For each chunk we need one L1 entry and one L2 bigblock.
	 * We use the first bigblock of each chunk as L2 block.
	 */
	for (phys_off = HAMMER_ENCODE_RAW_BUFFER(volume->ondisk->vol_no, 0);
	     phys_off < aligned_buf_end_off;
	     phys_off += HAMMER_BLOCKMAP_LAYER2) {
		for (block_off = 0;
		     block_off < HAMMER_BLOCKMAP_LAYER2;
		     block_off += HAMMER_LARGEBLOCK_SIZE) {
			layer2_off = phys_off +
				HAMMER_BLOCKMAP_LAYER2_OFFSET(block_off);
			layer2 = hammer_bread(hmp, layer2_off, &error, &buffer);
			if (error)
				goto end;

			error = callback(trans, volume, &buffer, NULL,
					 layer2, phys_off, block_off, data);
			if (error)
				goto end;
		}

		layer1_off = freemap->phys_offset +
				HAMMER_BLOCKMAP_LAYER1_OFFSET(phys_off);
		layer1 = hammer_bread(hmp, layer1_off, &error, &buffer);
		if (error)
			goto end;

		error = callback(trans, volume, &buffer, layer1, NULL,
				 phys_off, 0, data);
		if (error)
			goto end;
	}

end:
	if (buffer) {
		hammer_rel_buffer(buffer, 0);
		buffer = NULL;
	}

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
	 * at a bigblock (8 MB) boundary.
	 */
	hammer_off_t aligned_buf_end_off;
	aligned_buf_end_off = (HAMMER_ENCODE_RAW_BUFFER(volume->ondisk->vol_no,
		(volume->ondisk->vol_buf_end - volume->ondisk->vol_buf_beg)
		& ~HAMMER_LARGEBLOCK_MASK64));

	if (layer1) {
		KKASSERT(layer1->phys_offset == HAMMER_BLOCKMAP_UNAVAIL);

		hammer_modify_buffer(trans, *bufferp, layer1, sizeof(*layer1));
		bzero(layer1, sizeof(*layer1));
		layer1->phys_offset = phys_off;
		layer1->blocks_free = stat->counter;
		layer1->layer1_crc = crc32(layer1, HAMMER_LAYER1_CRCSIZE);
		hammer_modify_buffer_done(*bufferp);

		stat->total_free_bigblocks += stat->counter;
		stat->counter = 0; /* reset */
	} else if (layer2) {
		hammer_modify_buffer(trans, *bufferp, layer2, sizeof(*layer2));
		bzero(layer2, sizeof(*layer2));

		if (block_off == 0) {
			/*
			 * The first entry represents the L2 bigblock itself.
			 */
			layer2->zone = HAMMER_ZONE_FREEMAP_INDEX;
			layer2->append_off = HAMMER_LARGEBLOCK_SIZE;
			layer2->bytes_free = 0;
			++stat->total_bigblocks;
		} else if (phys_off + block_off < aligned_buf_end_off) {
			/*
			 * Available bigblock
			 */
			layer2->zone = 0;
			layer2->append_off = 0;
			layer2->bytes_free = HAMMER_LARGEBLOCK_SIZE;
			++stat->total_bigblocks;
			++stat->counter;
		} else {
			/*
			 * Bigblock outside of physically available
			 * space
			 */
			layer2->zone = HAMMER_ZONE_UNAVAIL_INDEX;
			layer2->append_off = HAMMER_LARGEBLOCK_SIZE;
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

	/*
	 * No modifications to ondisk structures
	 */
	int testonly = (stat == NULL);

	if (layer1) {
		if (layer1->phys_offset == HAMMER_BLOCKMAP_UNAVAIL) {
			/*
			 * This layer1 entry is already free.
			 */
			return 0;
		}

		KKASSERT((int)HAMMER_VOL_DECODE(layer1->phys_offset) ==
			trans->hmp->volume_to_remove);

		if (testonly)
			return 0;

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
			if (stat) {
				++stat->total_bigblocks;
			}
			return 0;
		}

		if (layer2->append_off == 0 &&
		    layer2->bytes_free == HAMMER_LARGEBLOCK_SIZE) {
			if (stat) {
				++stat->total_bigblocks;
				++stat->total_free_bigblocks;
			}
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

static int
hammer_free_freemap(hammer_transaction_t trans, hammer_volume_t volume,
	struct bigblock_stat *stat)
{
	int error;

	stat->total_bigblocks = 0;
	stat->total_free_bigblocks = 0;
	stat->counter = 0;

	error = hammer_iterate_l1l2_entries(trans, volume, free_callback, NULL);
	if (error)
		return error;

	error = hammer_iterate_l1l2_entries(trans, volume, free_callback, stat);
	return error;
}

/************************************************************************
 *				MISC					*
 ************************************************************************
 */

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
	if (*devvpp) {
		vinvalbuf(*devvpp, ronly ? 0 : V_SAVE, 0, 0);
		VOP_CLOSE(*devvpp, (ronly ? FREAD : FREAD|FWRITE));
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
		kprintf("hammer_volume_add: Formatting of valid HAMMER volume "
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

/*
 * Invalidates the volume header. Used by volume-del.
 */
static int
hammer_clear_volume_header(struct vnode *devvp)
{
	struct buf *bp = NULL;
	struct hammer_volume_ondisk *ondisk;
	int error;

	KKASSERT(HAMMER_BUFSIZE >= sizeof(struct hammer_volume_ondisk));
	error = bread(devvp, 0LL, HAMMER_BUFSIZE, &bp);
	if (error || bp->b_bcount < sizeof(struct hammer_volume_ondisk))
		goto late_failure;

	ondisk = (struct hammer_volume_ondisk*) bp->b_data;
	bzero(ondisk, sizeof(struct hammer_volume_ondisk));

	error = bwrite(bp);
	bp = NULL;

late_failure:
	if (bp)
		brelse(bp);
	return (error);
}
