/*-
 * Copyright (c) 1994-1995 Søren Schmidt
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer 
 *    in this position and unchanged.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software withough specific prior written permission
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD: src/sys/compat/linux/linux_file.c,v 1.41.2.6 2003/01/06 09:19:43 fjoe Exp $
 * $DragonFly: src/sys/emulation/linux/linux_file.c,v 1.18 2004/10/12 19:20:37 dillon Exp $
 */

#include "opt_compat.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/conf.h>
#include <sys/dirent.h>
#include <sys/fcntl.h>
#include <sys/file.h>
#include <sys/filedesc.h>
#include <sys/kern_syscall.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mount.h>
#include <sys/namei.h>
#include <sys/nlookup.h>
#include <sys/proc.h>
#include <sys/sysproto.h>
#include <sys/tty.h>
#include <sys/vnode.h>

#include <vfs/ufs/quota.h>
#include <vfs/ufs/ufsmount.h>

#include <sys/file2.h>

#include <arch_linux/linux.h>
#include <arch_linux/linux_proto.h>
#include "linux_util.h"

#ifndef __alpha__
int
linux_creat(struct linux_creat_args *args)
{
	struct thread *td = curthread;
	struct nameidata nd;
	char *path;
	int error;

	error = linux_copyin_path(args->path, &path, LINUX_PATH_CREATE);
	if (error)
		return (error);
#ifdef DEBUG
	if (ldebug(creat))
		printf(ARGS(creat, "%s, %d"), path, args->mode);
#endif
	NDINIT(&nd, NAMEI_LOOKUP, CNP_FOLLOW, UIO_SYSSPACE, path, td);

	error = kern_open(&nd, O_WRONLY | O_CREAT | O_TRUNC, args->mode,
	    &args->sysmsg_result);

	linux_free_path(&path);
	return(error);
}
#endif /*!__alpha__*/

int
linux_open(struct linux_open_args *args)
{
	struct thread *td = curthread;
	struct proc *p = td->td_proc;
	struct nameidata nd;
	char *path;
	int error, flags;

	KKASSERT(p);

	if (args->flags & LINUX_O_CREAT) {
		error = linux_copyin_path(args->path, &path,
		    LINUX_PATH_CREATE);
	} else {
		error = linux_copyin_path(args->path, &path,
		    LINUX_PATH_EXISTS);
	}
	if (error)
		return (error);

#ifdef DEBUG
	if (ldebug(open))
		printf(ARGS(open, "%s, 0x%x, 0x%x"), path, args->flags,
		    args->mode);
#endif
	flags = 0;
	if (args->flags & LINUX_O_RDONLY)
		flags |= O_RDONLY;
	if (args->flags & LINUX_O_WRONLY)
		flags |= O_WRONLY;
	if (args->flags & LINUX_O_RDWR)
		flags |= O_RDWR;
	if (args->flags & LINUX_O_NDELAY)
		flags |= O_NONBLOCK;
	if (args->flags & LINUX_O_APPEND)
		flags |= O_APPEND;
	if (args->flags & LINUX_O_SYNC)
		flags |= O_FSYNC;
	if (args->flags & LINUX_O_NONBLOCK)
		flags |= O_NONBLOCK;
	if (args->flags & LINUX_FASYNC)
		flags |= O_ASYNC;
	if (args->flags & LINUX_O_CREAT)
		flags |= O_CREAT;
	if (args->flags & LINUX_O_TRUNC)
		flags |= O_TRUNC;
	if (args->flags & LINUX_O_EXCL)
		flags |= O_EXCL;
	if (args->flags & LINUX_O_NOCTTY)
		flags |= O_NOCTTY;
	NDINIT(&nd, NAMEI_LOOKUP, CNP_FOLLOW, UIO_SYSSPACE, path, td);

	error = kern_open(&nd, flags, args->mode, &args->sysmsg_result);

	if (error == 0 && !(flags & O_NOCTTY) && 
		SESS_LEADER(p) && !(p->p_flag & P_CONTROLT)) {
		struct filedesc *fdp = p->p_fd;
		struct file *fp = fdp->fd_ofiles[args->sysmsg_result];

		if (fp->f_type == DTYPE_VNODE)
			fo_ioctl(fp, TIOCSCTTY, (caddr_t) 0, td);
    }
#ifdef DEBUG
	if (ldebug(open))
		printf(LMSG("open returns error %d"), error);
#endif
	linux_free_path(&path);
	return error;
}

