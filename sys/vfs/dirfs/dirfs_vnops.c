/*
 * Copyright (c) 2013 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Antonio Huete Jimenez <tuxillo@quantumachine.net>
 * by Matthew Dillon <dillon@dragonflybsd.org>
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
 */

/*
 * See below a small table with the vnode operation and syscall correspondence
 * where it applies:
 *
 * VNODE OP		SCALL	SCALL_AT  FD	PATH	COMMENTS
 * dirfs_ncreate	Y	Y	  Y	Y	open(2), openat(2)
 * dirfs_nresolve	-	-	  -	Y	no syscall needed
 * dirfs_nlookupdot	-	-	  -	-	-
 * dirfs_nmknod		Y	Y	  Y	Y	mknod(2), mknodat(2)
 * dirfs_open		Y	Y	  Y	Y	open(2), openat(2)
 * dirfs_close		Y	Y	  Y	Y	close(2)
 * dirfs_access		-	-	  -	-	data from stat(2)
 * dirfs_getattr	Y	Y	  Y	Y	lstat(2), fstatat(2)
 * dirfs_setattr	-	-	  -	-	-
 * dirfs_read		Y	-	  Y	-	read(2).
 * dirfs_write		Y	-	  Y	-	write(2).
 * dirfs_fsync		Y	-	  Y	-	fsync(2)
 * dirfs_mountctl	-	-	  -	-	-
 * dirfs_nremove	Y	-	  -	Y	unlink(2)
 * dirfs_nlink		-	-	  -	-	-
 * dirfs_nrename	Y	Y	  Y	Y	rename(2), renameat(2)
 * dirfs_nmkdir		Y	Y	  Y	Y	mkdir(2), mkdirat(2)
 * dirfs_nrmdir		Y	-	  -	Y	rmdir(2)
 * dirfs_nsymlink	Y	Y	  Y	Y	symlink(2), symlinkat(2)
 * dirfs_readdir	Y	-	  Y	-	getdirentries(2)
 * dirfs_readlink	Y	Y	  Y	Y	readlinkat(2)
 * dirfs_inactive	-	-	  -	-	-
 * dirfs_reclaim	-	-	  -	-	-
 * dirfs_print		-	-	  -	-	-
 * dirfs_pathconf	-	-	  -	-	-
 * dirfs_bmap		-	-	  -	-	-
 * dirfs_strategy	Y	-	  Y	-	pwrite(2), pread(2)
 * dirfs_advlock	-	-	  -	-	-
 * dirfs_kqfilter	-	-	  -	-	-
 */

#include <stdio.h>
#include <errno.h>
#include <strings.h>
#include <unistd.h>

#include <sys/vfsops.h>
#include <sys/vnode.h>
#include <sys/stat.h>
#include <sys/namecache.h>
#include <sys/queue.h>
#include <sys/systm.h>
#include <sys/dirent.h>
#include <sys/mount.h>
#include <sys/signalvar.h>
#include <sys/resource.h>
#include <sys/buf2.h>
#include <sys/kern_syscall.h>
#include <sys/ktr.h>

#include "dirfs.h"

/*
 * Kernel tracing facilities
 */
KTR_INFO_MASTER_EXTERN(dirfs);

KTR_INFO(KTR_DIRFS, dirfs, unsupported, 0,
    "DIRFS(func=%s)",
    const char *func);

KTR_INFO(KTR_DIRFS, dirfs, nresolve, 0,
    "DIRFS(dnp=%p ncp_name=%s parent=%p pfd=%d error=%d)",
    dirfs_node_t dnp, char *name, dirfs_node_t pdnp, int pfd, int error);

KTR_INFO(KTR_DIRFS, dirfs, ncreate, 1,
    "DIRFS(dnp=%p ncp_name=%s parent=%p pfd=%d error=%d)",
    dirfs_node_t dnp, char *name, dirfs_node_t pdnp, int pfd, int error);

KTR_INFO(KTR_DIRFS, dirfs, open, 2,
    "DIRFS(dnp=%p dn_name=%s nfd=%d)",
    dirfs_node_t dnp, char *name, int fd);

KTR_INFO(KTR_DIRFS, dirfs, close, 3,
    "DIRFS(dnp=%p fd=%d opencount=%d writecount=%d vfsync error=%d)",
    dirfs_node_t dnp, int fd, int oc, int wc, int error);

KTR_INFO(KTR_DIRFS, dirfs, readdir, 4,
    "DIRFS(dnp=%p fd=%d startoff=%jd uio_offset=%jd)",
    dirfs_node_t dnp, int fd, off_t startoff, off_t uoff);

KTR_INFO(KTR_DIRFS, dirfs, access, 5,
    "DIRFS(dnp=%p error=%d)",
    dirfs_node_t dnp, int error);

KTR_INFO(KTR_DIRFS, dirfs, getattr, 6,
    "DIRFS(dnp=%p error=%d)",
    dirfs_node_t dnp, int error);

KTR_INFO(KTR_DIRFS, dirfs, setattr, 7,
    "DIRFS(dnp=%p action=%s error=%d)",
    dirfs_node_t dnp, const char *action, int error);

KTR_INFO(KTR_DIRFS, dirfs, fsync, 8,
    "DIRFS(dnp=%p error=%d)",
    dirfs_node_t dnp, int error);

KTR_INFO(KTR_DIRFS, dirfs, read, 9,
    "DIRFS(dnp=%p size=%jd error=%d)",
    dirfs_node_t dnp, size_t size, int error);

KTR_INFO(KTR_DIRFS, dirfs, write, 10,
    "DIRFS(dnp=%p size=%jd boff=%jd uio_resid=%jd error=%d)",
    dirfs_node_t dnp, off_t boff, size_t resid, size_t size, int error);

KTR_INFO(KTR_DIRFS, dirfs, strategy, 11,
    "DIRFS(dnp=%p dnp_size=%jd iosize=%jd b_cmd=%d b_error=%d "
    "b_resid=%d bio_off=%jd error=%d)",
    dirfs_node_t dnp, size_t size, size_t iosize, int cmd, int berror,
    int bresid, off_t biooff, int error);

