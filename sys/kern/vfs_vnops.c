/*
 * Copyright (c) 1982, 1986, 1989, 1993
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
 *	@(#)vfs_vnops.c	8.2 (Berkeley) 1/21/94
 * $FreeBSD: src/sys/kern/vfs_vnops.c,v 1.87.2.13 2002/12/29 18:19:53 dillon Exp $
 * $DragonFly: src/sys/kern/vfs_vnops.c,v 1.9 2003/07/22 17:03:33 dillon Exp $
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/proc.h>
#include <sys/mount.h>
#include <sys/namei.h>
#include <sys/vnode.h>
#include <sys/buf.h>
#include <sys/filio.h>
#include <sys/ttycom.h>
#include <sys/conf.h>
#include <sys/syslog.h>

static int vn_closefile __P((struct file *fp, struct thread *td));
static int vn_ioctl __P((struct file *fp, u_long com, caddr_t data, 
		struct thread *td));
static int vn_read __P((struct file *fp, struct uio *uio, 
		struct ucred *cred, int flags, struct thread *td));
static int vn_poll __P((struct file *fp, int events, struct ucred *cred,
		struct thread *td));
static int vn_kqfilter __P((struct file *fp, struct knote *kn));
static int vn_statfile __P((struct file *fp, struct stat *sb, struct thread *td));
static int vn_write __P((struct file *fp, struct uio *uio, 
		struct ucred *cred, int flags, struct thread *td));

struct 	fileops vnops = {
	vn_read, vn_write, vn_ioctl, vn_poll, vn_kqfilter,
	vn_statfile, vn_closefile
};

/*
 * Common code for vnode open operations.
 * Check permissions, and call the VOP_OPEN or VOP_CREATE routine.
 * 
 * Note that this does NOT free nameidata for the successful case,
 * due to the NDINIT being done elsewhere.
 */
int
vn_open(ndp, fmode, cmode)
	register struct nameidata *ndp;
	int fmode, cmode;
{
	register struct vnode *vp;
	register struct thread *td = ndp->ni_cnd.cn_td;
	register struct ucred *cred = ndp->ni_cnd.cn_cred;
	struct vattr vat;
	struct vattr *vap = &vat;
	int mode, error;

	KKASSERT(cred == td->td_proc->p_ucred);

	if (fmode & O_CREAT) {
		ndp->ni_cnd.cn_nameiop = CREATE;
		ndp->ni_cnd.cn_flags = LOCKPARENT | LOCKLEAF;
		if ((fmode & O_EXCL) == 0 && (fmode & O_NOFOLLOW) == 0)
			ndp->ni_cnd.cn_flags |= FOLLOW;
		bwillwrite();
		error = namei(ndp);
		if (error)
			return (error);
		if (ndp->ni_vp == NULL) {
			VATTR_NULL(vap);
			vap->va_type = VREG;
			vap->va_mode = cmode;
			if (fmode & O_EXCL)
				vap->va_vaflags |= VA_EXCLUSIVE;
			VOP_LEASE(ndp->ni_dvp, td, cred, LEASE_WRITE);
			error = VOP_CREATE(ndp->ni_dvp, &ndp->ni_vp,
					   &ndp->ni_cnd, vap);
			if (error) {
				NDFREE(ndp, NDF_ONLY_PNBUF);
				vput(ndp->ni_dvp);
				return (error);
			}
			vput(ndp->ni_dvp);
			ASSERT_VOP_UNLOCKED(ndp->ni_dvp, "create");
			ASSERT_VOP_LOCKED(ndp->ni_vp, "create");
			fmode &= ~O_TRUNC;
			vp = ndp->ni_vp;
		} else {
			if (ndp->ni_dvp == ndp->ni_vp)
				vrele(ndp->ni_dvp);
			else
				vput(ndp->ni_dvp);
			ndp->ni_dvp = NULL;
			vp = ndp->ni_vp;
			if (fmode & O_EXCL) {
				error = EEXIST;
				goto bad;
			}
			fmode &= ~O_CREAT;
		}
	} else {
		ndp->ni_cnd.cn_nameiop = LOOKUP;
		ndp->ni_cnd.cn_flags =
		    ((fmode & O_NOFOLLOW) ? NOFOLLOW : FOLLOW) | LOCKLEAF;
		error = namei(ndp);
		if (error)
			return (error);
		vp = ndp->ni_vp;
	}
	if (vp->v_type == VLNK) {
		error = EMLINK;
		goto bad;
	}
	if (vp->v_type == VSOCK) {
		error = EOPNOTSUPP;
		goto bad;
	}
	if ((fmode & O_CREAT) == 0) {
		mode = 0;
		if (fmode & (FWRITE | O_TRUNC)) {
			if (vp->v_type == VDIR) {
				error = EISDIR;
				goto bad;
			}
			error = vn_writechk(vp);
			if (error)
				goto bad;
			mode |= VWRITE;
		}
		if (fmode & FREAD)
			mode |= VREAD;
		if (mode) {
		        error = VOP_ACCESS(vp, mode, cred, td);
			if (error)
				goto bad;
		}
	}
	if (fmode & O_TRUNC) {
		VOP_UNLOCK(vp, 0, td);				/* XXX */
		VOP_LEASE(vp, td, cred, LEASE_WRITE);
		vn_lock(vp, LK_EXCLUSIVE | LK_RETRY, td);	/* XXX */
		VATTR_NULL(vap);
		vap->va_size = 0;
		error = VOP_SETATTR(vp, vap, cred, td);
		if (error)
			goto bad;
	}
	error = VOP_OPEN(vp, fmode, cred, td);
	if (error)
		goto bad;
	/*
	 * Make sure that a VM object is created for VMIO support.
	 */
	if (vn_canvmio(vp) == TRUE) {
		if ((error = vfs_object_create(vp, td)) != 0)
			goto bad;
	}

	if (fmode & FWRITE)
		vp->v_writecount++;
	return (0);
bad:
	NDFREE(ndp, NDF_ONLY_PNBUF);
	vput(vp);
	return (error);
}

