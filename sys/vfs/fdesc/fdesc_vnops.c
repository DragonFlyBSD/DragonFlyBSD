/*
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software donated to Berkeley by
 * Jan-Simon Pendry.
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
 *	@(#)fdesc_vnops.c	8.9 (Berkeley) 1/21/94
 *
 * $FreeBSD: src/sys/miscfs/fdesc/fdesc_vnops.c,v 1.47.2.1 2001/10/22 22:49:26 chris Exp $
 */

/*
 * /dev/fd Filesystem
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/conf.h>
#include <sys/dirent.h>
#include <sys/filedesc.h>
#include <sys/kernel.h>	/* boottime */
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/file.h>	/* Must come after sys/malloc.h */
#include <sys/mount.h>
#include <sys/proc.h>
#include <sys/namei.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/vnode.h>
#include <sys/file2.h>

#include <machine/limits.h>

#include "fdesc.h"

#define FDL_WANT	0x01
#define FDL_LOCKED	0x02
static int fdcache_lock;

#define	NFDCACHE 4
#define FD_NHASH(ix) \
	(&fdhashtbl[(ix) & fdhash])
static LIST_HEAD(fdhashhead, fdescnode) *fdhashtbl;
static u_long fdhash;

static int	fdesc_getattr (struct vop_getattr_args *ap);
static int	fdesc_inactive (struct vop_inactive_args *ap);
static int	fdesc_lookup (struct vop_old_lookup_args *ap);
static int	fdesc_open (struct vop_open_args *ap);
static int	fdesc_print (struct vop_print_args *ap);
static int	fdesc_readdir (struct vop_readdir_args *ap);
static int	fdesc_reclaim (struct vop_reclaim_args *ap);
static int	fdesc_setattr (struct vop_setattr_args *ap);

/*
 * Initialise cache headers
 */
int
fdesc_init(struct vfsconf *vfsp)
{

	fdhashtbl = hashinit(NFDCACHE, M_CACHE, &fdhash);
	return (0);
}

int
fdesc_uninit(struct vfsconf *vfsp)
{
	if (fdhashtbl)
		hashdestroy(fdhashtbl, M_CACHE, fdhash);
	return (0);
}
int
fdesc_allocvp(fdntype ftype, int ix, struct mount *mp, struct vnode **vpp)
{
	struct fdhashhead *fc;
	struct fdescnode *fd;
	int error = 0;

	fc = FD_NHASH(ix);
loop:
	LIST_FOREACH(fd, fc, fd_hash) {
		if (fd->fd_ix == ix && fd->fd_vnode->v_mount == mp) {
			if (vget(fd->fd_vnode, LK_EXCLUSIVE|LK_SLEEPFAIL))
				goto loop;
			*vpp = fd->fd_vnode;
			return (error);
		}
	}

	/*
	 * otherwise lock the array while we call getnewvnode
	 * since that can block.
	 */
	if (fdcache_lock & FDL_LOCKED) {
		fdcache_lock |= FDL_WANT;
		tsleep((caddr_t) &fdcache_lock, 0, "fdalvp", 0);
		goto loop;
	}
	fdcache_lock |= FDL_LOCKED;

	/*
	 * Do the MALLOC before the getnewvnode since doing so afterward
	 * might cause a bogus v_data pointer to get dereferenced
	 * elsewhere if MALLOC should block.
	 */
	fd = kmalloc(sizeof(struct fdescnode), M_TEMP, M_WAITOK);

	error = getnewvnode(VT_FDESC, mp, vpp, 0, 0);
	if (error) {
		kfree(fd, M_TEMP);
		goto out;
	}
	(*vpp)->v_data = fd;
	fd->fd_vnode = *vpp;
	fd->fd_type = ftype;
	fd->fd_fd = -1;
	fd->fd_ix = ix;
	LIST_INSERT_HEAD(fc, fd, fd_hash);
	vx_unlock(*vpp);

out:
	fdcache_lock &= ~FDL_LOCKED;

	if (fdcache_lock & FDL_WANT) {
		fdcache_lock &= ~FDL_WANT;
		wakeup((caddr_t) &fdcache_lock);
	}

	return (error);
}

