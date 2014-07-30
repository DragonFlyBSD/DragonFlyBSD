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

/*
 * NOTE!  Global statistics may not be MPSAFE so HAMMER never uses them
 *	  in conditionals.
 */
int hammer_supported_version = HAMMER_VOL_VERSION_DEFAULT;
int hammer_debug_io;
int hammer_debug_general;
int hammer_debug_debug = 1;		/* medium-error panics */ 
int hammer_debug_inode;
int hammer_debug_locks;
int hammer_debug_btree;
int hammer_debug_tid;
int hammer_debug_recover;		/* -1 will disable, +1 will force */
int hammer_debug_recover_faults;
int hammer_debug_critical;		/* non-zero enter debugger on error */
int hammer_cluster_enable = 1;		/* enable read clustering by default */
int hammer_live_dedup = 0;
int hammer_tdmux_ticks;
int hammer_count_fsyncs;
int hammer_count_inodes;
int hammer_count_iqueued;
int hammer_count_reclaims;
int hammer_count_records;
int hammer_count_record_datas;
int hammer_count_volumes;
int hammer_count_buffers;
int hammer_count_nodes;
int64_t hammer_count_extra_space_used;
int64_t hammer_stats_btree_lookups;
int64_t hammer_stats_btree_searches;
int64_t hammer_stats_btree_inserts;
int64_t hammer_stats_btree_deletes;
int64_t hammer_stats_btree_elements;
int64_t hammer_stats_btree_splits;
int64_t hammer_stats_btree_iterations;
int64_t hammer_stats_btree_root_iterations;
int64_t hammer_stats_record_iterations;

int64_t hammer_stats_file_read;
int64_t hammer_stats_file_write;
int64_t hammer_stats_file_iopsr;
int64_t hammer_stats_file_iopsw;
int64_t hammer_stats_disk_read;
int64_t hammer_stats_disk_write;
int64_t hammer_stats_inode_flushes;
int64_t hammer_stats_commits;
int64_t hammer_stats_undo;
int64_t hammer_stats_redo;

long hammer_count_dirtybufspace;	/* global */
int hammer_count_refedbufs;		/* global */
int hammer_count_reservations;
long hammer_count_io_running_read;
long hammer_count_io_running_write;
int hammer_count_io_locked;
long hammer_limit_dirtybufspace;	/* per-mount */
int hammer_limit_recs;			/* as a whole XXX */
int hammer_limit_inode_recs = 2048;	/* per inode */
int hammer_limit_reclaims;
int hammer_live_dedup_cache_size = DEDUP_CACHE_SIZE;
int hammer_limit_redo = 4096 * 1024;	/* per inode */
int hammer_autoflush = 500;		/* auto flush (typ on reclaim) */
int hammer_bio_count;
int hammer_verify_zone;
int hammer_verify_data = 1;
int hammer_write_mode;
int hammer_double_buffer;
int hammer_btree_full_undo = 1;
int hammer_yield_check = 16;
int hammer_fsync_mode = 3;
int64_t hammer_contention_count;
int64_t hammer_zone_limit;

/*
 * Live dedup debug counters (sysctls are writable so that counters
 * can be reset from userspace).
 */
int64_t hammer_live_dedup_vnode_bcmps = 0;
int64_t hammer_live_dedup_device_bcmps = 0;
int64_t hammer_live_dedup_findblk_failures = 0;
int64_t hammer_live_dedup_bmap_saves = 0;


SYSCTL_NODE(_vfs, OID_AUTO, hammer, CTLFLAG_RW, 0, "HAMMER filesystem");

SYSCTL_INT(_vfs_hammer, OID_AUTO, supported_version, CTLFLAG_RD,
	   &hammer_supported_version, 0, "");
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
SYSCTL_INT(_vfs_hammer, OID_AUTO, debug_critical, CTLFLAG_RW,
	   &hammer_debug_critical, 0, "");
SYSCTL_INT(_vfs_hammer, OID_AUTO, cluster_enable, CTLFLAG_RW,
	   &hammer_cluster_enable, 0, "");
/*
 * 0 - live dedup is disabled
 * 1 - dedup cache is populated on reads only
 * 2 - dedup cache is populated on both reads and writes
 *
 * LIVE_DEDUP IS DISABLED PERMANENTLY!  This feature appears to cause
 * blockmap corruption over time so we've turned it off permanently.
 */
SYSCTL_INT(_vfs_hammer, OID_AUTO, live_dedup, CTLFLAG_RD,
	   &hammer_live_dedup, 0, "Enable live dedup (experimental)");
SYSCTL_INT(_vfs_hammer, OID_AUTO, tdmux_ticks, CTLFLAG_RW,
	   &hammer_tdmux_ticks, 0, "Hammer tdmux ticks");