/*
 * Check for write permissions on the specified vnode.
 * Prototype text segments cannot be written.
 */
int
vn_writechk(vp)
	register struct vnode *vp;
{

	/*
	 * If there's shared text associated with
	 * the vnode, try to free it up once.  If
	 * we fail, we can't allow writing.
	 */
	if (vp->v_flag & VTEXT)
		return (ETXTBSY);
	return (0);
}

/*
 * Vnode close call
 */
int
vn_close(struct vnode *vp, int flags, struct thread *td)
{
	int error;

	if (flags & FWRITE)
		vp->v_writecount--;
	error = VOP_CLOSE(vp, flags, td);
	vrele(vp);
	return (error);
}

static __inline
int
sequential_heuristic(struct uio *uio, struct file *fp)
{
	/*
	 * Sequential heuristic - detect sequential operation
	 */
	if ((uio->uio_offset == 0 && fp->f_seqcount > 0) ||
	    uio->uio_offset == fp->f_nextoff) {
		int tmpseq = fp->f_seqcount;
		/*
		 * XXX we assume that the filesystem block size is
		 * the default.  Not true, but still gives us a pretty
		 * good indicator of how sequential the read operations
		 * are.
		 */
		tmpseq += (uio->uio_resid + BKVASIZE - 1) / BKVASIZE;
		if (tmpseq > IO_SEQMAX)
			tmpseq = IO_SEQMAX;
		fp->f_seqcount = tmpseq;
		return(fp->f_seqcount << IO_SEQSHIFT);
	}

	/*
	 * Not sequential, quick draw-down of seqcount
	 */
	if (fp->f_seqcount > 1)
		fp->f_seqcount = 1;
	else
		fp->f_seqcount = 0;
	return(0);
}

/*
 * Package up an I/O request on a vnode into a uio and do it.
 */
int
vn_rdwr(rw, vp, base, len, offset, segflg, ioflg, cred, aresid, td)
	enum uio_rw rw;
	struct vnode *vp;
	caddr_t base;
	int len;
	off_t offset;
	enum uio_seg segflg;
	int ioflg;
	struct ucred *cred;
	int *aresid;
	struct thread *td;
{
	struct uio auio;
	struct iovec aiov;
	int error;