/*
 * vp is the current namei directory
 * ndp is the name to locate in that directory...
 *
 * fdesc_lookup(struct vnode *a_dvp, struct vnode **a_vpp,
 *		struct componentname *a_cnp)
 */
static int
fdesc_lookup(struct vop_old_lookup_args *ap)
{
	struct componentname *cnp = ap->a_cnp;
	struct thread *td = cnp->cn_td;
	struct proc *p = td->td_proc;
	struct vnode **vpp = ap->a_vpp;
	struct vnode *dvp = ap->a_dvp;
	char *pname = cnp->cn_nameptr;
	int nlen = cnp->cn_namelen;
	int nfiles;
	u_int fd;
	int error;
	struct vnode *fvp;

	KKASSERT(p);
	nfiles = p->p_fd->fd_nfiles;
	if (cnp->cn_nameiop == NAMEI_DELETE || cnp->cn_nameiop == NAMEI_RENAME) {
		error = EROFS;
		goto bad;
	}

	vn_unlock(dvp);
	if (cnp->cn_namelen == 1 && *pname == '.') {
		*vpp = dvp;
		vref(dvp);	
		vn_lock(dvp, LK_SHARED | LK_RETRY);
		return (0);
	}

	if (VTOFDESC(dvp)->fd_type != Froot) {
		error = ENOTDIR;
		goto bad;
	}

	fd = 0;
	/* the only time a leading 0 is acceptable is if it's "0" */
	if (*pname == '0' && nlen != 1) {
		error = ENOENT;
		goto bad;
	}
	while (nlen--) {
		if (*pname < '0' || *pname > '9') {
			error = ENOENT;
			goto bad;
		}
		fd = 10 * fd + *pname++ - '0';
	}

	if (fd >= nfiles || p->p_fd->fd_files[fd].fp == NULL) {
		error = EBADF;
		goto bad;
	}

	error = fdesc_allocvp(Fdesc, FD_DESC+fd, dvp->v_mount, &fvp);
	if (error)
		goto bad;
	VTOFDESC(fvp)->fd_fd = fd;
	vn_lock(fvp, LK_SHARED | LK_RETRY);
	*vpp = fvp;
	return (0);

bad:
	vn_lock(dvp, LK_SHARED | LK_RETRY);
	*vpp = NULL;
	return (error);
}

/*
 * fdesc_open(struct vnode *a_vp, int a_mode, struct ucred *a_cred,
 *	      struct file *a_fp)
 */
static int
fdesc_open(struct vop_open_args *ap)
{
	struct vnode *vp = ap->a_vp;
	struct lwp *lp = curthread->td_lwp;

	KKASSERT(lp);

	if (VTOFDESC(vp)->fd_type == Froot)
		return (vop_stdopen(ap));

	/*
	 * XXX Kludge: set lp->lwp_dupfd to contain the value of the the file
	 * descriptor being sought for duplication. The error return ensures
	 * that the vnode for this device will be released by vn_open. Open
	 * will detect this special error and take the actions in dupfdopen.
	 * Other callers of vn_open or VOP_OPEN will simply report the
	 * error.
	 */
	lp->lwp_dupfd = VTOFDESC(vp)->fd_fd;	/* XXX */
	return (ENODEV);
}

/*
 * fdesc_getattr(struct vnode *a_vp, struct vattr *a_vap, struct ucred *a_cred)
 */