int
linux_lseek(struct linux_lseek_args *args)
{
	int error;

#ifdef DEBUG
	if (ldebug(lseek))
		printf(ARGS(lseek, "%d, %ld, %d"),
		    args->fdes, (long)args->off, args->whence);
#endif
	error = kern_lseek(args->fdes, args->off, args->whence,
	    &args->sysmsg_offset);

	return error;
}

#ifndef __alpha__
int
linux_llseek(struct linux_llseek_args *args)
{
	int error;
	off_t off, res;

#ifdef DEBUG
	if (ldebug(llseek))
		printf(ARGS(llseek, "%d, %d:%d, %d"),
		    args->fd, args->ohigh, args->olow, args->whence);
#endif
	off = (args->olow) | (((off_t) args->ohigh) << 32);

	error = kern_lseek(args->fd, off, args->whence, &res);

	if (error == 0)
		error = copyout(&res, args->res, sizeof(res));
	return (error);
}
#endif /*!__alpha__*/

#ifndef __alpha__
int
linux_readdir(struct linux_readdir_args *args)
{
	struct linux_getdents_args lda;
	int error;

	lda.fd = args->fd;
	lda.dent = args->dent;
	lda.count = 1;
	lda.sysmsg_result = 0;
	error = linux_getdents(&lda);
	args->sysmsg_result = lda.sysmsg_result;
	return(error);
}
#endif /*!__alpha__*/

/*
 * Note that linux_getdents(2) and linux_getdents64(2) have the same
 * arguments. They only differ in the definition of struct dirent they
 * operate on. We use this to common the code, with the exception of
 * accessing struct dirent. Note that linux_readdir(2) is implemented
 * by means of linux_getdents(2). In this case we never operate on
 * struct dirent64 and thus don't need to handle it...
 */

struct l_dirent {
	l_long		d_ino;
	l_off_t		d_off;
	l_ushort	d_reclen;
	char		d_name[LINUX_NAME_MAX + 1];
};

struct l_dirent64 {
	uint64_t	d_ino;
	int64_t		d_off;
	l_ushort	d_reclen;
	u_char		d_type;
	char		d_name[LINUX_NAME_MAX + 1];
};

#define LINUX_RECLEN(de,namlen) \
    ALIGN((((char *)&(de)->d_name - (char *)de) + (namlen) + 1))

#define	LINUX_DIRBLKSIZ		512