KTR_INFO(KTR_DIRFS, dirfs, nremove, 12,
    "DIRFS(dnp=%p pdnp=%p error=%d)",
    dirfs_node_t dnp, dirfs_node_t pdnp, int error);

KTR_INFO(KTR_DIRFS, dirfs, nmkdir, 13,
    "DIRFS(pdnp=%p dnp=%p nc_name=%p error=%d)",
    dirfs_node_t dnp, dirfs_node_t pdnp, char *n, int error);

KTR_INFO(KTR_DIRFS, dirfs, nrmdir, 13,
    "DIRFS(pdnp=%p dnp=%p error=%d)",
    dirfs_node_t dnp, dirfs_node_t pdnp, int error);

KTR_INFO(KTR_DIRFS, dirfs, nsymlink, 14,
    "DIRFS(dnp=%p target=%s symlink=%s error=%d)",
    dirfs_node_t dnp, char *tgt, char *lnk, int error);

/* Needed prototypes */
int dirfs_access(struct vop_access_args *);
int dirfs_getattr(struct vop_getattr_args *);
int dirfs_setattr(struct vop_setattr_args *);
int dirfs_reclaim(struct vop_reclaim_args *);

static int
dirfs_nresolve(struct vop_nresolve_args *ap)
{
	dirfs_node_t pdnp, dnp, d1, d2;
	dirfs_mount_t dmp;
	struct namecache *ncp;
	struct nchandle *nch;
	struct vnode *dvp;
	struct vnode *vp;
	struct mount *mp;
	int error;

	debug_called();

	error = 0;
	nch = ap->a_nch;
	ncp = nch->ncp;
	mp = nch->mount;
	dvp = ap->a_dvp;
	vp = NULL;
	dnp = d1 = d2 = NULL;
	pdnp = VP_TO_NODE(dvp);
	dmp = VFS_TO_DIRFS(mp);

	dirfs_node_lock(pdnp);
	TAILQ_FOREACH_MUTABLE(d1, &dmp->dm_fdlist, dn_fdentry, d2) {
		if (d1->dn_parent == pdnp &&
		    (strcmp(d1->dn_name, ncp->nc_name) == 0)) {
			dnp = d1;
			dirfs_node_ref(dnp);
			passive_fd_list_hits++;
			break;
		}
	}
	dirfs_node_unlock(pdnp);

	if (dnp) {
		dirfs_alloc_vp(mp, &vp, LK_CANRECURSE, dnp);
		dirfs_node_drop(dmp, dnp);
	} else {
		passive_fd_list_miss++;
		error = dirfs_alloc_file(dmp, &dnp, pdnp, ncp, &vp, NULL, 0);
	}

	if (vp) {
		if (error && error == ENOENT) {
			cache_setvp(nch, NULL);
		} else {
			vn_unlock(vp);
			cache_setvp(nch, vp);
			vrele(vp);
		}
	}

	KTR_LOG(dirfs_nresolve, dnp, ncp->nc_name, pdnp, pdnp->dn_fd, error);

	return error;
}

static int
dirfs_nlookupdotdot(struct vop_nlookupdotdot_args *ap)
{
	debug_called();

	KTR_LOG(dirfs_unsupported, __func__);

	return EOPNOTSUPP;
}

static int
dirfs_ncreate(struct vop_ncreate_args *ap)
{
	dirfs_node_t pdnp;
	dirfs_node_t dnp;
	dirfs_mount_t dmp;
	struct namecache *ncp;
	struct vnode *dvp;
	struct vnode **vpp;
	struct vattr *vap;
	int perms = 0;
	int error;

	debug_called();

	error = 0;
	dnp = NULL;
	dvp = ap->a_dvp;
	pdnp = VP_TO_NODE(dvp);
	dmp = VFS_TO_DIRFS(dvp->v_mount);
	vap = ap->a_vap;
	ncp = ap->a_nch->ncp;
	vpp = ap->a_vpp;

	dirfs_mount_gettoken(dmp);

	dirfs_node_getperms(pdnp, &perms);
	if ((perms & DIRFS_NODE_WR) == 0)
		error = EPERM;

	error = dirfs_alloc_file(dmp, &dnp, pdnp, ncp, vpp, vap,
	    (O_CREAT | O_RDWR));

	if (error == 0) {
		cache_setunresolved(ap->a_nch);
		cache_setvp(ap->a_nch, *vpp);
	}

	dirfs_mount_reltoken(dmp);

	KTR_LOG(dirfs_ncreate, dnp, ncp->nc_name, pdnp, pdnp->dn_fd, error);

	return error;
}

static int
dirfs_nmknod(struct vop_nmknod_args *v)
{
	debug_called();

	return EOPNOTSUPP;
}

static int
dirfs_open(struct vop_open_args *ap)
{
	dirfs_node_t dnp;
	dirfs_mount_t dmp;
	struct vnode *vp;
	int error;

	debug_called();

	vp = ap->a_vp;
	dnp = VP_TO_NODE(vp);
	dmp = VFS_TO_DIRFS(vp->v_mount);
	error = 0;

	/*
	 * Root inode has been allocated and opened in VFS_ROOT() so
	 * no reason to attempt to open it again.
	 */
	if (dmp->dm_root != dnp && dnp->dn_fd == DIRFS_NOFD) {
		error = dirfs_open_helper(dmp, dnp, DIRFS_NOFD, NULL);
		if (error)
			return error;
	}

	KTR_LOG(dirfs_open, dnp, dnp->dn_name, dnp->dn_fd);

	return vop_stdopen(ap);
}

static int
dirfs_close(struct vop_close_args *ap)
{
	struct vnode *vp;
	dirfs_node_t dnp;
	int error;

	debug_called();

	error = 0;
	vp = ap->a_vp;
	dnp = VP_TO_NODE(vp);

	if (vp->v_type == VREG) {
		error = vfsync(vp, 0, 1, NULL, NULL);
		if (error)
			dbg(5, "vfsync error=%d\n", error);
	}
	vop_stdclose(ap);

	KTR_LOG(dirfs_close, dnp, dnp->dn_fd, vp->v_opencount,
	    vp->v_writecount, error);

	return 0;
}

