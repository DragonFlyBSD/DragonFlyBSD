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
 * $DragonFly: src/sys/kern/vfs_vnops.c,v 1.27 2004/11/24 08:37:16 dillon Exp $
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/proc.h>
#include <sys/mount.h>
#include <sys/nlookup.h>
#include <sys/vnode.h>
#include <sys/buf.h>
#include <sys/filio.h>
#include <sys/ttycom.h>
#include <sys/conf.h>
#include <sys/syslog.h>

static int vn_closefile (struct file *fp, struct thread *td);
static int vn_ioctl (struct file *fp, u_long com, caddr_t data, 
		struct thread *td);
static int vn_read (struct file *fp, struct uio *uio, 
		struct ucred *cred, int flags, struct thread *td);
static int svn_read (struct file *fp, struct uio *uio, 
		struct ucred *cred, int flags, struct thread *td);
static int vn_poll (struct file *fp, int events, struct ucred *cred,
		struct thread *td);
static int vn_kqfilter (struct file *fp, struct knote *kn);
static int vn_statfile (struct file *fp, struct stat *sb, struct thread *td);
static int vn_write (struct file *fp, struct uio *uio, 
		struct ucred *cred, int flags, struct thread *td);
static int svn_write (struct file *fp, struct uio *uio, 
		struct ucred *cred, int flags, struct thread *td);

struct fileops vnode_fileops = {
	NULL,	/* port */
	NULL,	/* clone */
	vn_read, vn_write, vn_ioctl, vn_poll, vn_kqfilter,
	vn_statfile, vn_closefile
};

struct fileops specvnode_fileops = {
	NULL,	/* port */
	NULL,	/* clone */
	svn_read, svn_write, vn_ioctl, vn_poll, vn_kqfilter,
	vn_statfile, vn_closefile
};

/*
 * Shortcut the device read/write.  This avoids a lot of vnode junk.
 * Basically the specfs vnops for read and write take the locked vnode,
 * unlock it (because we can't hold the vnode locked while reading or writing
 * a device which may block indefinitely), issues the device operation, then
 * relock the vnode before returning, plus other junk.  This bypasses all
 * of that and just does the device operation.
 */
void
vn_setspecops(struct file *fp)
{
	if (vfs_fastdev && fp->f_ops == &vnode_fileops) {
		fp->f_ops = &specvnode_fileops;
	}
}

/*
 * Common code for vnode open operations.  Check permissions, and call
 * the VOP_NOPEN or VOP_NCREATE routine.
 *
 * The caller is responsible for setting up nd with nlookup_init() and
 * for cleaning it up with nlookup_done(), whether we return an error
 * or not.
 *
 * On success nd->nl_open_vp will hold a referenced and, if requested,
 * locked vnode.  A locked vnode is requested via NLC_LOCKVP.  If fp
 * is non-NULL the vnode will be installed in the file pointer.
 *
 * NOTE: The vnode is referenced just once on return whether or not it
 * is also installed in the file pointer.
 */
