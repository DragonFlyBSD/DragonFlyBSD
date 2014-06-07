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
 *    derived from this software without specific prior written permission
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
 */

#include "opt_compat.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/conf.h>
#include <sys/dirent.h>
#include <sys/fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/filedesc.h>
#include <sys/kern_syscall.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mount.h>
#include <sys/nlookup.h>
#include <sys/proc.h>
#include <sys/sysproto.h>
#include <sys/tty.h>
#include <sys/vnode.h>

#include <vfs/ufs/quota.h>
#include <vfs/ufs/ufsmount.h>

#include <sys/file2.h>
#include <sys/mplock2.h>

#include <arch_linux/linux.h>
#include <arch_linux/linux_proto.h>
#include "linux_util.h"

/*
 * MPALMOSTSAFE
 */
int
sys_linux_creat(struct linux_creat_args *args)
{
	struct nlookupdata nd;
	char *path;
	int error;

	error = linux_copyin_path(args->path, &path, LINUX_PATH_CREATE);
	if (error)
		return (error);
#ifdef DEBUG
	if (ldebug(creat))
		kprintf(ARGS(creat, "%s, %d"), path, args->mode);
#endif
	error = nlookup_init(&nd, path, UIO_SYSSPACE, NLC_FOLLOW);
	if (error == 0) {
		error = kern_open(&nd, O_WRONLY | O_CREAT | O_TRUNC,
				  args->mode, &args->sysmsg_iresult);
	}
	linux_free_path(&path);
	return(error);
}

/*
 * MPALMOSTSAFE
 */
static int
linux_open_common(int dfd, char *lpath, int lflags, int mode, int *iresult)
{
	struct thread *td = curthread;
	struct proc *p = td->td_proc;
	struct nlookupdata nd;
	struct file *fp;
	char *path;
	int error, flags;

	if (lflags & LINUX_O_CREAT) {
		error = linux_copyin_path(lpath, &path,
		    LINUX_PATH_CREATE);
	} else {
		error = linux_copyin_path(lpath, &path,
		    LINUX_PATH_EXISTS);
	}
	if (error)
		return (error);

	flags = 0;
	if (lflags & LINUX_O_RDONLY)
		flags |= O_RDONLY;
	if (lflags & LINUX_O_WRONLY)
		flags |= O_WRONLY;
	if (lflags & LINUX_O_RDWR)
		flags |= O_RDWR;
	if (lflags & LINUX_O_NDELAY)
		flags |= O_NONBLOCK;
	if (lflags & LINUX_O_APPEND)
		flags |= O_APPEND;
	if (lflags & LINUX_O_SYNC)
		flags |= O_FSYNC;
	if (lflags & LINUX_O_NONBLOCK)
		flags |= O_NONBLOCK;
	if (lflags & LINUX_FASYNC)
		flags |= O_ASYNC;
	if (lflags & LINUX_O_CREAT)
		flags |= O_CREAT;
	if (lflags & LINUX_O_TRUNC)
		flags |= O_TRUNC;
	if (lflags & LINUX_O_EXCL)
		flags |= O_EXCL;
	if (lflags & LINUX_O_NOCTTY)
		flags |= O_NOCTTY;

	error = nlookup_init_at(&nd, &fp, dfd, path, UIO_SYSSPACE, NLC_FOLLOW);
	if (error == 0) {
		error = kern_open(&nd, flags, mode, iresult);
	}
	nlookup_done_at(&nd, fp);

	if (error == 0 && !(flags & O_NOCTTY) && 
		SESS_LEADER(p) && !(p->p_flags & P_CONTROLT)) {
		struct file *fp;

		fp = holdfp(p->p_fd, *iresult, -1);
		if (fp) {
			if (fp->f_type == DTYPE_VNODE) {
				fo_ioctl(fp, TIOCSCTTY, NULL,
					 td->td_ucred, NULL);
			}
			fdrop(fp);
		}
	}

	if (error == 0 && lflags & LINUX_O_DIRECTORY) {
		struct file *fp;
		struct vnode *vp;

		fp = holdfp(p->p_fd, *iresult, -1);
		if (fp) {
			vp = (struct vnode *) fp->f_data;
			if (vp->v_type != VDIR)
				error = ENOTDIR;
			fdrop(fp);

			if (error)
				kern_close(*iresult);
		}
	}

	linux_free_path(&path);
	return error;
}

