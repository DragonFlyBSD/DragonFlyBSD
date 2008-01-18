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
 * $DragonFly: src/sys/vfs/hammer/hammer_ondisk.c,v 1.22 2008/01/18 07:02:41 dillon Exp $
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
static int hammer_load_supercl(hammer_supercl_t supercl,
			hammer_alloc_state_t isnew);
static int hammer_load_cluster(hammer_cluster_t cluster,
			hammer_alloc_state_t isnew);
static int hammer_load_buffer(hammer_buffer_t buffer, u_int64_t buf_type);
static int hammer_load_node(hammer_node_t node);
static void alloc_new_buffer(hammer_cluster_t cluster, u_int64_t type,
			hammer_alist_t live,
		        int32_t start, int *errorp,
			struct hammer_buffer **bufferp);
#if 0
static void readhammerbuf(hammer_volume_t vol, void *data,
			int64_t offset);
static void writehammerbuf(hammer_volume_t vol, const void *data,
			int64_t offset);
#endif
static int64_t calculate_cluster_offset(hammer_volume_t vol, int32_t clu_no);
static int64_t calculate_supercl_offset(hammer_volume_t vol, int32_t scl_no);
static int32_t hammer_alloc_master(hammer_cluster_t cluster, int nblks,
			int32_t start, int isfwd);
static void hammer_adjust_stats(hammer_cluster_t cluster,
			u_int64_t buf_type, int nblks);

struct hammer_alist_config Buf_alist_config;
struct hammer_alist_config Vol_normal_alist_config;
struct hammer_alist_config Vol_super_alist_config;
struct hammer_alist_config Supercl_alist_config;
struct hammer_alist_config Clu_master_alist_config;
struct hammer_alist_config Clu_slave_alist_config;

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
hammer_scl_rb_compare(hammer_supercl_t cl1, hammer_supercl_t cl2)
{
	if (cl1->scl_no < cl2->scl_no)
		return(-1);
	if (cl1->scl_no > cl2->scl_no)
		return(1);
	return(0);
}

static int
hammer_clu_rb_compare(hammer_cluster_t cl1, hammer_cluster_t cl2)
{
	if (cl1->clu_no < cl2->clu_no)
		return(-1);
	if (cl1->clu_no > cl2->clu_no)
		return(1);
	return(0);
}

