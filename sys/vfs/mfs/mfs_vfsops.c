/*
 * Copyright (c) 1989, 1990, 1993, 1994
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)mfs_vfsops.c	8.11 (Berkeley) 6/19/95
 * $FreeBSD: src/sys/ufs/mfs/mfs_vfsops.c,v 1.81.2.3 2001/07/04 17:35:21 tegge Exp $
 * $DragonFly: src/sys/vfs/mfs/mfs_vfsops.c,v 1.17 2004/08/28 19:02:17 dillon Exp $
 */


#include "opt_mfs.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/conf.h>
#include <sys/kernel.h>
#include <sys/proc.h>
#include <sys/buf.h>
#include <sys/mount.h>
#include <sys/signalvar.h>
#include <sys/vnode.h>
#include <sys/malloc.h>
#include <sys/linker.h>
#include <sys/fcntl.h>

#include <sys/buf2.h>

#include <vfs/ufs/quota.h>
#include <vfs/ufs/inode.h>
#include <vfs/ufs/ufsmount.h>
#include <vfs/ufs/ufs_extern.h>
#include <vfs/ufs/fs.h>
#include <vfs/ufs/ffs_extern.h>

#include "mfsnode.h"
#include "mfs_extern.h"

MALLOC_DEFINE(M_MFSNODE, "MFS node", "MFS vnode private part");


extern struct vop_ops *mfs_vnode_vops;

static int	mfs_mount (struct mount *mp,
			char *path, caddr_t data, struct nameidata *ndp, 
			struct thread *td);
static int	mfs_start (struct mount *mp, int flags, struct thread *td);
static int	mfs_statfs (struct mount *mp, struct statfs *sbp, 
			struct thread *td);
static int	mfs_init (struct vfsconf *);

d_open_t	mfsopen;
d_close_t	mfsclose;
d_strategy_t	mfsstrategy;

#define MFS_CDEV_MAJOR	253

static struct cdevsw mfs_cdevsw = {
	/* name */      "MFS",
	/* maj */       MFS_CDEV_MAJOR,
	/* flags */     D_DISK,
	/* port */	NULL,
	/* clone */	NULL,

	/* open */      mfsopen,
	/* close */     mfsclose,
	/* read */      physread,
	/* write */     physwrite,
	/* ioctl */     noioctl,
	/* poll */      nopoll,
	/* mmap */      nommap,
	/* strategy */  mfsstrategy,
	/* dump */      nodump,
	/* psize */     nopsize
};

/*
 * mfs vfs operations.
 */
static struct vfsops mfs_vfsops = {
	mfs_mount,
	mfs_start,
	ffs_unmount,
	ufs_root,
	ufs_quotactl,
	mfs_statfs,
	ffs_sync,
	ffs_vget,
	ffs_fhtovp,
	ufs_check_export,
	ffs_vptofh,
	mfs_init,
	vfs_stduninit,
	vfs_stdextattrctl,
};

VFS_SET(mfs_vfsops, mfs, 0);

/*
 * We allow the underlying MFS block device to be opened and read.
 */
int
mfsopen(dev_t dev, int flags, int mode, struct thread *td)
{
	if (flags & FWRITE)
		return(EROFS);
	if (dev->si_drv1)
		return(0);
	return(ENXIO);
}

int
mfsclose(dev_t dev, int flags, int mode, struct thread *td)
{
	return(0);
}

void
mfsstrategy(struct buf *bp)
{
	struct mfsnode *mfsp;

	if ((mfsp = bp->b_dev->si_drv1) != NULL) {
		off_t boff = (off_t)bp->b_blkno << DEV_BSHIFT;
		off_t eoff = boff + bp->b_bcount;

		if (eoff <= mfsp->mfs_size) {
			bufq_insert_tail(&mfsp->buf_queue, bp);
			wakeup((caddr_t)mfsp);
		} else if (boff < mfsp->mfs_size) {
			bp->b_bcount = mfsp->mfs_size - boff;
			bufq_insert_tail(&mfsp->buf_queue, bp);
			wakeup((caddr_t)mfsp);
		} else if (boff == mfsp->mfs_size) {
			bp->b_resid = bp->b_bcount;
			biodone(bp);
		} else {
			bp->b_error = EINVAL;
			biodone(bp);
		}
	} else {
		bp->b_error = ENXIO;
		bp->b_flags |= B_ERROR;
		biodone(bp);
	}
}