int
sys_linux_open(struct linux_open_args *args)
{
	int error;

#ifdef DEBUG
	if (ldebug(open))
		kprintf(ARGS(open, "%s, 0x%x, 0x%x"), args->path, args->flags,
		    args->mode);
#endif

	error = linux_open_common(AT_FDCWD, args->path, args->flags,
	    args->mode, &args->sysmsg_iresult);

#ifdef DEBUG
	if (ldebug(open))
		kprintf(LMSG("open returns error %d"), error);
#endif
	return error;
}

int
sys_linux_openat(struct linux_openat_args *args)
{
	int error;
	int dfd;

#ifdef DEBUG
	if (ldebug(openat))
		kprintf(ARGS(openat, "%s, 0x%x, 0x%x"), args->path,
		    args->flags, args->mode);
#endif

	dfd = (args->dfd == LINUX_AT_FDCWD) ? AT_FDCWD : args->dfd;

	error = linux_open_common(dfd, args->path, args->flags,
	    args->mode, &args->sysmsg_iresult);

#ifdef DEBUG
	if (ldebug(openat))
		kprintf(LMSG("openat returns error %d"), error);
#endif
	return error;
}

/*
 * MPSAFE
 */
int
sys_linux_lseek(struct linux_lseek_args *args)
{
	int error;

#ifdef DEBUG
	if (ldebug(lseek))
		kprintf(ARGS(lseek, "%d, %ld, %d"),
		    args->fdes, (long)args->off, args->whence);
#endif
	error = kern_lseek(args->fdes, args->off, args->whence,
			   &args->sysmsg_offset);

	return error;
}

/*
 * MPSAFE
 */
int
sys_linux_llseek(struct linux_llseek_args *args)
{
	int error;
	off_t off, res;

#ifdef DEBUG
	if (ldebug(llseek))
		kprintf(ARGS(llseek, "%d, %d:%d, %d"),
		    args->fd, args->ohigh, args->olow, args->whence);
#endif
	off = (args->olow) | (((off_t) args->ohigh) << 32);

	error = kern_lseek(args->fd, off, args->whence, &res);

	if (error == 0)
		error = copyout(&res, args->res, sizeof(res));
	return (error);
}

/*
 * MPSAFE
 */
int
sys_linux_readdir(struct linux_readdir_args *args)
{
	struct linux_getdents_args lda;
	int error;

	lda.fd = args->fd;
	lda.dent = args->dent;
	lda.count = -1;
	lda.sysmsg_iresult = 0;
	error = sys_linux_getdents(&lda);
	args->sysmsg_iresult = lda.sysmsg_iresult;
	return(error);
}

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

/*
 * MPALMOSTSAFE
 */
