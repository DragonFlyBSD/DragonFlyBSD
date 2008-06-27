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
 * $DragonFly: src/sys/vfs/hammer/hammer_vfsops.c,v 1.55 2008/06/27 20:56:59 dillon Exp $
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/vnode.h>
#include <sys/mount.h>
#include <sys/malloc.h>
#include <sys/nlookup.h>
#include <sys/fcntl.h>
#include <sys/sysctl.h>
#include <sys/buf.h>
#include <sys/buf2.h>
#include "hammer.h"

int hammer_debug_io;
int hammer_debug_general;
int hammer_debug_debug;
int hammer_debug_inode;
int hammer_debug_locks;
int hammer_debug_btree;
int hammer_debug_tid;
int hammer_debug_recover;		/* -1 will disable, +1 will force */
int hammer_debug_recover_faults;
int hammer_debug_cluster_enable = 1;	/* enable read clustering by default */
int hammer_count_inodes;
int hammer_count_iqueued;
int hammer_count_reclaiming;
int hammer_count_records;
int hammer_count_record_datas;
int hammer_count_volumes;
int hammer_count_buffers;
int hammer_count_nodes;
int64_t hammer_stats_btree_lookups;
int64_t hammer_stats_btree_searches;
int64_t hammer_stats_btree_inserts;
int64_t hammer_stats_btree_deletes;
int64_t hammer_stats_btree_elements;
int64_t hammer_stats_btree_splits;
int64_t hammer_stats_btree_iterations;
int64_t hammer_stats_record_iterations;
int hammer_count_dirtybufs;		/* global */
int hammer_count_refedbufs;		/* global */
int hammer_count_reservations;
int hammer_count_io_running_read;
int hammer_count_io_running_write;
int hammer_count_io_locked;
int hammer_limit_dirtybufs;		/* per-mount */
int hammer_limit_recs;			/* as a whole XXX */
int hammer_limit_iqueued;		/* per-mount */
int hammer_bio_count;
int hammer_verify_zone;
int hammer_write_mode;
int64_t hammer_contention_count;
int64_t hammer_zone_limit;

SYSCTL_NODE(_vfs, OID_AUTO, hammer, CTLFLAG_RW, 0, "HAMMER filesystem");
SYSCTL_INT(_vfs_hammer, OID_AUTO, debug_general, CTLFLAG_RW,
	   &hammer_debug_general, 0, "");
SYSCTL_INT(_vfs_hammer, OID_AUTO, debug_io, CTLFLAG_RW,
	   &hammer_debug_io, 0, "");
SYSCTL_INT(_vfs_hammer, OID_AUTO, debug_debug, CTLFLAG_RW,
	   &hammer_debug_debug, 0, "");
SYSCTL_INT(_vfs_hammer, OID_AUTO, debug_inode, CTLFLAG_RW,
	   &hammer_debug_inode, 0, "");
SYSCTL_INT(_vfs_hammer, OID_AUTO, debug_locks, CTLFLAG_RW,
	   &hammer_debug_locks, 0, "");
SYSCTL_INT(_vfs_hammer, OID_AUTO, debug_btree, CTLFLAG_RW,
	   &hammer_debug_btree, 0, "");
SYSCTL_INT(_vfs_hammer, OID_AUTO, debug_tid, CTLFLAG_RW,
	   &hammer_debug_tid, 0, "");
SYSCTL_INT(_vfs_hammer, OID_AUTO, debug_recover, CTLFLAG_RW,
	   &hammer_debug_recover, 0, "");
SYSCTL_INT(_vfs_hammer, OID_AUTO, debug_recover_faults, CTLFLAG_RW,
	   &hammer_debug_recover_faults, 0, "");
SYSCTL_INT(_vfs_hammer, OID_AUTO, debug_cluster_enable, CTLFLAG_RW,
	   &hammer_debug_cluster_enable, 0, "");