int
dirfs_access(struct vop_access_args *ap)
{
	struct vnode *vp = ap->a_vp;
	int error;
	dirfs_node_t dnp;

	debug_called();

	dnp = VP_TO_NODE(vp);

	switch (vp->v_type) {
	case VDIR:
		/* FALLTHROUGH */
	case VLNK:
		/* FALLTHROUGH */
	case VREG:
		if ((ap->a_mode & VWRITE) && (vp->v_mount->mnt_flag & MNT_RDONLY)) {
			error = EROFS;
			goto out;
		}
		break;
	case VBLK:
		/* FALLTHROUGH */
	case VCHR:
		/* FALLTHROUGH */
	case VSOCK:
		/* FALLTHROUGH */
	case VFIFO:
		break;

	default:
		error = EINVAL;
		goto out;
	}

	error = vop_helper_access(ap, dnp->dn_uid,
	    dnp->dn_gid, dnp->dn_mode, 0);

out:
	KTR_LOG(dirfs_access, dnp, error);

	return error;
}

int
dirfs_getattr(struct vop_getattr_args *ap)
{
	dirfs_mount_t dmp;
	dirfs_node_t dnp;
	dirfs_node_t pathnp;
	struct vnode *vp;
	struct vattr *vap;
	char *tmp;
	char *pathfree;
	int error;

	debug_called();

	vp = ap->a_vp;
	vap = ap->a_vap;
	dnp = VP_TO_NODE(vp);
	dmp = VFS_TO_DIRFS(vp->v_mount);

	KKASSERT(dnp);	/* This must not happen */

	if (!dirfs_node_isroot(dnp)) {
		pathnp = dirfs_findfd(dmp, dnp, &tmp, &pathfree);

		KKASSERT(pathnp->dn_fd != DIRFS_NOFD);

		error = dirfs_node_stat(pathnp->dn_fd, tmp, dnp);
		dirfs_dropfd(dmp, pathnp, pathfree);
	} else {
		error = dirfs_node_stat(DIRFS_NOFD, dmp->dm_path, dnp);
	}

	if (error == 0) {
		dirfs_node_lock(dnp);
		vap->va_nlink = dnp->dn_links;
		vap->va_type = dnp->dn_type;
		vap->va_mode = dnp->dn_mode;
		vap->va_uid = dnp->dn_uid;
		vap->va_gid = dnp->dn_gid;
		vap->va_fileid = dnp->dn_ino;
		vap->va_size = dnp->dn_size;
		vap->va_blocksize = dnp->dn_blocksize;
		vap->va_atime.tv_sec = dnp->dn_atime;
		vap->va_atime.tv_nsec = dnp->dn_atimensec;
		vap->va_mtime.tv_sec = dnp->dn_mtime;
		vap->va_mtime.tv_nsec = dnp->dn_mtimensec;
		vap->va_ctime.tv_sec = dnp->dn_ctime;
		vap->va_ctime.tv_nsec = dnp->dn_ctimensec;
		vap->va_bytes = dnp->dn_size;
		vap->va_gen = dnp->dn_gen;
		vap->va_flags = dnp->dn_flags;
		vap->va_fsid = vp->v_mount->mnt_stat.f_fsid.val[0];
		dirfs_node_unlock(dnp);
	}

	KTR_LOG(dirfs_getattr, dnp, error);

	return 0;
}

int
dirfs_setattr(struct vop_setattr_args *ap)
{
	dirfs_mount_t dmp;
	dirfs_node_t dnp;
	struct vnode *vp;
	struct vattr *vap;
	struct ucred *cred;
	int error;
#ifdef KTR
	const char *msg[6] = {
		"invalid",
		"chflags",
		"chsize",
		"chown",
		"chmod",
		"chtimes"
	};
#endif
	int msgno;

	debug_called();

	error = msgno = 0;
	vp = ap->a_vp;
	vap = ap->a_vap;
	cred = ap->a_cred;
	dnp = VP_TO_NODE(vp);
	dmp = VFS_TO_DIRFS(vp->v_mount);

	dirfs_mount_gettoken(dmp);

	/*
	 * Check for unsettable attributes.
	 */
	if ((vap->va_type != VNON) || (vap->va_nlink != VNOVAL) ||
	    (vap->va_fsid != VNOVAL) || (vap->va_fileid != VNOVAL) ||
	    (vap->va_blocksize != VNOVAL) || (vap->va_rmajor != VNOVAL) ||
	    ((int)vap->va_bytes != VNOVAL) || (vap->va_gen != VNOVAL)) {
		msgno = 0;
		error = EINVAL;
		goto out;
	}

	/*
	 * Change file flags
	 */
	if (error == 0 && (vap->va_flags != VNOVAL)) {
		if (vp->v_mount->mnt_flag & MNT_RDONLY)
			error = EROFS;
		else
			error = dirfs_node_chflags(dnp, vap->va_flags, cred);
		msgno = 1;
		goto out;
	}

	/*
	 * Extend or truncate a file
	 */
	if (error == 0 && (vap->va_size != VNOVAL)) {
		if (vp->v_mount->mnt_flag & MNT_RDONLY)
			error = EROFS;
		else
			error = dirfs_node_chsize(dnp, vap->va_size);
		dbg(2, "dnp size=%jd vap size=%jd\n", dnp->dn_size, vap->va_size);
		msgno = 2;
		goto out;
	}

	/*
	 * Change file owner or group
	 */
	if (error == 0 && (vap->va_uid != (uid_t)VNOVAL ||
		vap->va_gid != (gid_t)VNOVAL)) {
		if (vp->v_mount->mnt_flag & MNT_RDONLY) {
			error = EROFS;
		} else {
			mode_t cur_mode = dnp->dn_mode;
			uid_t cur_uid = dnp->dn_uid;
			gid_t cur_gid = dnp->dn_gid;

			error = vop_helper_chown(ap->a_vp, vap->va_uid,
						 vap->va_gid, ap->a_cred,
						 &cur_uid, &cur_gid, &cur_mode);
			if (error == 0 &&
			    (cur_mode != dnp->dn_mode ||
			     cur_uid != dnp->dn_uid ||
			     cur_gid != dnp->dn_gid)) {
				error = dirfs_node_chown(dmp, dnp, cur_uid,
							 cur_gid, cur_mode);
			}
		}
		msgno = 3;
		goto out;
	}

	/*
	 * Change file mode
	 */
	if (error == 0 && (vap->va_mode != (mode_t)VNOVAL)) {
		if (vp->v_mount->mnt_flag & MNT_RDONLY) {
			error = EROFS;
		} else {
			mode_t cur_mode = dnp->dn_mode;
			uid_t cur_uid = dnp->dn_uid;
			gid_t cur_gid = dnp->dn_gid;

			error = vop_helper_chmod(ap->a_vp, vap->va_mode,
						 ap->a_cred,
						 cur_uid, cur_gid, &cur_mode);
			if (error == 0 && cur_mode != dnp->dn_mode) {
				error = dirfs_node_chmod(dmp, dnp, cur_mode);
			}
		}
		msgno = 4;
		goto out;
	}

	/*
	 * Change file times
	 */
	if (error == 0 && ((vap->va_atime.tv_sec != VNOVAL &&
		vap->va_atime.tv_nsec != VNOVAL) ||
		(vap->va_mtime.tv_sec != VNOVAL &&
		vap->va_mtime.tv_nsec != VNOVAL) )) {
		if (vp->v_mount->mnt_flag & MNT_RDONLY)
			error = EROFS;
		else
			error = dirfs_node_chtimes(dnp);
		msgno = 5;
		goto out;

	}
out:
	dirfs_mount_reltoken(dmp);

	KTR_LOG(dirfs_setattr, dnp, msg[msgno], error);

	return error;
}