static int
getdents_common(struct linux_getdents64_args *args, int is64bit)
{
	struct thread *td = curthread;
	struct proc *p = td->td_proc;
	struct dirent *bdp;
	struct vnode *vp;
	caddr_t inp, buf;		/* BSD-format */
	int reclen;			/* BSD-format */
	size_t len;
	caddr_t outp;			/* Linux-format */
	int linuxreclen = 0;		/* Linux-format */
	size_t resid;
	struct file *fp;
	struct uio auio;
	struct iovec aiov;
	struct vattr va;
	off_t off;
	struct l_dirent linux_dirent;
	struct l_dirent64 linux_dirent64;
	int error, eofflag, justone;
	size_t buflen, nbytes;
	off_t *cookies = NULL, *cookiep;
	int ncookies;

	if ((error = holdvnode(p->p_fd, args->fd, &fp)) != 0)
		return (error);

	get_mplock();
	if ((fp->f_flag & FREAD) == 0) {
		error = EBADF;
		goto done;
	}

	vp = (struct vnode *) fp->f_data;
	if (vp->v_type != VDIR) {
		error = EINVAL;
		goto done;
	}

	if ((error = VOP_GETATTR(vp, &va)) != 0)
		goto done;

	nbytes = args->count;
	if (nbytes == (size_t)-1) {
		/* readdir(2) case. Always struct dirent. */
		if (is64bit) {
			error = EINVAL;
			goto done;
		}
		nbytes = sizeof(linux_dirent);
		justone = 1;
	} else {
		justone = 0;
	}
	if ((ssize_t)nbytes < 0)
		nbytes = 0;

	off = fp->f_offset;

	buflen = max(LINUX_DIRBLKSIZ, nbytes);
	buflen = min(buflen, MAXBSIZE);
	buf = kmalloc(buflen, M_TEMP, M_WAITOK);

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
		kfree(cookies, M_TEMP);
		cookies = NULL;
	}

	eofflag = 0;
	ncookies = 0;
	if ((error = VOP_READDIR(vp, &auio, fp->f_cred, &eofflag, &ncookies,
		 &cookies)))
		goto out;

	inp = buf;
	outp = (caddr_t)args->dirent;
	resid = nbytes;
	if (auio.uio_resid >= buflen)
		goto eof;
	len = buflen - auio.uio_resid;
	cookiep = cookies;

	if (cookies) {
		/*
		 * When using cookies, the vfs has the option of reading from
		 * a different offset than that supplied (UFS truncates the
		 * offset to a block boundary to make sure that it never reads
		 * partway through a directory entry, even if the directory
		 * has been compacted).
		 */
		while (len > 0 && ncookies > 0 && *cookiep < off) {
			bdp = (struct dirent *) inp;
			len -= _DIRENT_DIRSIZ(bdp);
			inp += _DIRENT_DIRSIZ(bdp);
			cookiep++;
			ncookies--;
		}
	}

	while (len > 0) {
		if (cookiep && ncookies == 0)
			break;
		bdp = (struct dirent *) inp;
		reclen = _DIRENT_DIRSIZ(bdp);
		if (reclen & 3) {
			error = EFAULT;
			goto out;
		}

		if (bdp->d_ino == 0) {
			inp += reclen;
			if (cookiep) {
				off = *cookiep++;
				++off;
				ncookies--;
			} else {
				off += reclen;
			}
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

		bzero(&linux_dirent, sizeof(linux_dirent));
		bzero(&linux_dirent64, sizeof(linux_dirent64));
		if (justone) {
			/* readdir(2) case. */
			linux_dirent.d_ino = (l_long)INO64TO32(bdp->d_ino);
			linux_dirent.d_off = (l_off_t)linuxreclen;
			linux_dirent.d_reclen = (l_ushort)bdp->d_namlen;
			strcpy(linux_dirent.d_name, bdp->d_name);
			error = copyout(&linux_dirent, outp, linuxreclen);
		} else {
			if (is64bit) {
				linux_dirent64.d_ino = INO64TO32(bdp->d_ino);
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
				linux_dirent.d_ino = INO64TO32(bdp->d_ino);
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
			++off;
			ncookies--;
		} else {
			off += reclen;
		}

		outp += linuxreclen;
		resid -= linuxreclen;
		len -= reclen;
		if (justone)
			break;
	}

	if (outp == (caddr_t)args->dirent && eofflag == 0)
		goto again;

	fp->f_offset = off;
	if (justone)
		nbytes = resid + linuxreclen;

eof:
	args->sysmsg_iresult = (int)(nbytes - resid);

out:
	if (cookies)
		kfree(cookies, M_TEMP);

	kfree(buf, M_TEMP);
done:
	rel_mplock();
	fdrop(fp);
	return (error);
}

/*
 * MPSAFE
 */
int
sys_linux_getdents(struct linux_getdents_args *args)
{
#ifdef DEBUG
	if (ldebug(getdents))
		kprintf(ARGS(getdents, "%d, *, %d"), args->fd, args->count);
#endif
	return (getdents_common((struct linux_getdents64_args*)args, 0));
}

/*
 * MPSAFE
 */
int
sys_linux_getdents64(struct linux_getdents64_args *args)
{
#ifdef DEBUG
	if (ldebug(getdents64))
		kprintf(ARGS(getdents64, "%d, *, %d"), args->fd, args->count);
#endif
	return (getdents_common(args, 1));
}

/*
 * These exist mainly for hooks for doing /compat/linux translation.
 *
 * MPALMOSTSAFE
 */
int
sys_linux_access(struct linux_access_args *args)
{
	struct nlookupdata nd;
	char *path;
	int error;

	error = linux_copyin_path(args->path, &path, LINUX_PATH_EXISTS);
	if (error)
		return (error);
#ifdef DEBUG
	if (ldebug(access))
		kprintf(ARGS(access, "%s, %d"), path, args->flags);
#endif
	get_mplock();
	error = nlookup_init(&nd, path, UIO_SYSSPACE, NLC_FOLLOW);
	if (error == 0)
		error = kern_access(&nd, args->flags, 0);
	nlookup_done(&nd);
	rel_mplock();
	linux_free_path(&path);
	return(error);
}

/*
 * MPALMOSTSAFE
 */
int
sys_linux_unlink(struct linux_unlink_args *args)
{
	struct nlookupdata nd;
	char *path;
	int error;

	error = linux_copyin_path(args->path, &path, LINUX_PATH_EXISTS);
	if (error)
		return (error);
#ifdef DEBUG
	if (ldebug(unlink))
		kprintf(ARGS(unlink, "%s"), path);
#endif
	get_mplock();
	error = nlookup_init(&nd, path, UIO_SYSSPACE, 0);
	if (error == 0)
		error = kern_unlink(&nd);
	nlookup_done(&nd);
	rel_mplock();
	linux_free_path(&path);
	return(error);
}

int
sys_linux_unlinkat(struct linux_unlinkat_args *args)
{
	struct nlookupdata nd;
	struct file *fp;
	char *path;
	int dfd, error;

	if (args->flag & ~LINUX_AT_REMOVEDIR)
		return (EINVAL);

	error = linux_copyin_path(args->path, &path, LINUX_PATH_EXISTS);
	if (error) {
		kprintf("linux_copyin_path says error = %d\n", error);
		return (error);
	}
#ifdef DEBUG
	if (ldebug(unlink))
		kprintf(ARGS(unlink, "%s"), path);
#endif

	dfd = (args->dfd == LINUX_AT_FDCWD) ? AT_FDCWD : args->dfd;
	get_mplock();
	error = nlookup_init_at(&nd, &fp, dfd, path, UIO_SYSSPACE, 0);
	if (error == 0) {
		if (args->flag & LINUX_AT_REMOVEDIR)
			error = kern_rmdir(&nd);
		else
			error = kern_unlink(&nd);
	}
	nlookup_done_at(&nd, fp);
	rel_mplock();
	linux_free_path(&path);
	return(error);
}

/*
 * MPALMOSTSAFE
 */
int
sys_linux_chdir(struct linux_chdir_args *args)
{
	struct nlookupdata nd;
	char *path;
	int error;

	error = linux_copyin_path(args->path, &path, LINUX_PATH_EXISTS);
	if (error)
		return (error);
#ifdef DEBUG
	if (ldebug(chdir))
		kprintf(ARGS(chdir, "%s"), path);
#endif
	get_mplock();
	error = nlookup_init(&nd, path, UIO_SYSSPACE, NLC_FOLLOW);
	if (error == 0) {
		error = kern_chdir(&nd);
		nlookup_done(&nd);
	}
	rel_mplock();
	linux_free_path(&path);
	return(error);
}

/*
 * MPALMOSTSAFE
 */
int
sys_linux_chmod(struct linux_chmod_args *args)
{
	struct nlookupdata nd;
	char *path;
	int error;

	error = linux_copyin_path(args->path, &path, LINUX_PATH_EXISTS);
	if (error)
		return (error);
#ifdef DEBUG
	if (ldebug(chmod))
		kprintf(ARGS(chmod, "%s, %d"), path, args->mode);
#endif
	get_mplock();
	error = nlookup_init(&nd, path, UIO_SYSSPACE, NLC_FOLLOW);
	if (error == 0)
		error = kern_chmod(&nd, args->mode);
	nlookup_done(&nd);
	rel_mplock();
	linux_free_path(&path);
	return(error);
}

/*
 * MPALMOSTSAFE
 */
int
sys_linux_mkdir(struct linux_mkdir_args *args)
{
	struct nlookupdata nd;
	char *path;
	int error;

	error = linux_copyin_path(args->path, &path, LINUX_PATH_CREATE);
	if (error)
		return (error);
#ifdef DEBUG
	if (ldebug(mkdir))
		kprintf(ARGS(mkdir, "%s, %d"), path, args->mode);
#endif
	get_mplock();
	error = nlookup_init(&nd, path, UIO_SYSSPACE, 0);
	if (error == 0)
		error = kern_mkdir(&nd, args->mode);
	nlookup_done(&nd);
	rel_mplock();

	linux_free_path(&path);
	return(error);
}

int
sys_linux_mkdirat(struct linux_mkdirat_args *args)
{
	struct nlookupdata nd;
	struct file *fp;
	char *path;
	int dfd, error;

	error = linux_copyin_path(args->path, &path, LINUX_PATH_CREATE);
	if (error)
		return (error);
#ifdef DEBUG
	if (ldebug(mkdir))
		kprintf(ARGS(mkdir, "%s, %d"), path, args->mode);
#endif
	dfd = (args->dfd == LINUX_AT_FDCWD) ? AT_FDCWD : args->dfd;
	get_mplock();
	error = nlookup_init_at(&nd, &fp, dfd, path, UIO_SYSSPACE, 0);
	if (error == 0)
		error = kern_mkdir(&nd, args->mode);
	nlookup_done_at(&nd, fp);
	rel_mplock();

	linux_free_path(&path);
	return(error);
}

/*
 * MPALMOSTSAFE
 */
int
sys_linux_rmdir(struct linux_rmdir_args *args)
{
	struct nlookupdata nd;
	char *path;
	int error;

	error = linux_copyin_path(args->path, &path, LINUX_PATH_EXISTS);
	if (error)
		return (error);
#ifdef DEBUG
	if (ldebug(rmdir))
		kprintf(ARGS(rmdir, "%s"), path);
#endif
	get_mplock();
	error = nlookup_init(&nd, path, UIO_SYSSPACE, 0);
	if (error == 0)
		error = kern_rmdir(&nd);
	nlookup_done(&nd);
	rel_mplock();
	linux_free_path(&path);
	return(error);
}

/*
 * MPALMOSTSAFE
 */
int
sys_linux_rename(struct linux_rename_args *args)
{
	struct nlookupdata fromnd, tond;
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
		kprintf(ARGS(rename, "%s, %s"), from, to);
#endif
	get_mplock();
	do {
		error = nlookup_init(&fromnd, from, UIO_SYSSPACE, 0);
		if (error == 0) {
			error = nlookup_init(&tond, to, UIO_SYSSPACE, 0);
			if (error == 0)
				error = kern_rename(&fromnd, &tond);
			nlookup_done(&tond);
		}
		nlookup_done(&fromnd);
	} while (error == EAGAIN);
	rel_mplock();
	linux_free_path(&from);
	linux_free_path(&to);
	return(error);
}

int
sys_linux_renameat(struct linux_renameat_args *args)
{
	struct nlookupdata fromnd, tond;
	struct file *fp, *fp2;
	char *from, *to;
	int olddfd, newdfd,error;

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
		kprintf(ARGS(rename, "%s, %s"), from, to);
#endif
	olddfd = (args->olddfd == LINUX_AT_FDCWD) ? AT_FDCWD : args->olddfd;
	newdfd = (args->newdfd == LINUX_AT_FDCWD) ? AT_FDCWD : args->newdfd;
	get_mplock();
	error = nlookup_init_at(&fromnd, &fp, olddfd, from, UIO_SYSSPACE, 0);
	if (error == 0) {
		error = nlookup_init_at(&tond, &fp2, newdfd, to, UIO_SYSSPACE, 0);
		if (error == 0)
			error = kern_rename(&fromnd, &tond);
		nlookup_done_at(&tond, fp2);
	}
	nlookup_done_at(&fromnd, fp);
	rel_mplock();
	linux_free_path(&from);
	linux_free_path(&to);
	return(error);
}

/*
 * MPALMOSTSAFE
 */
int
sys_linux_symlink(struct linux_symlink_args *args)
{
	struct thread *td = curthread;
	struct nlookupdata nd;
	char *path, *link;
	int error;
	int mode;

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
		kprintf(ARGS(symlink, "%s, %s"), path, link);
#endif
	get_mplock();
	error = nlookup_init(&nd, link, UIO_SYSSPACE, 0);
	if (error == 0) {
		mode = ACCESSPERMS & ~td->td_proc->p_fd->fd_cmask;
		error = kern_symlink(&nd, path, mode);
	}
	nlookup_done(&nd);
	rel_mplock();
	linux_free_path(&path);
	linux_free_path(&link);
	return(error);
}

int
sys_linux_symlinkat(struct linux_symlinkat_args *args)
{
	struct thread *td = curthread;
	struct nlookupdata nd;
	struct file *fp;
	char *path, *link;
	int error;
	int newdfd, mode;

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
		kprintf(ARGS(symlink, "%s, %s"), path, link);
#endif
	newdfd = (args->newdfd == LINUX_AT_FDCWD) ? AT_FDCWD : args->newdfd;
	get_mplock();
	error = nlookup_init_at(&nd, &fp, newdfd, link, UIO_SYSSPACE, 0);
	if (error == 0) {
		mode = ACCESSPERMS & ~td->td_proc->p_fd->fd_cmask;
		error = kern_symlink(&nd, path, mode);
	}
	nlookup_done_at(&nd, fp);
	rel_mplock();
	linux_free_path(&path);
	linux_free_path(&link);
	return(error);
}

/*
 * MPALMOSTSAFE
 */
int
sys_linux_readlink(struct linux_readlink_args *args)
{
	struct nlookupdata nd;
	char *path;
	int error;

	error = linux_copyin_path(args->name, &path, LINUX_PATH_EXISTS);
	if (error)
		return (error);
#ifdef DEBUG
	if (ldebug(readlink))
		kprintf(ARGS(readlink, "%s, %p, %d"), path, (void *)args->buf,
		    args->count);
#endif
	get_mplock();
	error = nlookup_init(&nd, path, UIO_SYSSPACE, 0);
	if (error == 0) {
		error = kern_readlink(&nd, args->buf, args->count,
				      &args->sysmsg_iresult);
	}
	nlookup_done(&nd);
	rel_mplock();
	linux_free_path(&path);
	return(error);
}

int
sys_linux_readlinkat(struct linux_readlinkat_args *args)
{
	struct nlookupdata nd;
	struct file *fp;
	char *path;
	int dfd, error;

	error = linux_copyin_path(args->path, &path, LINUX_PATH_EXISTS);
	if (error)
		return (error);
#ifdef DEBUG
	if (ldebug(readlink))
		kprintf(ARGS(readlink, "%s, %p, %d"), path, (void *)args->buf,
		    args->count);
#endif
	dfd = (args->dfd == LINUX_AT_FDCWD) ? AT_FDCWD : args->dfd;
	get_mplock();
	error = nlookup_init_at(&nd, &fp, dfd, path, UIO_SYSSPACE, 0);
	if (error == 0) {
		error = kern_readlink(&nd, args->buf, args->count,
				      &args->sysmsg_iresult);
	}
	nlookup_done_at(&nd, fp);
	rel_mplock();
	linux_free_path(&path);
	return(error);
}

/*
 * MPALMOSTSAFE
 */
int
sys_linux_truncate(struct linux_truncate_args *args)
{
	struct nlookupdata nd;
	char *path;
	int error;

	error = linux_copyin_path(args->path, &path, LINUX_PATH_EXISTS);
	if (error)
		return (error);
#ifdef DEBUG
	if (ldebug(truncate))
		kprintf(ARGS(truncate, "%s, %ld"), path,
		    (long)args->length);
#endif
	get_mplock();
	error = nlookup_init(&nd, path, UIO_SYSSPACE, NLC_FOLLOW);
	if (error == 0)
		error = kern_truncate(&nd, args->length);
	nlookup_done(&nd);
	rel_mplock();
	linux_free_path(&path);
	return(error);
}

/*
 * MPALMOSTSAFE
 */
int
sys_linux_truncate64(struct linux_truncate64_args *args)
{
	struct nlookupdata nd;
	char *path;
	int error;

	error = linux_copyin_path(args->path, &path, LINUX_PATH_EXISTS);
	if (error)
		return (error);
#ifdef DEBUG
	if (ldebug(truncate64))
		kprintf(ARGS(truncate64, "%s, %lld"), path,
		    (off_t)args->length);
#endif
	get_mplock();
	error = nlookup_init(&nd, path, UIO_SYSSPACE, NLC_FOLLOW);
	if (error == 0)
		error = kern_truncate(&nd, args->length);
	nlookup_done(&nd);
	rel_mplock();
	linux_free_path(&path);
	return error;
}

/*
 * MPALMOSTSAFE
 */
int
sys_linux_ftruncate(struct linux_ftruncate_args *args)
{
	int error;

#ifdef DEBUG
	if (ldebug(ftruncate))
		kprintf(ARGS(ftruncate, "%d, %ld"), args->fd,
		    (long)args->length);
#endif
	get_mplock();
	error = kern_ftruncate(args->fd, args->length);
	rel_mplock();

	return error;
}

/*
 * MPALMOSTSAFE
 */
int
sys_linux_ftruncate64(struct linux_ftruncate64_args *args)
{
	int error;

#ifdef DEBUG
	if (ldebug(ftruncate))
		kprintf(ARGS(ftruncate64, "%d, %lld"), args->fd,
		    (off_t)args->length);
#endif
	get_mplock();
	error = kern_ftruncate(args->fd, args->length);
	rel_mplock();

	return error;
}

/*
 * MPALMOSTSAFE
 */
int
sys_linux_link(struct linux_link_args *args)
{
	struct nlookupdata nd, linknd;
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
		kprintf(ARGS(link, "%s, %s"), path, link);
#endif
	get_mplock();
	error = nlookup_init(&nd, path, UIO_SYSSPACE, NLC_FOLLOW);
	if (error == 0) {
		error = nlookup_init(&linknd, link, UIO_SYSSPACE, 0);
		if (error == 0)
			error = kern_link(&nd, &linknd);
		nlookup_done(&linknd);
	}
	nlookup_done(&nd);
	rel_mplock();
	linux_free_path(&path);
	linux_free_path(&link);
	return(error);
}

int
sys_linux_linkat(struct linux_linkat_args *args)
{
	struct nlookupdata nd, linknd;
	struct file *fp, *fp2;
	char *path, *link;
	int olddfd, newdfd, error;

	if (args->flags != 0)
		return (EINVAL);

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
		kprintf(ARGS(link, "%s, %s"), path, link);
#endif
	olddfd = (args->olddfd == LINUX_AT_FDCWD) ? AT_FDCWD : args->olddfd;
	newdfd = (args->newdfd == LINUX_AT_FDCWD) ? AT_FDCWD : args->newdfd;
	get_mplock();
	error = nlookup_init_at(&nd, &fp, olddfd, path, UIO_SYSSPACE, NLC_FOLLOW);
	if (error == 0) {
		error = nlookup_init_at(&linknd, &fp2, newdfd, link, UIO_SYSSPACE, 0);
		if (error == 0)
			error = kern_link(&nd, &linknd);
		nlookup_done_at(&linknd, fp2);
	}
	nlookup_done_at(&nd, fp);
	rel_mplock();
	linux_free_path(&path);
	linux_free_path(&link);
	return(error);
}

/*
 * MPSAFE
 */
int
sys_linux_fdatasync(struct linux_fdatasync_args *uap)
{
	struct fsync_args bsd;
	int error;

	bsd.fd = uap->fd;
	bsd.sysmsg_iresult = 0;

	error = sys_fsync(&bsd);
	uap->sysmsg_iresult = bsd.sysmsg_iresult;
	return(error);
}

/*
 * MPSAFE
 */
int
sys_linux_pread(struct linux_pread_args *uap)
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

	if ((ssize_t)auio.uio_resid < 0) {
		error = EINVAL;
	} else {
		error = kern_preadv(uap->fd, &auio, O_FOFFSET,
				    &uap->sysmsg_szresult);
	}
	return(error);
}

