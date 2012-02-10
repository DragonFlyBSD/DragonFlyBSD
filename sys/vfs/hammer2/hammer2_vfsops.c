/*-
 * Copyright (c) 2011, 2012 The DragonFly Project.  All rights reserved.
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
#include <sys/nlookup.h>
#include <sys/vnode.h>
#include <sys/mount.h>
#include <sys/fcntl.h>
#include <sys/buf.h>
#include <sys/uuid.h>
#include <sys/vfsops.h>

#include "hammer2.h"
#include "hammer2_disk.h"
#include "hammer2_mount.h"

static int	hammer2_init(struct vfsconf *conf);
static int	hammer2_mount(struct mount *mp, char *path, caddr_t data,
			      struct ucred *cred);
static int	hammer2_remount(struct mount *, char *, struct vnode *,
				struct ucred *);
static int	hammer2_unmount(struct mount *mp, int mntflags);
static int	hammer2_root(struct mount *mp, struct vnode **vpp);
static int	hammer2_statfs(struct mount *mp, struct statfs *sbp,
			       struct ucred *cred);
static int	hammer2_statvfs(struct mount *mp, struct statvfs *sbp,
				struct ucred *cred);
static int	hammer2_sync(struct mount *mp, int waitfor);
static int	hammer2_vget(struct mount *mp, struct vnode *dvp,
			     ino_t ino, struct vnode **vpp);
static int	hammer2_fhtovp(struct mount *mp, struct vnode *rootvp,
			       struct fid *fhp, struct vnode **vpp);
static int	hammer2_vptofh(struct vnode *vp, struct fid *fhp);
static int	hammer2_checkexp(struct mount *mp, struct sockaddr *nam,
				 int *exflagsp, struct ucred **credanonp);

/*
 * HAMMER2 vfs operations.
 */
static struct vfsops hammer2_vfsops = {
	.vfs_init	= hammer2_init,
	.vfs_sync	= hammer2_sync,
	.vfs_mount	= hammer2_mount,
	.vfs_unmount	= hammer2_unmount,
	.vfs_root 	= hammer2_root,
	.vfs_statfs	= hammer2_statfs,
	.vfs_statvfs	= hammer2_statvfs,
	.vfs_vget	= hammer2_vget,
	.vfs_vptofh	= hammer2_vptofh,
	.vfs_fhtovp	= hammer2_fhtovp,
	.vfs_checkexp	= hammer2_checkexp
};

MALLOC_DEFINE(M_HAMMER2, "HAMMER2-mount", "");

VFS_SET(hammer2_vfsops, hammer2, 0);
MODULE_VERSION(hammer2, 1);

static int
hammer2_init(struct vfsconf *conf)
{
	int error;

	error = 0;

	if (HAMMER2_BLOCKREF_BYTES != sizeof(struct hammer2_blockref))
		error = EINVAL;
	if (HAMMER2_INODE_BYTES != sizeof(struct hammer2_inode_data))
		error = EINVAL;
	if (HAMMER2_ALLOCREF_BYTES != sizeof(struct hammer2_allocref))
		error = EINVAL;
	if (HAMMER2_VOLUME_BYTES != sizeof(struct hammer2_volume_data))
		error = EINVAL;

	if (error)
		kprintf("HAMMER2 structure size mismatch; cannot continue.\n");

	return (error);
}

/*
 * Mount or remount HAMMER2 fileystem from physical media
 *
 *	mountroot
 *		mp		mount point structure
 *		path		NULL
 *		data		<unused>
 *		cred		<unused>
 *
 *	mount
 *		mp		mount point structure
 *		path		path to mount point
 *		data		pointer to argument structure in user space
 *			volume	volume path (device@LABEL form)
 *			hflags	user mount flags
 *		cred		user credentials
 *
 * RETURNS:	0	Success
 *		!0	error number
 */
static int
hammer2_mount(struct mount *mp, char *path, caddr_t data,
	      struct ucred *cred)
{
	struct hammer2_mount_info info;
	struct hammer2_mount *hmp;
	struct vnode *devvp;
	struct nlookupdata nd;
	char devstr[MNAMELEN];
	size_t size;
	size_t done;
	char *dev, *label;
	int ronly;
	int error;
	int rc;

	hmp = NULL;
	dev = label = NULL;
	devvp = NULL;

	kprintf("hammer2_mount\n");

