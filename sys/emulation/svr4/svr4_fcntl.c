/*
 * Copyright (c) 1998 Mark Newton
 * Copyright (c) 1994, 1997 Christos Zoulas.  
 * All rights reserved.
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
 *	This product includes software developed by Christos Zoulas.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
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
 * $FreeBSD: src/sys/svr4/svr4_fcntl.c,v 1.7 1999/12/12 10:27:04 newton Exp $
 * $DragonFly: src/sys/emulation/svr4/Attic/svr4_fcntl.c,v 1.15 2004/10/12 19:20:42 dillon Exp $
 */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/namei.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/filedesc.h>
/*#include <sys/ioctl.h>*/
#include <sys/kernel.h>
#include <sys/mount.h>
#include <sys/malloc.h>
#include <sys/vnode.h>
#include <sys/unistd.h>

#include <sys/sysproto.h>

#include "svr4.h"
#include "svr4_types.h"
#include "svr4_signal.h"
#include "svr4_proto.h"
#include "svr4_util.h"
#include "svr4_fcntl.h"

static int svr4_to_bsd_flags (int);
static u_long svr4_to_bsd_cmd (u_long);
static int fd_revoke (struct thread *, int);
static int fd_truncate (struct thread *, int, struct flock *, int *);
static int bsd_to_svr4_flags (int);
static void bsd_to_svr4_flock (struct flock *, struct svr4_flock *);
static void svr4_to_bsd_flock (struct svr4_flock *, struct flock *);
static void bsd_to_svr4_flock64 (struct flock *, struct svr4_flock64 *);
static void svr4_to_bsd_flock64 (struct svr4_flock64 *, struct flock *);

static u_long
svr4_to_bsd_cmd(cmd)
	u_long	cmd;
{
	switch (cmd) {
	case SVR4_F_DUPFD:
		return F_DUPFD;
	case SVR4_F_GETFD:
		return F_GETFD;
	case SVR4_F_SETFD:
		return F_SETFD;
	case SVR4_F_GETFL:
		return F_GETFL;
	case SVR4_F_SETFL:
		return F_SETFL;
	case SVR4_F_GETLK:
		return F_GETLK;
	case SVR4_F_SETLK:
		return F_SETLK;
	case SVR4_F_SETLKW:
		return F_SETLKW;
	default:
		return -1;
	}
}

static int
svr4_to_bsd_flags(l)
	int	l;
{
	int	r = 0;
	r |= (l & SVR4_O_RDONLY) ? O_RDONLY : 0;
	r |= (l & SVR4_O_WRONLY) ? O_WRONLY : 0;
	r |= (l & SVR4_O_RDWR) ? O_RDWR : 0;
	r |= (l & SVR4_O_NDELAY) ? O_NONBLOCK : 0;
	r |= (l & SVR4_O_APPEND) ? O_APPEND : 0;
	r |= (l & SVR4_O_SYNC) ? O_FSYNC : 0;
	r |= (l & SVR4_O_NONBLOCK) ? O_NONBLOCK : 0;
	r |= (l & SVR4_O_PRIV) ? O_EXLOCK : 0;
	r |= (l & SVR4_O_CREAT) ? O_CREAT : 0;
	r |= (l & SVR4_O_TRUNC) ? O_TRUNC : 0;
	r |= (l & SVR4_O_EXCL) ? O_EXCL : 0;
	r |= (l & SVR4_O_NOCTTY) ? O_NOCTTY : 0;
	return r;
}

static int
bsd_to_svr4_flags(l)
	int	l;
{
	int	r = 0;
	r |= (l & O_RDONLY) ? SVR4_O_RDONLY : 0;
	r |= (l & O_WRONLY) ? SVR4_O_WRONLY : 0;
	r |= (l & O_RDWR) ? SVR4_O_RDWR : 0;
	r |= (l & O_NDELAY) ? SVR4_O_NONBLOCK : 0;
	r |= (l & O_APPEND) ? SVR4_O_APPEND : 0;
	r |= (l & O_FSYNC) ? SVR4_O_SYNC : 0;
	r |= (l & O_NONBLOCK) ? SVR4_O_NONBLOCK : 0;
	r |= (l & O_EXLOCK) ? SVR4_O_PRIV : 0;
	r |= (l & O_CREAT) ? SVR4_O_CREAT : 0;
	r |= (l & O_TRUNC) ? SVR4_O_TRUNC : 0;
	r |= (l & O_EXCL) ? SVR4_O_EXCL : 0;
	r |= (l & O_NOCTTY) ? SVR4_O_NOCTTY : 0;
	return r;
}