SYSCTL_INT(_vfs_hammer, OID_AUTO, limit_dirtybufs, CTLFLAG_RW,
	   &hammer_limit_dirtybufs, 0, "");
SYSCTL_INT(_vfs_hammer, OID_AUTO, limit_recs, CTLFLAG_RW,
	   &hammer_limit_recs, 0, "");
SYSCTL_INT(_vfs_hammer, OID_AUTO, limit_iqueued, CTLFLAG_RW,
	   &hammer_limit_iqueued, 0, "");

SYSCTL_INT(_vfs_hammer, OID_AUTO, count_inodes, CTLFLAG_RD,
	   &hammer_count_inodes, 0, "");
SYSCTL_INT(_vfs_hammer, OID_AUTO, count_iqueued, CTLFLAG_RD,
	   &hammer_count_iqueued, 0, "");
SYSCTL_INT(_vfs_hammer, OID_AUTO, count_reclaiming, CTLFLAG_RD,
	   &hammer_count_reclaiming, 0, "");
SYSCTL_INT(_vfs_hammer, OID_AUTO, count_records, CTLFLAG_RD,
	   &hammer_count_records, 0, "");
SYSCTL_INT(_vfs_hammer, OID_AUTO, count_record_datas, CTLFLAG_RD,
	   &hammer_count_record_datas, 0, "");
SYSCTL_INT(_vfs_hammer, OID_AUTO, count_volumes, CTLFLAG_RD,
	   &hammer_count_volumes, 0, "");
SYSCTL_INT(_vfs_hammer, OID_AUTO, count_buffers, CTLFLAG_RD,
	   &hammer_count_buffers, 0, "");
SYSCTL_INT(_vfs_hammer, OID_AUTO, count_nodes, CTLFLAG_RD,
	   &hammer_count_nodes, 0, "");
SYSCTL_QUAD(_vfs_hammer, OID_AUTO, stats_btree_searches, CTLFLAG_RD,
	   &hammer_stats_btree_searches, 0, "");
SYSCTL_QUAD(_vfs_hammer, OID_AUTO, stats_btree_lookups, CTLFLAG_RD,
	   &hammer_stats_btree_lookups, 0, "");
SYSCTL_QUAD(_vfs_hammer, OID_AUTO, stats_btree_inserts, CTLFLAG_RD,
	   &hammer_stats_btree_inserts, 0, "");
SYSCTL_QUAD(_vfs_hammer, OID_AUTO, stats_btree_deletes, CTLFLAG_RD,
	   &hammer_stats_btree_deletes, 0, "");
SYSCTL_QUAD(_vfs_hammer, OID_AUTO, stats_btree_elements, CTLFLAG_RD,
	   &hammer_stats_btree_elements, 0, "");
SYSCTL_QUAD(_vfs_hammer, OID_AUTO, stats_btree_splits, CTLFLAG_RD,
	   &hammer_stats_btree_splits, 0, "");
SYSCTL_QUAD(_vfs_hammer, OID_AUTO, stats_btree_iterations, CTLFLAG_RD,
	   &hammer_stats_btree_iterations, 0, "");
SYSCTL_QUAD(_vfs_hammer, OID_AUTO, stats_record_iterations, CTLFLAG_RD,
	   &hammer_stats_record_iterations, 0, "");
SYSCTL_INT(_vfs_hammer, OID_AUTO, count_dirtybufs, CTLFLAG_RD,
	   &hammer_count_dirtybufs, 0, "");
SYSCTL_INT(_vfs_hammer, OID_AUTO, count_refedbufs, CTLFLAG_RD,
	   &hammer_count_refedbufs, 0, "");
SYSCTL_INT(_vfs_hammer, OID_AUTO, count_reservations, CTLFLAG_RD,
	   &hammer_count_reservations, 0, "");
SYSCTL_INT(_vfs_hammer, OID_AUTO, count_io_running_read, CTLFLAG_RD,
	   &hammer_count_io_running_read, 0, "");
