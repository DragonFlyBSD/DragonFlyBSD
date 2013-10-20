/*
 * Copyright (c) 1982, 1986, 1989, 1993, 1995
 *	The Regents of the University of California.  All rights reserved.
 * (c) UNIX System Laboratories, Inc.
 * All or some portions of this file are derived from material licensed
 * to the University of California by American Telephone and Telegraph
 * Co. or Unix System Laboratories, Inc. and are reproduced herein with
 * the permission of UNIX System Laboratories, Inc.
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
 *	@(#)ufs_vnops.c	8.27 (Berkeley) 5/27/95
 * $FreeBSD: src/sys/ufs/ufs/ufs_vnops.c,v 1.131.2.8 2003/01/02 17:26:19 bde Exp $
 */

#include "opt_quota.h"
#include "opt_suiddir.h"
#include "opt_ufs.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/fcntl.h>
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
#include <sys/lockf.h>
#include <sys/event.h>
#include <sys/conf.h>

#include <sys/file.h>		/* XXX */
#include <sys/jail.h>

#include <vm/vm.h>
#include <vm/vm_extern.h>

#include <vfs/fifofs/fifo.h>

#include "quota.h"
#include "inode.h"
#include "dir.h"
#include "ufsmount.h"
#include "ufs_extern.h"
#include "ffs_extern.h"
#include "fs.h"
#ifdef UFS_DIRHASH
#include "dirhash.h"
#endif

static int ufs_access (struct vop_access_args *);
static int ufs_advlock (struct vop_advlock_args *);
static int ufs_chmod (struct vnode *, int, struct ucred *);
static int ufs_chown (struct vnode *, uid_t, gid_t, struct ucred *);
static int ufs_close (struct vop_close_args *);
static int ufs_create (struct vop_old_create_args *);
static int ufs_getattr (struct vop_getattr_args *);
static int ufs_link (struct vop_old_link_args *);
static int ufs_makeinode (int mode, struct vnode *, struct vnode **, struct componentname *);
static int ufs_markatime (struct vop_markatime_args *);
static int ufs_missingop (struct vop_generic_args *ap);
static int ufs_mkdir (struct vop_old_mkdir_args *);
static int ufs_mknod (struct vop_old_mknod_args *);
static int ufs_mmap (struct vop_mmap_args *);
static int ufs_print (struct vop_print_args *);
static int ufs_readdir (struct vop_readdir_args *);
static int ufs_readlink (struct vop_readlink_args *);
static int ufs_remove (struct vop_old_remove_args *);
static int ufs_rename (struct vop_old_rename_args *);
static int ufs_rmdir (struct vop_old_rmdir_args *);
static int ufs_setattr (struct vop_setattr_args *);
static int ufs_strategy (struct vop_strategy_args *);
static int ufs_symlink (struct vop_old_symlink_args *);
static int ufs_whiteout (struct vop_old_whiteout_args *);
static int ufsfifo_close (struct vop_close_args *);
static int ufsfifo_kqfilter (struct vop_kqfilter_args *);
static int ufsfifo_read (struct vop_read_args *);
static int ufsfifo_write (struct vop_write_args *);
static int filt_ufsread (struct knote *kn, long hint);
static int filt_ufswrite (struct knote *kn, long hint);
static int filt_ufsvnode (struct knote *kn, long hint);
static void filt_ufsdetach (struct knote *kn);
static int ufs_kqfilter (struct vop_kqfilter_args *ap);

union _qcvt {
	int64_t qcvt;
	int32_t val[2];
};
#define SETHIGH(q, h) { \
	union _qcvt tmp; \
	tmp.qcvt = (q); \
	tmp.val[_QUAD_HIGHWORD] = (h); \
	(q) = tmp.qcvt; \
}
#define SETLOW(q, l) { \
	union _qcvt tmp; \
	tmp.qcvt = (q); \
	tmp.val[_QUAD_LOWWORD] = (l); \
	(q) = tmp.qcvt; \
}
#define VN_KNOTE(vp, b) \
	KNOTE(&vp->v_pollinfo.vpi_kqinfo.ki_note, (b))

#define OFSFMT(vp)		((vp)->v_mount->mnt_maxsymlinklen <= 0)

/*
 * A virgin directory (no blushing please).
 */
static struct dirtemplate mastertemplate = {
	0, 12, DT_DIR, 1, ".",
	0, DIRBLKSIZ - 12, DT_DIR, 2, ".."
};
static struct odirtemplate omastertemplate = {
	0, 12, 1, ".",
	0, DIRBLKSIZ - 12, 2, ".."
};

void
ufs_itimes(struct vnode *vp)
{
	struct inode *ip;
	struct timespec ts;

	ip = VTOI(vp);
	if ((ip->i_flag & (IN_ACCESS | IN_CHANGE | IN_UPDATE)) == 0)
		return;
	if ((vp->v_type == VBLK || vp->v_type == VCHR) && !DOINGSOFTDEP(vp))
		ip->i_flag |= IN_LAZYMOD;
	else
		ip->i_flag |= IN_MODIFIED;
	if ((vp->v_mount->mnt_flag & MNT_RDONLY) == 0) {
		vfs_timestamp(&ts);
		if (ip->i_flag & IN_ACCESS) {
			ip->i_atime = ts.tv_sec;
			ip->i_atimensec = ts.tv_nsec;
		}
		if (ip->i_flag & IN_UPDATE) {
			ip->i_mtime = ts.tv_sec;
			ip->i_mtimensec = ts.tv_nsec;
			ip->i_modrev++;
		}
		if (ip->i_flag & IN_CHANGE) {
			ip->i_ctime = ts.tv_sec;
			ip->i_ctimensec = ts.tv_nsec;
		}
	}
	ip->i_flag &= ~(IN_ACCESS | IN_CHANGE | IN_UPDATE);
}

/*
 * Create a regular file
 *
 * ufs_create(struct vnode *a_dvp, struct vnode **a_vpp,
 *	      struct componentname *a_cnp, struct vattr *a_vap)
 */
static
int
ufs_create(struct vop_old_create_args *ap)
{
	int error;

	error =
	    ufs_makeinode(MAKEIMODE(ap->a_vap->va_type, ap->a_vap->va_mode),
	    ap->a_dvp, ap->a_vpp, ap->a_cnp);
	if (error)
		return (error);
	VN_KNOTE(ap->a_dvp, NOTE_WRITE);
	return (0);
}

/*
 * Mknod vnode call
 *
 * ufs_mknod(struct vnode *a_dvp, struct vnode **a_vpp,
 *	     struct componentname *a_cnp, struct vattr *a_vap)
 */
/* ARGSUSED */
static
int
ufs_mknod(struct vop_old_mknod_args *ap)
{
	struct vattr *vap = ap->a_vap;
	struct vnode **vpp = ap->a_vpp;
	struct inode *ip;
	ino_t ino;
	int error;

	/*
	 * UFS cannot represent the entire major/minor range supported by
	 * the kernel.
	 */
	if (vap->va_rmajor != VNOVAL &&
	    makeudev(vap->va_rmajor, vap->va_rminor) == NOUDEV) {
		return(EINVAL);
	}

	/* no special directory support */
	if (vap->va_type == VDIR)
		return(EINVAL);

	error = ufs_makeinode(MAKEIMODE(vap->va_type, vap->va_mode),
	    ap->a_dvp, vpp, ap->a_cnp);
	if (error)
		return (error);
	VN_KNOTE(ap->a_dvp, NOTE_WRITE);
	ip = VTOI(*vpp);
	ip->i_flag |= IN_ACCESS | IN_CHANGE | IN_UPDATE;
	if (vap->va_rmajor != VNOVAL) {
		/*
		 * Want to be able to use this to make badblock
		 * inodes, so don't truncate the dev number.
		 */
		ip->i_rdev = makeudev(vap->va_rmajor, vap->va_rminor);
	}
	/*
	 * Remove inode, then reload it through VFS_VGET so it is
	 * checked to see if it is an alias of an existing entry in
	 * the inode cache.
	 */
	(*vpp)->v_type = VNON;
	ino = ip->i_number;	/* Save this before vgone() invalidates ip. */
	vgone_vxlocked(*vpp);
	vput(*vpp);
	error = VFS_VGET(ap->a_dvp->v_mount, NULL, ino, vpp);
	if (error) {
		*vpp = NULL;
		return (error);
	}
	return (0);
}

/*
 * Close called.
 *
 * Update the times on the inode.
 *
 * ufs_close(struct vnode *a_vp, int a_fflag)
 */
/* ARGSUSED */
static
int
ufs_close(struct vop_close_args *ap)
{
	struct vnode *vp = ap->a_vp;

	if (VREFCNT(vp) > 1)
		ufs_itimes(vp);
	return (vop_stdclose(ap));
}

/*
 * ufs_access(struct vnode *a_vp, int a_mode, struct ucred *a_cred)
 */
static
int
ufs_access(struct vop_access_args *ap)
{
	struct vnode *vp = ap->a_vp;
	struct inode *ip = VTOI(vp);
	int error;

#ifdef QUOTA
	if (ap->a_mode & VWRITE) {
		switch (vp->v_type) {
		case VDIR:
		case VLNK:
		case VREG:
			if ((error = ufs_getinoquota(ip)) != 0)
				return (error);
			break;
		default:
			break;
		}
	}
#endif

	error = vop_helper_access(ap, ip->i_uid, ip->i_gid, ip->i_mode, 0);
	return (error);
}

/*
 * ufs_getattr(struct vnode *a_vp, struct vattr *a_vap)
 */
