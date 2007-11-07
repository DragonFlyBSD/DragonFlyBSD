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
 * $DragonFly: src/sys/vfs/hammer/hammer_ondisk.c,v 1.3 2007/11/07 00:43:24 dillon Exp $
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

static void hammer_free_volume(struct hammer_volume *volume);
static struct hammer_cluster *
	    hammer_load_cluster(struct hammer_cluster *cluster, int *errorp,
			int new);
static void initbuffer(hammer_alist_t live, hammer_fsbuf_head_t head,
			u_int64_t type);
static void alloc_new_buffer(struct hammer_cluster *cluster,
			hammer_alist_t live, u_int64_t type, int32_t nelements,
			int32_t start,
			int *errorp, struct hammer_buffer **bufferp);
#if 0
static void readhammerbuf(struct hammer_volume *vol, void *data,
			int64_t offset);
static void writehammerbuf(struct hammer_volume *vol, const void *data,
			int64_t offset);
#endif
static int64_t calculate_cluster_offset(struct hammer_volume *vol,
			int32_t clu_no);
static int64_t calculate_supercl_offset(struct hammer_volume *vol,
			int32_t scl_no);


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
hammer_ino_rb_compare(struct hammer_inode *ip1, struct hammer_inode *ip2)
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
hammer_inode_info_cmp(hammer_inode_info_t info, struct hammer_inode *ip)
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
hammer_vol_rb_compare(struct hammer_volume *vol1, struct hammer_volume *vol2)
{
	if (vol1->vol_no < vol2->vol_no)
		return(-1);
	if (vol1->vol_no > vol2->vol_no)
		return(1);
	return(0);
}

static int
hammer_scl_rb_compare(struct hammer_supercl *cl1, struct hammer_supercl *cl2)
{
	if (cl1->scl_no < cl2->scl_no)
		return(-1);
	if (cl1->scl_no > cl2->scl_no)
		return(1);
	return(0);
}

static int
hammer_clu_rb_compare(struct hammer_cluster *cl1, struct hammer_cluster *cl2)
{
	if (cl1->clu_no < cl2->clu_no)
		return(-1);
	if (cl1->clu_no > cl2->clu_no)
		return(1);
	return(0);
}