static int
getdents_common(struct linux_getdents64_args *args, int is64bit)
{
	struct thread *td = curthread;
	struct proc *p = td->td_proc;
	struct dirent *bdp;
	struct vnode *vp;
	caddr_t inp, buf;		/* BSD-format */
	int len, reclen;		/* BSD-format */
	caddr_t outp;			/* Linux-format */
	int resid, linuxreclen=0;	/* Linux-format */
	struct file *fp;
	struct uio auio;
	struct iovec aiov;
	struct vattr va;
	off_t off;
	struct l_dirent linux_dirent;
	struct l_dirent64 linux_dirent64;
	int buflen, error, eofflag, nbytes, justone;
	u_long *cookies = NULL, *cookiep;
	int ncookies;

	KKASSERT(p);

	if ((error = getvnode(p->p_fd, args->fd, &fp)) != 0)
		return (error);

	if ((fp->f_flag & FREAD) == 0)
		return (EBADF);

	vp = (struct vnode *) fp->f_data;
	if (vp->v_type != VDIR)
		return (EINVAL);

	if ((error = VOP_GETATTR(vp, &va, td)))
		return (error);

	nbytes = args->count;
	if (nbytes == 1) {
		/* readdir(2) case. Always struct dirent. */
		if (is64bit)
			return (EINVAL);
		nbytes = sizeof(linux_dirent);
		justone = 1;
	} else
		justone = 0;

	off = fp->f_offset;

	buflen = max(LINUX_DIRBLKSIZ, nbytes);
	buflen = min(buflen, MAXBSIZE);
	buf = malloc(buflen, M_TEMP, M_WAITOK);
	vn_lock(vp, LK_EXCLUSIVE | LK_RETRY, td);

again:
	aiov.iov_base = buf;
	aiov.iov_len = buflen;
	auio.uio_iov = &aiov;
	auio.uio_iovcnt = 1;
	auio.uio_rw = UIO_READ;
	auio.uio_segflg = UIO_SYSSPACE;
	auio.uio_td = td;
	auio.uio_resid = buflen;
	auio.uio_offset = off;

	if (cookies) {
		free(cookies, M_TEMP);
		cookies = NULL;
	}

	if ((error = VOP_READDIR(vp, &auio, fp->f_cred, &eofflag, &ncookies,
		 &cookies)))
		goto out;

	inp = buf;
	outp = (caddr_t)args->dirent;
	resid = nbytes;
	if ((len = buflen - auio.uio_resid) <= 0)
		goto eof;

	cookiep = cookies;

	if (cookies) {
		/*
		 * When using cookies, the vfs has the option of reading from
		 * a different offset than that supplied (UFS truncates the
		 * offset to a block boundary to make sure that it never reads
		 * partway through a directory entry, even if the directory
		 * has been compacted).
		 */
		while (len > 0 && ncookies > 0 && *cookiep <= off) {
			bdp = (struct dirent *) inp;
			len -= bdp->d_reclen;
			inp += bdp->d_reclen;
			cookiep++;
			ncookies--;
		}
	}

	while (len > 0) {
		if (cookiep && ncookies == 0)
			break;
		bdp = (struct dirent *) inp;
		reclen = bdp->d_reclen;
		if (reclen & 3) {
			error = EFAULT;
			goto out;
		}

		if (bdp->d_fileno == 0) {
			inp += reclen;
			if (cookiep) {
				off = *cookiep++;
				ncookies--;
			} else
				off += reclen;

			len -= reclen;
			continue;
		}

		linuxreclen = (is64bit)
		    ? LINUX_RECLEN(&linux_dirent64, bdp->d_namlen)
		    : LINUX_RECLEN(&linux_dirent, bdp->d_namlen);

		if (reclen > len || resid < linuxreclen) {
			outp++;
			break;
		}

		if (justone) {
			/* readdir(2) case. */
			linux_dirent.d_ino = (l_long)bdp->d_fileno;
			linux_dirent.d_off = (l_off_t)linuxreclen;
			linux_dirent.d_reclen = (l_ushort)bdp->d_namlen;
			strcpy(linux_dirent.d_name, bdp->d_name);
			error = copyout(&linux_dirent, outp, linuxreclen);
		} else {
			if (is64bit) {
				linux_dirent64.d_ino = bdp->d_fileno;
				linux_dirent64.d_off = (cookiep)
				    ? (l_off_t)*cookiep
				    : (l_off_t)(off + reclen);
				linux_dirent64.d_reclen =
				    (l_ushort)linuxreclen;
				linux_dirent64.d_type = bdp->d_type;
				strcpy(linux_dirent64.d_name, bdp->d_name);
				error = copyout(&linux_dirent64, outp,
				    linuxreclen);
			} else {
				linux_dirent.d_ino = bdp->d_fileno;
				linux_dirent.d_off = (cookiep)
				    ? (l_off_t)*cookiep
				    : (l_off_t)(off + reclen);
				linux_dirent.d_reclen = (l_ushort)linuxreclen;
				strcpy(linux_dirent.d_name, bdp->d_name);
				error = copyout(&linux_dirent, outp,
				    linuxreclen);
			}
		}
		if (error)
			goto out;

		inp += reclen;
		if (cookiep) {
			off = *cookiep++;
			ncookies--;
		} else
			off += reclen;

		outp += linuxreclen;
		resid -= linuxreclen;
		len -= reclen;
		if (justone)
			break;
	}

	if (outp == (caddr_t)args->dirent)
		goto again;

	fp->f_offset = off;
	if (justone)
		nbytes = resid + linuxreclen;

eof:
	args->sysmsg_result = nbytes - resid;

out:
	if (cookies)
		free(cookies, M_TEMP);

	VOP_UNLOCK(vp, 0, td);
	free(buf, M_TEMP);
	return (error);
}

int
linux_getdents(struct linux_getdents_args *args)
{
#ifdef DEBUG
	if (ldebug(getdents))
		printf(ARGS(getdents, "%d, *, %d"), args->fd, args->count);
#endif
	return (getdents_common((struct linux_getdents64_args*)args, 0));
}

int
linux_getdents64(struct linux_getdents64_args *args)
{
#ifdef DEBUG
	if (ldebug(getdents64))
		printf(ARGS(getdents64, "%d, *, %d"), args->fd, args->count);
#endif
	return (getdents_common(args, 1));
}

/*
 * These exist mainly for hooks for doing /compat/linux translation.
 */

int
linux_access(struct linux_access_args *args)
{
	struct thread *td = curthread;
	struct nameidata nd;
	char *path;
	int error;

	error = linux_copyin_path(args->path, &path, LINUX_PATH_EXISTS);
	if (error)
		return (error);
#ifdef DEBUG
	if (ldebug(access))
		printf(ARGS(access, "%s, %d"), path, args->flags);
#endif
	NDINIT(&nd, NAMEI_LOOKUP, CNP_FOLLOW | CNP_LOCKLEAF | CNP_NOOBJ,
	    UIO_SYSSPACE, path, td);

	error = kern_access(&nd, args->flags);

	linux_free_path(&path);
	return(error);
}