SYSCTL_INT(_vfs_hammer, OID_AUTO, count_io_locked, CTLFLAG_RD,
	   &hammer_count_io_locked, 0, "");
SYSCTL_INT(_vfs_hammer, OID_AUTO, count_io_running_write, CTLFLAG_RD,
	   &hammer_count_io_running_write, 0, "");
SYSCTL_QUAD(_vfs_hammer, OID_AUTO, zone_limit, CTLFLAG_RW,
	   &hammer_zone_limit, 0, "");
SYSCTL_QUAD(_vfs_hammer, OID_AUTO, contention_count, CTLFLAG_RW,
	   &hammer_contention_count, 0, "");
SYSCTL_INT(_vfs_hammer, OID_AUTO, verify_zone, CTLFLAG_RW,
	   &hammer_verify_zone, 0, "");
SYSCTL_INT(_vfs_hammer, OID_AUTO, write_mode, CTLFLAG_RW,
	   &hammer_write_mode, 0, "");

KTR_INFO_MASTER(hammer);

/*
 * VFS ABI
 */
static void	hammer_free_hmp(struct mount *mp);

static int	hammer_vfs_mount(struct mount *mp, char *path, caddr_t data,
				struct ucred *cred);
static int	hammer_vfs_unmount(struct mount *mp, int mntflags);
static int	hammer_vfs_root(struct mount *mp, struct vnode **vpp);
static int	hammer_vfs_statfs(struct mount *mp, struct statfs *sbp,
				struct ucred *cred);
static int	hammer_vfs_statvfs(struct mount *mp, struct statvfs *sbp,
				struct ucred *cred);
static int	hammer_vfs_sync(struct mount *mp, int waitfor);
static int	hammer_vfs_vget(struct mount *mp, ino_t ino,
				struct vnode **vpp);
static int	hammer_vfs_init(struct vfsconf *conf);
static int	hammer_vfs_fhtovp(struct mount *mp, struct fid *fhp,
				struct vnode **vpp);
static int	hammer_vfs_vptofh(struct vnode *vp, struct fid *fhp);
static int	hammer_vfs_checkexp(struct mount *mp, struct sockaddr *nam,
				int *exflagsp, struct ucred **credanonp);


static struct vfsops hammer_vfsops = {
	.vfs_mount	= hammer_vfs_mount,
	.vfs_unmount	= hammer_vfs_unmount,
	.vfs_root 	= hammer_vfs_root,
	.vfs_statfs	= hammer_vfs_statfs,
	.vfs_statvfs	= hammer_vfs_statvfs,
	.vfs_sync	= hammer_vfs_sync,
	.vfs_vget	= hammer_vfs_vget,
	.vfs_init	= hammer_vfs_init,
	.vfs_vptofh	= hammer_vfs_vptofh,
	.vfs_fhtovp	= hammer_vfs_fhtovp,
	.vfs_checkexp	= hammer_vfs_checkexp
};

MALLOC_DEFINE(M_HAMMER, "hammer-mount", "hammer mount");

VFS_SET(hammer_vfsops, hammer, 0);
MODULE_VERSION(hammer, 1);

static int
hammer_vfs_init(struct vfsconf *conf)
{
	if (hammer_limit_recs == 0)		/* XXX TODO */
		hammer_limit_recs = nbuf * 25;
	if (hammer_limit_dirtybufs == 0) {
		hammer_limit_dirtybufs = hidirtybuffers / 2;
		if (hammer_limit_dirtybufs < 100)
			hammer_limit_dirtybufs = 100;
	}
	if (hammer_limit_iqueued == 0)
		hammer_limit_iqueued = desiredvnodes / 5;
	return(0);
}

static int
hammer_vfs_mount(struct mount *mp, char *mntpt, caddr_t data,
		 struct ucred *cred)
{
	struct hammer_mount_info info;
	hammer_mount_t hmp;
	hammer_volume_t rootvol;
	struct vnode *rootvp;
	const char *upath;	/* volume name in userspace */
	char *path;		/* volume name in system space */
	int error;
	int i;