static void
bsd_to_svr4_flock(iflp, oflp)
	struct flock		*iflp;
	struct svr4_flock	*oflp;
{
	switch (iflp->l_type) {
	case F_RDLCK:
		oflp->l_type = SVR4_F_RDLCK;
		break;
	case F_WRLCK:
		oflp->l_type = SVR4_F_WRLCK;
		break;
	case F_UNLCK:
		oflp->l_type = SVR4_F_UNLCK;
		break;
	default:
		oflp->l_type = -1;
		break;
	}

	oflp->l_whence = (short) iflp->l_whence;
	oflp->l_start = (svr4_off_t) iflp->l_start;
	oflp->l_len = (svr4_off_t) iflp->l_len;
	oflp->l_sysid = 0;
	oflp->l_pid = (svr4_pid_t) iflp->l_pid;
}


static void
svr4_to_bsd_flock(iflp, oflp)
	struct svr4_flock	*iflp;
	struct flock		*oflp;
{
	switch (iflp->l_type) {
	case SVR4_F_RDLCK:
		oflp->l_type = F_RDLCK;
		break;
	case SVR4_F_WRLCK:
		oflp->l_type = F_WRLCK;
		break;
	case SVR4_F_UNLCK:
		oflp->l_type = F_UNLCK;
		break;
	default:
		oflp->l_type = -1;
		break;
	}

	oflp->l_whence = iflp->l_whence;
	oflp->l_start = (off_t) iflp->l_start;
	oflp->l_len = (off_t) iflp->l_len;
	oflp->l_pid = (pid_t) iflp->l_pid;

}

static void
bsd_to_svr4_flock64(iflp, oflp)
	struct flock		*iflp;
	struct svr4_flock64	*oflp;
{
	switch (iflp->l_type) {
	case F_RDLCK:
		oflp->l_type = SVR4_F_RDLCK;
		break;
	case F_WRLCK:
		oflp->l_type = SVR4_F_WRLCK;
		break;
	case F_UNLCK:
		oflp->l_type = SVR4_F_UNLCK;
		break;
	default:
		oflp->l_type = -1;
		break;
	}

	oflp->l_whence = (short) iflp->l_whence;
	oflp->l_start = (svr4_off64_t) iflp->l_start;
	oflp->l_len = (svr4_off64_t) iflp->l_len;
	oflp->l_sysid = 0;
	oflp->l_pid = (svr4_pid_t) iflp->l_pid;
}


static void
svr4_to_bsd_flock64(iflp, oflp)
	struct svr4_flock64	*iflp;
	struct flock		*oflp;
{
	switch (iflp->l_type) {
	case SVR4_F_RDLCK:
		oflp->l_type = F_RDLCK;
		break;
	case SVR4_F_WRLCK:
		oflp->l_type = F_WRLCK;
		break;
	case SVR4_F_UNLCK:
		oflp->l_type = F_UNLCK;
		break;
	default:
		oflp->l_type = -1;
		break;
	}

	oflp->l_whence = iflp->l_whence;
	oflp->l_start = (off_t) iflp->l_start;
	oflp->l_len = (off_t) iflp->l_len;
	oflp->l_pid = (pid_t) iflp->l_pid;

}


static int
fd_revoke(struct thread *td, int fd)
{
	struct proc *p = td->td_proc;
	struct filedesc *fdp;
	struct file *fp;
	struct vnode *vp;
	struct vattr vattr;
	int error;

	KKASSERT(p);

	fdp = p->p_fd;
	if ((u_int)fd >= fdp->fd_nfiles || (fp = fdp->fd_ofiles[fd]) == NULL)
		return EBADF;

	if (fp->f_type != DTYPE_VNODE) 
		return EINVAL;

	vp = (struct vnode *) fp->f_data;

	if ((error = vx_get(vp)) != 0)
		return (error);

	if (vp->v_type != VCHR && vp->v_type != VBLK) {
		error = EINVAL;
		goto out;
	}

	if ((error = VOP_GETATTR(vp, &vattr, td)) != 0)
		goto out;

	if (p->p_ucred->cr_uid != vattr.va_uid &&
	    (error = suser(p->p_thread)) != 0)
		goto out;

	if (vcount(vp) > 1)
		VOP_REVOKE(vp, REVOKEALL);
out:
	vx_put(vp);
	return error;
}