static int
dirfs_fsync(struct vop_fsync_args *ap)
{
	dirfs_node_t dnp = VP_TO_NODE(ap->a_vp);
	int error = 0;

	debug_called();

	vfsync(ap->a_vp, ap->a_waitfor, 1, NULL, NULL);

	if (dnp->dn_fd != DIRFS_NOFD) {
		if (fsync(dnp->dn_fd) == -1)
			error = fsync(dnp->dn_fd);
	}

	KTR_LOG(dirfs_fsync, dnp, error);

	return 0;
}

static int
dirfs_read(struct vop_read_args *ap)
{
	struct buf *bp;
	struct vnode *vp = ap->a_vp;
	struct uio *uio = ap->a_uio;
	dirfs_node_t dnp;
	off_t base_offset;
	size_t offset;
	size_t len;
	int error;

	debug_called();

	error = 0;
	if (uio->uio_resid == 0) {
		dbg(5, "zero len uio->uio_resid\n");
		return error;
	}

	dnp = VP_TO_NODE(vp);

	if (uio->uio_offset < 0)
		return (EINVAL);
	if (vp->v_type != VREG)
		return (EINVAL);

	while (uio->uio_resid > 0 && uio->uio_offset < dnp->dn_size) {
		/*
		 * Use buffer cache I/O (via dirfs_strategy)
		 */
		offset = (size_t)uio->uio_offset & BMASK;
		base_offset = (off_t)uio->uio_offset - offset;
		bp = getcacheblk(vp, base_offset, BSIZE, 0);
		if (bp == NULL) {
			lwkt_gettoken(&vp->v_mount->mnt_token);
			error = bread(vp, base_offset, BSIZE, &bp);
			if (error) {
				brelse(bp);
				lwkt_reltoken(&vp->v_mount->mnt_token);
				dbg(5, "dirfs_read bread error %d\n", error);
				break;
			}
			lwkt_reltoken(&vp->v_mount->mnt_token);
		}

		/*
		 * Figure out how many bytes we can actually copy this loop.
		 */
		len = BSIZE - offset;
		if (len > uio->uio_resid)
			len = uio->uio_resid;
		if (len > dnp->dn_size - uio->uio_offset)
			len = (size_t)(dnp->dn_size - uio->uio_offset);

		error = uiomovebp(bp, (char *)bp->b_data + offset, len, uio);
		bqrelse(bp);
		if (error) {
			dbg(5, "dirfs_read uiomove error %d\n", error);
			break;
		}
	}

	KTR_LOG(dirfs_read, dnp, dnp->dn_size, error);

	return(error);
}

static int
dirfs_write (struct vop_write_args *ap)
{
	dirfs_node_t dnp;
	dirfs_mount_t dmp;
	struct buf *bp;
	struct vnode *vp = ap->a_vp;
	struct uio *uio = ap->a_uio;
	struct thread *td = uio->uio_td;
	int error;
	off_t osize;
	off_t nsize;
	off_t base_offset;
	size_t offset;
	size_t len;
	struct rlimit limit;

	debug_called();

	error = 0;
	if (uio->uio_resid == 0) {
		dbg(5, "zero-length uio->uio_resid\n");
		return error;
	}

	dnp = VP_TO_NODE(vp);
	dmp = VFS_TO_DIRFS(vp->v_mount);

	if (vp->v_type != VREG)
		return (EINVAL);

	if (vp->v_type == VREG && td != NULL) {
		error = kern_getrlimit(RLIMIT_FSIZE, &limit);
		if (error != 0) {
			dbg(5, "rlimit failure\n");
			return error;
		}
		if (uio->uio_offset + uio->uio_resid > limit.rlim_cur) {
			dbg(5, "file too big\n");
			ksignal(td->td_proc, SIGXFSZ);
			return (EFBIG);
		}
	}

	if (ap->a_ioflag & IO_APPEND)
		uio->uio_offset = dnp->dn_size;

	/*
	 * buffer cache operations may be deferred, make sure
	 * the file is correctly sized right now.
	 */
	osize = dnp->dn_size;
	nsize = uio->uio_offset + uio->uio_resid;
	if (nsize > osize && uio->uio_resid) {
		KKASSERT(dnp->dn_fd >= 0);
		dnp->dn_size = nsize;
		ftruncate(dnp->dn_fd, nsize);
		nvextendbuf(vp, osize, nsize,
			    BSIZE, BSIZE, -1, -1, 0);
	} /* else nsize = osize; NOT USED */

	while (uio->uio_resid > 0) {
		/*
		 * Use buffer cache I/O (via dirfs_strategy)
		 */
		offset = (size_t)uio->uio_offset & BMASK;
		base_offset = (off_t)uio->uio_offset - offset;
		len = BSIZE - offset;

		if (len > uio->uio_resid)
			len = uio->uio_resid;

		error = bread(vp, base_offset, BSIZE, &bp);
		error = uiomovebp(bp, (char *)bp->b_data + offset, len, uio);
		if (error) {
			brelse(bp);
			dbg(2, "WRITE uiomove failed\n");
			break;
		}

//		dbg(2, "WRITE dn_size=%jd uio_offset=%jd uio_resid=%jd base_offset=%jd\n",
//		    dnp->dn_size, uio->uio_offset, uio->uio_resid, base_offset);

		if (ap->a_ioflag & IO_SYNC)
			bwrite(bp);
		else
			bdwrite(bp);
	}

	KTR_LOG(dirfs_write, dnp, base_offset, uio->uio_resid,
	    dnp->dn_size, error);

	return error;
}