	if ((error = copyin(data, &info, sizeof(info))) != 0)
		return (error);
	if ((mp->mnt_flag & MNT_UPDATE) == 0) {
		if (info.nvolumes <= 0 || info.nvolumes >= 32768)
			return (EINVAL);
	}
	if ((info.hflags & HMNT_MASTERID) &&
	    (info.masterid < -1 || info.masterid >= HAMMER_MAX_MASTERS)) {
			return (EINVAL);
	}

	/*
	 * Interal mount data structure
	 */
	if (mp->mnt_flag & MNT_UPDATE) {
		hmp = (void *)mp->mnt_data;
		KKASSERT(hmp != NULL);
	} else {
		hmp = kmalloc(sizeof(*hmp), M_HAMMER, M_WAITOK | M_ZERO);
		mp->mnt_data = (qaddr_t)hmp;
		hmp->mp = mp;
		hmp->namekey_iterator = mycpu->gd_time_seconds;
		/*TAILQ_INIT(&hmp->recycle_list);*/

		hmp->root_btree_beg.localization = 0x00000000U;
		hmp->root_btree_beg.obj_id = -0x8000000000000000LL;
		hmp->root_btree_beg.key = -0x8000000000000000LL;
		hmp->root_btree_beg.create_tid = 1;
		hmp->root_btree_beg.delete_tid = 1;
		hmp->root_btree_beg.rec_type = 0;
		hmp->root_btree_beg.obj_type = 0;

		hmp->root_btree_end.localization = 0xFFFFFFFFU;
		hmp->root_btree_end.obj_id = 0x7FFFFFFFFFFFFFFFLL;
		hmp->root_btree_end.key = 0x7FFFFFFFFFFFFFFFLL;
		hmp->root_btree_end.create_tid = 0xFFFFFFFFFFFFFFFFULL;
		hmp->root_btree_end.delete_tid = 0;   /* special case */
		hmp->root_btree_end.rec_type = 0xFFFFU;
		hmp->root_btree_end.obj_type = 0;

		hmp->sync_lock.refs = 1;
		hmp->free_lock.refs = 1;
		hmp->undo_lock.refs = 1;
		hmp->blkmap_lock.refs = 1;

		TAILQ_INIT(&hmp->flush_list);
		TAILQ_INIT(&hmp->delay_list);
		TAILQ_INIT(&hmp->objid_cache_list);
		TAILQ_INIT(&hmp->undo_lru_list);
		TAILQ_INIT(&hmp->reclaim_list);
	}
	hmp->hflags &= ~HMNT_USERFLAGS;
	hmp->hflags |= info.hflags & HMNT_USERFLAGS;
	if (info.hflags & HMNT_MASTERID)
		hmp->masterid = info.masterid;
	else
		hmp->masterid = -1;
	if (info.asof) {
		kprintf("ASOF\n");
		mp->mnt_flag |= MNT_RDONLY;
		hmp->asof = info.asof;
	} else {
		hmp->asof = HAMMER_MAX_TID;
	}

	/*
	 * Re-open read-write if originally read-only, or vise-versa.
	 */
	if (mp->mnt_flag & MNT_UPDATE) {
		error = 0;
		if (hmp->ronly && (mp->mnt_kern_flag & MNTK_WANTRDWR)) {
			kprintf("HAMMER read-only -> read-write\n");
			hmp->ronly = 0;
			RB_SCAN(hammer_vol_rb_tree, &hmp->rb_vols_root, NULL,
				hammer_adjust_volume_mode, NULL);
			rootvol = hammer_get_root_volume(hmp, &error);
			if (rootvol) {
				hammer_recover_flush_buffers(hmp, rootvol, 1);
				bcopy(rootvol->ondisk->vol0_blockmap,
				      hmp->blockmap,
				      sizeof(hmp->blockmap));
				hammer_rel_volume(rootvol, 0);
			}
			RB_SCAN(hammer_ino_rb_tree, &hmp->rb_inos_root, NULL,
				hammer_reload_inode, NULL);
			/* kernel clears MNT_RDONLY */
		} else if (hmp->ronly == 0 && (mp->mnt_flag & MNT_RDONLY)) {
			kprintf("HAMMER read-write -> read-only\n");
			hmp->ronly = 1;	/* messy */
			RB_SCAN(hammer_ino_rb_tree, &hmp->rb_inos_root, NULL,
				hammer_reload_inode, NULL);
			hmp->ronly = 0;
			hammer_flusher_sync(hmp);
			hammer_flusher_sync(hmp);
			hammer_flusher_sync(hmp);
			hmp->ronly = 1;
			RB_SCAN(hammer_vol_rb_tree, &hmp->rb_vols_root, NULL,
				hammer_adjust_volume_mode, NULL);
		}
		return(error);
	}

