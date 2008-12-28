/*
 * Copyright (c) 2007-2008 The DragonFly Project.  All rights reserved.
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
 * $DragonFly: src/sys/vfs/hammer/hammer_ondisk.c,v 1.76 2008/08/29 20:19:08 dillon Exp $
 */
/*
 * Manage HAMMER's on-disk structures.  These routines are primarily
 * responsible for interfacing with the kernel's I/O subsystem and for
 * managing in-memory structures.
 */

#include "hammer.h"
#include <sys/fcntl.h>
#include <sys/nlookup.h>
#include <sys/buf.h>
#include <sys/buf2.h>

static void hammer_free_volume(hammer_volume_t volume);
static int hammer_load_volume(hammer_volume_t volume);
static int hammer_load_buffer(hammer_buffer_t buffer, int isnew);
static int hammer_load_node(hammer_node_t node, int isnew);

static int
hammer_vol_rb_compare(hammer_volume_t vol1, hammer_volume_t vol2)
{
	if (vol1->vol_no < vol2->vol_no)
		return(-1);
	if (vol1->vol_no > vol2->vol_no)
		return(1);
	return(0);
}

static int
hammer_buf_rb_compare(hammer_buffer_t buf1, hammer_buffer_t buf2)
{
	if (buf1->zoneX_offset < buf2->zoneX_offset)
		return(-1);
	if (buf1->zoneX_offset > buf2->zoneX_offset)
		return(1);
	return(0);
}

static int
hammer_nod_rb_compare(hammer_node_t node1, hammer_node_t node2)
{
	if (node1->node_offset < node2->node_offset)
		return(-1);
	if (node1->node_offset > node2->node_offset)
		return(1);
	return(0);
}

RB_GENERATE2(hammer_vol_rb_tree, hammer_volume, rb_node,
	     hammer_vol_rb_compare, int32_t, vol_no);
RB_GENERATE2(hammer_buf_rb_tree, hammer_buffer, rb_node,
	     hammer_buf_rb_compare, hammer_off_t, zoneX_offset);
RB_GENERATE2(hammer_nod_rb_tree, hammer_node, rb_node,
	     hammer_nod_rb_compare, hammer_off_t, node_offset);

/************************************************************************
 *				VOLUMES					*
 ************************************************************************
 *
 * Load a HAMMER volume by name.  Returns 0 on success or a positive error
 * code on failure.  Volumes must be loaded at mount time, get_volume() will
 * not load a new volume.
 *
 * Calls made to hammer_load_volume() or single-threaded
 */
int
hammer_install_volume(struct hammer_mount *hmp, const char *volname,
		      struct vnode *devvp)
{
	struct mount *mp;
	hammer_volume_t volume;
	struct hammer_volume_ondisk *ondisk;
	struct nlookupdata nd;
	struct buf *bp = NULL;
	int error;
	int ronly;
	int setmp = 0;

	mp = hmp->mp;
	ronly = ((mp->mnt_flag & MNT_RDONLY) ? 1 : 0);

	/*
	 * Allocate a volume structure
	 */
	++hammer_count_volumes;
	volume = kmalloc(sizeof(*volume), hmp->m_misc, M_WAITOK|M_ZERO);
	volume->vol_name = kstrdup(volname, hmp->m_misc);
	volume->io.hmp = hmp;	/* bootstrap */
	hammer_io_init(&volume->io, volume, HAMMER_STRUCTURE_VOLUME);
	volume->io.offset = 0LL;
	volume->io.bytes = HAMMER_BUFSIZE;

	/*
	 * Get the device vnode
	 */
	if (devvp == NULL) {
		error = nlookup_init(&nd, volume->vol_name, UIO_SYSSPACE, NLC_FOLLOW);
		if (error == 0)
			error = nlookup(&nd);
		if (error == 0)
			error = cache_vref(&nd.nl_nch, nd.nl_cred, &volume->devvp);
		nlookup_done(&nd);
	} else {
		error = 0;
		volume->devvp = devvp;
	}

	if (error == 0) {
		if (vn_isdisk(volume->devvp, &error)) {
			error = vfs_mountedon(volume->devvp);
		}
	}
	if (error == 0 &&
	    count_udev(volume->devvp->v_umajor, volume->devvp->v_uminor) > 0) {
		error = EBUSY;
	}
	if (error == 0) {
		vn_lock(volume->devvp, LK_EXCLUSIVE | LK_RETRY);
		error = vinvalbuf(volume->devvp, V_SAVE, 0, 0);
		if (error == 0) {
			error = VOP_OPEN(volume->devvp, 
					 (ronly ? FREAD : FREAD|FWRITE),
					 FSCRED, NULL);
		}
		vn_unlock(volume->devvp);
	}
	if (error) {
		hammer_free_volume(volume);
		return(error);
	}
	volume->devvp->v_rdev->si_mountpoint = mp;
	setmp = 1;

	/*
	 * Extract the volume number from the volume header and do various
	 * sanity checks.
	 */
	error = bread(volume->devvp, 0LL, HAMMER_BUFSIZE, &bp);
	if (error)
		goto late_failure;
	ondisk = (void *)bp->b_data;
	if (ondisk->vol_signature != HAMMER_FSBUF_VOLUME) {
		kprintf("hammer_mount: volume %s has an invalid header\n",
			volume->vol_name);
		error = EFTYPE;
		goto late_failure;
	}
	volume->vol_no = ondisk->vol_no;
	volume->buffer_base = ondisk->vol_buf_beg;
	volume->vol_flags = ondisk->vol_flags;
	volume->nblocks = ondisk->vol_nblocks; 
	volume->maxbuf_off = HAMMER_ENCODE_RAW_BUFFER(volume->vol_no,
				    ondisk->vol_buf_end - ondisk->vol_buf_beg);
	volume->maxraw_off = ondisk->vol_buf_end;

	if (RB_EMPTY(&hmp->rb_vols_root)) {
		hmp->fsid = ondisk->vol_fsid;
	} else if (bcmp(&hmp->fsid, &ondisk->vol_fsid, sizeof(uuid_t))) {
		kprintf("hammer_mount: volume %s's fsid does not match "
			"other volumes\n", volume->vol_name);
		error = EFTYPE;
		goto late_failure;
	}