int
linux_unlink(struct linux_unlink_args *args)
{
	struct thread *td = curthread;
	struct nameidata nd;
	char *path;
	int error;

	error = linux_copyin_path(args->path, &path, LINUX_PATH_EXISTS);
	if (error)
		return (error);
#ifdef DEBUG
	if (ldebug(unlink))
		printf(ARGS(unlink, "%s"), path);
#endif
	NDINIT(&nd, NAMEI_DELETE, CNP_LOCKPARENT, UIO_SYSSPACE, path, td);

	error = kern_unlink(&nd);

	linux_free_path(&path);
	return(error);
}

int
linux_chdir(struct linux_chdir_args *args)
{
	struct nlookupdata nd;
	char *path;
	int error;

	error = linux_copyin_path(args->path, &path, LINUX_PATH_EXISTS);
	if (error)
		return (error);
#ifdef DEBUG
	if (ldebug(chdir))
		printf(ARGS(chdir, "%s"), path);
#endif
	error = nlookup_init(&nd, path, UIO_SYSSPACE, NLC_FOLLOW);
	if (error == 0) {
		error = kern_chdir(&nd);
		nlookup_done(&nd);
	}
	linux_free_path(&path);
	return(error);
}

int
linux_chmod(struct linux_chmod_args *args)
{
	struct thread *td = curthread;
	struct nameidata nd;
	char *path;
	int error;

	error = linux_copyin_path(args->path, &path, LINUX_PATH_EXISTS);
	if (error)
		return (error);
#ifdef DEBUG
	if (ldebug(chmod))
		printf(ARGS(chmod, "%s, %d"), path, args->mode);
#endif
	NDINIT(&nd, NAMEI_LOOKUP, CNP_FOLLOW, UIO_SYSSPACE, path, td);

	error = kern_chmod(&nd, args->mode);

	linux_free_path(&path);
	return(error);
}

int
linux_mkdir(struct linux_mkdir_args *args)
{
	struct thread *td = curthread;
	struct nameidata nd;
	char *path;
	int error;

	error = linux_copyin_path(args->path, &path, LINUX_PATH_CREATE);
	if (error)
		return (error);
#ifdef DEBUG
	if (ldebug(mkdir))
		printf(ARGS(mkdir, "%s, %d"), path, args->mode);
#endif
	NDINIT(&nd, NAMEI_CREATE, CNP_LOCKPARENT, UIO_SYSSPACE, path, td);

	error = kern_mkdir(&nd, args->mode);

	linux_free_path(&path);
	return(error);
}

int
linux_rmdir(struct linux_rmdir_args *args)
{
	struct thread *td = curthread;
	struct nameidata nd;
	char *path;
	int error;

	error = linux_copyin_path(args->path, &path, LINUX_PATH_EXISTS);
	if (error)
		return (error);
#ifdef DEBUG
	if (ldebug(rmdir))
		printf(ARGS(rmdir, "%s"), path);
#endif
	NDINIT(&nd, NAMEI_DELETE, CNP_LOCKPARENT | CNP_LOCKLEAF,
	    UIO_SYSSPACE, path, td);

	error = kern_rmdir(&nd);

	linux_free_path(&path);
	return(error);
}

int
linux_rename(struct linux_rename_args *args)
{
	struct thread *td = curthread;
	struct nameidata fromnd, tond;
	char *from, *to;
	int error;

	error = linux_copyin_path(args->from, &from, LINUX_PATH_EXISTS);
	if (error)
		return (error);
	error = linux_copyin_path(args->to, &to, LINUX_PATH_CREATE);
	if (error) {
		linux_free_path(&from);
		return (error);
	}
#ifdef DEBUG
	if (ldebug(rename))
		printf(ARGS(rename, "%s, %s"), from, to);
#endif
	NDINIT(&fromnd, NAMEI_DELETE, CNP_WANTPARENT | CNP_SAVESTART,
	    UIO_SYSSPACE, from, td);
	NDINIT(&tond, NAMEI_RENAME,
	    CNP_LOCKPARENT | CNP_LOCKLEAF | CNP_NOCACHE |
	    CNP_SAVESTART | CNP_NOOBJ,
	    UIO_SYSSPACE, to, td);

	error = kern_rename(&fromnd, &tond);

	linux_free_path(&from);
	linux_free_path(&to);
	return(error);
}