static int
fd_truncate(struct thread *td, int fd, struct flock *flp, int *retval)
{
	struct proc *p = td->td_proc;
	struct filedesc *fdp;
	struct file *fp;
	off_t start, length;
	struct vnode *vp;
	struct vattr vattr;
	int error;
	struct ftruncate_args ft;

	KKASSERT(p);
	fdp = p->p_fd;

	/*
	 * We only support truncating the file.
	 */
	if ((u_int)fd >= fdp->fd_nfiles || (fp = fdp->fd_ofiles[fd]) == NULL)
		return EBADF;

	vp = (struct vnode *)fp->f_data;
	if (fp->f_type != DTYPE_VNODE || vp->v_type == VFIFO)
		return ESPIPE;

	if ((error = VOP_GETATTR(vp, &vattr, td)) != 0)
		return error;

	length = vattr.va_size;

	switch (flp->l_whence) {
	case SEEK_CUR:
		start = fp->f_offset + flp->l_start;
		break;

	case SEEK_END:
		start = flp->l_start + length;
		break;

	case SEEK_SET:
		start = flp->l_start;
		break;

	default:
		return EINVAL;
	}

	if (start + flp->l_len < length) {
		/* We don't support free'ing in the middle of the file */
		return EINVAL;
	}

	SCARG(&ft, fd) = fd;
	SCARG(&ft, length) = start;
	error = ftruncate(&ft);
	*retval = ft.sysmsg_result;
	return(error);
}

int
svr4_sys_open(struct svr4_sys_open_args *uap)
{
	struct thread *td = curthread;	/* XXX */
	struct proc *p = td->td_proc;
	struct open_args cup;
	int	error, retval;

	caddr_t sg = stackgap_init();
	CHECKALTEXIST(&sg, SCARG(uap, path));

	cup.sysmsg_result = 0;
	cup.path = uap->path;
	cup.flags = svr4_to_bsd_flags(uap->flags);
	cup.mode = uap->mode;
	error = open(&cup);

	if (error) {
	  /*	        uprintf("svr4_open(%s, 0x%0x, 0%o): %d\n", uap->path,
			uap->flags, uap->mode, error);*/
		return error;
	}

	KKASSERT(p);
	retval = uap->sysmsg_result = cup.sysmsg_result;

	if (!(SCARG(&cup, flags) & O_NOCTTY) && SESS_LEADER(p) &&
	    !(p->p_flag & P_CONTROLT)) {
#if defined(NOTYET)
		struct filedesc	*fdp = p->p_fd;
		struct file	*fp = fdp->fd_ofiles[retval];

		/* ignore any error, just give it a try */
		if (fp->f_type == DTYPE_VNODE)
			fo_ioctl(fp, TIOCSCTTY, (caddr_t) 0, p);
#endif
	}
	return error;
}

int
svr4_sys_open64(struct svr4_sys_open64_args *uap)
{
	return svr4_sys_open((struct svr4_sys_open_args *)uap);
}

int
svr4_sys_creat(struct svr4_sys_creat_args *uap)
{
	struct open_args cup;
	int error;

	caddr_t sg = stackgap_init();
	CHECKALTEXIST(&sg, SCARG(uap, path));

	SCARG(&cup, path) = SCARG(uap, path);
	SCARG(&cup, mode) = SCARG(uap, mode);
	SCARG(&cup, flags) = O_WRONLY | O_CREAT | O_TRUNC;

	cup.sysmsg_result = 0;
	error = open(&cup);
	uap->sysmsg_result = cup.sysmsg_result;
	return(error);
}

int
svr4_sys_creat64(struct svr4_sys_creat64_args *uap)
{
	return svr4_sys_creat((struct svr4_sys_creat_args *)uap);
}

int
svr4_sys_llseek(struct svr4_sys_llseek_args *v)
{
	struct svr4_sys_llseek_args *uap = v;
	struct lseek_args ap;

	SCARG(&ap, fd) = SCARG(uap, fd);

#if BYTE_ORDER == BIG_ENDIAN
	SCARG(&ap, offset) = (((long long) SCARG(uap, offset1)) << 32) | 
		SCARG(uap, offset2);
#else
	SCARG(&ap, offset) = (((long long) SCARG(uap, offset2)) << 32) | 
		SCARG(uap, offset1);
#endif
	SCARG(&ap, whence) = SCARG(uap, whence);

	return lseek(&ap);
}