	RB_INIT(&hmp->rb_vols_root);
	RB_INIT(&hmp->rb_inos_root);
	RB_INIT(&hmp->rb_nods_root);
	RB_INIT(&hmp->rb_undo_root);
	RB_INIT(&hmp->rb_resv_root);
	RB_INIT(&hmp->rb_bufs_root);

	hmp->ronly = ((mp->mnt_flag & MNT_RDONLY) != 0);

	TAILQ_INIT(&hmp->volu_list);
	TAILQ_INIT(&hmp->undo_list);
	TAILQ_INIT(&hmp->data_list);
	TAILQ_INIT(&hmp->meta_list);
	TAILQ_INIT(&hmp->lose_list);

	/*
	 * Load volumes
	 */
	path = objcache_get(namei_oc, M_WAITOK);
	hmp->nvolumes = info.nvolumes;
	for (i = 0; i < info.nvolumes; ++i) {
		error = copyin(&info.volumes[i], &upath, sizeof(char *));
		if (error == 0)
			error = copyinstr(upath, path, MAXPATHLEN, NULL);
		if (error == 0)
			error = hammer_install_volume(hmp, path);
		if (error)
			break;
	}
	objcache_put(namei_oc, path);

	/*
	 * Make sure we found a root volume
	 */
	if (error == 0 && hmp->rootvol == NULL) {
		kprintf("hammer_mount: No root volume found!\n");
		error = EINVAL;
	}
	if (error) {
		hammer_free_hmp(mp);
		return (error);
	}

	/*
	 * No errors, setup enough of the mount point so we can lookup the
	 * root vnode.
	 */
	mp->mnt_iosize_max = MAXPHYS;
	mp->mnt_kern_flag |= MNTK_FSMID;

	/* 
	 * note: f_iosize is used by vnode_pager_haspage() when constructing
	 * its VOP_BMAP call.
	 */
	mp->mnt_stat.f_iosize = HAMMER_BUFSIZE;
	mp->mnt_stat.f_bsize = HAMMER_BUFSIZE;

	mp->mnt_vstat.f_frsize = HAMMER_BUFSIZE;
	mp->mnt_vstat.f_bsize = HAMMER_BUFSIZE;

	mp->mnt_maxsymlinklen = 255;
	mp->mnt_flag |= MNT_LOCAL;

	vfs_add_vnodeops(mp, &hammer_vnode_vops, &mp->mnt_vn_norm_ops);
	vfs_add_vnodeops(mp, &hammer_spec_vops, &mp->mnt_vn_spec_ops);
	vfs_add_vnodeops(mp, &hammer_fifo_vops, &mp->mnt_vn_fifo_ops);

	/*
	 * The root volume's ondisk pointer is only valid if we hold a
	 * reference to it.
	 */
	rootvol = hammer_get_root_volume(hmp, &error);
	if (error)
		goto failed;

