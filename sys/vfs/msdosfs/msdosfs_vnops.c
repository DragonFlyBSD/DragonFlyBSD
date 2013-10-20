/* $FreeBSD: src/sys/msdosfs/msdosfs_vnops.c,v 1.95.2.4 2003/06/13 15:05:47 trhodes Exp $ */
/*	$NetBSD: msdosfs_vnops.c,v 1.68 1998/02/10 14:10:04 mrg Exp $	*/

/*-
 * Copyright (C) 1994, 1995, 1997 Wolfgang Solfrank.
 * Copyright (C) 1994, 1995, 1997 TooLs GmbH.
 * All rights reserved.
 * Original code by Paul Popelka (paulp@uts.amdahl.com) (see below).
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
 *	This product includes software developed by TooLs GmbH.
 * 4. The name of TooLs GmbH may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY TOOLS GMBH ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL TOOLS GMBH BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
/*
 * Written by Paul Popelka (paulp@uts.amdahl.com)
 *
 * You can do anything you want with this software, just don't say you wrote
 * it, and don't remove this notice.
 *
 * This software is provided "as is".
 *
 * The author supplies this software to be publicly redistributed on the
 * understanding that the author is not responsible for the correct
 * functioning of this software in any circumstances and is not liable for
 * any damages caused by this software.
 *
 * October 1992
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/resourcevar.h>	/* defines plimit structure in proc struct */
#include <sys/kernel.h>
#include <sys/stat.h>
#include <sys/buf.h>
#include <sys/proc.h>
#include <sys/priv.h>
#include <sys/namei.h>
#include <sys/mount.h>
#include <sys/unistd.h>
#include <sys/vnode.h>
#include <sys/malloc.h>
#include <sys/dirent.h>
#include <sys/signalvar.h>

#include <vm/vm.h>
#include <vm/vm_extern.h>
#include <vm/vm_zone.h>
#include <vm/vnode_pager.h>

#include <sys/buf2.h>

#include <machine/inttypes.h>

#include "bpb.h"
#include "direntry.h"
#include "denode.h"
#include "msdosfsmount.h"
#include "fat.h"

#define	DOS_FILESIZE_MAX	0xffffffff

/*
 * Prototypes for MSDOSFS vnode operations
 */
static int msdosfs_create (struct vop_old_create_args *);
static int msdosfs_mknod (struct vop_old_mknod_args *);
static int msdosfs_open (struct vop_open_args *);
static int msdosfs_close (struct vop_close_args *);
static int msdosfs_access (struct vop_access_args *);
static int msdosfs_getattr (struct vop_getattr_args *);
static int msdosfs_setattr (struct vop_setattr_args *);
static int msdosfs_read (struct vop_read_args *);
static int msdosfs_write (struct vop_write_args *);
static int msdosfs_fsync (struct vop_fsync_args *);
static int msdosfs_remove (struct vop_old_remove_args *);
static int msdosfs_link (struct vop_old_link_args *);
static int msdosfs_rename (struct vop_old_rename_args *);
static int msdosfs_mkdir (struct vop_old_mkdir_args *);
static int msdosfs_rmdir (struct vop_old_rmdir_args *);
static int msdosfs_symlink (struct vop_old_symlink_args *);
static int msdosfs_readdir (struct vop_readdir_args *);
static int msdosfs_bmap (struct vop_bmap_args *);
static int msdosfs_strategy (struct vop_strategy_args *);
static int msdosfs_print (struct vop_print_args *);
static int msdosfs_pathconf (struct vop_pathconf_args *ap);

/*
 * Some general notes:
 *
 * In the ufs filesystem the inodes, superblocks, and indirect blocks are
 * read/written using the vnode for the filesystem. Blocks that represent
 * the contents of a file are read/written using the vnode for the file
 * (including directories when they are read/written as files). This
 * presents problems for the dos filesystem because data that should be in
 * an inode (if dos had them) resides in the directory itself.  Since we
 * must update directory entries without the benefit of having the vnode
 * for the directory we must use the vnode for the filesystem.  This means
 * that when a directory is actually read/written (via read, write, or
 * readdir, or seek) we must use the vnode for the filesystem instead of
 * the vnode for the directory as would happen in ufs. This is to insure we
 * retreive the correct block from the buffer cache since the hash value is
 * based upon the vnode address and the desired block number.
 */

/*
 * Create a regular file. On entry the directory to contain the file being
 * created is locked.  We must release before we return. 
 *
 * msdosfs_create(struct vnode *a_dvp, struct vnode **a_vpp,
 *		  struct componentname *a_cnp, struct vattr *a_vap)
 */
static int
msdosfs_create(struct vop_old_create_args *ap)
{
	struct componentname *cnp = ap->a_cnp;
	struct denode ndirent;
	struct denode *dep;
	struct denode *pdep = VTODE(ap->a_dvp);
	struct timespec ts;
	int error;

#ifdef MSDOSFS_DEBUG
	kprintf("msdosfs_create(cnp %p, vap %p\n", cnp, ap->a_vap);
#endif

	/*
	 * If this is the root directory and there is no space left we
	 * can't do anything.  This is because the root directory can not
	 * change size.
	 */
	if (pdep->de_StartCluster == MSDOSFSROOT
	    && pdep->de_fndoffset >= pdep->de_FileSize) {
		error = ENOSPC;
		goto bad;
	}

	/*
	 * Create a directory entry for the file, then call createde() to
	 * have it installed. NOTE: DOS files are always executable.  We
	 * use the absence of the owner write bit to make the file
	 * readonly.
	 */
	bzero(&ndirent, sizeof(ndirent));
	error = uniqdosname(pdep, cnp, ndirent.de_Name);
	if (error)
		goto bad;

	ndirent.de_Attributes = (ap->a_vap->va_mode & VWRITE) ?
				ATTR_ARCHIVE : ATTR_ARCHIVE | ATTR_READONLY;
	ndirent.de_LowerCase = 0;
	ndirent.de_StartCluster = 0;
	ndirent.de_FileSize = 0;
	ndirent.de_dev = pdep->de_dev;
	ndirent.de_devvp = pdep->de_devvp;
	ndirent.de_pmp = pdep->de_pmp;
	ndirent.de_flag = DE_ACCESS | DE_CREATE | DE_UPDATE;
	getnanotime(&ts);
	DETIMES(&ndirent, &ts, &ts, &ts);
	error = createde(&ndirent, pdep, &dep, cnp);
	if (error)
		goto bad;
	*ap->a_vpp = DETOV(dep);
	return (0);

bad:
	return (error);
}

/*
 * msdosfs_mknod(struct vnode *a_dvp, struct vnode **a_vpp,
 *		 struct componentname *a_cnp, struct vattr *a_vap)
 */
static int
msdosfs_mknod(struct vop_old_mknod_args *ap)
{
	switch (ap->a_vap->va_type) {
	case VDIR:
		return (msdosfs_mkdir((struct vop_old_mkdir_args *)ap));
		break;

	case VREG:
		return (msdosfs_create((struct vop_old_create_args *)ap));
		break;

	default:
		return (EINVAL);
	}
	/* NOTREACHED */
}

/*
 * msdosfs_open(struct vnode *a_vp, int a_mode, struct ucred *a_cred,
 *		struct file *a_fp)
 */
static int
msdosfs_open(struct vop_open_args *ap)
{
	return(vop_stdopen(ap));
}

/*
 * msdosfs_close(struct vnode *a_vp, int a_fflag)
 */
static int
msdosfs_close(struct vop_close_args *ap)
{
	struct vnode *vp = ap->a_vp;
	struct denode *dep = VTODE(vp);
	struct timespec ts;

	if (VREFCNT(vp) > 1) {
		getnanotime(&ts);
		DETIMES(dep, &ts, &ts, &ts);
	}
	return (vop_stdclose(ap));
}

/*
 * msdosfs_access(struct vnode *a_vp, int a_mode, struct ucred *a_cred)
 */
static int
msdosfs_access(struct vop_access_args *ap)
{
	struct denode *dep = VTODE(ap->a_vp);
	struct msdosfsmount *pmp = dep->de_pmp;
	mode_t file_mode;

	file_mode = (S_IXUSR|S_IXGRP|S_IXOTH) | (S_IRUSR|S_IRGRP|S_IROTH) |
	    ((dep->de_Attributes & ATTR_READONLY) ? 
	     	0 : (S_IWUSR|S_IWGRP|S_IWOTH));
	file_mode &= pmp->pm_mask;

	return (vop_helper_access(ap, pmp->pm_uid, pmp->pm_gid, file_mode, 0));
}