	/*
	 * Insert the volume structure into the red-black tree.
	 */
	if (RB_INSERT(hammer_vol_rb_tree, &hmp->rb_vols_root, volume)) {
		kprintf("hammer_mount: volume %s has a duplicate vol_no %d\n",
			volume->vol_name, volume->vol_no);
		error = EEXIST;
	}

	/*
	 * Set the root volume .  HAMMER special cases rootvol the structure.
	 * We do not hold a ref because this would prevent related I/O
	 * from being flushed.
	 */
	if (error == 0 && ondisk->vol_rootvol == ondisk->vol_no) {
		hmp->rootvol = volume;
		hmp->nvolumes = ondisk->vol_count;
		if (bp) {
			brelse(bp);
			bp = NULL;
		}
		hmp->mp->mnt_stat.f_blocks += ondisk->vol0_stat_bigblocks *
			(HAMMER_LARGEBLOCK_SIZE / HAMMER_BUFSIZE);
		hmp->mp->mnt_vstat.f_blocks += ondisk->vol0_stat_bigblocks *
			(HAMMER_LARGEBLOCK_SIZE / HAMMER_BUFSIZE);
	}
late_failure:
	if (bp)
		brelse(bp);
	if (error) {
		/*vinvalbuf(volume->devvp, V_SAVE, 0, 0);*/
		if (setmp)
			volume->devvp->v_rdev->si_mountpoint = NULL;
		VOP_CLOSE(volume->devvp, ronly ? FREAD : FREAD|FWRITE);
		hammer_free_volume(volume);
	}
	return (error);
}

/*
 * This is called for each volume when updating the mount point from
 * read-write to read-only or vise-versa.
 */
int
hammer_adjust_volume_mode(hammer_volume_t volume, void *data __unused)
{
	if (volume->devvp) {
		vn_lock(volume->devvp, LK_EXCLUSIVE | LK_RETRY);
		if (volume->io.hmp->ronly) {
			/* do not call vinvalbuf */
			VOP_OPEN(volume->devvp, FREAD, FSCRED, NULL);
			VOP_CLOSE(volume->devvp, FREAD|FWRITE);
		} else {
			/* do not call vinvalbuf */
			VOP_OPEN(volume->devvp, FREAD|FWRITE, FSCRED, NULL);
			VOP_CLOSE(volume->devvp, FREAD);
		}
		vn_unlock(volume->devvp);
	}
	return(0);
}

/*
 * Unload and free a HAMMER volume.  Must return >= 0 to continue scan
 * so returns -1 on failure.
 */
int
hammer_unload_volume(hammer_volume_t volume, void *data __unused)
{
	hammer_mount_t hmp = volume->io.hmp;
	int ronly = ((hmp->mp->mnt_flag & MNT_RDONLY) ? 1 : 0);
	struct buf *bp;

	/*
	 * Clean up the root volume pointer, which is held unlocked in hmp.
	 */
	if (hmp->rootvol == volume)
		hmp->rootvol = NULL;

	/*
	 * We must not flush a dirty buffer to disk on umount.  It should
	 * have already been dealt with by the flusher, or we may be in
	 * catastrophic failure.
	 */
	hammer_io_clear_modify(&volume->io, 1);
	volume->io.waitdep = 1;
	bp = hammer_io_release(&volume->io, 1);

	/*
	 * Clean up the persistent ref ioerror might have on the volume
	 */
	if (volume->io.ioerror) {
		volume->io.ioerror = 0;
		hammer_unref(&volume->io.lock);
	}

	/*
	 * There should be no references on the volume, no clusters, and
	 * no super-clusters.
	 */
	KKASSERT(volume->io.lock.refs == 0);
	if (bp)
		brelse(bp);

	volume->ondisk = NULL;
	if (volume->devvp) {
		if (volume->devvp->v_rdev &&
		    volume->devvp->v_rdev->si_mountpoint == hmp->mp
		) {
			volume->devvp->v_rdev->si_mountpoint = NULL;
		}
		if (ronly) {
			/*
			 * Make sure we don't sync anything to disk if we
			 * are in read-only mode (1) or critically-errored
			 * (2).  Note that there may be dirty buffers in
			 * normal read-only mode from crash recovery.
			 */
			vinvalbuf(volume->devvp, 0, 0, 0);
			VOP_CLOSE(volume->devvp, FREAD);
		} else {
			/*
			 * Normal termination, save any dirty buffers
			 * (XXX there really shouldn't be any).
			 */
			vinvalbuf(volume->devvp, V_SAVE, 0, 0);
			VOP_CLOSE(volume->devvp, FREAD|FWRITE);
		}
	}

	/*
	 * Destroy the structure
	 */
	RB_REMOVE(hammer_vol_rb_tree, &hmp->rb_vols_root, volume);
	hammer_free_volume(volume);
	return(0);
}

static
void
hammer_free_volume(hammer_volume_t volume)
{
	hammer_mount_t hmp = volume->io.hmp;

	if (volume->vol_name) {
		kfree(volume->vol_name, hmp->m_misc);
		volume->vol_name = NULL;
	}
	if (volume->devvp) {
		vrele(volume->devvp);
		volume->devvp = NULL;
	}
	--hammer_count_volumes;
	kfree(volume, hmp->m_misc);
}

/*
 * Get a HAMMER volume.  The volume must already exist.
 */
hammer_volume_t
hammer_get_volume(struct hammer_mount *hmp, int32_t vol_no, int *errorp)
{
	struct hammer_volume *volume;

	/*
	 * Locate the volume structure
	 */
	volume = RB_LOOKUP(hammer_vol_rb_tree, &hmp->rb_vols_root, vol_no);
	if (volume == NULL) {
		*errorp = ENOENT;
		return(NULL);
	}
	hammer_ref(&volume->io.lock);

	/*
	 * Deal with on-disk info
	 */
	if (volume->ondisk == NULL || volume->io.loading) {
		*errorp = hammer_load_volume(volume);
		if (*errorp) {
			hammer_rel_volume(volume, 1);
			volume = NULL;
		}
	} else {
		*errorp = 0;
	}
	return(volume);
}

