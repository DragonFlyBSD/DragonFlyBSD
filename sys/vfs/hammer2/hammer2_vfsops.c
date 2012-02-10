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

static
int
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
static
int
hammer2_mount(struct mount *mp, char *path, caddr_t data,
	      struct ucred *cred)
{
	struct hammer2_mount_info info;
	hammer2_mount_t *hmp;
	struct vnode *devvp;
	struct hammer2_volume_data *vd;
	struct nlookupdata nd;
	struct buf *bp;
	char devstr[MNAMELEN];
	size_t size;
	size_t done;
	char *dev, *label;
	int ronly = 1;
	int error;

	hmp = NULL;
	dev = label = NULL;
	devvp = NULL;
	vd = NULL;

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
			devvp = hmp->devvp;
			error = hammer2_remount(mp, path, devvp, cred);
			return error;
		}
	}

	/*
	 * New non-root mount
	 */
	/* Lookup name and verify it refers to a block device */
	error = nlookup_init(&nd, dev, UIO_SYSSPACE, NLC_FOLLOW);
	if (error == 0)
		error = nlookup(&nd);
	if (error == 0)
		error = cache_vref(&nd.nl_nch, nd.nl_cred, &devvp);
	nlookup_done(&nd);

	if (error == 0) {
		if (vn_isdisk(devvp, &error))
			error = vfs_mountedon(devvp);
	}
	if (error == 0 && vcount(devvp) > 0)
		error = EBUSY;

	/*
	 * Now open the device
	 */
	if (error == 0) {
		ronly = ((mp->mnt_flag & MNT_RDONLY) != 0);
		vn_lock(devvp, LK_EXCLUSIVE | LK_RETRY);
		error = vinvalbuf(devvp, V_SAVE, 0, 0);
		if (error == 0) {
			error = VOP_OPEN(devvp, ronly ? FREAD : FREAD | FWRITE,
					 FSCRED, NULL);
		}
		vn_unlock(devvp);
	}
	if (error && devvp) {
		vrele(devvp);
		devvp = NULL;
	}
	if (error)
		return error;

	/* Read the volume header */
	/* XXX: Read four volhdrs, find one w/ highest TID & CRC */
	error = bread(devvp, 0, HAMMER2_PBUFSIZE, &bp);
	vd = (struct hammer2_volume_data *) bp->b_data;
	if (error ||
	    (vd->magic != HAMMER2_VOLUME_ID_HBO)) {
		brelse(bp);
		vrele(devvp);
		VOP_CLOSE(devvp, ronly ? FREAD : FREAD | FWRITE);
		return (EINVAL);
	}

	/*
	 * Block device opened successfully, finish initializing the
	 * mount structure.
	 *
	 * From this point on we have to call hammer2_unmount() on failure.
	 */
	hmp = kmalloc(sizeof(*hmp), M_HAMMER2, M_WAITOK | M_ZERO);
	mp->mnt_data = (qaddr_t)hmp;
	hmp->mp = mp;
	hmp->ronly = ronly;
	hmp->devvp = devvp;
	lockinit(&hmp->lk, "h2mp", 0, 0);
	kmalloc_create(&hmp->inodes, "HAMMER2-inodes");
	kmalloc_create(&hmp->ipstacks, "HAMMER2-ipstacks");
	
	bcopy(bp->b_data, &hmp->voldata, sizeof(struct hammer2_volume_data));
	brelse(bp);
	bp = NULL;

	mp->mnt_flag = MNT_LOCAL;

	/*
	 * Filesystem subroutines are self-synchronized
	 */
	mp->mnt_kern_flag |= MNTK_ALL_MPSAFE;

	/* Setup root inode */
	hmp->iroot = hammer2_alloci(hmp);
	hmp->iroot->type = HAMMER2_INODE_TYPE_DIR | HAMMER2_INODE_TYPE_ROOT;
	hmp->iroot->data.inum = 1;

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

	hammer2_inode_unlock_ex(hmp->iroot);

	return 0;
}

static
int
hammer2_remount(struct mount *mp, char *path, struct vnode *devvp,
                struct ucred *cred)
{
	return (0);
}

static
int
hammer2_unmount(struct mount *mp, int mntflags)
{
	hammer2_mount_t *hmp;
	int flags;
	int error;
	int ronly = ((mp->mnt_flag & MNT_RDONLY) != 0);
	struct vnode *devvp;

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

	if (hmp->iroot) {
		hammer2_inode_lock_ex(hmp->iroot);
		hammer2_freei(hmp->iroot);
		hmp->iroot = NULL;
	}
	if ((devvp = hmp->devvp) != NULL) {
		vinvalbuf(devvp, (ronly ? 0 : V_SAVE), 0, 0);
		hmp->devvp = NULL;
		VOP_CLOSE(devvp, (ronly ? FREAD : FREAD|FWRITE));
		vrele(devvp);
		devvp = NULL;
	}

	kmalloc_destroy(&hmp->inodes);
	kmalloc_destroy(&hmp->ipstacks);

	hammer2_mount_unlock(hmp);

	mp->mnt_data = NULL;
	hmp->mp = NULL;
	kfree(hmp, M_HAMMER2);

	return (error);
}

static
int
hammer2_vget(struct mount *mp, struct vnode *dvp,
	     ino_t ino, struct vnode **vpp)
{
	kprintf("hammer2_vget\n");
	return (EOPNOTSUPP);
}

static
int
hammer2_root(struct mount *mp, struct vnode **vpp)
{
	hammer2_mount_t *hmp;
	int error;
	struct vnode *vp;

	kprintf("hammer2_root\n");

	hmp = MPTOH2(mp);
	hammer2_mount_exlock(hmp);
	if (hmp->iroot == NULL) {
		*vpp = NULL;
		error = EINVAL;
	} else {
		vp = hammer2_igetv(hmp->iroot, &error);
		*vpp = vp;
		if (vp == NULL)
			kprintf("vnodefail\n");
	}
	hammer2_mount_unlock(hmp);

	return (error);
}

static
int
hammer2_statfs(struct mount *mp, struct statfs *sbp, struct ucred *cred)
{
	hammer2_mount_t *hmp;

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

static
int
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
static
int
hammer2_sync(struct mount *mp, int waitfor)
{
#if 0
	hammer2_mount_t *hmp;
	hammer2_inode_t *ip;
#endif

	kprintf("hammer2_sync \n");

#if 0
	hmp = MPTOH2(mp);
#endif

	return (0);
}

static
int
hammer2_vptofh(struct vnode *vp, struct fid *fhp)
{
	return (0);
}

static
int
hammer2_fhtovp(struct mount *mp, struct vnode *rootvp,
	       struct fid *fhp, struct vnode **vpp)
{
	return (0);
}

static
int
hammer2_checkexp(struct mount *mp, struct sockaddr *nam,
		 int *exflagsp, struct ucred **credanonp)
{
	return (0);
}