/*
 * msdosfs_getattr(struct vnode *a_vp, struct vattr *a_vap)
 */
static int
msdosfs_getattr(struct vop_getattr_args *ap)
{
	struct denode *dep = VTODE(ap->a_vp);
	struct msdosfsmount *pmp = dep->de_pmp;
	struct vattr *vap = ap->a_vap;
	mode_t mode;
	struct timespec ts;
	u_long dirsperblk = pmp->pm_BytesPerSec / sizeof(struct direntry);
	u_long fileid;

	getnanotime(&ts);
	DETIMES(dep, &ts, &ts, &ts);
	vap->va_fsid = dev2udev(dep->de_dev);
	/*
	 * The following computation of the fileid must be the same as that
	 * used in msdosfs_readdir() to compute d_fileno. If not, pwd
	 * doesn't work.
	 */
	if (dep->de_Attributes & ATTR_DIRECTORY) {
		fileid = xcntobn(pmp, dep->de_StartCluster) * dirsperblk;
		if (dep->de_StartCluster == MSDOSFSROOT)
			fileid = 1;
	} else {
		fileid = xcntobn(pmp, dep->de_dirclust) * dirsperblk;
		if (dep->de_dirclust == MSDOSFSROOT)
			fileid = roottobn(pmp, 0) * dirsperblk;
		fileid += dep->de_diroffset / sizeof(struct direntry);
	}
	vap->va_fileid = fileid;
	if ((dep->de_Attributes & ATTR_READONLY) == 0)
		mode = S_IRWXU|S_IRWXG|S_IRWXO;
	else
		mode = S_IRUSR|S_IXUSR|S_IRGRP|S_IXGRP|S_IROTH|S_IXOTH;
	vap->va_mode = mode & pmp->pm_mask;
	vap->va_uid = pmp->pm_uid;
	vap->va_gid = pmp->pm_gid;
	vap->va_nlink = 1;
	vap->va_rmajor = VNOVAL;
	vap->va_rminor = VNOVAL;
	vap->va_size = dep->de_FileSize;
	dos2unixtime(dep->de_MDate, dep->de_MTime, 0, &vap->va_mtime);
	if (pmp->pm_flags & MSDOSFSMNT_LONGNAME) {
		dos2unixtime(dep->de_ADate, 0, 0, &vap->va_atime);
		dos2unixtime(dep->de_CDate, dep->de_CTime, dep->de_CHun, &vap->va_ctime);
	} else {
		vap->va_atime = vap->va_mtime;
		vap->va_ctime = vap->va_mtime;
	}
	vap->va_flags = 0;
	if ((dep->de_Attributes & ATTR_ARCHIVE) == 0)
		vap->va_flags |= SF_ARCHIVED;
	vap->va_gen = 0;
	vap->va_blocksize = pmp->pm_bpcluster;
	vap->va_bytes =
	    (dep->de_FileSize + pmp->pm_crbomask) & ~pmp->pm_crbomask;
	vap->va_type = ap->a_vp->v_type;
	vap->va_filerev = dep->de_modrev;
	return (0);
}

/*
 * msdosfs_setattr(struct vnode *a_vp, struct vattr *a_vap,
 *		   struct ucred *a_cred)
 */
static int
msdosfs_setattr(struct vop_setattr_args *ap)
{
	struct vnode *vp = ap->a_vp;
	struct denode *dep = VTODE(ap->a_vp);
	struct msdosfsmount *pmp = dep->de_pmp;
	struct vattr *vap = ap->a_vap;
	struct ucred *cred = ap->a_cred;
	int error = 0;

#ifdef MSDOSFS_DEBUG
	kprintf("msdosfs_setattr(): vp %p, vap %p, cred %p\n",
	    ap->a_vp, vap, cred);
#endif

	/*
	 * Check for unsettable attributes.
	 */
	if ((vap->va_type != VNON) || (vap->va_nlink != VNOVAL) ||
	    (vap->va_fsid != VNOVAL) || (vap->va_fileid != VNOVAL) ||
	    (vap->va_blocksize != VNOVAL) || (vap->va_rmajor != VNOVAL) ||
	    (vap->va_bytes != VNOVAL) || (vap->va_gen != VNOVAL)) {
#ifdef MSDOSFS_DEBUG
		kprintf("msdosfs_setattr(): returning EINVAL\n");
		kprintf("    va_type %u, va_nlink %"PRIx64", va_fsid %x, va_fileid %"PRIx64"\n",
		    vap->va_type, vap->va_nlink, vap->va_fsid, vap->va_fileid);
		kprintf("    va_blocksize %lx, va_rmajor %x, va_bytes %"PRIx64", va_gen %"PRIx64"\n",
		    vap->va_blocksize, vap->va_rmajor, vap->va_bytes, vap->va_gen);
		kprintf("    va_uid %x, va_gid %x\n",
		    vap->va_uid, vap->va_gid);
#endif
		return (EINVAL);
	}
	if (vap->va_flags != VNOVAL) {
		if (vp->v_mount->mnt_flag & MNT_RDONLY)
			return (EROFS);
		if (cred->cr_uid != pmp->pm_uid &&
		    (error = priv_check_cred(cred, PRIV_VFS_SETATTR, 0)))
			return (error);
		/*
		 * We are very inconsistent about handling unsupported
		 * attributes.  We ignored the access time and the
		 * read and execute bits.  We were strict for the other
		 * attributes.
		 *
		 * Here we are strict, stricter than ufs in not allowing
		 * users to attempt to set SF_SETTABLE bits or anyone to
		 * set unsupported bits.  However, we ignore attempts to
		 * set ATTR_ARCHIVE for directories `cp -pr' from a more
		 * sensible file system attempts it a lot.
		 */
		if (cred->cr_uid != 0) {
			if (vap->va_flags & SF_SETTABLE)
				return EPERM;
		}
		if (vap->va_flags & ~SF_ARCHIVED)
			return EOPNOTSUPP;
		if (vap->va_flags & SF_ARCHIVED)
			dep->de_Attributes &= ~ATTR_ARCHIVE;
		else if (!(dep->de_Attributes & ATTR_DIRECTORY))
			dep->de_Attributes |= ATTR_ARCHIVE;
		dep->de_flag |= DE_MODIFIED;
	}

	if (vap->va_uid != (uid_t)VNOVAL || vap->va_gid != (gid_t)VNOVAL) {
		uid_t uid;
		gid_t gid;
		
		if (vp->v_mount->mnt_flag & MNT_RDONLY)
			return (EROFS);
		uid = vap->va_uid;
		if (uid == (uid_t)VNOVAL)
			uid = pmp->pm_uid;
		gid = vap->va_gid;
		if (gid == (gid_t)VNOVAL)
			gid = pmp->pm_gid;
		if ((cred->cr_uid != pmp->pm_uid || uid != pmp->pm_uid ||
		    (gid != pmp->pm_gid && !groupmember(gid, cred))) &&
		    (error = priv_check_cred(cred, PRIV_VFS_SETATTR, 0)))
			return error;
		if (uid != pmp->pm_uid || gid != pmp->pm_gid)
			return EINVAL;
	}

	if (vap->va_size != VNOVAL) {
		/*
		 * Disallow write attempts on read-only file systems;
		 * unless the file is a socket, fifo, or a block or
		 * character device resident on the file system.
		 */
		switch (vp->v_type) {
		case VDIR:
			return (EISDIR);
			/* NOT REACHED */
		case VLNK:
		case VREG:
			if (vp->v_mount->mnt_flag & MNT_RDONLY)
				return (EROFS);
			break;
		default:
			break;
		}
		error = detrunc(dep, vap->va_size, 0);
		if (error)
			return error;
	}
	if (vap->va_atime.tv_sec != VNOVAL || vap->va_mtime.tv_sec != VNOVAL) {
		if (vp->v_mount->mnt_flag & MNT_RDONLY)
			return (EROFS);
		if (cred->cr_uid != pmp->pm_uid &&
		    (error = priv_check_cred(cred, PRIV_VFS_SETATTR, 0)) &&
		    ((vap->va_vaflags & VA_UTIMES_NULL) == 0 ||
		    (error = VOP_EACCESS(ap->a_vp, VWRITE, cred))))
			return (error);
		if (vp->v_type != VDIR) {
			if ((pmp->pm_flags & MSDOSFSMNT_NOWIN95) == 0 &&
			    vap->va_atime.tv_sec != VNOVAL) {
				dep->de_flag &= ~DE_ACCESS;
				unix2dostime(&vap->va_atime, &dep->de_ADate,
				    NULL, NULL);
			}
			if (vap->va_mtime.tv_sec != VNOVAL) {
				dep->de_flag &= ~DE_UPDATE;
				unix2dostime(&vap->va_mtime, &dep->de_MDate,
				    &dep->de_MTime, NULL);
			}
			dep->de_Attributes |= ATTR_ARCHIVE;
			dep->de_flag |= DE_MODIFIED;
		}
	}
	/*
	 * DOS files only have the ability to have their writability
	 * attribute set, so we use the owner write bit to set the readonly
	 * attribute.
	 */
	if (vap->va_mode != (mode_t)VNOVAL) {
		if (vp->v_mount->mnt_flag & MNT_RDONLY)
			return (EROFS);
		if (cred->cr_uid != pmp->pm_uid &&
		    (error = priv_check_cred(cred, PRIV_VFS_SETATTR, 0)))
			return (error);
		if (vp->v_type != VDIR) {
			/* We ignore the read and execute bits. */
			if (vap->va_mode & VWRITE)
				dep->de_Attributes &= ~ATTR_READONLY;
			else
				dep->de_Attributes |= ATTR_READONLY;
			dep->de_Attributes |= ATTR_ARCHIVE;
			dep->de_flag |= DE_MODIFIED;
		}
	}
	return (deupdat(dep, 1));
}