int
hammer_ref_volume(hammer_volume_t volume)
{
	int error;

	hammer_ref(&volume->io.lock);

	/*
	 * Deal with on-disk info
	 */
	if (volume->ondisk == NULL || volume->io.loading) {
		error = hammer_load_volume(volume);
		if (error)
			hammer_rel_volume(volume, 1);
	} else {
		error = 0;
	}
	return (error);
}

hammer_volume_t
hammer_get_root_volume(struct hammer_mount *hmp, int *errorp)
{
	hammer_volume_t volume;

	volume = hmp->rootvol;
	KKASSERT(volume != NULL);
	hammer_ref(&volume->io.lock);

	/*
	 * Deal with on-disk info
	 */
	if (volume->ondisk == NULL || volume->io.loading) {
		*errorp = hammer_load_volume(volume);
		if (*errorp) {
			hammer_rel_volume(volume, 1);
			volume = NULL;
		}
	} else {
		*errorp = 0;
	}
	return (volume);
}

/*
 * Load a volume's on-disk information.  The volume must be referenced and
 * not locked.  We temporarily acquire an exclusive lock to interlock
 * against releases or multiple get's.
 */
static int
hammer_load_volume(hammer_volume_t volume)
{
	int error;

	++volume->io.loading;
	hammer_lock_ex(&volume->io.lock);

	if (volume->ondisk == NULL) {
		error = hammer_io_read(volume->devvp, &volume->io,
				       volume->maxraw_off);
		if (error == 0)
			volume->ondisk = (void *)volume->io.bp->b_data;
	} else {
		error = 0;
	}
	--volume->io.loading;
	hammer_unlock(&volume->io.lock);
	return(error);
}

/*
 * Release a volume.  Call hammer_io_release on the last reference.  We have
 * to acquire an exclusive lock to interlock against volume->ondisk tests
 * in hammer_load_volume(), and hammer_io_release() also expects an exclusive
 * lock to be held.
 *
 * Volumes are not unloaded from memory during normal operation.
 */
void
hammer_rel_volume(hammer_volume_t volume, int flush)
{
	struct buf *bp = NULL;

	crit_enter();
	if (volume->io.lock.refs == 1) {
		++volume->io.loading;
		hammer_lock_ex(&volume->io.lock);
		if (volume->io.lock.refs == 1) {
			volume->ondisk = NULL;
			bp = hammer_io_release(&volume->io, flush);
		}
		--volume->io.loading;
		hammer_unlock(&volume->io.lock);
	}
	hammer_unref(&volume->io.lock);
	if (bp)
		brelse(bp);
	crit_exit();
}

int
hammer_mountcheck_volumes(struct hammer_mount *hmp)
{
	hammer_volume_t vol;
	int i;

	for (i = 0; i < hmp->nvolumes; ++i) {
		vol = RB_LOOKUP(hammer_vol_rb_tree, &hmp->rb_vols_root, i);
		if (vol == NULL)
			return(EINVAL);
	}
	return(0);
}

/************************************************************************
 *				BUFFERS					*
 ************************************************************************
 *
 * Manage buffers.  Currently all blockmap-backed zones are translated
 * to zone-2 buffer offsets.
 */
hammer_buffer_t
hammer_get_buffer(hammer_mount_t hmp, hammer_off_t buf_offset,
		  int bytes, int isnew, int *errorp)
{
	hammer_buffer_t buffer;
	hammer_volume_t volume;
	hammer_off_t	zone2_offset;
	hammer_io_type_t iotype;
	int vol_no;
	int zone;

	buf_offset &= ~HAMMER_BUFMASK64;
again:
	/*
	 * Shortcut if the buffer is already cached
	 */
	buffer = RB_LOOKUP(hammer_buf_rb_tree, &hmp->rb_bufs_root, buf_offset);
	if (buffer) {
		if (buffer->io.lock.refs == 0)
			++hammer_count_refedbufs;
		hammer_ref(&buffer->io.lock);

		/*
		 * Once refed the ondisk field will not be cleared by
		 * any other action.
		 */
		if (buffer->ondisk && buffer->io.loading == 0) {
			*errorp = 0;
			return(buffer);
		}

		/*
		 * The buffer is no longer loose if it has a ref, and
		 * cannot become loose once it gains a ref.  Loose
		 * buffers will never be in a modified state.  This should
		 * only occur on the 0->1 transition of refs.
		 *
		 * lose_list can be modified via a biodone() interrupt.
		 */
		if (buffer->io.mod_list == &hmp->lose_list) {
			crit_enter();	/* biodone race against list */
			TAILQ_REMOVE(buffer->io.mod_list, &buffer->io,
				     mod_entry);
			crit_exit();
			buffer->io.mod_list = NULL;
			KKASSERT(buffer->io.modified == 0);
		}
		goto found;
	}

	/*
	 * What is the buffer class?
	 */
	zone = HAMMER_ZONE_DECODE(buf_offset);

	switch(zone) {
	case HAMMER_ZONE_LARGE_DATA_INDEX:
	case HAMMER_ZONE_SMALL_DATA_INDEX:
		iotype = HAMMER_STRUCTURE_DATA_BUFFER;
		break;
	case HAMMER_ZONE_UNDO_INDEX:
		iotype = HAMMER_STRUCTURE_UNDO_BUFFER;
		break;
	case HAMMER_ZONE_META_INDEX:
	default:
		/*
		 * NOTE: inode data and directory entries are placed in this
		 * zone.  inode atime/mtime is updated in-place and thus
		 * buffers containing inodes must be synchronized as
		 * meta-buffers, same as buffers containing B-Tree info.
		 */
		iotype = HAMMER_STRUCTURE_META_BUFFER;
		break;
	}

	/*
	 * Handle blockmap offset translations
	 */
	if (zone >= HAMMER_ZONE_BTREE_INDEX) {
		zone2_offset = hammer_blockmap_lookup(hmp, buf_offset, errorp);
	} else if (zone == HAMMER_ZONE_UNDO_INDEX) {
		zone2_offset = hammer_undo_lookup(hmp, buf_offset, errorp);
	} else {
		KKASSERT(zone == HAMMER_ZONE_RAW_BUFFER_INDEX);
		zone2_offset = buf_offset;
		*errorp = 0;
	}
	if (*errorp)
		return(NULL);

	/*
	 * NOTE: zone2_offset and maxbuf_off are both full zone-2 offset
	 * specifications.
	 */
	KKASSERT((zone2_offset & HAMMER_OFF_ZONE_MASK) ==
		 HAMMER_ZONE_RAW_BUFFER);
	vol_no = HAMMER_VOL_DECODE(zone2_offset);
	volume = hammer_get_volume(hmp, vol_no, errorp);
	if (volume == NULL)
		return(NULL);

	KKASSERT(zone2_offset < volume->maxbuf_off);

	/*
	 * Allocate a new buffer structure.  We will check for races later.
	 */
	++hammer_count_buffers;
	buffer = kmalloc(sizeof(*buffer), hmp->m_misc,
			 M_WAITOK|M_ZERO|M_USE_RESERVE);
	buffer->zone2_offset = zone2_offset;
	buffer->zoneX_offset = buf_offset;

	hammer_io_init(&buffer->io, volume, iotype);
	buffer->io.offset = volume->ondisk->vol_buf_beg +
			    (zone2_offset & HAMMER_OFF_SHORT_MASK);
	buffer->io.bytes = bytes;
	TAILQ_INIT(&buffer->clist);
	hammer_ref(&buffer->io.lock);

	/*
	 * Insert the buffer into the RB tree and handle late collisions.
	 */
	if (RB_INSERT(hammer_buf_rb_tree, &hmp->rb_bufs_root, buffer)) {
		hammer_unref(&buffer->io.lock);
		--hammer_count_buffers;
		kfree(buffer, hmp->m_misc);
		goto again;
	}
	++hammer_count_refedbufs;
found:

	/*
	 * Deal with on-disk info and loading races.
	 */
	if (buffer->ondisk == NULL || buffer->io.loading) {
		*errorp = hammer_load_buffer(buffer, isnew);
		if (*errorp) {
			hammer_rel_buffer(buffer, 1);
			buffer = NULL;
		}
	} else {
		*errorp = 0;
	}
	return(buffer);
}