static int
dirfs_advlock (struct vop_advlock_args *ap)
{
	struct vnode *vp = ap->a_vp;
	dirfs_node_t dnp = VP_TO_NODE(vp);

	debug_called();

	return (lf_advlock(ap, &dnp->dn_advlock, dnp->dn_size));
}

static int
dirfs_strategy(struct vop_strategy_args *ap)
{
	dirfs_node_t dnp;
	dirfs_mount_t dmp;
	struct bio *bio = ap->a_bio;
	struct buf *bp = bio->bio_buf;
	struct vnode *vp = ap->a_vp;
	int error;
	size_t iosize;
	char *tmp;
	char *pathfree;

	debug_called();

	dnp = VP_TO_NODE(vp);
	dmp = VFS_TO_DIRFS(vp->v_mount);

	error = 0;

	if (vp->v_type != VREG)  {
		dbg(5, "not VREG\n");
		bp->b_resid = bp->b_bcount;
		bp->b_flags |= B_ERROR | B_INVAL;
		bp->b_error = EINVAL;
		biodone(bio);
		return(0);
	}

	if (dnp->dn_fd == DIRFS_NOFD) {
		print_backtrace(-1);
		panic("Meh, no fd to write to. dnp=%p\n", dnp);
	}

	if (bio->bio_offset + bp->b_bcount > dnp->dn_size)
		iosize = dnp->dn_size - bio->bio_offset;
	else
		iosize = bp->b_bcount;
	KKASSERT((ssize_t)iosize >= 0);

	switch (bp->b_cmd) {
	case BUF_CMD_WRITE:
		error = pwrite(dnp->dn_fd, bp->b_data, iosize, bio->bio_offset);
		break;
	case BUF_CMD_READ:
		error = pread(dnp->dn_fd, bp->b_data, iosize, bio->bio_offset);
		break;
	default:
		bp->b_error = error = EINVAL;
		bp->b_flags |= B_ERROR;
		break;
	}

	if (error >= 0 && error < bp->b_bcount)
		bzero(bp->b_data + error, bp->b_bcount - error);

	if (error < 0 && errno != EINTR) {
		dbg(5, "error=%d dnp=%p dnp->dn_fd=%d "
		    "bio->bio_offset=%ld bcount=%d resid=%d iosize=%zd\n",
		    errno, dnp, dnp->dn_fd, bio->bio_offset, bp->b_bcount,
		    bp->b_resid, iosize);
		bp->b_error = errno;
		bp->b_resid = bp->b_bcount;
		bp->b_flags |= B_ERROR;
	} else {
		tmp = dirfs_node_absolute_path(dmp, dnp, &pathfree);
		dirfs_node_stat(DIRFS_NOFD, tmp, dnp);
		dirfs_dropfd(dmp, NULL, pathfree);
	}

	KTR_LOG(dirfs_strategy, dnp, dnp->dn_size, iosize, bp->b_cmd,
	    bp->b_error, bp->b_resid, bio->bio_offset, error);

	biodone(bio);

	return 0;
}

static int
dirfs_bmap(struct vop_bmap_args *ap)
{
	debug_called();

	if (ap->a_doffsetp != NULL)
		*ap->a_doffsetp = ap->a_loffset;
	if (ap->a_runp != NULL)
		*ap->a_runp = 0;
	if (ap->a_runb != NULL)
		*ap->a_runb = 0;

	return 0;
}

static int
dirfs_nremove(struct vop_nremove_args *ap)
{
	dirfs_node_t dnp, pdnp;
	dirfs_node_t pathnp;
	dirfs_mount_t dmp;
	struct vnode *dvp;
	struct nchandle *nch;
	struct namecache *ncp;
	struct mount *mp;
	struct vnode *vp;
	int error;
	char *tmp;
	char *pathfree;
	debug_called();

	error = 0;
	tmp = NULL;
	vp = NULL;
	dvp = ap->a_dvp;
	nch = ap->a_nch;
	ncp = nch->ncp;

	mp = dvp->v_mount;
	dmp = VFS_TO_DIRFS(mp);

	lwkt_gettoken(&mp->mnt_token);
	cache_vget(nch, ap->a_cred, LK_SHARED, &vp);
	vn_unlock(vp);

	pdnp = VP_TO_NODE(dvp);
	dnp = VP_TO_NODE(vp);

	if (vp->v_type == VDIR) {
		error = EISDIR;
	} else {
		pathnp = dirfs_findfd(dmp, dnp, &tmp, &pathfree);
		dirfs_node_lock(pdnp);
		error = unlinkat(pathnp->dn_fd, tmp, 0);
		if (error == 0) {
			cache_unlink(nch);
			dirfs_node_setpassive(dmp, dnp, 0);
			if (dnp->dn_parent) {
				dirfs_node_drop(dmp, dnp->dn_parent);
				dnp->dn_parent = NULL;
			}
		} else {
			error = errno;
		}
		dirfs_node_unlock(pdnp);
		dirfs_dropfd(dmp, pathnp, pathfree);
	}
	vrele(vp);
	lwkt_reltoken(&mp->mnt_token);

	KTR_LOG(dirfs_nremove, dnp, pdnp, error);

	return error;
}