/*
 * msdosfs_read(struct vnode *a_vp, struct uio *a_uio, int a_ioflag,
 *		struct ucred *a_cred)
 */
static int
msdosfs_read(struct vop_read_args *ap)
{
	int error = 0;
	int blsize;
	int isadir;
	size_t orig_resid;
	u_int n;
	u_long diff;
	u_long on;
	daddr_t lbn;
	daddr_t rablock;
	off_t raoffset;
	off_t loffset;
	int rasize;
	int seqcount;
	struct buf *bp;
	struct vnode *vp = ap->a_vp;
	struct denode *dep = VTODE(vp);
	struct msdosfsmount *pmp = dep->de_pmp;
	struct uio *uio = ap->a_uio;

	if (uio->uio_offset < 0)
		return (EINVAL);

	if ((uoff_t)uio->uio_offset > DOS_FILESIZE_MAX)
                return (0);
	/*
	 * If they didn't ask for any data, then we are done.
	 */
	orig_resid = uio->uio_resid;
	if (orig_resid == 0)
		return (0);

	seqcount = ap->a_ioflag >> IO_SEQSHIFT;

	isadir = dep->de_Attributes & ATTR_DIRECTORY;
	do {
		if (uio->uio_offset >= dep->de_FileSize)
			break;

		/*
		 * note: lbn is a cluster number, not a device block number.
		 */
		lbn = de_off2cn(pmp, uio->uio_offset);
		loffset = de_cn2doff(pmp, lbn);

		/*
		 * If we are operating on a directory file then be sure to
		 * do i/o with the vnode for the filesystem instead of the
		 * vnode for the directory.
		 */
		if (isadir) {
			/*
			 * convert cluster # to block #.  lbn is a
			 * device block number after this.
			 */
			error = pcbmap(dep, lbn, &lbn, NULL, &blsize);
			loffset = de_bntodoff(pmp, lbn);
			if (error == E2BIG) {
				error = EINVAL;
				break;
			} else if (error)
				break;
			error = bread(pmp->pm_devvp, loffset, blsize, &bp);
		} else {
			blsize = pmp->pm_bpcluster;
			rablock = lbn + 1;
			raoffset = de_cn2doff(pmp, rablock);
			if (seqcount > 1 &&
			    raoffset < dep->de_FileSize) {
				rasize = pmp->pm_bpcluster;
				error = breadn(vp, loffset, blsize,
						&raoffset, &rasize, 1, &bp); 
			} else {
				error = bread(vp, loffset, blsize, &bp);
			}
		}
		if (error) {
			brelse(bp);
			break;
		}
		on = uio->uio_offset & pmp->pm_crbomask;
		diff = pmp->pm_bpcluster - on;
		n = szmin(uio->uio_resid, diff);
		diff = dep->de_FileSize - uio->uio_offset;
		if (diff < n)
			n = diff;
		diff = blsize - bp->b_resid;
		if (diff < n)
			n = diff;
		error = uiomovebp(bp, bp->b_data + on, (size_t)n, uio);
		brelse(bp);
	} while (error == 0 && uio->uio_resid > 0 && n != 0);
	if (!isadir && (error == 0 || uio->uio_resid != orig_resid) &&
	    (vp->v_mount->mnt_flag & MNT_NOATIME) == 0)
		dep->de_flag |= DE_ACCESS;
	return (error);
}

/*
 * Write data to a file or directory.
 *
 * msdosfs_write(struct vnode *a_vp, struct uio *a_uio, int a_ioflag,
 *		 struct ucred *a_cred)
 */