	if (path == NULL) {
		/*
		 * Root mount
		 */

		return (EOPNOTSUPP);
	} else {
		/*
		 * Non-root mount or updating a mount
		 */

		error = copyin(data, &info, sizeof(info));
		if (error)
			return (error);

		error = copyinstr(info.volume, devstr, MNAMELEN - 1, &done);
		if (error)
			return (error);

		/* Extract device and label */
		dev = devstr;
		label = strchr(devstr, '@');
		if (label == NULL ||
		    ((label + 1) - dev) > done)
			return (EINVAL);
		*label = '\0';
		label++;
		if (*label == '\0')
			return (EINVAL);

		if (mp->mnt_flag & MNT_UPDATE) {
			/* Update mount */
			/* HAMMER2 implements NFS export via mountctl */
			hmp = MPTOH2(mp);
			devvp = hmp->hm_devvp;
			return hammer2_remount(mp, path, devvp, cred);
		}
	}

	/*
	 * New non-root mount
	 */
	/* Lookup name and verify it refers to a block device */
	error = nlookup_init(&nd, dev, UIO_SYSSPACE, NLC_FOLLOW);
	if (error)
		return (error);
	error = nlookup(&nd);
	if (error)
		return (error);
	error = cache_vref(&nd.nl_nch, nd.nl_cred, &devvp);
	if (error)
		return (error);
	nlookup_done(&nd);

	if (!vn_isdisk(devvp, &error)) {
		vrele(devvp);
		return (error);
	}

	/*
	 * Common path for new root/non-root mounts;
	 * devvp is a ref-ed by not locked vnode referring to the fs device
	 */

	error = vfs_mountedon(devvp);
	if (error) {
		vrele(devvp);
		return (error);
	}

	if (vcount(devvp) > 0) {
		vrele(devvp);
		return (EBUSY);
	}

	/*
	 * Open the fs device
	 */
	ronly = (mp->mnt_flag & MNT_RDONLY) != 0;
	vn_lock(devvp, LK_EXCLUSIVE | LK_RETRY);
	error = vinvalbuf(devvp, V_SAVE, 0, 0);
	if (error) {
		vn_unlock(devvp);
		vrele(devvp);
		return (error);
	}
	/* This is correct; however due to an NFS quirk of my setup, FREAD
	 * is required... */
	/*
	error = VOP_OPEN(devvp, ronly ? FREAD : FREAD | FWRITE, FSCRED, NULL);
	 */

	error = VOP_OPEN(devvp, FREAD, FSCRED, NULL);
	vn_unlock(devvp);
	if (error) {
		vrele(devvp);
		return (error);
	}

#ifdef notyet
	/* VOP_IOCTL(EXTENDED_DISK_INFO, devvp); */
	/* if vn device, never use bdwrite(); */
	/* check if device supports BUF_CMD_READALL; */
	/* check if device supports BUF_CMD_WRITEALL; */
#endif

	hmp = kmalloc(sizeof(*hmp), M_HAMMER2, M_WAITOK | M_ZERO);
	mp->mnt_data = (qaddr_t) hmp;
	hmp->hm_mp = mp;
	hmp->hm_ronly = ronly;
	hmp->hm_devvp = devvp;
	lockinit(&hmp->hm_lk, "h2mp", 0, 0);
	kmalloc_create(&hmp->hm_inodes, "HAMMER2-inodes");
	kmalloc_create(&hmp->hm_ipstacks, "HAMMER2-ipstacks");
	
	mp->mnt_flag = MNT_LOCAL;

	/*
	 * Filesystem subroutines are self-synchronized
	 */
	mp->mnt_kern_flag |= MNTK_ALL_MPSAFE;

	/* Setup root inode */
	hmp->hm_iroot = alloci(hmp);
	hmp->hm_iroot->type = HAMMER2_INODE_TYPE_DIR | HAMMER2_INODE_TYPE_ROOT;
	hmp->hm_iroot->inum = 1;

	/* currently rely on tmpfs routines */
	vfs_getnewfsid(mp);
	vfs_add_vnodeops(mp, &hammer2_vnode_vops, &mp->mnt_vn_norm_ops);
	vfs_add_vnodeops(mp, &hammer2_spec_vops, &mp->mnt_vn_spec_ops);
	vfs_add_vnodeops(mp, &hammer2_fifo_vops, &mp->mnt_vn_fifo_ops);