/* ARGSUSED */
static
int
ufs_getattr(struct vop_getattr_args *ap)
{
	struct vnode *vp = ap->a_vp;
	struct inode *ip = VTOI(vp);
	struct vattr *vap = ap->a_vap;

	ufs_itimes(vp);
	/*
	 * Copy from inode table
	 */
	vap->va_fsid = dev2udev(ip->i_dev);
	vap->va_fileid = ip->i_number;
	vap->va_mode = ip->i_mode & ~IFMT;
	vap->va_nlink = VFSTOUFS(vp->v_mount)->um_i_effnlink_valid ?
	    ip->i_effnlink : ip->i_nlink;
	vap->va_uid = ip->i_uid;
	vap->va_gid = ip->i_gid;
	vap->va_rmajor = umajor(ip->i_rdev);
	vap->va_rminor = uminor(ip->i_rdev);
	vap->va_size = ip->i_din.di_size;
	vap->va_atime.tv_sec = ip->i_atime;
	vap->va_atime.tv_nsec = ip->i_atimensec;
	vap->va_mtime.tv_sec = ip->i_mtime;
	vap->va_mtime.tv_nsec = ip->i_mtimensec;
	vap->va_ctime.tv_sec = ip->i_ctime;
	vap->va_ctime.tv_nsec = ip->i_ctimensec;
	vap->va_flags = ip->i_flags;
	vap->va_gen = ip->i_gen;
	vap->va_blocksize = vp->v_mount->mnt_stat.f_iosize;
	vap->va_bytes = dbtob((u_quad_t)ip->i_blocks);
	vap->va_type = IFTOVT(ip->i_mode);
	vap->va_filerev = ip->i_modrev;
	return (0);
}

static
int
ufs_markatime(struct vop_markatime_args *ap)
{
	struct vnode *vp = ap->a_vp;
	struct inode *ip = VTOI(vp);

	if (vp->v_mount->mnt_flag & MNT_RDONLY)
		return (EROFS);
	if (vp->v_mount->mnt_flag & MNT_NOATIME)
		return (0);
	ip->i_flag |= IN_ACCESS;
	VN_KNOTE(vp, NOTE_ATTRIB);
	return (0);
}

/*
 * Set attribute vnode op. called from several syscalls
 *
 * ufs_setattr(struct vnode *a_vp, struct vattr *a_vap,
 *		struct ucred *a_cred)
 */
static
int
ufs_setattr(struct vop_setattr_args *ap)
{
	struct vattr *vap = ap->a_vap;
	struct vnode *vp = ap->a_vp;
	struct inode *ip = VTOI(vp);
	struct ucred *cred = ap->a_cred;
	int error;

	/*
	 * Check for unsettable attributes.
	 */
	if ((vap->va_type != VNON) || (vap->va_nlink != VNOVAL) ||
	    (vap->va_fsid != VNOVAL) || (vap->va_fileid != VNOVAL) ||
	    (vap->va_blocksize != VNOVAL) || (vap->va_rmajor != VNOVAL) ||
	    ((int)vap->va_bytes != VNOVAL) || (vap->va_gen != VNOVAL)) {
		return (EINVAL);
	}
	if (vap->va_flags != VNOVAL) {
		if (vp->v_mount->mnt_flag & MNT_RDONLY)
			return (EROFS);
		if (cred->cr_uid != ip->i_uid &&
		    (error = priv_check_cred(cred, PRIV_VFS_SETATTR, 0)))
			return (error);
		/*
		 * Note that a root chflags becomes a user chflags when
		 * we are jailed, unless the jail.chflags_allowed sysctl
		 * is set.
		 */
		if (cred->cr_uid == 0 && 
		    (!jailed(cred) || jail_chflags_allowed)) {
			if ((ip->i_flags
			    & (SF_NOUNLINK | SF_IMMUTABLE | SF_APPEND)) &&
			    securelevel > 0)
				return (EPERM);
			ip->i_flags = vap->va_flags;
		} else {
			if (ip->i_flags
			    & (SF_NOUNLINK | SF_IMMUTABLE | SF_APPEND) ||
			    (vap->va_flags & UF_SETTABLE) != vap->va_flags)
				return (EPERM);
			ip->i_flags &= SF_SETTABLE;
			ip->i_flags |= (vap->va_flags & UF_SETTABLE);
		}
		ip->i_flag |= IN_CHANGE;
		if (vap->va_flags & (IMMUTABLE | APPEND))
			return (0);
	}
	if (ip->i_flags & (IMMUTABLE | APPEND))
		return (EPERM);
	/*
	 * Go through the fields and update iff not VNOVAL.
	 */
	if (vap->va_uid != (uid_t)VNOVAL || vap->va_gid != (gid_t)VNOVAL) {
		if (vp->v_mount->mnt_flag & MNT_RDONLY)
			return (EROFS);
		if ((error = ufs_chown(vp, vap->va_uid, vap->va_gid, cred)) != 0)
			return (error);
	}
	if (vap->va_size != VNOVAL) {
		/*
		 * Disallow write attempts on read-only filesystems;
		 * unless the file is a socket, fifo, or a block or
		 * character device resident on the filesystem.
		 */
		switch (vp->v_type) {
		case VDIR:
			return (EISDIR);
		case VLNK:
		case VREG:
			if (vp->v_mount->mnt_flag & MNT_RDONLY)
				return (EROFS);
			break;
		default:
			break;
		}
		if ((error = ffs_truncate(vp, vap->va_size, 0, cred)) != 0)
			return (error);
	}
	ip = VTOI(vp);
	if (vap->va_atime.tv_sec != VNOVAL || vap->va_mtime.tv_sec != VNOVAL) {
		if (vp->v_mount->mnt_flag & MNT_RDONLY)
			return (EROFS);
		if (cred->cr_uid != ip->i_uid &&
		    (error = priv_check_cred(cred, PRIV_VFS_SETATTR, 0)) &&
		    ((vap->va_vaflags & VA_UTIMES_NULL) == 0 ||
		    (error = VOP_EACCESS(vp, VWRITE, cred))))
			return (error);
		if (vap->va_atime.tv_sec != VNOVAL)
			ip->i_flag |= IN_ACCESS;
		if (vap->va_mtime.tv_sec != VNOVAL)
			ip->i_flag |= IN_CHANGE | IN_UPDATE;
		ufs_itimes(vp);
		if (vap->va_atime.tv_sec != VNOVAL) {
			ip->i_atime = vap->va_atime.tv_sec;
			ip->i_atimensec = vap->va_atime.tv_nsec;
		}
		if (vap->va_mtime.tv_sec != VNOVAL) {
			ip->i_mtime = vap->va_mtime.tv_sec;
			ip->i_mtimensec = vap->va_mtime.tv_nsec;
		}
		error = ffs_update(vp, 0);
		if (error)
			return (error);
	}
	error = 0;
	if (vap->va_mode != (mode_t)VNOVAL) {
		if (vp->v_mount->mnt_flag & MNT_RDONLY)
			return (EROFS);
		error = ufs_chmod(vp, (int)vap->va_mode, cred);
	}
	VN_KNOTE(vp, NOTE_ATTRIB);
	return (error);
}

/*
 * Change the mode on a file.
 * Inode must be locked before calling.
 */
static int
ufs_chmod(struct vnode *vp, int mode, struct ucred *cred)
{
	struct inode *ip = VTOI(vp);
	int error;
	mode_t	cur_mode = ip->i_mode;

	error = vop_helper_chmod(vp, mode, cred, ip->i_uid, ip->i_gid,
				 &cur_mode);
	if (error)
		return (error);
#if 0
	if (cred->cr_uid != ip->i_uid) {
	    error = priv_check_cred(cred, PRIV_VFS_CHMOD, 0);
	    if (error)
		return (error);
	}
	if (cred->cr_uid) {
		if (vp->v_type != VDIR && (mode & S_ISTXT))
			return (EFTYPE);
		if (!groupmember(ip->i_gid, cred) && (mode & ISGID))
			return (EPERM);
	}
#endif
	ip->i_mode = cur_mode;
	ip->i_flag |= IN_CHANGE;
	return (0);
}

/*
 * Perform chown operation on inode ip;
 * inode must be locked prior to call.
 */
