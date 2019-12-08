/* $FreeBSD$ */
/*	$NetBSD: msdosfs_vnops.c,v 1.68 1998/02/10 14:10:04 mrg Exp $	*/

/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
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
/*-
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
#include <sys/uio.h>
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
#include <sys/time.h>

#include <vm/vm.h>
#include <vm/vm_extern.h>
#include <vm/vm_zone.h>
#include <vm/vnode_pager.h>
#include <vm/vm_page2.h>

#include <sys/buf2.h>

#include <machine/inttypes.h>

#include <vfs/msdosfs/bpb.h>
#include <vfs/msdosfs/direntry.h>
#include <vfs/msdosfs/denode.h>
#include <vfs/msdosfs/fat.h>
#include <vfs/msdosfs/msdosfsmount.h>

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
 * retrieve the correct block from the buffer cache since the hash value is
 * based upon the vnode address and the desired block number.
 */

/*
 * Create a regular file. On entry the directory to contain the file being
 * created is locked.  We must release before we return. We must also free
 * the pathname buffer pointed at by cnp->cn_pnbuf, always on error, or
 * only if the SAVESTART bit in cn_flags is clear on success.
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

	mprintf("msdosfs_create(cnp %p, vap %p\n", cnp, ap->a_vap);

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
	memset(&ndirent, 0, sizeof(ndirent));
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
	vfs_timestamp(&ts);
	DETIMES(&ndirent, &ts, &ts, &ts);
	error = createde(&ndirent, pdep, &dep, cnp);
	if (error)
		goto bad;
	*ap->a_vpp = DETOV(dep);
	return (0);

bad:
	return (error);
}

static int
msdosfs_mknod(struct vop_old_mknod_args *ap)
{
	return (EINVAL);
}

static int
msdosfs_open(struct vop_open_args *ap)
{
	return(vop_stdopen(ap));
}

static int
msdosfs_close(struct vop_close_args *ap)
{
	struct vnode *vp = ap->a_vp;
	struct denode *dep = VTODE(vp);
	struct timespec ts;

	if (VREFCNT(vp) > 1) {
		vfs_timestamp(&ts);
		DETIMES(dep, &ts, &ts, &ts);
	}
	return (vop_stdclose(ap));
}

static int
msdosfs_access(struct vop_access_args *ap)
{
	struct denode *dep = VTODE(ap->a_vp);
	struct msdosfsmount *pmp = dep->de_pmp;
	mode_t file_mode;

	file_mode = (S_IXUSR|S_IXGRP|S_IXOTH) | (S_IRUSR|S_IRGRP|S_IROTH) |
	    ((dep->de_Attributes & ATTR_READONLY) ?
	    0 : (S_IWUSR|S_IWGRP|S_IWOTH));
	file_mode &= (ap->a_vp->v_type == VDIR ? pmp->pm_dirmask : pmp->pm_mask);

	return (vop_helper_access(ap, pmp->pm_uid, pmp->pm_gid, file_mode, 0));
}

static int
msdosfs_getattr(struct vop_getattr_args *ap)
{
	struct denode *dep = VTODE(ap->a_vp);
	struct msdosfsmount *pmp = dep->de_pmp;
	struct vattr *vap = ap->a_vap;
	mode_t mode;
	struct timespec ts;
	u_long dirsperblk = pmp->pm_BytesPerSec / sizeof(struct direntry);
	uint64_t fileid;

	vfs_timestamp(&ts);
	DETIMES(dep, &ts, &ts, &ts);
	vap->va_fsid = devid_from_dev(dep->de_dev);
	/*
	 * The following computation of the fileid must be the same as that
	 * used in msdosfs_readdir() to compute d_fileno. If not, pwd
	 * doesn't work.
	 */
	if (dep->de_Attributes & ATTR_DIRECTORY) {
		fileid = (uint64_t)cntobn(pmp, dep->de_StartCluster) *
		    dirsperblk;
		if (dep->de_StartCluster == MSDOSFSROOT)
			fileid = 1;
	} else {
		fileid = (uint64_t)cntobn(pmp, dep->de_dirclust) *
		    dirsperblk;
		if (dep->de_dirclust == MSDOSFSROOT)
			fileid = (uint64_t)roottobn(pmp, 0) * dirsperblk;
		fileid += (uoff_t)dep->de_diroffset / sizeof(struct direntry);
	}
	vap->va_fileid = fileid;
	if ((dep->de_Attributes & ATTR_READONLY) == 0)
		mode = S_IRWXU|S_IRWXG|S_IRWXO;
	else
		mode = S_IRUSR|S_IXUSR|S_IRGRP|S_IXGRP|S_IROTH|S_IXOTH;
	vap->va_mode = mode &
	    (ap->a_vp->v_type == VDIR ? pmp->pm_dirmask : pmp->pm_mask);
	vap->va_uid = pmp->pm_uid;
	vap->va_gid = pmp->pm_gid;
	vap->va_nlink = 1;
	vap->va_rmajor = VNOVAL;
	vap->va_rminor = VNOVAL;
	vap->va_size = dep->de_FileSize;
	fattime2timespec(dep->de_MDate, dep->de_MTime, 0, 0, &vap->va_mtime);
	if (pmp->pm_flags & MSDOSFSMNT_LONGNAME) {
		fattime2timespec(dep->de_ADate, 0, 0, 0, &vap->va_atime);
		fattime2timespec(dep->de_CDate, dep->de_CTime, dep->de_CHun,
		    0, &vap->va_ctime);
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

static int
msdosfs_setattr(struct vop_setattr_args *ap)
{
	struct vnode *vp = ap->a_vp;
	struct denode *dep = VTODE(vp);
	struct msdosfsmount *pmp = dep->de_pmp;
	struct vattr *vap = ap->a_vap;
	struct ucred *cred = ap->a_cred;
	int error = 0;

	mprintf("msdosfs_setattr(): vp %p, vap %p, cred %p\n", vp, vap, cred);

	/*
	 * Check for unsettable attributes.
	 */
	if ((vap->va_type != VNON) || (vap->va_nlink != VNOVAL) ||
	    (vap->va_fsid != VNOVAL) || (vap->va_fileid != VNOVAL) ||
	    (vap->va_blocksize != VNOVAL) || (vap->va_rmajor != VNOVAL) ||
	    (vap->va_bytes != VNOVAL) || (vap->va_gen != VNOVAL)) {
		mprintf("msdosfs_setattr(): returning EINVAL\n");
		mprintf("    va_type %d, va_nlink %llx, va_fsid %llx, va_fileid %llx\n",
		    vap->va_type, (unsigned long long)vap->va_nlink,
		    (unsigned long long)vap->va_fsid,
		    (unsigned long long)vap->va_fileid);
		mprintf("    va_blocksize %lx, va_rdev %llx, va_bytes %llx, va_gen %lx\n",
		    vap->va_blocksize, (unsigned long long)0,
		    (unsigned long long)vap->va_bytes, vap->va_gen);
		mprintf("    va_uid %x, va_gid %x\n",
		    vap->va_uid, vap->va_gid);
		return (EINVAL);
	}

	/*
	 * We don't allow setting attributes on the root directory.
	 * The special case for the root directory is because before
	 * FAT32, the root directory didn't have an entry for itself
	 * (and was otherwise special).  With FAT32, the root
	 * directory is not so special, but still doesn't have an
	 * entry for itself.
	 */
	if (vp->v_flag & VROOT)
		return (EINVAL);

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
		switch (vp->v_type) {
		case VDIR:
			return (EISDIR);
		case VREG:
			/*
			 * Truncation is only supported for regular files,
			 * Disallow it if the filesystem is read-only.
			 */
			if (vp->v_mount->mnt_flag & MNT_RDONLY)
				return (EROFS);
			break;
		default:
			/*
			 * According to POSIX, the result is unspecified
			 * for file types other than regular files,
			 * directories and shared memory objects.  We
			 * don't support any file types except regular
			 * files and directories in this file system, so
			 * this (default) case is unreachable and can do
			 * anything.  Keep falling through to detrunc()
			 * for now.
			 */
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
		    (error = VOP_EACCESS(vp, VWRITE, cred))))
			return (error);
		if (vp->v_type != VDIR) {
			if ((pmp->pm_flags & MSDOSFSMNT_NOWIN95) == 0 &&
			    vap->va_atime.tv_sec != VNOVAL) {
				dep->de_flag &= ~DE_ACCESS;
				timespec2fattime(&vap->va_atime, 0,
				    &dep->de_ADate, NULL, NULL);
			}
			if (vap->va_mtime.tv_sec != VNOVAL) {
				dep->de_flag &= ~DE_UPDATE;
				timespec2fattime(&vap->va_mtime, 0,
				    &dep->de_MDate, &dep->de_MTime, NULL);
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
	return (deupdat(dep, 0));
}

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
	u_long cn;
	daddr_t lbn;
	daddr_t rablock;
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

	if ((uoff_t)uio->uio_offset > MSDOSFS_FILESIZE_MAX)
		return (0);
	/*
	 * If they didn't ask for any data, then we are done.
	 */
	orig_resid = uio->uio_resid;
	if (orig_resid == 0)
		return (0);

	/*
	 * The caller is supposed to ensure that
	 * uio->uio_offset >= 0 and uio->uio_resid >= 0.
	 * We don't need to check for large offsets as in ffs because
	 * dep->de_FileSize <= MSDOSFS_FILESIZE_MAX < OFF_MAX, so large
	 * offsets cannot cause overflow even in theory.
	 */

	seqcount = ap->a_ioflag >> IO_SEQSHIFT;

	isadir = dep->de_Attributes & ATTR_DIRECTORY;
	do {
		if (uio->uio_offset >= dep->de_FileSize)
			break;

		cn = de_cluster(pmp, uio->uio_offset);
		loffset = de_cn2doff(pmp, cn);
		rablock = cn + 1;
		blsize = pmp->pm_bpcluster;
		on = uio->uio_offset & pmp->pm_crbomask;

		/*
		 * If we are operating on a directory file then be sure to
		 * do i/o with the vnode for the filesystem instead of the
		 * vnode for the directory.
		 */
		bp = NULL;
		if (isadir) {
			/* convert cluster # to block # */
			error = pcbmap(dep, cn, &lbn, NULL, &blsize);
			loffset = de_bn2doff(pmp, lbn);
			if (error == E2BIG) {
				error = EINVAL;
				break;
			} else if (error)
				break;
			error = bread(pmp->pm_devvp, loffset, blsize, &bp);
		} else if (de_cn2off(pmp, rablock) >= dep->de_FileSize) {
			error = bread(vp, loffset, blsize, &bp);
		} else if ((vp->v_mount->mnt_flag & MNT_NOCLUSTERR) == 0) {
			error = cluster_readx(vp, dep->de_FileSize, loffset,
			    blsize, B_NOTMETA, on + uio->uio_resid,
			    seqcount * MAXBSIZE, &bp);
			bp->b_flags |= B_CLUSTEROK;
		} else if (seqcount > 1) {
			off_t raoffset = de_cn2doff(pmp, rablock);
			rasize = blsize;
			error = breadn(vp, loffset,
			    blsize, &raoffset, &rasize, 1, &bp);
		} else {
			error = bread(vp, loffset, blsize, &bp);
		}
		if (error) {
			brelse(bp);
			break;
		}
		diff = pmp->pm_bpcluster - on;
		n = szmin(uio->uio_resid, diff);
		diff = dep->de_FileSize - uio->uio_offset;
		if (diff < n)
			n = diff;
		diff = blsize - bp->b_resid;
		if (diff < n)
			n = diff;
		error = uiomovebp(bp, bp->b_data + on, (size_t)n, uio);
		bqrelse(bp);
	} while (error == 0 && uio->uio_resid > 0 && n != 0);

	if (!isadir && (error == 0 || uio->uio_resid != orig_resid) &&
	    (vp->v_mount->mnt_flag & (MNT_NOATIME | MNT_RDONLY)) == 0)
		dep->de_flag |= DE_ACCESS;
	return (error);
}

/*
 * Write data to a file or directory.
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
	int seqcount;
	daddr_t cn, lastcn;
	struct buf *bp = NULL;
	int ioflag = ap->a_ioflag;
	struct uio *uio = ap->a_uio;
	struct thread *td = uio->uio_td;
	struct vnode *vp = ap->a_vp;
	struct vnode *thisvp;
	struct denode *dep = VTODE(vp);
	struct msdosfsmount *pmp = dep->de_pmp;
	struct proc *p = (td ? td->td_proc : NULL);
	struct lwp *lp = (td ? td->td_lwp : NULL);

	mprintf("msdosfs_write(vp %p, uio %p, ioflag %x, cred %p\n",
	    vp, uio, ioflag, ap->a_cred);
	mprintf("msdosfs_write(): diroff %lu, dirclust %lu, startcluster %lu\n",
	    dep->de_diroffset, dep->de_dirclust, dep->de_StartCluster);

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

	/*
	 * This is needed (unlike in ffs_write()) because we extend the
	 * file outside of the loop but we don't want to extend the file
	 * for writes of 0 bytes.
	 */
	if (uio->uio_resid == 0)
		return (0);

	/*
	 * The caller is supposed to ensure that
	 * uio->uio_offset >= 0 and uio->uio_resid >= 0.
	 */
	if ((uoff_t)uio->uio_offset + uio->uio_resid > MSDOSFS_FILESIZE_MAX)
		return (EFBIG);

	/*
	 * If they've exceeded their filesize limit, tell them about it.
	 */
	if (p &&
	    ((uoff_t)uio->uio_offset + uio->uio_resid >
	    p->p_rlimit[RLIMIT_FSIZE].rlim_cur)) {
		lwpsignal(p, lp, SIGXFSZ);
		return (EFBIG);
	}

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

	seqcount = ioflag >> IO_SEQSHIFT;
	do {
		if (de_cluster(pmp, uio->uio_offset) > lastcn) {
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
		cn = de_cluster(pmp, uio->uio_offset);
		if ((uio->uio_offset & pmp->pm_crbomask) == 0
		    && uio->uio_segflg != UIO_NOCOPY
		    && (de_cluster(pmp, uio->uio_offset + uio->uio_resid)
		        > de_cluster(pmp, uio->uio_offset)
			|| uio->uio_offset + uio->uio_resid >= dep->de_FileSize)) {
			bp = getblk(thisvp, de_cn2doff(pmp, cn),
				    pmp->pm_bpcluster, 0, 0);
			clrbuf(bp);
			/*
			 * Do the bmap now, since pcbmap needs buffers
			 * for the FAT table. (see msdosfs_strategy)
			 */
			if (bp->b_bio2.bio_offset == NOOFFSET) {
				daddr_t dblkno;

				error = pcbmap(dep,
					       de_cluster(pmp, bp->b_loffset),
					       &dblkno, NULL, NULL);
				if (error || dblkno == (daddr_t)-1) {
					bp->b_bio2.bio_offset = NOOFFSET;
				} else {
					bp->b_bio2.bio_offset = de_bn2doff(pmp,
					    dblkno);
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

		/* Prepare for clustered writes in some else clauses. */
		if ((vp->v_mount->mnt_flag & MNT_NOCLUSTERW) == 0)
			bp->b_flags |= B_CLUSTEROK;

		/*
		 * If IO_SYNC, then each buffer is written synchronously.
		 * Otherwise, if we have a severe page deficiency then
		 * write the buffer asynchronously.  Otherwise, if on a
		 * cluster boundary then write the buffer asynchronously,
		 * combining it with contiguous clusters if permitted and
		 * possible, since we don't expect more writes into this
		 * buffer soon.  Otherwise, do a delayed write because we
		 * expect more writes into this buffer soon.
		 */
		if (ioflag & IO_SYNC)
			bwrite(bp);
		else if (vm_page_count_severe() || buf_dirty_count_severe())
			bawrite(bp);
		else if (n + croffset == pmp->pm_bpcluster) {
			if ((vp->v_mount->mnt_flag & MNT_NOCLUSTERW) == 0)
				cluster_write(bp, dep->de_FileSize,
				    pmp->pm_bpcluster, seqcount);
			else
				bawrite(bp);
		} else
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
 */
static int
msdosfs_fsync(struct vop_fsync_args *ap)
{
	struct vnode *vp = ap->a_vp;
	struct vnode *devvp;
	int allerror, error;

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
	/*
	* If the syncing request comes from fsync(2), sync the entire
	* FAT and any other metadata that happens to be on devvp.  We
	* need this mainly for the FAT.  We write the FAT sloppily, and
	* syncing it all now is the best we can easily do to get all
	* directory entries associated with the file (not just the file)
	* fully synced.  The other metadata includes critical metadata
	* for all directory entries, but only in the MNT_ASYNC case.  We
	* will soon sync all metadata in the file's directory entry.
	* Non-critical metadata for associated directory entries only
	* gets synced accidentally, as in most file systems.
	*/
	if (ap->a_waitfor == MNT_WAIT) {
		devvp = VTODE(ap->a_vp)->de_pmp->pm_devvp;
		vn_lock(devvp, LK_EXCLUSIVE | LK_RETRY);
		allerror = VOP_FSYNC(devvp, MNT_WAIT, 0);
		vn_unlock(devvp);
	} else
		allerror = 0;

	error = deupdat(VTODE(ap->a_vp), ap->a_waitfor == MNT_WAIT);
	if (allerror == 0)
		allerror = error;
	return (allerror);
}

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
	mprintf("msdosfs_remove(), dep %p, v_refcnt 0x%08x\n",
		dep, ap->a_vp->v_refcnt);
	return (error);
}

/*
 * DOS filesystems don't know what links are.
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
	u_char toname[12], oldname[11];
	u_long from_diroffset, to_diroffset;
	u_char to_count;
	int doingdirectory = 0, newparent = 0;
	int error;
	u_long cn, pcl;
	daddr_t bn;
	struct msdosfsmount *pmp;
	struct direntry *dotdotp;
	struct buf *bp = NULL;

	pmp = VFSTOMSDOSFS(fdvp->v_mount);

	/*
	 * Check for cross-device rename.
	 */
	if (fvp->v_mount != tdvp->v_mount ||
	    (tvp && fvp->v_mount != tvp->v_mount)) {
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
	 * fvp is unlocked, fdvp, tvp, tdvp are locked.  Lock fvp and note
	 * that we have to unlock it to use the abortit target.
	 */
	error = vn_lock(fvp, LK_EXCLUSIVE | LK_FAILRECLAIM);
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
	 * directory hierarchy above the target, as this would
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
		memcpy(oldname, ip->de_Name, 11);
		memcpy(ip->de_Name, toname, 11);	/* update denode */
		dp->de_fndoffset = to_diroffset;
		dp->de_fndcnt = to_count;
		error = createde(ip, dp, NULL, tcnp);
		if (error) {
			memcpy(ip->de_Name, oldname, 11);
			goto done;
		}
		/*
		 * If ip is for a directory, then its name should always
		 * be "." since it is for the directory entry in the
		 * directory itself (msdosfs_lookup() always translates
		 * to the "." entry so as to get a unique denode, except
		 * for the root directory there are different
		 * complications).  However, we just corrupted its name
		 * to pass the correct name to createde().  Undo this.
		 */
		if ((ip->de_Attributes & ATTR_DIRECTORY) != 0)
			memcpy(ip->de_Name, oldname, 11);
		ip->de_refcnt++;
		zp->de_fndoffset = from_diroffset;
		error = removede(zp, ip);
		if (error) {
			/* XXX should downgrade to ro here, fs is corrupt */
			goto done;
		}
		if (!doingdirectory) {
			error = pcbmap(dp, de_cluster(pmp, to_diroffset),
				       NULL, &new_dirclust, NULL);
			if (error) {
				/* XXX should downgrade to ro here, fs is corrupt */
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
			bn = cntobn(pmp, cn);
		}
		error = bread(pmp->pm_devvp, de_bn2doff(pmp, bn),
			      pmp->pm_bpcluster, &bp);
		if (error) {
			/* XXX should downgrade to ro here, fs is corrupt */
			brelse(bp);
			goto done;
		}
		dotdotp = (struct direntry *)bp->b_data + 1;
		pcl = dp->de_StartCluster;
		if (FAT32(pmp) && pcl == pmp->pm_rootdirblk)
			pcl = MSDOSFSROOT;
		putushort(dotdotp->deStartCluster, pcl);
		if (FAT32(pmp))
			putushort(dotdotp->deHighClust, pcl >> 16);
		if (DOINGASYNC(fvp))
			bdwrite(bp);
		else if ((error = bwrite(bp)) != 0) {
			/* XXX should downgrade to ro here, fs is corrupt */
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
	 * 'bad' target: xp governs tvp.  tvp and tdvp are locked, fdvp and fvp
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
	{	".          ",				/* the . entry */
		ATTR_DIRECTORY,				/* file attribute */
		0,					/* reserved */
		0, { 0, 0 }, { 0, 0 },			/* create time & date */
		{ 0, 0 },				/* access date */
		{ 0, 0 },				/* high bits of start cluster */
		{ 210, 4 }, { 210, 4 },			/* modify time & date */
		{ 0, 0 },				/* startcluster */
		{ 0, 0, 0, 0 }				/* filesize */
	},
	{	"..         ",				/* the .. entry */
		ATTR_DIRECTORY,				/* file attribute */
		0,					/* reserved */
		0, { 0, 0 }, { 0, 0 },			/* create time & date */
		{ 0, 0 },				/* access date */
		{ 0, 0 },				/* high bits of start cluster */
		{ 210, 4 }, { 210, 4 },			/* modify time & date */
		{ 0, 0 },				/* startcluster */
		{ 0, 0, 0, 0 }				/* filesize */
	}
};

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

	memset(&ndirent, 0, sizeof(ndirent));
	ndirent.de_pmp = pmp;
	ndirent.de_flag = DE_ACCESS | DE_CREATE | DE_UPDATE;
	vfs_timestamp(&ts);
	DETIMES(&ndirent, &ts, &ts, &ts);

	/*
	 * Now fill the cluster with the "." and ".." entries. And write
	 * the cluster to disk.  This way it is there for the parent
	 * directory to be pointing at if there were a crash.
	 */
	bn = cntobn(pmp, newcluster);
	/* always succeeds */
	bp = getblk(pmp->pm_devvp, de_bn2doff(pmp, bn),
		    pmp->pm_bpcluster, 0, 0);
	memset(bp->b_data, 0, pmp->pm_bpcluster);
	memcpy(bp->b_data, &dosdirtemplate, sizeof(dosdirtemplate));
	denp = (struct direntry *)bp->b_data;
	putushort(denp[0].deStartCluster, newcluster);
	putushort(denp[0].deCDate, ndirent.de_CDate);
	putushort(denp[0].deCTime, ndirent.de_CTime);
	denp[0].deCHundredth = ndirent.de_CHun;
	putushort(denp[0].deADate, ndirent.de_ADate);
	putushort(denp[0].deMDate, ndirent.de_MDate);
	putushort(denp[0].deMTime, ndirent.de_MTime);
	pcl = pdep->de_StartCluster;
	/*
	 * Although the root directory has a non-magic starting cluster
	 * number for FAT32, chkdsk and fsck_msdosfs still require
	 * references to it in dotdot entries to be magic.
	 */
	if (FAT32(pmp) && pcl == pmp->pm_rootdirblk)
		pcl = MSDOSFSROOT;
	putushort(denp[1].deStartCluster, pcl);
	putushort(denp[1].deCDate, ndirent.de_CDate);
	putushort(denp[1].deCTime, ndirent.de_CTime);
	denp[1].deCHundredth = ndirent.de_CHun;
	putushort(denp[1].deADate, ndirent.de_ADate);
	putushort(denp[1].deMDate, ndirent.de_MDate);
	putushort(denp[1].deMTime, ndirent.de_MTime);
	if (FAT32(pmp)) {
		putushort(denp[0].deHighClust, newcluster >> 16);
		putushort(denp[1].deHighClust, pcl >> 16);
	}

	if (DOINGASYNC(ap->a_dvp))
		bdwrite(bp);
	else if ((error = bwrite(bp)) != 0)
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
	 * Truncate the directory that is being deleted.
	 */
	error = detrunc(ip, (u_long)0, IO_SYNC);
out:
	return (error);
}

/*
 * DOS filesystems don't know what symlinks are.
 */
static int
msdosfs_symlink(struct vop_old_symlink_args *ap)
{
	return (EOPNOTSUPP);
}

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
	struct buf *bp = NULL;
	struct denode *dep;
	struct msdosfsmount *pmp;
	struct direntry *dentp;
	struct dirent dirbuf;
	struct uio *uio = ap->a_uio;
	off_t *cookies = NULL;
	int ncookies = 0;
	off_t offset, off;
	int chksum = -1;

	error = vn_lock(ap->a_vp, LK_EXCLUSIVE | LK_RETRY | LK_FAILRECLAIM);
	if (error)
		return (error);

	dep = VTODE(ap->a_vp);
	pmp = dep->de_pmp;

	mprintf("msdosfs_readdir(): vp %p, uio %p, cred %p, eofflagp %p\n",
	    ap->a_vp, uio, ap->a_cred, ap->a_eofflag);

	/*
	 * msdosfs_readdir() won't operate properly on regular files since
	 * it does i/o only with the filesystem vnode, and hence can
	 * retrieve the wrong block from the buffer cache for a plain file.
	 * So, fail attempts to readdir() on a plain file.
	 */
	if ((dep->de_Attributes & ATTR_DIRECTORY) == 0) {
		error = ENOTDIR;
		goto done;
	}

	/*
	 * To be safe, initialize dirbuf
	 */
	memset(dirbuf.d_name, 0, sizeof(dirbuf.d_name));

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

#define d_fileno d_ino
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
		printf("msdosfs_readdir(): going after . or .. in root dir, offset %d\n",
		    offset);
#endif
		bias = 2 * sizeof(struct direntry);
		if (offset < bias) {
			for (n = (int)offset / sizeof(struct direntry);
			     n < 2; n++) {
				dirbuf.d_fileno = FAT32(pmp) ?
				    (uint64_t)cntobn(pmp, pmp->pm_rootdirblk) *
				    dirsperblk : 1;
				dirbuf.d_type = DT_DIR;
				switch (n) {
				case 0:
					dirbuf.d_namlen = 1;
					dirbuf.d_name[0] = '.';
					break;
				case 1:
					dirbuf.d_namlen = 2;
					dirbuf.d_name[0] = '.';
					dirbuf.d_name[1] = '.';
					break;
				}
				if (vop_write_dirent(&error, uio,
				    dirbuf.d_fileno, dirbuf.d_type,
				    dirbuf.d_namlen, dirbuf.d_name))
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

	mbnambuf_init(&nb);
	off = offset;

	while (uio->uio_resid > 0) {
		lbn = de_cluster(pmp, offset - bias);
		on = (offset - bias) & pmp->pm_crbomask;
		n = szmin(pmp->pm_bpcluster - on, uio->uio_resid);
		diff = dep->de_FileSize - (offset - bias);
		if (diff <= 0)
			break;
		n = min(n, diff);
		error = pcbmap(dep, lbn, &bn, &cn, &blsize);
		if (error)
			break;
		error = bread(pmp->pm_devvp, de_bn2doff(pmp, bn), blsize, &bp);
		if (error) {
			brelse(bp);
			goto done;
		}
		n = min(n, blsize - bp->b_resid);
		if (n == 0) {
			brelse(bp);
			error = EIO;
			goto done;
		}

		/*
		 * Convert from dos directory entries to fs-independent
		 * directory entries.
		 */
		for (dentp = (struct direntry *)(bp->b_data + on);
		     (char *)dentp < bp->b_data + on + n;
		     dentp++, offset += sizeof(struct direntry)) {
#if 0
			printf("rd: dentp %08x prev %08x crnt %08x deName %02x attr %02x\n",
			    dentp, prev, crnt, dentp->deName[0], dentp->deAttributes);
#endif
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
				    (struct winentry *)dentp, chksum, pmp);
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
			 * This computation of d_fileno must match
			 * the computation of va_fileid in
			 * msdosfs_getattr.
			 */
			if (dentp->deAttributes & ATTR_DIRECTORY) {
				cn = getushort(dentp->deStartCluster);
				if (FAT32(pmp)) {
					cn |= getushort(dentp->deHighClust) <<
					    16;
					if (cn == MSDOSFSROOT)
						cn = pmp->pm_rootdirblk;
				}
				if (cn == MSDOSFSROOT && !FAT32(pmp))
					dirbuf.d_fileno = 1;
				else
					dirbuf.d_fileno = cntobn(pmp, cn) *
					    dirsperblk;
				dirbuf.d_type = DT_DIR;
			} else {
				dirbuf.d_fileno = (uoff_t)offset /
				    sizeof(struct direntry);
				dirbuf.d_type = DT_REG;
			}

			if (chksum != winChksum(dentp->deName)) {
				dirbuf.d_namlen = dos2unixfn(dentp->deName,
				    (u_char *)dirbuf.d_name,
				    dentp->deLowerCase |
					((pmp->pm_flags & MSDOSFSMNT_SHORTNAME) ?
					(LCASE_BASE | LCASE_EXT) : 0),
				    pmp);
				mbnambuf_init(&nb);
			} else
				mbnambuf_flush(&nb, &dirbuf);
			chksum = -1;
			if (vop_write_dirent(&error, uio, dirbuf.d_fileno,
			    dirbuf.d_type, dirbuf.d_namlen, dirbuf.d_name)) {
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

/*-
 * a_vp   - pointer to the file's vnode
 * a_runp - where to return the "run past" a_bn.  This is the count of logical
 *          blocks whose physical blocks (together with a_bn's physical block)
 *          are contiguous.
 * a_runb - where to return the "run before" a_bn.
 */
static int
msdosfs_bmap(struct vop_bmap_args *ap)
{
	struct fatcache savefc;
	struct denode *dep;
	struct mount *mp;
	struct msdosfsmount *pmp;
	struct vnode *vp;
	daddr_t dbn, runbn;
	u_long cn;
	int bnpercn, error, maxio, maxrun, run;

	vp = ap->a_vp;
	dep = VTODE(vp);
	pmp = dep->de_pmp;

	if (ap->a_doffsetp == NULL)
		return (0);
	if (ap->a_runp != NULL)
		*ap->a_runp = 0;
	if (ap->a_runb != NULL)
		*ap->a_runb = 0;

	KKASSERT(((int)ap->a_loffset & ((1 << pmp->pm_cnshift) - 1)) == 0);
	error = pcbmap(dep, de_cluster(pmp, ap->a_loffset), &dbn, NULL, NULL);
	if (error || dbn == (daddr_t)-1)
		*ap->a_doffsetp = NOOFFSET;
	else
		*ap->a_doffsetp = de_bn2doff(pmp, dbn);
	if (error != 0 || dbn == (daddr_t)-1 ||
	    (ap->a_runp == NULL && ap->a_runb == NULL))
		return (error);

	/*
	 * Prepare to back out updates of the fatchain cache after the one
	 * for the first block done by pcbmap() above.  Without the backout,
	 * then whenever the caller doesn't do i/o to all of the blocks that
	 * we find, the single useful cache entry would be too far in advance
	 * of the actual i/o to work for the next sequential i/o.  Then the
	 * FAT would be searched from the beginning.  With the backout, the
	 * FAT is searched starting at most a few blocks early.  This wastes
	 * much less time.  Time is also wasted finding more blocks than the
	 * caller will do i/o to.  This is necessary because the runlength
	 * parameters are output-only.
	 */
	savefc = dep->de_fc[FC_LASTMAP];

	cn = de_cluster(pmp, ap->a_loffset);
	mp = vp->v_mount;
	maxio = mp->mnt_iosize_max / mp->mnt_stat.f_iosize;
	bnpercn = de_cn2bn(pmp, 1);
	if (ap->a_runp != NULL) {
		maxrun = ulmin(maxio - 1, pmp->pm_maxcluster - cn);
		for (run = 1; run <= maxrun; run++) {
			if (pcbmap(dep, cn + run, &runbn, NULL, NULL) != 0 ||
			    runbn != dbn + run * bnpercn)
				break;
		}
		*ap->a_runp = run - 1;
	}
	if (ap->a_runb != NULL) {
		maxrun = ulmin(maxio - 1, cn);
		for (run = 1; run < maxrun; run++) {
			if (pcbmap(dep, cn - run, &runbn, NULL, NULL) != 0 ||
			    runbn != dbn - run * bnpercn)
				break;
		}
		*ap->a_runb = run - 1;
	}
	dep->de_fc[FC_LASTMAP] = savefc;

	return (0);
}

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
		error = pcbmap(dep, de_cluster(pmp, bio->bio_offset),
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
			nbio->bio_offset = de_bn2doff(pmp, dblkno);
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

static int
msdosfs_print(struct vop_print_args *ap)
{
	struct denode *dep = VTODE(ap->a_vp);

	kprintf("tag VT_MSDOSFS, startcluster %lu, dircluster %lu, "
		"diroffset %lu, dev %d, %d",
		dep->de_StartCluster, dep->de_dirclust, dep->de_diroffset,
		major(dep->de_dev), minor(dep->de_dev));
	lockmgr_printinfo(&ap->a_vp->v_lock);
	return (0);
}

static int
msdosfs_pathconf(struct vop_pathconf_args *ap)
{
	struct msdosfsmount *pmp = VTODE(ap->a_vp)->de_pmp;

	switch (ap->a_name) {
	case _PC_FILESIZEBITS:
		*ap->a_retval = FAT12(pmp) ? 32 : 33;
		return (0);
	case _PC_LINK_MAX:
		*ap->a_retval = 1;
		return (0);
	case _PC_NAME_MAX:
		*ap->a_retval =
		    pmp->pm_flags & MSDOSFSMNT_LONGNAME ? WIN_MAXLEN : 12;
		return (0);
	case _PC_CHOWN_RESTRICTED:
		*ap->a_retval = 1;
		return (0);
	case _PC_NO_TRUNC:
		*ap->a_retval = 0;
		return (0);
	default:
		return (vop_stdpathconf(ap));
	}
	/* NOTREACHED */
}

/* Global vfs data structures for msdosfs */
struct vop_ops msdosfs_vnode_vops = {
	.vop_default =		vop_defaultop,
	.vop_access =		msdosfs_access,
	.vop_bmap =		msdosfs_bmap,
	.vop_getpages =		vop_stdgetpages,
	.vop_putpages =		vop_stdputpages,
	.vop_open =		msdosfs_open,
	.vop_close =		msdosfs_close,
	.vop_old_create =	msdosfs_create,
	.vop_fsync =		msdosfs_fsync,
	.vop_getattr =		msdosfs_getattr,
	.vop_inactive =		msdosfs_inactive,
	.vop_old_link =		msdosfs_link,
	.vop_old_lookup =	msdosfs_lookup,
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
};