int
vn_open(struct nlookupdata *nd, struct file *fp, int fmode, int cmode)
{
	struct vnode *vp;
	struct thread *td = nd->nl_td;
	struct ucred *cred = nd->nl_cred;
	struct vattr vat;
	struct vattr *vap = &vat;
	struct namecache *ncp;
	int mode, error;

	/*
	 * Lookup the path and create or obtain the vnode.  After a
	 * successful lookup a locked nd->nl_ncp will be returned.
	 *
	 * The result of this section should be a locked vnode.
	 *
	 * XXX with only a little work we should be able to avoid locking
	 * the vnode if FWRITE, O_CREAT, and O_TRUNC are *not* set.
	 */
	if (fmode & O_CREAT) {
		/*
		 * CONDITIONAL CREATE FILE CASE
		 *
		 * Setting NLC_CREATE causes a negative hit to store
		 * the negative hit ncp and not return an error.  Then
		 * nc_error or nc_vp may be checked to see if the ncp 
		 * represents a negative hit.  NLC_CREATE also requires
		 * write permission on the governing directory or EPERM
		 * is returned.
		 */
		if ((fmode & O_EXCL) == 0 && (fmode & O_NOFOLLOW) == 0)
			nd->nl_flags |= NLC_FOLLOW;
		nd->nl_flags |= NLC_CREATE;
		bwillwrite();
		error = nlookup(nd);
	} else {
		/*
		 * NORMAL OPEN FILE CASE
		 */
		error = nlookup(nd);
	}

	if (error)
		return (error);
	ncp = nd->nl_ncp;

	/*
	 * split case to allow us to re-resolve and retry the ncp in case
	 * we get ESTALE.
	 */
again:
	if (fmode & O_CREAT) {
		if (ncp->nc_vp == NULL) {
			VATTR_NULL(vap);
			vap->va_type = VREG;
			vap->va_mode = cmode;
			if (fmode & O_EXCL)
				vap->va_vaflags |= VA_EXCLUSIVE;
			error = VOP_NCREATE(ncp, &vp, nd->nl_cred, vap);
			if (error)
				return (error);
			fmode &= ~O_TRUNC;
			ASSERT_VOP_LOCKED(vp, "create");
			/* locked vnode is returned */
		} else {
			if (fmode & O_EXCL) {
				error = EEXIST;
			} else {
				error = cache_vget(ncp, cred, 
						    LK_EXCLUSIVE, &vp);
			}
			if (error)
				return (error);
			fmode &= ~O_CREAT;
		}
	} else {
		error = cache_vget(ncp, cred, LK_EXCLUSIVE, &vp);
		if (error)
			return (error);
	}

	/*
	 * We have a locked vnode and ncp now.  Note that the ncp will
	 * be cleaned up by the caller if nd->nl_ncp is left intact.
	 */
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
			if (error) {
				/*
				 * Special stale handling, re-resolve the
				 * vnode.
				 */
				if (error == ESTALE) {
					vput(vp);
					vp = NULL;
					cache_setunresolved(ncp);
					error = cache_resolve(ncp, cred);
					if (error == 0)
						goto again;
				}
				goto bad;
			}
			mode |= VWRITE;
		}
		if (fmode & FREAD)
			mode |= VREAD;
		if (mode) {
		        error = VOP_ACCESS(vp, mode, cred, td);
			if (error) {
				/*
				 * Special stale handling, re-resolve the
				 * vnode.
				 */
				if (error == ESTALE) {
					vput(vp);
					vp = NULL;
					cache_setunresolved(ncp);
					error = cache_resolve(ncp, cred);
					if (error == 0)
						goto again;
				}
				goto bad;
			}
		}
	}
	if (fmode & O_TRUNC) {
		VOP_UNLOCK(vp, 0, td);			/* XXX */
		VOP_LEASE(vp, td, cred, LEASE_WRITE);
		vn_lock(vp, LK_EXCLUSIVE | LK_RETRY, td);	/* XXX */
		VATTR_NULL(vap);
		vap->va_size = 0;
		error = VOP_SETATTR(vp, vap, cred, td);
		if (error)
			goto bad;
	}

	/*
	 * Setup the fp so VOP_OPEN can override it.  No descriptor has been
	 * associated with the fp yet so we own it clean.  f_data will inherit
	 * our vp reference as long as we do not shift f_ops to &badfileops.
	 * f_ncp inherits nl_ncp .
	 */
	if (fp) {
		fp->f_data = (caddr_t)vp;
		fp->f_flag = fmode & FMASK;
		fp->f_ops = &vnode_fileops;
		fp->f_type = (vp->v_type == VFIFO ? DTYPE_FIFO : DTYPE_VNODE);
		if (vp->v_type == VDIR) {
			fp->f_ncp = nd->nl_ncp;
			nd->nl_ncp = NULL;
			cache_unlock(fp->f_ncp);
		}
	}

	/*
	 * Get rid of nl_ncp.  vn_open does not return it (it returns the
	 * vnode or the file pointer).  Note: we can't leave nl_ncp locked
	 * through the VOP_OPEN anyway since the VOP_OPEN may block, e.g.
	 * on /dev/ttyd0
	 */
	if (nd->nl_ncp) {
		cache_put(nd->nl_ncp);
		nd->nl_ncp = NULL;
	}

	error = VOP_OPEN(vp, fmode, cred, fp, td);
	if (error) {
		/*
		 * setting f_ops to &badfileops will prevent the descriptor
		 * code from trying to close and release the vnode, since
		 * the open failed we do not want to call close.
		 */
		if (fp) {
			fp->f_data = NULL;
			fp->f_ops = &badfileops;
		}
		goto bad;
	}
	if (fmode & FWRITE)
		vp->v_writecount++;

	/*
	 * Make sure that a VM object is created for VMIO support.  If this
	 * fails we have to be sure to match VOP_CLOSE's with VOP_OPEN's.
	 * Cleanup the fp so we can just vput() the vp in 'bad'.
	 */
	if (vn_canvmio(vp) == TRUE) {
		if ((error = vfs_object_create(vp, td)) != 0) {
			if (fp) {
				fp->f_data = NULL;
				fp->f_ops = &badfileops;
			}
			VOP_CLOSE(vp, fmode, td);
			goto bad;
		}
	}

	/*
	 * Return the vnode.  XXX needs some cleaning up.  The vnode is
	 * only returned in the fp == NULL case, otherwise the vnode ref
	 * is inherited by the fp and we unconditionally unlock it.
	 */
	if (fp == NULL) {
		nd->nl_open_vp = vp;
		nd->nl_vp_fmode = fmode;
		if ((nd->nl_flags & NLC_LOCKVP) == 0)
			VOP_UNLOCK(vp, 0, td);
	} else {
		VOP_UNLOCK(vp, 0, td);
	}
	return (0);
bad:
	vput(vp);
	return (error);
}