/*
 * This is used by the direct-read code to deal with large-data buffers
 * created by the reblocker and mirror-write code.  The direct-read code
 * bypasses the HAMMER buffer subsystem and so any aliased dirty or write-
 * running hammer buffers must be fully synced to disk before we can issue
 * the direct-read.
 *
 * This code path is not considered critical as only the rebocker and
 * mirror-write code will create large-data buffers via the HAMMER buffer
 * subsystem.  They do that because they operate at the B-Tree level and
 * do not access the vnode/inode structures.
 */
void
hammer_sync_buffers(hammer_mount_t hmp, hammer_off_t base_offset, int bytes)
{
	hammer_buffer_t buffer;
	int error;

	KKASSERT((base_offset & HAMMER_OFF_ZONE_MASK) ==
		 HAMMER_ZONE_LARGE_DATA);

	while (bytes > 0) {
		buffer = RB_LOOKUP(hammer_buf_rb_tree, &hmp->rb_bufs_root,
				   base_offset);
		if (buffer && (buffer->io.modified || buffer->io.running)) {
			error = hammer_ref_buffer(buffer);
			if (error == 0) {
				hammer_io_wait(&buffer->io);
				if (buffer->io.modified) {
					hammer_io_write_interlock(&buffer->io);
					hammer_io_flush(&buffer->io);
					hammer_io_done_interlock(&buffer->io);
					hammer_io_wait(&buffer->io);
				}
				hammer_rel_buffer(buffer, 0);
			}
		}
		base_offset += HAMMER_BUFSIZE;
		bytes -= HAMMER_BUFSIZE;
	}
}

/*
 * Destroy all buffers covering the specified zoneX offset range.  This
 * is called when the related blockmap layer2 entry is freed or when
 * a direct write bypasses our buffer/buffer-cache subsystem.
 *
 * The buffers may be referenced by the caller itself.  Setting reclaim
 * will cause the buffer to be destroyed when it's ref count reaches zero.
 */
void
hammer_del_buffers(hammer_mount_t hmp, hammer_off_t base_offset,
		   hammer_off_t zone2_offset, int bytes)
{
	hammer_buffer_t buffer;
	hammer_volume_t volume;
	int vol_no;
	int error;

	vol_no = HAMMER_VOL_DECODE(zone2_offset);
	volume = hammer_get_volume(hmp, vol_no, &error);
	KKASSERT(error == 0);

	while (bytes > 0) {
		buffer = RB_LOOKUP(hammer_buf_rb_tree, &hmp->rb_bufs_root,
				   base_offset);
		if (buffer) {
			error = hammer_ref_buffer(buffer);
			if (error == 0) {
				KKASSERT(buffer->zone2_offset == zone2_offset);
				hammer_io_clear_modify(&buffer->io, 1);
				buffer->io.reclaim = 1;
				buffer->io.waitdep = 1;
				KKASSERT(buffer->io.volume == volume);
				hammer_rel_buffer(buffer, 0);
			}
		} else {
			hammer_io_inval(volume, zone2_offset);
		}
		base_offset += HAMMER_BUFSIZE;
		zone2_offset += HAMMER_BUFSIZE;
		bytes -= HAMMER_BUFSIZE;
	}
	hammer_rel_volume(volume, 0);
}

static int
hammer_load_buffer(hammer_buffer_t buffer, int isnew)
{
	hammer_volume_t volume;
	int error;

	/*
	 * Load the buffer's on-disk info
	 */
	volume = buffer->io.volume;
	++buffer->io.loading;
	hammer_lock_ex(&buffer->io.lock);

	if (hammer_debug_io & 0x0001) {
		kprintf("load_buffer %016llx %016llx isnew=%d od=%p\n",
			buffer->zoneX_offset, buffer->zone2_offset, isnew,
			buffer->ondisk);
	}

	if (buffer->ondisk == NULL) {
		if (isnew) {
			error = hammer_io_new(volume->devvp, &buffer->io);
		} else {
			error = hammer_io_read(volume->devvp, &buffer->io,
					       volume->maxraw_off);
		}
		if (error == 0)
			buffer->ondisk = (void *)buffer->io.bp->b_data;
	} else if (isnew) {
		error = hammer_io_new(volume->devvp, &buffer->io);
	} else {
		error = 0;
	}
	--buffer->io.loading;
	hammer_unlock(&buffer->io.lock);
	return (error);
}