static int
fdesc_getattr(struct vop_getattr_args *ap)
{
	struct proc *p = curproc;
	struct vnode *vp = ap->a_vp;
	struct vattr *vap = ap->a_vap;
	struct file *fp;
	struct stat stb;
	u_int fd;
	int error = 0;

	KKASSERT(p);

	switch (VTOFDESC(vp)->fd_type) {
	case Froot:
		VATTR_NULL(vap);

		vap->va_mode = S_IRUSR|S_IXUSR|S_IRGRP|S_IXGRP|S_IROTH|S_IXOTH;
		vap->va_type = VDIR;
		vap->va_nlink = 2;
		vap->va_size = DEV_BSIZE;
		vap->va_fileid = VTOFDESC(vp)->fd_ix;
		vap->va_uid = 0;
		vap->va_gid = 0;
		vap->va_blocksize = DEV_BSIZE;
		vap->va_atime.tv_sec = boottime.tv_sec;
		vap->va_atime.tv_nsec = 0;
		vap->va_mtime = vap->va_atime;
		vap->va_ctime = vap->va_mtime;
		vap->va_gen = 0;
		vap->va_flags = 0;
		vap->va_rmajor = VNOVAL;
		vap->va_rminor = VNOVAL;
		vap->va_bytes = 0;
		break;

	case Fdesc:
		fd = VTOFDESC(vp)->fd_fd;

		fp = holdfp(p->p_fd, fd, -1);
		if (fp == NULL)
			return (EBADF);

		bzero(&stb, sizeof(stb));
		error = fo_stat(fp, &stb, curproc->p_ucred);
		fdrop(fp);

		if (error == 0) {
			VATTR_NULL(vap);
			vap->va_type = IFTOVT(stb.st_mode);
			vap->va_mode = stb.st_mode;
#define FDRX (VREAD|VEXEC)
			if (vap->va_type == VDIR)
				vap->va_mode &= ~((FDRX)|(FDRX>>3)|(FDRX>>6));
#undef FDRX
			vap->va_nlink = 1;
			vap->va_flags = 0;
			vap->va_bytes = stb.st_blocks * stb.st_blksize;
			vap->va_fileid = VTOFDESC(vp)->fd_ix;
			vap->va_size = stb.st_size;
			vap->va_blocksize = stb.st_blksize;
			vap->va_rmajor = umajor(stb.st_rdev);
			vap->va_rminor = uminor(stb.st_rdev);

			/*
			 * If no time data is provided, use the current time.
			 */
			if (stb.st_atimespec.tv_sec == 0 &&
			    stb.st_atimespec.tv_nsec == 0)
				nanotime(&stb.st_atimespec);

			if (stb.st_ctimespec.tv_sec == 0 &&
			    stb.st_ctimespec.tv_nsec == 0)
				nanotime(&stb.st_ctimespec);

			if (stb.st_mtimespec.tv_sec == 0 &&
			    stb.st_mtimespec.tv_nsec == 0)
				nanotime(&stb.st_mtimespec);

			vap->va_atime = stb.st_atimespec;
			vap->va_mtime = stb.st_mtimespec;
			vap->va_ctime = stb.st_ctimespec;
			vap->va_uid = stb.st_uid;
			vap->va_gid = stb.st_gid;
		}
		break;

	default:
		panic("fdesc_getattr");
		break;
	}

	if (error == 0)
		vp->v_type = vap->va_type;
	return (error);
}

/*
 * fdesc_setattr(struct vnode *a_vp, struct vattr *a_vap,
 *		 struct ucred *a_cred)
 */
static int
fdesc_setattr(struct vop_setattr_args *ap)
{
	struct proc *p = curproc;
	struct vattr *vap = ap->a_vap;
	struct file *fp;
	unsigned fd;
	int error;

	/*
	 * Can't mess with the root vnode
	 */
	if (VTOFDESC(ap->a_vp)->fd_type == Froot)
		return (EACCES);

	fd = VTOFDESC(ap->a_vp)->fd_fd;
	KKASSERT(p);

	/*
	 * Allow setattr where there is an underlying vnode.
	 */
	error = holdvnode(p->p_fd, fd, &fp);
	if (error) {
		/*
		 * holdvnode() returns EINVAL if the file descriptor is not
		 * backed by a vnode.  Silently drop all changes except
		 * chflags(2) in this case.
		 */
		if (error == EINVAL) {
			if (vap->va_flags != VNOVAL)
				error = EOPNOTSUPP;
			else
				error = 0;
		}
	} else {
		fdrop(fp);
	}
	return (error);
}