/*
 * MPSAFE
 */
int
sys_linux_pwrite(struct linux_pwrite_args *uap)
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

	if ((ssize_t)auio.uio_resid < 0) {
		error = EINVAL;
	} else {
		error = kern_pwritev(uap->fd, &auio, O_FOFFSET,
				     &uap->sysmsg_szresult);
	}
	return(error);
}

/*
 * MPSAFE
 */
int
sys_linux_oldumount(struct linux_oldumount_args *args)
{
	struct linux_umount_args args2;
	int error;

	args2.path = args->path;
	args2.flags = 0;
	args2.sysmsg_iresult = 0;
	error = sys_linux_umount(&args2);
	args->sysmsg_iresult = args2.sysmsg_iresult;
	return(error);
}

/*
 * MPSAFE
 */
int
sys_linux_umount(struct linux_umount_args *args)
{
	struct unmount_args bsd;
	int error;

	bsd.path = args->path;
	bsd.flags = args->flags;	/* XXX correct? */
	bsd.sysmsg_iresult = 0;

	error = sys_unmount(&bsd);
	args->sysmsg_iresult = bsd.sysmsg_iresult;
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

/*
 * MPSAFE
 */
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

/*
 * MPSAFE
 */
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

/*
 * MPSAFE
 */
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

/*
 * MPSAFE
 */
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

/*
 * MPSAFE
 */
static int
linux_fcntl_common(struct linux_fcntl64_args *args)
{
	struct thread *td = curthread;
	struct l_flock linux_flock;
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
		fp = holdfp(td->td_proc->p_fd, args->fd, -1);
		if (fp == NULL)
			return (EBADF);
		if (fp->f_type == DTYPE_PIPE) {
			fdrop(fp);
			return (EINVAL);
		}
		fdrop(fp);
		cmd = F_SETOWN;
		dat.fc_owner = args->arg;
		break;
	default:
		return (EINVAL);
	}

	/* MPSAFE */
	error = kern_fcntl(args->fd, cmd, &dat, td->td_ucred);

	if (error == 0) {
		switch (args->cmd) {
		case LINUX_F_DUPFD:
			args->sysmsg_iresult = dat.fc_fd;
			break;
		case LINUX_F_GETFD:
			args->sysmsg_iresult = dat.fc_cloexec;
			break;
		case LINUX_F_SETFD:
			break;
		case LINUX_F_GETFL:
			args->sysmsg_iresult = 0;
			if (dat.fc_flags & O_RDONLY)
				args->sysmsg_iresult |= LINUX_O_RDONLY;
			if (dat.fc_flags & O_WRONLY)
				args->sysmsg_iresult |= LINUX_O_WRONLY;
			if (dat.fc_flags & O_RDWR)
				args->sysmsg_iresult |= LINUX_O_RDWR;
			if (dat.fc_flags & O_NDELAY)
				args->sysmsg_iresult |= LINUX_O_NONBLOCK;
			if (dat.fc_flags & O_APPEND)
				args->sysmsg_iresult |= LINUX_O_APPEND;
			if (dat.fc_flags & O_FSYNC)
				args->sysmsg_iresult |= LINUX_O_SYNC;
			if (dat.fc_flags & O_ASYNC)
				args->sysmsg_iresult |= LINUX_FASYNC;
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
			args->sysmsg_iresult = dat.fc_owner;
			break;
		case LINUX_F_SETOWN:
			break;
		}
	}

	return(error);
}

