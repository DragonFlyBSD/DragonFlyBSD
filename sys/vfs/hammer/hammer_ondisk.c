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
 * $DragonFly: src/sys/vfs/hammer/hammer_ondisk.c,v 1.9 2007/11/30 00:16:56 dillon Exp $
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
static int hammer_load_supercl(hammer_supercl_t supercl, int isnew);
static int hammer_load_cluster(hammer_cluster_t cluster, int isnew);
static int hammer_load_buffer(hammer_buffer_t buffer, u_int64_t buf_type);
static void hammer_remove_node_clist(hammer_buffer_t buffer,
			hammer_node_t node);
static void initbuffer(hammer_alist_t live, hammer_fsbuf_head_t head,
			u_int64_t type);
static void alloc_new_buffer(hammer_cluster_t cluster,
			hammer_alist_t live, u_int64_t type, int32_t nelements,
			int32_t start,
			int *errorp, struct hammer_buffer **bufferp);
#if 0
static void readhammerbuf(hammer_volume_t vol, void *data,
			int64_t offset);
static void writehammerbuf(hammer_volume_t vol, const void *data,
			int64_t offset);
#endif
static int64_t calculate_cluster_offset(hammer_volume_t vol, int32_t clu_no);
static int64_t calculate_supercl_offset(hammer_volume_t vol, int32_t scl_no);

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
	volume = kmalloc(sizeof(*volume), M_HAMMER, M_WAITOK|M_ZERO);
	volume->vol_name = kstrdup(volname, M_HAMMER);
	volume->hmp = hmp;
	volume->io.type = HAMMER_STRUCTURE_VOLUME;
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
	RB_INIT(&volume->rb_clus_root);
	RB_INIT(&volume->rb_scls_root);

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
		hmp->rootcl = hammer_get_cluster(volume,
						 ondisk->vol0_root_clu_no,
						 &error, 0);
		hammer_rel_cluster(hmp->rootcl, 0);
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

	/*
	 * Release our buffer and flush anything left in the buffer cache.
	 */
	hammer_io_release(&volume->io, 1);

	/*
	 * There should be no references on the volume.
	 */
	KKASSERT(volume->io.lock.refs == 0);

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
		hammer_alist_init(&volume->alist);
	} else {
		error = 0;
	}
	hammer_unlock(&volume->io.lock);
	return(0);
}

/*
 * Release a volume.  Call hammer_io_release on the last reference.  We have
 * to acquire an exclusive lock to interlock against volume->ondisk tests
 * in hammer_load_volume().
 */
