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
 * 3. Neither the name of the University nor the names of its contributors
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
 */


#include <sys/param.h>
#include <sys/systm.h>
#include <sys/conf.h>
#include <sys/device.h>
#include <sys/kernel.h>
#include <sys/proc.h>
#include <sys/buf.h>
#include <sys/mount.h>
#include <sys/signalvar.h>
#include <sys/signal2.h>
#include <sys/spinlock2.h>
#include <sys/vnode.h>
#include <sys/malloc.h>
#include <sys/sysmsg.h>
#include <sys/mman.h>
#include <sys/linker.h>
#include <sys/fcntl.h>
#include <sys/nlookup.h>
#include <sys/devfs.h>

#include <vm/vm.h>
#include <vm/vm_object.h>
#include <vm/vm_page.h>
#include <vm/vm_pager.h>
#include <vm/vnode_pager.h>
#include <vm/vm_extern.h>

#include <sys/buf2.h>
#include <sys/thread2.h>

#include <vfs/ufs/quota.h>
#include <vfs/ufs/inode.h>
#include <vfs/ufs/ufsmount.h>
#include <vfs/ufs/ufs_extern.h>
#include <vfs/ufs/fs.h>
#include <vfs/ufs/ffs_extern.h>

#include "mfsnode.h"
#include "mfs_extern.h"

MALLOC_DEFINE(M_MFSNODE, "MFS node", "MFS vnode private part");

static int	mfs_mount (struct mount *mp,
			char *path, caddr_t data, struct ucred *td);
static int	mfs_start (struct mount *mp, int flags);
static int	mfs_statfs (struct mount *mp, struct statfs *sbp,
			struct ucred *cred); 
static int	mfs_init (struct vfsconf *);
static void	mfs_doio(struct bio *bio, struct mfsnode *mfsp);

d_open_t	mfsopen;
d_close_t	mfsclose;
d_strategy_t	mfsstrategy;

static struct dev_ops mfs_ops = {
	{ "MFS", -1, D_DISK | D_NOEMERGPGR },
	.d_open =	mfsopen,
	.d_close =	mfsclose,
	.d_read =	physread,
	.d_write =	physwrite,
	.d_strategy =	mfsstrategy,
};

/*
 * mfs vfs operations.
 */
static struct vfsops mfs_vfsops = {
	.vfs_flags =		0,
	.vfs_mount =     	mfs_mount,
	.vfs_start =    	mfs_start,
	.vfs_unmount =   	ffs_unmount,
	.vfs_root =     	ufs_root,
	.vfs_quotactl =  	ufs_quotactl,
	.vfs_statfs =   	mfs_statfs,
	.vfs_sync =     	ffs_sync,
	.vfs_vget =      	ffs_vget,
	.vfs_fhtovp =   	ffs_fhtovp,
	.vfs_checkexp =  	ufs_check_export,
	.vfs_vptofh =   	ffs_vptofh,
	.vfs_init =     	mfs_init
};

VFS_SET(mfs_vfsops, mfs, 0);
MODULE_VERSION(mfs, 1);

/*
 * We allow the underlying MFS block device to be opened and read.
 */
int
mfsopen(struct dev_open_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;

#if 0
	if (ap->a_oflags & FWRITE)
		return(EROFS);
#endif
	if (dev->si_drv1)
		return(0);
	return(ENXIO);
}

int
mfsclose(struct dev_close_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	struct mfsnode *mfsp;

	if ((mfsp = dev->si_drv1) == NULL)
		return(0);
        mfsp->mfs_active = 0;
        wakeup((caddr_t)mfsp);
	return(0);
}