/*
 * NOTE: Called from RB_SCAN, must return >= 0 for scan to continue.
 * This routine is only called during unmount.
 */
int
hammer_unload_buffer(hammer_buffer_t buffer, void *data __unused)
{
	/*
	 * Clean up the persistent ref ioerror might have on the buffer
	 * and acquire a ref (steal ioerror's if we can).
	 */
	if (buffer->io.ioerror) {
		buffer->io.ioerror = 0;
	} else {
		if (buffer->io.lock.refs == 0)
			++hammer_count_refedbufs;
		hammer_ref(&buffer->io.lock);
	}

	/*
	 * We must not flush a dirty buffer to disk on umount.  It should
	 * have already been dealt with by the flusher, or we may be in
	 * catastrophic failure.
	 */
	hammer_io_clear_modify(&buffer->io, 1);
	hammer_flush_buffer_nodes(buffer);
	KKASSERT(buffer->io.lock.refs == 1);
	hammer_rel_buffer(buffer, 2);
	return(0);
}

/*
 * Reference a buffer that is either already referenced or via a specially
 * handled pointer (aka cursor->buffer).
 */
int
hammer_ref_buffer(hammer_buffer_t buffer)
{
	int error;

	if (buffer->io.lock.refs == 0)
		++hammer_count_refedbufs;
	hammer_ref(&buffer->io.lock);

	/*
	 * At this point a biodone() will not touch the buffer other then
	 * incidental bits.  However, lose_list can be modified via
	 * a biodone() interrupt.
	 *
	 * No longer loose
	 */
	if (buffer->io.mod_list == &buffer->io.hmp->lose_list) {
		crit_enter();
		TAILQ_REMOVE(buffer->io.mod_list, &buffer->io, mod_entry);
		buffer->io.mod_list = NULL;
		crit_exit();
	}

	if (buffer->ondisk == NULL || buffer->io.loading) {
		error = hammer_load_buffer(buffer, 0);
		if (error) {
			hammer_rel_buffer(buffer, 1);
			/*
			 * NOTE: buffer pointer can become stale after
			 * the above release.
			 */
		}
	} else {
		error = 0;
	}
	return(error);
}

/*
 * Release a buffer.  We have to deal with several places where
 * another thread can ref the buffer.
 *
 * Only destroy the structure itself if the related buffer cache buffer
 * was disassociated from it.  This ties the management of the structure
 * to the buffer cache subsystem.  buffer->ondisk determines whether the
 * embedded io is referenced or not.
 */
void
hammer_rel_buffer(hammer_buffer_t buffer, int flush)
{
	hammer_volume_t volume;
	hammer_mount_t hmp;
	struct buf *bp = NULL;
	int freeme = 0;

	hmp = buffer->io.hmp;

	crit_enter();
	if (buffer->io.lock.refs == 1) {
		++buffer->io.loading;	/* force interlock check */
		hammer_lock_ex(&buffer->io.lock);
		if (buffer->io.lock.refs == 1) {
			bp = hammer_io_release(&buffer->io, flush);

			if (buffer->io.lock.refs == 1)
				--hammer_count_refedbufs;

			if (buffer->io.bp == NULL &&
			    buffer->io.lock.refs == 1) {
				/*
				 * Final cleanup
				 *
				 * NOTE: It is impossible for any associated
				 * B-Tree nodes to have refs if the buffer
				 * has no additional refs.
				 */
				RB_REMOVE(hammer_buf_rb_tree,
					  &buffer->io.hmp->rb_bufs_root,
					  buffer);
				volume = buffer->io.volume;
				buffer->io.volume = NULL; /* sanity */
				hammer_rel_volume(volume, 0);
				hammer_io_clear_modlist(&buffer->io);
				hammer_flush_buffer_nodes(buffer);
				KKASSERT(TAILQ_EMPTY(&buffer->clist));
				freeme = 1;
			}
		}
		--buffer->io.loading;
		hammer_unlock(&buffer->io.lock);
	}
	hammer_unref(&buffer->io.lock);
	crit_exit();
	if (bp)
		brelse(bp);
	if (freeme) {
		--hammer_count_buffers;
		kfree(buffer, hmp->m_misc);
	}
}

/*
 * Access the filesystem buffer containing the specified hammer offset.
 * buf_offset is a conglomeration of the volume number and vol_buf_beg
 * relative buffer offset.  It must also have bit 55 set to be valid.
 * (see hammer_off_t in hammer_disk.h).
 *
 * Any prior buffer in *bufferp will be released and replaced by the
 * requested buffer.
 */
static __inline
void *
_hammer_bread(hammer_mount_t hmp, hammer_off_t buf_offset, int bytes,
	     int *errorp, struct hammer_buffer **bufferp)
{
	hammer_buffer_t buffer;
	int32_t xoff = (int32_t)buf_offset & HAMMER_BUFMASK;

	buf_offset &= ~HAMMER_BUFMASK64;
	KKASSERT((buf_offset & HAMMER_OFF_ZONE_MASK) != 0);

	buffer = *bufferp;
	if (buffer == NULL || (buffer->zone2_offset != buf_offset &&
			       buffer->zoneX_offset != buf_offset)) {
		if (buffer)
			hammer_rel_buffer(buffer, 0);
		buffer = hammer_get_buffer(hmp, buf_offset, bytes, 0, errorp);
		*bufferp = buffer;
	} else {
		*errorp = 0;
	}

	/*
	 * Return a pointer to the buffer data.
	 */
	if (buffer == NULL)
		return(NULL);
	else
		return((char *)buffer->ondisk + xoff);
}

void *
hammer_bread(hammer_mount_t hmp, hammer_off_t buf_offset,
	     int *errorp, struct hammer_buffer **bufferp)
{
	return(_hammer_bread(hmp, buf_offset, HAMMER_BUFSIZE, errorp, bufferp));
}