	/*
	 * Perform any necessary UNDO operations.  The recovery code does
	 * call hammer_undo_lookup() so we have to pre-cache the blockmap,
	 * and then re-copy it again after recovery is complete.
	 *
	 * If this is a read-only mount the UNDO information is retained
	 * in memory in the form of dirty buffer cache buffers, and not
	 * written back to the media.
	 */
	bcopy(rootvol->ondisk->vol0_blockmap, hmp->blockmap,
	      sizeof(hmp->blockmap));

	error = hammer_recover(hmp, rootvol);
	if (error) {
		kprintf("Failed to recover HAMMER filesystem on mount\n");
		goto done;
	}

	/*
	 * Finish setup now that we have a good root volume
	 */
	ksnprintf(mp->mnt_stat.f_mntfromname,
		  sizeof(mp->mnt_stat.f_mntfromname), "%s",
		  rootvol->ondisk->vol_name);
	mp->mnt_stat.f_fsid.val[0] =
		crc32((char *)&rootvol->ondisk->vol_fsid + 0, 8);
	mp->mnt_stat.f_fsid.val[1] =
		crc32((char *)&rootvol->ondisk->vol_fsid + 8, 8);

	mp->mnt_vstat.f_fsid_uuid = rootvol->ondisk->vol_fsid;
	mp->mnt_vstat.f_fsid = crc32(&mp->mnt_vstat.f_fsid_uuid,
				     sizeof(mp->mnt_vstat.f_fsid_uuid));

	/*
	 * Certain often-modified fields in the root volume are cached in
	 * the hammer_mount structure so we do not have to generate lots
	 * of little UNDO structures for them.
	 *
	 * Recopy after recovery.  This also has the side effect of
	 * setting our cached undo FIFO's first_offset, which serves to
	 * placemark the FIFO start for the NEXT flush cycle while the
	 * on-disk first_offset represents the LAST flush cycle.
	 */
	hmp->next_tid = rootvol->ondisk->vol0_next_tid;
	bcopy(rootvol->ondisk->vol0_blockmap, hmp->blockmap,
	      sizeof(hmp->blockmap));
	hmp->copy_stat_freebigblocks = rootvol->ondisk->vol0_stat_freebigblocks;

	hammer_flusher_create(hmp);

	/*
	 * Locate the root directory using the root cluster's B-Tree as a
	 * starting point.  The root directory uses an obj_id of 1.
	 *
	 * FUTURE: Leave the root directory cached referenced but unlocked
	 * in hmp->rootvp (need to flush it on unmount).
	 */
	error = hammer_vfs_vget(mp, 1, &rootvp);
	if (error)
		goto done;
	vput(rootvp);
	/*vn_unlock(hmp->rootvp);*/

done:
	hammer_rel_volume(rootvol, 0);
failed:
	/*
	 * Cleanup and return.
	 */
	if (error)
		hammer_free_hmp(mp);
	return (error);
}

static int
hammer_vfs_unmount(struct mount *mp, int mntflags)
{
#if 0
	struct hammer_mount *hmp = (void *)mp->mnt_data;
#endif
	int flags;
	int error;

	/*
	 * Clean out the vnodes
	 */
	flags = 0;
	if (mntflags & MNT_FORCE)
		flags |= FORCECLOSE;
	if ((error = vflush(mp, 0, flags)) != 0)
		return (error);

	/*
	 * Clean up the internal mount structure and related entities.  This
	 * may issue I/O.
	 */
	hammer_free_hmp(mp);
	return(0);
}

/*
 * Clean up the internal mount structure and disassociate it from the mount.
 * This may issue I/O.
 */
