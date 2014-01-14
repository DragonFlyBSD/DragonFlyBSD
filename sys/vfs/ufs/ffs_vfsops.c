/*
 * Copyright (c) 1989, 1991, 1993, 1994
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
 *	@(#)ffs_vfsops.c	8.31 (Berkeley) 5/20/95
 * $FreeBSD: src/sys/ufs/ffs/ffs_vfsops.c,v 1.117.2.10 2002/06/23 22:34:52 iedowse Exp $
 */

#include "opt_quota.h"

#include <sys/disk.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/nlookup.h>
#include <sys/kernel.h>
#include <sys/vnode.h>
#include <sys/mount.h>
#include <sys/buf.h>
#include <sys/conf.h>
#include <sys/fcntl.h>
#include <sys/diskslice.h>
#include <sys/malloc.h>

#include "quota.h"
#include "ufsmount.h"
#include "inode.h"
#include "ufs_extern.h"

#include "fs.h"
#include "ffs_extern.h"

#include <vm/vm.h>
#include <vm/vm_page.h>
#include <vm/vm_zone.h>

#include <sys/buf2.h>

static MALLOC_DEFINE(M_FFSNODE, "FFS node", "FFS vnode private part");

static int	ffs_sbupdate (struct ufsmount *, int);
static int	ffs_reload (struct mount *, struct ucred *);
static int	ffs_oldfscompat (struct fs *);
static int	ffs_mount (struct mount *, char *, caddr_t, struct ucred *);
static int	ffs_init (struct vfsconf *);

static struct vfsops ufs_vfsops = {
	.vfs_mount =    	ffs_mount,
	.vfs_unmount =    	ffs_unmount,
	.vfs_root =    		ufs_root,
	.vfs_quotactl =    	ufs_quotactl,
	.vfs_statfs =    	ffs_statfs,
	.vfs_sync =    		ffs_sync,
	.vfs_vget =    		ffs_vget,
	.vfs_fhtovp =    	ffs_fhtovp,
	.vfs_checkexp =    	ufs_check_export,
	.vfs_vptofh =    	ffs_vptofh,
	.vfs_init =    		ffs_init,
};

VFS_SET(ufs_vfsops, ufs, 0);
MODULE_VERSION(ufs, 1);

extern struct vop_ops ffs_vnode_vops;
extern struct vop_ops ffs_spec_vops;
extern struct vop_ops ffs_fifo_vops;

/*
 * ffs_mount
 *
 * Called when mounting local physical media
 *
 * PARAMETERS:
 *		mountroot
 *			mp	mount point structure
 *			path	NULL (flag for root mount!!!)
 *			data	<unused>
 *			p	process (user credentials check [statfs])
 *
 *		mount
 *			mp	mount point structure
 *			path	path to mount point
 *			data	pointer to argument struct in user space
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
 *		nlookup() if it is a genuine NULL from the user.
 */
static int
ffs_mount(struct mount *mp,		/* mount struct pointer */
          char *path,			/* path to mount point */
          caddr_t data,			/* arguments to FS specific mount */
          struct ucred	*cred)		/* process requesting mount */
{
	size_t		size;
	int		error;
	struct vnode	*devvp;

	struct ufs_args args;
	struct ufsmount *ump = NULL;
	struct fs *fs;
	int flags, ronly = 0;
	mode_t accessmode;
	struct nlookupdata nd;
	struct vnode *rootvp;

	devvp = NULL;
	error = 0;

	/*
	 * Use NULL path to flag a root mount
	 */
	if (path == NULL) {
		/*
		 ***
		 * Mounting root filesystem
		 ***
		 */
	
		if ((error = bdevvp(rootdev, &rootvp))) {
			kprintf("ffs_mountroot: can't find rootvp\n");
			return (error);
		}

		if( ( error = ffs_mountfs(rootvp, mp, M_FFSNODE)) != 0) {
			/* fs specific cleanup (if any)*/
			goto error_1;
		}
		devvp = rootvp;

		goto dostatfs;		/* success*/

	}

	/*
	 ***
	 * Mounting non-root filesystem or updating a filesystem
	 ***
	 */

	/* copy in user arguments*/
	error = copyin(data, (caddr_t)&args, sizeof (struct ufs_args));
	if (error)
		goto error_1;		/* can't get arguments*/