static int
ufs_chown(struct vnode *vp, uid_t uid, gid_t gid, struct ucred *cred)
{
	struct inode *ip = VTOI(vp);
	uid_t ouid;
	gid_t ogid;
	int error = 0;
#ifdef QUOTA
	int i;
	long change;
#endif

	if (uid == (uid_t)VNOVAL)
		uid = ip->i_uid;
	if (gid == (gid_t)VNOVAL)
		gid = ip->i_gid;
	/*
	 * If we don't own the file, are trying to change the owner
	 * of the file, or are not a member of the target group,
	 * the caller must be superuser or the call fails.
	 */
	if ((cred->cr_uid != ip->i_uid || uid != ip->i_uid ||
	    (gid != ip->i_gid && !(cred->cr_gid == gid ||
	    groupmember(gid, cred)))) &&
	    (error = priv_check_cred(cred, PRIV_VFS_CHOWN, 0)))
		return (error);
	ogid = ip->i_gid;
	ouid = ip->i_uid;
#ifdef QUOTA
	if ((error = ufs_getinoquota(ip)) != 0)
		return (error);
	if (ouid == uid) {
		ufs_dqrele(vp, ip->i_dquot[USRQUOTA]);
		ip->i_dquot[USRQUOTA] = NODQUOT;
	}
	if (ogid == gid) {
		ufs_dqrele(vp, ip->i_dquot[GRPQUOTA]);
		ip->i_dquot[GRPQUOTA] = NODQUOT;
	}
	change = ip->i_blocks;
	(void) ufs_chkdq(ip, -change, cred, CHOWN);
	(void) ufs_chkiq(ip, -1, cred, CHOWN);
	for (i = 0; i < MAXQUOTAS; i++) {
		ufs_dqrele(vp, ip->i_dquot[i]);
		ip->i_dquot[i] = NODQUOT;
	}
#endif
	ip->i_gid = gid;
	ip->i_uid = uid;
#ifdef QUOTA
	if ((error = ufs_getinoquota(ip)) == 0) {
		if (ouid == uid) {
			ufs_dqrele(vp, ip->i_dquot[USRQUOTA]);
			ip->i_dquot[USRQUOTA] = NODQUOT;
		}
		if (ogid == gid) {
			ufs_dqrele(vp, ip->i_dquot[GRPQUOTA]);
			ip->i_dquot[GRPQUOTA] = NODQUOT;
		}
		if ((error = ufs_chkdq(ip, change, cred, CHOWN)) == 0) {
			if ((error = ufs_chkiq(ip, 1, cred, CHOWN)) == 0)
				goto good;
			else
				(void)ufs_chkdq(ip, -change, cred, CHOWN|FORCE);
		}
		for (i = 0; i < MAXQUOTAS; i++) {
			ufs_dqrele(vp, ip->i_dquot[i]);
			ip->i_dquot[i] = NODQUOT;
		}
	}
	ip->i_gid = ogid;
	ip->i_uid = ouid;
	if (ufs_getinoquota(ip) == 0) {
		if (ouid == uid) {
			ufs_dqrele(vp, ip->i_dquot[USRQUOTA]);
			ip->i_dquot[USRQUOTA] = NODQUOT;
		}
		if (ogid == gid) {
			ufs_dqrele(vp, ip->i_dquot[GRPQUOTA]);
			ip->i_dquot[GRPQUOTA] = NODQUOT;
		}
		(void) ufs_chkdq(ip, change, cred, FORCE|CHOWN);
		(void) ufs_chkiq(ip, 1, cred, FORCE|CHOWN);
		(void) ufs_getinoquota(ip);
	}
	return (error);
good:
	if (ufs_getinoquota(ip))
		panic("ufs_chown: lost quota");
#endif /* QUOTA */
	ip->i_flag |= IN_CHANGE;
	if (cred->cr_uid != 0 && (ouid != uid || ogid != gid))
		ip->i_mode &= ~(ISUID | ISGID);
	return (0);
}

/*
 * Mmap a file
 *
 * NB Currently unsupported.
 *
 * ufs_mmap(struct vnode *a_vp, int a_fflags, struct ucred *a_cred)
 */
/* ARGSUSED */
static
int
ufs_mmap(struct vop_mmap_args *ap)
{
	return (EINVAL);
}

/*
 * ufs_remove(struct vnode *a_dvp, struct vnode *a_vp,
 *	      struct componentname *a_cnp)
 */
static
int
ufs_remove(struct vop_old_remove_args *ap)
{
	struct inode *ip;
	struct vnode *vp = ap->a_vp;
	struct vnode *dvp = ap->a_dvp;
	int error;

	ip = VTOI(vp);
#if 0	/* handled by kernel now */
	if ((ip->i_flags & (NOUNLINK | IMMUTABLE | APPEND)) ||
	    (VTOI(dvp)->i_flags & APPEND)) {
		error = EPERM;
		goto out;
	}
#endif
	error = ufs_dirremove(dvp, ip, ap->a_cnp->cn_flags, 0);
	VN_KNOTE(vp, NOTE_DELETE);
	VN_KNOTE(dvp, NOTE_WRITE);
#if 0
out:
#endif
	return (error);
}

/*
 * link vnode call
 *
 * ufs_link(struct vnode *a_tdvp, struct vnode *a_vp,
 *	    struct componentname *a_cnp)
 */
static
int
ufs_link(struct vop_old_link_args *ap)
{
	struct vnode *vp = ap->a_vp;
	struct vnode *tdvp = ap->a_tdvp;
	struct componentname *cnp = ap->a_cnp;
	struct inode *ip;
	struct direct newdir;
	int error;

	if (tdvp->v_mount != vp->v_mount) {
		error = EXDEV;
		goto out2;
	}
	if (tdvp != vp && (error = vn_lock(vp, LK_EXCLUSIVE))) {
		goto out2;
	}
	ip = VTOI(vp);
	if ((nlink_t)ip->i_nlink >= LINK_MAX) {
		error = EMLINK;
		goto out1;
	}
#if 0	/* handled by kernel now, also DragonFly allows this */
	if (ip->i_flags & (IMMUTABLE | APPEND)) {
		error = EPERM;
		goto out1;
	}
#endif
	ip->i_effnlink++;
	ip->i_nlink++;
	ip->i_flag |= IN_CHANGE;
	if (DOINGSOFTDEP(vp))
		softdep_change_linkcnt(ip);
	error = ffs_update(vp, !(DOINGSOFTDEP(vp) | DOINGASYNC(vp)));
	if (!error) {
		ufs_makedirentry(ip, cnp, &newdir);
		error = ufs_direnter(tdvp, vp, &newdir, cnp, NULL);
	}

	if (error) {
		ip->i_effnlink--;
		ip->i_nlink--;
		ip->i_flag |= IN_CHANGE;
		if (DOINGSOFTDEP(vp))
			softdep_change_linkcnt(ip);
	}
out1:
	if (tdvp != vp)
		vn_unlock(vp);
out2:
	VN_KNOTE(vp, NOTE_LINK);
	VN_KNOTE(tdvp, NOTE_WRITE);
	return (error);
}

/*
 * whiteout vnode call
 *
 * ufs_whiteout(struct vnode *a_dvp, struct componentname *a_cnp, int a_flags)
 */
static
int
ufs_whiteout(struct vop_old_whiteout_args *ap)
{
	struct vnode *dvp = ap->a_dvp;
	struct componentname *cnp = ap->a_cnp;
	struct direct newdir;
	int error = 0;

	switch (ap->a_flags) {
	case NAMEI_LOOKUP:
		/* 4.4 format directories support whiteout operations */
		if (dvp->v_mount->mnt_maxsymlinklen > 0)
			return (0);
		return (EOPNOTSUPP);

	case NAMEI_CREATE:
		/* create a new directory whiteout */
#ifdef DIAGNOSTIC
		if (dvp->v_mount->mnt_maxsymlinklen <= 0)
			panic("ufs_whiteout: old format filesystem");
#endif

		newdir.d_ino = WINO;
		newdir.d_namlen = cnp->cn_namelen;
		bcopy(cnp->cn_nameptr, newdir.d_name, (unsigned)cnp->cn_namelen + 1);
		newdir.d_type = DT_WHT;
		error = ufs_direnter(dvp, NULL, &newdir, cnp, NULL);
		break;

	case NAMEI_DELETE:
		/* remove an existing directory whiteout */
#ifdef DIAGNOSTIC
		if (dvp->v_mount->mnt_maxsymlinklen <= 0)
			panic("ufs_whiteout: old format filesystem");
#endif

		cnp->cn_flags &= ~CNP_DOWHITEOUT;
		error = ufs_dirremove(dvp, NULL, cnp->cn_flags, 0);
		break;
	default:
		panic("ufs_whiteout: unknown op");
	}
	return (error);
}

/*
 * Rename system call.
 * 	rename("foo", "bar");
 * is essentially
 *	unlink("bar");
 *	link("foo", "bar");
 *	unlink("foo");
 * but ``atomically''.  Can't do full commit without saving state in the
 * inode on disk which isn't feasible at this time.  Best we can do is
 * always guarantee the target exists.
 *
 * Basic algorithm is:
 *
 * 1) Bump link count on source while we're linking it to the
 *    target.  This also ensure the inode won't be deleted out
 *    from underneath us while we work (it may be truncated by
 *    a concurrent `trunc' or `open' for creation).
 * 2) Link source to destination.  If destination already exists,
 *    delete it first.
 * 3) Unlink source reference to inode if still around. If a
 *    directory was moved and the parent of the destination
 *    is different from the source, patch the ".." entry in the
 *    directory.
 *
 * ufs_rename(struct vnode *a_fdvp, struct vnode *a_fvp,
 *	      struct componentname *a_fcnp, struct vnode *a_tdvp,
 *	      struct vnode *a_tvp, struct componentname *a_tcnp)
 */