SYSCTL_LONG(_vfs_hammer, OID_AUTO, limit_dirtybufspace, CTLFLAG_RW,
	   &hammer_limit_dirtybufspace, 0, "");
SYSCTL_INT(_vfs_hammer, OID_AUTO, limit_recs, CTLFLAG_RW,
	   &hammer_limit_recs, 0, "");
SYSCTL_INT(_vfs_hammer, OID_AUTO, limit_inode_recs, CTLFLAG_RW,
	   &hammer_limit_inode_recs, 0, "");
SYSCTL_INT(_vfs_hammer, OID_AUTO, limit_reclaims, CTLFLAG_RW,
	   &hammer_limit_reclaims, 0, "");
SYSCTL_INT(_vfs_hammer, OID_AUTO, live_dedup_cache_size, CTLFLAG_RW,
	   &hammer_live_dedup_cache_size, 0,
	   "Number of cache entries");
SYSCTL_INT(_vfs_hammer, OID_AUTO, limit_redo, CTLFLAG_RW,
	   &hammer_limit_redo, 0, "");

SYSCTL_INT(_vfs_hammer, OID_AUTO, count_fsyncs, CTLFLAG_RD,
	   &hammer_count_fsyncs, 0, "");
SYSCTL_INT(_vfs_hammer, OID_AUTO, count_inodes, CTLFLAG_RD,
	   &hammer_count_inodes, 0, "");
SYSCTL_INT(_vfs_hammer, OID_AUTO, count_iqueued, CTLFLAG_RD,
	   &hammer_count_iqueued, 0, "");
SYSCTL_INT(_vfs_hammer, OID_AUTO, count_reclaims, CTLFLAG_RD,
	   &hammer_count_reclaims, 0, "");
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
SYSCTL_QUAD(_vfs_hammer, OID_AUTO, count_extra_space_used, CTLFLAG_RD,
	   &hammer_count_extra_space_used, 0, "");

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
SYSCTL_QUAD(_vfs_hammer, OID_AUTO, stats_btree_root_iterations, CTLFLAG_RD,
	   &hammer_stats_btree_root_iterations, 0, "");
SYSCTL_QUAD(_vfs_hammer, OID_AUTO, stats_record_iterations, CTLFLAG_RD,
	   &hammer_stats_record_iterations, 0, "");

SYSCTL_QUAD(_vfs_hammer, OID_AUTO, stats_file_read, CTLFLAG_RD,
	   &hammer_stats_file_read, 0, "");
SYSCTL_QUAD(_vfs_hammer, OID_AUTO, stats_file_write, CTLFLAG_RD,
	   &hammer_stats_file_write, 0, "");
SYSCTL_QUAD(_vfs_hammer, OID_AUTO, stats_file_iopsr, CTLFLAG_RD,
	   &hammer_stats_file_iopsr, 0, "");
SYSCTL_QUAD(_vfs_hammer, OID_AUTO, stats_file_iopsw, CTLFLAG_RD,
	   &hammer_stats_file_iopsw, 0, "");
SYSCTL_QUAD(_vfs_hammer, OID_AUTO, stats_disk_read, CTLFLAG_RD,
	   &hammer_stats_disk_read, 0, "");
SYSCTL_QUAD(_vfs_hammer, OID_AUTO, stats_disk_write, CTLFLAG_RD,
	   &hammer_stats_disk_write, 0, "");
SYSCTL_QUAD(_vfs_hammer, OID_AUTO, stats_inode_flushes, CTLFLAG_RD,
	   &hammer_stats_inode_flushes, 0, "");
SYSCTL_QUAD(_vfs_hammer, OID_AUTO, stats_commits, CTLFLAG_RD,
	   &hammer_stats_commits, 0, "");
SYSCTL_QUAD(_vfs_hammer, OID_AUTO, stats_undo, CTLFLAG_RD,
	   &hammer_stats_undo, 0, "");
SYSCTL_QUAD(_vfs_hammer, OID_AUTO, stats_redo, CTLFLAG_RD,
	   &hammer_stats_redo, 0, "");

SYSCTL_QUAD(_vfs_hammer, OID_AUTO, live_dedup_vnode_bcmps, CTLFLAG_RW,
	    &hammer_live_dedup_vnode_bcmps, 0,
	    "successful vnode buffer comparisons");
SYSCTL_QUAD(_vfs_hammer, OID_AUTO, live_dedup_device_bcmps, CTLFLAG_RW,
	    &hammer_live_dedup_device_bcmps, 0,
	    "successful device buffer comparisons");
SYSCTL_QUAD(_vfs_hammer, OID_AUTO, live_dedup_findblk_failures, CTLFLAG_RW,
	    &hammer_live_dedup_findblk_failures, 0,
	    "block lookup failures for comparison");
