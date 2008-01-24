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
 * $DragonFly: src/sys/vfs/hammer/hammer_vfsops.c,v 1.14 2008/01/24 02:14:45 dillon Exp $
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

int hammer_debug_btree;
int hammer_debug_tid;
int hammer_debug_recover = -1;	/* temporarily disabled */
int hammer_count_inodes;
int hammer_count_records;
int hammer_count_record_datas;
int hammer_count_volumes;
int hammer_count_supercls;
int hammer_count_clusters;
int hammer_count_buffers;
int hammer_count_nodes;
int hammer_count_spikes;

SYSCTL_NODE(_vfs, OID_AUTO, hammer, CTLFLAG_RW, 0, "HAMMER filesystem");
SYSCTL_INT(_vfs_hammer, OID_AUTO, debug_btree, CTLFLAG_RW,
	   &hammer_debug_btree, 0, "");
SYSCTL_INT(_vfs_hammer, OID_AUTO, debug_tid, CTLFLAG_RW,
	   &hammer_debug_tid, 0, "");
SYSCTL_INT(_vfs_hammer, OID_AUTO, debug_recover, CTLFLAG_RW,
	   &hammer_debug_recover, 0, "");
SYSCTL_INT(_vfs_hammer, OID_AUTO, count_inodes, CTLFLAG_RD,
	   &hammer_count_inodes, 0, "");
SYSCTL_INT(_vfs_hammer, OID_AUTO, count_records, CTLFLAG_RD,
	   &hammer_count_records, 0, "");
SYSCTL_INT(_vfs_hammer, OID_AUTO, count_record_datas, CTLFLAG_RD,
	   &hammer_count_record_datas, 0, "");
SYSCTL_INT(_vfs_hammer, OID_AUTO, count_volumes, CTLFLAG_RD,
	   &hammer_count_volumes, 0, "");
SYSCTL_INT(_vfs_hammer, OID_AUTO, count_supercls, CTLFLAG_RD,
	   &hammer_count_supercls, 0, "");
SYSCTL_INT(_vfs_hammer, OID_AUTO, count_clusters, CTLFLAG_RD,
	   &hammer_count_clusters, 0, "");
SYSCTL_INT(_vfs_hammer, OID_AUTO, count_buffers, CTLFLAG_RD,
	   &hammer_count_buffers, 0, "");
SYSCTL_INT(_vfs_hammer, OID_AUTO, count_nodes, CTLFLAG_RD,
	   &hammer_count_nodes, 0, "");
SYSCTL_INT(_vfs_hammer, OID_AUTO, count_spikes, CTLFLAG_RD,
	   &hammer_count_spikes, 0, "");

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
static int	hammer_vfs_sync(struct mount *mp, int waitfor);
static int	hammer_vfs_init(struct vfsconf *conf);

static struct vfsops hammer_vfsops = {
	.vfs_mount	= hammer_vfs_mount,
	.vfs_unmount	= hammer_vfs_unmount,
	.vfs_root 	= hammer_vfs_root,
	.vfs_statfs	= hammer_vfs_statfs,
	.vfs_sync	= hammer_vfs_sync,
	.vfs_vget	= hammer_vfs_vget,
	.vfs_init	= hammer_vfs_init
};

MALLOC_DEFINE(M_HAMMER, "hammer-mount", "hammer mount");

VFS_SET(hammer_vfsops, hammer, 0);
MODULE_VERSION(hammer, 1);