int
mfsstrategy(struct dev_strategy_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	struct bio *bio = ap->a_bio;
	struct buf *bp = bio->bio_buf;
	off_t boff = bio->bio_offset;
	off_t eoff = boff + bp->b_bcount;
	struct mfsnode *mfsp;

	if ((mfsp = dev->si_drv1) == NULL) {
		bp->b_error = ENXIO;
		goto error;
	}
	if (boff < 0)
		goto bad;
	if (eoff > mfsp->mfs_size) {
		if (boff > mfsp->mfs_size || (bp->b_flags & B_BNOCLIP))
			goto bad;
		/*
		 * Return EOF by completing the I/O with 0 bytes transfered.
		 * Set B_INVAL to indicate that any data in the buffer is not
		 * valid.
		 */
		if (boff == mfsp->mfs_size) {
			bp->b_resid = bp->b_bcount;
			bp->b_flags |= B_INVAL;
			goto done;
		}
		bp->b_bcount = mfsp->mfs_size - boff;
	}

	/*
	 * Initiate I/O
	 */
	if (mfsp->mfs_td == curthread) {
		mfs_doio(bio, mfsp);
	} else {
		bioq_insert_tail(&mfsp->bio_queue, bio);
		wakeup((caddr_t)mfsp);
	}
	return(0);

	/*
	 * Failure conditions on bio
	 */
bad:
	bp->b_error = EINVAL;
error:
	bp->b_flags |= B_ERROR | B_INVAL;
done:
	biodone(bio);
	return(0);
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
mfs_mount(struct mount *mp, char *path, caddr_t data, struct ucred *cred)
{
	struct vnode *devvp;
	struct mfs_args args;
	struct ufsmount *ump;
	struct fs *fs;
	struct mfsnode *mfsp;
	struct nlookupdata nd;
	size_t size;
	char devname[16];
	int flags;
	int minnum;
	int error;
	cdev_t dev;

	/*
	 * Use NULL path to flag a root mount
	 */
	if (path == NULL) {
		/*
		 ***
		 * Mounting root file system
		 ***
		 */

		/* you lose */
		panic("mfs_mount: mount MFS as root: not configured!");
	}

	mfsp = NULL;

	/*
	 ***
	 * Mounting non-root file system or updating a file system
	 ***
	 */

	/* copy in user arguments*/
	error = copyin(data, (caddr_t)&args, sizeof (struct mfs_args));
	if (error)
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
			error = ffs_flushfiles(mp, flags);
			if (error)
				goto error_1;
		}
		if (fs->fs_ronly && (mp->mnt_kern_flag & MNTK_WANTRDWR)) {
			/* XXX reopen the device vnode read-write */
			fs->fs_ronly = 0;
		}
		/* if not updating name...*/
		if (args.fspec == 0) {
			/*
			 * Process export requests.  Jumping to "success"
			 * will return the vfs_export() error code. 
			 */
			error = vfs_export(mp, &ump->um_export, &args.export);
			goto success;
		}

		/* XXX MFS does not support name updating*/
		goto success;
	}

	/*
	 * Do the MALLOC before the make_dev since doing so afterward
	 * might cause a bogus v_data pointer to get dereferenced
	 * elsewhere if MALLOC should block.
	 */
	mfsp = kmalloc(sizeof *mfsp, M_MFSNODE, M_WAITOK | M_ZERO);

	minnum = (int)curproc->p_pid;

	dev = make_dev(&mfs_ops, minnum, UID_ROOT, GID_WHEEL, 0600,
		       "mfs%d", minnum);
	/* It is not clear that these will get initialized otherwise */
	dev->si_bsize_phys = DEV_BSIZE;
	dev->si_iosize_max = MAXPHYS;
	dev->si_drv1 = mfsp;
	mfsp->mfs_baseoff = args.base;
	mfsp->mfs_size = args.size;
	mfsp->mfs_dev = dev;
	mfsp->mfs_td = curthread;
	mfsp->mfs_active = 1;
	bioq_init(&mfsp->bio_queue);

	devfs_config();	/* sync devfs work */
	ksnprintf(devname, sizeof(devname), "/dev/mfs%d", minnum);
	nlookup_init(&nd, devname, UIO_SYSSPACE, 0);
	devvp = NULL;
	error = nlookup(&nd);
	if (error == 0) {
		devvp = nd.nl_nch.ncp->nc_vp;
		if (devvp == NULL)
			error = ENOENT;
		error = vget(devvp, LK_SHARED);
	}
	nlookup_done(&nd);

	if (error)
		goto error_1;
	vn_unlock(devvp);

	/*
	 * Our 'block' device must be backed by a VM object.  Theoretically
	 * we could use the anonymous memory VM object supplied by userland,
	 * but it would be somewhat of a complex task to deal with it
	 * that way since it would result in I/O requests which supply
	 * the VM pages from our own object.
	 *
	 * vnode_pager_alloc() is typically called when a VM object is
	 * being referenced externally.  We have to undo the refs for
	 * the self reference between vnode and object.
	 */
	vnode_pager_setsize(devvp, args.size);

	/* Save "mounted from" info for mount point (NULL pad)*/
	copyinstr(args.fspec,			/* device name*/
		  mp->mnt_stat.f_mntfromname,	/* save area*/
		  MNAMELEN - 1,			/* max size*/
		  &size);			/* real size*/
	bzero(mp->mnt_stat.f_mntfromname + size, MNAMELEN - size);
	/* vref is eaten by mount? */

	error = ffs_mountfs(devvp, mp, M_MFSNODE);
	if (error) {
		mfsp->mfs_active = 0;
		goto error_2;
	}

	/*
	 * Initialize FS stat information in mount struct; uses
	 * mp->mnt_stat.f_mntfromname.
	 *
	 * This code is common to root and non-root mounts
	 */
	VFS_STATFS(mp, &mp->mnt_stat, cred);

	/*
	 * Mark VFS_START MPSAFE; this is to avoid accessing
	 * per-mount token after VFS_START exits
	 */
	mp->mnt_kern_flag |= MNTK_ST_MPSAFE;

	goto success;

error_2:	/* error with devvp held*/
	vrele(devvp);

