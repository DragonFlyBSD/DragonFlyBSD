/*
 * Copyright (c) 2007 The DragonFly Project.  All rights reserved.
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
 * $DragonFly: src/sys/vfs/hammer/hammer_ondisk.c,v 1.31 2008/02/23 03:01:08 dillon Exp $
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
static int hammer_load_node(hammer_node_t node);
#if 0
static hammer_off_t hammer_advance_fifo(hammer_volume_t volume,
		hammer_off_t off, int32_t bytes);

static hammer_off_t hammer_alloc_fifo(hammer_mount_t hmp, int32_t rec_len,
		int32_t data_len, struct hammer_buffer **rec_bufferp,
		u_int16_t hdr_type, int can_cross, 
		struct hammer_buffer **data2_bufferp, int *errorp);
#endif

/*
 * Red-Black tree support for various structures
 */
static int
hammer_ino_rb_compare(hammer_inode_t ip1, hammer_inode_t ip2)
{
	if (ip1->obj_id < ip2->obj_id)
		return(-1);
	if (ip1->obj_id > ip2->obj_id)
		return(1);
	if (ip1->obj_asof < ip2->obj_asof)
		return(-1);
	if (ip1->obj_asof > ip2->obj_asof)
		return(1);
	return(0);
}

static int
hammer_inode_info_cmp(hammer_inode_info_t info, hammer_inode_t ip)
{
	if (info->obj_id < ip->obj_id)
		return(-1);
	if (info->obj_id > ip->obj_id)
		return(1);
	if (info->obj_asof < ip->obj_asof)
		return(-1);
	if (info->obj_asof > ip->obj_asof)
		return(1);
	return(0);
}

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
	if (buf1->zone2_offset < buf2->zone2_offset)
		return(-1);
	if (buf1->zone2_offset > buf2->zone2_offset)
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

/*
 * Note: The lookup function for hammer_ino_rb_tree winds up being named
 * hammer_ino_rb_tree_RB_LOOKUP_INFO(root, info).  The other lookup
 * functions are normal, e.g. hammer_buf_rb_tree_RB_LOOKUP(root, zone2_offset).
 */
RB_GENERATE(hammer_ino_rb_tree, hammer_inode, rb_node, hammer_ino_rb_compare);
RB_GENERATE_XLOOKUP(hammer_ino_rb_tree, INFO, hammer_inode, rb_node,
		hammer_inode_info_cmp, hammer_inode_info_t);
RB_GENERATE2(hammer_vol_rb_tree, hammer_volume, rb_node,
	     hammer_vol_rb_compare, int32_t, vol_no);
RB_GENERATE2(hammer_buf_rb_tree, hammer_buffer, rb_node,
	     hammer_buf_rb_compare, hammer_off_t, zone2_offset);
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
hammer_install_volume(struct hammer_mount *hmp, const char *volname)
{
	struct mount *mp;
	hammer_volume_t volume;
	struct hammer_volume_ondisk *ondisk;
	struct nlookupdata nd;
	struct buf *bp = NULL;
	int error;
	int ronly;

	mp = hmp->mp;
	ronly = ((mp->mnt_flag & MNT_RDONLY) ? 1 : 0);

	/*
	 * Allocate a volume structure
	 */
	++hammer_count_volumes;
	volume = kmalloc(sizeof(*volume), M_HAMMER, M_WAITOK|M_ZERO);
	volume->vol_name = kstrdup(volname, M_HAMMER);
	volume->hmp = hmp;
	hammer_io_init(&volume->io, HAMMER_STRUCTURE_VOLUME);
	volume->io.offset = 0LL;

	/*
	 * Get the device vnode
	 */
	error = nlookup_init(&nd, volume->vol_name, UIO_SYSSPACE, NLC_FOLLOW);
	if (error == 0)
		error = nlookup(&nd);
	if (error == 0)
		error = cache_vref(&nd.nl_nch, nd.nl_cred, &volume->devvp);
	nlookup_done(&nd);
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
	RB_INIT(&volume->rb_bufs_root);

	hmp->mp->mnt_stat.f_blocks += volume->nblocks;

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
		if (bp) {
			brelse(bp);
			bp = NULL;
		}
		hmp->fsid_udev = dev2udev(vn_todev(volume->devvp));
	}
late_failure:
	if (bp)
		brelse(bp);
	if (error) {
		/*vinvalbuf(volume->devvp, V_SAVE, 0, 0);*/
		VOP_CLOSE(volume->devvp, ronly ? FREAD : FREAD|FWRITE);
		hammer_free_volume(volume);
	}
	return (error);
}

/*
 * Unload and free a HAMMER volume.  Must return >= 0 to continue scan
 * so returns -1 on failure.
 */
