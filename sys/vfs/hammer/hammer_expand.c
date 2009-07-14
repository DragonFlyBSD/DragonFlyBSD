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
hammer_format_volume_header(struct hammer_mount *hmp, const char *vol_name,
	int vol_no, int vol_count,
	int64_t vol_size, int64_t boot_area_size, int64_t mem_area_size);


int
hammer_ioc_expand(hammer_transaction_t trans, hammer_inode_t ip,
		struct hammer_ioc_expand *expand)
{
	struct hammer_mount *hmp = trans->hmp;
	struct mount *mp = hmp->mp;
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

	error = hammer_format_volume_header(
		hmp,
		hmp->rootvol->ondisk->vol_name,
		free_vol_no,
		hmp->nvolumes+1,
		expand->vol_size,
		expand->boot_area_size,
		expand->mem_area_size);

	if (error == 0) {
		error = hammer_install_volume(hmp, expand->device_name, NULL);
	}

	if (error == 0) {
		++hmp->nvolumes;
		hammer_sync_lock_sh(trans);
		hammer_lock_ex(&hmp->blkmap_lock);

		/*
		 * Set each volumes new value of the vol_count field.
		 */
		for (int vol_no = 0; vol_no < HAMMER_MAX_VOLUMES; ++vol_no) {
			hammer_volume_t volume;
			volume = hammer_get_volume(hmp, vol_no, &error);
			if (volume == NULL && error == ENOENT) {
				/*
				 * Skip unused volume numbers
				 */
				continue;
			}
			KKASSERT(error == 0);
			hammer_modify_volume_field(trans, volume, vol_count);
			volume->ondisk->vol_count = hmp->nvolumes;
			hammer_modify_volume_done(volume);
			hammer_rel_volume(volume, 0);
		}

		hammer_unlock(&hmp->blkmap_lock);
		hammer_sync_unlock(trans);
	}

	if (error) {
		kprintf("An error occured: %d\n", error);
	}

	return (error);
}

static int
hammer_format_volume_header(struct hammer_mount *hmp, const char *vol_name,
	int vol_no, int vol_count,
	int64_t vol_size, int64_t boot_area_size, int64_t mem_area_size)
{
	struct vnode *devvp = NULL;
	struct buf *bp = NULL;
	struct nlookupdata nd;
	struct hammer_volume_ondisk *ondisk;
	int error;

	/*
	 * Get the device vnode
	 */
	error = nlookup_init(&nd, vol_name, UIO_SYSSPACE, NLC_FOLLOW);
	if (error == 0)
		error = nlookup(&nd);
	if (error == 0)
		error = cache_vref(&nd.nl_nch, nd.nl_cred, &devvp);
	nlookup_done(&nd);

	if (error == 0) {
		if (vn_isdisk(devvp, &error)) {
			error = vfs_mountedon(devvp);
		}
	}
	if (error == 0 &&
	    count_udev(devvp->v_umajor, devvp->v_uminor) > 0) {
		error = EBUSY;
	}
	if (error == 0) {
		vn_lock(devvp, LK_EXCLUSIVE | LK_RETRY);
		error = vinvalbuf(devvp, V_SAVE, 0, 0);
		if (error == 0) {
			error = VOP_OPEN(devvp, FREAD|FWRITE, FSCRED, NULL);
		}
		vn_unlock(devvp);
	}
	if (error) {
		if (devvp)
			vrele(devvp);
		return (error);
	}

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
	VOP_CLOSE(devvp, FREAD|FWRITE);
	if (devvp)
		vrele(devvp);
	return (error);
}