int
linux_symlink(struct linux_symlink_args *args)
{
	struct thread *td = curthread;
	struct nameidata nd;
	char *path, *link;
	int error;

	error = linux_copyin_path(args->path, &path, LINUX_PATH_EXISTS);
	if (error)
		return (error);
	error = linux_copyin_path(args->to, &link, LINUX_PATH_CREATE);
	if (error) {
		linux_free_path(&path);
		return (error);
	}
#ifdef DEBUG
	if (ldebug(symlink))
		printf(ARGS(symlink, "%s, %s"), path, link);
#endif
	NDINIT(&nd, NAMEI_CREATE, CNP_LOCKPARENT | CNP_NOOBJ, UIO_SYSSPACE,
	    link, td);

	error = kern_symlink(path, &nd);

	linux_free_path(&path);
	linux_free_path(&link);
	return(error);
}

int
linux_readlink(struct linux_readlink_args *args)
{
	struct thread *td = curthread;
	struct nameidata nd;
	char *path;
	int error;

	error = linux_copyin_path(args->name, &path, LINUX_PATH_EXISTS);
	if (error)
		return (error);
#ifdef DEBUG
	if (ldebug(readlink))
		printf(ARGS(readlink, "%s, %p, %d"), path, (void *)args->buf,
		    args->count);
#endif
	NDINIT(&nd, NAMEI_LOOKUP, CNP_LOCKLEAF | CNP_NOOBJ, UIO_SYSSPACE,
	   path, td);

	error = kern_readlink(&nd, args->buf, args->count,
	    &args->sysmsg_result);

	linux_free_path(&path);
	return(error);
}

int
linux_truncate(struct linux_truncate_args *args)
{
	struct thread *td = curthread;
	struct nameidata nd;
	char *path;
	int error;

	error = linux_copyin_path(args->path, &path, LINUX_PATH_EXISTS);
	if (error)
		return (error);
#ifdef DEBUG
	if (ldebug(truncate))
		printf(ARGS(truncate, "%s, %ld"), path,
		    (long)args->length);
#endif
	NDINIT(&nd, NAMEI_LOOKUP, CNP_FOLLOW, UIO_SYSSPACE, path, td);

	error = kern_truncate(&nd, args->length);

	linux_free_path(&path);
	return(error);
}

int
linux_truncate64(struct linux_truncate64_args *args)
{
	struct thread *td = curthread;
	struct nameidata nd;
	char *path;
	int error;

	error = linux_copyin_path(args->path, &path, LINUX_PATH_EXISTS);
	if (error)
		return (error);
#ifdef DEBUG
	if (ldebug(truncate64))
		printf(ARGS(truncate64, "%s, %lld"), path,
		    (off_t)args->length);
#endif
	NDINIT(&nd, NAMEI_LOOKUP, CNP_FOLLOW, UIO_SYSSPACE, path, td);

	error = kern_truncate(&nd, args->length);

	linux_free_path(&path);
	return error;
}

int
linux_ftruncate(struct linux_ftruncate_args *args)
{
	int error;

#ifdef DEBUG
	if (ldebug(ftruncate))
		printf(ARGS(ftruncate, "%d, %ld"), args->fd,
		    (long)args->length);
#endif
	error = kern_ftruncate(args->fd, args->length);

	return error;
}

int
linux_ftruncate64(struct linux_ftruncate64_args *args)
{
	int error;

#ifdef DEBUG
	if (ldebug(ftruncate))
		printf(ARGS(ftruncate64, "%d, %lld"), args->fd,
		    (off_t)args->length);
#endif
	error = kern_ftruncate(args->fd, args->length);

	return error;
}

int
linux_link(struct linux_link_args *args)
{
	struct thread *td = curthread;
	struct nameidata nd, linknd;
	char *path, *link;
	int error;

	error = linux_copyin_path(args->path, &path, LINUX_PATH_EXISTS);
	if (error)
		return (error);
	error = linux_copyin_path(args->to, &link, LINUX_PATH_CREATE);
	if (error) {
		linux_free_path(&path);
		return (error);
	}
#ifdef DEBUG
	if (ldebug(link))
		printf(ARGS(link, "%s, %s"), path, link);
#endif
	NDINIT(&nd, NAMEI_LOOKUP, CNP_FOLLOW | CNP_NOOBJ, UIO_SYSSPACE,
	    path, td);
	NDINIT(&linknd, NAMEI_CREATE, CNP_LOCKPARENT | CNP_NOOBJ,
	    UIO_SYSSPACE, link, td);

	error = kern_link(&nd, &linknd);

	linux_free_path(&path);
	linux_free_path(&link);
	return(error);
}