static int
dirfs_nlink(struct vop_nlink_args *ap)
{
	debug_called();

	KTR_LOG(dirfs_unsupported, __func__);

	return EOPNOTSUPP;
}

static int
dirfs_nrename(struct vop_nrename_args *ap)
{
	dirfs_node_t dnp, fdnp, tdnp;
	dirfs_mount_t dmp;
	struct namecache *fncp, *tncp;
	struct vnode *fdvp, *tdvp, *vp;
	struct mount *mp;
	char *fpath, *fpathfree;
	char *tpath, *tpathfree;
	int error;

	debug_called();

	error = 0;
	fdvp = ap->a_fdvp;
	tdvp = ap->a_tdvp;
	fncp = ap->a_fnch->ncp;
	tncp = ap->a_tnch->ncp;
	mp = fdvp->v_mount;
	dmp = VFS_TO_DIRFS(mp);
	fdnp = VP_TO_NODE(fdvp);
	tdnp = VP_TO_NODE(tdvp);

	dbg(5, "fdnp=%p tdnp=%p from=%s to=%s\n", fdnp, tdnp, fncp->nc_name,
	    tncp->nc_name);

	if (fdvp->v_mount != tdvp->v_mount)
		return(EXDEV);
	if (fdvp->v_mount != fncp->nc_vp->v_mount)
		return(EXDEV);
	if (fdvp->v_mount->mnt_flag & MNT_RDONLY)
		return (EROFS);

	tpath = dirfs_node_absolute_path_plus(dmp, tdnp,
					      tncp->nc_name, &tpathfree);
	fpath = dirfs_node_absolute_path_plus(dmp, fdnp,
					      fncp->nc_name, &fpathfree);
	error = rename(fpath, tpath);
	if (error < 0)
		error = errno;
	if (error == 0) {
		vp = fncp->nc_vp;	/* file being renamed */
		dnp = VP_TO_NODE(vp);
		dirfs_node_setname(dnp, tncp->nc_name, tncp->nc_nlen);

		/*
		 * We have to mark the target file that was replaced by
		 * the rename as having been unlinked.
		 */
		vp = tncp->nc_vp;
		if (vp) {
			dbg(5, "RENAME2\n");
			dnp = VP_TO_NODE(vp);
			cache_unlink(ap->a_tnch);
			dirfs_node_setpassive(dmp, dnp, 0);
			if (dnp->dn_parent) {
				dirfs_node_drop(dmp, dnp->dn_parent);
				dnp->dn_parent = NULL;
			}

			/*
			 * nlinks on directories can be a bit weird.  Zero
			 * it out.
			 */
			dnp->dn_links = 0;
			cache_inval_vp(vp, CINV_DESTROY);
		}
		cache_rename(ap->a_fnch, ap->a_tnch);
	}
	dirfs_dropfd(dmp, NULL, fpathfree);
	dirfs_dropfd(dmp, NULL, tpathfree);

	return error;
}

static int
dirfs_nmkdir(struct vop_nmkdir_args *ap)
{
	dirfs_mount_t dmp;
	dirfs_node_t dnp, pdnp, dnp1;
	struct namecache *ncp;
	struct vattr *vap;
	struct vnode *dvp;
	struct vnode **vpp;
	char *tmp, *pathfree;
	char *path;
	int pfd, error;
	int extrapath;

	debug_called();

	extrapath = error = 0;
	dvp = ap->a_dvp;
	vpp = ap->a_vpp;
	dmp = VFS_TO_DIRFS(dvp->v_mount);
	pdnp = VP_TO_NODE(dvp);
	ncp = ap->a_nch->ncp;
	vap = ap->a_vap;
	pathfree = tmp = path = NULL;
	dnp = NULL;

	dirfs_node_lock(pdnp);
	if (pdnp->dn_fd != DIRFS_NOFD) {
		pfd = pdnp->dn_fd;
		path = ncp->nc_name;
	} else {
		dnp1 = dirfs_findfd(dmp, pdnp, &tmp, &pathfree);
		pfd = dnp1->dn_fd;
		/* XXX check there is room to copy the path */
		path = kmalloc(MAXPATHLEN, M_DIRFS_MISC, M_ZERO | M_WAITOK);
		ksnprintf(path, MAXPATHLEN, "%s/%s", tmp, ncp->nc_name);
		extrapath = 1;
		dirfs_dropfd(dmp, dnp1, pathfree);
	}

	error = mkdirat(pfd, path, vap->va_mode);
	if (error) {
		error = errno;
	} else { /* Directory has been made */
		error = dirfs_alloc_file(dmp, &dnp, pdnp, ncp, vpp,
		    vap, O_DIRECTORY);
		if (error)
			error = errno;
		cache_setunresolved(ap->a_nch);
		cache_setvp(ap->a_nch, *vpp);
	}
	dirfs_node_unlock(pdnp);

	if (extrapath)
		kfree(path, M_DIRFS_MISC);

	KTR_LOG(dirfs_nmkdir, pdnp, dnp, ncp->nc_name, error);

	return error;
}