error_1:	/* no state to back out*/
	if (mfsp) {
		if (mfsp->mfs_dev) {
			destroy_dev(mfsp->mfs_dev);
			mfsp->mfs_dev = NULL;
		}
		kfree(mfsp, M_MFSNODE);
	}

success:
	return(error);
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
mfs_start(struct mount *mp, int flags)
{
	struct vnode *vp = VFSTOUFS(mp)->um_devvp;
	struct mfsnode *mfsp = vp->v_rdev->si_drv1;
	struct bio *bio;
	struct buf *bp;
	int gotsig = 0, sig;
	thread_t td = curthread;

	/*
	 * We must prevent the system from trying to swap
	 * out or kill ( when swap space is low, see vm/pageout.c ) the
	 * process.  A deadlock can occur if the process is swapped out,
	 * and the system can loop trying to kill the unkillable ( while
	 * references exist ) MFS process when swap space is low.
	 */
	KKASSERT(curproc);
	PHOLD(curproc);

	mfsp->mfs_td = td;

	while (mfsp->mfs_active) {
		crit_enter();

		while ((bio = bioq_takefirst(&mfsp->bio_queue)) != NULL) {
			crit_exit();
			bp = bio->bio_buf;
			mfs_doio(bio, mfsp);
			wakeup(bp);
			crit_enter();
		}

		crit_exit();

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
			if (dounmount(mp, 0, 0) != 0) {
				KKASSERT(td->td_proc);
				lwkt_gettoken(&td->td_proc->p_token);
				sig = CURSIG(td->td_lwp);
				if (sig) {
					spin_lock(&td->td_lwp->lwp_spin);
					lwp_delsig(td->td_lwp, sig, 1);
					spin_unlock(&td->td_lwp->lwp_spin);
				}
				lwkt_reltoken(&td->td_proc->p_token);
			}
		}
		else if (tsleep((caddr_t)mfsp, PCATCH, "mfsidl", 0))
			gotsig++;	/* try to unmount in next pass */
	}
	PRELE(curproc);
        if (mfsp->mfs_dev) {
                destroy_dev(mfsp->mfs_dev);
                mfsp->mfs_dev = NULL;
        }
	kfree(mfsp, M_MFSNODE);
	return (EMOUNTEXIT);
}

/*
 * Get file system statistics.
 */
static int
mfs_statfs(struct mount *mp, struct statfs *sbp, struct ucred *cred)
{
	int error;

	error = ffs_statfs(mp, sbp, cred);
	sbp->f_type = mp->mnt_vfc->vfc_typenum;
	return (error);
}

/*
 * Memory based filesystem initialization.
 */
static int
mfs_init(struct vfsconf *vfsp)
{
	return (0);
}

/*
 * Memory file system I/O.
 *
 * Trivial on the HP since buffer has already been mapping into KVA space.
 *
 * Read and Write are handled with a simple copyin and copyout.
 *
 * We also partially support VOP_FREEBLKS().  We can't implement
 * completely -- for example, on fragments or inode metadata, but we can
 * implement it for page-aligned requests.
 */
static void
mfs_doio(struct bio *bio, struct mfsnode *mfsp)
{
	struct buf *bp = bio->bio_buf;
	caddr_t base = mfsp->mfs_baseoff + bio->bio_offset;
	int bytes;

	switch(bp->b_cmd) {
	case BUF_CMD_FREEBLKS:
		/*
		 * Implement FREEBLKS, which allows the filesystem to tell
		 * a block device when blocks are no longer needed (like when
		 * a file is deleted).  We use the hook to MADV_FREE the VM.
		 * This makes an MFS filesystem work as well or better then
		 * a sun-style swap-mounted filesystem.
		 */
		bytes = bp->b_bcount;

		if ((vm_offset_t)base & PAGE_MASK) {
			int n = PAGE_SIZE - ((vm_offset_t)base & PAGE_MASK);
			bytes -= n;
			base += n;
		}
                if (bytes > 0) {
                        struct madvise_args uap;

			bytes &= ~PAGE_MASK;
			if (bytes != 0) {
				struct sysmsg sysmsg;

				bzero(&sysmsg, sizeof(sysmsg));
				bzero(&uap, sizeof(uap));
				uap.addr  = base;
				uap.len   = bytes;
				uap.behav = MADV_FREE;
				sys_madvise(&sysmsg, &uap);
			}
                }
		bp->b_error = 0;
		break;
	case BUF_CMD_READ:
		/*
		 * Read data from our 'memory' disk
		 */
		bp->b_error = copyin(base, bp->b_data, bp->b_bcount);
		break;
	case BUF_CMD_WRITE:
		/*
		 * Write data to our 'memory' disk
		 */
		bp->b_error = copyout(bp->b_data, base, bp->b_bcount);
		break;
	default:
		panic("mfs: bad b_cmd %d", bp->b_cmd);
	}
	if (bp->b_error)
		bp->b_flags |= B_ERROR;
	biodone(bio);
}