void *
hammer_bread_ext(hammer_mount_t hmp, hammer_off_t buf_offset, int bytes,
	         int *errorp, struct hammer_buffer **bufferp)
{
	bytes = (bytes + HAMMER_BUFMASK) & ~HAMMER_BUFMASK;
	return(_hammer_bread(hmp, buf_offset, bytes, errorp, bufferp));
}

/*
 * Access the filesystem buffer containing the specified hammer offset.
 * No disk read operation occurs.  The result buffer may contain garbage.
 *
 * Any prior buffer in *bufferp will be released and replaced by the
 * requested buffer.
 *
 * This function marks the buffer dirty but does not increment its
 * modify_refs count.
 */
static __inline
void *
_hammer_bnew(hammer_mount_t hmp, hammer_off_t buf_offset, int bytes,
	     int *errorp, struct hammer_buffer **bufferp)
{
	hammer_buffer_t buffer;
	int32_t xoff = (int32_t)buf_offset & HAMMER_BUFMASK;

	buf_offset &= ~HAMMER_BUFMASK64;

	buffer = *bufferp;
	if (buffer == NULL || (buffer->zone2_offset != buf_offset &&
			       buffer->zoneX_offset != buf_offset)) {
		if (buffer)
			hammer_rel_buffer(buffer, 0);
		buffer = hammer_get_buffer(hmp, buf_offset, bytes, 1, errorp);
		*bufferp = buffer;
	} else {
		*errorp = 0;
	}

	/*
	 * Return a pointer to the buffer data.
	 */
	if (buffer == NULL)
		return(NULL);
	else
		return((char *)buffer->ondisk + xoff);
}

void *
hammer_bnew(hammer_mount_t hmp, hammer_off_t buf_offset,
	     int *errorp, struct hammer_buffer **bufferp)
{
	return(_hammer_bnew(hmp, buf_offset, HAMMER_BUFSIZE, errorp, bufferp));
}

void *
hammer_bnew_ext(hammer_mount_t hmp, hammer_off_t buf_offset, int bytes,
		int *errorp, struct hammer_buffer **bufferp)
{
	bytes = (bytes + HAMMER_BUFMASK) & ~HAMMER_BUFMASK;
	return(_hammer_bnew(hmp, buf_offset, bytes, errorp, bufferp));
}

/************************************************************************
 *				NODES					*
 ************************************************************************
 *
 * Manage B-Tree nodes.  B-Tree nodes represent the primary indexing
 * method used by the HAMMER filesystem.
 *
 * Unlike other HAMMER structures, a hammer_node can be PASSIVELY
 * associated with its buffer, and will only referenced the buffer while
 * the node itself is referenced.
 *
 * A hammer_node can also be passively associated with other HAMMER
 * structures, such as inodes, while retaining 0 references.  These
 * associations can be cleared backwards using a pointer-to-pointer in
 * the hammer_node.
 *
 * This allows the HAMMER implementation to cache hammer_nodes long-term
 * and short-cut a great deal of the infrastructure's complexity.  In
 * most cases a cached node can be reacquired without having to dip into
 * either the buffer or cluster management code.
 *
 * The caller must pass a referenced cluster on call and will retain
 * ownership of the reference on return.  The node will acquire its own
 * additional references, if necessary.
 */
hammer_node_t
hammer_get_node(hammer_transaction_t trans, hammer_off_t node_offset,
		int isnew, int *errorp)
{
	hammer_mount_t hmp = trans->hmp;
	hammer_node_t node;

	KKASSERT((node_offset & HAMMER_OFF_ZONE_MASK) == HAMMER_ZONE_BTREE);

	/*
	 * Locate the structure, allocating one if necessary.
	 */
again:
	node = RB_LOOKUP(hammer_nod_rb_tree, &hmp->rb_nods_root, node_offset);
	if (node == NULL) {
		++hammer_count_nodes;
		node = kmalloc(sizeof(*node), hmp->m_misc, M_WAITOK|M_ZERO|M_USE_RESERVE);
		node->node_offset = node_offset;
		node->hmp = hmp;
		TAILQ_INIT(&node->cursor_list);
		TAILQ_INIT(&node->cache_list);
		if (RB_INSERT(hammer_nod_rb_tree, &hmp->rb_nods_root, node)) {
			--hammer_count_nodes;
			kfree(node, hmp->m_misc);
			goto again;
		}
	}
	hammer_ref(&node->lock);
	if (node->ondisk) {
		*errorp = 0;
	} else {
		*errorp = hammer_load_node(node, isnew);
		trans->flags |= HAMMER_TRANSF_DIDIO;
	}
	if (*errorp) {
		hammer_rel_node(node);
		node = NULL;
	}
	return(node);
}

/*
 * Reference an already-referenced node.
 */
void
hammer_ref_node(hammer_node_t node)
{
	KKASSERT(node->lock.refs > 0 && node->ondisk != NULL);
	hammer_ref(&node->lock);
}

/*
 * Load a node's on-disk data reference.
 */
static int
hammer_load_node(hammer_node_t node, int isnew)
{
	hammer_buffer_t buffer;
	hammer_off_t buf_offset;
	int error;

	error = 0;
	++node->loading;
	hammer_lock_ex(&node->lock);
	if (node->ondisk == NULL) {
		/*
		 * This is a little confusing but the jist is that
		 * node->buffer determines whether the node is on
		 * the buffer's clist and node->ondisk determines
		 * whether the buffer is referenced.
		 *
		 * We could be racing a buffer release, in which case
		 * node->buffer may become NULL while we are blocked
		 * referencing the buffer.
		 */
		if ((buffer = node->buffer) != NULL) {
			error = hammer_ref_buffer(buffer);
			if (error == 0 && node->buffer == NULL) {
				TAILQ_INSERT_TAIL(&buffer->clist,
						  node, entry);
				node->buffer = buffer;
			}
		} else {
			buf_offset = node->node_offset & ~HAMMER_BUFMASK64;
			buffer = hammer_get_buffer(node->hmp, buf_offset,
						   HAMMER_BUFSIZE, 0, &error);
			if (buffer) {
				KKASSERT(error == 0);
				TAILQ_INSERT_TAIL(&buffer->clist,
						  node, entry);
				node->buffer = buffer;
			}
		}
		if (error)
			goto failed;
		node->ondisk = (void *)((char *)buffer->ondisk +
				        (node->node_offset & HAMMER_BUFMASK));
		if (isnew == 0 && 
		    (node->flags & HAMMER_NODE_CRCGOOD) == 0) {
			if (hammer_crc_test_btree(node->ondisk) == 0)
				Debugger("CRC FAILED: B-TREE NODE");
			node->flags |= HAMMER_NODE_CRCGOOD;
		}
	}
failed:
	--node->loading;
	hammer_unlock(&node->lock);
	return (error);
}