	if ((ioflg & IO_NODELOCKED) == 0)
		vn_lock(vp, LK_EXCLUSIVE | LK_RETRY, td);
	auio.uio_iov = &aiov;
	auio.uio_iovcnt = 1;
	aiov.iov_base = base;
	aiov.iov_len = len;
	auio.uio_resid = len;
	auio.uio_offset = offset;
	auio.uio_segflg = segflg;
	auio.uio_rw = rw;
	auio.uio_td = td;
	if (rw == UIO_READ) {
		error = VOP_READ(vp, &auio, ioflg, cred);
	} else {
		error = VOP_WRITE(vp, &auio, ioflg, cred);
	}
	if (aresid)
		*aresid = auio.uio_resid;
	else
		if (auio.uio_resid && error == 0)
			error = EIO;
	if ((ioflg & IO_NODELOCKED) == 0)
		VOP_UNLOCK(vp, 0, td);
	return (error);
}

/*
 * Package up an I/O request on a vnode into a uio and do it.  The I/O
 * request is split up into smaller chunks and we try to avoid saturating
 * the buffer cache while potentially holding a vnode locked, so we 
 * check bwillwrite() before calling vn_rdwr().  We also call uio_yield()
 * to give other processes a chance to lock the vnode (either other processes
 * core'ing the same binary, or unrelated processes scanning the directory).
 */
int
vn_rdwr_inchunks(rw, vp, base, len, offset, segflg, ioflg, cred, aresid, td)
	enum uio_rw rw;
	struct vnode *vp;
	caddr_t base;
	int len;
	off_t offset;
	enum uio_seg segflg;
	int ioflg;
	struct ucred *cred;
	int *aresid;
	struct thread *td;
{
	int error = 0;

	do {
		int chunk = (len > MAXBSIZE) ? MAXBSIZE : len;

		if (rw != UIO_READ && vp->v_type == VREG)
			bwillwrite();
		error = vn_rdwr(rw, vp, base, chunk, offset, segflg,
			    ioflg, cred, aresid, td);
		len -= chunk;	/* aresid calc already includes length */
		if (error)
			break;
		offset += chunk;
		base += chunk;
		uio_yield();
	} while (len);
	if (aresid)
		*aresid += len;
	return (error);
}

/*
 * File table vnode read routine.
 */
static int
vn_read(fp, uio, cred, flags, td)
	struct file *fp;
	struct uio *uio;
	struct ucred *cred;
	struct thread *td;
	int flags;
{
	struct vnode *vp;
	int error, ioflag;

	KASSERT(uio->uio_td == td, ("uio_td %p is not td %p", uio->uio_td, td));
	vp = (struct vnode *)fp->f_data;
	ioflag = 0;
	if (fp->f_flag & FNONBLOCK)
		ioflag |= IO_NDELAY;
	if (fp->f_flag & O_DIRECT)
		ioflag |= IO_DIRECT;
	VOP_LEASE(vp, td, cred, LEASE_READ);
	vn_lock(vp, LK_SHARED | LK_NOPAUSE | LK_RETRY, td);
	if ((flags & FOF_OFFSET) == 0)
		uio->uio_offset = fp->f_offset;

	ioflag |= sequential_heuristic(uio, fp);

	error = VOP_READ(vp, uio, ioflag, cred);
	if ((flags & FOF_OFFSET) == 0)
		fp->f_offset = uio->uio_offset;
	fp->f_nextoff = uio->uio_offset;
	VOP_UNLOCK(vp, 0, td);
	return (error);
}

/*
 * File table vnode write routine.
 */