#ifndef __alpha__
int
linux_fdatasync(struct linux_fdatasync_args *uap)
{
	struct fsync_args bsd;
	int error;

	bsd.fd = uap->fd;
	bsd.sysmsg_result = 0;

	error = fsync(&bsd);
	uap->sysmsg_result = bsd.sysmsg_result;
	return(error);
}
#endif /*!__alpha__*/

int
linux_pread(struct linux_pread_args *uap)
{
	struct thread *td = curthread;
	struct uio auio;
	struct iovec aiov;
	int error;

	aiov.iov_base = uap->buf;
	aiov.iov_len = uap->nbyte;
	auio.uio_iov = &aiov;
	auio.uio_iovcnt = 1;
	auio.uio_offset = uap->offset;
	auio.uio_resid = uap->nbyte;
	auio.uio_rw = UIO_READ;
	auio.uio_segflg = UIO_USERSPACE;
	auio.uio_td = td;

	error = kern_readv(uap->fd, &auio, FOF_OFFSET, &uap->sysmsg_result);

	return(error);
}

int
linux_pwrite(struct linux_pwrite_args *uap)
{
	struct thread *td = curthread;
	struct uio auio;
	struct iovec aiov;
	int error;

        aiov.iov_base = uap->buf;
        aiov.iov_len = uap->nbyte;
        auio.uio_iov = &aiov;
        auio.uio_iovcnt = 1;
        auio.uio_offset = uap->offset;
        auio.uio_resid = uap->nbyte;
        auio.uio_rw = UIO_WRITE;
        auio.uio_segflg = UIO_USERSPACE;
        auio.uio_td = td;

	error = kern_writev(uap->fd, &auio, FOF_OFFSET, &uap->sysmsg_result);

	return(error);
}

int
linux_oldumount(struct linux_oldumount_args *args)
{
	struct linux_umount_args args2;
	int error;

	args2.path = args->path;
	args2.flags = 0;
	args2.sysmsg_result = 0;
	error = linux_umount(&args2);
	args->sysmsg_result = args2.sysmsg_result;
	return(error);
}

int
linux_umount(struct linux_umount_args *args)
{
	struct unmount_args bsd;
	int error;

	bsd.path = args->path;
	bsd.flags = args->flags;	/* XXX correct? */
	bsd.sysmsg_result = 0;

	error = unmount(&bsd);
	args->sysmsg_result = bsd.sysmsg_result;
	return(error);
}

/*
 * fcntl family of syscalls
 */

struct l_flock {
	l_short		l_type;
	l_short		l_whence;
	l_off_t		l_start;
	l_off_t		l_len;
	l_pid_t		l_pid;
};

static void
linux_to_bsd_flock(struct l_flock *linux_flock, struct flock *bsd_flock)
{
	switch (linux_flock->l_type) {
	case LINUX_F_RDLCK:
		bsd_flock->l_type = F_RDLCK;
		break;
	case LINUX_F_WRLCK:
		bsd_flock->l_type = F_WRLCK;
		break;
	case LINUX_F_UNLCK:
		bsd_flock->l_type = F_UNLCK;
		break;
	default:
		bsd_flock->l_type = -1;
		break;
	}
	bsd_flock->l_whence = linux_flock->l_whence;
	bsd_flock->l_start = (off_t)linux_flock->l_start;
	bsd_flock->l_len = (off_t)linux_flock->l_len;
	bsd_flock->l_pid = (pid_t)linux_flock->l_pid;
}

static void
bsd_to_linux_flock(struct flock *bsd_flock, struct l_flock *linux_flock)
{
	switch (bsd_flock->l_type) {
	case F_RDLCK:
		linux_flock->l_type = LINUX_F_RDLCK;
		break;
	case F_WRLCK:
		linux_flock->l_type = LINUX_F_WRLCK;
		break;
	case F_UNLCK:
		linux_flock->l_type = LINUX_F_UNLCK;
		break;
	}
	linux_flock->l_whence = bsd_flock->l_whence;
	linux_flock->l_start = (l_off_t)bsd_flock->l_start;
	linux_flock->l_len = (l_off_t)bsd_flock->l_len;
	linux_flock->l_pid = (l_pid_t)bsd_flock->l_pid;
}

#if defined(__i386__)
struct l_flock64 {
	l_short		l_type;
	l_short		l_whence;
	l_loff_t	l_start;
	l_loff_t	l_len;
	l_pid_t		l_pid;
};