static int
msdosfs_write(struct vop_write_args *ap)
{
	int n;
	int croffset;
	size_t resid;
	u_long osize;
	int error = 0;
	u_long count;
	daddr_t cn, lastcn;
	struct buf *bp;
	int ioflag = ap->a_ioflag;
	struct uio *uio = ap->a_uio;
	struct thread *td = uio->uio_td;
	struct vnode *vp = ap->a_vp;
	struct vnode *thisvp;
	struct denode *dep = VTODE(vp);
	struct msdosfsmount *pmp = dep->de_pmp;
	struct proc *p = (td ? td->td_proc : NULL);
	struct lwp *lp = (td ? td->td_lwp : NULL);

#ifdef MSDOSFS_DEBUG
	kprintf("msdosfs_write(vp %p, uio %p, ioflag %x, cred %p\n",
	    vp, uio, ioflag, ap->a_cred);
	kprintf("msdosfs_write(): diroff %lu, dirclust %lu, startcluster %lu\n",
	    dep->de_diroffset, dep->de_dirclust, dep->de_StartCluster);
#endif

	switch (vp->v_type) {
	case VREG:
		if (ioflag & IO_APPEND)
			uio->uio_offset = dep->de_FileSize;
		thisvp = vp;
		break;
	case VDIR:
		return EISDIR;
	default:
		panic("msdosfs_write(): bad file type");
	}

	if (uio->uio_offset < 0)
		return (EFBIG);

	if (uio->uio_resid == 0)
		return (0);

	/*
	 * If they've exceeded their filesize limit, tell them about it.
	 */
	if (p &&
	    ((uoff_t)uio->uio_offset + uio->uio_resid >
	    p->p_rlimit[RLIMIT_FSIZE].rlim_cur)) {
		lwpsignal(p, lp, SIGXFSZ);
		return (EFBIG);
	}

	if ((uoff_t)uio->uio_offset > DOS_FILESIZE_MAX)
                return (EFBIG);
	if ((uoff_t)uio->uio_offset + uio->uio_resid > DOS_FILESIZE_MAX)
                return (EFBIG);

	/*
	 * If the offset we are starting the write at is beyond the end of
	 * the file, then they've done a seek.  Unix filesystems allow
	 * files with holes in them, DOS doesn't so we must fill the hole
	 * with zeroed blocks.
	 */
	if (uio->uio_offset > dep->de_FileSize) {
		error = deextend(dep, uio->uio_offset);
		if (error)
			return (error);
	}

	/*
	 * Remember some values in case the write fails.
	 */
	resid = uio->uio_resid;
	osize = dep->de_FileSize;

	/*
	 * If we write beyond the end of the file, extend it to its ultimate
	 * size ahead of the time to hopefully get a contiguous area.
	 */
	if (uio->uio_offset + resid > osize) {
		count = de_clcount(pmp, uio->uio_offset + resid) -
			de_clcount(pmp, osize);
		error = extendfile(dep, count, NULL, NULL, 0);
		if (error &&  (error != ENOSPC || (ioflag & IO_UNIT)))
			goto errexit;
		lastcn = dep->de_fc[FC_LASTFC].fc_frcn;
	} else
		lastcn = de_clcount(pmp, osize) - 1;

	do {
		if (de_off2cn(pmp, uio->uio_offset) > lastcn) {
			error = ENOSPC;
			break;
		}

		croffset = uio->uio_offset & pmp->pm_crbomask;
		n = (int)szmin(uio->uio_resid, pmp->pm_bpcluster - croffset);
		if (uio->uio_offset + n > dep->de_FileSize) {
			dep->de_FileSize = uio->uio_offset + n;
			/* The object size needs to be set before buffer is allocated */
			vnode_pager_setsize(vp, dep->de_FileSize);
		}

		/*
		 * If either the whole cluster gets written, or we write
		 * the cluster from its start beyond EOF, then no need to
		 * read data from disk.
		 *
		 * If UIO_NOCOPY is set we have to do a read-before-write
		 * to fill in any missing pieces of the buffer since no
		 * actual overwrite will occur.
		 */
		cn = de_off2cn(pmp, uio->uio_offset);
		if ((uio->uio_offset & pmp->pm_crbomask) == 0
		    && uio->uio_segflg != UIO_NOCOPY
		    && (de_off2cn(pmp, uio->uio_offset + uio->uio_resid) 
		        > de_off2cn(pmp, uio->uio_offset)
			|| uio->uio_offset + uio->uio_resid >= dep->de_FileSize)) {
			bp = getblk(thisvp, de_cn2doff(pmp, cn),
				    pmp->pm_bpcluster, 0, 0);
			clrbuf(bp);
			/*
			 * Do the bmap now, since pcbmap needs buffers
			 * for the fat table. (see msdosfs_strategy)
			 */
			if (bp->b_bio2.bio_offset == NOOFFSET) {
				daddr_t lblkno = de_off2cn(pmp, bp->b_loffset);
				daddr_t dblkno;

				error = pcbmap(dep, lblkno,
					       &dblkno, NULL, NULL);
				if (error || dblkno == (daddr_t)-1) {
					bp->b_bio2.bio_offset = NOOFFSET;
				} else {
					bp->b_bio2.bio_offset = de_bntodoff(pmp, dblkno);
				}
			}
			if (bp->b_bio2.bio_offset == NOOFFSET) {
				brelse(bp);
				if (!error)
					error = EIO;		/* XXX */
				break;
			}
		} else {
			/*
			 * The block we need to write into exists, so read
			 * it in.
			 */
			error = bread(thisvp, de_cn2doff(pmp, cn),
				      pmp->pm_bpcluster, &bp);
			if (error) {
				brelse(bp);
				break;
			}
		}

		/*
		 * Should these vnode_pager_* functions be done on dir
		 * files?
		 */

		/*
		 * Copy the data from user space into the buf header.
		 */
		error = uiomovebp(bp, bp->b_data + croffset, (size_t)n, uio);
		if (error) {
			brelse(bp);
			break;
		}

		/*
		 * If they want this synchronous then write it and wait for
		 * it.  Otherwise, if on a cluster boundary write it
		 * asynchronously so we can move on to the next block
		 * without delay.  Otherwise do a delayed write because we
		 * may want to write somemore into the block later.
		 */
		if (ioflag & IO_SYNC)
			bwrite(bp);
		else if (n + croffset == pmp->pm_bpcluster)
			bawrite(bp);
		else
			bdwrite(bp);
		dep->de_flag |= DE_UPDATE;
	} while (error == 0 && uio->uio_resid > 0);

	/*
	 * If the write failed and they want us to, truncate the file back
	 * to the size it was before the write was attempted.
	 */
errexit:
	if (error) {
		if (ioflag & IO_UNIT) {
			detrunc(dep, osize, ioflag & IO_SYNC);
			uio->uio_offset -= resid - uio->uio_resid;
			uio->uio_resid = resid;
		} else {
			detrunc(dep, dep->de_FileSize, ioflag & IO_SYNC);
			if (uio->uio_resid != resid)
				error = 0;
		}
	} else if (ioflag & IO_SYNC)
		error = deupdat(dep, 1);
	return (error);
}

/*
 * Flush the blocks of a file to disk.
 *
 * This function is worthless for vnodes that represent directories. Maybe we
 * could just do a sync if they try an fsync on a directory file.
 *
 * msdosfs_fsync(struct vnode *a_vp, int a_waitfor)
 */
static int
msdosfs_fsync(struct vop_fsync_args *ap)
{
	struct vnode *vp = ap->a_vp;

	/*
	 * Flush all dirty buffers associated with a vnode.
	 */
#ifdef DIAGNOSTIC
loop:
#endif
	vfsync(vp, ap->a_waitfor, 0, NULL, NULL);
#ifdef DIAGNOSTIC
	if (ap->a_waitfor == MNT_WAIT && !RB_EMPTY(&vp->v_rbdirty_tree)) {
		vprint("msdosfs_fsync: dirty", vp);
		goto loop;
	}
#endif
	return (deupdat(VTODE(vp), ap->a_waitfor == MNT_WAIT));
}

/*
 * msdosfs_remove(struct vnode *a_dvp, struct vnode *a_vp,
 *		  struct componentname *a_cnp)
 */
static int
msdosfs_remove(struct vop_old_remove_args *ap)
{
	struct denode *dep = VTODE(ap->a_vp);
	struct denode *ddep = VTODE(ap->a_dvp);
	int error;

	if (ap->a_vp->v_type == VDIR)
		error = EPERM;
	else
		error = removede(ddep, dep);
#ifdef MSDOSFS_DEBUG
	kprintf("msdosfs_remove(), dep %p, v_refcnt 0x%08x\n",
		dep, ap->a_vp->v_refcnt);
#endif
	return (error);
}

/*
 * DOS filesystems don't know what links are. But since we already called
 * msdosfs_lookup() with create and lockparent, the parent is locked so we
 * have to free it before we return the error.
 *
 * msdosfs_link(struct vnode *a_tdvp, struct vnode *a_vp,
 *		struct componentname *a_cnp)
 */
static int
msdosfs_link(struct vop_old_link_args *ap)
{
	return (EOPNOTSUPP);
}

/*
 * Renames on files require moving the denode to a new hash queue since the
 * denode's location is used to compute which hash queue to put the file
 * in. Unless it is a rename in place.  For example "mv a b".
 *
 * What follows is the basic algorithm:
 *
 * if (file move) {
 *	if (dest file exists) {
 *		remove dest file
 *	}
 *	if (dest and src in same directory) {
 *		rewrite name in existing directory slot
 *	} else {
 *		write new entry in dest directory
 *		update offset and dirclust in denode
 *		move denode to new hash chain
 *		clear old directory entry
 *	}
 * } else {
 *	directory move
 *	if (dest directory exists) {
 *		if (dest is not empty) {
 *			return ENOTEMPTY
 *		}
 *		remove dest directory
 *	}
 *	if (dest and src in same directory) {
 *		rewrite name in existing entry
 *	} else {
 *		be sure dest is not a child of src directory
 *		write entry in dest directory
 *		update "." and ".." in moved directory
 *		clear old directory entry for moved directory
 *	}
 * }
 *
 * On entry:
 *	source's parent directory is unlocked
 *	source file or directory is unlocked
 *	destination's parent directory is locked
 *	destination file or directory is locked if it exists
 *
 * On exit:
 *	all denodes should be released
 *
 * Notes:
 * I'm not sure how the memory containing the pathnames pointed at by the
 * componentname structures is freed, there may be some memory bleeding
 * for each rename done.
 *
 * msdosfs_rename(struct vnode *a_fdvp, struct vnode *a_fvp,
 *		  struct componentname *a_fcnp, struct vnode *a_tdvp,
 *		  struct vnode *a_tvp, struct componentname *a_tcnp)
 */