SYSCTL_QUAD(_vfs_hammer, OID_AUTO, live_dedup_bmap_saves, CTLFLAG_RW,
	    &hammer_live_dedup_bmap_saves, 0,
	    "useful physical block lookups");

SYSCTL_LONG(_vfs_hammer, OID_AUTO, count_dirtybufspace, CTLFLAG_RD,
	   &hammer_count_dirtybufspace, 0, "");
SYSCTL_INT(_vfs_hammer, OID_AUTO, count_refedbufs, CTLFLAG_RD,
	   &hammer_count_refedbufs, 0, "");
SYSCTL_INT(_vfs_hammer, OID_AUTO, count_reservations, CTLFLAG_RD,
	   &hammer_count_reservations, 0, "");
SYSCTL_LONG(_vfs_hammer, OID_AUTO, count_io_running_read, CTLFLAG_RD,
	   &hammer_count_io_running_read, 0, "");
SYSCTL_INT(_vfs_hammer, OID_AUTO, count_io_locked, CTLFLAG_RD,
	   &hammer_count_io_locked, 0, "");
SYSCTL_LONG(_vfs_hammer, OID_AUTO, count_io_running_write, CTLFLAG_RD,
	   &hammer_count_io_running_write, 0, "");
SYSCTL_QUAD(_vfs_hammer, OID_AUTO, zone_limit, CTLFLAG_RW,
	   &hammer_zone_limit, 0, "");
SYSCTL_QUAD(_vfs_hammer, OID_AUTO, contention_count, CTLFLAG_RW,
	   &hammer_contention_count, 0, "");
SYSCTL_INT(_vfs_hammer, OID_AUTO, autoflush, CTLFLAG_RW,
	   &hammer_autoflush, 0, "");
SYSCTL_INT(_vfs_hammer, OID_AUTO, verify_zone, CTLFLAG_RW,
	   &hammer_verify_zone, 0, "");
SYSCTL_INT(_vfs_hammer, OID_AUTO, verify_data, CTLFLAG_RW,
	   &hammer_verify_data, 0, "");
SYSCTL_INT(_vfs_hammer, OID_AUTO, write_mode, CTLFLAG_RW,
	   &hammer_write_mode, 0, "");
SYSCTL_INT(_vfs_hammer, OID_AUTO, double_buffer, CTLFLAG_RW,
	   &hammer_double_buffer, 0, "");
SYSCTL_INT(_vfs_hammer, OID_AUTO, btree_full_undo, CTLFLAG_RW,
	   &hammer_btree_full_undo, 0, "");
SYSCTL_INT(_vfs_hammer, OID_AUTO, yield_check, CTLFLAG_RW,
	   &hammer_yield_check, 0, "");
SYSCTL_INT(_vfs_hammer, OID_AUTO, fsync_mode, CTLFLAG_RW,
	   &hammer_fsync_mode, 0, "");

/* KTR_INFO_MASTER(hammer); */

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
static int	hammer_vfs_vget(struct mount *mp, struct vnode *dvp,
				ino_t ino, struct vnode **vpp);
static int	hammer_vfs_init(struct vfsconf *conf);
static int	hammer_vfs_fhtovp(struct mount *mp, struct vnode *rootvp,
				struct fid *fhp, struct vnode **vpp);
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

MALLOC_DEFINE(M_HAMMER, "HAMMER-mount", "");

VFS_SET(hammer_vfsops, hammer, 0);
MODULE_VERSION(hammer, 1);