static void
hammer_free_hmp(struct mount *mp)
{
	struct hammer_mount *hmp = (void *)mp->mnt_data;

#if 0
	/*
	 * Clean up the root vnode
	 */
	if (hmp->rootvp) {
		vrele(hmp->rootvp);
		hmp->rootvp = NULL;
	}
#endif
	hammer_flusher_sync(hmp);
	hammer_flusher_sync(hmp);
	hammer_flusher_destroy(hmp);

	KKASSERT(RB_EMPTY(&hmp->rb_inos_root));

#if 0
	/*
	 * Unload & flush inodes
	 *
	 * XXX illegal to call this from here, it can only be done from
	 * the flusher.
	 */
	RB_SCAN(hammer_ino_rb_tree, &hmp->rb_inos_root, NULL,
		hammer_unload_inode, (void *)MNT_WAIT);

	/*
	 * Unload & flush volumes
	 */
#endif
	/*
	 * Unload buffers and then volumes
	 */
        RB_SCAN(hammer_buf_rb_tree, &hmp->rb_bufs_root, NULL,
		hammer_unload_buffer, NULL);
	RB_SCAN(hammer_vol_rb_tree, &hmp->rb_vols_root, NULL,
		hammer_unload_volume, NULL);

	mp->mnt_data = NULL;
	mp->mnt_flag &= ~MNT_LOCAL;
	hmp->mp = NULL;
	hammer_destroy_objid_cache(hmp);
	kfree(hmp, M_HAMMER);
}

/*
 * Obtain a vnode for the specified inode number.  An exclusively locked
 * vnode is returned.
 */
int
hammer_vfs_vget(struct mount *mp, ino_t ino, struct vnode **vpp)
{
	struct hammer_transaction trans;
	struct hammer_mount *hmp = (void *)mp->mnt_data;
	struct hammer_inode *ip;
	int error;

	hammer_simple_transaction(&trans, hmp);

	/*
	 * Lookup the requested HAMMER inode.  The structure must be
	 * left unlocked while we manipulate the related vnode to avoid
	 * a deadlock.
	 */
	ip = hammer_get_inode(&trans, NULL, ino,
			      hmp->asof, HAMMER_DEF_LOCALIZATION, 
			      0, &error);
	if (ip == NULL) {
		*vpp = NULL;
		return(error);
	}
	error = hammer_get_vnode(ip, vpp);
	hammer_rel_inode(ip, 0);
	hammer_done_transaction(&trans);
	return (error);
}

/*
 * Return the root vnode for the filesystem.
 *
 * HAMMER stores the root vnode in the hammer_mount structure so
 * getting it is easy.
 */
static int
hammer_vfs_root(struct mount *mp, struct vnode **vpp)
{
#if 0
	struct hammer_mount *hmp = (void *)mp->mnt_data;
#endif
	int error;

	error = hammer_vfs_vget(mp, 1, vpp);
	return (error);
}

static int
hammer_vfs_statfs(struct mount *mp, struct statfs *sbp, struct ucred *cred)
{
	struct hammer_mount *hmp = (void *)mp->mnt_data;
	hammer_volume_t volume;
	hammer_volume_ondisk_t ondisk;
	int error;
	int64_t bfree;

	volume = hammer_get_root_volume(hmp, &error);
	if (error)
		return(error);
	ondisk = volume->ondisk;

	/*
	 * Basic stats
	 */
	mp->mnt_stat.f_files = ondisk->vol0_stat_inodes;
	bfree = ondisk->vol0_stat_freebigblocks * HAMMER_LARGEBLOCK_SIZE;
	hammer_rel_volume(volume, 0);

	mp->mnt_stat.f_bfree = bfree / HAMMER_BUFSIZE;
	mp->mnt_stat.f_bavail = mp->mnt_stat.f_bfree;
	if (mp->mnt_stat.f_files < 0)
		mp->mnt_stat.f_files = 0;

	*sbp = mp->mnt_stat;
	return(0);
}