static int
msdosfs_rename(struct vop_old_rename_args *ap)
{
	struct vnode *tdvp = ap->a_tdvp;
	struct vnode *fvp = ap->a_fvp;
	struct vnode *fdvp = ap->a_fdvp;
	struct vnode *tvp = ap->a_tvp;
	struct componentname *tcnp = ap->a_tcnp;
	struct componentname *fcnp = ap->a_fcnp;
	struct denode *ip, *xp, *dp, *zp;
	u_char toname[11], oldname[11];
	u_long from_diroffset, to_diroffset;
	u_char to_count;
	int doingdirectory = 0, newparent = 0;
	int error;
	u_long cn;
	daddr_t bn;
	struct msdosfsmount *pmp;
	struct direntry *dotdotp;
	struct buf *bp;

	pmp = VFSTOMSDOSFS(fdvp->v_mount);

	/*
	 * Check for cross-device rename.
	 */
	if ((fvp->v_mount != tdvp->v_mount) ||
	    (tvp && (fvp->v_mount != tvp->v_mount))) {
		error = EXDEV;
abortit:
		if (tdvp == tvp)
			vrele(tdvp);
		else
			vput(tdvp);
		if (tvp)
			vput(tvp);
		vrele(fdvp);
		vrele(fvp);
		return (error);
	}

	/*
	 * If source and dest are the same, do nothing.
	 */
	if (tvp == fvp) {
		error = 0;
		goto abortit;
	}

	/*
	 * fvp, fdvp are unlocked, tvp, tdvp are locked.  Lock fvp and note
	 * that we have to unlock it to use the abortit target.
	 */
	error = vn_lock(fvp, LK_EXCLUSIVE);
	if (error)
		goto abortit;
	dp = VTODE(fdvp);
	ip = VTODE(fvp);

	/*
	 * Be sure we are not renaming ".", "..", or an alias of ".". This
	 * leads to a crippled directory tree.  It's pretty tough to do a
	 * "ls" or "pwd" with the "." directory entry missing, and "cd .."
	 * doesn't work if the ".." entry is missing.
	 */
	if (ip->de_Attributes & ATTR_DIRECTORY) {
		/*
		 * Avoid ".", "..", and aliases of "." for obvious reasons.
		 */
		if ((fcnp->cn_namelen == 1 && fcnp->cn_nameptr[0] == '.') ||
		    dp == ip ||
		    (fcnp->cn_flags & CNP_ISDOTDOT) ||
		    (tcnp->cn_flags & CNP_ISDOTDOT) ||
		    (ip->de_flag & DE_RENAME)) {
			vn_unlock(fvp);
			error = EINVAL;
			goto abortit;
		}
		ip->de_flag |= DE_RENAME;
		doingdirectory++;
	}

	/*
	 * fvp locked, fdvp unlocked, tvp, tdvp locked.  DE_RENAME only
	 * set if doingdirectory.  We will get fvp unlocked in fairly
	 * short order.  dp and xp must be setup and fvp must be unlocked
	 * for the out and bad targets to work properly.
	 */
	dp = VTODE(tdvp);
	xp = tvp ? VTODE(tvp) : NULL;

	/*
	 * Remember direntry place to use for destination
	 */
	to_diroffset = dp->de_fndoffset;
	to_count = dp->de_fndcnt;

	/*
	 * If ".." must be changed (ie the directory gets a new
	 * parent) then the source directory must not be in the
	 * directory heirarchy above the target, as this would
	 * orphan everything below the source directory. Also
	 * the user must have write permission in the source so
	 * as to be able to change "..". We must repeat the call
	 * to namei, as the parent directory is unlocked by the
	 * call to doscheckpath().
	 */
	error = VOP_EACCESS(fvp, VWRITE, tcnp->cn_cred);
	vn_unlock(fvp);
	if (VTODE(fdvp)->de_StartCluster != VTODE(tdvp)->de_StartCluster)
		newparent = 1;

	/*
	 * ok. fvp, fdvp unlocked, tvp, tdvp locked. tvp may be NULL.
	 * DE_RENAME only set if doingdirectory.
	 */
	if (doingdirectory && newparent) {
		if (error)	/* write access check above */
			goto bad;
		if (xp != NULL) {
			vput(tvp);
			xp = NULL;
		}
		/*
		 * checkpath vput's tdvp (VTOI(dp)) on return no matter what,
		 * get an extra ref so we wind up with just an unlocked, ref'd
		 * tdvp.  The 'out' target skips tvp and tdvp cleanups (tdvp
		 * isn't locked so we can't use the out target).
		 */
		vref(tdvp);
		error = doscheckpath(ip, dp);
		tcnp->cn_flags |= CNP_PDIRUNLOCK;
		if (error) {
			vrele(tdvp);
			goto out;
		}
		/*
		 * relookup no longer messes with the ref count.  tdvp must
		 * be unlocked on entry and on success will be locked on
		 * return.
		 */
		error = relookup(tdvp, &tvp, tcnp);
		if (error) {
			if (tcnp->cn_flags & CNP_PDIRUNLOCK)
				vrele(tdvp);
			else
				vput(tdvp);
			goto out;
		}

		/*
		 * tvp and tdvp are now locked again.
		 */
		dp = VTODE(tdvp);
		xp = tvp ? VTODE(tvp) : NULL;
	}

	/*
	 * tvp and tdvp are now locked again, the 'bad' target can be used
	 * to clean them up again.  Delete an existant target and clean
	 * up tvp.  Set xp to NULL to indicate that tvp has been cleaned up.
	 */
	if (xp != NULL) {
		/*
		 * Target must be empty if a directory and have no links
		 * to it. Also, ensure source and target are compatible
		 * (both directories, or both not directories).
		 */
		if (xp->de_Attributes & ATTR_DIRECTORY) {
			if (!dosdirempty(xp)) {
				error = ENOTEMPTY;
				goto bad;
			}
			if (!doingdirectory) {
				error = ENOTDIR;
				goto bad;
			}
		} else if (doingdirectory) {
			error = EISDIR;
			goto bad;
		}
		error = removede(dp, xp);
		if (error)
			goto bad;
		vput(tvp);
		xp = NULL;
		tvp = NULL;
	}

	/*
	 * Convert the filename in tcnp into a dos filename. We copy this
	 * into the denode and directory entry for the destination
	 * file/directory.
	 */
	error = uniqdosname(VTODE(tdvp), tcnp, toname);
	if (error)
		goto bad;

	/*
	 * Since from wasn't locked at various places above, we have to do
	 * a relookup here.  If the target and source are the same directory
	 * we have to unlock the target directory in order to safely relookup
	 * the source, because relookup expects its directory to be unlocked.
	 *
	 * Note that ap->a_fvp is still valid and ref'd.  Any cleanup must
	 * now take that into account.
	 *
	 * The tdvp locking issues make this a real mess.
	 */
	fcnp->cn_flags &= ~CNP_MODMASK;
	fcnp->cn_flags |= CNP_LOCKPARENT;
	if (newparent == 0)
		vn_unlock(tdvp);
	error = relookup(fdvp, &fvp, fcnp);
	if (error || fvp == NULL) {
		/*
		 * From name has disappeared.  Note: fdvp might == tdvp.
		 *
		 * DE_RENAME is only set if doingdirectory.
		 */
		if (doingdirectory)
			panic("rename: lost dir entry");
		if (fcnp->cn_flags & CNP_PDIRUNLOCK)
			vrele(fdvp);
		else
			vput(fdvp);
		if (newparent == 0)
			vrele(tdvp);
		else
			vput(tdvp);
		vrele(ap->a_fvp);
		return(0);
	}

	/*
	 * No error occured.  tdvp, fdvp and fvp are all locked.  If 
	 * newparent was 0 be aware that fdvp == tdvp.  tvp has been cleaned
	 * up.  ap->a_fvp is still refd.
	 */
	xp = VTODE(fvp);
	zp = VTODE(fdvp);
	from_diroffset = zp->de_fndoffset;

	/*
	 * Ensure that the directory entry still exists and has not
	 * changed till now. If the source is a file the entry may
	 * have been unlinked or renamed. In either case there is
	 * no further work to be done. If the source is a directory
	 * then it cannot have been rmdir'ed or renamed; this is
	 * prohibited by the DE_RENAME flag.
	 *
	 * DE_RENAME is only set if doingdirectory.
	 */
	if (xp != ip) {
		if (doingdirectory)
			panic("rename: lost dir entry");
		goto done;
	} else {
		u_long new_dirclust;
		u_long new_diroffset;

		/*
		 * First write a new entry in the destination
		 * directory and mark the entry in the source directory
		 * as deleted.  Then move the denode to the correct hash
		 * chain for its new location in the filesystem.  And, if
		 * we moved a directory, then update its .. entry to point
		 * to the new parent directory.
		 */
		bcopy(ip->de_Name, oldname, 11);
		bcopy(toname, ip->de_Name, 11);	/* update denode */
		dp->de_fndoffset = to_diroffset;
		dp->de_fndcnt = to_count;
		error = createde(ip, dp, NULL, tcnp);
		if (error) {
			bcopy(oldname, ip->de_Name, 11);
			goto done;
		}
		ip->de_refcnt++;
		zp->de_fndoffset = from_diroffset;
		error = removede(zp, ip);
		if (error) {
			/* XXX should really panic here, fs is corrupt */
			goto done;
		}
		if (!doingdirectory) {
			error = pcbmap(dp, de_cluster(pmp, to_diroffset), 
				       NULL, &new_dirclust, NULL);
			if (error) {
				/* XXX should really panic here, fs is corrupt */
				goto done;
			}
			if (new_dirclust == MSDOSFSROOT)
				new_diroffset = to_diroffset;
			else
				new_diroffset = to_diroffset & pmp->pm_crbomask;
			msdosfs_reinsert(ip, new_dirclust, new_diroffset);
		}
	}

	/*
	 * If we moved a directory to a new parent directory, then we must
	 * fixup the ".." entry in the moved directory.
	 */
	if (doingdirectory && newparent) {
		cn = ip->de_StartCluster;
		if (cn == MSDOSFSROOT) {
			/* this should never happen */
			panic("msdosfs_rename(): updating .. in root directory?");
		} else {
			bn = xcntobn(pmp, cn);
		}
		error = bread(pmp->pm_devvp, de_bntodoff(pmp, bn), pmp->pm_bpcluster, &bp);
		if (error) {
			/* XXX should really panic here, fs is corrupt */
			brelse(bp);
			goto done;
		}
		dotdotp = (struct direntry *)bp->b_data + 1;
		putushort(dotdotp->deStartCluster, dp->de_StartCluster);
		if (FAT32(pmp))
			putushort(dotdotp->deHighClust, dp->de_StartCluster >> 16);
		error = bwrite(bp);
		if (error) {
			/* XXX should really panic here, fs is corrupt */
			goto done;
		}
	}

	/*
	 * done case fvp, fdvp, tdvp are locked.  ap->a_fvp is refd
	 */
done:
	if (doingdirectory)
		ip->de_flag &= ~DE_RENAME;	/* XXX fvp not locked */
	vput(fvp);
	if (newparent)
		vput(fdvp);
	else
		vrele(fdvp);
	vput(tdvp);
	vrele(ap->a_fvp);
	return (error);

	/*
	 * 'bad' target: xp governs tvp.  tvp and tdvp arel ocked, fdvp and fvp
	 * are not locked.  ip points to fvp's inode which may have DE_RENAME
	 * set.
	 */
bad:
	if (xp)
		vput(tvp);
	vput(tdvp);
out:
	/*
	 * 'out' target: tvp and tdvp have already been cleaned up.
	 */
	if (doingdirectory)
		ip->de_flag &= ~DE_RENAME;
	vrele(fdvp);
	vrele(fvp);
	return (error);

}