static void
linux_to_bsd_flock64(struct l_flock64 *linux_flock, struct flock *bsd_flock)
{
	switch (linux_flock->l_type) {
	case LINUX_F_RDLCK:
		bsd_flock->l_type = F_RDLCK;
		break;
	case LINUX_F_WRLCK:
		bsd_flock->l_type = F_WRLCK;
		break;
	case LINUX_F_UNLCK:
		bsd_flock->l_type = F_UNLCK;
		break;
	default:
		bsd_flock->l_type = -1;
		break;
	}
	bsd_flock->l_whence = linux_flock->l_whence;
	bsd_flock->l_start = (off_t)linux_flock->l_start;
	bsd_flock->l_len = (off_t)linux_flock->l_len;
	bsd_flock->l_pid = (pid_t)linux_flock->l_pid;
}

static void
bsd_to_linux_flock64(struct flock *bsd_flock, struct l_flock64 *linux_flock)
{
	switch (bsd_flock->l_type) {
	case F_RDLCK:
		linux_flock->l_type = LINUX_F_RDLCK;
		break;
	case F_WRLCK:
		linux_flock->l_type = LINUX_F_WRLCK;
		break;
	case F_UNLCK:
		linux_flock->l_type = LINUX_F_UNLCK;
		break;
	}
	linux_flock->l_whence = bsd_flock->l_whence;
	linux_flock->l_start = (l_loff_t)bsd_flock->l_start;
	linux_flock->l_len = (l_loff_t)bsd_flock->l_len;
	linux_flock->l_pid = (l_pid_t)bsd_flock->l_pid;
}
#endif /* __i386__ */

#if defined(__alpha__)
#define	linux_fcntl64_args	linux_fcntl_args
#endif

static int
linux_fcntl_common(struct linux_fcntl64_args *args)
{
	struct proc *p = curproc;
	struct l_flock linux_flock;
	struct filedesc *fdp;
	struct file *fp;
	union fcntl_dat dat;
	int error, cmd;

	switch (args->cmd) {
	case LINUX_F_DUPFD:
		cmd = F_DUPFD;
		dat.fc_fd = args->arg;
		break;
	case LINUX_F_GETFD:
		cmd = F_GETFD;
		break;
	case LINUX_F_SETFD:
		cmd = F_SETFD;
		dat.fc_cloexec = args->arg;
		break;
	case LINUX_F_GETFL:
		cmd = F_GETFL;
		break;
	case LINUX_F_SETFL:
		cmd = F_SETFL;
		dat.fc_flags = 0;
		if (args->arg & LINUX_O_NDELAY)
			dat.fc_flags |= O_NONBLOCK;
		if (args->arg & LINUX_O_APPEND)
			dat.fc_flags |= O_APPEND;
		if (args->arg & LINUX_O_SYNC)
			dat.fc_flags |= O_FSYNC;
		if (args->arg & LINUX_FASYNC)
			dat.fc_flags |= O_ASYNC;
		break;
	case LINUX_F_GETLK:
	case LINUX_F_SETLK:
	case LINUX_F_SETLKW:
		cmd = F_GETLK;
		error = copyin((caddr_t)args->arg, &linux_flock,
		    sizeof(linux_flock));
		if (error)
			return (error);
		linux_to_bsd_flock(&linux_flock, &dat.fc_flock);
		break;
	case LINUX_F_GETOWN:
		cmd = F_GETOWN;
		break;
	case LINUX_F_SETOWN:
		/*
		 * XXX some Linux applications depend on F_SETOWN having no
		 * significant effect for pipes (SIGIO is not delivered for
		 * pipes under Linux-2.2.35 at least).
		 */
		fdp = p->p_fd;
		if ((u_int)args->fd >= fdp->fd_nfiles ||
		    (fp = fdp->fd_ofiles[args->fd]) == NULL)
			return (EBADF);
		if (fp->f_type == DTYPE_PIPE)
			return (EINVAL);
		cmd = F_SETOWN;
		dat.fc_owner = args->arg;
		break;
	default:
		return (EINVAL);
	}

	error = kern_fcntl(args->fd, cmd, &dat);

	if (error == 0) {
		switch (args->cmd) {
		case LINUX_F_DUPFD:
			args->sysmsg_result = dat.fc_fd;
			break;
		case LINUX_F_GETFD:
			args->sysmsg_result = dat.fc_cloexec;
			break;
		case LINUX_F_SETFD:
			break;
		case LINUX_F_GETFL:
			args->sysmsg_result = 0;
			if (dat.fc_flags & O_RDONLY)
				args->sysmsg_result |= LINUX_O_RDONLY;
			if (dat.fc_flags & O_WRONLY)
				args->sysmsg_result |= LINUX_O_WRONLY;
			if (dat.fc_flags & O_RDWR)
				args->sysmsg_result |= LINUX_O_RDWR;
			if (dat.fc_flags & O_NDELAY)
				args->sysmsg_result |= LINUX_O_NONBLOCK;
			if (dat.fc_flags & O_APPEND)
				args->sysmsg_result |= LINUX_O_APPEND;
			if (dat.fc_flags & O_FSYNC)
				args->sysmsg_result |= LINUX_O_SYNC;
			if (dat.fc_flags & O_ASYNC)
				args->sysmsg_result |= LINUX_FASYNC;
			break;
		case LINUX_F_GETLK:
			bsd_to_linux_flock(&dat.fc_flock, &linux_flock);
			error = copyout(&linux_flock, (caddr_t)args->arg,
			    sizeof(linux_flock));
			break;
		case LINUX_F_SETLK:
		case LINUX_F_SETLKW:
			break;
		case LINUX_F_GETOWN:
			args->sysmsg_result = dat.fc_owner;
			break;
		case LINUX_F_SETOWN:
			break;
		}
	}

	return(error);
}