static int
hammer_vfs_statvfs(struct mount *mp, struct statvfs *sbp, struct ucred *cred)
{
	struct hammer_mount *hmp = (void *)mp->mnt_data;
	hammer_volume_t volume;
	hammer_volume_ondisk_t ondisk;
	int error;
	int64_t bfree;

	volume = hammer_get_root_volume(hmp, &error);
	if (error)
		return(error);
	ondisk = volume->ondisk;

	/*
	 * Basic stats
	 */
	mp->mnt_vstat.f_files = ondisk->vol0_stat_inodes;
	bfree = ondisk->vol0_stat_freebigblocks * HAMMER_LARGEBLOCK_SIZE;
	hammer_rel_volume(volume, 0);

	mp->mnt_vstat.f_bfree = bfree / HAMMER_BUFSIZE;
	mp->mnt_vstat.f_bavail = mp->mnt_stat.f_bfree;
	if (mp->mnt_vstat.f_files < 0)
		mp->mnt_vstat.f_files = 0;
	*sbp = mp->mnt_vstat;
	return(0);
}

/*
 * Sync the filesystem.  Currently we have to run it twice, the second
 * one will advance the undo start index to the end index, so if a crash
 * occurs no undos will be run on mount.
 *
 * We do not sync the filesystem if we are called from a panic.  If we did
 * we might end up blowing up a sync that was already in progress.
 */
static int
hammer_vfs_sync(struct mount *mp, int waitfor)
{
	struct hammer_mount *hmp = (void *)mp->mnt_data;
	int error;

	if (panicstr == NULL) {
		error = hammer_sync_hmp(hmp, waitfor);
		if (error == 0)
			error = hammer_sync_hmp(hmp, waitfor);
	} else {
		error = EIO;
	}
	return (error);
}

/*
 * Convert a vnode to a file handle.
 */
static int
hammer_vfs_vptofh(struct vnode *vp, struct fid *fhp)
{
	hammer_inode_t ip;

	KKASSERT(MAXFIDSZ >= 16);
	ip = VTOI(vp);
	fhp->fid_len = offsetof(struct fid, fid_data[16]);
	fhp->fid_reserved = 0;
	bcopy(&ip->obj_id, fhp->fid_data + 0, sizeof(ip->obj_id));
	bcopy(&ip->obj_asof, fhp->fid_data + 8, sizeof(ip->obj_asof));
	return(0);
}


/*
 * Convert a file handle back to a vnode.
 */
static int
hammer_vfs_fhtovp(struct mount *mp, struct fid *fhp, struct vnode **vpp)
{
	struct hammer_transaction trans;
	struct hammer_inode *ip;
	struct hammer_inode_info info;
	int error;

	bcopy(fhp->fid_data + 0, &info.obj_id, sizeof(info.obj_id));
	bcopy(fhp->fid_data + 8, &info.obj_asof, sizeof(info.obj_asof));

	hammer_simple_transaction(&trans, (void *)mp->mnt_data);

	/*
	 * Get/allocate the hammer_inode structure.  The structure must be
	 * unlocked while we manipulate the related vnode to avoid a
	 * deadlock.
	 */
	ip = hammer_get_inode(&trans, NULL, info.obj_id,
			      info.obj_asof, HAMMER_DEF_LOCALIZATION,
			      0, &error);
	if (ip == NULL) {
		*vpp = NULL;
		return(error);
	}
	error = hammer_get_vnode(ip, vpp);
	hammer_rel_inode(ip, 0);
	hammer_done_transaction(&trans);
	return (error);
}

static int
hammer_vfs_checkexp(struct mount *mp, struct sockaddr *nam,
		    int *exflagsp, struct ucred **credanonp)
{
	hammer_mount_t hmp = (void *)mp->mnt_data;
	struct netcred *np;
	int error;

	np = vfs_export_lookup(mp, &hmp->export, nam);
	if (np) {
		*exflagsp = np->netc_exflags;
		*credanonp = &np->netc_anon;
		error = 0;
	} else {
		error = EACCES;
	}
	return (error);

}

int
hammer_vfs_export(struct mount *mp, int op, const struct export_args *export)
{
	hammer_mount_t hmp = (void *)mp->mnt_data;
	int error;

	switch(op) {
	case MOUNTCTL_SET_EXPORT:
		error = vfs_export(mp, &hmp->export, export);
		break;
	default:
		error = EOPNOTSUPP;
		break;
	}
	return(error);
}