	copystr(info.volume, mp->mnt_stat.f_mntfromname, MNAMELEN - 1, &size);
	bzero(mp->mnt_stat.f_mntfromname + size, MNAMELEN - size);
	bzero(mp->mnt_stat.f_mntonname, sizeof(mp->mnt_stat.f_mntonname));
	copyinstr(path, mp->mnt_stat.f_mntonname,
		  sizeof(mp->mnt_stat.f_mntonname) - 1,
		  &size);

	hammer2_statfs(mp, &mp->mnt_stat, cred);

	hammer2_inode_unlock_ex(hmp->hm_iroot);

	return 0;
}

static int
hammer2_remount(struct mount *mp, char *path, struct vnode *devvp,
                struct ucred *cred)
{
	return (0);
}

static int
hammer2_unmount(struct mount *mp, int mntflags)
{
	struct hammer2_mount *hmp;
	int flags;
	int error;

	kprintf("hammer2_unmount\n");

	hmp = MPTOH2(mp);
	flags = 0;

	if (mntflags & MNT_FORCE)
		flags |= FORCECLOSE;

	hammer2_mount_exlock(hmp);

	error = vflush(mp, 0, flags);

	/*
	 * Work to do:
	 *	1) Wait on the flusher having no work; heat up if needed
	 *	2) Scan inode RB tree till all the inodes are free
	 *	3) Destroy the kmalloc inode zone
	 *	4) Free the mount point
	 */

	kmalloc_destroy(&hmp->hm_inodes);
	kmalloc_destroy(&hmp->hm_ipstacks);

	hammer2_mount_unlock(hmp);

	// Tmpfs does this
	//kfree(hmp, M_HAMMER2);


	return (error);
}

static int
hammer2_vget(struct mount *mp, struct vnode *dvp,
	     ino_t ino, struct vnode **vpp)
{
	kprintf("hammer2_vget\n");
	return (EOPNOTSUPP);
}

static int
hammer2_root(struct mount *mp, struct vnode **vpp)
{
	struct hammer2_mount *hmp;
	int error;
	struct vnode *vp;

	kprintf("hammer2_root\n");

	hmp = MPTOH2(mp);
	hammer2_mount_lock_ex(hmp);
	if (hmp->hm_iroot == NULL) {
		*vpp = NULL;
		error = EINVAL;
	} else {
		vp = igetv(hmp->hm_iroot, &error);
		*vpp = vp;
		if (vp == NULL)
			kprintf("vnodefail\n");
	}
	hammer2_mount_unlock(hmp);

	return (error);
}

static int
hammer2_statfs(struct mount *mp, struct statfs *sbp, struct ucred *cred)
{
	struct hammer2_mount *hmp;

	kprintf("hammer2_statfs\n");

	hmp = MPTOH2(mp);

	sbp->f_iosize = PAGE_SIZE;
	sbp->f_bsize = PAGE_SIZE;

	sbp->f_blocks = 10;
	sbp->f_bavail = 10;
	sbp->f_bfree = 10;

	sbp->f_files = 10;
	sbp->f_ffree = 10;
	sbp->f_owner = 0;

	return (0);
}

static int
hammer2_statvfs(struct mount *mp, struct statvfs *sbp, struct ucred *cred)
{
	kprintf("hammer2_statvfs\n");
	return (EOPNOTSUPP);
}

/*
 * Sync the entire filesystem; this is called from the filesystem syncer
 * process periodically and whenever a user calls sync(1) on the hammer
 * mountpoint.
 *
 * Currently is actually called from the syncer! \o/
 *
 * This task will have to snapshot the state of the dirty inode chain.
 * From that, it will have to make sure all of the inodes on the dirty
 * chain have IO initiated. We make sure that io is initiated for the root
 * block.
 *
 * If waitfor is set, we wait for media to acknowledge the new rootblock.
 *
 * THINKS: side A vs side B, to have sync not stall all I/O?
 */
static int
hammer2_sync(struct mount *mp, int waitfor)
{
	struct hammer2_mount *hmp;
	struct hammer2_inode *ip;

	kprintf("hammer2_sync \n");

//	hmp = MPTOH2(mp);

	return (0);
}

static int
hammer2_vptofh(struct vnode *vp, struct fid *fhp)
{
	return (0);
}

static int
hammer2_fhtovp(struct mount *mp, struct vnode *rootvp,
	       struct fid *fhp, struct vnode **vpp)
{
	return (0);
}

static int
hammer2_checkexp(struct mount *mp, struct sockaddr *nam,
		 int *exflagsp, struct ucred **credanonp)
{
	return (0);
}