int
linux_fcntl(struct linux_fcntl_args *args)
{
	struct linux_fcntl64_args args64;
	int error;

#ifdef DEBUG
	if (ldebug(fcntl))
		printf(ARGS(fcntl, "%d, %08x, *"), args->fd, args->cmd);
#endif

	args64.fd = args->fd;
	args64.cmd = args->cmd;
	args64.arg = args->arg;
	args64.sysmsg_result = 0;
	error = linux_fcntl_common(&args64);
	args->sysmsg_result = args64.sysmsg_result;
	return(error);
}

#if defined(__i386__)
int
linux_fcntl64(struct linux_fcntl64_args *args)
{
	struct l_flock64 linux_flock;
	union fcntl_dat dat;
	int error, cmd = 0;

#ifdef DEBUG
	if (ldebug(fcntl64))
		printf(ARGS(fcntl64, "%d, %08x, *"), args->fd, args->cmd);
#endif
	if (args->cmd == LINUX_F_GETLK64 || args->cmd == LINUX_F_SETLK64 ||
	    args->cmd == LINUX_F_SETLKW64) {
		switch (args->cmd) {
		case LINUX_F_GETLK64:
			cmd = F_GETLK;
			break;
		case LINUX_F_SETLK64:
			cmd = F_SETLK;
			break;
		case LINUX_F_SETLKW64:
			cmd = F_SETLKW;
			break;
		}

		error = copyin((caddr_t)args->arg, &linux_flock,
		    sizeof(linux_flock));
		if (error)
			return (error);
		linux_to_bsd_flock64(&linux_flock, &dat.fc_flock);

		error = kern_fcntl(args->fd, cmd, &dat);

		if (error == 0 && args->cmd == LINUX_F_GETLK64) {
			bsd_to_linux_flock64(&dat.fc_flock, &linux_flock);
			error = copyout(&linux_flock, (caddr_t)args->arg,
			    sizeof(linux_flock));
		}
	} else {
		error = linux_fcntl_common(args);
	}

	return (error);
}
#endif /* __i386__ */

int
linux_chown(struct linux_chown_args *args)
{
	struct thread *td = curthread;
	struct nameidata nd;
	char *path;
	int error;

	error = linux_copyin_path(args->path, &path, LINUX_PATH_EXISTS);
	if (error)
		return (error);
#ifdef DEBUG
	if (ldebug(chown))
		printf(ARGS(chown, "%s, %d, %d"), path, args->uid, args->gid);
#endif
	NDINIT(&nd, NAMEI_LOOKUP, CNP_FOLLOW, UIO_SYSSPACE, path, td);

	error = kern_chown(&nd, args->uid, args->gid);

	linux_free_path(&path);
	return(error);
}

int
linux_lchown(struct linux_lchown_args *args)
{
	struct thread *td = curthread;
	struct nameidata nd;
	char *path;
	int error;

	error = linux_copyin_path(args->path, &path, LINUX_PATH_EXISTS);
	if (error)
		return (error);
#ifdef DEBUG
	if (ldebug(lchown))
		printf(ARGS(lchown, "%s, %d, %d"), path, args->uid, args->gid);
#endif
	NDINIT(&nd, NAMEI_LOOKUP, 0, UIO_SYSSPACE, path, td);

	error = kern_chown(&nd, args->uid, args->gid);

	linux_free_path(&path);
	return(error);
}

