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
 * $DragonFly: src/sys/vfs/hammer/hammer_vfsops.c,v 1.2 2007/11/02 00:57:16 dillon Exp $
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/vnode.h>
#include <sys/mount.h>
#include <sys/malloc.h>
#include <sys/nlookup.h>
#include <sys/fcntl.h>
#include <sys/buf.h>
#include <sys/buf2.h>
#include "hammer.h"

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
	struct hammer_mount *hmp;
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
	hmp = kmalloc(sizeof(*hmp), M_HAMMER, M_WAITOK | M_ZERO);
	mp->mnt_data = (qaddr_t)hmp;
	hmp->mp = mp;
	RB_INIT(&hmp->rb_vols_root);
	RB_INIT(&hmp->rb_inos_root);

	/*
	 * Load volumes
	 */
	path = objcache_get(namei_oc, M_WAITOK);
	for (i = 0; i < info.nvolumes; ++i) {
		error = copyin(&info.volumes[i], &upath, sizeof(char *));
		if (error == 0)
			error = copyinstr(upath, path, MAXPATHLEN, NULL);
		if (error == 0)
			error = hammer_load_volume(hmp, path);
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
	vfs_getnewfsid(mp);		/* XXX */
	mp->mnt_maxsymlinklen = 255;
	mp->mnt_flag |= MNT_LOCAL;

	vfs_add_vnodeops(mp, &hammer_vnode_vops, &mp->mnt_vn_norm_ops);

	/*
	 * Locate the root directory using the root cluster's B-Tree as a
	 * starting point.  The root directory uses an obj_id of 1.
	 *
	 * FUTURE: Leave the root directory cached referenced but unlocked
	 * in hmp->rootvp (need to flush it on unmount).
	 */
	error = hammer_vfs_vget(mp, 1, &rootvp);
	if (error == 0)
		vput(rootvp);
	/*vn_unlock(hmp->rootvp);*/

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

	/*
	 *
	 */

	/*
	 * Clean out the vnodes
	 */
	flags = WRITECLOSE | ((mntflags & MNT_FORCE) ? FORCECLOSE : 0);
	vflush(mp, 0, flags);

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
		hammer_unload_inode, hmp);

	/*
	 * Unload & flush volumes
	 */
	RB_SCAN(hammer_vol_rb_tree, &hmp->rb_vols_root, NULL,
		hammer_unload_volume, NULL);

	mp->mnt_data = NULL;
	hmp->mp = NULL;
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
	*sbp = mp->mnt_stat;
	return(0);
}

static int
hammer_vfs_sync(struct mount *mp, int waitfor)
{
	return(0);
}

