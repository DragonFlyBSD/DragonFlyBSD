/*
 * 43BSD_FILE.C		- 4.3BSD compatibility file syscalls
 *
 * Copyright (c) 1989, 1993
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
 * $DragonFly: src/sys/emulation/43bsd/43bsd_file.c,v 1.3 2003/11/11 09:28:15 daver Exp $
 * 	from: DragonFly kern/vfs_syscalls.c,v 1.20
 *
 * These syscalls used to live in kern/vfs_syscalls.c.  They are modified
 * to use the new split syscalls.
 */

#include "opt_compat.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/sysproto.h>
#include <sys/conf.h>
#include <sys/dirent.h>
#include <sys/fcntl.h>
#include <sys/file.h>
#include <sys/filedesc.h>
#include <sys/kernel.h>
#include <sys/kern_syscall.h>
#include <sys/malloc.h>
#include <sys/mount.h>
#include <sys/namei.h>
#include <sys/uio.h>
#include <sys/vnode.h>

#include <vfs/union/union.h>

int
ocreat(struct ocreat_args *uap)
{
	struct thread *td = curthread;
	struct nameidata nd;
	int error;

	NDINIT(&nd, NAMEI_LOOKUP, CNP_FOLLOW, UIO_USERSPACE, uap->path, td);

	error = kern_open(&nd, O_WRONLY | O_CREAT | O_TRUNC, uap->mode,
	    &uap->sysmsg_result);

	return (error);
}

int
oftruncate(struct oftruncate_args *uap)
{
	int error;

	error = kern_ftruncate(uap->fd, uap->length);

	return (error);
}

int
olseek(struct olseek_args *uap)
{
	int error;

	error = kern_lseek(uap->fd, uap->offset, uap->whence,
	    &uap->sysmsg_offset);

	return (error);
}

int
otruncate(struct otruncate_args *uap)
{
	struct thread *td = curthread;
	struct nameidata nd;
	int error;

	NDINIT(&nd, NAMEI_LOOKUP, CNP_FOLLOW, UIO_USERSPACE, uap->path, td);

	error = kern_truncate(&nd, uap->length);

	return (error);
}

int
ogetdirentries(struct ogetdirentries_args *uap)
{
	struct thread *td = curthread;
	struct proc *p = td->td_proc;
	struct vnode *vp;
	struct file *fp;
	struct uio auio, kuio;
	struct iovec aiov, kiov;
	struct dirent *dp, *edp;
	caddr_t dirbuf;
	int error, eofflag, readcnt;
	long loff;

	/* XXX arbitrary sanity limit on `count'. */
	if (uap->count > 64 * 1024)
		return (EINVAL);
	if ((error = getvnode(p->p_fd, uap->fd, &fp)) != 0)
		return (error);
	if ((fp->f_flag & FREAD) == 0)
		return (EBADF);
	vp = (struct vnode *)fp->f_data;
unionread:
	if (vp->v_type != VDIR)
		return (EINVAL);
	aiov.iov_base = uap->buf;
	aiov.iov_len = uap->count;
	auio.uio_iov = &aiov;
	auio.uio_iovcnt = 1;
	auio.uio_rw = UIO_READ;
	auio.uio_segflg = UIO_USERSPACE;
	auio.uio_td = td;
	auio.uio_resid = uap->count;
	vn_lock(vp, LK_EXCLUSIVE | LK_RETRY, td);
	loff = auio.uio_offset = fp->f_offset;
#	if (BYTE_ORDER != LITTLE_ENDIAN)
		if (vp->v_mount->mnt_maxsymlinklen <= 0) {
			error = VOP_READDIR(vp, &auio, fp->f_cred, &eofflag,
			    NULL, NULL);
			fp->f_offset = auio.uio_offset;
		} else
#	endif
	{
		kuio = auio;
		kuio.uio_iov = &kiov;
		kuio.uio_segflg = UIO_SYSSPACE;
		kiov.iov_len = uap->count;
		MALLOC(dirbuf, caddr_t, uap->count, M_TEMP, M_WAITOK);
		kiov.iov_base = dirbuf;
		error = VOP_READDIR(vp, &kuio, fp->f_cred, &eofflag,
			    NULL, NULL);
		fp->f_offset = kuio.uio_offset;
		if (error == 0) {
			readcnt = uap->count - kuio.uio_resid;
			edp = (struct dirent *)&dirbuf[readcnt];
			for (dp = (struct dirent *)dirbuf; dp < edp; ) {
#				if (BYTE_ORDER == LITTLE_ENDIAN)
					/*
					 * The expected low byte of
					 * dp->d_namlen is our dp->d_type.
					 * The high MBZ byte of dp->d_namlen
					 * is our dp->d_namlen.
					 */
					dp->d_type = dp->d_namlen;
					dp->d_namlen = 0;
#				else
					/*
					 * The dp->d_type is the high byte
					 * of the expected dp->d_namlen,
					 * so must be zero'ed.
					 */
					dp->d_type = 0;
#				endif
				if (dp->d_reclen > 0) {
					dp = (struct dirent *)
					    ((char *)dp + dp->d_reclen);
				} else {
					error = EIO;
					break;
				}
			}
			if (dp >= edp)
				error = uiomove(dirbuf, readcnt, &auio);
		}
		FREE(dirbuf, M_TEMP);
	}
	VOP_UNLOCK(vp, 0, td);
	if (error)
		return (error);
	if (uap->count == auio.uio_resid) {
		if (union_dircheckp) {
			error = union_dircheckp(td, &vp, fp);
			if (error == -1)
				goto unionread;
			if (error)
				return (error);
		}
		if ((vp->v_flag & VROOT) &&
		    (vp->v_mount->mnt_flag & MNT_UNION)) {
			struct vnode *tvp = vp;
			vp = vp->v_mount->mnt_vnodecovered;
			VREF(vp);
			fp->f_data = (caddr_t) vp;
			fp->f_offset = 0;
			vrele(tvp);
			goto unionread;
		}
	}
	error = copyout(&loff, uap->basep, sizeof(long));
	uap->sysmsg_result = uap->count - auio.uio_resid;
	return (error);
}