static struct {
	struct direntry dot;
	struct direntry dotdot;
} dosdirtemplate = {
	{	".       ", "   ",			/* the . entry */
		ATTR_DIRECTORY,				/* file attribute */
		0,	 				/* reserved */
		0, { 0, 0 }, { 0, 0 },			/* create time & date */
		{ 0, 0 },				/* access date */
		{ 0, 0 },				/* high bits of start cluster */
		{ 210, 4 }, { 210, 4 },			/* modify time & date */
		{ 0, 0 },				/* startcluster */
		{ 0, 0, 0, 0 } 				/* filesize */
	},
	{	"..      ", "   ",			/* the .. entry */
		ATTR_DIRECTORY,				/* file attribute */
		0,	 				/* reserved */
		0, { 0, 0 }, { 0, 0 },			/* create time & date */
		{ 0, 0 },				/* access date */
		{ 0, 0 },				/* high bits of start cluster */
		{ 210, 4 }, { 210, 4 },			/* modify time & date */
		{ 0, 0 },				/* startcluster */
		{ 0, 0, 0, 0 }				/* filesize */
	}
};

/*
 * msdosfs_mkdir(struct vnode *a_dvp, struct vnode **a_vpp,
 *		 struct componentname *a_cnp, struct vattr *a_vap)
 */
static int
msdosfs_mkdir(struct vop_old_mkdir_args *ap)
{
	struct componentname *cnp = ap->a_cnp;
	struct denode *dep;
	struct denode *pdep = VTODE(ap->a_dvp);
	struct direntry *denp;
	struct msdosfsmount *pmp = pdep->de_pmp;
	struct buf *bp;
	u_long newcluster, pcl;
	int bn;
	int error;
	struct denode ndirent;
	struct timespec ts;

	/*
	 * If this is the root directory and there is no space left we
	 * can't do anything.  This is because the root directory can not
	 * change size.
	 */
	if (pdep->de_StartCluster == MSDOSFSROOT
	    && pdep->de_fndoffset >= pdep->de_FileSize) {
		error = ENOSPC;
		goto bad2;
	}

	/*
	 * Allocate a cluster to hold the about to be created directory.
	 */
	error = clusteralloc(pmp, 0, 1, CLUST_EOFE, &newcluster, NULL);
	if (error)
		goto bad2;

	bzero(&ndirent, sizeof(ndirent));
	ndirent.de_pmp = pmp;
	ndirent.de_flag = DE_ACCESS | DE_CREATE | DE_UPDATE;
	getnanotime(&ts);
	DETIMES(&ndirent, &ts, &ts, &ts);

	/*
	 * Now fill the cluster with the "." and ".." entries. And write
	 * the cluster to disk.  This way it is there for the parent
	 * directory to be pointing at if there were a crash.
	 */
	bn = xcntobn(pmp, newcluster);
	/* always succeeds */
	bp = getblk(pmp->pm_devvp, de_bntodoff(pmp, bn),
		    pmp->pm_bpcluster, 0, 0);
	bzero(bp->b_data, pmp->pm_bpcluster);
	bcopy(&dosdirtemplate, bp->b_data, sizeof dosdirtemplate);
	denp = (struct direntry *)bp->b_data;
	putushort(denp[0].deStartCluster, newcluster);
	putushort(denp[0].deCDate, ndirent.de_CDate);
	putushort(denp[0].deCTime, ndirent.de_CTime);
	denp[0].deCHundredth = ndirent.de_CHun;
	putushort(denp[0].deADate, ndirent.de_ADate);
	putushort(denp[0].deMDate, ndirent.de_MDate);
	putushort(denp[0].deMTime, ndirent.de_MTime);
	pcl = pdep->de_StartCluster;
	if (FAT32(pmp) && pcl == pmp->pm_rootdirblk)
		pcl = 0;
	putushort(denp[1].deStartCluster, pcl);
	putushort(denp[1].deCDate, ndirent.de_CDate);
	putushort(denp[1].deCTime, ndirent.de_CTime);
	denp[1].deCHundredth = ndirent.de_CHun;
	putushort(denp[1].deADate, ndirent.de_ADate);
	putushort(denp[1].deMDate, ndirent.de_MDate);
	putushort(denp[1].deMTime, ndirent.de_MTime);
	if (FAT32(pmp)) {
		putushort(denp[0].deHighClust, newcluster >> 16);
		putushort(denp[1].deHighClust, pdep->de_StartCluster >> 16);
	}

	error = bwrite(bp);
	if (error)
		goto bad;

	/*
	 * Now build up a directory entry pointing to the newly allocated
	 * cluster.  This will be written to an empty slot in the parent
	 * directory.
	 */
	error = uniqdosname(pdep, cnp, ndirent.de_Name);
	if (error)
		goto bad;

	ndirent.de_Attributes = ATTR_DIRECTORY;
	ndirent.de_LowerCase = 0;
	ndirent.de_StartCluster = newcluster;
	ndirent.de_FileSize = 0;
	ndirent.de_dev = pdep->de_dev;
	ndirent.de_devvp = pdep->de_devvp;
	error = createde(&ndirent, pdep, &dep, cnp);
	if (error)
		goto bad;
	*ap->a_vpp = DETOV(dep);
	return (0);

bad:
	clusterfree(pmp, newcluster, NULL);
bad2:
	return (error);
}

/*
 * msdosfs_rmdir(struct vnode *a_dvp, struct vnode *a_vp,
 *		 struct componentname *a_cnp)
 */