static int
vn_write(fp, uio, cred, flags, td)
	struct file *fp;
	struct uio *uio;
	struct ucred *cred;
	struct thread *td;
	int flags;
{
	struct vnode *vp;
	int error, ioflag;

	KASSERT(uio->uio_td == td, ("uio_procp %p is not p %p", 
	    uio->uio_td, td));
	vp = (struct vnode *)fp->f_data;
	if (vp->v_type == VREG)
		bwillwrite();
	vp = (struct vnode *)fp->f_data;	/* XXX needed? */
	ioflag = IO_UNIT;
	if (vp->v_type == VREG && (fp->f_flag & O_APPEND))
		ioflag |= IO_APPEND;
	if (fp->f_flag & FNONBLOCK)
		ioflag |= IO_NDELAY;
	if (fp->f_flag & O_DIRECT)
		ioflag |= IO_DIRECT;
	if ((fp->f_flag & O_FSYNC) ||
	    (vp->v_mount && (vp->v_mount->mnt_flag & MNT_SYNCHRONOUS)))
		ioflag |= IO_SYNC;
	VOP_LEASE(vp, td, cred, LEASE_WRITE);
	vn_lock(vp, LK_EXCLUSIVE | LK_RETRY, td);
	if ((flags & FOF_OFFSET) == 0)
		uio->uio_offset = fp->f_offset;
	ioflag |= sequential_heuristic(uio, fp);
	error = VOP_WRITE(vp, uio, ioflag, cred);
	if ((flags & FOF_OFFSET) == 0)
		fp->f_offset = uio->uio_offset;
	fp->f_nextoff = uio->uio_offset;
	VOP_UNLOCK(vp, 0, td);
	return (error);
}

/*
 * File table vnode stat routine.
 */
static int
vn_statfile(struct file *fp, struct stat *sb, struct thread *td)
{
	struct vnode *vp = (struct vnode *)fp->f_data;

	return vn_stat(vp, sb, td);
}

int
vn_stat(struct vnode *vp, struct stat *sb, struct thread *td)
{
	struct vattr vattr;
	struct vattr *vap;
	int error;
	u_short mode;

	vap = &vattr;
	error = VOP_GETATTR(vp, vap, td);
	if (error)
		return (error);

	/*
	 * Zero the spare stat fields
	 */
	sb->st_lspare = 0;
	sb->st_qspare[0] = 0;
	sb->st_qspare[1] = 0;

	/*
	 * Copy from vattr table
	 */
	if (vap->va_fsid != VNOVAL)
		sb->st_dev = vap->va_fsid;
	else
		sb->st_dev = vp->v_mount->mnt_stat.f_fsid.val[0];
	sb->st_ino = vap->va_fileid;
	mode = vap->va_mode;
	switch (vap->va_type) {
	case VREG:
		mode |= S_IFREG;
		break;
	case VDIR:
		mode |= S_IFDIR;
		break;
	case VBLK:
		mode |= S_IFBLK;
		break;
	case VCHR:
		mode |= S_IFCHR;
		break;
	case VLNK:
		mode |= S_IFLNK;
		/* This is a cosmetic change, symlinks do not have a mode. */
		if (vp->v_mount->mnt_flag & MNT_NOSYMFOLLOW)
			sb->st_mode &= ~ACCESSPERMS;	/* 0000 */
		else
			sb->st_mode |= ACCESSPERMS;	/* 0777 */
		break;
	case VSOCK:
		mode |= S_IFSOCK;
		break;
	case VFIFO:
		mode |= S_IFIFO;
		break;
	default:
		return (EBADF);
	};
	sb->st_mode = mode;
	sb->st_nlink = vap->va_nlink;
	sb->st_uid = vap->va_uid;
	sb->st_gid = vap->va_gid;
	sb->st_rdev = vap->va_rdev;
	sb->st_size = vap->va_size;
	sb->st_atimespec = vap->va_atime;
	sb->st_mtimespec = vap->va_mtime;
	sb->st_ctimespec = vap->va_ctime;

        /*
	 * According to www.opengroup.org, the meaning of st_blksize is 
	 *   "a filesystem-specific preferred I/O block size for this 
	 *    object.  In some filesystem types, this may vary from file
	 *    to file"
	 * Default to PAGE_SIZE after much discussion.
	 */

	if (vap->va_type == VREG) {
		sb->st_blksize = vap->va_blocksize;
	} else if (vn_isdisk(vp, NULL)) {
		sb->st_blksize = vp->v_rdev->si_bsize_best;
		if (sb->st_blksize < vp->v_rdev->si_bsize_phys)
			sb->st_blksize = vp->v_rdev->si_bsize_phys;
		if (sb->st_blksize < BLKDEV_IOSIZE)
			sb->st_blksize = BLKDEV_IOSIZE;
	} else {
		sb->st_blksize = PAGE_SIZE;
	}
	
	sb->st_flags = vap->va_flags;
	if (suser(td))
		sb->st_gen = 0;
	else
		sb->st_gen = vap->va_gen;

#if (S_BLKSIZE == 512)
	/* Optimize this case */
	sb->st_blocks = vap->va_bytes >> 9;
#else
	sb->st_blocks = vap->va_bytes / S_BLKSIZE;
#endif
	return (0);
}