static int
hammer_vfs_init(struct vfsconf *conf)
{
	long n;

	/*
	 * Wait up to this long for an exclusive deadlock to clear
	 * before acquiring a new shared lock on the ip.  The deadlock
	 * may have occured on a b-tree node related to the ip.
	 */
	if (hammer_tdmux_ticks == 0)
		hammer_tdmux_ticks = hz / 5;

	/*
	 * Autosize, but be careful because a hammer filesystem's
	 * reserve is partially calculated based on dirtybufspace,
	 * so we simply cannot allow it to get too large.
	 */
	if (hammer_limit_recs == 0) {
		n = nbuf * 25;
		if (n > kmalloc_limit(M_HAMMER) / 512)
			n = kmalloc_limit(M_HAMMER) / 512;
		if (n > 2 * 1024 * 1024)
			n = 2 * 1024 * 1024;
		hammer_limit_recs = (int)n;
	}
	if (hammer_limit_dirtybufspace == 0) {
		hammer_limit_dirtybufspace = hidirtybufspace / 2;
		if (hammer_limit_dirtybufspace < 1L * 1024 * 1024)
			hammer_limit_dirtybufspace = 1024L * 1024;
		if (hammer_limit_dirtybufspace > 1024L * 1024 * 1024)
			hammer_limit_dirtybufspace = 1024L * 1024 * 1024;
	}

	/*
	 * The hammer_inode structure detaches from the vnode on reclaim.
	 * This limits the number of inodes in this state to prevent a
	 * memory pool blowout.
	 */
	if (hammer_limit_reclaims == 0)
		hammer_limit_reclaims = desiredvnodes / 10;

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
	struct vnode *devvp = NULL;
	const char *upath;	/* volume name in userspace */
	char *path;		/* volume name in system space */
	int error;
	int i;
	int master_id;
	char *next_volume_ptr = NULL;

	/*
	 * Accept hammer_mount_info.  mntpt is NULL for root mounts at boot.
	 */
	if (mntpt == NULL) {
		bzero(&info, sizeof(info));
		info.asof = 0;
		info.hflags = 0;
		info.nvolumes = 1;

		next_volume_ptr = mp->mnt_stat.f_mntfromname;

		/* Count number of volumes separated by ':' */
		for (char *p = next_volume_ptr; *p != '\0'; ++p) {
			if (*p == ':') {
				++info.nvolumes;
			}
		}

		mp->mnt_flag &= ~MNT_RDONLY; /* mount R/W */
	} else {
		if ((error = copyin(data, &info, sizeof(info))) != 0)
			return (error);
	}

	/*
	 * updating or new mount
	 */
	if (mp->mnt_flag & MNT_UPDATE) {
		hmp = (void *)mp->mnt_data;
		KKASSERT(hmp != NULL);
	} else {
		if (info.nvolumes <= 0 || info.nvolumes > HAMMER_MAX_VOLUMES)
			return (EINVAL);
		hmp = NULL;
	}

	/*
	 * master-id validation.  The master id may not be changed by a
	 * mount update.
	 */
	if (info.hflags & HMNT_MASTERID) {
		if (hmp && hmp->master_id != info.master_id) {
			kprintf("hammer: cannot change master id "
				"with mount update\n");
			return(EINVAL);
		}
		master_id = info.master_id;
		if (master_id < -1 || master_id >= HAMMER_MAX_MASTERS)
			return (EINVAL);
	} else {
		if (hmp)
			master_id = hmp->master_id;
		else
			master_id = 0;
	}

	/*
	 * Internal mount data structure
	 */
	if (hmp == NULL) {
		hmp = kmalloc(sizeof(*hmp), M_HAMMER, M_WAITOK | M_ZERO);
		mp->mnt_data = (qaddr_t)hmp;
		hmp->mp = mp;
		/*TAILQ_INIT(&hmp->recycle_list);*/

		/*
		 * Make sure kmalloc type limits are set appropriately.
		 *
		 * Our inode kmalloc group is sized based on maxvnodes
		 * (controlled by the system, not us).
		 */
		kmalloc_create(&hmp->m_misc, "HAMMER-others");
		kmalloc_create(&hmp->m_inodes, "HAMMER-inodes");

		kmalloc_raise_limit(hmp->m_inodes, 0);	/* unlimited */

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

		hmp->krate.freq = 1;	/* maximum reporting rate (hz) */
		hmp->krate.count = -16;	/* initial burst */

		hmp->sync_lock.refs = 1;
		hmp->free_lock.refs = 1;
		hmp->undo_lock.refs = 1;
		hmp->blkmap_lock.refs = 1;
		hmp->snapshot_lock.refs = 1;
		hmp->volume_lock.refs = 1;

		TAILQ_INIT(&hmp->delay_list);
		TAILQ_INIT(&hmp->flush_group_list);
		TAILQ_INIT(&hmp->objid_cache_list);
		TAILQ_INIT(&hmp->undo_lru_list);
		TAILQ_INIT(&hmp->reclaim_list);

		RB_INIT(&hmp->rb_dedup_crc_root);
		RB_INIT(&hmp->rb_dedup_off_root);	
		TAILQ_INIT(&hmp->dedup_lru_list);
	}
	hmp->hflags &= ~HMNT_USERFLAGS;
	hmp->hflags |= info.hflags & HMNT_USERFLAGS;

	hmp->master_id = master_id;

	if (info.asof) {
		mp->mnt_flag |= MNT_RDONLY;
		hmp->asof = info.asof;
	} else {
		hmp->asof = HAMMER_MAX_TID;
	}

	hmp->volume_to_remove = -1;

	/*
	 * Re-open read-write if originally read-only, or vise-versa.
	 *
	 * When going from read-only to read-write execute the stage2
	 * recovery if it has not already been run.
	 */
	if (mp->mnt_flag & MNT_UPDATE) {
		lwkt_gettoken(&hmp->fs_token);
		error = 0;
		if (hmp->ronly && (mp->mnt_kern_flag & MNTK_WANTRDWR)) {
			kprintf("HAMMER read-only -> read-write\n");
			hmp->ronly = 0;
			RB_SCAN(hammer_vol_rb_tree, &hmp->rb_vols_root, NULL,
				hammer_adjust_volume_mode, NULL);
			rootvol = hammer_get_root_volume(hmp, &error);
			if (rootvol) {
				hammer_recover_flush_buffers(hmp, rootvol, 1);
				error = hammer_recover_stage2(hmp, rootvol);
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
		lwkt_reltoken(&hmp->fs_token);
		return(error);
	}

	RB_INIT(&hmp->rb_vols_root);
	RB_INIT(&hmp->rb_inos_root);
	RB_INIT(&hmp->rb_redo_root);
	RB_INIT(&hmp->rb_nods_root);
	RB_INIT(&hmp->rb_undo_root);
	RB_INIT(&hmp->rb_resv_root);
	RB_INIT(&hmp->rb_bufs_root);
	RB_INIT(&hmp->rb_pfsm_root);

	hmp->ronly = ((mp->mnt_flag & MNT_RDONLY) != 0);

	RB_INIT(&hmp->volu_root);
	RB_INIT(&hmp->undo_root);
	RB_INIT(&hmp->data_root);
	RB_INIT(&hmp->meta_root);
	RB_INIT(&hmp->lose_root);
	TAILQ_INIT(&hmp->iorun_list);

	lwkt_token_init(&hmp->fs_token, "hammerfs");
	lwkt_token_init(&hmp->io_token, "hammerio");

	lwkt_gettoken(&hmp->fs_token);

	/*
	 * Load volumes
	 */
	path = objcache_get(namei_oc, M_WAITOK);
	hmp->nvolumes = -1;
	for (i = 0; i < info.nvolumes; ++i) {
		if (mntpt == NULL) {
			/*
			 * Root mount.
			 */
			KKASSERT(next_volume_ptr != NULL);
			strcpy(path, "");
			if (*next_volume_ptr != '/') {
				/* relative path */
				strcpy(path, "/dev/");
			}
			int k;
			for (k = strlen(path); k < MAXPATHLEN-1; ++k) {
				if (*next_volume_ptr == '\0') {
					break;
				} else if (*next_volume_ptr == ':') {
					++next_volume_ptr;
					break;
				} else {
					path[k] = *next_volume_ptr;
					++next_volume_ptr;
				}
			}
			path[k] = '\0';

			error = 0;
			cdev_t dev = kgetdiskbyname(path);
			error = bdevvp(dev, &devvp);
			if (error) {
				kprintf("hammer_mountroot: can't find devvp\n");
			}
		} else {
			error = copyin(&info.volumes[i], &upath,
				       sizeof(char *));
			if (error == 0)
				error = copyinstr(upath, path,
						  MAXPATHLEN, NULL);
		}
		if (error == 0)
			error = hammer_install_volume(hmp, path, devvp);
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

	/*
	 * Check that all required volumes are available
	 */
	if (error == 0 && hammer_mountcheck_volumes(hmp)) {
		kprintf("hammer_mount: Missing volumes, cannot mount!\n");
		error = EINVAL;
	}

	if (error) {
		/* called with fs_token held */
		hammer_free_hmp(mp);
		return (error);
	}

	/*
	 * No errors, setup enough of the mount point so we can lookup the
	 * root vnode.
	 */
	mp->mnt_iosize_max = MAXPHYS;
	mp->mnt_kern_flag |= MNTK_FSMID;
	mp->mnt_kern_flag |= MNTK_THR_SYNC;	/* new vsyncscan semantics */

	/*
	 * MPSAFE code.  Note that VOPs and VFSops which are not MPSAFE
	 * will acquire a per-mount token prior to entry and release it
	 * on return, so even if we do not specify it we no longer get
	 * the BGL regardlless of how we are flagged.
	 */
	mp->mnt_kern_flag |= MNTK_ALL_MPSAFE;
	/*MNTK_RD_MPSAFE | MNTK_GA_MPSAFE | MNTK_IN_MPSAFE;*/

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

	/*
	 * Check filesystem version
	 */
	hmp->version = rootvol->ondisk->vol_version;
	if (hmp->version < HAMMER_VOL_VERSION_MIN ||
	    hmp->version > HAMMER_VOL_VERSION_MAX) {
		kprintf("HAMMER: mount unsupported fs version %d\n",
			hmp->version);
		error = ERANGE;
		goto done;
	}

	/*
	 * The undo_rec_limit limits the size of flush groups to avoid
	 * blowing out the UNDO FIFO.  This calculation is typically in
	 * the tens of thousands and is designed primarily when small
	 * HAMMER filesystems are created.
	 */
	hmp->undo_rec_limit = hammer_undo_max(hmp) / 8192 + 100;
	if (hammer_debug_general & 0x0001)
		kprintf("HAMMER: undo_rec_limit %d\n", hmp->undo_rec_limit);

	/*
	 * NOTE: Recover stage1 not only handles meta-data recovery, it
	 * 	 also sets hmp->undo_seqno for HAMMER VERSION 4+ filesystems.
	 */
	error = hammer_recover_stage1(hmp, rootvol);
	if (error) {
		kprintf("Failed to recover HAMMER filesystem on mount\n");
		goto done;
	}

	/*
	 * Finish setup now that we have a good root volume.
	 *
	 * The top 16 bits of fsid.val[1] is a pfs id.
	 */
	ksnprintf(mp->mnt_stat.f_mntfromname,
		  sizeof(mp->mnt_stat.f_mntfromname), "%s",
		  rootvol->ondisk->vol_name);
	mp->mnt_stat.f_fsid.val[0] =
		crc32((char *)&rootvol->ondisk->vol_fsid + 0, 8);
	mp->mnt_stat.f_fsid.val[1] =
		crc32((char *)&rootvol->ondisk->vol_fsid + 8, 8);
	mp->mnt_stat.f_fsid.val[1] &= 0x0000FFFF;

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
	hmp->flush_tid1 = hmp->next_tid;
	hmp->flush_tid2 = hmp->next_tid;
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
	error = hammer_vfs_vget(mp, NULL, 1, &rootvp);
	if (error)
		goto done;
	vput(rootvp);
	/*vn_unlock(hmp->rootvp);*/
	if (hmp->ronly == 0)
		error = hammer_recover_stage2(hmp, rootvol);

	/*
	 * If the stage2 recovery fails be sure to clean out all cached
	 * vnodes before throwing away the mount structure or bad things
	 * will happen.
	 */
	if (error)
		vflush(mp, 0, 0);

done:
	if ((mp->mnt_flag & MNT_UPDATE) == 0) {
		/* New mount */

		/* Populate info for mount point (NULL pad)*/
		bzero(mp->mnt_stat.f_mntonname, MNAMELEN);
		size_t size;
		if (mntpt) {
			copyinstr(mntpt, mp->mnt_stat.f_mntonname,
							MNAMELEN -1, &size);
		} else { /* Root mount */
			mp->mnt_stat.f_mntonname[0] = '/';
		}
	}
	(void)VFS_STATFS(mp, &mp->mnt_stat, cred);
	hammer_rel_volume(rootvol, 0);
failed:
	/*
	 * Cleanup and return.
	 */
	if (error) {
		/* called with fs_token held */
		hammer_free_hmp(mp);
	} else {
		lwkt_reltoken(&hmp->fs_token);
	}
	return (error);
}

static int
hammer_vfs_unmount(struct mount *mp, int mntflags)
{
	hammer_mount_t hmp = (void *)mp->mnt_data;
	int flags;
	int error;

	/*
	 * Clean out the vnodes
	 */
	lwkt_gettoken(&hmp->fs_token);
	flags = 0;
	if (mntflags & MNT_FORCE)
		flags |= FORCECLOSE;
	error = vflush(mp, 0, flags);

	/*
	 * Clean up the internal mount structure and related entities.  This
	 * may issue I/O.
	 */
	if (error == 0) {
		/* called with fs_token held */
		hammer_free_hmp(mp);
	} else {
		lwkt_reltoken(&hmp->fs_token);
	}
	return(error);
}

/*
 * Clean up the internal mount structure and disassociate it from the mount.
 * This may issue I/O.
 *
 * Called with fs_token held.
 */
static void
hammer_free_hmp(struct mount *mp)
{
	hammer_mount_t hmp = (void *)mp->mnt_data;
	hammer_flush_group_t flg;
	int count;
	int dummy;

	/*
	 * Flush anything dirty.  This won't even run if the
	 * filesystem errored-out.
	 */
	count = 0;
	while (hammer_flusher_haswork(hmp)) {
		hammer_flusher_sync(hmp);
		++count;
		if (count >= 5) {
			if (count == 5)
				kprintf("HAMMER: umount flushing.");
			else
				kprintf(".");
			tsleep(&dummy, 0, "hmrufl", hz);
		}
		if (count == 30) {
			kprintf("giving up\n");
			break;
		}
	}
	if (count >= 5 && count < 30)
		kprintf("\n");

	/*
	 * If the mount had a critical error we have to destroy any
	 * remaining inodes before we can finish cleaning up the flusher.
	 */
	if (hmp->flags & HAMMER_MOUNT_CRITICAL_ERROR) {
		RB_SCAN(hammer_ino_rb_tree, &hmp->rb_inos_root, NULL,
			hammer_destroy_inode_callback, NULL);
	}

	/*
	 * There shouldn't be any inodes left now and any left over
	 * flush groups should now be empty.
	 */
	KKASSERT(RB_EMPTY(&hmp->rb_inos_root));
	while ((flg = TAILQ_FIRST(&hmp->flush_group_list)) != NULL) {
		TAILQ_REMOVE(&hmp->flush_group_list, flg, flush_entry);
		KKASSERT(RB_EMPTY(&flg->flush_tree));
		if (flg->refs) {
			kprintf("HAMMER: Warning, flush_group %p was "
				"not empty on umount!\n", flg);
		}
		kfree(flg, hmp->m_misc);
	}

	/*
	 * We can finally destroy the flusher
	 */
	hammer_flusher_destroy(hmp);

	/*
	 * We may have held recovered buffers due to a read-only mount.
	 * These must be discarded.
	 */
	if (hmp->ronly)
		hammer_recover_flush_buffers(hmp, NULL, -1);

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
	hammer_destroy_dedup_cache(hmp);
	if (hmp->dedup_free_cache != NULL) {
		kfree(hmp->dedup_free_cache, hmp->m_misc);
		hmp->dedup_free_cache = NULL;
	}
	kmalloc_destroy(&hmp->m_misc);
	kmalloc_destroy(&hmp->m_inodes);
	lwkt_reltoken(&hmp->fs_token);
	kfree(hmp, M_HAMMER);
}

/*
 * Report critical errors.  ip may be NULL.
 */
void
hammer_critical_error(hammer_mount_t hmp, hammer_inode_t ip,
		      int error, const char *msg)
{
	hmp->flags |= HAMMER_MOUNT_CRITICAL_ERROR;

	krateprintf(&hmp->krate,
		    "HAMMER(%s): Critical error inode=%jd error=%d %s\n",
		    hmp->mp->mnt_stat.f_mntfromname,
		    (intmax_t)(ip ? ip->obj_id : -1),
		    error, msg);

	if (hmp->ronly == 0) {
		hmp->ronly = 2;		/* special errored read-only mode */
		hmp->mp->mnt_flag |= MNT_RDONLY;
		RB_SCAN(hammer_vol_rb_tree, &hmp->rb_vols_root, NULL,
			hammer_adjust_volume_mode, NULL);
		kprintf("HAMMER(%s): Forcing read-only mode\n",
			hmp->mp->mnt_stat.f_mntfromname);
	}
	hmp->error = error;
	if (hammer_debug_critical)
		Debugger("Entering debugger");
}


/*
 * Obtain a vnode for the specified inode number.  An exclusively locked
 * vnode is returned.
 */
int
hammer_vfs_vget(struct mount *mp, struct vnode *dvp,
		ino_t ino, struct vnode **vpp)
{
	struct hammer_transaction trans;
	struct hammer_mount *hmp = (void *)mp->mnt_data;
	struct hammer_inode *ip;
	int error;
	u_int32_t localization;

	lwkt_gettoken(&hmp->fs_token);
	hammer_simple_transaction(&trans, hmp);

	/*
	 * If a directory vnode is supplied (mainly NFS) then we can acquire
	 * the PFS domain from it.  Otherwise we would only be able to vget
	 * inodes in the root PFS.
	 */
	if (dvp) {
		localization = HAMMER_DEF_LOCALIZATION +
				VTOI(dvp)->obj_localization;
	} else {
		localization = HAMMER_DEF_LOCALIZATION;
	}

	/*
	 * Lookup the requested HAMMER inode.  The structure must be
	 * left unlocked while we manipulate the related vnode to avoid
	 * a deadlock.
	 */
	ip = hammer_get_inode(&trans, NULL, ino,
			      hmp->asof, localization,
			      0, &error);
	if (ip == NULL) {
		*vpp = NULL;
	} else {
		error = hammer_get_vnode(ip, vpp);
		hammer_rel_inode(ip, 0);
	}
	hammer_done_transaction(&trans);
	lwkt_reltoken(&hmp->fs_token);
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
	int error;

	error = hammer_vfs_vget(mp, NULL, 1, vpp);
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
	int64_t breserved;

	lwkt_gettoken(&hmp->fs_token);
	volume = hammer_get_root_volume(hmp, &error);
	if (error) {
		lwkt_reltoken(&hmp->fs_token);
		return(error);
	}
	ondisk = volume->ondisk;

	/*
	 * Basic stats
	 */
	_hammer_checkspace(hmp, HAMMER_CHKSPC_WRITE, &breserved);
	mp->mnt_stat.f_files = ondisk->vol0_stat_inodes;
	bfree = ondisk->vol0_stat_freebigblocks * HAMMER_LARGEBLOCK_SIZE;
	hammer_rel_volume(volume, 0);

	mp->mnt_stat.f_bfree = (bfree - breserved) / HAMMER_BUFSIZE;
	mp->mnt_stat.f_bavail = mp->mnt_stat.f_bfree;
	if (mp->mnt_stat.f_files < 0)
		mp->mnt_stat.f_files = 0;

	*sbp = mp->mnt_stat;
	lwkt_reltoken(&hmp->fs_token);
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
	int64_t breserved;

	lwkt_gettoken(&hmp->fs_token);
	volume = hammer_get_root_volume(hmp, &error);
	if (error) {
		lwkt_reltoken(&hmp->fs_token);
		return(error);
	}
	ondisk = volume->ondisk;

	/*
	 * Basic stats
	 */
	_hammer_checkspace(hmp, HAMMER_CHKSPC_WRITE, &breserved);
	mp->mnt_vstat.f_files = ondisk->vol0_stat_inodes;
	bfree = ondisk->vol0_stat_freebigblocks * HAMMER_LARGEBLOCK_SIZE;
	hammer_rel_volume(volume, 0);

	mp->mnt_vstat.f_bfree = (bfree - breserved) / HAMMER_BUFSIZE;
	mp->mnt_vstat.f_bavail = mp->mnt_vstat.f_bfree;
	if (mp->mnt_vstat.f_files < 0)
		mp->mnt_vstat.f_files = 0;
	*sbp = mp->mnt_vstat;
	lwkt_reltoken(&hmp->fs_token);
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

	lwkt_gettoken(&hmp->fs_token);
	if (panicstr == NULL) {
		error = hammer_sync_hmp(hmp, waitfor);
	} else {
		error = EIO;
	}
	lwkt_reltoken(&hmp->fs_token);
	return (error);
}

/*
 * Convert a vnode to a file handle.
 *
 * Accesses read-only fields on already-referenced structures so
 * no token is needed.
 */
static int
hammer_vfs_vptofh(struct vnode *vp, struct fid *fhp)
{
	hammer_inode_t ip;

	KKASSERT(MAXFIDSZ >= 16);
	ip = VTOI(vp);
	fhp->fid_len = offsetof(struct fid, fid_data[16]);
	fhp->fid_ext = ip->obj_localization >> 16;
	bcopy(&ip->obj_id, fhp->fid_data + 0, sizeof(ip->obj_id));
	bcopy(&ip->obj_asof, fhp->fid_data + 8, sizeof(ip->obj_asof));
	return(0);
}


/*
 * Convert a file handle back to a vnode.
 *
 * Use rootvp to enforce PFS isolation when a PFS is exported via a
 * null mount.
 */
static int
hammer_vfs_fhtovp(struct mount *mp, struct vnode *rootvp,
		  struct fid *fhp, struct vnode **vpp)
{
	hammer_mount_t hmp = (void *)mp->mnt_data;
	struct hammer_transaction trans;
	struct hammer_inode *ip;
	struct hammer_inode_info info;
	int error;
	u_int32_t localization;

	bcopy(fhp->fid_data + 0, &info.obj_id, sizeof(info.obj_id));
	bcopy(fhp->fid_data + 8, &info.obj_asof, sizeof(info.obj_asof));
	if (rootvp)
		localization = VTOI(rootvp)->obj_localization;
	else
		localization = (u_int32_t)fhp->fid_ext << 16;

	lwkt_gettoken(&hmp->fs_token);
	hammer_simple_transaction(&trans, hmp);

	/*
	 * Get/allocate the hammer_inode structure.  The structure must be
	 * unlocked while we manipulate the related vnode to avoid a
	 * deadlock.
	 */
	ip = hammer_get_inode(&trans, NULL, info.obj_id,
			      info.obj_asof, localization, 0, &error);
	if (ip) {
		error = hammer_get_vnode(ip, vpp);
		hammer_rel_inode(ip, 0);
	} else {
		*vpp = NULL;
	}
	hammer_done_transaction(&trans);
	lwkt_reltoken(&hmp->fs_token);
	return (error);
}

static int
hammer_vfs_checkexp(struct mount *mp, struct sockaddr *nam,
		    int *exflagsp, struct ucred **credanonp)
{
	hammer_mount_t hmp = (void *)mp->mnt_data;
	struct netcred *np;
	int error;

	lwkt_gettoken(&hmp->fs_token);
	np = vfs_export_lookup(mp, &hmp->export, nam);
	if (np) {
		*exflagsp = np->netc_exflags;
		*credanonp = &np->netc_anon;
		error = 0;
	} else {
		error = EACCES;
	}
	lwkt_reltoken(&hmp->fs_token);
	return (error);

}

int
hammer_vfs_export(struct mount *mp, int op, const struct export_args *export)
{
	hammer_mount_t hmp = (void *)mp->mnt_data;
	int error;

	lwkt_gettoken(&hmp->fs_token);

	switch(op) {
	case MOUNTCTL_SET_EXPORT:
		error = vfs_export(mp, &hmp->export, export);
		break;
	default:
		error = EOPNOTSUPP;
		break;
	}
	lwkt_reltoken(&hmp->fs_token);

	return(error);
}