static int
msdosfs_rmdir(struct vop_old_rmdir_args *ap)
{
	struct vnode *vp = ap->a_vp;
	struct vnode *dvp = ap->a_dvp;
	struct denode *ip, *dp;
	int error;
	
	ip = VTODE(vp);
	dp = VTODE(dvp);

	/*
	 * Verify the directory is empty (and valid).
	 * (Rmdir ".." won't be valid since
	 *  ".." will contain a reference to
	 *  the current directory and thus be
	 *  non-empty.)
	 */
	error = 0;
	if (!dosdirempty(ip) || ip->de_flag & DE_RENAME) {
		error = ENOTEMPTY;
		goto out;
	}
	/*
	 * Delete the entry from the directory.  For dos filesystems this
	 * gets rid of the directory entry on disk, the in memory copy
	 * still exists but the de_refcnt is <= 0.  This prevents it from
	 * being found by deget().  When the vput() on dep is done we give
	 * up access and eventually msdosfs_reclaim() will be called which
	 * will remove it from the denode cache.
	 */
	error = removede(dp, ip);
	if (error)
		goto out;
	/*
	 * This is where we decrement the link count in the parent
	 * directory.  Since dos filesystems don't do this we just purge
	 * the name cache.
	 */
	vn_unlock(dvp);
	/*
	 * Truncate the directory that is being deleted.
	 */
	error = detrunc(ip, (u_long)0, IO_SYNC);

	vn_lock(dvp, LK_EXCLUSIVE | LK_RETRY);
out:
	return (error);
}

/*
 * DOS filesystems don't know what symlinks are.
 *
 * msdosfs_symlink(struct vnode *a_dvp, struct vnode **a_vpp,
 *		   struct componentname *a_cnp, struct vattr *a_vap,
 *		   char *a_target)
 */
static int
msdosfs_symlink(struct vop_old_symlink_args *ap)
{
	return (EOPNOTSUPP);
}

/*
 * msdosfs_readdir(struct vnode *a_vp, struct uio *a_uio,
 *		   struct ucred *a_cred, int *a_eofflag, int *a_ncookies,
 *		   off_t **a_cookies)
 */
static int
msdosfs_readdir(struct vop_readdir_args *ap)
{
	struct mbnambuf nb;
	int error = 0;
	int diff;
	long n;
	int blsize;
	long on;
	u_long cn;
	u_long dirsperblk;
	long bias = 0;
	daddr_t bn, lbn;
	struct buf *bp;
	struct denode *dep;
	struct msdosfsmount *pmp;
	struct direntry *dentp;
	struct uio *uio = ap->a_uio;
	off_t *cookies = NULL;
	int ncookies = 0;
	off_t offset, off;
	int chksum = -1;
	ino_t d_ino;
	uint16_t d_namlen;
	uint8_t d_type;
	char *d_name_storage = NULL;
	char *d_name = NULL;

	if ((error = vn_lock(ap->a_vp, LK_EXCLUSIVE | LK_RETRY)) != 0)
		return (error);

	dep = VTODE(ap->a_vp);
	pmp = dep->de_pmp;

#ifdef MSDOSFS_DEBUG
	kprintf("msdosfs_readdir(): vp %p, uio %p, cred %p, eofflagp %p\n",
	    ap->a_vp, uio, ap->a_cred, ap->a_eofflag);
#endif

	/*
	 * msdosfs_readdir() won't operate properly on regular files since
	 * it does i/o only with the the filesystem vnode, and hence can
	 * retrieve the wrong block from the buffer cache for a plain file.
	 * So, fail attempts to readdir() on a plain file.
	 */
	if ((dep->de_Attributes & ATTR_DIRECTORY) == 0) {
		error = ENOTDIR;
		goto done;
	}

	/*
	 * If the user buffer is smaller than the size of one dos directory
	 * entry or the file offset is not a multiple of the size of a
	 * directory entry, then we fail the read.
	 */
	off = offset = uio->uio_offset;
	if (uio->uio_resid < sizeof(struct direntry) ||
	    (offset & (sizeof(struct direntry) - 1))) {
		error = EINVAL;
		goto done;
	}

	if (ap->a_ncookies) {
		ncookies = uio->uio_resid / 16 + 1;
		if (ncookies > 1024)
			ncookies = 1024;
		cookies = kmalloc(ncookies * sizeof(off_t), M_TEMP, M_WAITOK);
		*ap->a_cookies = cookies;
		*ap->a_ncookies = ncookies;
	}

	dirsperblk = pmp->pm_BytesPerSec / sizeof(struct direntry);

	/*
	 * If they are reading from the root directory then, we simulate
	 * the . and .. entries since these don't exist in the root
	 * directory.  We also set the offset bias to make up for having to
	 * simulate these entries. By this I mean that at file offset 64 we
	 * read the first entry in the root directory that lives on disk.
	 */
	if (dep->de_StartCluster == MSDOSFSROOT
	    || (FAT32(pmp) && dep->de_StartCluster == pmp->pm_rootdirblk)) {
#if 0
		kprintf("msdosfs_readdir(): going after . or .. in root dir, offset %d\n",
		    offset);
#endif
		bias = 2 * sizeof(struct direntry);
		if (offset < bias) {
			for (n = (int)offset / sizeof(struct direntry); n < 2;
			     n++) {
				if (FAT32(pmp))
					d_ino = xcntobn(pmp, pmp->pm_rootdirblk)
					    * dirsperblk;
				else
					d_ino = 1;
				d_type = DT_DIR;
				if (n == 0) {
					d_namlen = 1;
					d_name = ".";
				} else if (n == 1) {
					d_namlen = 2;
					d_name = "..";
				}
				if (vop_write_dirent(&error, uio, d_ino, d_type,
				    d_namlen, d_name))
					goto out;
				if (error)
					goto out;
				offset += sizeof(struct direntry);
				off = offset;
				if (cookies) {
					*cookies++ = offset;
					if (--ncookies <= 0)
						goto out;
				}
			}
		}
	}

	d_name_storage = kmalloc(WIN_MAXLEN, M_TEMP, M_WAITOK);
	mbnambuf_init(&nb);
	off = offset;

	while (uio->uio_resid > 0) {
		lbn = de_off2cn(pmp, offset - bias);
		on = (offset - bias) & pmp->pm_crbomask;
		n = szmin(pmp->pm_bpcluster - on, uio->uio_resid);
		diff = dep->de_FileSize - (offset - bias);
		if (diff <= 0)
			break;
		n = min(n, diff);
		error = pcbmap(dep, lbn, &bn, &cn, &blsize);
		if (error)
			break;
		error = bread(pmp->pm_devvp, de_bntodoff(pmp, bn), blsize, &bp);
		if (error) {
			brelse(bp);
			kfree(d_name_storage, M_TEMP);
			goto done;
		}
		n = min(n, blsize - bp->b_resid);

		/*
		 * Convert from dos directory entries to fs-independent
		 * directory entries.
		 */
		for (dentp = (struct direntry *)(bp->b_data + on);
		     (char *)dentp < bp->b_data + on + n;
		     dentp++, offset += sizeof(struct direntry)) {
#if 0
			kprintf("rd: dentp %08x prev %08x crnt %08x deName %02x attr %02x\n",
			    dentp, prev, crnt, dentp->deName[0], dentp->deAttributes);
#endif
			d_name = d_name_storage;
			d_namlen = 0;
			/*
			 * If this is an unused entry, we can stop.
			 */
			if (dentp->deName[0] == SLOT_EMPTY) {
				brelse(bp);
				goto out;
			}
			/*
			 * Skip deleted entries.
			 */
			if (dentp->deName[0] == SLOT_DELETED) {
				chksum = -1;
				mbnambuf_init(&nb);
				continue;
			}
			/*
			 * Handle Win95 long directory entries
			 */
			if (dentp->deAttributes == ATTR_WIN95) {
				if (pmp->pm_flags & MSDOSFSMNT_SHORTNAME)
					continue;
				chksum = win2unixfn(&nb,
					(struct winentry *)dentp,
					chksum,
					pmp);
				continue;
			}

			/*
			 * Skip volume labels
			 */
			if (dentp->deAttributes & ATTR_VOLUME) {
				chksum = -1;
				mbnambuf_init(&nb);
				continue;
			}
			/*
			 * This computation of d_ino must match
			 * the computation of va_fileid in
			 * msdosfs_getattr.
			 */
			if (dentp->deAttributes & ATTR_DIRECTORY) {
				d_ino = getushort(dentp->deStartCluster);
				if (FAT32(pmp))
					d_ino |= getushort(dentp->deHighClust) << 16;
				/* if this is the root directory */
				if (d_ino != MSDOSFSROOT)
					d_ino = xcntobn(pmp, d_ino) * dirsperblk;
				else if (FAT32(pmp))
					d_ino = xcntobn(pmp, pmp->pm_rootdirblk)
					    * dirsperblk;
				else
					d_ino = 1;
				d_type = DT_DIR;
			} else {
				d_ino = offset / sizeof(struct direntry);
				d_type = DT_REG;
			}
			if (chksum != winChksum(dentp->deName)) {
				d_namlen = dos2unixfn(dentp->deName,
				    (u_char *)d_name,
				    dentp->deLowerCase |
					((pmp->pm_flags & MSDOSFSMNT_SHORTNAME) ?
					(LCASE_BASE | LCASE_EXT) : 0),
					pmp);
					mbnambuf_init(&nb);
			} else {
					mbnambuf_flush(&nb, d_name, &d_namlen);
}
			chksum = -1;
			if (vop_write_dirent(&error, uio, d_ino, d_type,
			    d_namlen, d_name)) {
				brelse(bp);
				goto out;
			}
			if (error) {
				brelse(bp);
				goto out;
			}

			if (cookies) {
				*cookies++ = offset + sizeof(struct direntry);
				if (--ncookies <= 0) {
					brelse(bp);
					goto out;
				}
			}
			off = offset + sizeof(struct direntry);
		}
		brelse(bp);
	}
out:
	if (d_name_storage != NULL)
		kfree(d_name_storage, M_TEMP);

	/* Subtract unused cookies */
	if (ap->a_ncookies)
		*ap->a_ncookies -= ncookies;

	uio->uio_offset = off;

	/*
	 * Set the eofflag (NFS uses it)
	 */
	if (ap->a_eofflag) {
		if (dep->de_FileSize - (offset - bias) <= 0)
			*ap->a_eofflag = 1;
		else
			*ap->a_eofflag = 0;
	}
done:
	vn_unlock(ap->a_vp);
	return (error);
}