static
int
ufs_rename(struct vop_old_rename_args *ap)
{
	struct vnode *tvp = ap->a_tvp;
	struct vnode *tdvp = ap->a_tdvp;
	struct vnode *fvp = ap->a_fvp;
	struct vnode *fdvp = ap->a_fdvp;
	struct componentname *tcnp = ap->a_tcnp;
	struct componentname *fcnp = ap->a_fcnp;
	struct inode *ip, *xp, *dp;
	struct direct newdir;
	ino_t oldparent = 0, newparent = 0;
	int doingdirectory = 0;
	int error = 0, ioflag;

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

#if 0	/* handled by kernel now */
	if (tvp && ((VTOI(tvp)->i_flags & (NOUNLINK | IMMUTABLE | APPEND)) ||
	    (VTOI(tdvp)->i_flags & APPEND))) {
		error = EPERM;
		goto abortit;
	}
#endif

	/*
	 * Renaming a file to itself has no effect.  The upper layers should
	 * not call us in that case.  Temporarily just warn if they do.
	 */
	if (fvp == tvp) {
		kprintf("ufs_rename: fvp == tvp (can't happen)\n");
		error = 0;
		goto abortit;
	}

	if ((error = vn_lock(fvp, LK_EXCLUSIVE)) != 0)
		goto abortit;

	/*
	 * Note: now that fvp is locked we have to be sure to unlock it before
	 * using the 'abortit' target.
	 */
	dp = VTOI(fdvp);
	ip = VTOI(fvp);
	if (ip->i_nlink >= LINK_MAX) {
		vn_unlock(fvp);
		error = EMLINK;
		goto abortit;
	}
#if 0	/* handled by kernel now */
	if ((ip->i_flags & (NOUNLINK | IMMUTABLE | APPEND))
	    || (dp->i_flags & APPEND)) {
		vn_unlock(fvp);
		error = EPERM;
		goto abortit;
	}
#endif
	if ((ip->i_mode & IFMT) == IFDIR) {
		/*
		 * Avoid ".", "..", and aliases of "." for obvious reasons.
		 */
		if ((fcnp->cn_namelen == 1 && fcnp->cn_nameptr[0] == '.') ||
		    dp == ip || (fcnp->cn_flags | tcnp->cn_flags) & CNP_ISDOTDOT ||
		    (ip->i_flag & IN_RENAME)) {
			vn_unlock(fvp);
			error = EINVAL;
			goto abortit;
		}
		ip->i_flag |= IN_RENAME;
		oldparent = dp->i_number;
		doingdirectory = 1;
	}
	VN_KNOTE(fdvp, NOTE_WRITE);		/* XXX right place? */

	/*
	 * fvp still locked.  ip->i_flag has IN_RENAME set if doingdirectory.
	 * Cleanup fvp requirements so we can unlock it.
	 *
	 * tvp and tdvp are locked.  tvp may be NULL.  Now that dp and xp
	 * is setup we can use the 'bad' target if we unlock fvp.  We cannot
	 * use the abortit target anymore because of IN_RENAME.
	 */
	dp = VTOI(tdvp);
	if (tvp)
		xp = VTOI(tvp);
	else
		xp = NULL;

	/*
	 * 1) Bump link count while we're moving stuff
	 *    around.  If we crash somewhere before
	 *    completing our work, the link count
	 *    may be wrong, but correctable.
	 */
	ip->i_effnlink++;
	ip->i_nlink++;
	ip->i_flag |= IN_CHANGE;
	if (DOINGSOFTDEP(fvp))
		softdep_change_linkcnt(ip);
	if ((error = ffs_update(fvp, !(DOINGSOFTDEP(fvp) |
				       DOINGASYNC(fvp)))) != 0) {
		vn_unlock(fvp);
		goto bad;
	}

	/*
	 * If ".." must be changed (ie the directory gets a new
	 * parent) then the source directory must not be in the
	 * directory heirarchy above the target, as this would
	 * orphan everything below the source directory. Also
	 * the user must have write permission in the source so
	 * as to be able to change "..". We must repeat the call
	 * to namei, as the parent directory is unlocked by the
	 * call to checkpath().
	 */
	error = VOP_EACCESS(fvp, VWRITE, tcnp->cn_cred);
	vn_unlock(fvp);

	/*
	 * We are now back to where we were in that fvp, fdvp are unlocked
	 * and tvp, tdvp are locked.  tvp may be NULL.  IN_RENAME may be
	 * set.  Only the bad target or, if we clean up tvp and tdvp, the
	 * out target, may be used.
	 */
	if (oldparent != dp->i_number)
		newparent = dp->i_number;
	if (doingdirectory && newparent) {
		if (error)	/* write access check above */
			goto bad;

		/*
		 * Once we start messing with tvp and tdvp we cannot use the
		 * 'bad' target, only finish cleaning tdvp and tvp up and
		 * use the 'out' target.
		 *
		 * This cleans up tvp.
		 */
		if (xp != NULL) {
			vput(tvp);
			xp = NULL;
		}

		/*
		 * This is a real mess. ufs_checkpath vput's the target
		 * directory so retain an extra ref and note that tdvp will
		 * lose its lock on return.  This leaves us with one good
		 * ref after ufs_checkpath returns.
		 */
		vref(tdvp);
		error = ufs_checkpath(ip, dp, tcnp->cn_cred);
		tcnp->cn_flags |= CNP_PDIRUNLOCK;
		if (error) {
			vrele(tdvp);
			goto out;
	        }

		/*
		 * relookup no longer messes with tdvp's refs. tdvp must be
		 * unlocked on entry and will be locked on a successful
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
		KKASSERT((tcnp->cn_flags & CNP_PDIRUNLOCK) == 0);
		dp = VTOI(tdvp);
		if (tvp)
			xp = VTOI(tvp);
	}

	/*
	 * We are back to fvp, fdvp unlocked, tvp, tdvp locked.  tvp may 
	 * be NULL (xp will also be NULL in that case), and IN_RENAME will
	 * be set if doingdirectory.  This means we can use the 'bad' target
	 * again.
	 */

	/*
	 * 2) If target doesn't exist, link the target
	 *    to the source and unlink the source.
	 *    Otherwise, rewrite the target directory
	 *    entry to reference the source inode and
	 *    expunge the original entry's existence.
	 */
	if (xp == NULL) {
		if (dp->i_dev != ip->i_dev)
			panic("ufs_rename: EXDEV");
		/*
		 * Account for ".." in new directory.
		 * When source and destination have the same
		 * parent we don't fool with the link count.
		 */
		if (doingdirectory && newparent) {
			if ((nlink_t)dp->i_nlink >= LINK_MAX) {
				error = EMLINK;
				goto bad;
			}
			dp->i_effnlink++;
			dp->i_nlink++;
			dp->i_flag |= IN_CHANGE;
			if (DOINGSOFTDEP(tdvp))
				softdep_change_linkcnt(dp);
			error = ffs_update(tdvp, !(DOINGSOFTDEP(tdvp) |
						   DOINGASYNC(tdvp)));
			if (error)
				goto bad;
		}
		ufs_makedirentry(ip, tcnp, &newdir);
		error = ufs_direnter(tdvp, NULL, &newdir, tcnp, NULL);
		if (error) {
			if (doingdirectory && newparent) {
				dp->i_effnlink--;
				dp->i_nlink--;
				dp->i_flag |= IN_CHANGE;
				if (DOINGSOFTDEP(tdvp))
					softdep_change_linkcnt(dp);
				(void)ffs_update(tdvp, 1);
			}
			goto bad;
		}
		VN_KNOTE(tdvp, NOTE_WRITE);
		vput(tdvp);
	} else {
		if (xp->i_dev != dp->i_dev || xp->i_dev != ip->i_dev)
			panic("ufs_rename: EXDEV");
		/*
		 * Short circuit rename(foo, foo).
		 */
		if (xp->i_number == ip->i_number)
			panic("ufs_rename: same file");
		/*
		 * If the parent directory is "sticky", then the user must
		 * own the parent directory, or the destination of the rename,
		 * otherwise the destination may not be changed (except by
		 * root). This implements append-only directories.
		 */
		if ((dp->i_mode & S_ISTXT) && tcnp->cn_cred->cr_uid != 0 &&
		    tcnp->cn_cred->cr_uid != dp->i_uid &&
		    xp->i_uid != tcnp->cn_cred->cr_uid) {
			error = EPERM;
			goto bad;
		}
		/*
		 * Target must be empty if a directory and have no links
		 * to it. Also, ensure source and target are compatible
		 * (both directories, or both not directories).
		 *
		 * Purge the file or directory being replaced from the
		 * nameccache.
		 */
		if ((xp->i_mode&IFMT) == IFDIR) {
			if ((xp->i_effnlink > 2) ||
			    !ufs_dirempty(xp, dp->i_number, tcnp->cn_cred)) {
				error = ENOTEMPTY;
				goto bad;
			}
			if (!doingdirectory) {
				error = ENOTDIR;
				goto bad;
			}
			/* cache_purge removed - handled by VFS compat layer */
		} else if (doingdirectory == 0) {
			/* cache_purge removed - handled by VFS compat layer */
		} else {
			error = EISDIR;
			goto bad;
		}
		/*
		 * note: inode passed to ufs_dirrewrite() is 0 for a 
		 * non-directory file rename, 1 for a directory rename
		 * in the same directory, and > 1 for an inode representing
		 * the new directory.
		 */
		error = ufs_dirrewrite(dp, xp, ip->i_number,
		    IFTODT(ip->i_mode),
		    (doingdirectory && newparent) ?
			newparent : (ino_t)doingdirectory);
		if (error)
			goto bad;
		if (doingdirectory) {
			if (!newparent) {
				dp->i_effnlink--;
				if (DOINGSOFTDEP(tdvp))
					softdep_change_linkcnt(dp);
			}
			xp->i_effnlink--;
			if (DOINGSOFTDEP(tvp))
				softdep_change_linkcnt(xp);
		}
		if (doingdirectory && !DOINGSOFTDEP(tvp)) {
			/*
			 * Truncate inode. The only stuff left in the directory
			 * is "." and "..". The "." reference is inconsequential
			 * since we are quashing it. We have removed the "."
			 * reference and the reference in the parent directory,
			 * but there may be other hard links. The soft
			 * dependency code will arrange to do these operations
			 * after the parent directory entry has been deleted on
			 * disk, so when running with that code we avoid doing
			 * them now.
			 */
			if (!newparent) {
				dp->i_nlink--;
				dp->i_flag |= IN_CHANGE;
			}
			xp->i_nlink--;
			xp->i_flag |= IN_CHANGE;
			ioflag = DOINGASYNC(tvp) ? 0 : IO_SYNC;
			error = ffs_truncate(tvp, (off_t)0, ioflag,
					     tcnp->cn_cred);
			if (error)
				goto bad;
		}
		VN_KNOTE(tdvp, NOTE_WRITE);
		vput(tdvp);
		VN_KNOTE(tvp, NOTE_DELETE);
		vput(tvp);
		xp = NULL;
	}

	/*
	 * tvp and tdvp have been cleaned up.  only fvp and fdvp (both
	 * unlocked) remain.  We are about to overwrite fvp but we have to
	 * keep 'ip' intact so we cannot release the old fvp, which is still
	 * refd and accessible via ap->a_fvp.
	 *
	 * This means we cannot use either 'bad' or 'out' to cleanup any 
	 * more.
	 */

	/*
	 * 3) Unlink the source.
	 */
	fcnp->cn_flags &= ~CNP_MODMASK;
	fcnp->cn_flags |= CNP_LOCKPARENT;
	error = relookup(fdvp, &fvp, fcnp);
	if (error || fvp == NULL) {
		/*
		 * From name has disappeared.  IN_RENAME will not be set if
		 * we get past the panic so we don't have to clean it up.
		 */
		if (doingdirectory)
			panic("ufs_rename: lost dir entry");
		vrele(ap->a_fvp);
		if (fcnp->cn_flags & CNP_PDIRUNLOCK)
			vrele(fdvp);
		else
			vput(fdvp);
		return(0);
	}
	KKASSERT((fcnp->cn_flags & CNP_PDIRUNLOCK) == 0);

	/*
	 * fdvp and fvp are locked.
	 */
	xp = VTOI(fvp);
	dp = VTOI(fdvp);

	/*
	 * Ensure that the directory entry still exists and has not
	 * changed while the new name has been entered. If the source is
	 * a file then the entry may have been unlinked or renamed. In
	 * either case there is no further work to be done. If the source
	 * is a directory then it cannot have been rmdir'ed; the IN_RENAME
	 * flag ensures that it cannot be moved by another rename or removed
	 * by a rmdir.  Cleanup IN_RENAME.
	 */
	if (xp != ip) {
		if (doingdirectory)
			panic("ufs_rename: lost dir entry");
	} else {
		/*
		 * If the source is a directory with a
		 * new parent, the link count of the old
		 * parent directory must be decremented
		 * and ".." set to point to the new parent.
		 */
		if (doingdirectory && newparent) {
			xp->i_offset = mastertemplate.dot_reclen;
			ufs_dirrewrite(xp, dp, newparent, DT_DIR, 0);
			/* cache_purge removed - handled by VFS compat layer */
		}
		error = ufs_dirremove(fdvp, xp, fcnp->cn_flags, 0);
		xp->i_flag &= ~IN_RENAME;
	}

	VN_KNOTE(fvp, NOTE_RENAME);
	vput(fdvp);
	vput(fvp);
	vrele(ap->a_fvp);
	return (error);

bad:
	if (xp)
		vput(ITOV(xp));
	vput(ITOV(dp));
out:
	if (doingdirectory)
		ip->i_flag &= ~IN_RENAME;
	if (vn_lock(fvp, LK_EXCLUSIVE) == 0) {
		ip->i_effnlink--;
		ip->i_nlink--;
		ip->i_flag |= IN_CHANGE;
		ip->i_flag &= ~IN_RENAME;
		if (DOINGSOFTDEP(fvp))
			softdep_change_linkcnt(ip);
		vput(fvp);
	} else {
		vrele(fvp);
	}
	return (error);
}

/*
 * Mkdir system call
 *
 * ufs_mkdir(struct vnode *a_dvp, struct vnode **a_vpp,
 *	     struct componentname *a_cnp, struct vattr *a_vap)
 */
static
int
ufs_mkdir(struct vop_old_mkdir_args *ap)
{
	struct vnode *dvp = ap->a_dvp;
	struct vattr *vap = ap->a_vap;
	struct componentname *cnp = ap->a_cnp;
	struct inode *ip, *dp;
	struct vnode *tvp;
	struct buf *bp;
	struct dirtemplate dirtemplate, *dtp;
	struct direct newdir;
	int error, dmode;
	long blkoff;

	dp = VTOI(dvp);
	if ((nlink_t)dp->i_nlink >= LINK_MAX) {
		error = EMLINK;
		goto out;
	}
	dmode = vap->va_mode & 0777;
	dmode |= IFDIR;
	/*
	 * Must simulate part of ufs_makeinode here to acquire the inode,
	 * but not have it entered in the parent directory. The entry is
	 * made later after writing "." and ".." entries.
	 */
	error = ffs_valloc(dvp, dmode, cnp->cn_cred, &tvp);
	if (error)
		goto out;
	ip = VTOI(tvp);
	ip->i_gid = dp->i_gid;
#ifdef SUIDDIR
	{
#ifdef QUOTA
		struct ucred ucred, *ucp;
		ucp = cnp->cn_cred;
#endif
		/*
		 * If we are hacking owners here, (only do this where told to)
		 * and we are not giving it TO root, (would subvert quotas)
		 * then go ahead and give it to the other user.
		 * The new directory also inherits the SUID bit.
		 * If user's UID and dir UID are the same,
		 * 'give it away' so that the SUID is still forced on.
		 */
		if ((dvp->v_mount->mnt_flag & MNT_SUIDDIR) &&
		    (dp->i_mode & ISUID) && dp->i_uid) {
			dmode |= ISUID;
			ip->i_uid = dp->i_uid;
#ifdef QUOTA
			if (dp->i_uid != cnp->cn_cred->cr_uid) {
				/*
				 * Make sure the correct user gets charged
				 * for the space.
				 * Make a dummy credential for the victim.
				 * XXX This seems to never be accessed out of
				 * our context so a stack variable is ok.
				 */
				ucred.cr_ref = 1;
				ucred.cr_uid = ip->i_uid;
				ucred.cr_ngroups = 1;
				ucred.cr_groups[0] = dp->i_gid;
				ucp = &ucred;
			}
#endif
		} else
			ip->i_uid = cnp->cn_cred->cr_uid;
#ifdef QUOTA
		if ((error = ufs_getinoquota(ip)) ||
	    	    (error = ufs_chkiq(ip, 1, ucp, 0))) {
			ffs_vfree(tvp, ip->i_number, dmode);
			vput(tvp);
			return (error);
		}
#endif
	}
#else	/* !SUIDDIR */
	ip->i_uid = cnp->cn_cred->cr_uid;
#ifdef QUOTA
	if ((error = ufs_getinoquota(ip)) ||
	    (error = ufs_chkiq(ip, 1, cnp->cn_cred, 0))) {
		ffs_vfree(tvp, ip->i_number, dmode);
		vput(tvp);
		return (error);
	}
#endif
#endif	/* !SUIDDIR */
	ip->i_flag |= IN_ACCESS | IN_CHANGE | IN_UPDATE;
	ip->i_mode = dmode;
	tvp->v_type = VDIR;	/* Rest init'd in getnewvnode(). */
	ip->i_effnlink = 2;
	ip->i_nlink = 2;
	if (DOINGSOFTDEP(tvp))
		softdep_change_linkcnt(ip);
	if (cnp->cn_flags & CNP_ISWHITEOUT)
		ip->i_flags |= UF_OPAQUE;

	/*
	 * Bump link count in parent directory to reflect work done below.
	 * Should be done before reference is created so cleanup is
	 * possible if we crash.
	 */
	dp->i_effnlink++;
	dp->i_nlink++;
	dp->i_flag |= IN_CHANGE;
	if (DOINGSOFTDEP(dvp))
		softdep_change_linkcnt(dp);
	error = ffs_update(tvp, !(DOINGSOFTDEP(dvp) | DOINGASYNC(dvp)));
	if (error)
		goto bad;

	/*
	 * The vnode must have a VM object in order to issue buffer cache
	 * ops on it.
	 */
	vinitvmio(tvp, DIRBLKSIZ, DIRBLKSIZ, -1);

	/*
	 * Initialize directory with "." and ".." from static template.
	 */
	if (dvp->v_mount->mnt_maxsymlinklen > 0)
		dtp = &mastertemplate;
	else
		dtp = (struct dirtemplate *)&omastertemplate;
	dirtemplate = *dtp;
	dirtemplate.dot_ino = ip->i_number;
	dirtemplate.dotdot_ino = dp->i_number;
	nvnode_pager_setsize(tvp, DIRBLKSIZ, DIRBLKSIZ, -1);
	error = VOP_BALLOC(tvp, 0LL, DIRBLKSIZ, cnp->cn_cred, B_CLRBUF, &bp);
	if (error)
		goto bad;
	ip->i_size = DIRBLKSIZ;
	ip->i_flag |= IN_CHANGE | IN_UPDATE;
	bcopy((caddr_t)&dirtemplate, (caddr_t)bp->b_data, sizeof dirtemplate);
	if (DOINGSOFTDEP(tvp)) {
		/*
		 * Ensure that the entire newly allocated block is a
		 * valid directory so that future growth within the
		 * block does not have to ensure that the block is
		 * written before the inode.
		 */
		blkoff = DIRBLKSIZ;
		while (blkoff < bp->b_bcount) {
			((struct direct *)
			   (bp->b_data + blkoff))->d_reclen = DIRBLKSIZ;
			blkoff += DIRBLKSIZ;
		}
	}
	if ((error = ffs_update(tvp, !(DOINGSOFTDEP(tvp) |
				       DOINGASYNC(tvp)))) != 0) {
		bwrite(bp);
		goto bad;
	}
	/*
	 * Directory set up, now install its entry in the parent directory.
	 *
	 * If we are not doing soft dependencies, then we must write out the
	 * buffer containing the new directory body before entering the new 
	 * name in the parent. If we are doing soft dependencies, then the
	 * buffer containing the new directory body will be passed to and
	 * released in the soft dependency code after the code has attached
	 * an appropriate ordering dependency to the buffer which ensures that
	 * the buffer is written before the new name is written in the parent.
	 */
	if (DOINGASYNC(dvp))
		bdwrite(bp);
	else if (!DOINGSOFTDEP(dvp) && (error = bwrite(bp)) != 0)
		goto bad;
	ufs_makedirentry(ip, cnp, &newdir);
	error = ufs_direnter(dvp, tvp, &newdir, cnp, bp);
	
bad:
	if (error == 0) {
		VN_KNOTE(dvp, NOTE_WRITE | NOTE_LINK);
		*ap->a_vpp = tvp;
	} else {
		dp->i_effnlink--;
		dp->i_nlink--;
		dp->i_flag |= IN_CHANGE;
		if (DOINGSOFTDEP(dvp))
			softdep_change_linkcnt(dp);
		/*
		 * No need to do an explicit VOP_TRUNCATE here, vrele will
		 * do this for us because we set the link count to 0.
		 */
		ip->i_effnlink = 0;
		ip->i_nlink = 0;
		ip->i_flag |= IN_CHANGE;
		if (DOINGSOFTDEP(tvp))
			softdep_change_linkcnt(ip);
		vput(tvp);
	}
out:
	return (error);
}

/*
 * Rmdir system call.
 *
 * ufs_rmdir(struct vnode *a_dvp, struct vnode *a_vp,
 *	     struct componentname *a_cnp)
 */
static
int
ufs_rmdir(struct vop_old_rmdir_args *ap)
{
	struct vnode *vp = ap->a_vp;
	struct vnode *dvp = ap->a_dvp;
	struct componentname *cnp = ap->a_cnp;
	struct inode *ip, *dp;
	int error, ioflag;

	ip = VTOI(vp);
	dp = VTOI(dvp);

	/*
	 * Do not remove a directory that is in the process of being renamed.
	 * Verify the directory is empty (and valid). Rmdir ".." will not be
	 * valid since ".." will contain a reference to the current directory
	 * and thus be non-empty. Do not allow the removal of mounted on
	 * directories (this can happen when an NFS exported filesystem
	 * tries to remove a locally mounted on directory).
	 */
	error = 0;
	if (ip->i_flag & IN_RENAME) {
		error = EINVAL;
		goto out;
	}
	if (ip->i_effnlink != 2 ||
	    !ufs_dirempty(ip, dp->i_number, cnp->cn_cred)) {
		error = ENOTEMPTY;
		goto out;
	}
#if 0	/* handled by kernel now */
	if ((dp->i_flags & APPEND)
	    || (ip->i_flags & (NOUNLINK | IMMUTABLE | APPEND))) {
		error = EPERM;
		goto out;
	}
#endif
	/*
	 * Delete reference to directory before purging
	 * inode.  If we crash in between, the directory
	 * will be reattached to lost+found,
	 */
	dp->i_effnlink--;
	ip->i_effnlink--;
	if (DOINGSOFTDEP(vp)) {
		softdep_change_linkcnt(dp);
		softdep_change_linkcnt(ip);
	}
	error = ufs_dirremove(dvp, ip, cnp->cn_flags, 1);
	if (error) {
		dp->i_effnlink++;
		ip->i_effnlink++;
		if (DOINGSOFTDEP(vp)) {
			softdep_change_linkcnt(dp);
			softdep_change_linkcnt(ip);
		}
		goto out;
	}
	VN_KNOTE(dvp, NOTE_WRITE | NOTE_LINK);
	/*
	 * Truncate inode. The only stuff left in the directory is "." and
	 * "..". The "." reference is inconsequential since we are quashing
	 * it. The soft dependency code will arrange to do these operations
	 * after the parent directory entry has been deleted on disk, so
	 * when running with that code we avoid doing them now.
	 */
	if (!DOINGSOFTDEP(vp)) {
		dp->i_nlink--;
		dp->i_flag |= IN_CHANGE;
		ip->i_nlink--;
		ip->i_flag |= IN_CHANGE;
		ioflag = DOINGASYNC(vp) ? 0 : IO_SYNC;
		error = ffs_truncate(vp, (off_t)0, ioflag, cnp->cn_cred);
	}
	/* cache_purge removed - handled by VFS compat layer */
#ifdef UFS_DIRHASH
	/* Kill any active hash; i_effnlink == 0, so it will not come back. */
	if (ip->i_dirhash != NULL)
		ufsdirhash_free(ip);
#endif
out:
	VN_KNOTE(vp, NOTE_DELETE);
	return (error);
}

/*
 * symlink -- make a symbolic link
 *
 * ufs_symlink(struct vnode *a_dvp, struct vnode **a_vpp,
 *		struct componentname *a_cnp, struct vattr *a_vap,
 *		char *a_target)
 */
static
int
ufs_symlink(struct vop_old_symlink_args *ap)
{
	struct vnode *vp, **vpp = ap->a_vpp;
	struct inode *ip;
	int len, error;

	error = ufs_makeinode(IFLNK | ap->a_vap->va_mode, ap->a_dvp,
			      vpp, ap->a_cnp);
	if (error)
		return (error);
	VN_KNOTE(ap->a_dvp, NOTE_WRITE);
	vp = *vpp;
	len = strlen(ap->a_target);
	if (len < vp->v_mount->mnt_maxsymlinklen) {
		ip = VTOI(vp);
		bcopy(ap->a_target, (char *)ip->i_shortlink, len);
		ip->i_size = len;
		ip->i_flag |= IN_CHANGE | IN_UPDATE;
	} else {
		/*
		 * Make sure we have a VM object in order to use
		 * the buffer cache.
		 */
		if (vp->v_object == NULL)
			vinitvmio(vp, 0, PAGE_SIZE, -1);
		error = vn_rdwr(UIO_WRITE, vp, ap->a_target, len, (off_t)0,
				UIO_SYSSPACE, IO_NODELOCKED, 
				ap->a_cnp->cn_cred, NULL);
	}
	if (error)
		vput(vp);
	return (error);
}

/*
 * Vnode op for reading directories.
 *
 * ufs_readdir(struct vnode *a_vp, struct uio *a_uio, struct ucred *a_cred,
 *		int *a_eofflag, int *ncookies, off_t **a_cookies)
 */
static
int
ufs_readdir(struct vop_readdir_args *ap)
{
	struct uio *uio = ap->a_uio;
	struct vnode *vp = ap->a_vp;
	struct direct *dp;
	struct buf *bp;
	int retval;
	int error;
	int offset;	/* offset into buffer cache buffer */
	int eoffset;	/* end of buffer clipped to file EOF */
	int pickup;	/* pickup point */
	int ncookies;
	int cookie_index;
	off_t *cookies;

	if (uio->uio_offset < 0)
		return (EINVAL);
	/*
	 * Guess the number of cookies needed.  Make sure we compute at
	 * least 1, and no more then a reasonable limit.
	 */
	if (ap->a_ncookies) {
		ncookies = uio->uio_resid / 16 + 1;
		if (ncookies > 1024)
			ncookies = 1024;
		cookies = kmalloc(ncookies * sizeof(off_t), M_TEMP, M_WAITOK);
	} else {
		ncookies = -1;	/* force conditionals below */
		cookies = NULL;
	}
	cookie_index = 0;

	if ((error = vn_lock(vp, LK_EXCLUSIVE | LK_RETRY)) != 0)
		return (error);

	/*
	 * Past or at EOF
	 */
	if (uio->uio_offset >= VTOI(vp)->i_size) {
		if (ap->a_eofflag)
			*ap->a_eofflag = 1;
		if (ap->a_ncookies) {
			*ap->a_ncookies = cookie_index;
			*ap->a_cookies = cookies;
		}
		goto done;
	}

	/*
	 * Loop until we run out of cookies, we run out of user buffer,
	 * or we hit the directory EOF.
	 *
	 * Always start scans at the beginning of the buffer, don't trust
	 * the offset supplied by userland.
	 */
	while ((error = ffs_blkatoff_ra(vp, uio->uio_offset, NULL, &bp, 2)) == 0) {
		pickup = (int)(uio->uio_offset - bp->b_loffset);
		offset = 0;
		retval = 0;
		if (bp->b_loffset + bp->b_bcount > VTOI(vp)->i_size)
			eoffset = (int)(VTOI(vp)->i_size - bp->b_loffset);
		else
			eoffset = bp->b_bcount;

		while (offset < eoffset) {
			dp = (struct direct *)(bp->b_data + offset);
			if (dp->d_reclen <= 0 || (dp->d_reclen & 3) ||
			    offset + dp->d_reclen > bp->b_bcount) {
				error = EIO;
				break;
			}
			if (offsetof(struct direct, d_name[dp->d_namlen]) >				     dp->d_reclen) {
				error = EIO;
				break;
			}
			if (offset < pickup) {
				offset += dp->d_reclen;
				continue;
			}
#if BYTE_ORDER == LITTLE_ENDIAN
			if (OFSFMT(vp)) {
				retval = vop_write_dirent(&error, uio,
				    dp->d_ino, dp->d_namlen, dp->d_type,
				    dp->d_name);
			} else
#endif
			{
				retval = vop_write_dirent(&error, uio,
				    dp->d_ino, dp->d_type, dp->d_namlen,
				    dp->d_name);
			}
			if (retval)
				break;
			if (cookies)
				cookies[cookie_index] = bp->b_loffset + offset;
			++cookie_index;
			offset += dp->d_reclen;
			if (cookie_index == ncookies)
				break;
		}

		/*
		 * This will align the next loop to the beginning of the
		 * next block, and pickup will calculate to 0.
		 */
		uio->uio_offset = bp->b_loffset + offset;
		brelse(bp);

		if (retval || error || cookie_index == ncookies ||
		    uio->uio_offset >= VTOI(vp)->i_size) {
			break;
		}
	}
	if (ap->a_eofflag)
		*ap->a_eofflag = VTOI(vp)->i_size <= uio->uio_offset;

	/*
	 * Report errors only if we didn't manage to read anything
	 */
	if (error && cookie_index == 0) {
		if (cookies) {
			kfree(cookies, M_TEMP);
			*ap->a_ncookies = 0;
			*ap->a_cookies = NULL;
		}
	} else {
		error = 0;
		if (cookies) {
			*ap->a_ncookies = cookie_index;
			*ap->a_cookies = cookies;
		}
	}
done:
	vn_unlock(vp);
        return (error);
}

/*
 * Return target name of a symbolic link
 *
 * ufs_readlink(struct vnode *a_vp, struct uio *a_uio, struct ucred *a_cred)
 */
static
int
ufs_readlink(struct vop_readlink_args *ap)
{
	struct vnode *vp = ap->a_vp;
	struct inode *ip = VTOI(vp);
	int isize;

	isize = ip->i_size;
	if ((isize < vp->v_mount->mnt_maxsymlinklen) ||
	    (ip->i_din.di_blocks == 0)) {   /* XXX - for old fastlink support */
		uiomove((char *)ip->i_shortlink, isize, ap->a_uio);
		return (0);
	}

	/*
	 * Perform the equivalent of an OPEN on vp so we can issue a
	 * VOP_READ.
	 */
	return (VOP_READ(vp, ap->a_uio, 0, ap->a_cred));
}

/*
 * Calculate the logical to physical mapping if not done already,
 * then call the device strategy routine.
 *
 * In order to be able to swap to a file, the VOP_BMAP operation may not
 * deadlock on memory.  See ufs_bmap() for details.
 *
 * ufs_strategy(struct vnode *a_vp, struct bio *a_bio)
 */
static
int
ufs_strategy(struct vop_strategy_args *ap)
{
	struct bio *bio = ap->a_bio;
	struct bio *nbio;
	struct buf *bp = bio->bio_buf;
	struct vnode *vp = ap->a_vp;
	struct inode *ip;
	int error;

	ip = VTOI(vp);
	if (vp->v_type == VBLK || vp->v_type == VCHR)
		panic("ufs_strategy: spec");
	nbio = push_bio(bio);
	if (nbio->bio_offset == NOOFFSET) {
		error = VOP_BMAP(vp, bio->bio_offset, &nbio->bio_offset,
				 NULL, NULL, bp->b_cmd);
		if (error) {
			bp->b_error = error;
			bp->b_flags |= B_ERROR;
			/* I/O was never started on nbio, must biodone(bio) */
			biodone(bio);
			return (error);
		}
		if (nbio->bio_offset == NOOFFSET)
			vfs_bio_clrbuf(bp);
	}
	if (nbio->bio_offset == NOOFFSET) {
		/*
		 * We hit a hole in the file.  The buffer has been zero-filled
		 * so just biodone() it.
		 */
		biodone(bio);
	} else {
		vn_strategy(ip->i_devvp, nbio);
	}
	return (0);
}

/*
 * Print out the contents of an inode.
 *
 * ufs_print(struct vnode *a_vp)
 */
static
int
ufs_print(struct vop_print_args *ap)
{
	struct vnode *vp = ap->a_vp;
	struct inode *ip = VTOI(vp);

	kprintf("tag VT_UFS, ino %lu, on dev %s (%d, %d)",
	    (u_long)ip->i_number, devtoname(ip->i_dev), major(ip->i_dev),
	    minor(ip->i_dev));
	if (vp->v_type == VFIFO)
		fifo_printinfo(vp);
	lockmgr_printinfo(&vp->v_lock);
	kprintf("\n");
	return (0);
}

/*
 * Read wrapper for fifos.
 *
 * ufsfifo_read(struct vnode *a_vp, struct uio *a_uio, int a_ioflag,
 *		struct ucred *a_cred)
 */
static
int
ufsfifo_read(struct vop_read_args *ap)
{
	int error, resid;
	struct inode *ip;
	struct uio *uio;

	uio = ap->a_uio;
	resid = uio->uio_resid;
	error = VOCALL(&fifo_vnode_vops, &ap->a_head);
	ip = VTOI(ap->a_vp);
	if ((ap->a_vp->v_mount->mnt_flag & MNT_NOATIME) == 0 && ip != NULL &&
	    (uio->uio_resid != resid || (error == 0 && resid != 0)))
		VTOI(ap->a_vp)->i_flag |= IN_ACCESS;
	return (error);
}

/*
 * Write wrapper for fifos.
 *
 * ufsfifo_write(struct vnode *a_vp, struct uio *a_uio, int a_ioflag,
 *		 struct ucred *a_cred)
 */
static
int
ufsfifo_write(struct vop_write_args *ap)
{
	int error, resid;
	struct inode *ip;
	struct uio *uio;

	uio = ap->a_uio;
	resid = uio->uio_resid;
	error = VOCALL(&fifo_vnode_vops, &ap->a_head);
	ip = VTOI(ap->a_vp);
	if (ip != NULL && (uio->uio_resid != resid || (error == 0 && resid != 0)))
		VTOI(ap->a_vp)->i_flag |= IN_CHANGE | IN_UPDATE;
	return (error);
}

/*
 * Close wrapper for fifos.
 *
 * Update the times on the inode then do device close.
 *
 * ufsfifo_close(struct vnode *a_vp, int a_fflag)
 */
static
int
ufsfifo_close(struct vop_close_args *ap)
{
	struct vnode *vp = ap->a_vp;

	if (VREFCNT(vp) > 1)
		ufs_itimes(vp);
	return (VOCALL(&fifo_vnode_vops, &ap->a_head));
}

/*
 * Kqfilter wrapper for fifos.
 *
 * Fall through to ufs kqfilter routines if needed 
 */
static
int
ufsfifo_kqfilter(struct vop_kqfilter_args *ap)
{
	int error;

	error = VOCALL(&fifo_vnode_vops, &ap->a_head);
	if (error)
		error = ufs_kqfilter(ap);
	return (error);
}

/*
 * Advisory record locking support
 *
 * ufs_advlock(struct vnode *a_vp, caddr_t a_id, int a_op, struct flock *a_fl,
 *	       int a_flags)
 */
static
int
ufs_advlock(struct vop_advlock_args *ap)
{
	struct inode *ip = VTOI(ap->a_vp);

	return (lf_advlock(ap, &(ip->i_lockf), ip->i_size));
}

/*
 * Initialize the vnode associated with a new inode, handle aliased
 * vnodes.
 *
 * Make sure directories have their VM object now rather then later,
 * saving us from having to check on all the myrid directory VOPs
 * that might be executed without a VOP_OPEN being performed.
 */
int
ufs_vinit(struct mount *mntp, struct vnode **vpp)
{
	struct inode *ip;
	struct vnode *vp;
	struct timeval tv;

	vp = *vpp;
	ip = VTOI(vp);

	vp->v_type = IFTOVT(ip->i_mode);

	switch(vp->v_type) {
	case VCHR:
	case VBLK:
		vp->v_ops = &mntp->mnt_vn_spec_ops;
		addaliasu(vp, umajor(ip->i_rdev), uminor(ip->i_rdev));
		break;
	case VFIFO:
		vp->v_ops = &mntp->mnt_vn_fifo_ops;
		break;
	case VDIR:
	case VREG:
		vinitvmio(vp, ip->i_size,
			  blkoffsize(ip->i_fs, ip, ip->i_size),
			  blkoff(ip->i_fs, ip->i_size));
		break;
	case VLNK:
		if (ip->i_size >= vp->v_mount->mnt_maxsymlinklen) {
			vinitvmio(vp, ip->i_size,
				  blkoffsize(ip->i_fs, ip, ip->i_size),
				  blkoff(ip->i_fs, ip->i_size));
		}
		break;
	default:
		break;

	}

	if (ip->i_number == ROOTINO)
		vsetflags(vp, VROOT);
	/*
	 * Initialize modrev times
	 */
	getmicrouptime(&tv);
	SETHIGH(ip->i_modrev, tv.tv_sec);
	SETLOW(ip->i_modrev, tv.tv_usec * 4294);
	*vpp = vp;
	return (0);
}

/*
 * Allocate a new inode.
 */
static
int
ufs_makeinode(int mode, struct vnode *dvp, struct vnode **vpp,
	      struct componentname *cnp)
{
	struct inode *ip, *pdir;
	struct direct newdir;
	struct vnode *tvp;
	int error;

	pdir = VTOI(dvp);
	*vpp = NULL;
	if ((mode & IFMT) == 0)
		mode |= IFREG;

	error = ffs_valloc(dvp, mode, cnp->cn_cred, &tvp);
	if (error)
		return (error);
	ip = VTOI(tvp);
	ip->i_flags = pdir->i_flags & (SF_NOHISTORY|UF_NOHISTORY|UF_NODUMP);
	ip->i_gid = pdir->i_gid;
#ifdef SUIDDIR
	{
#ifdef QUOTA
		struct ucred ucred, *ucp;
		ucp = cnp->cn_cred;
#endif
		/*
		 * If we are not the owner of the directory,
		 * and we are hacking owners here, (only do this where told to)
		 * and we are not giving it TO root, (would subvert quotas)
		 * then go ahead and give it to the other user.
		 * Note that this drops off the execute bits for security.
		 */
		if ((dvp->v_mount->mnt_flag & MNT_SUIDDIR) &&
		    (pdir->i_mode & ISUID) &&
		    (pdir->i_uid != cnp->cn_cred->cr_uid) && pdir->i_uid) {
			ip->i_uid = pdir->i_uid;
			mode &= ~07111;
#ifdef QUOTA
			/*
			 * Make sure the correct user gets charged
			 * for the space.
			 * Quickly knock up a dummy credential for the victim.
			 * XXX This seems to never be accessed out of our
			 * context so a stack variable is ok.
			 */
			ucred.cr_ref = 1;
			ucred.cr_uid = ip->i_uid;
			ucred.cr_ngroups = 1;
			ucred.cr_groups[0] = pdir->i_gid;
			ucp = &ucred;
#endif
		} else
			ip->i_uid = cnp->cn_cred->cr_uid;

#ifdef QUOTA
		if ((error = ufs_getinoquota(ip)) ||
	    	    (error = ufs_chkiq(ip, 1, ucp, 0))) {
			ffs_vfree(tvp, ip->i_number, mode);
			vput(tvp);
			return (error);
		}
#endif
	}
#else	/* !SUIDDIR */
	ip->i_uid = cnp->cn_cred->cr_uid;
#ifdef QUOTA
	if ((error = ufs_getinoquota(ip)) ||
	    (error = ufs_chkiq(ip, 1, cnp->cn_cred, 0))) {
		ffs_vfree(tvp, ip->i_number, mode);
		vput(tvp);
		return (error);
	}
#endif
#endif	/* !SUIDDIR */
	ip->i_din.di_spare[0] = 0;
	ip->i_din.di_spare[1] = 0;
	ip->i_flag |= IN_ACCESS | IN_CHANGE | IN_UPDATE;
	ip->i_mode = mode;
	tvp->v_type = IFTOVT(mode);	/* Rest init'd in getnewvnode(). */
	ip->i_effnlink = 1;
	ip->i_nlink = 1;
	if (DOINGSOFTDEP(tvp))
		softdep_change_linkcnt(ip);
	if ((ip->i_mode & ISGID) && !groupmember(ip->i_gid, cnp->cn_cred) &&
	    priv_check_cred(cnp->cn_cred, PRIV_VFS_SETGID, 0)) {
		ip->i_mode &= ~ISGID;
	}

	if (cnp->cn_flags & CNP_ISWHITEOUT)
		ip->i_flags |= UF_OPAQUE;

	/*
	 * Regular files and directories need VM objects.  Softlinks do
	 * not (not immediately anyway).
	 */
	if (tvp->v_type == VREG || tvp->v_type == VDIR)
		vinitvmio(tvp, 0, PAGE_SIZE, -1);

	/*
	 * Make sure inode goes to disk before directory entry.
	 */
	error = ffs_update(tvp, !(DOINGSOFTDEP(tvp) | DOINGASYNC(tvp)));
	if (error)
		goto bad;
	ufs_makedirentry(ip, cnp, &newdir);
	error = ufs_direnter(dvp, tvp, &newdir, cnp, NULL);
	if (error)
		goto bad;
	*vpp = tvp;
	return (0);

bad:
	/*
	 * Write error occurred trying to update the inode
	 * or the directory so must deallocate the inode.
	 */
	ip->i_effnlink = 0;
	ip->i_nlink = 0;
	ip->i_flag |= IN_CHANGE;
	if (DOINGSOFTDEP(tvp))
		softdep_change_linkcnt(ip);
	vput(tvp);
	return (error);
}

static int
ufs_missingop(struct vop_generic_args *ap)
{
	panic("no vop function for %s in ufs child", ap->a_desc->sd_name);
	return (EOPNOTSUPP);
}

static struct filterops ufsread_filtops = 
	{ FILTEROP_ISFD, NULL, filt_ufsdetach, filt_ufsread };
static struct filterops ufswrite_filtops = 
	{ FILTEROP_ISFD, NULL, filt_ufsdetach, filt_ufswrite };
static struct filterops ufsvnode_filtops = 
	{ FILTEROP_ISFD, NULL, filt_ufsdetach, filt_ufsvnode };

/*
 * ufs_kqfilter(struct vnode *a_vp, struct knote *a_kn)
 */
static int
ufs_kqfilter(struct vop_kqfilter_args *ap)
{
	struct vnode *vp = ap->a_vp;
	struct knote *kn = ap->a_kn;

	switch (kn->kn_filter) {
	case EVFILT_READ:
		kn->kn_fop = &ufsread_filtops;
		break;
	case EVFILT_WRITE:
		kn->kn_fop = &ufswrite_filtops;
		break;
	case EVFILT_VNODE:
		kn->kn_fop = &ufsvnode_filtops;
		break;
	default:
		return (EOPNOTSUPP);
	}

	kn->kn_hook = (caddr_t)vp;

	/* XXX: kq token actually protects the list */
	lwkt_gettoken(&vp->v_token);
	knote_insert(&vp->v_pollinfo.vpi_kqinfo.ki_note, kn);
	lwkt_reltoken(&vp->v_token);

	return (0);
}

static void
filt_ufsdetach(struct knote *kn)
{
	struct vnode *vp = (struct vnode *)kn->kn_hook;

	lwkt_gettoken(&vp->v_token);
	knote_remove(&vp->v_pollinfo.vpi_kqinfo.ki_note, kn);
	lwkt_reltoken(&vp->v_token);
}

/*ARGSUSED*/
static int
filt_ufsread(struct knote *kn, long hint)
{
	struct vnode *vp = (struct vnode *)kn->kn_hook;
	struct inode *ip = VTOI(vp);
	off_t off;

	/*
	 * filesystem is gone, so set the EOF flag and schedule 
	 * the knote for deletion.
	 */
	if (hint == NOTE_REVOKE) {
		kn->kn_flags |= (EV_EOF | EV_NODATA | EV_ONESHOT);
		return (1);
	}

	off = ip->i_size - kn->kn_fp->f_offset;
	kn->kn_data = (off < INTPTR_MAX) ? off : INTPTR_MAX;
	if (kn->kn_sfflags & NOTE_OLDAPI)
		return(1);
        return (kn->kn_data != 0);
}

/*ARGSUSED*/
static int
filt_ufswrite(struct knote *kn, long hint)
{
	/*
	 * filesystem is gone, so set the EOF flag and schedule 
	 * the knote for deletion.
	 */
	if (hint == NOTE_REVOKE)
		kn->kn_flags |= (EV_EOF | EV_NODATA | EV_ONESHOT);

        kn->kn_data = 0;
        return (1);
}

static int
filt_ufsvnode(struct knote *kn, long hint)
{
	if (kn->kn_sfflags & hint)
		kn->kn_fflags |= hint;
	if (hint == NOTE_REVOKE) {
		kn->kn_flags |= (EV_EOF | EV_NODATA);
		return (1);
	}
	return (kn->kn_fflags != 0);
}

/* Global vfs data structures for ufs. */
static struct vop_ops ufs_vnode_vops = {
	.vop_default =		vop_defaultop,
	.vop_fsync =		(void *)ufs_missingop,
	.vop_read =		(void *)ufs_missingop,
	.vop_reallocblks =	(void *)ufs_missingop,
	.vop_write =		(void *)ufs_missingop,
	.vop_access =		ufs_access,
	.vop_advlock =		ufs_advlock,
	.vop_bmap =		ufs_bmap,
	.vop_old_lookup =	ufs_lookup,
	.vop_close =		ufs_close,
	.vop_old_create =	ufs_create,
	.vop_getattr =		ufs_getattr,
	.vop_inactive =		ufs_inactive,
	.vop_old_link =		ufs_link,
	.vop_old_mkdir =	ufs_mkdir,
	.vop_old_mknod =	ufs_mknod,
	.vop_mmap =		ufs_mmap,
	.vop_open =		vop_stdopen,
	.vop_pathconf =		vop_stdpathconf,
	.vop_kqfilter =		ufs_kqfilter,
	.vop_print =		ufs_print,
	.vop_readdir =		ufs_readdir,
	.vop_readlink =		ufs_readlink,
	.vop_reclaim =		ufs_reclaim,
	.vop_old_remove =	ufs_remove,
	.vop_old_rename =	ufs_rename,
	.vop_old_rmdir =	ufs_rmdir,
	.vop_setattr =		ufs_setattr,
	.vop_markatime =	ufs_markatime,
	.vop_strategy =		ufs_strategy,
	.vop_old_symlink =	ufs_symlink,
	.vop_old_whiteout =	ufs_whiteout
};

static struct vop_ops ufs_spec_vops = {
	.vop_default =		vop_defaultop,
	.vop_fsync =		(void *)ufs_missingop,
	.vop_access =		ufs_access,
	.vop_close =		ufs_close,
	.vop_getattr =		ufs_getattr,
	.vop_inactive =		ufs_inactive,
	.vop_print =		ufs_print,
	.vop_read =		vop_stdnoread,
	.vop_reclaim =		ufs_reclaim,
	.vop_setattr =		ufs_setattr,
	.vop_markatime =	ufs_markatime,
	.vop_write =		vop_stdnowrite
};

static struct vop_ops ufs_fifo_vops = {
	.vop_default =		fifo_vnoperate,
	.vop_fsync =		(void *)ufs_missingop,
	.vop_access =		ufs_access,
	.vop_close =		ufsfifo_close,
	.vop_getattr =		ufs_getattr,
	.vop_inactive =		ufs_inactive,
	.vop_kqfilter =		ufsfifo_kqfilter,
	.vop_print =		ufs_print,
	.vop_read =		ufsfifo_read,
	.vop_reclaim =		ufs_reclaim,
	.vop_setattr =		ufs_setattr,
	.vop_markatime =	ufs_markatime,
	.vop_write =		ufsfifo_write
};

VNODEOP_SET(ufs_vnode_vops);
VNODEOP_SET(ufs_spec_vops);
VNODEOP_SET(ufs_fifo_vops);

/*
 * ufs_vnoperate()
 */
int
ufs_vnoperate(struct vop_generic_args *ap)
{
	return (VOCALL(&ufs_vnode_vops, ap));
}

/*
 * ufs_vnoperatefifo()
 */
int
ufs_vnoperatefifo(struct vop_generic_args *ap)
{
	return (VOCALL(&ufs_fifo_vops, ap));
}

/*
 * ufs_vnoperatespec()
 */
int
ufs_vnoperatespec(struct vop_generic_args *ap)
{
	return (VOCALL(&ufs_spec_vops, ap));
}