/*
 * mfs_mount
 *
 * Called when mounting local physical media
 *
 * PARAMETERS:
 *		mountroot
 *			mp	mount point structure
 *			path	NULL (flag for root mount!!!)
 *			data	<unused>
 *			ndp	<unused>
 *			p	process (user credentials check [statfs])
 *
 *		mount
 *			mp	mount point structure
 *			path	path to mount point
 *			data	pointer to argument struct in user space
 *			ndp	mount point namei() return (used for
 *				credentials on reload), reused to look
 *				up block device.
 *			p	process (user credentials check)
 *
 * RETURNS:	0	Success
 *		!0	error number (errno.h)
 *
 * LOCK STATE:
 *
 *		ENTRY
 *			mount point is locked
 *		EXIT
 *			mount point is locked
 *
 * NOTES:
 *		A NULL path can be used for a flag since the mount
 *		system call will fail with EFAULT in copyinstr in
 *		namei() if it is a genuine NULL from the user.
 */
/* ARGSUSED */
static int
mfs_mount(struct mount *mp, char *path, caddr_t data, struct nameidata *ndp,
	  struct thread *td)
{
	struct vnode *devvp;
	struct mfs_args args;
	struct ufsmount *ump;
	struct fs *fs;
	struct mfsnode *mfsp;
	size_t size;
	int flags, err;
	int minnum;
	dev_t dev;

	/*
	 * Use NULL path to flag a root mount
	 */
	if( path == NULL) {
		/*
		 ***
		 * Mounting root file system
		 ***
		 */

		/* you lose */
		panic("mfs_mount: mount MFS as root: not configured!");
	}

	/*
	 ***
	 * Mounting non-root file system or updating a file system
	 ***
	 */

	/* copy in user arguments*/
	if ((err = copyin(data, (caddr_t)&args, sizeof (struct mfs_args))) != 0)
		goto error_1;

	/*
	 * If updating, check whether changing from read-only to
	 * read/write; if there is no device name, that's all we do.
	 */
	if (mp->mnt_flag & MNT_UPDATE) {
		/*
		 ********************
		 * UPDATE
		 ********************
		 */
		ump = VFSTOUFS(mp);
		fs = ump->um_fs;
		if (fs->fs_ronly == 0 && (mp->mnt_flag & MNT_RDONLY)) {
			flags = WRITECLOSE;
			if (mp->mnt_flag & MNT_FORCE)
				flags |= FORCECLOSE;
			err = ffs_flushfiles(mp, flags, td);
			if (err)
				goto error_1;
		}
		if (fs->fs_ronly && (mp->mnt_kern_flag & MNTK_WANTRDWR))
			fs->fs_ronly = 0;
		/* if not updating name...*/
		if (args.fspec == 0) {
			/*
			 * Process export requests.  Jumping to "success"
			 * will return the vfs_export() error code. 
			 */
			err = vfs_export(mp, &ump->um_export, &args.export);
			goto success;
		}

		/* XXX MFS does not support name updating*/
		goto success;
	}
	/*
	 * Do the MALLOC before the getnewvnode since doing so afterward
	 * might cause a bogus v_data pointer to get dereferenced
	 * elsewhere if MALLOC should block.
	 */
	MALLOC(mfsp, struct mfsnode *, sizeof *mfsp, M_MFSNODE, M_WAITOK);

	err = getnewvnode(VT_MFS, NULL, mfs_vnode_vops, &devvp, 0, 0);
	if (err) {
		FREE(mfsp, M_MFSNODE);
		goto error_1;
	}

	minnum = (curproc->p_pid & 0xFF) |
		((curproc->p_pid & ~0xFF) << 8);

	devvp->v_type = VCHR;
	dev = make_dev(&mfs_cdevsw, minnum, UID_ROOT, GID_WHEEL, 0600,
			"MFS%d", minnum >> 16);
	/* It is not clear that these will get initialized otherwise */
	dev->si_bsize_phys = DEV_BSIZE;
	dev->si_iosize_max = DFLTPHYS;
	dev->si_drv1 = mfsp;
	addaliasu(devvp, makeudev(MFS_CDEV_MAJOR, minnum));
	devvp->v_data = mfsp;
	mfsp->mfs_baseoff = args.base;
	mfsp->mfs_size = args.size;
	mfsp->mfs_vnode = devvp;
	mfsp->mfs_dev = reference_dev(dev);
	mfsp->mfs_td = td;
	mfsp->mfs_active = 1;
	bufq_init(&mfsp->buf_queue);