	/*
	 * If updating, check whether changing from read-only to
	 * read/write; if there is no device name, that's all we do.
	 */
	if (mp->mnt_flag & MNT_UPDATE) {
		ump = VFSTOUFS(mp);
		fs = ump->um_fs;
		devvp = ump->um_devvp;
		error = 0;
		ronly = fs->fs_ronly;	/* MNT_RELOAD might change this */
		if (ronly == 0 && (mp->mnt_flag & MNT_RDONLY)) {
			/*
			 * Flush any dirty data.
			 */
			VFS_SYNC(mp, MNT_WAIT);
			/*
			 * Check for and optionally get rid of files open
			 * for writing.
			 */
			flags = WRITECLOSE;
			if (mp->mnt_flag & MNT_FORCE)
				flags |= FORCECLOSE;
			if (mp->mnt_flag & MNT_SOFTDEP) {
				error = softdep_flushfiles(mp, flags);
			} else {
				error = ffs_flushfiles(mp, flags);
			}
			ronly = 1;
		}
		if (!error && (mp->mnt_flag & MNT_RELOAD)) {
			error = ffs_reload(mp, NULL);
		}
		if (error) {
			goto error_1;
		}
		if (ronly && (mp->mnt_kern_flag & MNTK_WANTRDWR)) {
			/*
			 * If upgrade to read-write by non-root, then verify
			 * that user has necessary permissions on the device.
			 */
			if (cred->cr_uid != 0) {
				vn_lock(devvp, LK_EXCLUSIVE | LK_RETRY);
				if ((error = VOP_EACCESS(devvp, VREAD | VWRITE,
				    cred)) != 0) {
					vn_unlock(devvp);
					return (error);
				}
				vn_unlock(devvp);
			}

			fs->fs_flags &= ~FS_UNCLEAN;
			if (fs->fs_clean == 0) {
				fs->fs_flags |= FS_UNCLEAN;
				if (mp->mnt_flag & MNT_FORCE) {
					kprintf(
"WARNING: %s was not properly dismounted\n",
					    fs->fs_fsmnt);
				} else {
					kprintf(
"WARNING: R/W mount of %s denied.  Filesystem is not clean - run fsck\n",
					    fs->fs_fsmnt);
					error = EPERM;
					goto error_1;
				}
			}

			/* check to see if we need to start softdep */
			if (fs->fs_flags & FS_DOSOFTDEP) {
				error = softdep_mount(devvp, mp, fs);
				if (error)
					goto error_1;
			}
			ronly = 0;
		}
		/*
		 * Soft updates is incompatible with "async",
		 * so if we are doing softupdates stop the user
		 * from setting the async flag in an update.
		 * Softdep_mount() clears it in an initial mount 
		 * or ro->rw remount.
		 */
		if (mp->mnt_flag & MNT_SOFTDEP) {
			mp->mnt_flag &= ~MNT_ASYNC;
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
	}

	/*
	 * Not an update, or updating the name: look up the name
	 * and verify that it refers to a sensible block device.
	 */
	devvp = NULL;
	error = nlookup_init(&nd, args.fspec, UIO_USERSPACE, NLC_FOLLOW);
	if (error == 0)
		error = nlookup(&nd);
	if (error == 0)
		error = cache_vref(&nd.nl_nch, nd.nl_cred, &devvp);
	nlookup_done(&nd);
	if (error)
		goto error_1;

	if (!vn_isdisk(devvp, &error))
		goto error_2;

	/*
	 * If mount by non-root, then verify that user has necessary
	 * permissions on the device.
	 */
	if (cred->cr_uid != 0) {
		accessmode = VREAD;
		if ((mp->mnt_flag & MNT_RDONLY) == 0)
			accessmode |= VWRITE;
		vn_lock(devvp, LK_EXCLUSIVE | LK_RETRY);
		if ((error = VOP_EACCESS(devvp, accessmode, cred)) != 0) {
			vput(devvp);
			return (error);
		}
		vn_unlock(devvp);
	}

	if (mp->mnt_flag & MNT_UPDATE) {
		/*
		 * UPDATE - make sure the resolved vnode represents the same
		 * device. Since devfs, devvp->v_rdev can be used directly as
		 * it is always associated as long as the vnode exists.
		 *
		 * Our current open/writecount state is associated with
		 * um_devvp, so continue using um_devvp and throw away devvp.
		 */
		if (devvp != ump->um_devvp) {
			if (devvp->v_rdev == ump->um_devvp->v_rdev) {
				vrele(devvp);
				devvp = ump->um_devvp;
			} else {
				kprintf("cannot update mount, v_rdev does"
					" not match (%p vs %p)\n",
					devvp->v_rdev, ump->um_devvp->v_rdev);
				error = EINVAL;	/* needs translation */
			}
		} else {
			vrele(devvp);
		}
		/*
		 * Update device name only on success
		 */
		if (!error) {
			/* Save "mounted from" info for mount point (NULL pad)*/
			copyinstr(	args.fspec,
					mp->mnt_stat.f_mntfromname,
					MNAMELEN - 1,
					&size);
			bzero( mp->mnt_stat.f_mntfromname + size, MNAMELEN - size);
		}
	} else {
		/*
		 ********************
		 * NEW MOUNT
		 ********************
		 */

		/* Save "mounted from" info for mount point (NULL pad)*/
		copyinstr(	args.fspec,			/* device name*/
				mp->mnt_stat.f_mntfromname,	/* save area*/
				MNAMELEN - 1,			/* max size*/
				&size);				/* real size*/
		bzero( mp->mnt_stat.f_mntfromname + size, MNAMELEN - size);

                /* Save "last mounted on" info for mount point (NULL pad)*/
		bzero(mp->mnt_stat.f_mntonname,
		      sizeof(mp->mnt_stat.f_mntonname));
		if (path) {
			copyinstr(path, mp->mnt_stat.f_mntonname,
				  sizeof(mp->mnt_stat.f_mntonname) - 1,
				  &size);
		} else {	/* Root mount */
			mp->mnt_stat.f_mntonname[0] = '/';
		}

		error = ffs_mountfs(devvp, mp, M_FFSNODE);
	}
	if (error) {
		goto error_2;
	}

dostatfs:
	/*
	 * Initialize FS stat information in mount struct; uses
	 * mp->mnt_stat.f_mntfromname.
	 *
	 * This code is common to root and non-root mounts
	 */
	(void)VFS_STATFS(mp, &mp->mnt_stat, cred);

	goto success;


error_2:	/* error with devvp held*/

	/* release devvp before failing*/
	vrele(devvp);

error_1:	/* no state to back out*/

success:
	if (!error && path && (mp->mnt_flag & MNT_UPDATE)) {
		/* Update clean flag after changing read-onlyness. */
		fs = ump->um_fs;
		if (ronly != fs->fs_ronly) {
			fs->fs_ronly = ronly;
			fs->fs_clean = ronly &&
			    (fs->fs_flags & FS_UNCLEAN) == 0 ? 1 : 0;

			/*
			 * The device must be re-opened as appropriate or
			 * the device close at unmount time will panic.
			 */
			vn_lock(devvp, LK_EXCLUSIVE | LK_RETRY);
			if (ronly) {
				VOP_OPEN(devvp, FREAD, FSCRED, NULL);
				VOP_CLOSE(devvp, FREAD|FWRITE, NULL);
			} else {
				VOP_OPEN(devvp, FREAD|FWRITE, FSCRED, NULL);
				VOP_CLOSE(devvp, FREAD, NULL);
			}
			vn_unlock(devvp);
			ffs_sbupdate(ump, MNT_WAIT);
		}
	}
	return (error);
}

/*
 * Reload all incore data for a filesystem (used after running fsck on
 * the root filesystem and finding things to fix). The filesystem must
 * be mounted read-only.
 *
 * Things to do to update the mount:
 *	1) invalidate all cached meta-data.
 *	2) re-read superblock from disk.
 *	3) re-read summary information from disk.
 *	4) invalidate all inactive vnodes.
 *	5) invalidate all cached file data.
 *	6) re-read inode data for all active vnodes.
 */

static int ffs_reload_scan2(struct mount *mp, struct vnode *vp, void *data);

struct scaninfo {
	int rescan;
	struct fs *fs;
	struct vnode *devvp;
	int waitfor;
	int allerror;
};

static int
ffs_reload(struct mount *mp, struct ucred *cred)
{
	struct vnode *devvp;
	void *space;
	struct buf *bp;
	struct fs *fs, *newfs;
	struct partinfo dpart;
	int i, blks, size, error;
	struct scaninfo scaninfo;
	int32_t *lp;

	if ((mp->mnt_flag & MNT_RDONLY) == 0)
		return (EINVAL);
	/*
	 * Step 1: invalidate all cached meta-data.
	 */
	devvp = VFSTOUFS(mp)->um_devvp;
	vn_lock(devvp, LK_EXCLUSIVE | LK_RETRY);
	error = vinvalbuf(devvp, 0, 0, 0);
	vn_unlock(devvp);
	if (error)
		panic("ffs_reload: dirty1");

	/*
	 * The backing device must be VMIO-capable because we use getblk().
	 * NOTE: the MFS driver now returns a VMIO-enabled descriptor.
	 */
	if (devvp->v_object == NULL)
		panic("ffs_reload: devvp has no VM object!");

	/*
	 * Step 2: re-read superblock from disk.
	 */
	if (VOP_IOCTL(devvp, DIOCGPART, (caddr_t)&dpart, FREAD,
	    cred, NULL) != 0) {
		size = DEV_BSIZE;
	} else {
		size = dpart.media_blksize;
	}
	if ((error = bread(devvp, SBOFF, SBSIZE, &bp)) != 0) {
		brelse(bp);
		return (error);
	}
	newfs = (struct fs *)bp->b_data;
	if (newfs->fs_magic != FS_MAGIC || newfs->fs_bsize > MAXBSIZE ||
		newfs->fs_bsize < sizeof(struct fs)) {
			brelse(bp);
			return (EIO);		/* XXX needs translation */
	}
	fs = VFSTOUFS(mp)->um_fs;
	/*
	 * Copy pointer fields back into superblock before copying in	XXX
	 * new superblock. These should really be in the ufsmount.	XXX
	 * Note that important parameters (eg fs_ncg) are unchanged.
	 */
	newfs->fs_csp = fs->fs_csp;
	newfs->fs_maxcluster = fs->fs_maxcluster;
	newfs->fs_contigdirs = fs->fs_contigdirs;
	/* The filesystem is still read-only. */
	newfs->fs_ronly = 1;
	bcopy(newfs, fs, (uint)fs->fs_sbsize);
	if (fs->fs_sbsize < SBSIZE)
		bp->b_flags |= B_INVAL;
	brelse(bp);
	mp->mnt_maxsymlinklen = fs->fs_maxsymlinklen;
	ffs_oldfscompat(fs);
	/* An old fsck may have zeroed these fields, so recheck them. */
	if (fs->fs_avgfilesize <= 0)		/* XXX */
		fs->fs_avgfilesize = AVFILESIZ;	/* XXX */
	if (fs->fs_avgfpdir <= 0)		/* XXX */
		fs->fs_avgfpdir = AFPDIR;	/* XXX */

	/*
	 * Step 3: re-read summary information from disk.
	 */
	blks = howmany(fs->fs_cssize, fs->fs_fsize);
	space = fs->fs_csp;
	for (i = 0; i < blks; i += fs->fs_frag) {
		size = fs->fs_bsize;
		if (i + fs->fs_frag > blks)
			size = (blks - i) * fs->fs_fsize;
		error = bread(devvp, fsbtodoff(fs, fs->fs_csaddr + i), size, &bp);
		if (error) {
			brelse(bp);
			return (error);
		}
		bcopy(bp->b_data, space, (uint)size);
		space = (char *)space + size;
		brelse(bp);
	}
	/*
	 * We no longer know anything about clusters per cylinder group.
	 */
	if (fs->fs_contigsumsize > 0) {
		lp = fs->fs_maxcluster;
		for (i = 0; i < fs->fs_ncg; i++)
			*lp++ = fs->fs_contigsumsize;
	}

	scaninfo.rescan = 0;
	scaninfo.fs = fs;
	scaninfo.devvp = devvp;
	while (error == 0 && scaninfo.rescan) {
		scaninfo.rescan = 0;
		error = vmntvnodescan(mp, VMSC_GETVX, 
					NULL, ffs_reload_scan2, &scaninfo);
	}
	return(error);
}

static int
ffs_reload_scan2(struct mount *mp, struct vnode *vp, void *data)
{
	struct scaninfo *info = data;
	struct inode *ip;
	struct buf *bp;
	int error;

	/*
	 * Try to recycle
	 */
	if (vrecycle(vp))
		return(0);

	if (vinvalbuf(vp, 0, 0, 0))
		panic("ffs_reload: dirty2");
	/*
	 * Step 6: re-read inode data for all active vnodes.
	 */
	ip = VTOI(vp);
	error = bread(info->devvp,
		    fsbtodoff(info->fs, ino_to_fsba(info->fs, ip->i_number)),
		    (int)info->fs->fs_bsize, &bp);
	if (error) {
		brelse(bp);
		return (error);
	}
	ip->i_din = *((struct ufs1_dinode *)bp->b_data +
	    ino_to_fsbo(info->fs, ip->i_number));
	ip->i_effnlink = ip->i_nlink;
	brelse(bp);
	return(0);
}

/*
 * Common code for mount and mountroot
 */
int
ffs_mountfs(struct vnode *devvp, struct mount *mp, struct malloc_type *mtype)
{
	struct ufsmount *ump;
	struct buf *bp;
	struct fs *fs;
	cdev_t dev;
	struct partinfo dpart;
	void *space;
	int error, i, blks, size, ronly;
	int32_t *lp;
	uint64_t maxfilesize;					/* XXX */

	/*
	 * Disallow multiple mounts of the same device.
	 * Disallow mounting of a device that is currently in use
	 * Flush out any old buffers remaining from a previous use.
	 */
	error = vfs_mountedon(devvp);
	if (error)
		return (error);
	if (vcount(devvp) > 0)
		return (EBUSY);
	vn_lock(devvp, LK_EXCLUSIVE | LK_RETRY);
	error = vinvalbuf(devvp, V_SAVE, 0, 0);
	vn_unlock(devvp);
	if (error)
		return (error);

	ronly = (mp->mnt_flag & MNT_RDONLY) != 0;
	vn_lock(devvp, LK_EXCLUSIVE | LK_RETRY);
	error = VOP_OPEN(devvp, ronly ? FREAD : FREAD|FWRITE, FSCRED, NULL);
	vn_unlock(devvp);
	if (error)
		return (error);
	dev = devvp->v_rdev;
	if (dev->si_iosize_max != 0)
		mp->mnt_iosize_max = dev->si_iosize_max;
	if (mp->mnt_iosize_max > MAXPHYS)
		mp->mnt_iosize_max = MAXPHYS;

	/*
	 * Filesystem supports native FSMIDs
	 */
	mp->mnt_kern_flag |= MNTK_FSMID;

	/*
	 * The backing device must be VMIO-capable because we use getblk().
	 * NOTE: the MFS driver now returns a VMIO-enabled descriptor.
	 * The VOP_OPEN() call above should have associated a VM object
	 * with devvp.
	 */
	if (devvp->v_object == NULL)
		panic("ffs_reload: devvp has no VM object!");

	if (VOP_IOCTL(devvp, DIOCGPART, (caddr_t)&dpart, FREAD,
		      proc0.p_ucred, NULL) != 0) {
		size = DEV_BSIZE;
	} else {
		size = dpart.media_blksize;
	}

	bp = NULL;
	ump = NULL;
	if ((error = bread(devvp, SBOFF, SBSIZE, &bp)) != 0)
		goto out;
	fs = (struct fs *)bp->b_data;
	if (fs->fs_magic != FS_MAGIC || fs->fs_bsize > MAXBSIZE ||
	    fs->fs_bsize < sizeof(struct fs)) {
		error = EINVAL;		/* XXX needs translation */
		goto out;
	}
	fs->fs_fmod = 0;
	fs->fs_flags &= ~FS_UNCLEAN;
	if (fs->fs_clean == 0) {
		fs->fs_flags |= FS_UNCLEAN;
		if (ronly || (mp->mnt_flag & MNT_FORCE)) {
			kprintf(
"WARNING: %s was not properly dismounted\n",
			    fs->fs_fsmnt);
		} else {
			kprintf(
"WARNING: R/W mount of %s denied.  Filesystem is not clean - run fsck\n",
			    fs->fs_fsmnt);
			error = EPERM;
			goto out;
		}
	}
	/* XXX updating 4.2 FFS superblocks trashes rotational layout tables */
	if (fs->fs_postblformat == FS_42POSTBLFMT && !ronly) {
		error = EROFS;          /* needs translation */
		goto out;
	}
	ump = kmalloc(sizeof *ump, M_UFSMNT, M_WAITOK | M_ZERO);
	ump->um_malloctype = mtype;
	ump->um_i_effnlink_valid = 1;
	ump->um_fs = kmalloc((u_long)fs->fs_sbsize, M_UFSMNT,
	    M_WAITOK);
	bcopy(bp->b_data, ump->um_fs, (uint)fs->fs_sbsize);
	if (fs->fs_sbsize < SBSIZE)
		bp->b_flags |= B_INVAL;
	brelse(bp);
	bp = NULL;
	fs = ump->um_fs;
	fs->fs_ronly = ronly;
	size = fs->fs_cssize;
	blks = howmany(size, fs->fs_fsize);
	if (fs->fs_contigsumsize > 0)
		size += fs->fs_ncg * sizeof(int32_t);
	size += fs->fs_ncg * sizeof(uint8_t);
	space = kmalloc((u_long)size, M_UFSMNT, M_WAITOK);
	fs->fs_csp = space;
	for (i = 0; i < blks; i += fs->fs_frag) {
		size = fs->fs_bsize;
		if (i + fs->fs_frag > blks)
			size = (blks - i) * fs->fs_fsize;
		if ((error = bread(devvp, fsbtodoff(fs, fs->fs_csaddr + i),
				   size, &bp)) != 0) {
			kfree(fs->fs_csp, M_UFSMNT);
			goto out;
		}
		bcopy(bp->b_data, space, (uint)size);
		space = (char *)space + size;
		brelse(bp);
		bp = NULL;
	}
	if (fs->fs_contigsumsize > 0) {
		fs->fs_maxcluster = lp = space;
		for (i = 0; i < fs->fs_ncg; i++)
			*lp++ = fs->fs_contigsumsize;
		space = lp;
	}
	size = fs->fs_ncg * sizeof(uint8_t);
	fs->fs_contigdirs = (uint8_t *)space;
	bzero(fs->fs_contigdirs, size);
	/* Compatibility for old filesystems 	   XXX */
	if (fs->fs_avgfilesize <= 0)		/* XXX */
		fs->fs_avgfilesize = AVFILESIZ;	/* XXX */
	if (fs->fs_avgfpdir <= 0)		/* XXX */
		fs->fs_avgfpdir = AFPDIR;	/* XXX */
	mp->mnt_data = (qaddr_t)ump;
	mp->mnt_stat.f_fsid.val[0] = fs->fs_id[0];
	mp->mnt_stat.f_fsid.val[1] = fs->fs_id[1];
	if (fs->fs_id[0] == 0 || fs->fs_id[1] == 0 || 
	    vfs_getvfs(&mp->mnt_stat.f_fsid)) 
		vfs_getnewfsid(mp);
	mp->mnt_maxsymlinklen = fs->fs_maxsymlinklen;
	mp->mnt_flag |= MNT_LOCAL;
	ump->um_mountp = mp;
	ump->um_dev = dev;
	ump->um_devvp = devvp;
	ump->um_nindir = fs->fs_nindir;
	ump->um_bptrtodb = fs->fs_fsbtodb;
	ump->um_seqinc = fs->fs_frag;
	for (i = 0; i < MAXQUOTAS; i++)
		ump->um_quotas[i] = NULLVP;
	dev->si_mountpoint = mp;
	ffs_oldfscompat(fs);

	/* restore "last mounted on" here */
	bzero(fs->fs_fsmnt, sizeof(fs->fs_fsmnt));
	ksnprintf(fs->fs_fsmnt, sizeof(fs->fs_fsmnt),
		 "%s", mp->mnt_stat.f_mntonname);

	if( mp->mnt_flag & MNT_ROOTFS) {
		/*
		 * Root mount; update timestamp in mount structure.
		 * this will be used by the common root mount code
		 * to update the system clock.
		 */
		mp->mnt_time = fs->fs_time;
	}

	ufs_ihashinit(ump);
	ump->um_savedmaxfilesize = fs->fs_maxfilesize;		/* XXX */
	maxfilesize = (uint64_t)0x40000000 * fs->fs_bsize - 1;	/* XXX */
	/* Enforce limit caused by vm object backing (32 bits vm_pindex_t). */
	if (maxfilesize > (uint64_t)0x80000000u * PAGE_SIZE - 1)
		maxfilesize = (uint64_t)0x80000000u * PAGE_SIZE - 1;
	if (fs->fs_maxfilesize > maxfilesize)			/* XXX */
		fs->fs_maxfilesize = maxfilesize;		/* XXX */
	if (ronly == 0) {
		if ((fs->fs_flags & FS_DOSOFTDEP) &&
		    (error = softdep_mount(devvp, mp, fs)) != 0) {
			kfree(fs->fs_csp, M_UFSMNT);
			goto out;
		}
		fs->fs_fmod = 1;
		fs->fs_clean = 0;
		(void) ffs_sbupdate(ump, MNT_WAIT);
	}
	vfs_add_vnodeops(mp, &ffs_vnode_vops, &mp->mnt_vn_norm_ops);
	vfs_add_vnodeops(mp, &ffs_spec_vops, &mp->mnt_vn_spec_ops);
	vfs_add_vnodeops(mp, &ffs_fifo_vops, &mp->mnt_vn_fifo_ops);

	return (0);
out:
	dev->si_mountpoint = NULL;
	if (bp)
		brelse(bp);
	vn_lock(devvp, LK_EXCLUSIVE | LK_RETRY);
	VOP_CLOSE(devvp, ronly ? FREAD : FREAD|FWRITE, NULL);
	vn_unlock(devvp);
	if (ump) {
		ufs_ihashuninit(ump);
		kfree(ump->um_fs, M_UFSMNT);
		kfree(ump, M_UFSMNT);
		mp->mnt_data = (qaddr_t)0;
	}
	return (error);
}

/*
 * Sanity checks for old filesystems.
 *
 * XXX - goes away some day.
 */
static int
ffs_oldfscompat(struct fs *fs)
{
	fs->fs_npsect = max(fs->fs_npsect, fs->fs_nsect);	/* XXX */
	fs->fs_interleave = max(fs->fs_interleave, 1);		/* XXX */
	if (fs->fs_postblformat == FS_42POSTBLFMT)		/* XXX */
		fs->fs_nrpos = 8;				/* XXX */
	if (fs->fs_inodefmt < FS_44INODEFMT) {			/* XXX */
#if 0
		int i;						/* XXX */
		uint64_t sizepb = fs->fs_bsize;		/* XXX */
								/* XXX */
		fs->fs_maxfilesize = fs->fs_bsize * NDADDR - 1;	/* XXX */
		for (i = 0; i < NIADDR; i++) {			/* XXX */
			sizepb *= NINDIR(fs);			/* XXX */
			fs->fs_maxfilesize += sizepb;		/* XXX */
		}						/* XXX */
#endif
		fs->fs_maxfilesize = (u_quad_t) 1LL << 39;
		fs->fs_qbmask = ~fs->fs_bmask;			/* XXX */
		fs->fs_qfmask = ~fs->fs_fmask;			/* XXX */
	}							/* XXX */
	return (0);
}

/*
 * unmount system call
 */
int
ffs_unmount(struct mount *mp, int mntflags)
{
	struct ufsmount *ump;
	struct fs *fs;
	int error, flags;

	flags = 0;
	if (mntflags & MNT_FORCE) {
		flags |= FORCECLOSE;
	}
	if (mp->mnt_flag & MNT_SOFTDEP) {
		if ((error = softdep_flushfiles(mp, flags)) != 0)
			return (error);
	} else {
		if ((error = ffs_flushfiles(mp, flags)) != 0)
			return (error);
	}
	ump = VFSTOUFS(mp);
	fs = ump->um_fs;
	if (fs->fs_ronly == 0) {
		fs->fs_clean = fs->fs_flags & FS_UNCLEAN ? 0 : 1;
		error = ffs_sbupdate(ump, MNT_WAIT);
		if (error) {
			fs->fs_clean = 0;
			return (error);
		}
	}
	ump->um_devvp->v_rdev->si_mountpoint = NULL;

	vn_lock(ump->um_devvp, LK_EXCLUSIVE | LK_RETRY);
	vinvalbuf(ump->um_devvp, V_SAVE, 0, 0);
	error = VOP_CLOSE(ump->um_devvp, 
			fs->fs_ronly ? FREAD : FREAD|FWRITE,
			NULL);
	vn_unlock(ump->um_devvp);

	vrele(ump->um_devvp);

	ufs_ihashuninit(ump);
	kfree(fs->fs_csp, M_UFSMNT);
	kfree(fs, M_UFSMNT);
	kfree(ump, M_UFSMNT);
	mp->mnt_data = (qaddr_t)0;
	mp->mnt_flag &= ~MNT_LOCAL;
	return (error);
}

/*
 * Flush out all the files in a filesystem.
 */
int
ffs_flushfiles(struct mount *mp, int flags)
{
	struct ufsmount *ump;
	int error;

	ump = VFSTOUFS(mp);
#ifdef QUOTA
	if (mp->mnt_flag & MNT_QUOTA) {
		int i;
		error = vflush(mp, 0, SKIPSYSTEM|flags);
		if (error)
			return (error);
		/* Find out how many quota files  we have open. */
		for (i = 0; i < MAXQUOTAS; i++) {
			if (ump->um_quotas[i] == NULLVP)
				continue;
			ufs_quotaoff(mp, i);
		}
		/*
		 * Here we fall through to vflush again to ensure
		 * that we have gotten rid of all the system vnodes.
		 */
	}
#endif
        /*
	 * Flush all the files.
	 */
	if ((error = vflush(mp, 0, flags)) != 0)
		return (error);
	/*
	 * Flush filesystem metadata.
	 */
	vn_lock(ump->um_devvp, LK_EXCLUSIVE | LK_RETRY);
	error = VOP_FSYNC(ump->um_devvp, MNT_WAIT, 0);
	vn_unlock(ump->um_devvp);
	return (error);
}

/*
 * Get filesystem statistics.
 */
int
ffs_statfs(struct mount *mp, struct statfs *sbp, struct ucred *cred)
{
	struct ufsmount *ump;
	struct fs *fs;

	ump = VFSTOUFS(mp);
	fs = ump->um_fs;
	if (fs->fs_magic != FS_MAGIC)
		panic("ffs_statfs");
	sbp->f_bsize = fs->fs_fsize;
	sbp->f_iosize = fs->fs_bsize;
	sbp->f_blocks = fs->fs_dsize;
	sbp->f_bfree = fs->fs_cstotal.cs_nbfree * fs->fs_frag +
		fs->fs_cstotal.cs_nffree;
	sbp->f_bavail = freespace(fs, fs->fs_minfree);
	sbp->f_files =  fs->fs_ncg * fs->fs_ipg - ROOTINO;
	sbp->f_ffree = fs->fs_cstotal.cs_nifree;
	if (sbp != &mp->mnt_stat) {
		sbp->f_type = mp->mnt_vfc->vfc_typenum;
		bcopy((caddr_t)mp->mnt_stat.f_mntfromname,
			(caddr_t)&sbp->f_mntfromname[0], MNAMELEN);
	}
	return (0);
}

/*
 * Go through the disk queues to initiate sandbagged IO;
 * go through the inodes to write those that have been modified;
 * initiate the writing of the super block if it has been modified.
 *
 * Note: we are always called with the filesystem marked `MPBUSY'.
 */


static int ffs_sync_scan1(struct mount *mp, struct vnode *vp, void *data);
static int ffs_sync_scan2(struct mount *mp, struct vnode *vp, void *data);

int
ffs_sync(struct mount *mp, int waitfor)
{
	struct ufsmount *ump = VFSTOUFS(mp);
	struct fs *fs;
	int error;
	struct scaninfo scaninfo;

	fs = ump->um_fs;
	if (fs->fs_fmod != 0 && fs->fs_ronly != 0) {		/* XXX */
		kprintf("fs = %s\n", fs->fs_fsmnt);
		panic("ffs_sync: rofs mod");
	}

	/*
	 * Write back each (modified) inode.
	 */
	scaninfo.allerror = 0;
	scaninfo.rescan = 1;
	scaninfo.waitfor = waitfor;
	while (scaninfo.rescan) {
		scaninfo.rescan = 0;
		vmntvnodescan(mp, VMSC_GETVP|VMSC_NOWAIT,
				ffs_sync_scan1, ffs_sync_scan2, &scaninfo);
	}

	/*
	 * Force stale filesystem control information to be flushed.
	 */
	if ((waitfor & MNT_LAZY) == 0) {
		if (ump->um_mountp->mnt_flag & MNT_SOFTDEP)
			waitfor = MNT_NOWAIT;
		vn_lock(ump->um_devvp, LK_EXCLUSIVE | LK_RETRY);
		if ((error = VOP_FSYNC(ump->um_devvp, waitfor, 0)) != 0)
			scaninfo.allerror = error;
		vn_unlock(ump->um_devvp);
	}
#ifdef QUOTA
	ufs_qsync(mp);
#endif
	/*
	 * Write back modified superblock.
	 */
	if (fs->fs_fmod != 0 && (error = ffs_sbupdate(ump, waitfor)) != 0)
		scaninfo.allerror = error;
	return (scaninfo.allerror);
}

static int
ffs_sync_scan1(struct mount *mp, struct vnode *vp, void *data)
{
	struct inode *ip;

	/*
	 * Depend on the mount list's vnode lock to keep things stable 
	 * enough for a quick test.  Since there might be hundreds of 
	 * thousands of vnodes, we cannot afford even a subroutine
	 * call unless there's a good chance that we have work to do.
	 */
	ip = VTOI(vp);
	/* Restart out whole search if this guy is locked
	 * or is being reclaimed.
	 */
	if (vp->v_type == VNON || (ip == NULL) || ((ip->i_flag &
	     (IN_ACCESS | IN_CHANGE | IN_MODIFIED | IN_UPDATE)) == 0 &&
	     RB_EMPTY(&vp->v_rbdirty_tree))) {
		return(-1);
	}
	return(0);
}

static int 
ffs_sync_scan2(struct mount *mp, struct vnode *vp, void *data)
{
	struct scaninfo *info = data;
	struct inode *ip;
	int error;

	/*
	 * We have to recheck after having obtained the vnode interlock.
	 */
	ip = VTOI(vp);
	if (vp->v_type == VNON || vp->v_type == VBAD ||
	     ((ip->i_flag &
	      (IN_ACCESS | IN_CHANGE | IN_MODIFIED | IN_UPDATE)) == 0 &&
	     RB_EMPTY(&vp->v_rbdirty_tree))) {
		return(0);
	}
	if (vp->v_type != VCHR) {
		if ((error = VOP_FSYNC(vp, info->waitfor, 0)) != 0)
			info->allerror = error;
	} else {
		/*
		 * We must reference the vp to prevent it from
		 * getting ripped out from under ffs_update, since
		 * we are not holding a vnode lock.
		 */
		/* ffs_update(vp, waitfor == MNT_WAIT); */
		ffs_update(vp, 0);
	}
	return(0);
}

/*
 * Look up a FFS dinode number to find its incore vnode, otherwise read it
 * in from disk.  If it is in core, wait for the lock bit to clear, then
 * return the inode locked.  Detection and handling of mount points must be
 * done by the calling routine.
 */

int
ffs_vget(struct mount *mp, struct vnode *dvp, ino_t ino, struct vnode **vpp)
{
	struct fs *fs;
	struct inode *ip;
	struct ufsmount *ump;
	struct buf *bp;
	struct vnode *vp;
	cdev_t dev;
	int error;

	ump = VFSTOUFS(mp);
	dev = ump->um_dev;

retry:
	if ((*vpp = ufs_ihashget(ump, dev, ino)) != NULL) {
		return (0);
	}

	/*
	 * If this MALLOC() is performed after the getnewvnode()
	 * it might block, leaving a vnode with a NULL v_data to be
	 * found by ffs_sync() if a sync happens to fire right then,
	 * which will cause a panic because ffs_sync() blindly
	 * dereferences vp->v_data (as well it should).
	 *
	 * XXX this may no longer be true since getnewvnode returns a
	 * VX locked vnode now.
	 */
	ip = kmalloc(sizeof(struct inode), ump->um_malloctype,
		     M_WAITOK | M_ZERO);

	/* Allocate a new vnode/inode. */
	error = getnewvnode(VT_UFS, mp, &vp, VLKTIMEOUT, LK_CANRECURSE);
	if (error) {
		*vpp = NULL;
		kfree(ip, ump->um_malloctype);
		return (error);
	}
	ip->i_vnode = vp;
	ip->i_fs = fs = ump->um_fs;
	ip->i_dev = dev;
	ip->i_number = ino;
#ifdef QUOTA
	{
		int i;
		for (i = 0; i < MAXQUOTAS; i++)
			ip->i_dquot[i] = NODQUOT;
	}
#endif

	/*
	 * Insert it into the inode hash table.
	 */
	if (ufs_ihashins(ump, ip) != 0) {
		kprintf("debug: ufs ihashins collision, retrying inode %ld\n",
			(long) ip->i_number);
		*vpp = NULL;
		vp->v_type = VBAD;
		vx_put(vp);
		kfree(ip, ump->um_malloctype);
		goto retry;
	}
	vp->v_data = ip;

	/* Read in the disk contents for the inode, copy into the inode. */
	error = bread(ump->um_devvp, fsbtodoff(fs, ino_to_fsba(fs, ino)),
	    (int)fs->fs_bsize, &bp);
	if (error) {
		/*
		 * The inode does not contain anything useful, so it would
		 * be misleading to leave it on its hash chain. With mode
		 * still zero, it will be unlinked and returned to the free
		 * list by vput().
		 */
		vp->v_type = VBAD;
		brelse(bp);
		vx_put(vp);
		*vpp = NULL;
		return (error);
	}
	ip->i_din = *((struct ufs1_dinode *)bp->b_data + ino_to_fsbo(fs, ino));
	if (DOINGSOFTDEP(vp))
		softdep_load_inodeblock(ip);
	else
		ip->i_effnlink = ip->i_nlink;
	bqrelse(bp);

	/*
	 * Initialize the vnode from the inode, check for aliases.
	 * Note that the underlying vnode may have changed.
	 */
	error = ufs_vinit(mp, &vp);
	if (error) {
		vp->v_type = VBAD;
		vx_put(vp);
		*vpp = NULL;
		return (error);
	}
	/*
	 * Finish inode initialization now that aliasing has been resolved.
	 */
	ip->i_devvp = ump->um_devvp;
	vref(ip->i_devvp);
	/*
	 * Set up a generation number for this inode if it does not
	 * already have one. This should only happen on old filesystems.
	 */
	if (ip->i_gen == 0) {
		ip->i_gen = krandom() / 2 + 1;
		if ((vp->v_mount->mnt_flag & MNT_RDONLY) == 0)
			ip->i_flag |= IN_MODIFIED;
	}
	/*
	 * Ensure that uid and gid are correct. This is a temporary
	 * fix until fsck has been changed to do the update.
	 */
	if (fs->fs_inodefmt < FS_44INODEFMT) {		/* XXX */
		ip->i_uid = ip->i_din.di_ouid;		/* XXX */
		ip->i_gid = ip->i_din.di_ogid;		/* XXX */
	}						/* XXX */

	/* 
	 * return a VX locked and refd vnode (VX == same as normal vget()
	 * vnode so we are ok)
	 */
	*vpp = vp;
	return (0);
}

/*
 * File handle to vnode
 *
 * Have to be really careful about stale file handles:
 * - check that the inode number is valid
 * - call ffs_vget() to get the locked inode
 * - check for an unallocated inode (i_mode == 0)
 * - check that the given client host has export rights and return
 *   those rights via. exflagsp and credanonp
 */
int
ffs_fhtovp(struct mount *mp, struct vnode *rootvp,
	   struct fid *fhp, struct vnode **vpp)
{
	struct ufid *ufhp;
	struct fs *fs;