int
svr4_sys_access(struct svr4_sys_access_args *uap)
{
	struct access_args cup;
	int error;

	caddr_t sg = stackgap_init();
	CHECKALTEXIST(&sg, SCARG(uap, path));

	SCARG(&cup, path) = SCARG(uap, path);
	SCARG(&cup, flags) = SCARG(uap, flags);
	cup.sysmsg_result = 0;
	error = access(&cup);
	uap->sysmsg_result = cup.sysmsg_result;
	return(error);
}

#if defined(NOTYET)
int
svr4_sys_pread(struct svr4_sys_pread_args *uap)
{
	struct pread_args pra;
	int error;

	/*
	 * Just translate the args structure and call the NetBSD
	 * pread(2) system call (offset type is 64-bit in NetBSD).
	 */
	SCARG(&pra, fd) = SCARG(uap, fd);
	SCARG(&pra, buf) = SCARG(uap, buf);
	SCARG(&pra, nbyte) = SCARG(uap, nbyte);
	SCARG(&pra, offset) = SCARG(uap, off);

	pra.sysmsg_result = 0;
	error = pread(&pra);
	uap->sysmsg_result = pra.sysmsg_result;
	return(error);
}
#endif

#if defined(NOTYET)
int
svr4_sys_pread64(struct thread *td, void *v, register_t *retval)
{
	struct svr4_sys_pread64_args *uap = v;
	struct sys_pread_args pra;
	int error;

	/*
	 * Just translate the args structure and call the NetBSD
	 * pread(2) system call (offset type is 64-bit in NetBSD).
	 */
	SCARG(&pra, fd) = SCARG(uap, fd);
	SCARG(&pra, buf) = SCARG(uap, buf);
	SCARG(&pra, nbyte) = SCARG(uap, nbyte);
	SCARG(&pra, offset) = SCARG(uap, off);

	pra.sysmsg_result = 0;
	error = sys_pread(&pra, retval);
	uap->sysmsg_result = pra.sysmsg_result;
	return(error);
}
#endif /* NOTYET */

#if defined(NOTYET)
int
svr4_sys_pwrite(struct svr4_sys_pwrite_args *uap)
{
	struct pwrite_args pwa;
	int error;

	/*
	 * Just translate the args structure and call the NetBSD
	 * pwrite(2) system call (offset type is 64-bit in NetBSD).
	 */
	SCARG(&pwa, fd) = SCARG(uap, fd);
	SCARG(&pwa, buf) = SCARG(uap, buf);
	SCARG(&pwa, nbyte) = SCARG(uap, nbyte);
	SCARG(&pwa, offset) = SCARG(uap, off);

	pwa.sysmsg_result = 0;
	error = pwrite(&pwa);
	uap->sysmsg_result = pwa.sysmsg_result;
	return(error);
}
#endif

#if defined(NOTYET)
int
svr4_sys_pwrite64(struct thread *td, void *v, register_t *retval)
{
	struct svr4_sys_pwrite64_args *uap = v;
	struct sys_pwrite_args pwa;

	/*
	 * Just translate the args structure and call the NetBSD
	 * pwrite(2) system call (offset type is 64-bit in NetBSD).
	 */
	SCARG(&pwa, fd) = SCARG(uap, fd);
	SCARG(&pwa, buf) = SCARG(uap, buf);
	SCARG(&pwa, nbyte) = SCARG(uap, nbyte);
	SCARG(&pwa, offset) = SCARG(uap, off);

	return (sys_pwrite(p, &pwa, retval));
}
#endif /* NOTYET */