static int
hammer_buf_rb_compare(hammer_buffer_t buf1, hammer_buffer_t buf2)
{
	if (buf1->buf_no < buf2->buf_no)
		return(-1);
	if (buf1->buf_no > buf2->buf_no)
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
 * functions are normal, e.g. hammer_clu_rb_tree_RB_LOOKUP(root, clu_no).
 */
RB_GENERATE(hammer_ino_rb_tree, hammer_inode, rb_node, hammer_ino_rb_compare);
RB_GENERATE_XLOOKUP(hammer_ino_rb_tree, INFO, hammer_inode, rb_node,
		hammer_inode_info_cmp, hammer_inode_info_t);
RB_GENERATE2(hammer_vol_rb_tree, hammer_volume, rb_node,
	     hammer_vol_rb_compare, int32_t, vol_no);
RB_GENERATE2(hammer_scl_rb_tree, hammer_supercl, rb_node,
	     hammer_scl_rb_compare, int32_t, scl_no);
RB_GENERATE2(hammer_clu_rb_tree, hammer_cluster, rb_node,
	     hammer_clu_rb_compare, int32_t, clu_no);
RB_GENERATE2(hammer_buf_rb_tree, hammer_buffer, rb_node,
	     hammer_buf_rb_compare, int32_t, buf_no);
RB_GENERATE2(hammer_nod_rb_tree, hammer_node, rb_node,
	     hammer_nod_rb_compare, int32_t, node_offset);

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
		vn_isdisk(volume->devvp, &error);
	}
	if (error == 0) {
		vn_lock(volume->devvp, LK_EXCLUSIVE | LK_RETRY);
		error = VOP_OPEN(volume->devvp, (ronly ? FREAD : FREAD|FWRITE),
				 FSCRED, NULL);
		vn_unlock(volume->devvp);
	}
	if (error) {
		hammer_free_volume(volume);
		return(error);
	}

	/*
	 * Extract the volume number from the volume header and do various
	 * sanity checks.
	 */
	error = bread(volume->devvp, 0LL, HAMMER_BUFSIZE, &bp);
	if (error)
		goto late_failure;
	ondisk = (void *)bp->b_data;
	if (ondisk->head.buf_type != HAMMER_FSBUF_VOLUME) {
		kprintf("hammer_mount: volume %s has an invalid header\n",
			volume->vol_name);
		error = EFTYPE;
		goto late_failure;
	}
	volume->vol_no = ondisk->vol_no;
	volume->cluster_base = ondisk->vol_clo_beg;
	volume->vol_clsize = ondisk->vol_clsize;
	volume->vol_flags = ondisk->vol_flags;
	volume->nblocks = ondisk->vol_nblocks; 
	RB_INIT(&volume->rb_clus_root);
	RB_INIT(&volume->rb_scls_root);

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
	 * Set the root volume and load the root cluster.  HAMMER special
	 * cases rootvol and rootcl and will not deallocate the structures.
	 * We do not hold a ref because this would prevent related I/O
	 * from being flushed.
	 */
	if (error == 0 && ondisk->vol_rootvol == ondisk->vol_no) {
		hmp->rootvol = volume;
		if (bp) {
			brelse(bp);
			bp = NULL;
		}
		hammer_ref_volume(volume);
		hmp->rootcl = hammer_get_cluster(volume,
						 ondisk->vol0_root_clu_no,
						 &error, 0);
		hammer_rel_cluster(hmp->rootcl, 0);
		hammer_rel_volume(volume, 0);
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
	hammer_cluster_t rootcl;
	int ronly = ((hmp->mp->mnt_flag & MNT_RDONLY) ? 1 : 0);

	/*
	 * Sync clusters, sync volume
	 */

	hmp->mp->mnt_stat.f_blocks -= volume->nblocks;

	/*
	 * Clean up the root cluster, which is held unlocked in the root
	 * volume.
	 */
	if (hmp->rootvol == volume) {
		if ((rootcl = hmp->rootcl) != NULL)
			hmp->rootcl = NULL;
		hmp->rootvol = NULL;
	}

	/*
	 * Unload clusters and super-clusters.  Unloading a super-cluster
	 * also unloads related clusters, but the filesystem may not be
	 * using super-clusters so unload clusters anyway.
	 */
	RB_SCAN(hammer_clu_rb_tree, &volume->rb_clus_root, NULL,
			hammer_unload_cluster, NULL);
	RB_SCAN(hammer_scl_rb_tree, &volume->rb_scls_root, NULL,
			hammer_unload_supercl, NULL);
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
	KKASSERT(RB_EMPTY(&volume->rb_clus_root));
	KKASSERT(RB_EMPTY(&volume->rb_scls_root));

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
	if (volume->ondisk == NULL) {
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
	if (volume->ondisk == NULL) {
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
	if (volume->ondisk == NULL) {
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
	if (volume->ondisk == NULL) {
		error = hammer_io_read(volume->devvp, &volume->io);
		if (error) {
			hammer_unlock(&volume->io.lock);
			return (error);
		}
		volume->ondisk = ondisk = (void *)volume->io.bp->b_data;

		/*
		 * Configure the volume's A-lists.  These are used to
		 * allocate clusters.
		 */
		if (volume->vol_flags & HAMMER_VOLF_USINGSUPERCL) {
			volume->alist.config = &Vol_super_alist_config;
			volume->alist.meta = ondisk->vol_almeta.super;
			volume->alist.info = volume;
		} else {
			volume->alist.config = &Vol_normal_alist_config;
			volume->alist.meta = ondisk->vol_almeta.normal;
			volume->alist.info = NULL;
		}
	} else {
		error = 0;
	}
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
 *				SUPER-CLUSTERS				*
 ************************************************************************
 *
 * Manage super-clusters.  Note that a supercl holds a reference to its
 * associated volume.
 */
static int
hammer_find_supercl(hammer_volume_t volume, int32_t scl_no)
{
	if (RB_LOOKUP(hammer_scl_rb_tree, &volume->rb_scls_root, scl_no))
		return(1);
	return(0);
}

hammer_supercl_t
hammer_get_supercl(hammer_volume_t volume, int32_t scl_no,
		   int *errorp, hammer_alloc_state_t isnew)
{
	hammer_supercl_t supercl;

	/*
	 * Locate and lock the super-cluster structure, creating one
	 * if necessary.
	 */
again:
	supercl = RB_LOOKUP(hammer_scl_rb_tree, &volume->rb_scls_root, scl_no);
	if (supercl == NULL) {
		++hammer_count_supercls;
		supercl = kmalloc(sizeof(*supercl), M_HAMMER, M_WAITOK|M_ZERO);
		supercl->scl_no = scl_no;
		supercl->volume = volume;
		supercl->io.offset = calculate_supercl_offset(volume, scl_no);
		hammer_io_init(&supercl->io, HAMMER_STRUCTURE_SUPERCL);
		hammer_ref(&supercl->io.lock);

		/*
		 * Insert the cluster into the RB tree and handle late
		 * collisions.
		 */
		if (RB_INSERT(hammer_scl_rb_tree, &volume->rb_scls_root, supercl)) {
			hammer_unref(&supercl->io.lock);
			--hammer_count_supercls;
			kfree(supercl, M_HAMMER);
			goto again;
		}
		hammer_ref(&volume->io.lock);
	} else {
		hammer_ref(&supercl->io.lock);
	}

	/*
	 * Deal with on-disk info
	 */
	if (supercl->ondisk == NULL || isnew) {
		*errorp = hammer_load_supercl(supercl, isnew);
		if (*errorp) {
			hammer_rel_supercl(supercl, 1);
			supercl = NULL;
		}
	} else {
		*errorp = 0;
	}
	return(supercl);
}

static int
hammer_load_supercl(hammer_supercl_t supercl, hammer_alloc_state_t isnew)
{
	struct hammer_supercl_ondisk *ondisk;
	hammer_volume_t volume = supercl->volume;
	int error;
	int64_t nclusters;

	hammer_lock_ex(&supercl->io.lock);
	if (supercl->ondisk == NULL) {
		if (isnew)
			error = hammer_io_new(volume->devvp, &supercl->io);
		else
			error = hammer_io_read(volume->devvp, &supercl->io);
		if (error) {
			hammer_unlock(&supercl->io.lock);
			return (error);
		}
		supercl->ondisk = ondisk = (void *)supercl->io.bp->b_data;

		supercl->alist.config = &Supercl_alist_config;
		supercl->alist.meta = ondisk->scl_meta;
		supercl->alist.info = NULL;
	} else if (isnew) {
		error = hammer_io_new(volume->devvp, &supercl->io);
	} else {
		error = 0;
	}
	if (error == 0 && isnew) {
		/*
		 * If this is a new super-cluster we have to initialize
		 * various ondisk structural elements.  The caller is
		 * responsible for the remainder.
		 */
		struct hammer_alist_live dummy;

		hammer_modify_supercl(supercl);

		ondisk = supercl->ondisk;
		dummy.config = &Buf_alist_config;
		dummy.meta = ondisk->head.buf_almeta;
		dummy.info = NULL;
		hammer_initbuffer(&dummy, &ondisk->head, HAMMER_FSBUF_SUPERCL);

		nclusters = volume->ondisk->vol_nclusters -
			    ((int64_t)supercl->scl_no * HAMMER_SCL_MAXCLUSTERS);
		KKASSERT(nclusters > 0);
		if (nclusters > HAMMER_SCL_MAXCLUSTERS)
			nclusters = HAMMER_SCL_MAXCLUSTERS;
		hammer_alist_init(&supercl->alist, 0, (int32_t)nclusters,
				  isnew);
	}
	hammer_unlock(&supercl->io.lock);
	return (error);
}

/*
 * NOTE: Called from RB_SCAN, must return >= 0 for scan to continue.
 */
int
hammer_unload_supercl(hammer_supercl_t supercl, void *data __unused)
{
	KKASSERT(supercl->io.lock.refs == 0);
	hammer_ref(&supercl->io.lock);
	hammer_rel_supercl(supercl, 2);
	return(0);
}

/*
 * Release a super-cluster.  We have to deal with several places where
 * another thread can ref the super-cluster.
 *
 * Only destroy the structure itself if the related buffer cache buffer
 * was disassociated from it.  This ties the management of the structure
 * to the buffer cache subsystem.
 */
void
hammer_rel_supercl(hammer_supercl_t supercl, int flush)
{
	hammer_volume_t volume;

	if (supercl->io.lock.refs == 1) {
		hammer_lock_ex(&supercl->io.lock);
		if (supercl->io.lock.refs == 1) {
			hammer_io_release(&supercl->io, flush);
			if (supercl->io.bp == NULL &&
			    supercl->io.lock.refs == 1) {
				volume = supercl->volume;
				RB_REMOVE(hammer_scl_rb_tree,
					  &volume->rb_scls_root, supercl);
				supercl->volume = NULL;	/* sanity */
				--hammer_count_supercls;
				kfree(supercl, M_HAMMER);
				hammer_rel_volume(volume, 0);
				return;
			}
		} else if (flush) {
			hammer_io_flush(&supercl->io);
		}
		hammer_unlock(&supercl->io.lock);
	}
	hammer_unref(&supercl->io.lock);
}

/************************************************************************
 *				CLUSTERS				*
 ************************************************************************
 *
 */
hammer_cluster_t
hammer_get_cluster(hammer_volume_t volume, int32_t clu_no,
		   int *errorp, hammer_alloc_state_t isnew)
{
	hammer_cluster_t cluster;

again:
	cluster = RB_LOOKUP(hammer_clu_rb_tree, &volume->rb_clus_root, clu_no);
	if (cluster == NULL) {
		++hammer_count_clusters;
		cluster = kmalloc(sizeof(*cluster), M_HAMMER, M_WAITOK|M_ZERO);
		cluster->clu_no = clu_no;
		cluster->volume = volume;
		RB_INIT(&cluster->rb_bufs_root);
		RB_INIT(&cluster->rb_nods_root);
		hammer_io_init(&cluster->io, HAMMER_STRUCTURE_CLUSTER);
		cluster->io.offset = calculate_cluster_offset(volume, clu_no);
		hammer_ref(&cluster->io.lock);

		/*
		 * Insert the cluster into the RB tree and handle late
		 * collisions.
		 */
		if (RB_INSERT(hammer_clu_rb_tree, &volume->rb_clus_root, cluster)) {
			hammer_unref(&cluster->io.lock);
			--hammer_count_clusters;
			kfree(cluster, M_HAMMER);
			goto again;
		}
		hammer_ref(&volume->io.lock);
	} else {
		hammer_ref(&cluster->io.lock);
	}

	/*
	 * Deal with on-disk info
	 */
	if (cluster->ondisk == NULL || isnew) {
		*errorp = hammer_load_cluster(cluster, isnew);
		if (*errorp) {
			hammer_rel_cluster(cluster, 1);
			cluster = NULL;
		}
	} else {
		*errorp = 0;
	}
	return (cluster);
}

hammer_cluster_t
hammer_get_root_cluster(struct hammer_mount *hmp, int *errorp)
{
	hammer_cluster_t cluster;

	cluster = hmp->rootcl;
	KKASSERT(cluster != NULL);
	hammer_ref(&cluster->io.lock);

	/*
	 * Deal with on-disk info
	 */
	if (cluster->ondisk == NULL) {
		*errorp = hammer_load_cluster(cluster, 0);
		if (*errorp) {
			hammer_rel_cluster(cluster, 1);
			cluster = NULL;
		}
	} else {
		*errorp = 0;
	}
	return (cluster);
}

static
int
hammer_load_cluster(hammer_cluster_t cluster, hammer_alloc_state_t isnew)
{
	hammer_volume_t volume = cluster->volume;
	struct hammer_cluster_ondisk *ondisk;
	int error;

	/*
	 * Load the cluster's on-disk info
	 */
	hammer_lock_ex(&cluster->io.lock);
	if (cluster->ondisk == NULL) {
		if (isnew)
			error = hammer_io_new(volume->devvp, &cluster->io);
		else
			error = hammer_io_read(volume->devvp, &cluster->io);
		if (error) {
			hammer_unlock(&cluster->io.lock);
			return (error);
		}
		cluster->ondisk = ondisk = (void *)cluster->io.bp->b_data;

		cluster->alist_master.config = &Clu_master_alist_config;
		cluster->alist_master.meta = ondisk->clu_master_meta;
		cluster->alist_btree.config = &Clu_slave_alist_config;
		cluster->alist_btree.meta = ondisk->clu_btree_meta;
		cluster->alist_btree.info = cluster;
		cluster->alist_record.config = &Clu_slave_alist_config;
		cluster->alist_record.meta = ondisk->clu_record_meta;
		cluster->alist_record.info = cluster;
		cluster->alist_mdata.config = &Clu_slave_alist_config;
		cluster->alist_mdata.meta = ondisk->clu_mdata_meta;
		cluster->alist_mdata.info = cluster;

		if (isnew == 0) {
			/*
			 * Load cluster range info for easy access
			 */
			cluster->clu_btree_beg = ondisk->clu_btree_beg;
			cluster->clu_btree_end = ondisk->clu_btree_end;

			/*
			 * Recover a cluster that was marked open.  This
			 * can be rather involved and block for a hefty
			 * chunk of time.
			 */
			/*if (ondisk->clu_flags & HAMMER_CLUF_OPEN)*/
				hammer_recover(cluster);
		}
	} else if (isnew) {
		error = hammer_io_new(volume->devvp, &cluster->io);
	} else {
		error = 0;
	}
	if (error == 0 && isnew) {
		/*
		 * If this is a new cluster we have to initialize
		 * various ondisk structural elements.  The caller is
		 * responsible for the remainder.
		 */
		struct hammer_alist_live dummy;
		hammer_node_t croot;
		hammer_volume_ondisk_t voldisk;
		int32_t nbuffers;

		hammer_modify_cluster(cluster);
		ondisk = cluster->ondisk;
		voldisk = volume->ondisk;

		dummy.config = &Buf_alist_config;
		dummy.meta = ondisk->head.buf_almeta;
		dummy.info = NULL;
		hammer_initbuffer(&dummy, &ondisk->head, HAMMER_FSBUF_CLUSTER);

		ondisk->vol_fsid = voldisk->vol_fsid;
		ondisk->vol_fstype = voldisk->vol_fstype;
		ondisk->clu_gen = 1;
		ondisk->clu_id = 0;	/* XXX */
		ondisk->clu_no = cluster->clu_no;
		ondisk->clu_flags = 0;
		ondisk->clu_start = HAMMER_BUFSIZE;
		KKASSERT(voldisk->vol_clo_end > cluster->io.offset);
		if (voldisk->vol_clo_end - cluster->io.offset >
		    voldisk->vol_clsize) {
			ondisk->clu_limit = voldisk->vol_clsize;
		} else {
			ondisk->clu_limit = (int32_t)(voldisk->vol_clo_end -
						      cluster->io.offset);
		}
		nbuffers = ondisk->clu_limit / HAMMER_BUFSIZE;
		KKASSERT(isnew == HAMMER_ASTATE_FREE);
		hammer_alist_init(&cluster->alist_master, 1, nbuffers - 1,
				  HAMMER_ASTATE_FREE);
		hammer_alist_init(&cluster->alist_btree,
				  HAMMER_FSBUF_MAXBLKS,
				  (nbuffers - 1) * HAMMER_FSBUF_MAXBLKS,
				  HAMMER_ASTATE_ALLOC);
		hammer_alist_init(&cluster->alist_record,
				  HAMMER_FSBUF_MAXBLKS,
				  (nbuffers - 1) * HAMMER_FSBUF_MAXBLKS,
				  HAMMER_ASTATE_ALLOC);
		hammer_alist_init(&cluster->alist_mdata,
				  HAMMER_FSBUF_MAXBLKS,
				  (nbuffers - 1) * HAMMER_FSBUF_MAXBLKS,
				  HAMMER_ASTATE_ALLOC);

		ondisk->idx_data = 1 * HAMMER_FSBUF_MAXBLKS;
		ondisk->idx_index = 0 * HAMMER_FSBUF_MAXBLKS;
		ondisk->idx_record = nbuffers * HAMMER_FSBUF_MAXBLKS;

		/*
		 * Initialize the B-Tree.  We don't know what the caller
		 * intends to do with the cluster so make sure it causes
		 * an assertion if the caller makes no changes.
		 */
		ondisk->clu_btree_parent_vol_no = -2;
		ondisk->clu_btree_parent_clu_no = -2;
		ondisk->clu_btree_parent_offset = -2;
		ondisk->clu_btree_parent_clu_gen = -2;

		croot = hammer_alloc_btree(cluster, &error);
		if (error == 0) {
			hammer_modify_node(croot);
			bzero(croot->ondisk, sizeof(*croot->ondisk));
			croot->ondisk->count = 0;
			croot->ondisk->type = HAMMER_BTREE_TYPE_LEAF;
			hammer_modify_cluster(cluster);
			ondisk->clu_btree_root = croot->node_offset;
			hammer_rel_node(croot);
		}
	}
	hammer_unlock(&cluster->io.lock);
	return (error);
}

/*
 * NOTE: Called from RB_SCAN, must return >= 0 for scan to continue.
 */
int
hammer_unload_cluster(hammer_cluster_t cluster, void *data __unused)
{
	hammer_ref(&cluster->io.lock);
	RB_SCAN(hammer_buf_rb_tree, &cluster->rb_bufs_root, NULL,
		hammer_unload_buffer, NULL);
	hammer_io_waitdep(&cluster->io);
	KKASSERT(cluster->io.lock.refs == 1);
	hammer_rel_cluster(cluster, 2);
	return(0);
}

/*
 * Update the cluster's synchronization TID, which is used during cluster
 * recovery.  NOTE: The cluster header is not written out until all related
 * records have been written out.
 */
void
hammer_update_syncid(hammer_cluster_t cluster, hammer_tid_t tid)
{
	hammer_modify_cluster(cluster);
	if (cluster->ondisk->synchronized_tid < tid)
		cluster->ondisk->synchronized_tid = tid;
}

/*
 * Reference a cluster that is either already referenced or via a specially
 * handled pointer (aka rootcl).
 */
int
hammer_ref_cluster(hammer_cluster_t cluster)
{
	int error;

	KKASSERT(cluster != NULL);
	hammer_ref(&cluster->io.lock);

	/*
	 * Deal with on-disk info
	 */
	if (cluster->ondisk == NULL) {
		error = hammer_load_cluster(cluster, 0);
		if (error)
			hammer_rel_cluster(cluster, 1);
	} else {
		error = 0;
	}
	return(error);
}

/*
 * Release a cluster.  We have to deal with several places where
 * another thread can ref the cluster.
 *
 * Only destroy the structure itself if we no longer have an IO or any
 * hammer buffers associated with the structure.
 */
void
hammer_rel_cluster(hammer_cluster_t cluster, int flush)
{
	hammer_volume_t volume;

	if (cluster->io.lock.refs == 1) {
		hammer_lock_ex(&cluster->io.lock);
		if (cluster->io.lock.refs == 1) {
			/*
			 * Release the I/O.  If we or the kernel wants to
			 * flush, this will release the bp.  Otherwise the
			 * bp may be written and flushed passively by the
			 * kernel later on.
			 */
			hammer_io_release(&cluster->io, flush);

			/*
			 * Final cleanup
			 */
			if (cluster != cluster->volume->hmp->rootcl &&
			    cluster->io.bp == NULL &&
			    cluster->io.lock.refs == 1 &&
			    RB_EMPTY(&cluster->rb_bufs_root)) {
				KKASSERT(RB_EMPTY(&cluster->rb_nods_root));
				volume = cluster->volume;
				RB_REMOVE(hammer_clu_rb_tree,
					  &volume->rb_clus_root, cluster);
				cluster->volume = NULL;	/* sanity */
				--hammer_count_clusters;
				kfree(cluster, M_HAMMER);
				hammer_rel_volume(volume, 0);
				return;
			}
		} else if (flush) {
			hammer_io_flush(&cluster->io);
		}
		hammer_unlock(&cluster->io.lock);
	}
	hammer_unref(&cluster->io.lock);
}

/************************************************************************
 *				BUFFERS					*
 ************************************************************************
 *
 * Manage buffers.  Note that a buffer holds a reference to its associated
 * cluster, and its cluster will hold a reference to the cluster's volume.
 *
 * A non-zero buf_type indicates that a new buffer should be created and
 * zero'd.
 */
hammer_buffer_t
hammer_get_buffer(hammer_cluster_t cluster, int32_t buf_no,
		  u_int64_t buf_type, int *errorp)
{
	hammer_buffer_t buffer;

	/*
	 * Find the buffer.  Note that buffer 0 corresponds to the cluster
	 * header and should never be requested.
	 */
	KKASSERT(buf_no >= cluster->ondisk->clu_start / HAMMER_BUFSIZE &&
		 buf_no < cluster->ondisk->clu_limit / HAMMER_BUFSIZE);

	/*
	 * Locate and lock the buffer structure, creating one if necessary.
	 */
again:
	buffer = RB_LOOKUP(hammer_buf_rb_tree, &cluster->rb_bufs_root, buf_no);
	if (buffer == NULL) {
		++hammer_count_buffers;
		buffer = kmalloc(sizeof(*buffer), M_HAMMER, M_WAITOK|M_ZERO);
		buffer->buf_no = buf_no;
		buffer->cluster = cluster;
		buffer->volume = cluster->volume;
		hammer_io_init(&buffer->io, HAMMER_STRUCTURE_BUFFER);
		buffer->io.offset = cluster->io.offset +
				    (buf_no * HAMMER_BUFSIZE);
		TAILQ_INIT(&buffer->clist);
		hammer_ref(&buffer->io.lock);

		/*
		 * Insert the cluster into the RB tree and handle late
		 * collisions.
		 */
		if (RB_INSERT(hammer_buf_rb_tree, &cluster->rb_bufs_root, buffer)) {
			hammer_unref(&buffer->io.lock);
			--hammer_count_buffers;
			kfree(buffer, M_HAMMER);
			goto again;
		}
		hammer_ref(&cluster->io.lock);
	} else {
		hammer_ref(&buffer->io.lock);
	}

	/*
	 * Deal with on-disk info
	 */
	if (buffer->ondisk == NULL || buf_type) {
		*errorp = hammer_load_buffer(buffer, buf_type);
		if (*errorp) {
			hammer_rel_buffer(buffer, 1);
			buffer = NULL;
		}
	} else {
		*errorp = 0;
	}
	return(buffer);
}

static int
hammer_load_buffer(hammer_buffer_t buffer, u_int64_t buf_type)
{
	hammer_volume_t volume;
	hammer_fsbuf_ondisk_t ondisk;
	int error;

	/*
	 * Load the buffer's on-disk info
	 */
	volume = buffer->volume;
	hammer_lock_ex(&buffer->io.lock);
	if (buffer->ondisk == NULL) {
		if (buf_type) {
			error = hammer_io_new(volume->devvp, &buffer->io);
		} else {
			error = hammer_io_read(volume->devvp, &buffer->io);
		}
		if (error) {
			hammer_unlock(&buffer->io.lock);
			return (error);
		}
		buffer->ondisk = ondisk = (void *)buffer->io.bp->b_data;
		buffer->alist.config = &Buf_alist_config;
		buffer->alist.meta = ondisk->head.buf_almeta;
		buffer->buf_type = ondisk->head.buf_type;
	} else if (buf_type) {
		error = hammer_io_new(volume->devvp, &buffer->io);
	} else {
		error = 0;
	}
	if (error == 0 && buf_type) {
		hammer_modify_buffer(buffer);
		ondisk = buffer->ondisk;
		hammer_initbuffer(&buffer->alist, &ondisk->head, buf_type);
		buffer->buf_type = ondisk->head.buf_type;
	}
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
	if (buffer->ondisk == NULL) {
		error = hammer_load_buffer(buffer, 0);
		if (error) {
			hammer_rel_buffer(buffer, 1);
			/*
			 * NOTE: buffer pointer can become stale after
			 * the above release.
			 */
		} else {
			KKASSERT(buffer->buf_type ==
				 buffer->ondisk->head.buf_type);
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
	hammer_cluster_t cluster;

	if (buffer->io.lock.refs == 1) {
		hammer_lock_ex(&buffer->io.lock);
		if (buffer->io.lock.refs == 1) {
			hammer_io_release(&buffer->io, flush);

			if (buffer->io.bp == NULL &&
			    buffer->io.lock.refs == 1) {
				hammer_flush_buffer_nodes(buffer);
				KKASSERT(TAILQ_EMPTY(&buffer->clist));
				cluster = buffer->cluster;
				RB_REMOVE(hammer_buf_rb_tree,
					  &cluster->rb_bufs_root, buffer);
				buffer->cluster = NULL; /* sanity */
				--hammer_count_buffers;
				kfree(buffer, M_HAMMER);
				hammer_rel_cluster(cluster, 0);
				return;
			}
		} else if (flush) {
			hammer_io_flush(&buffer->io);
		}
		hammer_unlock(&buffer->io.lock);
	}
	hammer_unref(&buffer->io.lock);
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
hammer_get_node(hammer_cluster_t cluster, int32_t node_offset, int *errorp)
{
	hammer_node_t node;

	/*
	 * Locate the structure, allocating one if necessary.
	 */
again:
	node = RB_LOOKUP(hammer_nod_rb_tree, &cluster->rb_nods_root,
			 node_offset);
	if (node == NULL) {
		++hammer_count_nodes;
		node = kmalloc(sizeof(*node), M_HAMMER, M_WAITOK|M_ZERO);
		node->node_offset = node_offset;
		node->cluster = cluster;
		if (RB_INSERT(hammer_nod_rb_tree, &cluster->rb_nods_root,
			      node)) {
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
	int32_t buf_no;
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
			buf_no = node->node_offset / HAMMER_BUFSIZE;
			buffer = hammer_get_buffer(node->cluster,
						   buf_no, 0, &error);
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
	hammer_cluster_t cluster;
	hammer_buffer_t buffer;
	int32_t node_offset;
	int flags;

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
	 * Destroy the node.  Record pertainant data because the node
	 * becomes stale the instant we flush it.
	 */
	flags = node->flags;
	node_offset = node->node_offset;
	hammer_unref(&node->lock);
	hammer_flush_node(node);
	/* node is stale */

	cluster = buffer->cluster;
	if (flags & HAMMER_NODE_DELETED) {
		hammer_free_btree(cluster, node_offset);
		if (node_offset == cluster->ondisk->clu_btree_root) {
			kprintf("FREE CLUSTER %d\n", cluster->clu_no);
			hammer_free_cluster(cluster);
			/*hammer_io_undirty(&cluster->io);*/
		}
	}
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
		RB_REMOVE(hammer_nod_rb_tree, &node->cluster->rb_nods_root,
			  node);
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
 *				A-LIST ALLOCATORS			*
 ************************************************************************/

/*
 * Allocate HAMMER clusters
 */
hammer_cluster_t
hammer_alloc_cluster(hammer_mount_t hmp, hammer_cluster_t cluster_hint,
		     int *errorp)
{
	hammer_volume_t volume;
	hammer_cluster_t cluster;
	int32_t clu_no;
	int32_t clu_hint;
	int32_t vol_beg;
	int32_t vol_no;

	/*
	 * Figure out our starting volume and hint.
	 */
	if (cluster_hint) {
		vol_beg = cluster_hint->volume->vol_no;
		clu_hint = cluster_hint->clu_no;
	} else {
		vol_beg = hmp->volume_iterator;
		clu_hint = -1;
	}

	/*
	 * Loop through volumes looking for a free cluster.  If allocating
	 * a new cluster relative to an existing cluster try to find a free
	 * cluster on either side (clu_hint >= 0), otherwise just do a
	 * forwards iteration.
	 */
	vol_no = vol_beg;
	do {
		volume = hammer_get_volume(hmp, vol_no, errorp);
		kprintf("VOLUME %p %d\n", volume, vol_no);
		if (*errorp) {
			clu_no = HAMMER_ALIST_BLOCK_NONE;
			break;
		}
		hammer_modify_volume(volume);
		if (clu_hint == -1) {
			clu_hint = volume->clu_iterator;
			clu_no = hammer_alist_alloc_fwd(&volume->alist, 1,
							clu_hint);
			if (clu_no == HAMMER_ALIST_BLOCK_NONE) {
				clu_no = hammer_alist_alloc_fwd(&volume->alist,
								1, 0);
			}
		} else {
			clu_no = hammer_alist_alloc_fwd(&volume->alist, 1,
							clu_hint);
			if (clu_no == HAMMER_ALIST_BLOCK_NONE) {
				clu_no = hammer_alist_alloc_rev(&volume->alist,
								1, clu_hint);
			}
		}
		if (clu_no != HAMMER_ALIST_BLOCK_NONE)
			break;
		hammer_rel_volume(volume, 0);
		volume = NULL;
		*errorp = ENOSPC;
		vol_no = (vol_no + 1) % hmp->nvolumes;
		clu_hint = -1;
	} while (vol_no != vol_beg);

	/*
	 * Acquire the cluster.  On success this will force *errorp to 0.
	 */
	if (clu_no != HAMMER_ALIST_BLOCK_NONE) {
		kprintf("ALLOC CLUSTER %d:%d\n", volume->vol_no, clu_no);
		cluster = hammer_get_cluster(volume, clu_no, errorp,
					     HAMMER_ASTATE_FREE);
		volume->clu_iterator = clu_no;
		hammer_rel_volume(volume, 0);
	} else {
		cluster = NULL;
	}
	if (cluster)
		hammer_lock_ex(&cluster->io.lock);
	return(cluster);
}

void
hammer_init_cluster(hammer_cluster_t cluster, hammer_base_elm_t left_bound, 
		    hammer_base_elm_t right_bound)
{
	hammer_cluster_ondisk_t ondisk = cluster->ondisk;

	hammer_modify_cluster(cluster);
	ondisk->clu_btree_beg = *left_bound;
	ondisk->clu_btree_end = *right_bound;
	cluster->clu_btree_beg = ondisk->clu_btree_beg;
	cluster->clu_btree_end = ondisk->clu_btree_end;
}

/*
 * Deallocate a cluster
 */
void
hammer_free_cluster(hammer_cluster_t cluster)
{
	hammer_modify_cluster(cluster);
	hammer_alist_free(&cluster->volume->alist, cluster->clu_no, 1);
}

/*
 * Allocate HAMMER elements - btree nodes, data storage, and record elements
 *
 * The passed *bufferp should be initialized to NULL.  On successive calls
 * *bufferp caches the most recent buffer used until put away by the caller.
 * Note that previously returned pointers using the cached buffer become
 * invalid on successive calls which reuse *bufferp.
 *
 * All allocations first attempt to use the block found at the specified
 * iterator.  If that fails the first available block is used.  If that
 * fails a new buffer is allocated and associated with the buffer type
 * A-list and the element is allocated out of the new buffer.
 */

hammer_node_t
hammer_alloc_btree(hammer_cluster_t cluster, int *errorp)
{
	hammer_buffer_t buffer;
	hammer_alist_t live;
	hammer_node_t node;
	int32_t elm_no;
	int32_t buf_no;
	int32_t node_offset;

	/*
	 * Allocate a B-Tree element
	 */
	hammer_modify_cluster(cluster);
	buffer = NULL;
	live = &cluster->alist_btree;
	elm_no = hammer_alist_alloc_fwd(live, 1, cluster->ondisk->idx_index);
	if (elm_no == HAMMER_ALIST_BLOCK_NONE)
		elm_no = hammer_alist_alloc_fwd(live, 1, 0);
	if (elm_no == HAMMER_ALIST_BLOCK_NONE) {
		alloc_new_buffer(cluster, HAMMER_FSBUF_BTREE, live,
				 cluster->ondisk->idx_index, errorp, &buffer);
		elm_no = hammer_alist_alloc(live, 1);
		if (elm_no == HAMMER_ALIST_BLOCK_NONE) {
			*errorp = ENOSPC;
			if (buffer)
				hammer_rel_buffer(buffer, 0);
			return(NULL);
		}
	}
	cluster->ondisk->idx_index = elm_no;
	KKASSERT((elm_no & HAMMER_FSBUF_BLKMASK) < HAMMER_BTREE_NODES);

	/*
	 * Load and return the B-Tree element
	 */
	buf_no = elm_no / HAMMER_FSBUF_MAXBLKS;
	node_offset = buf_no * HAMMER_BUFSIZE +
		      offsetof(union hammer_fsbuf_ondisk,
			       btree.nodes[elm_no & HAMMER_FSBUF_BLKMASK]);
	node = hammer_get_node(cluster, node_offset, errorp);
	if (node) {
		hammer_modify_node(node);
		bzero(node->ondisk, sizeof(*node->ondisk));
	} else {
		hammer_alist_free(live, elm_no, 1);
		hammer_rel_node(node);
		node = NULL;
	}
	if (buffer)
		hammer_rel_buffer(buffer, 0);
	return(node);
}

void *
hammer_alloc_data(hammer_cluster_t cluster, int32_t bytes,
		  int *errorp, struct hammer_buffer **bufferp)
{
	hammer_buffer_t buffer;
	hammer_alist_t live;
	int32_t elm_no;
	int32_t buf_no;
	int32_t nblks;
	void *item;

	/*
	 * Deal with large data blocks.  The blocksize is HAMMER_BUFSIZE
	 * for these allocations.
	 */
	hammer_modify_cluster(cluster);
	if ((bytes & HAMMER_BUFMASK) == 0) {
		nblks = bytes / HAMMER_BUFSIZE;
		/* only one block allowed for now (so buffer can hold it) */
		KKASSERT(nblks == 1);

		buf_no = hammer_alloc_master(cluster, nblks,
					     cluster->ondisk->idx_ldata, 1);
		if (buf_no == HAMMER_ALIST_BLOCK_NONE) {
			*errorp = ENOSPC;
			return(NULL);
		}
		hammer_adjust_stats(cluster, HAMMER_FSBUF_DATA, nblks);
		cluster->ondisk->idx_ldata = buf_no;
		buffer = *bufferp;
		*bufferp = hammer_get_buffer(cluster, buf_no, -1, errorp);
		if (buffer)
			hammer_rel_buffer(buffer, 0);
		buffer = *bufferp;
		return(buffer->ondisk);
	}

	/*
	 * Allocate a data element.  The block size is HAMMER_DATA_BLKSIZE
	 * (64 bytes) for these allocations.
	 */
	nblks = (bytes + HAMMER_DATA_BLKMASK) & ~HAMMER_DATA_BLKMASK;
	nblks /= HAMMER_DATA_BLKSIZE;
	live = &cluster->alist_mdata;
	elm_no = hammer_alist_alloc_fwd(live, nblks, cluster->ondisk->idx_data);
	if (elm_no == HAMMER_ALIST_BLOCK_NONE)
		elm_no = hammer_alist_alloc_fwd(live, nblks, 0);
	if (elm_no == HAMMER_ALIST_BLOCK_NONE) {
		alloc_new_buffer(cluster, HAMMER_FSBUF_DATA, live,
				 cluster->ondisk->idx_data, errorp, bufferp);
		elm_no = hammer_alist_alloc(live, nblks);
		if (elm_no == HAMMER_ALIST_BLOCK_NONE) {
			*errorp = ENOSPC;
			return(NULL);
		}
	}
	cluster->ondisk->idx_index = elm_no;

	/*
	 * Load and return the B-Tree element
	 */
	buf_no = elm_no / HAMMER_FSBUF_MAXBLKS;
	buffer = *bufferp;
	if (buffer == NULL || buffer->cluster != cluster ||
	    buffer->buf_no != buf_no) {
		if (buffer)
			hammer_rel_buffer(buffer, 0);
		buffer = hammer_get_buffer(cluster, buf_no, 0, errorp);
		*bufferp = buffer;
	}
	KKASSERT(buffer->ondisk->head.buf_type == HAMMER_FSBUF_DATA);
	KKASSERT((elm_no & HAMMER_FSBUF_BLKMASK) < HAMMER_DATA_NODES);
	hammer_modify_buffer(buffer);
	item = &buffer->ondisk->data.data[elm_no & HAMMER_FSBUF_BLKMASK];
	bzero(item, nblks * HAMMER_DATA_BLKSIZE);
	*errorp = 0;
	return(item);
}

void *
hammer_alloc_record(hammer_cluster_t cluster,
		    int *errorp, struct hammer_buffer **bufferp)
{
	hammer_buffer_t buffer;
	hammer_alist_t live;
	int32_t elm_no;
	int32_t buf_no;
	void *item;

	/*
	 * Allocate a record element
	 */
	hammer_modify_cluster(cluster);
	live = &cluster->alist_record;
	elm_no = hammer_alist_alloc_rev(live, 1, cluster->ondisk->idx_record);
	if (elm_no == HAMMER_ALIST_BLOCK_NONE)
		elm_no = hammer_alist_alloc_rev(live, 1,HAMMER_ALIST_BLOCK_MAX);
	if (elm_no == HAMMER_ALIST_BLOCK_NONE) {
		alloc_new_buffer(cluster, HAMMER_FSBUF_RECORDS, live,
				 cluster->ondisk->idx_record, errorp, bufferp);
		elm_no = hammer_alist_alloc_rev(live, 1,HAMMER_ALIST_BLOCK_MAX);
		kprintf("hammer_alloc_record elm again %08x\n", elm_no);
		if (elm_no == HAMMER_ALIST_BLOCK_NONE) {
			*errorp = ENOSPC;
			return(NULL);
		}
	}
	cluster->ondisk->idx_record = elm_no;

	/*
	 * Load and return the record element
	 */
	buf_no = elm_no / HAMMER_FSBUF_MAXBLKS;
	buffer = *bufferp;
	if (buffer == NULL || buffer->cluster != cluster ||
	    buffer->buf_no != buf_no) {
		if (buffer)
			hammer_rel_buffer(buffer, 0);
		buffer = hammer_get_buffer(cluster, buf_no, 0, errorp);
		*bufferp = buffer;
	}
	KKASSERT(buffer->ondisk->head.buf_type == HAMMER_FSBUF_RECORDS);
	KASSERT((elm_no & HAMMER_FSBUF_BLKMASK) < HAMMER_RECORD_NODES,
		("elm_no %d (%d) out of bounds", elm_no, elm_no & HAMMER_FSBUF_BLKMASK));
	hammer_modify_buffer(buffer);
	item = &buffer->ondisk->record.recs[elm_no & HAMMER_FSBUF_BLKMASK];
	bzero(item, sizeof(union hammer_record_ondisk));
	*errorp = 0;
	return(item);
}

void
hammer_free_data_ptr(hammer_buffer_t buffer, void *data, int bytes)
{
	int32_t elm_no;
	int32_t nblks;
	hammer_alist_t live;

	hammer_modify_cluster(buffer->cluster);
	if ((bytes & HAMMER_BUFMASK) == 0) {
		nblks = bytes / HAMMER_BUFSIZE;
		KKASSERT(nblks == 1 && data == (void *)buffer->ondisk);
		hammer_alist_free(&buffer->cluster->alist_master,
				  buffer->buf_no, nblks);
		hammer_adjust_stats(buffer->cluster, HAMMER_FSBUF_DATA, -nblks);
		return;
	}

	elm_no = ((char *)data - (char *)buffer->ondisk->data.data) /
		 HAMMER_DATA_BLKSIZE;
	KKASSERT(elm_no >= 0 && elm_no < HAMMER_DATA_NODES);
	elm_no += buffer->buf_no * HAMMER_FSBUF_MAXBLKS;
	nblks = (bytes + HAMMER_DATA_BLKMASK) & ~HAMMER_DATA_BLKMASK;
	nblks /= HAMMER_DATA_BLKSIZE;
	live = &buffer->cluster->alist_mdata;
	hammer_alist_free(live, elm_no, nblks);
}

void
hammer_free_record_ptr(hammer_buffer_t buffer, union hammer_record_ondisk *rec)
{
	int32_t elm_no;
	hammer_alist_t live;

	hammer_modify_cluster(buffer->cluster);
	elm_no = rec - &buffer->ondisk->record.recs[0];
	KKASSERT(elm_no >= 0 && elm_no < HAMMER_BTREE_NODES);
	elm_no += buffer->buf_no * HAMMER_FSBUF_MAXBLKS;
	live = &buffer->cluster->alist_record;
	hammer_alist_free(live, elm_no, 1);
}

void
hammer_free_btree(hammer_cluster_t cluster, int32_t bclu_offset)
{
	const int32_t blksize = sizeof(struct hammer_node_ondisk);
	int32_t fsbuf_offset = bclu_offset & HAMMER_BUFMASK;
	hammer_alist_t live;
	int32_t elm_no;

	hammer_modify_cluster(cluster);
	elm_no = bclu_offset / HAMMER_BUFSIZE * HAMMER_FSBUF_MAXBLKS;
	fsbuf_offset -= offsetof(union hammer_fsbuf_ondisk, btree.nodes[0]);
	live = &cluster->alist_btree;
	KKASSERT(fsbuf_offset >= 0 && fsbuf_offset % blksize == 0);
	elm_no += fsbuf_offset / blksize;
	hammer_alist_free(live, elm_no, 1);
}

void
hammer_free_data(hammer_cluster_t cluster, int32_t bclu_offset, int32_t bytes)
{
	const int32_t blksize = HAMMER_DATA_BLKSIZE;
	int32_t fsbuf_offset = bclu_offset & HAMMER_BUFMASK;
	hammer_alist_t live;
	int32_t elm_no;
	int32_t buf_no;
	int32_t nblks;

	hammer_modify_cluster(cluster);
	if ((bytes & HAMMER_BUFMASK) == 0) {
		nblks = bytes / HAMMER_BUFSIZE;
		KKASSERT(nblks == 1 && (bclu_offset & HAMMER_BUFMASK) == 0);
		buf_no = bclu_offset / HAMMER_BUFSIZE;
		hammer_alist_free(&cluster->alist_master, buf_no, nblks);
		hammer_adjust_stats(cluster, HAMMER_FSBUF_DATA, -nblks);
		return;
	}

	elm_no = bclu_offset / HAMMER_BUFSIZE * HAMMER_FSBUF_MAXBLKS;
	fsbuf_offset -= offsetof(union hammer_fsbuf_ondisk, data.data[0][0]);
	live = &cluster->alist_mdata;
	nblks = (bytes + HAMMER_DATA_BLKMASK) & ~HAMMER_DATA_BLKMASK;
	nblks /= HAMMER_DATA_BLKSIZE;
	KKASSERT(fsbuf_offset >= 0 && fsbuf_offset % blksize == 0);
	elm_no += fsbuf_offset / blksize;
	hammer_alist_free(live, elm_no, nblks);
}

void
hammer_free_record(hammer_cluster_t cluster, int32_t bclu_offset)
{
	const int32_t blksize = sizeof(union hammer_record_ondisk);
	int32_t fsbuf_offset = bclu_offset & HAMMER_BUFMASK;
	hammer_alist_t live;
	int32_t elm_no;

	hammer_modify_cluster(cluster);
	elm_no = bclu_offset / HAMMER_BUFSIZE * HAMMER_FSBUF_MAXBLKS;
	fsbuf_offset -= offsetof(union hammer_fsbuf_ondisk, record.recs[0]);
	live = &cluster->alist_record;
	KKASSERT(fsbuf_offset >= 0 && fsbuf_offset % blksize == 0);
	elm_no += fsbuf_offset / blksize;
	hammer_alist_free(live, elm_no, 1);
}


/*
 * Allocate a new filesystem buffer and assign it to the specified
 * filesystem buffer type.  The new buffer will be added to the
 * type-specific A-list and initialized.
 *
 * buffers used for records will also be added to the clu_record_buf_bitmap.
 */
static void
alloc_new_buffer(hammer_cluster_t cluster, u_int64_t type, hammer_alist_t live,
		 int start, int *errorp, struct hammer_buffer **bufferp)
{
	hammer_buffer_t buffer;
	int32_t buf_no;
	int32_t base_blk;
	int isfwd;

	if (*bufferp)
		hammer_rel_buffer(*bufferp, 0);
	*bufferp = NULL;

	start = start / HAMMER_FSBUF_MAXBLKS;	/* convert to buf_no */
	isfwd = (type != HAMMER_FSBUF_RECORDS);
	buf_no = hammer_alloc_master(cluster, 1, start, isfwd);
	if (buf_no == HAMMER_ALIST_BLOCK_NONE) {
		*errorp = ENOSPC;
		return;
	}

	/*
	 * The new buffer must be initialized (type != 0) regardless of
	 * whether we already have it cached or not, so don't try to
	 * optimize the cached buffer check.  Just call hammer_get_buffer().
	 */
	buffer = hammer_get_buffer(cluster, buf_no, type, errorp);
	*bufferp = buffer;

	/*
	 * Do a meta-free of the buffer's elements into the type-specific
	 * A-list and update our statistics to reflect the allocation.
	 */
	if (buffer) {
#if 0
		kprintf("alloc_new_buffer buf_no %d type %016llx nelms %d\n",
			buf_no, type, nelements);
#endif
		hammer_modify_buffer(buffer);  /*XXX*/
		hammer_adjust_stats(cluster, type, 1);

		/*
		 * Free the buffer to the appropriate slave list so the
		 * cluster-based allocator sees it.
		 */
		base_blk = buf_no * HAMMER_FSBUF_MAXBLKS;

		switch(type) {
		case HAMMER_FSBUF_BTREE:
			hammer_alist_free(live, base_blk, HAMMER_BTREE_NODES);
			break;
		case HAMMER_FSBUF_DATA:
			hammer_alist_free(live, base_blk, HAMMER_DATA_NODES);
			break;
		case HAMMER_FSBUF_RECORDS:
			hammer_alist_free(live, base_blk, HAMMER_RECORD_NODES);
			break;
		}
	}

	/*
	 * And, finally, update clu_record_buf_bitmap for record buffers.
	 * Since buffers are synced to disk before their associated cluster
	 * header, a recovery operation will only see synced record buffers
	 * in the bitmap.  XXX We can't use alist_record for recovery due
	 * to the way we currently manage it.
	 */
	if (buffer && type == HAMMER_FSBUF_RECORDS) {
		KKASSERT(buf_no >= 0 && buf_no < HAMMER_CLU_MAXBUFFERS);
		hammer_modify_cluster(cluster);
		cluster->ondisk->clu_record_buf_bitmap[buf_no >> 5] |=
			(1 << (buf_no & 31));
	}
}

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

	kprintf("hammer_sync\n");
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
	if (vp->v_type != VCHR) {
		error = VOP_FSYNC(vp, info->waitfor);
		if (error)
			info->error = error;
	}
	return(0);
}

int
hammer_sync_volume(hammer_volume_t volume, void *data)
{
	struct hammer_sync_info *info = data;

	RB_SCAN(hammer_clu_rb_tree, &volume->rb_clus_root, NULL,
		hammer_sync_cluster, info);
	if (hammer_ref_volume(volume) == 0)
		hammer_rel_volume(volume, 1);
	return(0);
}

int
hammer_sync_cluster(hammer_cluster_t cluster, void *data)
{
	struct hammer_sync_info *info = data;

	RB_SCAN(hammer_buf_rb_tree, &cluster->rb_bufs_root, NULL,
		hammer_sync_buffer, info);
	/*hammer_io_waitdep(&cluster->io);*/
	if (hammer_ref_cluster(cluster) == 0)
		hammer_rel_cluster(cluster, 1);
	return(0);
}

int
hammer_sync_buffer(hammer_buffer_t buffer, void *data __unused)
{
	if (hammer_ref_buffer(buffer) == 0)
		hammer_rel_buffer(buffer, 1);
	return(0);
}

/*
 * Generic buffer initialization.  Initialize the A-list into an all-allocated
 * state with the free block limit properly set.
 *
 * Note that alloc_new_buffer() will free the appropriate block range via
 * the appropriate cluster alist, so the free count is properly propogated.
 */
void
hammer_initbuffer(hammer_alist_t live, hammer_fsbuf_head_t head, u_int64_t type)
{
	head->buf_type = type;

	switch(type) {
	case HAMMER_FSBUF_BTREE:
		hammer_alist_init(live, 0, HAMMER_BTREE_NODES,
				  HAMMER_ASTATE_ALLOC);
		break;
	case HAMMER_FSBUF_DATA:
		hammer_alist_init(live, 0, HAMMER_DATA_NODES,
				  HAMMER_ASTATE_ALLOC);
		break;
	case HAMMER_FSBUF_RECORDS:
		hammer_alist_init(live, 0, HAMMER_RECORD_NODES,
				  HAMMER_ASTATE_ALLOC);
		break;
	default:
		hammer_alist_init(live, 0, 0, HAMMER_ASTATE_ALLOC);
		break;
	}
}

/*
 * Calculate the cluster's offset in the volume.  This calculation is
 * slightly more complex when using superclusters because superclusters
 * are grouped in blocks of 16, followed by 16 x N clusters where N
 * is the number of clusters a supercluster can manage.
 */
static int64_t
calculate_cluster_offset(hammer_volume_t volume, int32_t clu_no)
{
	int32_t scl_group;
	int64_t scl_group_size;
	int64_t off;

	if (volume->vol_flags & HAMMER_VOLF_USINGSUPERCL) {
		scl_group = clu_no / HAMMER_VOL_SUPERCLUSTER_GROUP /
			    HAMMER_SCL_MAXCLUSTERS;
		scl_group_size = 
			    ((int64_t)HAMMER_BUFSIZE *
			     HAMMER_VOL_SUPERCLUSTER_GROUP) +
			    ((int64_t)HAMMER_VOL_SUPERCLUSTER_GROUP *
			     volume->vol_clsize * HAMMER_SCL_MAXCLUSTERS);
		scl_group_size += 
			    HAMMER_VOL_SUPERCLUSTER_GROUP * HAMMER_BUFSIZE;

		off = volume->cluster_base +
		      scl_group * scl_group_size +
		      (HAMMER_BUFSIZE * HAMMER_VOL_SUPERCLUSTER_GROUP) +
		      ((int64_t)clu_no % ((int64_t)HAMMER_SCL_MAXCLUSTERS *
		       HAMMER_VOL_SUPERCLUSTER_GROUP))
		      * volume->vol_clsize;
	} else {
		off = volume->cluster_base +
		      (int64_t)clu_no * volume->vol_clsize;
	}
	return(off);
}

/*
 * Calculate a super-cluster's offset in the volume.
 */
static int64_t
calculate_supercl_offset(hammer_volume_t volume, int32_t scl_no)
{
	int64_t off;
	int32_t scl_group;
	int64_t scl_group_size;

	KKASSERT (volume->vol_flags & HAMMER_VOLF_USINGSUPERCL);
	scl_group = scl_no / HAMMER_VOL_SUPERCLUSTER_GROUP;
	if (scl_group) {
		scl_group_size = 
			    ((int64_t)HAMMER_BUFSIZE *
			     HAMMER_VOL_SUPERCLUSTER_GROUP) +
			    ((int64_t)HAMMER_VOL_SUPERCLUSTER_GROUP *
			     volume->vol_clsize * HAMMER_SCL_MAXCLUSTERS);
		scl_group_size += 
			    HAMMER_VOL_SUPERCLUSTER_GROUP * HAMMER_BUFSIZE;
		off = volume->cluster_base + (scl_group * scl_group_size) +
		      (scl_no % HAMMER_VOL_SUPERCLUSTER_GROUP) * HAMMER_BUFSIZE;
	} else {
		off = volume->cluster_base + (scl_no * HAMMER_BUFSIZE);
	}
	return(off);
}

/*
 * Allocate nblks buffers from the cluster's master alist.
 */
static int32_t
hammer_alloc_master(hammer_cluster_t cluster, int nblks,
		    int32_t start, int isfwd)
{
	int32_t buf_no;

	hammer_modify_cluster(cluster);
	if (isfwd) {
		buf_no = hammer_alist_alloc_fwd(&cluster->alist_master,
						nblks, start);
		if (buf_no == HAMMER_ALIST_BLOCK_NONE) {
			buf_no = hammer_alist_alloc_fwd(&cluster->alist_master,
						nblks, 0);
		}
	} else {
		buf_no = hammer_alist_alloc_rev(&cluster->alist_master,
						nblks, start);
		if (buf_no == HAMMER_ALIST_BLOCK_NONE) {
			buf_no = hammer_alist_alloc_rev(&cluster->alist_master,
						nblks, HAMMER_ALIST_BLOCK_MAX);
		}
	}

	/*
	 * Recover space from empty record, b-tree, and data a-lists.
	 */

	return(buf_no);
}

/*
 * Adjust allocation statistics
 */
static void
hammer_adjust_stats(hammer_cluster_t cluster, u_int64_t buf_type, int nblks)
{
	hammer_modify_cluster(cluster);
	hammer_modify_volume(cluster->volume);
	hammer_modify_volume(cluster->volume->hmp->rootvol);

	switch(buf_type) {
	case HAMMER_FSBUF_BTREE:
		cluster->ondisk->stat_idx_bufs += nblks;
		cluster->volume->ondisk->vol_stat_idx_bufs += nblks;
		cluster->volume->hmp->rootvol->ondisk->vol0_stat_idx_bufs += nblks;
		break;
	case HAMMER_FSBUF_DATA:
		cluster->ondisk->stat_data_bufs += nblks;
		cluster->volume->ondisk->vol_stat_data_bufs += nblks;
		cluster->volume->hmp->rootvol->ondisk->vol0_stat_data_bufs += nblks;
		break;
	case HAMMER_FSBUF_RECORDS:
		cluster->ondisk->stat_rec_bufs += nblks;
		cluster->volume->ondisk->vol_stat_rec_bufs += nblks;
		cluster->volume->hmp->rootvol->ondisk->vol0_stat_rec_bufs += nblks;
		break;
	}
}

/*
 * A-LIST SUPPORT
 *
 * Setup the parameters for the various A-lists we use in hammer.  The
 * supercluster A-list must be chained to the cluster A-list and cluster
 * slave A-lists are chained to buffer A-lists.
 *
 * See hammer_init_alist_config() below.
 */

/*
 * A-LIST - cluster recursion into a filesystem buffer
 *
 * In the init case the buffer has already been initialized by
 * alloc_new_buffer() when it allocated the buffer out of the master
 * alist and marked it as free in the slave alist.
 *
 * Because we use a somewhat odd mechanism to assign buffers to slave
 * pools we can't actually free the buffer back to the master alist in
 * buffer_alist_destroy(), but instead must deal with that logic somewhere
 * else.
 */
static int
buffer_alist_init(void *info, int32_t blk, int32_t radix,
		  hammer_alloc_state_t state)
{
	return(0);
}

static int
buffer_alist_recover(void *info, int32_t blk, int32_t radix, int32_t count)
{
	hammer_cluster_t cluster = info;
	hammer_buffer_t buffer;
	int32_t buf_no;
	int error = 0;

	buf_no = blk / HAMMER_FSBUF_MAXBLKS;
	buffer = hammer_get_buffer(cluster, buf_no, 0, &error);
	if (buffer) {
		hammer_modify_buffer(buffer);
		error = hammer_alist_recover(&buffer->alist, blk, 0, count);
		/* free block count is returned if >= 0 */
		hammer_rel_buffer(buffer, 0);
	} else {
		error = -error;
	}
	return (error);
}

/*
 * Note: This routine is only called when freeing the last elements of
 * an initialized buffer.  Freeing all elements of the buffer when the
 * buffer was not previously initialized does not call this routine.
 */
static int
buffer_alist_destroy(void *info, int32_t blk, int32_t radix)
{
	hammer_cluster_t cluster = info;
	int32_t buf_no;

	buf_no = blk / HAMMER_FSBUF_MAXBLKS;
	kprintf("destroy buffer %d:%d:%d\n", cluster->volume->vol_no, cluster->clu_no, buf_no);
	return (0);
}

/*
 * Note: atblk can be negative and atblk - blk can go negative.
 */
static int
buffer_alist_alloc_fwd(void *info, int32_t blk, int32_t radix,
                      int32_t count, int32_t atblk, int32_t *fullp)
{
	hammer_cluster_t cluster = info;
	hammer_buffer_t buffer;
	int32_t buf_no;
	int32_t r;
	int error = 0;

	buf_no = blk / HAMMER_FSBUF_MAXBLKS;
	buffer = hammer_get_buffer(cluster, buf_no, 0, &error);
	if (buffer) {
		KKASSERT(buffer->ondisk->head.buf_type != 0);

		hammer_modify_buffer(buffer);
		r = hammer_alist_alloc_fwd(&buffer->alist, count, atblk - blk);
		if (r != HAMMER_ALIST_BLOCK_NONE)
			r += blk;
		*fullp = hammer_alist_isfull(&buffer->alist);
		hammer_rel_buffer(buffer, 0);
	} else {
		r = HAMMER_ALIST_BLOCK_NONE;
		*fullp = 1;
	}
	return(r);
}

static int
buffer_alist_alloc_rev(void *info, int32_t blk, int32_t radix,
                      int32_t count, int32_t atblk, int32_t *fullp)
{
	hammer_cluster_t cluster = info;
	hammer_buffer_t buffer;
	int32_t buf_no;
	int32_t r;
	int error = 0;

	buf_no = blk / HAMMER_FSBUF_MAXBLKS;
	buffer = hammer_get_buffer(cluster, buf_no, 0, &error);
	if (buffer) {
		KKASSERT(buffer->ondisk->head.buf_type != 0);
		hammer_modify_buffer(buffer);
		r = hammer_alist_alloc_rev(&buffer->alist, count, atblk - blk);
		if (r != HAMMER_ALIST_BLOCK_NONE)
			r += blk;
		*fullp = hammer_alist_isfull(&buffer->alist);
		hammer_rel_buffer(buffer, 0);
	} else {
		r = HAMMER_ALIST_BLOCK_NONE;
		*fullp = 1;
	}
	return(r);
}

static void
buffer_alist_free(void *info, int32_t blk, int32_t radix,
                 int32_t base_blk, int32_t count, int32_t *emptyp)
{
	hammer_cluster_t cluster = info;
	hammer_buffer_t buffer;
	int32_t buf_no;
	int error = 0;

	buf_no = blk / HAMMER_FSBUF_MAXBLKS;
	buffer = hammer_get_buffer(cluster, buf_no, 0, &error);
	if (buffer) {
		KKASSERT(buffer->ondisk->head.buf_type != 0);
		hammer_modify_buffer(buffer);
		hammer_alist_free(&buffer->alist, base_blk, count);
		*emptyp = hammer_alist_isempty(&buffer->alist);
		/* XXX don't bother updating the buffer is completely empty? */
		hammer_rel_buffer(buffer, 0);
	} else {
		*emptyp = 0;
	}
}

static void
buffer_alist_print(void *info, int32_t blk, int32_t radix, int tab)
{
}

/*
 * A-LIST - super-cluster recursion into a cluster and cluster recursion
 * into a filesystem buffer.  A-List's are mostly self-contained entities,
 * but callbacks must be installed to recurse from one A-List to another.
 *
 * Implementing these callbacks allows us to operate a multi-layered A-List
 * as a single entity.
 */

/*
 * This occurs when allocating a cluster via the volume a-list and the
 * entry in the volume a-list indicated all-free.  The underlying supercl
 * has not yet been initialized.
 */
static int
super_alist_init(void *info, int32_t blk, int32_t radix,
		 hammer_alloc_state_t state)
{
	hammer_volume_t volume = info;
	hammer_supercl_t supercl;
	int32_t scl_no;
	int error = 0;

	/*
	 * Calculate the super-cluster number containing the cluster (blk)
	 * and obtain the super-cluster buffer.
	 */
	scl_no = blk / HAMMER_SCL_MAXCLUSTERS;
	supercl = hammer_get_supercl(volume, scl_no, &error, state);
	if (supercl)
		hammer_rel_supercl(supercl, 0);
	return (error);
}

static int
super_alist_recover(void *info, int32_t blk, int32_t radix, int32_t count)
{
	hammer_volume_t volume = info;
	hammer_supercl_t supercl;
	int32_t scl_no;
	int error = 0;

	/*
	 * Calculate the super-cluster number containing the cluster (blk)
	 * and obtain the super-cluster buffer.
	 */
	scl_no = blk / HAMMER_SCL_MAXCLUSTERS;
	supercl = hammer_get_supercl(volume, scl_no, &error,
				     HAMMER_ASTATE_NONE);
	if (supercl) {
		hammer_modify_supercl(supercl);
		error = hammer_alist_recover(&supercl->alist, blk, 0, count);
		/* free block count is returned if >= 0 */
		hammer_rel_supercl(supercl, 0);
	} else {
		error = -error;
	}
	return (error);
}

/*
 * This occurs when freeing a cluster via the volume a-list and the
 * supercl is now 100% free.  We can destroy the supercl.
 *
 * What we actually do is just unset the modify bit so it doesn't get
 * written out.
 */
static int
super_alist_destroy(void *info, int32_t blk, int32_t radix)
{
	hammer_volume_t volume = info;
	hammer_supercl_t supercl;
	int32_t scl_no;
	int error = 0;

	/*
	 * Calculate the super-cluster number containing the cluster (blk)
	 * and obtain the super-cluster buffer.
	 */
	scl_no = blk / HAMMER_SCL_MAXCLUSTERS;
	if (hammer_find_supercl(volume, scl_no)) {
		supercl = hammer_get_supercl(volume, scl_no, &error,
					     HAMMER_ASTATE_FREE);
					     /* XXX */
		hammer_io_clear_modify(&supercl->io);
		if (supercl)
			hammer_rel_supercl(supercl, 0);
	}
	return (error);
}

static int
super_alist_alloc_fwd(void *info, int32_t blk, int32_t radix,
		      int32_t count, int32_t atblk, int32_t *fullp)
{
	hammer_volume_t volume = info;
	hammer_supercl_t supercl;
	int32_t scl_no;
	int32_t r;
	int error = 0;

	scl_no = blk / HAMMER_SCL_MAXCLUSTERS;
	supercl = hammer_get_supercl(volume, scl_no, &error, 0);
	if (supercl) {
		hammer_modify_supercl(supercl);
		r = hammer_alist_alloc_fwd(&supercl->alist, count, atblk - blk);
		if (r != HAMMER_ALIST_BLOCK_NONE)
			r += blk;
		*fullp = hammer_alist_isfull(&supercl->alist);
		hammer_rel_supercl(supercl, 0);
	} else {
		r = HAMMER_ALIST_BLOCK_NONE;
		*fullp = 1;
	}
	return(r);
}

static int
super_alist_alloc_rev(void *info, int32_t blk, int32_t radix,
		      int32_t count, int32_t atblk, int32_t *fullp)
{
	hammer_volume_t volume = info;
	hammer_supercl_t supercl;
	int32_t scl_no;
	int32_t r;
	int error = 0;

	scl_no = blk / HAMMER_SCL_MAXCLUSTERS;
	supercl = hammer_get_supercl(volume, scl_no, &error, 0);
	if (supercl) {
		hammer_modify_supercl(supercl);
		r = hammer_alist_alloc_rev(&supercl->alist, count, atblk - blk);
		if (r != HAMMER_ALIST_BLOCK_NONE)
			r += blk;
		*fullp = hammer_alist_isfull(&supercl->alist);
		hammer_rel_supercl(supercl, 0);
	} else { 
		r = HAMMER_ALIST_BLOCK_NONE;
		*fullp = 1;
	}
	return(r);
}

static void
super_alist_free(void *info, int32_t blk, int32_t radix,
		 int32_t base_blk, int32_t count, int32_t *emptyp)
{
	hammer_volume_t volume = info;
	hammer_supercl_t supercl;
	int32_t scl_no;
	int error = 0;

	scl_no = blk / HAMMER_SCL_MAXCLUSTERS;
	supercl = hammer_get_supercl(volume, scl_no, &error, 0);
	if (supercl) {
		hammer_modify_supercl(supercl);
		hammer_alist_free(&supercl->alist, base_blk, count);
		*emptyp = hammer_alist_isempty(&supercl->alist);
		hammer_rel_supercl(supercl, 0);
	} else {
		*emptyp = 0;
	}
}

static void
super_alist_print(void *info, int32_t blk, int32_t radix, int tab)
{
}

void
hammer_init_alist_config(void)
{
	hammer_alist_config_t config;

	hammer_alist_template(&Buf_alist_config, HAMMER_FSBUF_MAXBLKS,
			      1, HAMMER_FSBUF_METAELMS);
	hammer_alist_template(&Vol_normal_alist_config, HAMMER_VOL_MAXCLUSTERS,
			      1, HAMMER_VOL_METAELMS_1LYR);
	hammer_alist_template(&Vol_super_alist_config,
			  HAMMER_VOL_MAXSUPERCLUSTERS * HAMMER_SCL_MAXCLUSTERS,
			      HAMMER_SCL_MAXCLUSTERS, HAMMER_VOL_METAELMS_2LYR);
	hammer_alist_template(&Supercl_alist_config, HAMMER_VOL_MAXCLUSTERS,
			      1, HAMMER_SUPERCL_METAELMS);
	hammer_alist_template(&Clu_master_alist_config, HAMMER_CLU_MAXBUFFERS,
			      1, HAMMER_CLU_MASTER_METAELMS);
	hammer_alist_template(&Clu_slave_alist_config,
			      HAMMER_CLU_MAXBUFFERS * HAMMER_FSBUF_MAXBLKS,
			      HAMMER_FSBUF_MAXBLKS, HAMMER_CLU_SLAVE_METAELMS);

	config = &Vol_super_alist_config;
	config->bl_radix_init = super_alist_init;
	config->bl_radix_recover = super_alist_recover;
	config->bl_radix_destroy = super_alist_destroy;
	config->bl_radix_alloc_fwd = super_alist_alloc_fwd;
	config->bl_radix_alloc_rev = super_alist_alloc_rev;
	config->bl_radix_free = super_alist_free;
	config->bl_radix_print = super_alist_print;

	config = &Clu_slave_alist_config;
	config->bl_radix_init = buffer_alist_init;
	config->bl_radix_recover = buffer_alist_recover;
	config->bl_radix_destroy = buffer_alist_destroy;
	config->bl_radix_alloc_fwd = buffer_alist_alloc_fwd;
	config->bl_radix_alloc_rev = buffer_alist_alloc_rev;
	config->bl_radix_free = buffer_alist_free;
	config->bl_radix_print = buffer_alist_print;
}