/*
 * vp  - address of vnode file the file
 * bn  - which cluster we are interested in mapping to a filesystem block number.
 * vpp - returns the vnode for the block special file holding the filesystem
 *	 containing the file of interest
 * bnp - address of where to return the filesystem relative block number
 *
 * msdosfs_bmap(struct vnode *a_vp, off_t a_loffset,
 *		off_t *a_doffsetp, int *a_runp, int *a_runb)
 */
static int
msdosfs_bmap(struct vop_bmap_args *ap)
{
	struct denode *dep = VTODE(ap->a_vp);
	struct msdosfsmount *pmp = dep->de_pmp;
	daddr_t lbn;
	daddr_t dbn;
	int error;

	if (ap->a_doffsetp == NULL)
		return (0);
	if (ap->a_runp) {
		/*
		 * Sequential clusters should be counted here.
		 */
		*ap->a_runp = 0;
	}
	if (ap->a_runb) {
		*ap->a_runb = 0;
	}
	KKASSERT(((int)ap->a_loffset & ((1 << pmp->pm_cnshift) - 1)) == 0);
	lbn = de_off2cn(pmp, ap->a_loffset);
	error = pcbmap(dep, lbn, &dbn, NULL, NULL);
	if (error || dbn == (daddr_t)-1) {
		*ap->a_doffsetp = NOOFFSET;
	} else {
		*ap->a_doffsetp = de_bntodoff(pmp, dbn);
	}
	return (error);
}

/*
 * msdosfs_strategy(struct vnode *a_vp, struct bio *a_bio)
 */
static int
msdosfs_strategy(struct vop_strategy_args *ap)
{
	struct bio *bio = ap->a_bio;
	struct bio *nbio;
	struct buf *bp = bio->bio_buf;
	struct vnode *vp = ap->a_vp;
	struct denode *dep = VTODE(vp);
	struct msdosfsmount *pmp = dep->de_pmp;
	int error = 0;
	daddr_t dblkno;

	if (vp->v_type == VBLK || vp->v_type == VCHR)
		panic("msdosfs_strategy: spec");
	/*
	 * If we don't already know the filesystem relative block number
	 * then get it using pcbmap().  If pcbmap() returns the block
	 * number as -1 then we've got a hole in the file.  DOS filesystems
	 * don't allow files with holes, so we shouldn't ever see this.
	 */
	nbio = push_bio(bio);
	if (nbio->bio_offset == NOOFFSET) {
		error = pcbmap(dep, de_off2cn(pmp, bio->bio_offset),
			       &dblkno, NULL, NULL);
		if (error) {
			bp->b_error = error;
			bp->b_flags |= B_ERROR;
			/* I/O was never started on nbio, must biodone(bio) */
			biodone(bio);
			return (error);
		}
		if (dblkno == (daddr_t)-1) {
			nbio->bio_offset = NOOFFSET;
			vfs_bio_clrbuf(bp);
		} else {
			nbio->bio_offset = de_bntodoff(pmp, dblkno);
		}
	}
	if (nbio->bio_offset == NOOFFSET) {
		/* I/O was never started on nbio, must biodone(bio) */
		biodone(bio);
		return (0);
	}
	/*
	 * Read/write the block from/to the disk that contains the desired
	 * file block.
	 */
	vn_strategy(dep->de_devvp, nbio);
	return (0);
}

/*
 * msdosfs_print(struct vnode *vp)
 */
static int
msdosfs_print(struct vop_print_args *ap)
{
	struct denode *dep = VTODE(ap->a_vp);

	kprintf(
	    "tag VT_MSDOSFS, startcluster %lu, dircluster %lu, diroffset %lu ",
	       dep->de_StartCluster, dep->de_dirclust, dep->de_diroffset);
	kprintf(" dev %d, %d", major(dep->de_dev), minor(dep->de_dev));
	lockmgr_printinfo(&ap->a_vp->v_lock);
	kprintf("\n");
	return (0);
}

/*
 * msdosfs_pathconf(struct vnode *a_vp, int a_name, int *a_retval)
 */
static int
msdosfs_pathconf(struct vop_pathconf_args *ap)
{
	struct msdosfsmount *pmp = VTODE(ap->a_vp)->de_pmp;

	switch (ap->a_name) {
	case _PC_LINK_MAX:
		*ap->a_retval = 1;
		return (0);
	case _PC_NAME_MAX:
		*ap->a_retval = pmp->pm_flags & MSDOSFSMNT_LONGNAME ? WIN_MAXLEN : 12;
		return (0);
	case _PC_PATH_MAX:
		*ap->a_retval = PATH_MAX;
		return (0);
	case _PC_CHOWN_RESTRICTED:
		*ap->a_retval = 1;
		return (0);
	case _PC_NO_TRUNC:
		*ap->a_retval = 0;
		return (0);
	default:
		return (EINVAL);
	}
	/* NOTREACHED */
}

/* Global vfs data structures for msdosfs */
struct vop_ops msdosfs_vnode_vops = {
	.vop_default =		vop_defaultop,
	.vop_access =		msdosfs_access,
	.vop_bmap =		msdosfs_bmap,
	.vop_old_lookup =	msdosfs_lookup,
	.vop_open =		msdosfs_open,
	.vop_close =		msdosfs_close,
	.vop_old_create =	msdosfs_create,
	.vop_fsync =		msdosfs_fsync,
	.vop_getattr =		msdosfs_getattr,
	.vop_inactive =		msdosfs_inactive,
	.vop_old_link =		msdosfs_link,
	.vop_old_mkdir =	msdosfs_mkdir,
	.vop_old_mknod =	msdosfs_mknod,
	.vop_pathconf =		msdosfs_pathconf,
	.vop_print =		msdosfs_print,
	.vop_read =		msdosfs_read,
	.vop_readdir =		msdosfs_readdir,
	.vop_reclaim =		msdosfs_reclaim,
	.vop_old_remove =	msdosfs_remove,
	.vop_old_rename =	msdosfs_rename,
	.vop_old_rmdir =	msdosfs_rmdir,
	.vop_setattr =		msdosfs_setattr,
	.vop_strategy =		msdosfs_strategy,
	.vop_old_symlink =	msdosfs_symlink,
	.vop_write =		msdosfs_write,
	.vop_getpages =		vop_stdgetpages,
	.vop_putpages =		vop_stdputpages
};