/*
 * MPSAFE
 */
int
sys_linux_fcntl(struct linux_fcntl_args *args)
{
	struct linux_fcntl64_args args64;
	int error;

#ifdef DEBUG
	if (ldebug(fcntl))
		kprintf(ARGS(fcntl, "%d, %08x, *"), args->fd, args->cmd);
#endif

	args64.fd = args->fd;
	args64.cmd = args->cmd;
	args64.arg = args->arg;
	args64.sysmsg_iresult = 0;
	error = linux_fcntl_common(&args64);
	args->sysmsg_iresult = args64.sysmsg_iresult;
	return(error);
}

#if defined(__i386__)
/*
 * MPSAFE
 */
int
sys_linux_fcntl64(struct linux_fcntl64_args *args)
{
	struct thread *td = curthread;
	struct l_flock64 linux_flock;
	union fcntl_dat dat;
	int error, cmd = 0;

#ifdef DEBUG
	if (ldebug(fcntl64))
		kprintf(ARGS(fcntl64, "%d, %08x, *"), args->fd, args->cmd);
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

		/* MPSAFE */
		error = kern_fcntl(args->fd, cmd, &dat, td->td_ucred);

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

/*
 * MPALMOSTSAFE
 */
int
sys_linux_chown(struct linux_chown_args *args)
{
	struct nlookupdata nd;
	char *path;
	int error;

	error = linux_copyin_path(args->path, &path, LINUX_PATH_EXISTS);
	if (error)
		return (error);
#ifdef DEBUG
	if (ldebug(chown))
		kprintf(ARGS(chown, "%s, %d, %d"), path, args->uid, args->gid);
#endif
	get_mplock();
	error = nlookup_init(&nd, path, UIO_SYSSPACE, NLC_FOLLOW);
	if (error == 0)
		error = kern_chown(&nd, args->uid, args->gid);
	nlookup_done(&nd);
	rel_mplock();
	linux_free_path(&path);
	return(error);
}

/*
 * MPALMOSTSAFE
 */
int
sys_linux_lchown(struct linux_lchown_args *args)
{
	struct nlookupdata nd;
	char *path;
	int error;

	error = linux_copyin_path(args->path, &path, LINUX_PATH_EXISTS);
	if (error)
		return (error);
#ifdef DEBUG
	if (ldebug(lchown))
		kprintf(ARGS(lchown, "%s, %d, %d"), path, args->uid, args->gid);
#endif
	get_mplock();
	error = nlookup_init(&nd, path, UIO_SYSSPACE, 0);
	if (error == 0)
		error = kern_chown(&nd, args->uid, args->gid);
	nlookup_done(&nd);
	rel_mplock();
	linux_free_path(&path);
	return(error);
}

int
sys_linux_fchmodat(struct linux_fchmodat_args *args)
{
	struct fchmodat_args uap;
	int error;

	uap.fd = (args->dfd == LINUX_AT_FDCWD) ? AT_FDCWD : args->dfd;
	uap.path = args->filename;
	uap.mode = args->mode;
	uap.flags = 0;

	error = sys_fchmodat(&uap);

	return (error);
}

int
sys_linux_fchownat(struct linux_fchownat_args *args)
{
	struct fchownat_args uap;
	int error;

	if (args->flag & ~LINUX_AT_SYMLINK_NOFOLLOW)
		return (EINVAL);

	uap.fd = (args->dfd == LINUX_AT_FDCWD) ? AT_FDCWD : args->dfd;
	uap.path = args->filename;
	uap.uid = args->uid;
	uap.gid = args->gid;
	uap.flags = (args->flag & LINUX_AT_SYMLINK_NOFOLLOW) == 0 ? 0 :
	    AT_SYMLINK_NOFOLLOW;

	error = sys_fchownat(&uap);

	return (error);
}

int
sys_linux_faccessat(struct linux_faccessat_args *args)
{
	struct faccessat_args uap;
	int error;

	uap.fd = (args->dfd == LINUX_AT_FDCWD) ? AT_FDCWD : args->dfd;
	uap.path = args->filename;
	uap.amode = args->mode;
	uap.flags = 0;

	error = sys_faccessat(&uap);

	return error;
}