static int
dirfs_nrmdir(struct vop_nrmdir_args *ap)
{
	dirfs_node_t dnp, pdnp;
	dirfs_mount_t dmp;
	struct vnode *dvp;
	struct nchandle *nch;
	struct namecache *ncp;
	struct mount *mp;
	struct vnode *vp;
	int error;
	char *tmp;
	char *pathfree;

	debug_called();

	error = 0;
	tmp = NULL;
	vp = NULL;
	dvp = ap->a_dvp;
	nch = ap->a_nch;
	ncp = nch->ncp;

	mp = dvp->v_mount;
	dmp = VFS_TO_DIRFS(mp);

	lwkt_gettoken(&mp->mnt_token);
	cache_vget(nch, ap->a_cred, LK_SHARED, &vp);
	vn_unlock(vp);

	pdnp = VP_TO_NODE(dvp);
	dnp = VP_TO_NODE(vp);

	if (vp->v_type != VDIR) {
		error = ENOTDIR;
	} else {
		tmp = dirfs_node_absolute_path(dmp, dnp, &pathfree);
		dirfs_node_lock(pdnp);
		error = rmdir(tmp);
		if (error == 0) {
			cache_unlink(nch);
			dirfs_node_setpassive(dmp, dnp, 0);
			if (dnp->dn_parent) {
				dirfs_node_drop(dmp, dnp->dn_parent);
				dnp->dn_parent = NULL;
			}

			/*
			 * nlinks on directories can be a bit weird.  Zero
			 * it out.
			 */
			dnp->dn_links = 0;
			cache_inval_vp(vp, CINV_DESTROY);
		} else {
			error = errno;
		}
		dirfs_node_unlock(pdnp);
		dirfs_dropfd(dmp, NULL, pathfree);
	}
	vrele(vp);
	lwkt_reltoken(&mp->mnt_token);

	KTR_LOG(dirfs_nrmdir, dnp, pdnp, error);

	return error;
}

static int
dirfs_nsymlink(struct vop_nsymlink_args *ap)
{
	dirfs_mount_t dmp;
	dirfs_node_t dnp, pdnp;
	struct mount *mp;
	struct namecache *ncp;
	struct vattr *vap;
	struct vnode *dvp;
	struct vnode **vpp;
	char *tmp, *pathfree;
	char *path;
	int error;

	debug_called();

	error = 0;
	dvp = ap->a_dvp;
	vpp = ap->a_vpp;
	mp = dvp->v_mount;
	dmp = VFS_TO_DIRFS(dvp->v_mount);
	pdnp = VP_TO_NODE(dvp);
	ncp = ap->a_nch->ncp;
	vap = ap->a_vap;
	pathfree = tmp = path = NULL;
	dnp = NULL;

	lwkt_gettoken(&mp->mnt_token);
	vap->va_type = VLNK;

	/* Find out the whole path of our new symbolic link */
	tmp = dirfs_node_absolute_path(dmp, pdnp, &pathfree);
	/* XXX check there is room to copy the path */
	path = kmalloc(MAXPATHLEN, M_DIRFS_MISC, M_ZERO | M_WAITOK);
	ksnprintf(path, MAXPATHLEN, "%s/%s", tmp, ncp->nc_name);
	dirfs_dropfd(dmp, NULL, pathfree);

	error = symlink(ap->a_target, path);
	if (error) {
		error = errno;
	} else { /* Symlink has been made */
		error = dirfs_alloc_file(dmp, &dnp, pdnp, ncp, vpp,
		    NULL, 0);
		if (error)
			error = errno;
		cache_setunresolved(ap->a_nch);
		cache_setvp(ap->a_nch, *vpp);
	}
	dbg(5, "path=%s a_target=%s\n", path, ap->a_target);

	KTR_LOG(dirfs_nsymlink, dnp, ap->a_target, path, error);
	kfree(path, M_DIRFS_MISC);
	lwkt_reltoken(&mp->mnt_token);

	return error;

}

static int
dirfs_readdir(struct vop_readdir_args *ap)
{

	struct dirent *dp, *dpn;
	off_t __unused **cookies = ap->a_cookies;
	int *ncookies = ap->a_ncookies;
	int bytes;
	char *buf;
	long base;
	struct vnode *vp = ap->a_vp;
	struct uio *uio;
	dirfs_node_t dnp;
	off_t startoff;
	off_t cnt;
	int error, r;
	size_t bufsiz;
	off_t curoff;

	debug_called();

	if (ncookies)
		debug(1, "ncookies=%d\n", *ncookies);

	dnp = VP_TO_NODE(vp);
	uio = ap->a_uio;
	startoff = uio->uio_offset;
	cnt = 0;
	error = 0;
	base = 0;
	bytes = 0;

	if (vp->v_type != VDIR)
		return ENOTDIR;
	if (uio->uio_resid < 0)
		return EINVAL;
	if ((bufsiz = uio->uio_resid) > 4096)
		bufsiz = 4096;
	buf = kmalloc(bufsiz, M_DIRFS_MISC, M_WAITOK | M_ZERO);

	/*
	 * Generally speaking we have to be able to process ALL the
	 * entries returned by getdirentries() in order for the seek
	 * position to be correct.  For now try to size the buffer
	 * to make this happen.  A smaller buffer always works.  For
	 * now just use an appropriate size.
	 */
	dirfs_node_lock(dnp);
	lseek(dnp->dn_fd, startoff, SEEK_SET);
	bytes = getdirentries(dnp->dn_fd, buf, bufsiz, &base);
	dbg(5, "seek %016jx %016jx %016jx\n",
		(intmax_t)startoff, (intmax_t)base,
		(intmax_t)lseek(dnp->dn_fd, 0, SEEK_CUR));
	if (bytes < 0) {
		if (errno == EINVAL)
			panic("EINVAL on readdir\n");
		error = errno;
		curoff = startoff;
		goto out;
	} else if (bytes == 0) {
		*ap->a_eofflag = 1;
		curoff = startoff;
		goto out;
	}

	for (dp = (struct dirent *)buf; bytes > 0 && uio->uio_resid > 0;
	    bytes -= _DIRENT_DIRSIZ(dp), dp = dpn) {
		r = vop_write_dirent(&error, uio, dp->d_ino, dp->d_type,
		    dp->d_namlen, dp->d_name);
		if (error || r)
			break;
		dpn = _DIRENT_NEXT(dp);
		dp = dpn;
		cnt++;
	}
	curoff = lseek(dnp->dn_fd, 0, SEEK_CUR);

out:
	kfree(buf, M_DIRFS_MISC);
	uio->uio_offset = curoff;
	dirfs_node_unlock(dnp);

	KTR_LOG(dirfs_readdir, dnp, dnp->dn_fd, startoff, uio->uio_offset);

	return error;
}