int
svr4_sys_fcntl(struct svr4_sys_fcntl_args *uap)
{
	struct thread *td = curthread;	/* XXX */
	int				error;
	struct fcntl_args		fa;
	int                             *retval;

	retval = &uap->sysmsg_result;

	SCARG(&fa, fd) = SCARG(uap, fd);
	SCARG(&fa, cmd) = svr4_to_bsd_cmd(SCARG(uap, cmd));

	switch (SCARG(&fa, cmd)) {
	case F_DUPFD:
	case F_GETFD:
	case F_SETFD:
		SCARG(&fa, arg) = (long) SCARG(uap, arg);
		fa.sysmsg_result = 0;
		error = fcntl(&fa);
		*retval = fa.sysmsg_result;
		return error;

	case F_GETFL:
		SCARG(&fa, arg) = (long) SCARG(uap, arg);
		error = fcntl(&fa);
		if (error)
			return error;
		*retval = bsd_to_svr4_flags(fa.sysmsg_result);
		return error;

	case F_SETFL:
		{
			/*
			 * we must save the O_ASYNC flag, as that is
			 * handled by ioctl(_, I_SETSIG, _) emulation.
			 */
			long cmd;
			int flags;

			DPRINTF(("Setting flags %p\n", SCARG(uap, arg)));
			cmd = SCARG(&fa, cmd); /* save it for a while */

			SCARG(&fa, cmd) = F_GETFL;
			if ((error = fcntl(&fa)) != 0)
				return error;
			flags = fa.sysmsg_result;
			flags &= O_ASYNC;
			flags |= svr4_to_bsd_flags((u_long) SCARG(uap, arg));
			SCARG(&fa, cmd) = cmd;
			SCARG(&fa, arg) = (long) flags;
			error = fcntl(&fa);
			*retval = fa.sysmsg_result;
			return error;
		}

	case F_GETLK:
	case F_SETLK:
	case F_SETLKW:
		{
			struct svr4_flock	 ifl;
			struct flock		*flp, fl;
			caddr_t sg = stackgap_init();

			flp = stackgap_alloc(&sg, sizeof(struct flock));
			SCARG(&fa, arg) = (long) flp;

			error = copyin(SCARG(uap, arg), &ifl, sizeof ifl);
			if (error)
				return error;

			svr4_to_bsd_flock(&ifl, &fl);

			error = copyout(&fl, flp, sizeof fl);
			if (error)
				return error;

			fa.sysmsg_result = 0;
			error = fcntl(&fa);
			*retval = fa.sysmsg_result;
			if (error || SCARG(&fa, cmd) != F_GETLK)
				return error;

			error = copyin(flp, &fl, sizeof fl);
			if (error)
				return error;

			bsd_to_svr4_flock(&fl, &ifl);

			return copyout(&ifl, SCARG(uap, arg), sizeof ifl);
		}
	case -1:
		switch (SCARG(uap, cmd)) {
		case SVR4_F_DUP2FD:
			{
				struct dup2_args du;

				SCARG(&du, from) = SCARG(uap, fd);
				SCARG(&du, to) = (int)SCARG(uap, arg);
				error = dup2(&du);
				if (error)
					return error;
				*retval = SCARG(&du, to);
				return 0;
			}

		case SVR4_F_FREESP:
			{
				struct svr4_flock	 ifl;
				struct flock		 fl;

				error = copyin(SCARG(uap, arg), &ifl,
				    sizeof ifl);
				if (error)
					return error;
				svr4_to_bsd_flock(&ifl, &fl);
				return fd_truncate(td, SCARG(uap, fd), &fl, retval);
			}

		case SVR4_F_GETLK64:
		case SVR4_F_SETLK64:
		case SVR4_F_SETLKW64:
			{
				struct svr4_flock64	 ifl;
				struct flock		*flp, fl;
				caddr_t sg = stackgap_init();

				flp = stackgap_alloc(&sg, sizeof(struct flock));
				SCARG(&fa, arg) = (long) flp;

				error = copyin(SCARG(uap, arg), &ifl,
				    sizeof ifl);
				if (error)
					return error;

				svr4_to_bsd_flock64(&ifl, &fl);

				error = copyout(&fl, flp, sizeof fl);
				if (error)
					return error;

				fa.sysmsg_result = 0;
				error = fcntl(&fa);
				*retval = fa.sysmsg_result;
				if (error || SCARG(&fa, cmd) != F_GETLK)
					return error;

				error = copyin(flp, &fl, sizeof fl);
				if (error)
					return error;

				bsd_to_svr4_flock64(&fl, &ifl);

				return copyout(&ifl, SCARG(uap, arg),
				    sizeof ifl);
			}

		case SVR4_F_FREESP64:
			{
				struct svr4_flock64	 ifl;
				struct flock		 fl;

				error = copyin(SCARG(uap, arg), &ifl,
				    sizeof ifl);
				if (error)
					return error;
				svr4_to_bsd_flock64(&ifl, &fl);
				return fd_truncate(td, SCARG(uap, fd), &fl, retval);
			}

		case SVR4_F_REVOKE:
			return fd_revoke(td, SCARG(uap, fd));

		default:
			return ENOSYS;
		}

	default:
		return ENOSYS;
	}
}