int
hammer_unload_volume(hammer_volume_t volume, void *data __unused)
{
	struct hammer_mount *hmp = volume->hmp;
	int ronly = ((hmp->mp->mnt_flag & MNT_RDONLY) ? 1 : 0);

	/*
	 * Sync clusters, sync volume
	 */

	hmp->mp->mnt_stat.f_blocks -= volume->nblocks;

	/*
	 * Clean up the root volume pointer, which is held unlocked in hmp.
	 */
	if (hmp->rootvol == volume)
		hmp->rootvol = NULL;

	/*
	 * Unload clusters and super-clusters.  Unloading a super-cluster
	 * also unloads related clusters, but the filesystem may not be
	 * using super-clusters so unload clusters anyway.
	 */
	RB_SCAN(hammer_buf_rb_tree, &volume->rb_bufs_root, NULL,
			hammer_unload_buffer, NULL);
	hammer_io_waitdep(&volume->io);

	/*
	 * Release our buffer and flush anything left in the buffer cache.
	 */
	hammer_io_release(&volume->io, 2);

	/*
	 * There should be no references on the volume, no clusters, and
	 * no super-clusters.
	 */
	KKASSERT(volume->io.lock.refs == 0);
	KKASSERT(RB_EMPTY(&volume->rb_bufs_root));

	volume->ondisk = NULL;
	if (volume->devvp) {
		if (ronly) {
			vinvalbuf(volume->devvp, 0, 0, 0);
			VOP_CLOSE(volume->devvp, FREAD);
		} else {
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
	if (volume->vol_name) {
		kfree(volume->vol_name, M_HAMMER);
		volume->vol_name = NULL;
	}
	if (volume->devvp) {
		if (vn_isdisk(volume->devvp, NULL) &&
		    volume->devvp->v_rdev &&
		    volume->devvp->v_rdev->si_mountpoint == volume->hmp->mp
		) {
			volume->devvp->v_rdev->si_mountpoint = NULL;
		}
		vrele(volume->devvp);
		volume->devvp = NULL;
	}
	--hammer_count_volumes;
	kfree(volume, M_HAMMER);
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
	struct hammer_volume_ondisk *ondisk;
	int error;

	hammer_lock_ex(&volume->io.lock);
	KKASSERT(volume->io.loading == 0);
	volume->io.loading = 1;

	if (volume->ondisk == NULL) {
		error = hammer_io_read(volume->devvp, &volume->io);
		if (error) {
			volume->io.loading = 0;
			hammer_unlock(&volume->io.lock);
			return (error);
		}
		volume->ondisk = ondisk = (void *)volume->io.bp->b_data;
	} else {
		error = 0;
	}
	volume->io.loading = 0;
	hammer_unlock(&volume->io.lock);
	return(0);
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
	if (volume->io.lock.refs == 1) {
		hammer_lock_ex(&volume->io.lock);
		if (volume->io.lock.refs == 1) {
			volume->ondisk = NULL;
			hammer_io_release(&volume->io, flush);
		} else if (flush) {
			hammer_io_flush(&volume->io);
		}
		hammer_unlock(&volume->io.lock);
	}
	hammer_unref(&volume->io.lock);
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
		  int isnew, int *errorp)
{
	hammer_buffer_t buffer;
	hammer_volume_t volume;
	hammer_off_t	zoneX_offset;
	int vol_no;
	int zone;

	zoneX_offset = buf_offset;
	zone = HAMMER_ZONE_DECODE(buf_offset);
	if (zone > HAMMER_ZONE_RAW_BUFFER_INDEX) {
		buf_offset = hammer_blockmap_lookup(hmp, buf_offset, errorp);
		KKASSERT(*errorp == 0);
	}
	buf_offset &= ~HAMMER_BUFMASK64;
	KKASSERT((buf_offset & HAMMER_ZONE_RAW_BUFFER) ==
		 HAMMER_ZONE_RAW_BUFFER);
	vol_no = HAMMER_VOL_DECODE(buf_offset);
	volume = hammer_get_volume(hmp, vol_no, errorp);
	if (volume == NULL)
		return(NULL);

	/*
	 * NOTE: buf_offset and maxbuf_off are both full offset
	 * specifications.
	 */
	KKASSERT(buf_offset < volume->maxbuf_off);

	/*
	 * Locate and lock the buffer structure, creating one if necessary.
	 */
again:
	buffer = RB_LOOKUP(hammer_buf_rb_tree, &volume->rb_bufs_root,
			   buf_offset);
	if (buffer == NULL) {
		++hammer_count_buffers;
		buffer = kmalloc(sizeof(*buffer), M_HAMMER, M_WAITOK|M_ZERO);
		buffer->zone2_offset = buf_offset;
		buffer->volume = volume;
		hammer_io_init(&buffer->io, HAMMER_STRUCTURE_BUFFER);
		buffer->io.offset = volume->ondisk->vol_buf_beg +
				    (buf_offset & HAMMER_OFF_SHORT_MASK);
		TAILQ_INIT(&buffer->clist);
		hammer_ref(&buffer->io.lock);

		/*
		 * Insert the buffer into the RB tree and handle late
		 * collisions.
		 */
		if (RB_INSERT(hammer_buf_rb_tree, &volume->rb_bufs_root, buffer)) {
			hammer_unref(&buffer->io.lock);
			--hammer_count_buffers;
			kfree(buffer, M_HAMMER);
			goto again;
		}
		hammer_ref(&volume->io.lock);
	} else {
		hammer_ref(&buffer->io.lock);
	}

	/*
	 * Cache the blockmap translation
	 */
	if ((zoneX_offset & HAMMER_ZONE_RAW_BUFFER) != HAMMER_ZONE_RAW_BUFFER)
		buffer->zoneX_offset = zoneX_offset;

	/*
	 * Deal with on-disk info
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
	hammer_rel_volume(volume, 0);
	return(buffer);
}

static int
hammer_load_buffer(hammer_buffer_t buffer, int isnew)
{
	hammer_volume_t volume;
	void *ondisk;
	int error;

	/*
	 * Load the buffer's on-disk info
	 */
	volume = buffer->volume;
	hammer_lock_ex(&buffer->io.lock);
	KKASSERT(buffer->io.loading == 0);
	buffer->io.loading = 1;

	if (buffer->ondisk == NULL) {
		if (isnew) {
			error = hammer_io_new(volume->devvp, &buffer->io);
		} else {
			error = hammer_io_read(volume->devvp, &buffer->io);
		}
		if (error) {
			buffer->io.loading = 0;
			hammer_unlock(&buffer->io.lock);
			return (error);
		}
		buffer->ondisk = ondisk = (void *)buffer->io.bp->b_data;
	} else if (isnew) {
		error = hammer_io_new(volume->devvp, &buffer->io);
	} else {
		error = 0;
	}
	if (error == 0 && isnew) {
		hammer_modify_buffer(buffer, NULL, 0);
		/* additional initialization goes here */
	}
	buffer->io.loading = 0;
	hammer_unlock(&buffer->io.lock);
	return (error);
}

/*
 * NOTE: Called from RB_SCAN, must return >= 0 for scan to continue.
 */
int
hammer_unload_buffer(hammer_buffer_t buffer, void *data __unused)
{
	hammer_ref(&buffer->io.lock);
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

	hammer_ref(&buffer->io.lock);
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

	if (buffer->io.lock.refs == 1) {
		hammer_lock_ex(&buffer->io.lock);
		if (buffer->io.lock.refs == 1) {
			hammer_io_release(&buffer->io, flush);

			if (buffer->io.bp == NULL &&
			    buffer->io.lock.refs == 1) {
				hammer_flush_buffer_nodes(buffer);
				KKASSERT(TAILQ_EMPTY(&buffer->clist));
				volume = buffer->volume;
				RB_REMOVE(hammer_buf_rb_tree,
					  &volume->rb_bufs_root, buffer);
				buffer->volume = NULL; /* sanity */
				--hammer_count_buffers;
				kfree(buffer, M_HAMMER);
				hammer_rel_volume(volume, 0);
				return;
			}
		} else if (flush) {
			hammer_io_flush(&buffer->io);
		}
		hammer_unlock(&buffer->io.lock);
	}
	hammer_unref(&buffer->io.lock);
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
void *
hammer_bread(hammer_mount_t hmp, hammer_off_t buf_offset, int *errorp, 
	     struct hammer_buffer **bufferp)
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
		buffer = hammer_get_buffer(hmp, buf_offset, 0, errorp);
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

/*
 * Access the filesystem buffer containing the specified hammer offset.
 * No disk read operation occurs.  The result buffer may contain garbage.
 *
 * Any prior buffer in *bufferp will be released and replaced by the
 * requested buffer.
 */
void *
hammer_bnew(hammer_mount_t hmp, hammer_off_t buf_offset, int *errorp, 
	     struct hammer_buffer **bufferp)
{
	hammer_buffer_t buffer;
	int32_t xoff = (int32_t)buf_offset & HAMMER_BUFMASK;

	buf_offset &= ~HAMMER_BUFMASK64;

	buffer = *bufferp;
	if (buffer == NULL || (buffer->zone2_offset != buf_offset &&
			       buffer->zoneX_offset != buf_offset)) {
		if (buffer)
			hammer_rel_buffer(buffer, 0);
		buffer = hammer_get_buffer(hmp, buf_offset, 1, errorp);
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
hammer_get_node(hammer_mount_t hmp, hammer_off_t node_offset, int *errorp)
{
	hammer_node_t node;

	KKASSERT((node_offset & HAMMER_OFF_ZONE_MASK) == HAMMER_ZONE_BTREE);

	/*
	 * Locate the structure, allocating one if necessary.
	 */
again:
	node = RB_LOOKUP(hammer_nod_rb_tree, &hmp->rb_nods_root, node_offset);
	if (node == NULL) {
		++hammer_count_nodes;
		node = kmalloc(sizeof(*node), M_HAMMER, M_WAITOK|M_ZERO);
		node->node_offset = node_offset;
		node->hmp = hmp;
		if (RB_INSERT(hammer_nod_rb_tree, &hmp->rb_nods_root, node)) {
			--hammer_count_nodes;
			kfree(node, M_HAMMER);
			goto again;
		}
	}
	hammer_ref(&node->lock);
	*errorp = hammer_load_node(node);
	if (*errorp) {
		hammer_rel_node(node);
		node = NULL;
	}
	return(node);
}

/*
 * Reference an already-referenced node.
 */
int
hammer_ref_node(hammer_node_t node)
{
	int error;

	KKASSERT(node->lock.refs > 0);
	hammer_ref(&node->lock);
	if ((error = hammer_load_node(node)) != 0)
		hammer_rel_node(node);
	return(error);
}

/*
 * Load a node's on-disk data reference.
 */
static int
hammer_load_node(hammer_node_t node)
{
	hammer_buffer_t buffer;
	int error;

	if (node->ondisk)
		return(0);
	error = 0;
	hammer_lock_ex(&node->lock);
	if (node->ondisk == NULL) {
		/*
		 * This is a little confusing but the jist is that
		 * node->buffer determines whether the node is on
		 * the buffer's clist and node->ondisk determines
		 * whether the buffer is referenced.
		 */
		if ((buffer = node->buffer) != NULL) {
			error = hammer_ref_buffer(buffer);
		} else {
			buffer = hammer_get_buffer(node->hmp,
						   node->node_offset, 0,
						   &error);
			if (buffer) {
				KKASSERT(error == 0);
				TAILQ_INSERT_TAIL(&buffer->clist,
						  node, entry);
				node->buffer = buffer;
			}
		}
		if (error == 0) {
			node->ondisk = (void *)((char *)buffer->ondisk +
			       (node->node_offset & HAMMER_BUFMASK));
		}
	}
	hammer_unlock(&node->lock);
	return (error);
}

/*
 * Safely reference a node, interlock against flushes via the IO subsystem.
 */
hammer_node_t
hammer_ref_node_safe(struct hammer_mount *hmp, struct hammer_node **cache,
		     int *errorp)
{
	hammer_node_t node;

	if ((node = *cache) != NULL)
		hammer_ref(&node->lock);
	if (node) {
		*errorp = hammer_load_node(node);
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
	 * Do final cleanups and then either destroy the node and leave it
	 * passively cached.  The buffer reference is removed regardless.
	 */
	buffer = node->buffer;
	node->ondisk = NULL;

	if ((node->flags & (HAMMER_NODE_DELETED|HAMMER_NODE_FLUSH)) == 0) {
		hammer_unref(&node->lock);
		hammer_rel_buffer(buffer, 0);
		return;
	}

	/*
	 * Destroy the node if it has been marked for deletion.  We mark
	 * it as being free.  Note that the disk space is physically
	 * freed when the fifo cycles back through the node.
	 */
	if (node->flags & HAMMER_NODE_DELETED) {
		hammer_blockmap_free(node->hmp, node->node_offset,
				     sizeof(*node->ondisk));
	}

	/*
	 * Destroy the node.  Record pertainant data because the node
	 * becomes stale the instant we flush it.
	 */
	hammer_unref(&node->lock);
	hammer_flush_node(node);
	/* node is stale */
	hammer_rel_buffer(buffer, 0);
}

/*
 * Passively cache a referenced hammer_node in *cache.  The caller may
 * release the node on return.
 */
void
hammer_cache_node(hammer_node_t node, struct hammer_node **cache)
{
	hammer_node_t old;

	/*
	 * If the node is being deleted, don't cache it!
	 */
	if (node->flags & HAMMER_NODE_DELETED)
		return;

	/*
	 * Cache the node.  If we previously cached a different node we
	 * have to give HAMMER a chance to destroy it.
	 */
again:
	if (node->cache1 != cache) {
		if (node->cache2 != cache) {
			if ((old = *cache) != NULL) {
				KKASSERT(node->lock.refs != 0);
				hammer_uncache_node(cache);
				goto again;
			}
			if (node->cache2)
				*node->cache2 = NULL;
			node->cache2 = node->cache1;
			node->cache1 = cache;
			*cache = node;
		} else {
			struct hammer_node **tmp;
			tmp = node->cache1;
			node->cache1 = node->cache2;
			node->cache2 = tmp;
		}
	}
}

void
hammer_uncache_node(struct hammer_node **cache)
{
	hammer_node_t node;

	if ((node = *cache) != NULL) {
		*cache = NULL;
		if (node->cache1 == cache) {
			node->cache1 = node->cache2;
			node->cache2 = NULL;
		} else if (node->cache2 == cache) {
			node->cache2 = NULL;
		} else {
			panic("hammer_uncache_node: missing cache linkage");
		}
		if (node->cache1 == NULL && node->cache2 == NULL)
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
	hammer_buffer_t buffer;

	if (node->cache1)
		*node->cache1 = NULL;
	if (node->cache2)
		*node->cache2 = NULL;
	if (node->lock.refs == 0 && node->ondisk == NULL) {
		RB_REMOVE(hammer_nod_rb_tree, &node->hmp->rb_nods_root, node);
		if ((buffer = node->buffer) != NULL) {
			node->buffer = NULL;
			TAILQ_REMOVE(&buffer->clist, node, entry);
			/* buffer is unreferenced because ondisk is NULL */
		}
		--hammer_count_nodes;
		kfree(node, M_HAMMER);
	}
}

/*
 * Flush passively cached B-Tree nodes associated with this buffer.
 * This is only called when the buffer is about to be destroyed, so
 * none of the nodes should have any references.
 */
void
hammer_flush_buffer_nodes(hammer_buffer_t buffer)
{
	hammer_node_t node;

	while ((node = TAILQ_FIRST(&buffer->clist)) != NULL) {
		KKASSERT(node->lock.refs == 0 && node->ondisk == NULL);
		hammer_ref(&node->lock);
		node->flags |= HAMMER_NODE_FLUSH;
		hammer_rel_node(node);
	}
}


/************************************************************************
 *				ALLOCATORS				*
 ************************************************************************/

/*
 * Allocate a B-Tree node.
 */
hammer_node_t
hammer_alloc_btree(hammer_mount_t hmp, int *errorp)
{
	hammer_buffer_t buffer = NULL;
	hammer_node_t node = NULL;
	hammer_off_t node_offset;

	node_offset = hammer_blockmap_alloc(hmp, HAMMER_ZONE_BTREE_INDEX,
					    sizeof(struct hammer_node_ondisk),
					    errorp);
	if (*errorp == 0) {
		node = hammer_get_node(hmp, node_offset, errorp);
		hammer_modify_node(node);
		bzero(node->ondisk, sizeof(*node->ondisk));
	}
	if (buffer)
		hammer_rel_buffer(buffer, 0);
	return(node);
}

/*
 * The returned buffers are already appropriately marked as being modified.
 * If the caller marks them again unnecessary undo records may be generated.
 *
 * In-band data is indicated by data_bufferp == NULL.  Pass a data_len of 0
 * for zero-fill (caller modifies data_len afterwords).
 */
void *
hammer_alloc_record(hammer_mount_t hmp, 
		    hammer_off_t *rec_offp, u_int8_t rec_type, 
		    struct hammer_buffer **rec_bufferp,
		    int32_t data_len, void **datap,
		    struct hammer_buffer **data_bufferp, int *errorp)
{
	hammer_record_ondisk_t rec;
	hammer_off_t rec_offset;
	hammer_off_t data_offset;
	int32_t reclen;

	if (datap)
		*datap = NULL;

	/*
	 * Allocate the record
	 */
	rec_offset = hammer_blockmap_alloc(hmp, HAMMER_ZONE_RECORD_INDEX,
					   HAMMER_RECORD_SIZE, errorp);
	if (*errorp)
		return(NULL);

	/*
	 * Allocate data
	 */
	if (data_len) {
		if (data_bufferp == NULL) {
			switch(rec_type) {
			case HAMMER_RECTYPE_DATA:
				reclen = offsetof(struct hammer_data_record,
						  data[0]);
				break;
			case HAMMER_RECTYPE_DIRENTRY:
				reclen = offsetof(struct hammer_entry_record,
						  name[0]);
				break;
			default:
				panic("hammer_alloc_record: illegal "
				      "in-band data");
				/* NOT REACHED */
				reclen = 0;
				break;
			}
			KKASSERT(reclen + data_len <= HAMMER_RECORD_SIZE);
			data_offset = rec_offset + reclen;
		} else if (data_len < HAMMER_BUFSIZE) {
			data_offset = hammer_blockmap_alloc(hmp,
						HAMMER_ZONE_SMALL_DATA_INDEX,
						data_len, errorp);
		} else {
			data_offset = hammer_blockmap_alloc(hmp,
						HAMMER_ZONE_LARGE_DATA_INDEX,
						data_len, errorp);
		}
	} else {
		data_offset = 0;
	}
	if (*errorp) {
		hammer_blockmap_free(hmp, rec_offset, HAMMER_RECORD_SIZE);
		return(NULL);
	}

	/*
	 * Basic return values.
	 */
	*rec_offp = rec_offset;
	rec = hammer_bread(hmp, rec_offset, errorp, rec_bufferp);
	hammer_modify_buffer(*rec_bufferp, NULL, 0);
	bzero(rec, sizeof(*rec));
	KKASSERT(*errorp == 0);
	rec->base.data_off = data_offset;
	rec->base.data_len = data_len;

	if (data_bufferp) {
		if (data_len) {
			*datap = hammer_bread(hmp, data_offset, errorp,
					      data_bufferp);
			KKASSERT(*errorp == 0);
			hammer_modify_buffer(*data_bufferp, NULL, 0);
		} else {
			*datap = NULL;
		}
	} else if (data_len) {
		KKASSERT(data_offset + data_len - rec_offset <=
			 HAMMER_RECORD_SIZE); 
		if (datap) {
			*datap = (void *)((char *)rec +
					  (int32_t)(data_offset - rec_offset));
		}
	} else {
		KKASSERT(datap == NULL);
	}
	KKASSERT(*errorp == 0);
	return(rec);
}

/*
 * Generate an undo fifo entry and return the buffer to the caller (XXX).
 * The caller must create a dependancy to ensure that the undo record is
 * flushed before the modified buffer is flushed.
 */
int
hammer_generate_undo(hammer_mount_t hmp, hammer_off_t off, void *base, int len)
{
	return(0);
#if 0
	hammer_off_t rec_offset;
	hammer_fifo_undo_t undo;
	hammer_buffer_t buffer = NULL;
	int error;

	rec_offset = hammer_alloc_fifo(hmp, sizeof(*undo), len,
				       &buffer, HAMMER_HEAD_TYPE_UNDO,
				       0, NULL, &error);
	if (error == 0) {
		undo = (void *)((char *)buffer->ondisk + 
				((int32_t)rec_offset & HAMMER_BUFMASK));
		undo->undo_offset = off;
		bcopy(base, undo + 1, len);
	}
	if (buffer)
		hammer_rel_buffer(buffer, 0);
	return(error);
#endif
}

#if 0

/*
 * Allocate space from the FIFO.  The first rec_len bytes will be zero'd.
 * The entire space is marked modified (the caller should not remark it as
 * that will cause unnecessary undo records to be added).
 */
static
hammer_off_t
hammer_alloc_fifo(hammer_mount_t hmp, int32_t rec_len, int32_t data_len,
		  struct hammer_buffer **rec_bufferp, u_int16_t hdr_type,
		  int can_cross, 
		  struct hammer_buffer **data2_bufferp, int *errorp)
{
	hammer_volume_t root_volume;
	hammer_volume_t end_volume;
	hammer_volume_ondisk_t ondisk;
	hammer_fifo_head_t head;
	hammer_fifo_tail_t tail;
	hammer_off_t end_off = 0;
	hammer_off_t tmp_off = 0;
	int32_t end_vol_no;
	int32_t tmp_vol_no;
	int32_t xoff;
	int32_t aligned_bytes;
	int must_pad;

	aligned_bytes = (rec_len + data_len + HAMMER_TAIL_ONDISK_SIZE +
			 HAMMER_HEAD_ALIGN_MASK) & ~HAMMER_HEAD_ALIGN_MASK;

	root_volume = hammer_get_root_volume(hmp, errorp);
	if (root_volume)
		hammer_modify_volume(root_volume, NULL, 0);

	while (root_volume) {
		ondisk = root_volume->ondisk;

		end_off = ondisk->vol0_fifo_end;
		end_vol_no = HAMMER_VOL_DECODE(end_off);

		end_volume = hammer_get_volume(hmp, end_vol_no, errorp);
		if (*errorp)
			goto done;

		/*
		 * Check to see if we ran out of space.  Include some extra
		 * room.
		 *
		 * vol0_fifo_end cannot be advanced into the same buffer
		 * that vol0_fifo_beg resides in.  This allows us to
		 * instantiate a new buffer without reading it in.
		 *
		 * XXX messy.
		 */
		tmp_off = ondisk->vol0_fifo_beg & ~HAMMER_BUFMASK64;
		tmp_vol_no = HAMMER_VOL_DECODE(tmp_off);
		if ((tmp_off & HAMMER_OFF_SHORT_MASK) == 0) {
			if (end_vol_no + 1 == tmp_vol_no) {
				tmp_vol_no = end_vol_no;
				tmp_off = end_volume->maxbuf_off;
			} else if (end_vol_no + 1 == hmp->nvolumes &&
				   tmp_vol_no == 0) {
				tmp_vol_no = end_vol_no;
				tmp_off = end_volume->maxbuf_off;
			}
		}
		hammer_rel_volume(end_volume, 0);

		/*
		 * XXX dummy head at end of fifo
		 */
		if (end_vol_no == tmp_vol_no &&
		    end_off < tmp_off &&
		    end_off + aligned_bytes + sizeof(*head) >= tmp_off) {
			*errorp = ENOSPC;
			goto done;
		}

		if ((int32_t)end_off & HAMMER_BUFMASK)
			head = hammer_bread(hmp, end_off, errorp, rec_bufferp);
		else
			head = hammer_bnew(hmp, end_off, errorp, rec_bufferp);
		if (*errorp)
			goto done;

		/*
		 * Load the buffer, retry if someone else squeeked in
		 * while we were blocked.
		 */

		if (ondisk->vol0_fifo_end != end_off)
			continue;

		/*
		 * Ok, we're gonna do something.  Modify the buffer
		 */
		hammer_modify_buffer(*rec_bufferp, NULL, 0);
		if (ondisk->vol0_fifo_end != end_off)
			continue;
		xoff = (int32_t)end_off & HAMMER_BUFMASK;

		/*
		 * The non-data portion of the fifo record cannot cross
		 * a buffer boundary.
		 *
		 * The entire record cannot cross a buffer boundary if
		 * can_cross is 0.
		 *
		 * The entire record cannot cover more then two whole buffers
		 * regardless.  Even if the data portion is 16K, this case
		 * can occur due to the addition of the fifo_tail.
		 *
		 * It is illegal for a record to cross a volume boundary.
		 *
		 * It is illegal for a record to cross a recovery boundary
		 * (this is so recovery code is guaranteed a record rather
		 * then data at certain points).
		 *
		 * Add a pad record and loop if it does.
		 */
		must_pad = 0;
		if (xoff + rec_len > HAMMER_BUFSIZE)
			must_pad = 1;
		if (can_cross == 0) {
			if (xoff + aligned_bytes > HAMMER_BUFSIZE)
				must_pad = 1;
		} else {
			if (xoff + aligned_bytes > HAMMER_BUFSIZE &&
			    (end_off + aligned_bytes) >=
			    (*rec_bufferp)->volume->maxbuf_off) {
				must_pad = 1;
			}
			if ((end_off ^ (end_off + aligned_bytes)) &
			    HAMMER_OFF_SHORT_REC_MASK) {
				must_pad = 1;
			}
			if (xoff + aligned_bytes - HAMMER_BUFSIZE >
			    HAMMER_BUFSIZE) {
				KKASSERT(xoff != 0);
				must_pad = 1;
			}
		}

		/*
		 * Pad to end of the buffer if necessary.  PADs can be
		 * squeezed into as little as 8 bytes (hence our alignment
		 * requirement).  The crc, reserved, and sequence number
		 * fields are not used, but initialize them anyway if there
		 * is enough room.
		 */
		if (must_pad) {
			xoff = HAMMER_BUFSIZE - xoff;
			head->hdr_signature = HAMMER_HEAD_SIGNATURE;
			head->hdr_type = HAMMER_HEAD_TYPE_PAD;
			head->hdr_size = xoff;
			if (xoff >= HAMMER_HEAD_ONDISK_SIZE +
				    HAMMER_TAIL_ONDISK_SIZE) {
				head->hdr_crc = 0;
				head->hdr_reserved02 = 0;
				head->hdr_seq = 0;
			}

			tail = (void *)((char *)head + xoff -
					HAMMER_TAIL_ONDISK_SIZE);
			if ((void *)head != (void *)tail) {
				tail->tail_signature = HAMMER_TAIL_SIGNATURE;
				tail->tail_type = HAMMER_HEAD_TYPE_PAD;
				tail->tail_size = xoff;
			}
			KKASSERT((xoff & HAMMER_HEAD_ALIGN_MASK) == 0);
			ondisk->vol0_fifo_end =
				hammer_advance_fifo((*rec_bufferp)->volume,
						    end_off, xoff);
			continue;
		}

		if (xoff + aligned_bytes > HAMMER_BUFSIZE) {
			xoff = xoff + aligned_bytes - HAMMER_BUFSIZE;

			KKASSERT(xoff <= HAMMER_BUFSIZE);
			tail = hammer_bnew(hmp, end_off + aligned_bytes -
						HAMMER_TAIL_ONDISK_SIZE,
					   errorp, data2_bufferp);
			hammer_modify_buffer(*data2_bufferp, NULL, 0);
			if (*errorp)
				goto done;

			/*
			 * Retry if someone else appended to the fifo while
			 * we were blocked.
			 */
			if (ondisk->vol0_fifo_end != end_off)
				continue;
		} else {
			tail = (void *)((char *)head + aligned_bytes -
					HAMMER_TAIL_ONDISK_SIZE);
		}

		bzero(head, rec_len);
		head->hdr_signature = HAMMER_HEAD_SIGNATURE;
		head->hdr_type = hdr_type;
		head->hdr_size = aligned_bytes;
		head->hdr_crc = 0;
		head->hdr_seq = root_volume->ondisk->vol0_next_seq++;

		tail->tail_signature = HAMMER_TAIL_SIGNATURE;
		tail->tail_type = hdr_type;
		tail->tail_size = aligned_bytes;

		ondisk->vol0_fifo_end =
			hammer_advance_fifo((*rec_bufferp)->volume,
					    end_off, aligned_bytes);
done:
		hammer_rel_volume(root_volume, 0);
		break;
	}
	if (*errorp)
		end_off = 0;
	return(end_off);
}

/*
 * Mark a fifo record as having been freed.  XXX needs undo.
 */
void
hammer_free_fifo(hammer_mount_t hmp, hammer_off_t fifo_offset)
{
	hammer_buffer_t buffer = NULL;
	hammer_fifo_head_t head;
	int error;

	head = hammer_bread(hmp, fifo_offset, &error, &buffer);
	if (head) {
		hammer_modify_buffer(buffer, &head->hdr_type,
				     sizeof(head->hdr_type));
		head->hdr_type |= HAMMER_HEAD_FLAG_FREE;
	}
	if (buffer)
		hammer_rel_buffer(buffer, 0);
}

/*
 * Attempt to rewind the FIFO
 *
 * This routine is allowed to do nothing.
 */
void
hammer_unwind_fifo(hammer_mount_t hmp, hammer_off_t rec_offset)
{
}

/*
 * Advance the FIFO a certain number of bytes.
 */
static
hammer_off_t
hammer_advance_fifo(hammer_volume_t volume, hammer_off_t off, int32_t bytes)
{
	int32_t vol_no;

	off += bytes;
	KKASSERT(off <= volume->maxbuf_off);
	KKASSERT((off & HAMMER_OFF_ZONE_MASK) == HAMMER_ZONE_RAW_BUFFER);
	if (off == volume->maxbuf_off) {
		vol_no = volume->vol_no + 1;
		if (vol_no == volume->hmp->nvolumes)
			vol_no = 0;
		off = HAMMER_ENCODE_RAW_BUFFER(vol_no, 0);
	}
	return(off);
}
#endif

/*
 * Sync dirty buffers to the media
 */

static int hammer_sync_scan1(struct mount *mp, struct vnode *vp, void *data);
static int hammer_sync_scan2(struct mount *mp, struct vnode *vp, void *data);

int
hammer_sync_hmp(hammer_mount_t hmp, int waitfor)
{
	struct hammer_sync_info info;

	info.error = 0;
	info.waitfor = waitfor;

	vmntvnodescan(hmp->mp, VMSC_GETVP|VMSC_NOWAIT,
		      hammer_sync_scan1, hammer_sync_scan2, &info);

	RB_SCAN(hammer_vol_rb_tree, &hmp->rb_vols_root, NULL,
		hammer_sync_volume, &info);
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
	error = VOP_FSYNC(vp, info->waitfor);
	if (error)
		info->error = error;
	return(0);
}

int
hammer_sync_volume(hammer_volume_t volume, void *data)
{
	struct hammer_sync_info *info = data;

	hammer_ref(&volume->io.lock);
	RB_SCAN(hammer_buf_rb_tree, &volume->rb_bufs_root, NULL,
		hammer_sync_buffer, info);
	hammer_rel_volume(volume, 1);
	return(0);
}

int
hammer_sync_buffer(hammer_buffer_t buffer, void *data __unused)
{
	hammer_ref(&buffer->io.lock);
	hammer_rel_buffer(buffer, 1);
	return(0);
}

#if 0
/*
 * Generic buffer initialization.  Initialize the A-list into an all-allocated
 * state with the free block limit properly set.
 *
 * Note that alloc_new_buffer() will free the appropriate block range via
 * the appropriate cluster alist, so the free count is properly propogated.
 */
void
hammer_init_fifo(hammer_fifo_head_t head, u_int16_t type)
{
	head->hdr_signature = HAMMER_HEAD_SIGNATURE;
	head->hdr_type = type;
	head->hdr_size = 0;
	head->hdr_crc = 0;
	head->hdr_seq = 0;
}

#endif