/*
 * Safely reference a node, interlock against flushes via the IO subsystem.
 */
hammer_node_t
hammer_ref_node_safe(struct hammer_mount *hmp, hammer_node_cache_t cache,
		     int *errorp)
{
	hammer_node_t node;

	node = cache->node;
	if (node != NULL) {
		hammer_ref(&node->lock);
		if (node->ondisk)
			*errorp = 0;
		else
			*errorp = hammer_load_node(node, 0);
		if (*errorp) {
			hammer_rel_node(node);
			node = NULL;
		}
	} else {
		*errorp = ENOENT;
	}
	return(node);
}

/*
 * Release a hammer_node.  On the last release the node dereferences
 * its underlying buffer and may or may not be destroyed.
 */
void
hammer_rel_node(hammer_node_t node)
{
	hammer_buffer_t buffer;

	/*
	 * If this isn't the last ref just decrement the ref count and
	 * return.
	 */
	if (node->lock.refs > 1) {
		hammer_unref(&node->lock);
		return;
	}

	/*
	 * If there is no ondisk info or no buffer the node failed to load,
	 * remove the last reference and destroy the node.
	 */
	if (node->ondisk == NULL) {
		hammer_unref(&node->lock);
		hammer_flush_node(node);
		/* node is stale now */
		return;
	}

	/*
	 * Do not disassociate the node from the buffer if it represents
	 * a modified B-Tree node that still needs its crc to be generated.
	 */
	if (node->flags & HAMMER_NODE_NEEDSCRC)
		return;

	/*
	 * Do final cleanups and then either destroy the node and leave it
	 * passively cached.  The buffer reference is removed regardless.
	 */
	buffer = node->buffer;
	node->ondisk = NULL;

	if ((node->flags & HAMMER_NODE_FLUSH) == 0) {
		hammer_unref(&node->lock);
		hammer_rel_buffer(buffer, 0);
		return;
	}

	/*
	 * Destroy the node.
	 */
	hammer_unref(&node->lock);
	hammer_flush_node(node);
	/* node is stale */
	hammer_rel_buffer(buffer, 0);
}

/*
 * Free space on-media associated with a B-Tree node.
 */
void
hammer_delete_node(hammer_transaction_t trans, hammer_node_t node)
{
	KKASSERT((node->flags & HAMMER_NODE_DELETED) == 0);
	node->flags |= HAMMER_NODE_DELETED;
	hammer_blockmap_free(trans, node->node_offset, sizeof(*node->ondisk));
}

/*
 * Passively cache a referenced hammer_node.  The caller may release
 * the node on return.
 */
void
hammer_cache_node(hammer_node_cache_t cache, hammer_node_t node)
{
	/*
	 * If the node doesn't exist, or is being deleted, don't cache it!
	 *
	 * The node can only ever be NULL in the I/O failure path.
	 */
	if (node == NULL || (node->flags & HAMMER_NODE_DELETED))
		return;
	if (cache->node == node)
		return;
	while (cache->node)
		hammer_uncache_node(cache);
	if (node->flags & HAMMER_NODE_DELETED)
		return;
	cache->node = node;
	TAILQ_INSERT_TAIL(&node->cache_list, cache, entry);
}

void
hammer_uncache_node(hammer_node_cache_t cache)
{
	hammer_node_t node;

	if ((node = cache->node) != NULL) {
		TAILQ_REMOVE(&node->cache_list, cache, entry);
		cache->node = NULL;
		if (TAILQ_EMPTY(&node->cache_list))
			hammer_flush_node(node);
	}
}

/*
 * Remove a node's cache references and destroy the node if it has no
 * other references or backing store.
 */
void
hammer_flush_node(hammer_node_t node)
{
	hammer_node_cache_t cache;
	hammer_buffer_t buffer;
	hammer_mount_t hmp = node->hmp;

	while ((cache = TAILQ_FIRST(&node->cache_list)) != NULL) {
		TAILQ_REMOVE(&node->cache_list, cache, entry);
		cache->node = NULL;
	}
	if (node->lock.refs == 0 && node->ondisk == NULL) {
		KKASSERT((node->flags & HAMMER_NODE_NEEDSCRC) == 0);
		RB_REMOVE(hammer_nod_rb_tree, &node->hmp->rb_nods_root, node);
		if ((buffer = node->buffer) != NULL) {
			node->buffer = NULL;
			TAILQ_REMOVE(&buffer->clist, node, entry);
			/* buffer is unreferenced because ondisk is NULL */
		}
		--hammer_count_nodes;
		kfree(node, hmp->m_misc);
	}
}

/*
 * Flush passively cached B-Tree nodes associated with this buffer.
 * This is only called when the buffer is about to be destroyed, so
 * none of the nodes should have any references.  The buffer is locked.
 *
 * We may be interlocked with the buffer.
 */
void
hammer_flush_buffer_nodes(hammer_buffer_t buffer)
{
	hammer_node_t node;

	while ((node = TAILQ_FIRST(&buffer->clist)) != NULL) {
		KKASSERT(node->ondisk == NULL);
		KKASSERT((node->flags & HAMMER_NODE_NEEDSCRC) == 0);

		if (node->lock.refs == 0) {
			hammer_ref(&node->lock);
			node->flags |= HAMMER_NODE_FLUSH;
			hammer_rel_node(node);
		} else {
			KKASSERT(node->loading != 0);
			KKASSERT(node->buffer != NULL);
			buffer = node->buffer;
			node->buffer = NULL;
			TAILQ_REMOVE(&buffer->clist, node, entry);
			/* buffer is unreferenced because ondisk is NULL */
		}
	}
}