void
hammer_rel_volume(hammer_volume_t volume, int flush)
{
	if (hammer_islastref(&volume->io.lock)) {
		hammer_lock_ex(&volume->io.lock);
		if (hammer_islastref(&volume->io.lock)) {
			volume->ondisk = NULL;
			hammer_io_release(&volume->io, flush);
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
hammer_supercl_t
hammer_get_supercl(hammer_volume_t volume, int32_t scl_no,
		   int *errorp, int isnew)
{
	hammer_supercl_t supercl;

	/*
	 * Locate and lock the super-cluster structure, creating one
	 * if necessary.
	 */
again:
	supercl = RB_LOOKUP(hammer_scl_rb_tree, &volume->rb_scls_root, scl_no);
	if (supercl == NULL) {
		supercl = kmalloc(sizeof(*supercl), M_HAMMER, M_WAITOK|M_ZERO);
		supercl->scl_no = scl_no;
		supercl->volume = volume;
		supercl->io.offset = calculate_supercl_offset(volume, scl_no);
		supercl->io.type = HAMMER_STRUCTURE_SUPERCL;
		hammer_ref(&supercl->io.lock);

		/*
		 * Insert the cluster into the RB tree and handle late
		 * collisions.
		 */
		if (RB_INSERT(hammer_scl_rb_tree, &volume->rb_scls_root, supercl)) {
			hammer_unref(&supercl->io.lock);
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
hammer_load_supercl(hammer_supercl_t supercl, int isnew)
{
	struct hammer_supercl_ondisk *ondisk;
	hammer_volume_t volume = supercl->volume;
	int error;

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

		ondisk = supercl->ondisk;
		dummy.config = &Buf_alist_config;
		dummy.meta = ondisk->head.buf_almeta;
		dummy.info = NULL;
		initbuffer(&dummy, &ondisk->head, HAMMER_FSBUF_SUPERCL);
		hammer_alist_init(&supercl->alist);
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
	hammer_io_release(&supercl->io, 1);
	hammer_rel_supercl(supercl, 1);
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

	if (hammer_islastref(&supercl->io.lock)) {
		hammer_lock_ex(&supercl->io.lock);
		if (hammer_islastref(&supercl->io.lock)) {
			hammer_io_release(&supercl->io, flush);
			if (supercl->io.bp == NULL &&
			    hammer_islastref(&supercl->io.lock)) {
				volume = supercl->volume;
				RB_REMOVE(hammer_scl_rb_tree,
					  &volume->rb_scls_root, supercl);
				supercl->volume = NULL;	/* sanity */
				kfree(supercl, M_HAMMER);
				hammer_rel_volume(volume, 0);
				return;
			}
		}
		hammer_unlock(&supercl->io.lock);
	}
	hammer_unref(&supercl->io.lock);
}

/************************************************************************
 *				CLUSTERS				*
 ************************************************************************
 *
 * Manage clusters.  Note that a cluster holds a reference to its
 * associated volume.
 */
hammer_cluster_t
hammer_get_cluster(hammer_volume_t volume, int32_t clu_no,
		   int *errorp, int isnew)
{
	hammer_cluster_t cluster;

again:
	cluster = RB_LOOKUP(hammer_clu_rb_tree, &volume->rb_clus_root, clu_no);
	if (cluster == NULL) {
		cluster = kmalloc(sizeof(*cluster), M_HAMMER, M_WAITOK|M_ZERO);
		cluster->clu_no = clu_no;
		cluster->volume = volume;
		cluster->io.offset = calculate_cluster_offset(volume, clu_no);
		RB_INIT(&cluster->rb_bufs_root);
		RB_INIT(&cluster->rb_nods_root);
		cluster->io.type = HAMMER_STRUCTURE_CLUSTER;
		hammer_ref(&cluster->io.lock);

		/*
		 * Insert the cluster into the RB tree and handle late
		 * collisions.
		 */
		if (RB_INSERT(hammer_clu_rb_tree, &volume->rb_clus_root, cluster)) {
			hammer_unref(&cluster->io.lock);
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
hammer_load_cluster(hammer_cluster_t cluster, int isnew)
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

		cluster->clu_btree_beg = ondisk->clu_btree_beg;
		cluster->clu_btree_end = ondisk->clu_btree_end;
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

		ondisk = cluster->ondisk;

		dummy.config = &Buf_alist_config;
		dummy.meta = ondisk->head.buf_almeta;
		dummy.info = NULL;
		initbuffer(&dummy, &ondisk->head, HAMMER_FSBUF_CLUSTER);

		hammer_alist_init(&cluster->alist_master);
		hammer_alist_init(&cluster->alist_btree);
		hammer_alist_init(&cluster->alist_record);
		hammer_alist_init(&cluster->alist_mdata);
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
	KKASSERT(cluster->io.lock.refs == 1);
	hammer_io_release(&cluster->io, 1);
	hammer_rel_cluster(cluster, 1);
	return(0);
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
 * Only destroy the structure itself if the related buffer cache buffer
 * was disassociated from it.  This ties the management of the structure
 * to the buffer cache subsystem.
 */
void
hammer_rel_cluster(hammer_cluster_t cluster, int flush)
{
	hammer_node_t node;
	hammer_volume_t volume;

	if (hammer_islastref(&cluster->io.lock)) {
		hammer_lock_ex(&cluster->io.lock);
		if (hammer_islastref(&cluster->io.lock)) {
			hammer_io_release(&cluster->io, flush);

			/*
			 * Clean out the B-Tree node cache, if any, then
			 * clean up the volume ref and free the cluster.
			 *
			 * If the cluster acquires a new reference while we
			 * are trying to clean it out, abort the cleaning.
			 *
			 * There really shouldn't be any nodes at this point
			 * but we allow a node with no buffer association
			 * so handle the case.
			 */
			while (cluster->io.bp == NULL &&
			       hammer_islastref(&cluster->io.lock) &&
			       (node = RB_ROOT(&cluster->rb_nods_root)) != NULL
			) {
				KKASSERT(node->lock.refs == 0);
				hammer_flush_node(node);
			}
			if (cluster->io.bp == NULL &&
			    hammer_islastref(&cluster->io.lock)) {
				volume = cluster->volume;
				RB_REMOVE(hammer_clu_rb_tree,
					  &volume->rb_clus_root, cluster);
				cluster->volume = NULL;	/* sanity */
				kfree(cluster, M_HAMMER);
				hammer_rel_volume(volume, 0);
				return;
			}
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
		buffer = kmalloc(sizeof(*cluster), M_HAMMER, M_WAITOK|M_ZERO);
		buffer->buf_no = buf_no;
		buffer->cluster = cluster;
		buffer->volume = cluster->volume;
		buffer->io.offset = cluster->io.offset +
				    (buf_no * HAMMER_BUFSIZE);
		buffer->io.type = HAMMER_STRUCTURE_BUFFER;
		TAILQ_INIT(&buffer->clist);
		hammer_ref(&buffer->io.lock);

		/*
		 * Insert the cluster into the RB tree and handle late
		 * collisions.
		 */
		if (RB_INSERT(hammer_buf_rb_tree, &cluster->rb_bufs_root, buffer)) {
			hammer_unref(&buffer->io.lock);
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
		ondisk = buffer->ondisk;
		initbuffer(&buffer->alist, &ondisk->head, buf_type);
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
	hammer_io_release(&buffer->io, 1);
	KKASSERT(buffer->io.lock.refs == 1);
	hammer_rel_buffer(buffer, 1);
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
	hammer_node_t node;

	if (hammer_islastref(&buffer->io.lock)) {
		hammer_lock_ex(&buffer->io.lock);
		if (hammer_islastref(&buffer->io.lock)) {
			hammer_io_release(&buffer->io, flush);

			/*
			 * Clean out the B-Tree node cache, if any, then
			 * clean up the cluster ref and free the buffer.
			 *
			 * If the buffer acquires a new reference while we
			 * are trying to clean it out, abort the cleaning.
			 */
			while (buffer->io.bp == NULL &&
			       hammer_islastref(&buffer->io.lock) &&
			       (node = TAILQ_FIRST(&buffer->clist)) != NULL
			) {
				KKASSERT(node->lock.refs == 0);
				hammer_flush_node(node);
			}
			if (buffer->io.bp == NULL &&
			    hammer_islastref(&buffer->io.lock)) {
				cluster = buffer->cluster;
				RB_REMOVE(hammer_buf_rb_tree,
					  &cluster->rb_bufs_root, buffer);
				buffer->cluster = NULL; /* sanity */
				kfree(buffer, M_HAMMER);
				hammer_rel_cluster(cluster, 0);
				return;
			}
		}
		hammer_unlock(&buffer->io.lock);
	}
	hammer_unref(&buffer->io.lock);
}

/*
 * Flush passively cached B-Tree nodes associated with this buffer.
 *
 * NOTE: The buffer is referenced and locked.
 */
void
hammer_flush_buffer_nodes(hammer_buffer_t buffer)
{
	hammer_node_t node;

	node = TAILQ_FIRST(&buffer->clist);
	while (node) {
		buffer->save_scan = TAILQ_NEXT(node, entry);
		if (node->lock.refs == 0)
			hammer_flush_node(node);
		node = buffer->save_scan;
	}
}

/************************************************************************
 *				NODES					*
 ************************************************************************
 *
 * Manage B-Tree nodes.  B-Tree nodes represent the primary indexing
 * method used by the HAMMER filesystem.
 *
 * Unlike other HAMMER structures, a hammer_node can be PASSIVELY
 * associated with its buffer.  It can have an active buffer reference
 * even when the node itself has no references.  The node also passively
 * associates itself with its cluster without holding any cluster refs.
 * The cluster ref is indirectly maintained by the active buffer ref when
 * a node is acquired.
 *
 * A hammer_node can also be passively associated with other HAMMER
 * structures, such as inodes, while retaining 0 references.  These
 * associations can be cleared backwards using a pointer-to-pointer in
 * the hammer_node.
 *
 * This allows the HAMMER implementation to cache hammer_node's long-term
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
		node = kmalloc(sizeof(*node), M_HAMMER, M_WAITOK|M_ZERO);
		node->node_offset = node_offset;
		node->cluster = cluster;
		if (RB_INSERT(hammer_nod_rb_tree, &cluster->rb_nods_root,
			      node)) {
			kfree(node, M_HAMMER);
			goto again;
		}
	}
	*errorp = hammer_ref_node(node);
	if (*errorp) {
		/*
		 * NOTE: The node pointer may be stale on error return.
		 * In fact, its probably been destroyed.
		 */
		node = NULL;
	}
	return(node);
}

/*
 * Reference the node to prevent disassociations, then associate and
 * load the related buffer.  This routine can also be called to reference
 * a node from a cache pointer.
 *
 * NOTE: Because the caller does not have a ref on the node, the caller's
 * node pointer will be stale if an error is returned.  We may also wind
 * up clearing the related cache pointers.
 *
 * NOTE: The cluster is indirectly referenced by our buffer ref.
 */
int
hammer_ref_node(hammer_node_t node)
{
	hammer_buffer_t buffer;
	int32_t buf_no;
	int error;

	hammer_ref(&node->lock);
	error = 0;
	if (node->ondisk == NULL) {
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
	}
	if (error)
		hammer_rel_node(node);
	return (error);
}

/*
 * Release a hammer_node.  The node retains a passive association with
 * its cluster, buffer and caches.
 *
 * However, to avoid cluttering up kernel memory with tons of B-Tree
 * node cache structures we destroy the node if no passive cache or
 * (instantiated) buffer references exist.
 */
void
hammer_rel_node(hammer_node_t node)
{
	hammer_cluster_t cluster;
	hammer_buffer_t buffer;

	if (hammer_islastref(&node->lock)) {
		cluster = node->cluster;
		/*
		 * Clutter control, this case only occurs after a failed
		 * load since otherwise ondisk will be non-NULL.
		 */
		if (node->cache1 == NULL && node->cache2 == NULL && 
		    node->ondisk == NULL) {
			RB_REMOVE(hammer_nod_rb_tree, &cluster->rb_nods_root,
				  node);
			if ((buffer = node->buffer) != NULL) {
				node->buffer = NULL;
				hammer_remove_node_clist(buffer, node);
			}
			kfree(node, M_HAMMER);
			return;
		}

		/*
		 * node->ondisk determines whether we have a buffer reference
		 * to get rid of or not.  Only get rid of the reference if
		 * the kernel tried to flush the buffer.
		 *
		 * NOTE: Once unref'd the node can be physically destroyed,
		 * so our node is stale afterwords.
		 *
		 * This case occurs if the node still has cache references.
		 * We could remove the references and free the structure
		 * but for now we allow them (and the node structure) to
		 * remain intact.
		 */
		if (node->ondisk && hammer_io_checkflush(&node->buffer->io)) {
			buffer = node->buffer;
			node->buffer = NULL;
			node->ondisk = NULL;
			hammer_remove_node_clist(buffer, node);
			hammer_unref(&node->lock);
			hammer_rel_buffer(buffer, 0);
		} else {
			hammer_unref(&node->lock);
		}
	} else {
		hammer_unref(&node->lock);
	}
}

/*
 * Cache-and-release a hammer_node.  Kinda like catching and releasing a
 * fish, but keeping an eye on him.  The node is passively cached in *cache.
 *
 * NOTE!  HAMMER may NULL *cache at any time, even after you have
 * referenced the node!
 */
void
hammer_cache_node(hammer_node_t node, struct hammer_node **cache)
{
	if (node->cache1 != cache) {
		if (node->cache2 == cache) {
			struct hammer_node **tmp;
			tmp = node->cache1;
			node->cache1 = node->cache2;
			node->cache2 = tmp;
		} else {
			if (node->cache2)
				*node->cache2 = NULL;
			node->cache2 = node->cache1;
			node->cache1 = cache;
			*cache = node;
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
		if (node->cache1 == NULL && node->cache2 == NULL &&
		    node->lock.refs == 0) {
			hammer_flush_node(node);
		}
	}
}

/*
 * Remove a node's cache references and destroy the node if it has no
 * references.  This is typically called from the buffer handling code.
 *
 * The node may have an active buffer reference (ondisk != NULL) even
 * if the node itself has no references.
 *
 * Note that a caller iterating through nodes via a buffer must have its
 * own reference on the buffer or our hammer_rel_buffer() call below may
 * rip it out from under the caller.
 */
void
hammer_flush_node(hammer_node_t node)
{
	hammer_buffer_t buffer;

	if (node->cache1)
		*node->cache1 = NULL;
	if (node->cache2)
		*node->cache2 = NULL;
	if (node->lock.refs == 0) {
		RB_REMOVE(hammer_nod_rb_tree, &node->cluster->rb_nods_root,
			  node);
		if ((buffer = node->buffer) != NULL) {
			node->buffer = NULL;
			hammer_remove_node_clist(buffer, node);
			if (node->ondisk) {
				node->ondisk = NULL;
				hammer_rel_buffer(buffer, 0);
			}
		}
		kfree(node, M_HAMMER);
	}
}

/*
 * Remove a node from the buffer's clist.  Adjust save_scan as appropriate.
 * This is in its own little routine to properly handle interactions with
 * save_scan, so it is possible to block while scanning a buffer's node list.
 */
static
void
hammer_remove_node_clist(hammer_buffer_t buffer, hammer_node_t node)
{
	if (buffer->save_scan == node)
		buffer->save_scan = TAILQ_NEXT(node, entry);
	TAILQ_REMOVE(&buffer->clist, node, entry);
}

/************************************************************************
 *				A-LIST ALLOCATORS			*
 ************************************************************************/

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
	buffer = NULL;
	live = &cluster->alist_btree;
	elm_no = hammer_alist_alloc_fwd(live, 1, cluster->ondisk->idx_index);
	if (elm_no == HAMMER_ALIST_BLOCK_NONE)
		elm_no = hammer_alist_alloc_fwd(live, 1, 0);
	if (elm_no == HAMMER_ALIST_BLOCK_NONE) {
		alloc_new_buffer(cluster, live,
				 HAMMER_FSBUF_BTREE, HAMMER_BTREE_NODES,
				 cluster->ondisk->idx_index, errorp, &buffer);
		elm_no = hammer_alist_alloc(live, 1);
		if (elm_no == HAMMER_ALIST_BLOCK_NONE) {
			*errorp = ENOSPC;
			if (buffer)
				hammer_rel_buffer(buffer, 0);
			hammer_modify_cluster(cluster);
			return(NULL);
		}
	}
	cluster->ondisk->idx_index = elm_no;
	KKASSERT((elm_no & HAMMER_FSBUF_BLKMASK) < HAMMER_BTREE_NODES);
	hammer_modify_cluster(cluster);

	/*
	 * Load and return the B-Tree element
	 */
	buf_no = elm_no / HAMMER_FSBUF_MAXBLKS;
	node_offset = buf_no * HAMMER_BUFSIZE +
		      offsetof(union hammer_fsbuf_ondisk,
			       btree.nodes[elm_no & HAMMER_FSBUF_BLKMASK]);
	node = hammer_get_node(cluster, node_offset, errorp);
	if (node) {
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
	if ((bytes & HAMMER_BUFMASK) == 0) {
		nblks = bytes / HAMMER_BUFSIZE;
		/* only one block allowed for now (so buffer can hold it) */
		KKASSERT(nblks == 1);

		buf_no = hammer_alist_alloc_fwd(&cluster->alist_master,
						nblks,
						cluster->ondisk->idx_ldata);
		if (buf_no == HAMMER_ALIST_BLOCK_NONE) {
			buf_no = hammer_alist_alloc_fwd(&cluster->alist_master,
						nblks,
						0);
		}
		hammer_modify_cluster(cluster);
		if (buf_no == HAMMER_ALIST_BLOCK_NONE) {
			*errorp = ENOSPC;
			return(NULL);
		}
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
		elm_no = hammer_alist_alloc_fwd(live, 1, 0);
	if (elm_no == HAMMER_ALIST_BLOCK_NONE) {
		alloc_new_buffer(cluster, live,
				 HAMMER_FSBUF_DATA, HAMMER_DATA_NODES,
				 cluster->ondisk->idx_data, errorp, bufferp);
		elm_no = hammer_alist_alloc(live, nblks);
		if (elm_no == HAMMER_ALIST_BLOCK_NONE) {
			*errorp = ENOSPC;
			hammer_modify_cluster(cluster);
			return(NULL);
		}
	}
	cluster->ondisk->idx_index = elm_no;
	hammer_modify_cluster(cluster);

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
	live = &cluster->alist_record;
	kprintf("IDX_RECORD %d\n", cluster->ondisk->idx_record);
	elm_no = hammer_alist_alloc_rev(live, 1, cluster->ondisk->idx_record);
	if (elm_no == HAMMER_ALIST_BLOCK_NONE)
		elm_no = hammer_alist_alloc_rev(live, 1,HAMMER_ALIST_BLOCK_MAX);
	kprintf("hammer_alloc_record elm %08x\n", elm_no);
	if (elm_no == HAMMER_ALIST_BLOCK_NONE) {
		alloc_new_buffer(cluster, live,
				 HAMMER_FSBUF_RECORDS, HAMMER_RECORD_NODES,
				 cluster->ondisk->idx_record, errorp, bufferp);
		elm_no = hammer_alist_alloc_rev(live, 1,HAMMER_ALIST_BLOCK_MAX);
		kprintf("hammer_alloc_record elm again %08x\n", elm_no);
		if (elm_no == HAMMER_ALIST_BLOCK_NONE) {
			*errorp = ENOSPC;
			hammer_modify_cluster(cluster);
			return(NULL);
		}
	}
	cluster->ondisk->idx_record = elm_no;
	hammer_modify_cluster(cluster);

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
	KKASSERT(buffer->ondisk->head.buf_type == HAMMER_FSBUF_RECORDS);
	KKASSERT((elm_no & HAMMER_FSBUF_BLKMASK) < HAMMER_RECORD_NODES);
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

	if ((bytes & HAMMER_BUFMASK) == 0) {
		nblks = bytes / HAMMER_BUFSIZE;
		KKASSERT(nblks == 1 && data == (void *)buffer->ondisk);
		hammer_alist_free(&buffer->cluster->alist_master,
				  buffer->buf_no, nblks);
		hammer_modify_cluster(buffer->cluster);
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
	hammer_modify_cluster(buffer->cluster);
}

void
hammer_free_record_ptr(hammer_buffer_t buffer, union hammer_record_ondisk *rec)
{
	int32_t elm_no;
	hammer_alist_t live;

	elm_no = rec - &buffer->ondisk->record.recs[0];
	KKASSERT(elm_no >= 0 && elm_no < HAMMER_BTREE_NODES);
	elm_no += buffer->buf_no * HAMMER_FSBUF_MAXBLKS;
	live = &buffer->cluster->alist_record;
	hammer_alist_free(live, elm_no, 1);
	hammer_modify_cluster(buffer->cluster);
}

void
hammer_free_btree(hammer_cluster_t cluster, int32_t bclu_offset)
{
	const int32_t blksize = sizeof(struct hammer_node_ondisk);
	int32_t fsbuf_offset = bclu_offset & HAMMER_BUFMASK;
	hammer_alist_t live;
	int32_t elm_no;

	elm_no = bclu_offset / HAMMER_BUFSIZE * HAMMER_FSBUF_MAXBLKS;
	fsbuf_offset -= offsetof(union hammer_fsbuf_ondisk, btree.nodes[0]);
	live = &cluster->alist_btree;
	KKASSERT(fsbuf_offset >= 0 && fsbuf_offset % blksize == 0);
	elm_no += fsbuf_offset / blksize;
	hammer_alist_free(live, elm_no, 1);
	hammer_modify_cluster(cluster);
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

	if ((bytes & HAMMER_BUFMASK) == 0) {
		nblks = bytes / HAMMER_BUFSIZE;
		KKASSERT(nblks == 1 && (bclu_offset & HAMMER_BUFMASK) == 0);
		buf_no = bclu_offset / HAMMER_BUFSIZE;
		hammer_alist_free(&cluster->alist_master, buf_no, nblks);
		hammer_modify_cluster(cluster);
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
	hammer_modify_cluster(cluster);
}

void
hammer_free_record(hammer_cluster_t cluster, int32_t bclu_offset)
{
	const int32_t blksize = sizeof(union hammer_record_ondisk);
	int32_t fsbuf_offset = bclu_offset & HAMMER_BUFMASK;
	hammer_alist_t live;
	int32_t elm_no;

	elm_no = bclu_offset / HAMMER_BUFSIZE * HAMMER_FSBUF_MAXBLKS;
	fsbuf_offset -= offsetof(union hammer_fsbuf_ondisk, record.recs[0]);
	live = &cluster->alist_record;
	KKASSERT(fsbuf_offset >= 0 && fsbuf_offset % blksize == 0);
	elm_no += fsbuf_offset / blksize;
	hammer_alist_free(live, elm_no, 1);
	hammer_modify_cluster(cluster);
}


/*
 * Allocate a new filesystem buffer and assign it to the specified
 * filesystem buffer type.  The new buffer will be added to the
 * type-specific A-list and initialized.
 */
static void
alloc_new_buffer(hammer_cluster_t cluster, hammer_alist_t live,
		 u_int64_t type, int32_t nelements,
		 int start, int *errorp, struct hammer_buffer **bufferp)
{
	hammer_buffer_t buffer;
	int32_t buf_no;

	start = start / HAMMER_FSBUF_MAXBLKS;	/* convert to buf_no */

	if (type == HAMMER_FSBUF_RECORDS) {
		buf_no = hammer_alist_alloc_rev(&cluster->alist_master,
						1, start);
		if (buf_no == HAMMER_ALIST_BLOCK_NONE) {
			buf_no = hammer_alist_alloc_rev(&cluster->alist_master,
						1, HAMMER_ALIST_BLOCK_MAX);
		}
	} else {
		buf_no = hammer_alist_alloc_fwd(&cluster->alist_master,
						1, start);
		if (buf_no == HAMMER_ALIST_BLOCK_NONE) {
			buf_no = hammer_alist_alloc_fwd(&cluster->alist_master,
						1, 0);
		}
	}
	KKASSERT(buf_no != HAMMER_ALIST_BLOCK_NONE); /* XXX */
	hammer_modify_cluster(cluster);

	/*
	 * The new buffer must be initialized (type != 0) regardless of
	 * whether we already have it cached or not, so don't try to
	 * optimize the cached buffer check.  Just call hammer_get_buffer().
	 */
	buffer = hammer_get_buffer(cluster, buf_no, type, errorp);
	if (*bufferp)
		hammer_rel_buffer(*bufferp, 0);
	*bufferp = buffer;

	/*
	 * Finally, do a meta-free of the buffer's elements.
	 */
	if (buffer) {
		kprintf("alloc_new_buffer buf_no %d type %016llx nelms %d\n",
			buf_no, type, nelements);
		hammer_alist_free(live, buf_no * HAMMER_FSBUF_MAXBLKS,
				  nelements);
	}
}

#if 0

/*
 * Flush various tracking structures to disk
 */

/*
 * Flush various tracking structures to disk
 */
void
flush_all_volumes(void)
{
	hammer_volume_t vol;

	for (vol = VolBase; vol; vol = vol->next)
		flush_volume(vol);
}

void
flush_volume(hammer_volume_t vol)
{
	hammer_supercl_t supercl;
	hammer_cluster_t cl;

	for (supercl = vol->supercl_base; supercl; supercl = supercl->next)
		flush_supercl(supercl);
	for (cl = vol->cluster_base; cl; cl = cl->next)
		flush_cluster(cl);
	writehammerbuf(vol, vol->ondisk, 0);
}

void
flush_supercl(hammer_supercl_t supercl)
{
	int64_t supercl_offset;

	supercl_offset = supercl->scl_offset;
	writehammerbuf(supercl->volume, supercl->ondisk, supercl_offset);
}

void
flush_cluster(hammer_cluster_t cl)
{
	hammer_buffer_t buf;
	int64_t cluster_offset;

	for (buf = cl->buffer_base; buf; buf = buf->next)
		flush_buffer(buf);
	cluster_offset = cl->clu_offset;
	writehammerbuf(cl->volume, cl->ondisk, cluster_offset);
}

void
flush_buffer(hammer_buffer_t buf)
{
	int64_t buffer_offset;

	buffer_offset = buf->buf_offset + buf->cluster->clu_offset;
	writehammerbuf(buf->volume, buf->ondisk, buffer_offset);
}

#endif

/*
 * Generic buffer initialization
 */
static void
initbuffer(hammer_alist_t live, hammer_fsbuf_head_t head, u_int64_t type)
{
	head->buf_type = type;
	hammer_alist_init(live);
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
		      * HAMMER_BUFSIZE;
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
 */
static int
buffer_alist_init(void *info, int32_t blk, int32_t radix)
{
	hammer_cluster_t cluster = info;
	hammer_buffer_t buffer;
	int32_t buf_no;
	int error = 0;

	/*
	 * Calculate the buffer number, initialize based on the buffer type.
	 * The buffer has already been allocated so assert that it has been
	 * initialized.
	 */
	buf_no = blk / HAMMER_FSBUF_MAXBLKS;
	buffer = hammer_get_buffer(cluster, buf_no, 0, &error);
	if (buffer)
		hammer_rel_buffer(buffer, 0);
	return (error);
}

static int
buffer_alist_destroy(void *info, int32_t blk, int32_t radix)
{
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

		r = hammer_alist_alloc_fwd(&buffer->alist, count, atblk - blk);
		if (r != HAMMER_ALIST_BLOCK_NONE)
			r += blk;
		*fullp = hammer_alist_isfull(&buffer->alist);
		hammer_modify_buffer(buffer);
		hammer_rel_buffer(buffer, 0);
	} else {
		r = HAMMER_ALIST_BLOCK_NONE;
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

		r = hammer_alist_alloc_rev(&buffer->alist, count, atblk - blk);
		if (r != HAMMER_ALIST_BLOCK_NONE)
			r += blk;
		*fullp = hammer_alist_isfull(&buffer->alist);
		hammer_modify_buffer(buffer);
		hammer_rel_buffer(buffer, 0);
	} else {
		r = HAMMER_ALIST_BLOCK_NONE;
		*fullp = 0;
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
		hammer_alist_free(&buffer->alist, base_blk, count);
		*emptyp = hammer_alist_isempty(&buffer->alist);
		hammer_modify_buffer(buffer);
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
static int
super_alist_init(void *info, int32_t blk, int32_t radix)
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
	supercl = hammer_get_supercl(volume, scl_no, &error, 1);
	if (supercl)
		hammer_rel_supercl(supercl, 0);
	return (error);
}

static int
super_alist_destroy(void *info, int32_t blk, int32_t radix)
{
	return(0);
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
	supercl = hammer_get_supercl(volume, scl_no, &error, 1);
	if (supercl) {
		r = hammer_alist_alloc_fwd(&supercl->alist, count, atblk - blk);
		if (r != HAMMER_ALIST_BLOCK_NONE)
			r += blk;
		*fullp = hammer_alist_isfull(&supercl->alist);
		hammer_modify_supercl(supercl);
		hammer_rel_supercl(supercl, 0);
	} else {
		r = HAMMER_ALIST_BLOCK_NONE;
		*fullp = 0;
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
	supercl = hammer_get_supercl(volume, scl_no, &error, 1);
	if (supercl) {
		r = hammer_alist_alloc_rev(&supercl->alist, count, atblk - blk);
		if (r != HAMMER_ALIST_BLOCK_NONE)
			r += blk;
		*fullp = hammer_alist_isfull(&supercl->alist);
		hammer_modify_supercl(supercl);
		hammer_rel_supercl(supercl, 0);
	} else { 
		r = HAMMER_ALIST_BLOCK_NONE;
		*fullp = 0;
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
	supercl = hammer_get_supercl(volume, scl_no, &error, 1);
	if (supercl) {
		hammer_alist_free(&supercl->alist, base_blk, count);
		*emptyp = hammer_alist_isempty(&supercl->alist);
		hammer_modify_supercl(supercl);
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
	config->bl_radix_destroy = super_alist_destroy;
	config->bl_radix_alloc_fwd = super_alist_alloc_fwd;
	config->bl_radix_alloc_rev = super_alist_alloc_rev;
	config->bl_radix_free = super_alist_free;
	config->bl_radix_print = super_alist_print;

	config = &Clu_slave_alist_config;
	config->bl_radix_init = buffer_alist_init;
	config->bl_radix_destroy = buffer_alist_destroy;
	config->bl_radix_alloc_fwd = buffer_alist_alloc_fwd;
	config->bl_radix_alloc_rev = buffer_alist_alloc_rev;
	config->bl_radix_free = buffer_alist_free;
	config->bl_radix_print = buffer_alist_print;
}