/*
 * File table vnode ioctl routine.
 */
static int
vn_ioctl(struct file *fp, u_long com, caddr_t data, struct thread *td)
{
	struct vnode *vp = ((struct vnode *)fp->f_data);
	struct ucred *ucred;
	struct vattr vattr;
	int error;

	KKASSERT(td->td_proc != NULL);
	ucred = td->td_proc->p_ucred;

	switch (vp->v_type) {
	case VREG:
	case VDIR:
		if (com == FIONREAD) {
			error = VOP_GETATTR(vp, &vattr, td);
			if (error)
				return (error);
			*(int *)data = vattr.va_size - fp->f_offset;
			return (0);
		}
		if (com == FIONBIO || com == FIOASYNC)	/* XXX */
			return (0);			/* XXX */
		/* fall into ... */
	default:
#if 0
		return (ENOTTY);
#endif
	case VFIFO:
	case VCHR:
	case VBLK:
		if (com == FIODTYPE) {
			if (vp->v_type != VCHR && vp->v_type != VBLK)
				return (ENOTTY);
			*(int *)data = dev_dflags(vp->v_rdev) & D_TYPEMASK;
			return (0);
		}
		error = VOP_IOCTL(vp, com, data, fp->f_flag, ucred, td);
		if (error == 0 && com == TIOCSCTTY) {
			struct session *sess = td->td_proc->p_session;

			/* Do nothing if reassigning same control tty */
			if (sess->s_ttyvp == vp)
				return (0);

			/* Get rid of reference to old control tty */
			if (sess->s_ttyvp)
				vrele(sess->s_ttyvp);

			sess->s_ttyvp = vp;
			VREF(vp);
		}
		return (error);
	}
}

/*
 * File table vnode poll routine.
 */
static int
vn_poll(struct file *fp, int events, struct ucred *cred, struct thread *td)
{
	return (VOP_POLL(((struct vnode *)fp->f_data), events, cred, td));
}

/*
 * Check that the vnode is still valid, and if so
 * acquire requested lock.
 */
int
#ifndef	DEBUG_LOCKS
vn_lock(struct vnode *vp, int flags, struct thread *td)
#else
debug_vn_lock(struct vnode *vp, int flags, struct thread *td,
		const char *filename, int line)
#endif
{
	int error;
	
	do {
		if ((flags & LK_INTERLOCK) == 0)
			lwkt_gettoken(&vp->v_interlock);
		if ((vp->v_flag & VXLOCK) && vp->v_vxproc != curproc) {
			vp->v_flag |= VXWANT;
			lwkt_reltoken(&vp->v_interlock);
			tsleep((caddr_t)vp, 0, "vn_lock", 0);
			error = ENOENT;
		} else {
#if 0
			/* this can now occur in normal operation */
			if (vp->v_vxproc != NULL)
				log(LOG_INFO, "VXLOCK interlock avoided in vn_lock\n");
#endif
#ifdef	DEBUG_LOCKS
			vp->filename = filename;
			vp->line = line;
#endif
			error = VOP_LOCK(vp,
				    flags | LK_NOPAUSE | LK_INTERLOCK, td);
			if (error == 0)
				return (error);
		}
		flags &= ~LK_INTERLOCK;
	} while (flags & LK_RETRY);
	return (error);
}

/*
 * File table vnode close routine.
 */
static int
vn_closefile(struct file *fp, struct thread *td)
{
	int err;

	fp->f_ops = &badfileops;
	err = vn_close(((struct vnode *)fp->f_data), fp->f_flag, td);
	return(err);
}

static int
vn_kqfilter(struct file *fp, struct knote *kn)
{

	return (VOP_KQFILTER(((struct vnode *)fp->f_data), kn));
}