	/*
	 * Since this is a new mount, we want the names for
	 * the device and the mount point copied in.  If an
	 * error occurs,  the mountpoint is discarded by the
	 * upper level code.
	 */
	/* Save "last mounted on" info for mount point (NULL pad)*/
	copyinstr(	path,				/* mount point*/
			mp->mnt_stat.f_mntonname,	/* save area*/
			MNAMELEN - 1,			/* max size*/
			&size);				/* real size*/
	bzero( mp->mnt_stat.f_mntonname + size, MNAMELEN - size);

	/* Save "mounted from" info for mount point (NULL pad)*/
	copyinstr(	args.fspec,			/* device name*/
			mp->mnt_stat.f_mntfromname,	/* save area*/
			MNAMELEN - 1,			/* max size*/
			&size);				/* real size*/
	bzero( mp->mnt_stat.f_mntfromname + size, MNAMELEN - size);

	if ((err = ffs_mountfs(devvp, mp, td, M_MFSNODE)) != 0) { 
		mfsp->mfs_active = 0;
		goto error_2;
	}

	/*
	 * Initialize FS stat information in mount struct; uses both
	 * mp->mnt_stat.f_mntonname and mp->mnt_stat.f_mntfromname
	 *
	 * This code is common to root and non-root mounts
	 */
	(void) VFS_STATFS(mp, &mp->mnt_stat, td);

	goto success;

error_2:	/* error with devvp held*/

	/* release devvp before failing*/
	vrele(devvp);

error_1:	/* no state to back out*/

success:
	return( err);
}

/*
 * Used to grab the process and keep it in the kernel to service
 * memory filesystem I/O requests.
 *
 * Loop servicing I/O requests.
 * Copy the requested data into or out of the memory filesystem
 * address space.
 */
/* ARGSUSED */
static int
mfs_start(struct mount *mp, int flags, struct thread *td)
{
	struct vnode *vp = VFSTOUFS(mp)->um_devvp;
	struct mfsnode *mfsp = VTOMFS(vp);
	struct buf *bp;
	int gotsig = 0, sig;

	/*
	 * We must prevent the system from trying to swap
	 * out or kill ( when swap space is low, see vm/pageout.c ) the
	 * process.  A deadlock can occur if the process is swapped out,
	 * and the system can loop trying to kill the unkillable ( while
	 * references exist ) MFS process when swap space is low.
	 */
	KKASSERT(curproc);
	PHOLD(curproc);

	while (mfsp->mfs_active) {
		int s;

		s = splbio();

		while ((bp = bufq_first(&mfsp->buf_queue)) != NULL) {
			bufq_remove(&mfsp->buf_queue, bp);
			splx(s);
			mfs_doio(bp, mfsp);
			wakeup((caddr_t)bp);
			s = splbio();
		}

		splx(s);

		/*
		 * If a non-ignored signal is received, try to unmount.
		 * If that fails, clear the signal (it has been "processed"),
		 * otherwise we will loop here, as tsleep will always return
		 * EINTR/ERESTART.
		 */
		/*
		 * Note that dounmount() may fail if work was queued after
		 * we slept. We have to jump hoops here to make sure that we
		 * process any buffers after the sleep, before we dounmount()
		 */
		if (gotsig) {
			gotsig = 0;
			if (dounmount(mp, 0, td) != 0) {
				KKASSERT(td->td_proc);
				sig = CURSIG(td->td_proc);
				if (sig)
					SIGDELSET(td->td_proc->p_siglist, sig);
			}
		}
		else if (tsleep((caddr_t)mfsp, PCATCH, "mfsidl", 0))
			gotsig++;	/* try to unmount in next pass */
	}
	PRELE(curproc);
	v_release_rdev(vp);	/* hack because we do not implement CLOSE */
	/* XXX destroy/release devvp */
	return (0);
}

/*
 * Get file system statistics.
 */
static int
mfs_statfs(struct mount *mp, struct statfs *sbp, struct thread *td)
{
	int error;

	error = ffs_statfs(mp, sbp, td);
	sbp->f_type = mp->mnt_vfc->vfc_typenum;
	return (error);
}

/*
 * Memory based filesystem initialization.
 */
static int
mfs_init(struct vfsconf *vfsp)
{
	cdevsw_add(&mfs_cdevsw, 0, 0);
	return (0);
}