static int
dirfs_readlink(struct vop_readlink_args *ap)
{
	dirfs_node_t dnp, pathnp;
	dirfs_mount_t dmp;
	struct vnode *vp;
	struct mount *mp;
	struct uio *uio;
	char *tmp, *pathfree, *buf;
	ssize_t nlen;
	int error;

	debug_called();

	vp = ap->a_vp;

	KKASSERT(vp->v_type == VLNK);

	error = 0;
	tmp = pathfree = NULL;
	uio = ap->a_uio;
	mp = vp->v_mount;
	dmp = VFS_TO_DIRFS(mp);
	dnp = VP_TO_NODE(vp);

	lwkt_gettoken(&mp->mnt_token);

	pathnp = dirfs_findfd(dmp, dnp, &tmp, &pathfree);

	buf = kmalloc(uio->uio_resid, M_DIRFS_MISC, M_WAITOK | M_ZERO);
	nlen = readlinkat(pathnp->dn_fd, dnp->dn_name, buf, uio->uio_resid);
	if (nlen == -1 ) {
		error = errno;
	} else {
		error = uiomove(buf, nlen + 1, uio);
		buf[nlen] = '\0';
		if (error)
			error = errno;
	}
	dirfs_dropfd(dmp, pathnp, pathfree);
	kfree(buf, M_DIRFS_MISC);

	lwkt_reltoken(&mp->mnt_token);

	return error;
}

/*
 * Main tasks to be performed.
 * 1) When inode is NULL recycle the vnode
 * 2) When the inode has 0 links:
 *	- Check if in the TAILQ, if so remove.
 *	- Destroy the inode.
 *	- Recycle the vnode.
 * 3) If none of the above, add the node to the TAILQ
 *    when it has a valid fd and there is room on the
 *    queue.
 *
 */
static int
dirfs_inactive(struct vop_inactive_args *ap)
{
	struct vnode *vp;
	dirfs_mount_t dmp;
	dirfs_node_t dnp;

	debug_called();

	vp = ap->a_vp;
	dmp = VFS_TO_DIRFS(vp->v_mount);
	dnp = VP_TO_NODE(vp);

	/* Degenerate case */
	if (dnp == NULL) {
		dbg(5, "dnp was NULL\n");
		vrecycle(vp);
		return 0;
	}

	dirfs_mount_gettoken(dmp);

	/*
	 * Deal with the case the inode has 0 links which means it was unlinked.
	 */
	if (dnp->dn_links == 0) {
		vrecycle(vp);
		dbg(5, "recycled a vnode of an unlinked dnp\n");

		goto out;
	}

	/*
	 * Try to retain the fd in our fd cache.
	 */
	dirfs_node_setpassive(dmp, dnp, 1);
out:
	dirfs_mount_reltoken(dmp);

	return 0;

}

int
dirfs_reclaim(struct vop_reclaim_args *ap)
{
	struct vnode *vp;
	dirfs_node_t dnp;
	dirfs_mount_t dmp;

	debug_called();

	vp = ap->a_vp;
	dnp = VP_TO_NODE(vp);
	dmp = VFS_TO_DIRFS(vp->v_mount);

	dirfs_free_vp(dmp, dnp);
	/* dnp is now invalid, may have been destroyed */

	return 0;
}

static int
dirfs_mountctl(struct vop_mountctl_args *ap)
{
	debug_called();

	KTR_LOG(dirfs_unsupported, __func__);

	return EOPNOTSUPP;
}

static int
dirfs_print(struct vop_print_args *v)
{
	debug_called();

	KTR_LOG(dirfs_unsupported, __func__);

	return EOPNOTSUPP;
}

static int __unused
dirfs_pathconf(struct vop_pathconf_args *v)
{
	debug_called();

	return EOPNOTSUPP;
}

static int
dirfs_kqfilter (struct vop_kqfilter_args *ap)
{
	debug_called();

	KTR_LOG(dirfs_unsupported, __func__);

	return EOPNOTSUPP;
}

struct vop_ops dirfs_vnode_vops = {
	.vop_default =			vop_defaultop,
	.vop_nwhiteout =		vop_compat_nwhiteout,
	.vop_ncreate =			dirfs_ncreate,
	.vop_nresolve =			dirfs_nresolve,
	.vop_markatime =		vop_stdmarkatime,
	.vop_nlookupdotdot =		dirfs_nlookupdotdot,
	.vop_nmknod =			dirfs_nmknod,
	.vop_open =			dirfs_open,
	.vop_close =			dirfs_close,
	.vop_access =			dirfs_access,
	.vop_getattr =			dirfs_getattr,
	.vop_setattr =			dirfs_setattr,
	.vop_read =			dirfs_read,
	.vop_write =			dirfs_write,
	.vop_fsync =			dirfs_fsync,
	.vop_mountctl =			dirfs_mountctl,
	.vop_nremove =			dirfs_nremove,
	.vop_nlink =			dirfs_nlink,
	.vop_nrename =			dirfs_nrename,
	.vop_nmkdir =			dirfs_nmkdir,
	.vop_nrmdir =			dirfs_nrmdir,
	.vop_nsymlink =			dirfs_nsymlink,
	.vop_readdir =			dirfs_readdir,
	.vop_readlink =			dirfs_readlink,
	.vop_inactive =			dirfs_inactive,
	.vop_reclaim =			dirfs_reclaim,
	.vop_print =			dirfs_print,
	.vop_pathconf =			vop_stdpathconf,
	.vop_bmap =			dirfs_bmap,
	.vop_strategy =			dirfs_strategy,
	.vop_advlock =			dirfs_advlock,
	.vop_kqfilter =			dirfs_kqfilter,
	.vop_getpages =			vop_stdgetpages,
	.vop_putpages =			vop_stdputpages
};