/************************************************************************
 *				ALLOCATORS				*
 ************************************************************************/

/*
 * Allocate a B-Tree node.
 */
hammer_node_t
hammer_alloc_btree(hammer_transaction_t trans, int *errorp)
{
	hammer_buffer_t buffer = NULL;
	hammer_node_t node = NULL;
	hammer_off_t node_offset;

	node_offset = hammer_blockmap_alloc(trans, HAMMER_ZONE_BTREE_INDEX,
					    sizeof(struct hammer_node_ondisk),
					    errorp);
	if (*errorp == 0) {
		node = hammer_get_node(trans, node_offset, 1, errorp);
		hammer_modify_node_noundo(trans, node);
		bzero(node->ondisk, sizeof(*node->ondisk));
		hammer_modify_node_done(node);
	}
	if (buffer)
		hammer_rel_buffer(buffer, 0);
	return(node);
}

/*
 * Allocate data.  If the address of a data buffer is supplied then
 * any prior non-NULL *data_bufferp will be released and *data_bufferp
 * will be set to the related buffer.  The caller must release it when
 * finally done.  The initial *data_bufferp should be set to NULL by
 * the caller.
 *
 * The caller is responsible for making hammer_modify*() calls on the
 * *data_bufferp.
 */
void *
hammer_alloc_data(hammer_transaction_t trans, int32_t data_len, 
		  u_int16_t rec_type, hammer_off_t *data_offsetp,
		  struct hammer_buffer **data_bufferp, int *errorp)
{
	void *data;
	int zone;

	/*
	 * Allocate data
	 */
	if (data_len) {
		switch(rec_type) {
		case HAMMER_RECTYPE_INODE:
		case HAMMER_RECTYPE_DIRENTRY:
		case HAMMER_RECTYPE_EXT:
		case HAMMER_RECTYPE_FIX:
		case HAMMER_RECTYPE_PFS:
			zone = HAMMER_ZONE_META_INDEX;
			break;
		case HAMMER_RECTYPE_DATA:
		case HAMMER_RECTYPE_DB:
			if (data_len <= HAMMER_BUFSIZE / 2) {
				zone = HAMMER_ZONE_SMALL_DATA_INDEX;
			} else {
				data_len = (data_len + HAMMER_BUFMASK) &
					   ~HAMMER_BUFMASK;
				zone = HAMMER_ZONE_LARGE_DATA_INDEX;
			}
			break;
		default:
			panic("hammer_alloc_data: rec_type %04x unknown",
			      rec_type);
			zone = 0;	/* NOT REACHED */
			break;
		}
		*data_offsetp = hammer_blockmap_alloc(trans, zone,
						      data_len, errorp);
	} else {
		*data_offsetp = 0;
	}
	if (*errorp == 0 && data_bufferp) {
		if (data_len) {
			data = hammer_bread_ext(trans->hmp, *data_offsetp,
						data_len, errorp, data_bufferp);
		} else {
			data = NULL;
		}
	} else {
		data = NULL;
	}
	return(data);
}

/*
 * Sync dirty buffers to the media and clean-up any loose ends.
 *
 * These functions do not start the flusher going, they simply
 * queue everything up to the flusher.
 */
static int hammer_sync_scan1(struct mount *mp, struct vnode *vp, void *data);
static int hammer_sync_scan2(struct mount *mp, struct vnode *vp, void *data);

int
hammer_queue_inodes_flusher(hammer_mount_t hmp, int waitfor)
{
	struct hammer_sync_info info;

	info.error = 0;
	info.waitfor = waitfor;
	if (waitfor == MNT_WAIT) {
		vmntvnodescan(hmp->mp, VMSC_GETVP|VMSC_ONEPASS,
			      hammer_sync_scan1, hammer_sync_scan2, &info);
	} else {
		vmntvnodescan(hmp->mp, VMSC_GETVP|VMSC_ONEPASS|VMSC_NOWAIT,
			      hammer_sync_scan1, hammer_sync_scan2, &info);
	}
	return(info.error);
}

/*
 * Filesystem sync.  If doing a synchronous sync make a second pass on
 * the vnodes in case any were already flushing during the first pass,
 * and activate the flusher twice (the second time brings the UNDO FIFO's
 * start position up to the end position after the first call).
 */
int
hammer_sync_hmp(hammer_mount_t hmp, int waitfor)
{
	struct hammer_sync_info info;

	info.error = 0;
	info.waitfor = MNT_NOWAIT;
	vmntvnodescan(hmp->mp, VMSC_GETVP|VMSC_NOWAIT,
		      hammer_sync_scan1, hammer_sync_scan2, &info);
	if (info.error == 0 && waitfor == MNT_WAIT) {
		info.waitfor = waitfor;
		vmntvnodescan(hmp->mp, VMSC_GETVP,
			      hammer_sync_scan1, hammer_sync_scan2, &info);
	}
        if (waitfor == MNT_WAIT) {
                hammer_flusher_sync(hmp);
                hammer_flusher_sync(hmp);
	} else {
                hammer_flusher_async(hmp, NULL);
                hammer_flusher_async(hmp, NULL);
	}
	return(info.error);
}

static int
hammer_sync_scan1(struct mount *mp, struct vnode *vp, void *data)
{
	struct hammer_inode *ip;

	ip = VTOI(vp);
	if (vp->v_type == VNON || ip == NULL ||
	    ((ip->flags & HAMMER_INODE_MODMASK) == 0 &&
	     RB_EMPTY(&vp->v_rbdirty_tree))) {
		return(-1);
	}
	return(0);
}

static int
hammer_sync_scan2(struct mount *mp, struct vnode *vp, void *data)
{
	struct hammer_sync_info *info = data;
	struct hammer_inode *ip;
	int error;

	ip = VTOI(vp);
	if (vp->v_type == VNON || vp->v_type == VBAD ||
	    ((ip->flags & HAMMER_INODE_MODMASK) == 0 &&
	     RB_EMPTY(&vp->v_rbdirty_tree))) {
		return(0);
	}
	error = VOP_FSYNC(vp, MNT_NOWAIT);
	if (error)
		info->error = error;
	return(0);
}