	ufhp = (struct ufid *)fhp;
	fs = VFSTOUFS(mp)->um_fs;
	if (ufhp->ufid_ino < ROOTINO ||
	    ufhp->ufid_ino >= fs->fs_ncg * fs->fs_ipg)
		return (ESTALE);
	return (ufs_fhtovp(mp, rootvp, ufhp, vpp));
}

/*
 * Vnode pointer to File handle
 */
/* ARGSUSED */
int
ffs_vptofh(struct vnode *vp, struct fid *fhp)
{
	struct inode *ip;
	struct ufid *ufhp;

	ip = VTOI(vp);
	ufhp = (struct ufid *)fhp;
	ufhp->ufid_len = sizeof(struct ufid);
	ufhp->ufid_ino = ip->i_number;
	ufhp->ufid_gen = ip->i_gen;
	return (0);
}

/*
 * Initialize the filesystem; just use ufs_init.
 */
static int
ffs_init(struct vfsconf *vfsp)
{
	softdep_initialize();
	kmalloc_raise_limit(M_FFSNODE, 0);
	return (ufs_init(vfsp));
}

/*
 * Write a superblock and associated information back to disk.
 */
static int
ffs_sbupdate(struct ufsmount *mp, int waitfor)
{
	struct fs *dfs, *fs = mp->um_fs;
	struct buf *bp;
	int blks;
	void *space;
	int i, size, error, allerror = 0;

	/*
	 * First write back the summary information.
	 *
	 * NOTE: the getblk is relative to the device vnode so bio1
	 * contains the device block number.
	 */
	blks = howmany(fs->fs_cssize, fs->fs_fsize);
	space = fs->fs_csp;
	for (i = 0; i < blks; i += fs->fs_frag) {
		size = fs->fs_bsize;
		if (i + fs->fs_frag > blks)
			size = (blks - i) * fs->fs_fsize;
		bp = getblk(mp->um_devvp, fsbtodoff(fs, fs->fs_csaddr + i),
			    size, 0, 0);
		bcopy(space, bp->b_data, (uint)size);
		space = (char *)space + size;
		if (waitfor != MNT_WAIT)
			bawrite(bp);
		else if ((error = bwrite(bp)) != 0)
			allerror = error;
	}
	/*
	 * Now write back the superblock itself. If any errors occurred
	 * up to this point, then fail so that the superblock avoids
	 * being written out as clean.
	 */
	if (allerror)
		return (allerror);
	bp = getblk(mp->um_devvp, SBOFF, (int)fs->fs_sbsize, 0, 0);
	fs->fs_fmod = 0;
	fs->fs_time = time_second;
	bcopy((caddr_t)fs, bp->b_data, (uint)fs->fs_sbsize);
	/* Restore compatibility to old filesystems.		   XXX */
	dfs = (struct fs *)bp->b_data;				/* XXX */
	if (fs->fs_postblformat == FS_42POSTBLFMT)		/* XXX */
		dfs->fs_nrpos = -1;				/* XXX */
	if (fs->fs_inodefmt < FS_44INODEFMT) {			/* XXX */
		int32_t *lp, tmp;				/* XXX */
								/* XXX */
		lp = (int32_t *)&dfs->fs_qbmask;		/* XXX */
		tmp = lp[4];					/* XXX */
		for (i = 4; i > 0; i--)				/* XXX */
			lp[i] = lp[i-1];			/* XXX */
		lp[0] = tmp;					/* XXX */
	}							/* XXX */
	dfs->fs_maxfilesize = mp->um_savedmaxfilesize;		/* XXX */
	if (waitfor != MNT_WAIT)
		bawrite(bp);
	else if ((error = bwrite(bp)) != 0)
		allerror = error;
	return (allerror);
}