static int
hammer_buf_rb_compare(struct hammer_buffer *buf1, struct hammer_buffer *buf2)
{
	if (buf1->buf_no < buf2->buf_no)
		return(-1);
	if (buf1->buf_no > buf2->buf_no)
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

/*
 * Load a HAMMER volume by name.  Returns 0 on success or a positive error
 * code on failure.  Volumes must be loaded at mount time, get_volume() will
 * not load a new volume.
 *
 * Calls made to hammer_load_volume() or single-threaded
 */
int
hammer_load_volume(struct hammer_mount *hmp, const char *volname)
{
	struct mount *mp;
	struct hammer_volume *volume;
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
	volume->cluster_base = ondisk->vol_beg;
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
	 */
	if (error == 0 && ondisk->vol_rootvol == ondisk->vol_no) {
		hmp->rootvol = volume;
		hmp->rootcl = hammer_get_cluster(volume,
						 ondisk->vol0_root_clu_no,
						 &error, 0);
		hammer_put_cluster(hmp->rootcl, 0);
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
hammer_unload_volume(struct hammer_volume *volume, void *data __unused)
{
	struct hammer_mount *hmp = volume->hmp;
	struct hammer_cluster *rootcl;
	int ronly = ((hmp->mp->mnt_flag & MNT_RDONLY) ? 1 : 0);

	/*
	 * Sync clusters, sync volume
	 */

	/*
	 * Clean up the root cluster, which is held unlocked in the root
	 * volume.
	 */
	if (hmp->rootvol == volume) {
		if ((rootcl = hmp->rootcl) != NULL) {
			hammer_lock(&rootcl->io.lock);
			hmp->rootcl = NULL;
			hammer_put_cluster(rootcl, 1);
		}
		hmp->rootvol = NULL;
	}

	/*
	 * Flush the volume
	 */
	hammer_io_release(&volume->io, 1);
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
hammer_free_volume(struct hammer_volume *volume)
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
struct hammer_volume *
hammer_get_volume(struct hammer_mount *hmp, int32_t vol_no, int *errorp)
{
	struct hammer_volume *volume;
	struct hammer_volume_ondisk *ondisk;

	/*
	 * Locate the volume structure
	 */
	volume = RB_LOOKUP(hammer_vol_rb_tree, &hmp->rb_vols_root, vol_no);
	if (volume == NULL) {
		*errorp = ENOENT;
		return(NULL);
	}

	/*
	 * Load the ondisk buffer if necessary.  Create a b_dep dependancy
	 * for the buffer that allows us to manage our structure with the
	 * buffer (mostly) left in a released state.
	 */
	hammer_lock(&volume->io.lock);
	if (volume->ondisk == NULL) {
		*errorp = hammer_io_read(volume->devvp, &volume->io);
		if (*errorp) {
			hammer_unlock(&volume->io.lock);
			return(NULL);
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
	}
	*errorp = 0;
	return(volume);
}

/*
 * Unlock and release a volume.
 *
 * Buffer cache interactions (applies to volumes, superclusters, clusters,
 * and buffer structures): 
 *
 */
void
hammer_put_volume(struct hammer_volume *volume, int flush)
{
	if (hammer_islastref(&volume->io.lock)) {
		hammer_io_release(&volume->io, flush);
	} else {
		hammer_unlock(&volume->io.lock);
	}
}

struct hammer_supercl *
hammer_get_supercl(struct hammer_volume *volume, int32_t scl_no,
		   int *errorp, int isnew)
{
	struct hammer_supercl_ondisk *ondisk;
	struct hammer_supercl *supercl;

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
		hammer_lock(&supercl->io.lock);

		/*
		 * Insert the cluster into the RB tree and handle late
		 * collisions.
		 */
		if (RB_INSERT(hammer_scl_rb_tree, &volume->rb_scls_root, supercl)) {
			hammer_unlock(&supercl->io.lock);
			kfree(supercl, M_HAMMER);
			goto again;
		}
		hammer_ref(&volume->io.lock);
	} else {
		hammer_lock(&supercl->io.lock);
	}

	/*
	 * Load the cluster's on-disk info
	 */
	*errorp = 0;
	if (supercl->ondisk == NULL) {
		if (isnew)
			*errorp = hammer_io_new(volume->devvp, &supercl->io);
		else
			*errorp = hammer_io_read(volume->devvp, &supercl->io);
		if (*errorp) {
			hammer_put_supercl(supercl, 1);
			return(NULL);
		}
		supercl->ondisk = ondisk = (void *)supercl->io.bp->b_data;

		supercl->alist.config = &Supercl_alist_config;
		supercl->alist.meta = ondisk->scl_meta;
		supercl->alist.info = NULL;

		/*
		 * If this is a new super-cluster we have to initialize
		 * various ondisk structural elements.  The caller is
		 * responsible for the remainder.
		 */
		if (isnew) {
			struct hammer_alist_live dummy;

			dummy.config = &Buf_alist_config;
			dummy.meta = ondisk->head.buf_almeta;
			dummy.info = NULL;
			initbuffer(&dummy, &ondisk->head, HAMMER_FSBUF_SUPERCL);
			hammer_alist_init(&supercl->alist);
		}
	} else if (isnew) {
		*errorp = hammer_io_new(volume->devvp, &supercl->io);
		if (*errorp) {
			hammer_put_supercl(supercl, 1);
			supercl = NULL;
		}
	}
	return (supercl);
}

void
hammer_put_supercl(struct hammer_supercl *supercl, int flush)
{
	struct hammer_volume *volume;

	if (hammer_islastref(&supercl->io.lock)) {
		volume = supercl->volume;
		hammer_io_release(&supercl->io, flush);
		if (supercl->io.bp == NULL &&
		    hammer_islastref(&supercl->io.lock)) {
			RB_REMOVE(hammer_scl_rb_tree,
				  &volume->rb_scls_root, supercl);
			kfree(supercl, M_HAMMER);
			if (hammer_islastref(&volume->io.lock)) {
				hammer_ref_to_lock(&volume->io.lock);
				hammer_put_volume(volume, 0);
			}
		}
	} else {
		hammer_unlock(&supercl->io.lock);
	}
}

struct hammer_cluster *
hammer_get_cluster(struct hammer_volume *volume, int32_t clu_no,
		   int *errorp, int isnew)
{
	struct hammer_cluster *cluster;

	/*
	 * Locate and lock the cluster structure, creating one if necessary.
	 */
again:
	cluster = RB_LOOKUP(hammer_clu_rb_tree, &volume->rb_clus_root, clu_no);
	if (cluster == NULL) {
		cluster = kmalloc(sizeof(*cluster), M_HAMMER, M_WAITOK|M_ZERO);
		cluster->clu_no = clu_no;
		cluster->volume = volume;
		cluster->io.offset = calculate_cluster_offset(volume, clu_no);
		RB_INIT(&cluster->rb_bufs_root);
		cluster->io.type = HAMMER_STRUCTURE_CLUSTER;
		hammer_lock(&cluster->io.lock);

		/*
		 * Insert the cluster into the RB tree and handle late
		 * collisions.
		 */
		if (RB_INSERT(hammer_clu_rb_tree, &volume->rb_clus_root, cluster)) {
			hammer_unlock(&cluster->io.lock);
			kfree(cluster, M_HAMMER);
			goto again;
		}
		hammer_ref(&volume->io.lock);
	} else {
		hammer_lock(&cluster->io.lock);
	}
	return(hammer_load_cluster(cluster, errorp, isnew));
}

struct hammer_cluster *
hammer_get_rootcl(struct hammer_mount *hmp)
{
	struct hammer_cluster *cluster = hmp->rootcl;
	int dummy_error;

	hammer_lock(&cluster->io.lock);
	return(hammer_load_cluster(cluster, &dummy_error, 0));
}


static
struct hammer_cluster *
hammer_load_cluster(struct hammer_cluster *cluster, int *errorp, int isnew)
{
	struct hammer_volume *volume = cluster->volume;
	struct hammer_cluster_ondisk *ondisk;

	/*
	 * Load the cluster's on-disk info
	 */
	*errorp = 0;
	if (cluster->ondisk == NULL) {
		if (isnew)
			*errorp = hammer_io_new(volume->devvp, &cluster->io);
		else
			*errorp = hammer_io_read(volume->devvp, &cluster->io);
		if (*errorp) {
			hammer_put_cluster(cluster, 1);
			return(NULL);
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

		/*
		 * If this is a new cluster we have to initialize
		 * various ondisk structural elements.  The caller is
		 * responsible for the remainder.
		 */
		if (isnew) {
			struct hammer_alist_live dummy;

			dummy.config = &Buf_alist_config;
			dummy.meta = ondisk->head.buf_almeta;
			dummy.info = NULL;
			initbuffer(&dummy, &ondisk->head, HAMMER_FSBUF_CLUSTER);

			hammer_alist_init(&cluster->alist_master);
			hammer_alist_init(&cluster->alist_btree);
			hammer_alist_init(&cluster->alist_record);
			hammer_alist_init(&cluster->alist_mdata);
		}
	} else if (isnew) {
		*errorp = hammer_io_new(volume->devvp, &cluster->io);
		if (*errorp) {
			hammer_put_cluster(cluster, 1);
			cluster = NULL;
		}
	}
	return(cluster);
}

void
hammer_put_cluster(struct hammer_cluster *cluster, int flush)
{
	struct hammer_volume *volume;

	if (hammer_islastref(&cluster->io.lock)) {
		volume = cluster->volume;
		hammer_io_release(&cluster->io, flush);
		if (cluster->io.bp == NULL &&
		    volume->hmp->rootcl != cluster &&
		    hammer_islastref(&cluster->io.lock)) {
			RB_REMOVE(hammer_clu_rb_tree,
				  &volume->rb_clus_root, cluster);
			kfree(cluster, M_HAMMER);
			if (hammer_islastref(&volume->io.lock)) {
				hammer_ref_to_lock(&volume->io.lock);
				hammer_put_volume(volume, 0);
			}
		}
	} else {
		hammer_unlock(&cluster->io.lock);
	}
}

/*
 * Get a buffer from a cluster.  Note that buffer #0 is the cluster header
 * itself and may not be retrieved with this function.
 *
 * If buf_type is 0 the buffer already exists in-memory or on-disk.
 * Otherwise a new buffer is initialized with the specified buffer type.
 */
struct hammer_buffer *
hammer_get_buffer(struct hammer_cluster *cluster, int32_t buf_no,
		  int64_t buf_type, int *errorp)
{
	hammer_fsbuf_ondisk_t ondisk;
	struct hammer_buffer *buffer;

	/*
	 * Find the buffer.  Note that buffer 0 corresponds to the cluster
	 * header and should never be requested.
	 */
	KKASSERT(buf_no != 0);

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
		hammer_lock(&buffer->io.lock);

		/*
		 * Insert the cluster into the RB tree and handle late
		 * collisions.
		 */
		if (RB_INSERT(hammer_buf_rb_tree, &cluster->rb_bufs_root, buffer)) {
			hammer_unlock(&buffer->io.lock);
			kfree(buffer, M_HAMMER);
			goto again;
		}
		hammer_ref(&cluster->io.lock);
	} else {
		hammer_lock(&buffer->io.lock);
	}

	*errorp = 0;
	if (buffer->ondisk == NULL) {
		if (buf_type) {
			*errorp = hammer_io_new(buffer->volume->devvp,
						&buffer->io);
		} else {
			*errorp = hammer_io_read(buffer->volume->devvp,
						&buffer->io);
		}
		if (*errorp) {
			hammer_put_buffer(buffer, 1);
			return(NULL);
		}
		buffer->ondisk = ondisk = (void *)buffer->io.bp->b_data;
		buffer->alist.config = &Buf_alist_config;
		buffer->alist.meta = ondisk->head.buf_almeta;

		if (buf_type) {
			initbuffer(&buffer->alist, &ondisk->head, buf_type);
		}
	} else if (buf_type) {
		*errorp = hammer_io_new(buffer->volume->devvp,
					&buffer->io);
		if (*errorp) {
			hammer_put_buffer(buffer, 1);
			buffer = NULL;
		}
	}
	return (buffer);
}

void
hammer_put_buffer(struct hammer_buffer *buffer, int flush)
{
	struct hammer_cluster *cluster;

	if (hammer_islastref(&buffer->io.lock)) {
		hammer_io_release(&buffer->io, flush);
		if (buffer->io.bp == NULL &&
		    hammer_islastref(&buffer->io.lock)) {
			RB_REMOVE(hammer_buf_rb_tree,
				  &buffer->cluster->rb_bufs_root, buffer);
			cluster = buffer->cluster;
			kfree(buffer, M_HAMMER);
			if (hammer_islastref(&cluster->io.lock)) {
				hammer_ref_to_lock(&cluster->io.lock);
				hammer_put_cluster(cluster, 0);
			}
		}
	} else {
		hammer_unlock(&buffer->io.lock);
	}
}

void
hammer_dup_buffer(struct hammer_buffer **bufferp, struct hammer_buffer *buffer)
{
	if (buffer != *bufferp) {
		if (buffer)
			hammer_lock(&buffer->io.lock);
		if (*bufferp)
			hammer_put_buffer(*bufferp, 0);
		*bufferp = buffer;
	}
}

void
hammer_dup_cluster(struct hammer_cluster **clusterp,
		   struct hammer_cluster *cluster)
{
	if (cluster != *clusterp) {
		if (cluster)
			hammer_lock(&cluster->io.lock);
		if (*clusterp)
			hammer_put_cluster(*clusterp, 0);
		*clusterp = cluster;
	}
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
void *
hammer_alloc_btree(struct hammer_cluster *cluster,
		    int *errorp, struct hammer_buffer **bufferp)
{
	struct hammer_buffer *buffer;
	hammer_alist_t live;
	int32_t elm_no;
	int32_t buf_no;
	void *item;

	/*
	 * Allocate a B-Tree element
	 */
	live = &cluster->alist_btree;
	elm_no = hammer_alist_alloc_fwd(live, 1, cluster->ondisk->idx_index);
	if (elm_no == HAMMER_ALIST_BLOCK_NONE)
		elm_no = hammer_alist_alloc_fwd(live, 1, 0);
	if (elm_no == HAMMER_ALIST_BLOCK_NONE) {
		alloc_new_buffer(cluster, live,
				 HAMMER_FSBUF_BTREE, HAMMER_BTREE_NODES,
				 cluster->ondisk->idx_index, errorp, bufferp);
		elm_no = hammer_alist_alloc(live, 1);
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
			hammer_put_buffer(buffer, 0);
		buffer = hammer_get_buffer(cluster, buf_no, 0, errorp);
		*bufferp = buffer;
	}
	KKASSERT(buffer->ondisk->head.buf_type == HAMMER_FSBUF_BTREE);
	item = &buffer->ondisk->btree.nodes[elm_no & HAMMER_FSBUF_BLKMASK];
	bzero(item, sizeof(union hammer_btree_node));
	*errorp = 0;
	return(item);
}

void *
hammer_alloc_data(struct hammer_cluster *cluster, int32_t bytes,
		   int *errorp, struct hammer_buffer **bufferp)
{
	struct hammer_buffer *buffer;
	hammer_alist_t live;
	int32_t elm_no;
	int32_t buf_no;
	int32_t nblks;
	void *item;

	/*
	 * Allocate a data element
	 */
	nblks = (bytes + HAMMER_DATA_BLKMASK) & ~HAMMER_DATA_BLKMASK;
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
			hammer_put_buffer(buffer, 0);
		buffer = hammer_get_buffer(cluster, buf_no, 0, errorp);
		*bufferp = buffer;
	}
	KKASSERT(buffer->ondisk->head.buf_type == HAMMER_FSBUF_BTREE);
	item = &buffer->ondisk->data.data[elm_no & HAMMER_FSBUF_BLKMASK];
	bzero(item, nblks * HAMMER_DATA_BLKSIZE);
	*errorp = 0;
	return(item);
}

void *
hammer_alloc_record(struct hammer_cluster *cluster,
		     int *errorp, struct hammer_buffer **bufferp)
{
	struct hammer_buffer *buffer;
	hammer_alist_t live;
	int32_t elm_no;
	int32_t buf_no;
	void *item;

	/*
	 * Allocate a record element
	 */
	live = &cluster->alist_record;
	elm_no = hammer_alist_alloc_rev(live, 1, cluster->ondisk->idx_record);
	if (elm_no == HAMMER_ALIST_BLOCK_NONE)
		elm_no = hammer_alist_alloc_rev(live, 1,HAMMER_ALIST_BLOCK_MAX);
	if (elm_no == HAMMER_ALIST_BLOCK_NONE) {
		alloc_new_buffer(cluster, live,
				 HAMMER_FSBUF_RECORDS, HAMMER_RECORD_NODES,
				 cluster->ondisk->idx_record, errorp, bufferp);
		elm_no = hammer_alist_alloc(live, 1);
		if (elm_no == HAMMER_ALIST_BLOCK_NONE) {
			*errorp = ENOSPC;
			return(NULL);
		}
	}
	cluster->ondisk->idx_record = elm_no;

	/*
	 * Load and return the B-Tree element
	 */
	buf_no = elm_no / HAMMER_FSBUF_MAXBLKS;
	buffer = *bufferp;
	if (buffer == NULL || buffer->cluster != cluster ||
	    buffer->buf_no != buf_no) {
		if (buffer)
			hammer_put_buffer(buffer, 0);
		buffer = hammer_get_buffer(cluster, buf_no, 0, errorp);
		*bufferp = buffer;
	}
	KKASSERT(buffer->ondisk->head.buf_type != 0);
	item = &buffer->ondisk->record.recs[elm_no & HAMMER_FSBUF_BLKMASK];
	bzero(item, sizeof(union hammer_record_ondisk));
	*errorp = 0;
	return(item);
}

/*
 * Free HAMMER elements based on either a hammer_buffer and element pointer
 * or a cluster-relative byte offset.
 */
void
hammer_free_btree_ptr(struct hammer_buffer *buffer, hammer_btree_node_t node)
{
	int32_t elm_no;
	hammer_alist_t live;

	elm_no = node - buffer->ondisk->btree.nodes;
	KKASSERT(elm_no >= 0 && elm_no < HAMMER_BTREE_NODES);
	elm_no += buffer->buf_no * HAMMER_FSBUF_MAXBLKS;
	live = &buffer->cluster->alist_btree;
	hammer_alist_free(live, elm_no, 1);
}

void
hammer_free_data_ptr(struct hammer_buffer *buffer, void *data, int bytes)
{
	int32_t elm_no;
	int32_t nblks;
	hammer_alist_t live;

	elm_no = ((char *)data - (char *)buffer->ondisk->data.data) /
		 HAMMER_DATA_BLKSIZE;
	KKASSERT(elm_no >= 0 && elm_no < HAMMER_DATA_NODES);
	elm_no += buffer->buf_no * HAMMER_FSBUF_MAXBLKS;
	nblks = (bytes + HAMMER_DATA_BLKMASK) & ~HAMMER_DATA_BLKMASK;
	live = &buffer->cluster->alist_mdata;
	hammer_alist_free(live, elm_no, nblks);
}

void
hammer_free_record_ptr(struct hammer_buffer *buffer,
		       union hammer_record_ondisk *rec)
{
	int32_t elm_no;
	hammer_alist_t live;

	elm_no = rec - &buffer->ondisk->record.recs[0];
	KKASSERT(elm_no >= 0 && elm_no < HAMMER_BTREE_NODES);
	elm_no += buffer->buf_no * HAMMER_FSBUF_MAXBLKS;
	live = &buffer->cluster->alist_record;
	hammer_alist_free(live, elm_no, 1);
}

void
hammer_free_btree(struct hammer_cluster *cluster, int32_t bclu_offset)
{
	const int32_t blksize = sizeof(union hammer_btree_node);
	int32_t fsbuf_offset = bclu_offset & HAMMER_BUFMASK;
	hammer_alist_t live;
	int32_t elm_no;

	elm_no = bclu_offset / HAMMER_BUFSIZE * HAMMER_FSBUF_MAXBLKS;
	fsbuf_offset -= offsetof(union hammer_fsbuf_ondisk, btree.nodes[0]);
	live = &cluster->alist_btree;
	KKASSERT(fsbuf_offset >= 0 && fsbuf_offset % blksize == 0);
	elm_no += fsbuf_offset / blksize;
	hammer_alist_free(live, elm_no, 1);
}

void
hammer_free_data(struct hammer_cluster *cluster, int32_t bclu_offset,
		 int32_t bytes)
{
	const int32_t blksize = HAMMER_DATA_BLKSIZE;
	int32_t fsbuf_offset = bclu_offset & HAMMER_BUFMASK;
	hammer_alist_t live;
	int32_t elm_no;
	int32_t nblks;

	elm_no = bclu_offset / HAMMER_BUFSIZE * HAMMER_FSBUF_MAXBLKS;
	fsbuf_offset -= offsetof(union hammer_fsbuf_ondisk, data.data[0][0]);
	live = &cluster->alist_mdata;
	nblks = (bytes + HAMMER_DATA_BLKMASK) & ~HAMMER_DATA_BLKMASK;
	KKASSERT(fsbuf_offset >= 0 && fsbuf_offset % blksize == 0);
	elm_no += fsbuf_offset / blksize;
	hammer_alist_free(live, elm_no, nblks);
}

void
hammer_free_record(struct hammer_cluster *cluster, int32_t bclu_offset)
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
}


/*
 * Allocate a new filesystem buffer and assign it to the specified
 * filesystem buffer type.  The new buffer will be added to the
 * type-specific A-list and initialized.
 */
static void
alloc_new_buffer(struct hammer_cluster *cluster, hammer_alist_t live,
		 u_int64_t type, int32_t nelements,
		 int start, int *errorp, struct hammer_buffer **bufferp)
{
	struct hammer_buffer *buffer;
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

	/*
	 * The new buffer must be initialized (type != 0) regardless of
	 * whether we already have it cached or not, so don't try to
	 * optimize the cached buffer check.  Just call hammer_get_buffer().
	 */
	buffer = hammer_get_buffer(cluster, buf_no, type, errorp);
	if (*bufferp)
		hammer_put_buffer(*bufferp, 0);
	*bufferp = buffer;
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
	struct hammer_volume *vol;

	for (vol = VolBase; vol; vol = vol->next)
		flush_volume(vol);
}

void
flush_volume(struct hammer_volume *vol)
{
	struct hammer_supercl *supercl;
	struct hammer_cluster *cl;

	for (supercl = vol->supercl_base; supercl; supercl = supercl->next)
		flush_supercl(supercl);
	for (cl = vol->cluster_base; cl; cl = cl->next)
		flush_cluster(cl);
	writehammerbuf(vol, vol->ondisk, 0);
}

void
flush_supercl(struct hammer_supercl *supercl)
{
	int64_t supercl_offset;

	supercl_offset = supercl->scl_offset;
	writehammerbuf(supercl->volume, supercl->ondisk, supercl_offset);
}

void
flush_cluster(struct hammer_cluster *cl)
{
	struct hammer_buffer *buf;
	int64_t cluster_offset;

	for (buf = cl->buffer_base; buf; buf = buf->next)
		flush_buffer(buf);
	cluster_offset = cl->clu_offset;
	writehammerbuf(cl->volume, cl->ondisk, cluster_offset);
}

void
flush_buffer(struct hammer_buffer *buf)
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
calculate_cluster_offset(struct hammer_volume *volume, int32_t clu_no)
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
calculate_supercl_offset(struct hammer_volume *volume, int32_t scl_no)
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
	struct hammer_cluster *cluster = info;
	struct hammer_buffer *buffer;
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
		hammer_put_buffer(buffer, 0);
	return (error);
}

static int
buffer_alist_destroy(void *info, int32_t blk, int32_t radix)
{
	return (0);
}

static int
buffer_alist_alloc_fwd(void *info, int32_t blk, int32_t radix,
                      int32_t count, int32_t atblk, int32_t *fullp)
{
	struct hammer_cluster *cluster = info;
	struct hammer_buffer *buffer;
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
		hammer_put_buffer(buffer, 0);
	} else {
		r = HAMMER_ALIST_BLOCK_NONE;
	}
	return(r);
}

static int
buffer_alist_alloc_rev(void *info, int32_t blk, int32_t radix,
                      int32_t count, int32_t atblk, int32_t *fullp)
{
	struct hammer_cluster *cluster = info;
	struct hammer_buffer *buffer;
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
		hammer_put_buffer(buffer, 0);
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
	struct hammer_cluster *cluster = info;
	struct hammer_buffer *buffer;
	int32_t buf_no;
	int error = 0;

	buf_no = blk / HAMMER_FSBUF_MAXBLKS;
	buffer = hammer_get_buffer(cluster, buf_no, 0, &error);
	if (buffer) {
		KKASSERT(buffer->ondisk->head.buf_type != 0);
		hammer_alist_free(&buffer->alist, base_blk, count);
		*emptyp = hammer_alist_isempty(&buffer->alist);
		hammer_put_buffer(buffer, 0);
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
	struct hammer_volume *volume = info;
	struct hammer_supercl *supercl;
	int32_t scl_no;
	int error = 0;

	/*
	 * Calculate the super-cluster number containing the cluster (blk)
	 * and obtain the super-cluster buffer.
	 */
	scl_no = blk / HAMMER_SCL_MAXCLUSTERS;
	supercl = hammer_get_supercl(volume, scl_no, &error, 1);
	if (supercl)
		hammer_put_supercl(supercl, 0);
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
	struct hammer_volume *volume = info;
	struct hammer_supercl *supercl;
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
		hammer_put_supercl(supercl, 0);
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
	struct hammer_volume *volume = info;
	struct hammer_supercl *supercl;
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
		hammer_put_supercl(supercl, 0);
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
	struct hammer_volume *volume = info;
	struct hammer_supercl *supercl;
	int32_t scl_no;
	int error = 0;

	scl_no = blk / HAMMER_SCL_MAXCLUSTERS;
	supercl = hammer_get_supercl(volume, scl_no, &error, 1);
	if (supercl) {
		hammer_alist_free(&supercl->alist, base_blk, count);
		*emptyp = hammer_alist_isempty(&supercl->alist);
		hammer_put_supercl(supercl, 0);
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
			      HAMMER_VOL_MAXSUPERCLUSTERS,
			      HAMMER_SCL_MAXCLUSTERS, HAMMER_VOL_METAELMS_2LYR);
	hammer_alist_template(&Supercl_alist_config, HAMMER_VOL_MAXCLUSTERS,
			      1, HAMMER_SUPERCL_METAELMS);
	hammer_alist_template(&Clu_master_alist_config, HAMMER_CLU_MAXBUFFERS,
			      1, HAMMER_CLU_MASTER_METAELMS);
	hammer_alist_template(&Clu_slave_alist_config, HAMMER_CLU_MAXBUFFERS,
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