static int
hammer_vfs_init(struct vfsconf *conf)
{
	hammer_init_alist_config();
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
	if (info.nvolumes <= 0 || info.nvolumes >= 32768)
		return (EINVAL);

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
		hmp->zbuf = kmalloc(HAMMER_BUFSIZE, M_HAMMER, M_WAITOK|M_ZERO);
		hmp->namekey_iterator = mycpu->gd_time_seconds;
	}
	hmp->hflags = info.hflags;
	if (info.asof) {
		mp->mnt_flag |= MNT_RDONLY;
		hmp->asof = info.asof;
	} else {
		hmp->asof = HAMMER_MAX_TID;
	}

	/*
	 * Re-open read-write if originally read-only, or vise-versa XXX
	 */
	if (mp->mnt_flag & MNT_UPDATE) {
		if (hmp->ronly == 0 && (mp->mnt_flag & MNT_RDONLY)) {
			kprintf("HAMMER read-write -> read-only XXX\n");
			hmp->ronly = 1;
		} else if (hmp->ronly && (mp->mnt_flag & MNT_RDONLY) == 0) {
			kprintf("HAMMER read-only -> read-write XXX\n");
			hmp->ronly = 0;
		}
		return(0);
	}

	RB_INIT(&hmp->rb_vols_root);
	RB_INIT(&hmp->rb_inos_root);
	hmp->ronly = ((mp->mnt_flag & MNT_RDONLY) != 0);

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
	if (error == 0 && hmp->rootcl == NULL) {
		kprintf("hammer_mount: No root cluster found!\n");
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
	mp->mnt_stat.f_fsid.val[0] = 0;	/* XXX */
	mp->mnt_stat.f_fsid.val[1] = 0;	/* XXX */

	/* 
	 * note: f_iosize is used by vnode_pager_haspage() when constructing
	 * its VOP_BMAP call.
	 */
	mp->mnt_stat.f_iosize = HAMMER_BUFSIZE;
	mp->mnt_stat.f_bsize = HAMMER_BUFSIZE;
	vfs_getnewfsid(mp);		/* XXX */
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
		goto done;
	ksnprintf(mp->mnt_stat.f_mntfromname,
		  sizeof(mp->mnt_stat.f_mntfromname), "%s",
		  rootvol->ondisk->vol_name);
	hammer_rel_volume(rootvol, 0);

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

	/*
	 * Unload & flush inodes
	 */
	RB_SCAN(hammer_ino_rb_tree, &hmp->rb_inos_root, NULL,
		hammer_unload_inode, (void *)MNT_WAIT);

	/*
	 * Unload & flush volumes
	 */
	RB_SCAN(hammer_vol_rb_tree, &hmp->rb_vols_root, NULL,
		hammer_unload_volume, NULL);

	mp->mnt_data = NULL;
	mp->mnt_flag &= ~MNT_LOCAL;
	hmp->mp = NULL;
	kfree(hmp->zbuf, M_HAMMER);
	kfree(hmp, M_HAMMER);
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
	struct hammer_mount *hmp = (void *)mp->mnt_data;
	int error;

	if (hmp->rootcl == NULL)
		error = EIO;
	else
		error = hammer_vfs_vget(mp, 1, vpp);
	return (error);
#if 0
	/* FUTURE - cached root vnode */
	if ((vp = hmp->rootvp) != NULL) {
		vref(vp);
		vn_lock(vp, LK_EXCLUSIVE | LK_RETRY);
		*vpp = vp;
		return (0);
	} else {
		*vpp = NULL;
		return (EIO);
	}
#endif
}

static int
hammer_vfs_statfs(struct mount *mp, struct statfs *sbp, struct ucred *cred)
{
	struct hammer_mount *hmp = (void *)mp->mnt_data;
	hammer_volume_t volume;
	hammer_volume_ondisk_t ondisk;
	int error;

	volume = hammer_get_root_volume(hmp, &error);
	if (error)
		return(error);

	ondisk = volume->ondisk;

	mp->mnt_stat.f_bfree = mp->mnt_stat.f_blocks -
				ondisk->vol0_stat_idx_bufs -
				ondisk->vol0_stat_rec_bufs -
				ondisk->vol0_stat_data_bufs;
	if (mp->mnt_stat.f_bfree < 0)
		mp->mnt_stat.f_bfree = 0;
	mp->mnt_stat.f_bavail = mp->mnt_stat.f_bfree;
	mp->mnt_stat.f_files = ondisk->vol0_stat_inodes;
	if (mp->mnt_stat.f_files < 0)
		mp->mnt_stat.f_files = 0;

	hammer_rel_volume(volume, 0);
	*sbp = mp->mnt_stat;
	return(0);
}

static int
hammer_vfs_sync(struct mount *mp, int waitfor)
{
	struct hammer_mount *hmp = (void *)mp->mnt_data;
	int error;

	error = hammer_sync_hmp(hmp, waitfor);
	return(error);
}