#define UIO_MX 16

/*
 * fdesc_readdir(struct vnode *a_vp, struct uio *a_uio, struct ucred *a_cred,
 *		 int *a_eofflag, off_t *a_cookies, int a_ncookies)
 */
static int
fdesc_readdir(struct vop_readdir_args *ap)
{
	struct uio *uio = ap->a_uio;
	struct filedesc *fdp;
	int error, i, fcnt;
	size_t namelen;
	char name[20]; /* enough for %d */

	/*
	 * We don't allow exporting fdesc mounts, and currently local
	 * requests do not need cookies.
	 */
	if (ap->a_ncookies)
		panic("fdesc_readdir: not hungry");

	if (VTOFDESC(ap->a_vp)->fd_type != Froot)
		panic("fdesc_readdir: not dir");

	if (uio->uio_offset < 0 || uio->uio_offset > INT_MAX)
		return(EINVAL);
	i = (int)uio->uio_offset;
	KKASSERT(uio->uio_td->td_proc);
	fdp = uio->uio_td->td_proc->p_fd;
	error = 0;

	fcnt = i - 2;		/* The first two nodes are `.' and `..' */

	while (fcnt < fdp->fd_nfiles && uio->uio_resid > 0 && !error) {
		switch (i) {
		case 0:	/* `.' */
			if (vop_write_dirent(&error, uio, FD_ROOT + i, DT_DIR,
					 1, "."))
				goto done;
			if (error)
				return (error);
			break;
		case 1: /* `..' */
			if (vop_write_dirent(&error, uio, FD_ROOT + i, DT_DIR,
					     2, ".."))
				goto done;
			if (error)
				return (error);
			break;
		default:
			if (fdp->fd_files[fcnt].fp == NULL) {
				fcnt++;
				continue;
			}

			namelen = ksnprintf(name, sizeof(name), "%d", fcnt);
			if (vop_write_dirent(&error, uio, FD_ROOT + i,
					     DT_UNKNOWN, namelen, name))
				goto done;
			if (error)
				return (error);
			break;
		}
		i++;
		fcnt++;
	}

done:
	if (i >= 2)
		uio->uio_offset = fcnt + 2;
	else
		uio->uio_offset = i;
	return (error);
}

/*
 * fdesc_inactive(struct vnode *a_vp)
 */
static int
fdesc_inactive(struct vop_inactive_args *ap)
{
	struct vnode *vp = ap->a_vp;

	/*
	 * Clear out the v_type field to avoid
	 * nasty things happening in vgone().
	 */
	vp->v_type = VNON;
	return (0);
}

/*
 * fdesc_reclaim(struct vnode *a_vp)
 */
static int
fdesc_reclaim(struct vop_reclaim_args *ap)
{
	struct vnode *vp = ap->a_vp;
	struct fdescnode *fd = VTOFDESC(vp);

	LIST_REMOVE(fd, fd_hash);
	kfree(vp->v_data, M_TEMP);
	vp->v_data = 0;

	return (0);
}

/*
 * Print out the contents of a /dev/fd vnode.
 *
 * fdesc_print(struct vnode *a_vp)
 */
/* ARGSUSED */
static int
fdesc_print(struct vop_print_args *ap)
{
	kprintf("tag VT_NON, fdesc vnode\n");
	return (0);
}

struct vop_ops fdesc_vnode_vops = {
	.vop_default =		vop_defaultop,
	.vop_access =		VOP_NULL,
	.vop_getattr =		fdesc_getattr,
	.vop_inactive =		fdesc_inactive,
	.vop_old_lookup =	fdesc_lookup,
	.vop_open =		fdesc_open,
	.vop_pathconf =		vop_stdpathconf,
	.vop_print =		fdesc_print,
	.vop_readdir =		fdesc_readdir,
	.vop_reclaim =		fdesc_reclaim,
	.vop_setattr =		fdesc_setattr
};