/*
 * Check for write permissions on the specified vnode.
 * Prototype text segments cannot be written.
 */
int
vn_writechk(vp)
	struct vnode *vp;
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
	if ((error = vn_lock(vp, LK_EXCLUSIVE | LK_RETRY, td)) == 0) {
		error = VOP_CLOSE(vp, flags, td);
		VOP_UNLOCK(vp, 0, td);
	}
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
 *
 * We are going to assume the caller has done the appropriate
 * VOP_LEASE() call before calling vn_rdwr()
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
		int chunk;

		/*
		 * Force `offset' to a multiple of MAXBSIZE except possibly
		 * for the first chunk, so that filesystems only need to
		 * write full blocks except possibly for the first and last
		 * chunks.
		 */
		chunk = MAXBSIZE - (uoff_t)offset % MAXBSIZE;

		if (chunk > len)
			chunk = len;
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
 * Device-optimized file table vnode read routine.
 *
 * This bypasses the VOP table and talks directly to the device.  Most
 * filesystems just route to specfs and can make this optimization.
 */
static int
svn_read(fp, uio, cred, flags, td)
	struct file *fp;
	struct uio *uio;
	struct ucred *cred;
	struct thread *td;
	int flags;
{
	struct vnode *vp;
	int ioflag;
	int error;
	dev_t dev;

	KASSERT(uio->uio_td == td, ("uio_td %p is not td %p", uio->uio_td, td));

	vp = (struct vnode *)fp->f_data;
	if (vp == NULL || vp->v_type == VBAD)
		return (EBADF);

	if ((dev = vp->v_rdev) == NULL)
		return (EBADF);
	reference_dev(dev);

	if (uio->uio_resid == 0)
		return (0);
	if ((flags & FOF_OFFSET) == 0)
		uio->uio_offset = fp->f_offset;

	ioflag = 0;
	if (fp->f_flag & FNONBLOCK)
		ioflag |= IO_NDELAY;
	if (fp->f_flag & O_DIRECT)
		ioflag |= IO_DIRECT;
	ioflag |= sequential_heuristic(uio, fp);

	error = dev_dread(dev, uio, ioflag);

	release_dev(dev);
	if ((flags & FOF_OFFSET) == 0)
		fp->f_offset = uio->uio_offset;
	fp->f_nextoff = uio->uio_offset;
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
 * Device-optimized file table vnode write routine.
 *
 * This bypasses the VOP table and talks directly to the device.  Most
 * filesystems just route to specfs and can make this optimization.
 */
static int
svn_write(fp, uio, cred, flags, td)
	struct file *fp;
	struct uio *uio;
	struct ucred *cred;
	struct thread *td;
	int flags;
{
	struct vnode *vp;
	int ioflag;
	int error;
	dev_t dev;

	KASSERT(uio->uio_td == td, ("uio_procp %p is not p %p", 
	    uio->uio_td, td));

	vp = (struct vnode *)fp->f_data;
	if (vp == NULL || vp->v_type == VBAD)
		return (EBADF);
	if (vp->v_type == VREG)
		bwillwrite();
	vp = (struct vnode *)fp->f_data;	/* XXX needed? */

	if ((dev = vp->v_rdev) == NULL)
		return (EBADF);
	reference_dev(dev);

	if ((flags & FOF_OFFSET) == 0)
		uio->uio_offset = fp->f_offset;

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
	ioflag |= sequential_heuristic(uio, fp);

	error = dev_dwrite(dev, uio, ioflag);

	release_dev(dev);
	if ((flags & FOF_OFFSET) == 0)
		fp->f_offset = uio->uio_offset;
	fp->f_nextoff = uio->uio_offset;

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
		/*
		 * XXX this is broken.  If the device is not yet open (aka
		 * stat() call, aka v_rdev == NULL), how are we supposed
		 * to get a valid block size out of it?
		 */
		dev_t dev;

		if ((dev = vp->v_rdev) == NULL)
			dev = udev2dev(vp->v_udev, vp->v_type == VBLK);
		sb->st_blksize = dev->si_bsize_best;
		if (sb->st_blksize < dev->si_bsize_phys)
			sb->st_blksize = dev->si_bsize_phys;
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
	struct vnode *ovp;
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
			ovp = sess->s_ttyvp;
			vref(vp);
			sess->s_ttyvp = vp;
			if (ovp)
				vrele(ovp);
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
#ifdef	DEBUG_LOCKS
		vp->filename = filename;
		vp->line = line;
#endif
		error = VOP_LOCK(vp, flags | LK_NOPAUSE, td);
		if (error == 0)
			break;
	} while (flags & LK_RETRY);

	/*
	 * Because we (had better!) have a ref on the vnode, once it
	 * goes to VRECLAIMED state it will not be recycled until all
	 * refs go away.  So we can just check the flag.
	 */
	if (error == 0 && (vp->v_flag & VRECLAIMED)) {
		VOP_UNLOCK(vp, 0, td);
		error = ENOENT;
	}
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
