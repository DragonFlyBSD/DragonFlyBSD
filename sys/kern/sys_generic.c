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
 *	@(#)sys_generic.c	8.5 (Berkeley) 1/21/94
 * $FreeBSD: src/sys/kern/sys_generic.c,v 1.55.2.10 2001/03/17 10:39:32 peter Exp $
 */

#include "opt_ktrace.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/sysproto.h>
#include <sys/event.h>
#include <sys/filedesc.h>
#include <sys/filio.h>
#include <sys/fcntl.h>
#include <sys/file.h>
#include <sys/proc.h>
#include <sys/signalvar.h>
#include <sys/socketvar.h>
#include <sys/uio.h>
#include <sys/kernel.h>
#include <sys/kern_syscall.h>
#include <sys/malloc.h>
#include <sys/mapped_ioctl.h>
#include <sys/poll.h>
#include <sys/queue.h>
#include <sys/resourcevar.h>
#include <sys/socketops.h>
#include <sys/sysctl.h>
#include <sys/sysent.h>
#include <sys/buf.h>
#ifdef KTRACE
#include <sys/ktrace.h>
#endif
#include <vm/vm.h>
#include <vm/vm_page.h>

#include <sys/file2.h>
#include <sys/mplock2.h>
#include <sys/spinlock2.h>

#include <machine/limits.h>

static MALLOC_DEFINE(M_IOCTLOPS, "ioctlops", "ioctl data buffer");
static MALLOC_DEFINE(M_IOCTLMAP, "ioctlmap", "mapped ioctl handler buffer");
static MALLOC_DEFINE(M_SELECT, "select", "select() buffer");
MALLOC_DEFINE(M_IOV, "iov", "large iov's");

typedef struct kfd_set {
        fd_mask	fds_bits[2];
} kfd_set;

enum select_copyin_states {
    COPYIN_READ, COPYIN_WRITE, COPYIN_EXCEPT, COPYIN_DONE };

struct select_kevent_copyin_args {
	kfd_set		*read_set;
	kfd_set		*write_set;
	kfd_set		*except_set;
	int		active_set;	/* One of select_copyin_states */
	struct lwp	*lwp;		/* Pointer to our lwp */
	int		num_fds;	/* Number of file descriptors (syscall arg) */
	int		proc_fds;	/* Processed fd's (wraps) */
	int		error;		/* Returned to userland */
};

struct poll_kevent_copyin_args {
	struct lwp	*lwp;
	struct pollfd	*fds;
	int		nfds;
	int		pfds;
	int		error;
};

static struct lwkt_token mioctl_token = LWKT_TOKEN_INITIALIZER(mioctl_token);

static int 	doselect(int nd, fd_set *in, fd_set *ou, fd_set *ex,
			 struct timespec *ts, int *res);
static int	dopoll(int nfds, struct pollfd *fds, struct timespec *ts,
		       int *res);
static int	dofileread(int, struct file *, struct uio *, int, size_t *);
static int	dofilewrite(int, struct file *, struct uio *, int, size_t *);

/*
 * Read system call.
 *
 * MPSAFE
 */
int
sys_read(struct read_args *uap)
{
	struct thread *td = curthread;
	struct uio auio;
	struct iovec aiov;
	int error;

	if ((ssize_t)uap->nbyte < 0)
		error = EINVAL;

	aiov.iov_base = uap->buf;
	aiov.iov_len = uap->nbyte;
	auio.uio_iov = &aiov;
	auio.uio_iovcnt = 1;
	auio.uio_offset = -1;
	auio.uio_resid = uap->nbyte;
	auio.uio_rw = UIO_READ;
	auio.uio_segflg = UIO_USERSPACE;
	auio.uio_td = td;

	error = kern_preadv(uap->fd, &auio, 0, &uap->sysmsg_szresult);
	return(error);
}

/*
 * Positioned (Pread) read system call
 *
 * MPSAFE
 */
int
sys_extpread(struct extpread_args *uap)
{
	struct thread *td = curthread;
	struct uio auio;
	struct iovec aiov;
	int error;
	int flags;

	if ((ssize_t)uap->nbyte < 0)
		return(EINVAL);

	aiov.iov_base = uap->buf;
	aiov.iov_len = uap->nbyte;
	auio.uio_iov = &aiov;
	auio.uio_iovcnt = 1;
	auio.uio_offset = uap->offset;
	auio.uio_resid = uap->nbyte;
	auio.uio_rw = UIO_READ;
	auio.uio_segflg = UIO_USERSPACE;
	auio.uio_td = td;

	flags = uap->flags & O_FMASK;
	if (uap->offset != (off_t)-1)
		flags |= O_FOFFSET;

	error = kern_preadv(uap->fd, &auio, flags, &uap->sysmsg_szresult);
	return(error);
}

/*
 * Scatter read system call.
 *
 * MPSAFE
 */
int
sys_readv(struct readv_args *uap)
{
	struct thread *td = curthread;
	struct uio auio;
	struct iovec aiov[UIO_SMALLIOV], *iov = NULL;
	int error;

	error = iovec_copyin(uap->iovp, &iov, aiov, uap->iovcnt,
			     &auio.uio_resid);
	if (error)
		return (error);
	auio.uio_iov = iov;
	auio.uio_iovcnt = uap->iovcnt;
	auio.uio_offset = -1;
	auio.uio_rw = UIO_READ;
	auio.uio_segflg = UIO_USERSPACE;
	auio.uio_td = td;

	error = kern_preadv(uap->fd, &auio, 0, &uap->sysmsg_szresult);

	iovec_free(&iov, aiov);
	return (error);
}


/*
 * Scatter positioned read system call.
 *
 * MPSAFE
 */
int
sys_extpreadv(struct extpreadv_args *uap)
{
	struct thread *td = curthread;
	struct uio auio;
	struct iovec aiov[UIO_SMALLIOV], *iov = NULL;
	int error;
	int flags;

	error = iovec_copyin(uap->iovp, &iov, aiov, uap->iovcnt,
			     &auio.uio_resid);
	if (error)
		return (error);
	auio.uio_iov = iov;
	auio.uio_iovcnt = uap->iovcnt;
	auio.uio_offset = uap->offset;
	auio.uio_rw = UIO_READ;
	auio.uio_segflg = UIO_USERSPACE;
	auio.uio_td = td;

	flags = uap->flags & O_FMASK;
	if (uap->offset != (off_t)-1)
		flags |= O_FOFFSET;

	error = kern_preadv(uap->fd, &auio, flags, &uap->sysmsg_szresult);

	iovec_free(&iov, aiov);
	return(error);
}

/*
 * MPSAFE
 */
int
kern_preadv(int fd, struct uio *auio, int flags, size_t *res)
{
	struct thread *td = curthread;
	struct proc *p = td->td_proc;
	struct file *fp;
	int error;

	KKASSERT(p);

	fp = holdfp(p->p_fd, fd, FREAD);
	if (fp == NULL)
		return (EBADF);
	if (flags & O_FOFFSET && fp->f_type != DTYPE_VNODE) {
		error = ESPIPE;
	} else {
		error = dofileread(fd, fp, auio, flags, res);
	}
	fdrop(fp);
	return(error);
}

/*
 * Common code for readv and preadv that reads data in
 * from a file using the passed in uio, offset, and flags.
 *
 * MPALMOSTSAFE - ktrace needs help
 */
static int
dofileread(int fd, struct file *fp, struct uio *auio, int flags, size_t *res)
{
	int error;
	size_t len;
#ifdef KTRACE
	struct thread *td = curthread;
	struct iovec *ktriov = NULL;
	struct uio ktruio;
#endif

#ifdef KTRACE
	/*
	 * if tracing, save a copy of iovec
	 */
	if (KTRPOINT(td, KTR_GENIO))  {
		int iovlen = auio->uio_iovcnt * sizeof(struct iovec);

		ktriov = kmalloc(iovlen, M_TEMP, M_WAITOK);
		bcopy((caddr_t)auio->uio_iov, (caddr_t)ktriov, iovlen);
		ktruio = *auio;
	}
#endif
	len = auio->uio_resid;
	error = fo_read(fp, auio, fp->f_cred, flags);
	if (error) {
		if (auio->uio_resid != len && (error == ERESTART ||
		    error == EINTR || error == EWOULDBLOCK))
			error = 0;
	}
#ifdef KTRACE
	if (ktriov != NULL) {
		if (error == 0) {
			ktruio.uio_iov = ktriov;
			ktruio.uio_resid = len - auio->uio_resid;
			get_mplock();
			ktrgenio(td->td_lwp, fd, UIO_READ, &ktruio, error);
			rel_mplock();
		}
		kfree(ktriov, M_TEMP);
	}
#endif
	if (error == 0)
		*res = len - auio->uio_resid;

	return(error);
}

/*
 * Write system call
 *
 * MPSAFE
 */
int
sys_write(struct write_args *uap)
{
	struct thread *td = curthread;
	struct uio auio;
	struct iovec aiov;
	int error;

	if ((ssize_t)uap->nbyte < 0)
		error = EINVAL;

	aiov.iov_base = (void *)(uintptr_t)uap->buf;
	aiov.iov_len = uap->nbyte;
	auio.uio_iov = &aiov;
	auio.uio_iovcnt = 1;
	auio.uio_offset = -1;
	auio.uio_resid = uap->nbyte;
	auio.uio_rw = UIO_WRITE;
	auio.uio_segflg = UIO_USERSPACE;
	auio.uio_td = td;

	error = kern_pwritev(uap->fd, &auio, 0, &uap->sysmsg_szresult);

	return(error);
}

/*
 * Pwrite system call
 *
 * MPSAFE
 */
int
sys_extpwrite(struct extpwrite_args *uap)
{
	struct thread *td = curthread;
	struct uio auio;
	struct iovec aiov;
	int error;
	int flags;

	if ((ssize_t)uap->nbyte < 0)
		error = EINVAL;

	aiov.iov_base = (void *)(uintptr_t)uap->buf;
	aiov.iov_len = uap->nbyte;
	auio.uio_iov = &aiov;
	auio.uio_iovcnt = 1;
	auio.uio_offset = uap->offset;
	auio.uio_resid = uap->nbyte;
	auio.uio_rw = UIO_WRITE;
	auio.uio_segflg = UIO_USERSPACE;
	auio.uio_td = td;

	flags = uap->flags & O_FMASK;
	if (uap->offset != (off_t)-1)
		flags |= O_FOFFSET;
	error = kern_pwritev(uap->fd, &auio, flags, &uap->sysmsg_szresult);
	return(error);
}

/*
 * MPSAFE
 */
int
sys_writev(struct writev_args *uap)
{
	struct thread *td = curthread;
	struct uio auio;
	struct iovec aiov[UIO_SMALLIOV], *iov = NULL;
	int error;

	error = iovec_copyin(uap->iovp, &iov, aiov, uap->iovcnt,
			     &auio.uio_resid);
	if (error)
		return (error);
	auio.uio_iov = iov;
	auio.uio_iovcnt = uap->iovcnt;
	auio.uio_offset = -1;
	auio.uio_rw = UIO_WRITE;
	auio.uio_segflg = UIO_USERSPACE;
	auio.uio_td = td;

	error = kern_pwritev(uap->fd, &auio, 0, &uap->sysmsg_szresult);

	iovec_free(&iov, aiov);
	return (error);
}


/*
 * Gather positioned write system call
 *
 * MPSAFE
 */
int
sys_extpwritev(struct extpwritev_args *uap)
{
	struct thread *td = curthread;
	struct uio auio;
	struct iovec aiov[UIO_SMALLIOV], *iov = NULL;
	int error;
	int flags;

	error = iovec_copyin(uap->iovp, &iov, aiov, uap->iovcnt,
			     &auio.uio_resid);
	if (error)
		return (error);
	auio.uio_iov = iov;
	auio.uio_iovcnt = uap->iovcnt;
	auio.uio_offset = uap->offset;
	auio.uio_rw = UIO_WRITE;
	auio.uio_segflg = UIO_USERSPACE;
	auio.uio_td = td;

	flags = uap->flags & O_FMASK;
	if (uap->offset != (off_t)-1)
		flags |= O_FOFFSET;

	error = kern_pwritev(uap->fd, &auio, flags, &uap->sysmsg_szresult);

	iovec_free(&iov, aiov);
	return(error);
}

/*
 * MPSAFE
 */
int
kern_pwritev(int fd, struct uio *auio, int flags, size_t *res)
{
	struct thread *td = curthread;
	struct proc *p = td->td_proc;
	struct file *fp;
	int error;

	KKASSERT(p);

	fp = holdfp(p->p_fd, fd, FWRITE);
	if (fp == NULL)
		return (EBADF);
	else if ((flags & O_FOFFSET) && fp->f_type != DTYPE_VNODE) {
		error = ESPIPE;
	} else {
		error = dofilewrite(fd, fp, auio, flags, res);
	}
	
	fdrop(fp);
	return (error);
}

/*
 * Common code for writev and pwritev that writes data to
 * a file using the passed in uio, offset, and flags.
 *
 * MPALMOSTSAFE - ktrace needs help
 */
static int
dofilewrite(int fd, struct file *fp, struct uio *auio, int flags, size_t *res)
{	
	struct thread *td = curthread;
	struct lwp *lp = td->td_lwp;
	int error;
	size_t len;
#ifdef KTRACE
	struct iovec *ktriov = NULL;
	struct uio ktruio;
#endif

#ifdef KTRACE
	/*
	 * if tracing, save a copy of iovec and uio
	 */
	if (KTRPOINT(td, KTR_GENIO))  {
		int iovlen = auio->uio_iovcnt * sizeof(struct iovec);

		ktriov = kmalloc(iovlen, M_TEMP, M_WAITOK);
		bcopy((caddr_t)auio->uio_iov, (caddr_t)ktriov, iovlen);
		ktruio = *auio;
	}
#endif
	len = auio->uio_resid;
	error = fo_write(fp, auio, fp->f_cred, flags);
	if (error) {
		if (auio->uio_resid != len && (error == ERESTART ||
		    error == EINTR || error == EWOULDBLOCK))
			error = 0;
		/* Socket layer is responsible for issuing SIGPIPE. */
		if (error == EPIPE && fp->f_type != DTYPE_SOCKET)
			lwpsignal(lp->lwp_proc, lp, SIGPIPE);
	}
#ifdef KTRACE
	if (ktriov != NULL) {
		if (error == 0) {
			ktruio.uio_iov = ktriov;
			ktruio.uio_resid = len - auio->uio_resid;
			get_mplock();
			ktrgenio(lp, fd, UIO_WRITE, &ktruio, error);
			rel_mplock();
		}
		kfree(ktriov, M_TEMP);
	}
#endif
	if (error == 0)
		*res = len - auio->uio_resid;

	return(error);
}

/*
 * Ioctl system call
 *
 * MPSAFE
 */
int
sys_ioctl(struct ioctl_args *uap)
{
	int error;

	error = mapped_ioctl(uap->fd, uap->com, uap->data, NULL, &uap->sysmsg);
	return (error);
}

struct ioctl_map_entry {
	const char *subsys;
	struct ioctl_map_range *cmd_ranges;
	LIST_ENTRY(ioctl_map_entry) entries;
};

/*
 * The true heart of all ioctl syscall handlers (native, emulation).
 * If map != NULL, it will be searched for a matching entry for com,
 * and appropriate conversions/conversion functions will be utilized.
 *
 * MPSAFE
 */
int
mapped_ioctl(int fd, u_long com, caddr_t uspc_data, struct ioctl_map *map,
	     struct sysmsg *msg)
{
	struct thread *td = curthread;
	struct proc *p = td->td_proc;
	struct ucred *cred;
	struct file *fp;
	struct ioctl_map_range *iomc = NULL;
	int error;
	u_int size;
	u_long ocom = com;
	caddr_t data, memp;
	int tmp;
#define STK_PARAMS	128
	union {
	    char stkbuf[STK_PARAMS];
	    long align;
	} ubuf;

	KKASSERT(p);
	cred = td->td_ucred;
	memp = NULL;

	fp = holdfp(p->p_fd, fd, FREAD|FWRITE);
	if (fp == NULL)
		return(EBADF);

	if (map != NULL) {	/* obey translation map */
		u_long maskcmd;
		struct ioctl_map_entry *e;

		maskcmd = com & map->mask;

		lwkt_gettoken(&mioctl_token);
		LIST_FOREACH(e, &map->mapping, entries) {
			for (iomc = e->cmd_ranges; iomc->start != 0 ||
			     iomc->maptocmd != 0 || iomc->wrapfunc != NULL ||
			     iomc->mapfunc != NULL;
			     iomc++) {
				if (maskcmd >= iomc->start &&
				    maskcmd <= iomc->end)
					break;
			}

			/* Did we find a match? */
			if (iomc->start != 0 || iomc->maptocmd != 0 ||
			    iomc->wrapfunc != NULL || iomc->mapfunc != NULL)
				break;
		}
		lwkt_reltoken(&mioctl_token);

		if (iomc == NULL ||
		    (iomc->start == 0 && iomc->maptocmd == 0
		     && iomc->wrapfunc == NULL && iomc->mapfunc == NULL)) {
			kprintf("%s: 'ioctl' fd=%d, cmd=0x%lx ('%c',%d) not implemented\n",
			       map->sys, fd, maskcmd,
			       (int)((maskcmd >> 8) & 0xff),
			       (int)(maskcmd & 0xff));
			error = EINVAL;
			goto done;
		}

		/*
		 * If it's a non-range one to one mapping, maptocmd should be
		 * correct. If it's a ranged one to one mapping, we pass the
		 * original value of com, and for a range mapped to a different
		 * range, we always need a mapping function to translate the
		 * ioctl to our native ioctl. Ex. 6500-65ff <-> 9500-95ff
		 */
		if (iomc->start == iomc->end && iomc->maptocmd == iomc->maptoend) {
			com = iomc->maptocmd;
		} else if (iomc->start == iomc->maptocmd && iomc->end == iomc->maptoend) {
			if (iomc->mapfunc != NULL)
				com = iomc->mapfunc(iomc->start, iomc->end,
						    iomc->start, iomc->end,
						    com, com);
		} else {
			if (iomc->mapfunc != NULL) {
				com = iomc->mapfunc(iomc->start, iomc->end,
						    iomc->maptocmd, iomc->maptoend,
						    com, ocom);
			} else {
				kprintf("%s: Invalid mapping for fd=%d, cmd=%#lx ('%c',%d)\n",
				       map->sys, fd, maskcmd,
				       (int)((maskcmd >> 8) & 0xff),
				       (int)(maskcmd & 0xff));
				error = EINVAL;
				goto done;
			}
		}
	}

	switch (com) {
	case FIONCLEX:
		error = fclrfdflags(p->p_fd, fd, UF_EXCLOSE);
		goto done;
	case FIOCLEX:
		error = fsetfdflags(p->p_fd, fd, UF_EXCLOSE);
		goto done;
	}

	/*
	 * Interpret high order word to find amount of data to be
	 * copied to/from the user's address space.
	 */
	size = IOCPARM_LEN(com);
	if (size > IOCPARM_MAX) {
		error = ENOTTY;
		goto done;
	}

	if ((com & IOC_VOID) == 0 && size > sizeof(ubuf.stkbuf)) {
		memp = kmalloc(size, M_IOCTLOPS, M_WAITOK);
		data = memp;
	} else {
		memp = NULL;
		data = ubuf.stkbuf;
	}
	if (com & IOC_VOID) {
		*(caddr_t *)data = uspc_data;
	} else if (com & IOC_IN) {
		if (size != 0) {
			error = copyin(uspc_data, data, (size_t)size);
			if (error)
				goto done;
		} else {
			*(caddr_t *)data = uspc_data;
		}
	} else if ((com & IOC_OUT) != 0 && size) {
		/*
		 * Zero the buffer so the user always
		 * gets back something deterministic.
		 */
		bzero(data, (size_t)size);
	}

	switch (com) {
	case FIONBIO:
		if ((tmp = *(int *)data))
			atomic_set_int(&fp->f_flag, FNONBLOCK);
		else
			atomic_clear_int(&fp->f_flag, FNONBLOCK);
		error = 0;
		break;

	case FIOASYNC:
		if ((tmp = *(int *)data))
			atomic_set_int(&fp->f_flag, FASYNC);
		else
			atomic_clear_int(&fp->f_flag, FASYNC);
		error = fo_ioctl(fp, FIOASYNC, (caddr_t)&tmp, cred, msg);
		break;

	default:
		/*
		 *  If there is a override function,
		 *  call it instead of directly routing the call
		 */
		if (map != NULL && iomc->wrapfunc != NULL)
			error = iomc->wrapfunc(fp, com, ocom, data, cred);
		else
			error = fo_ioctl(fp, com, data, cred, msg);
		/*
		 * Copy any data to user, size was
		 * already set and checked above.
		 */
		if (error == 0 && (com & IOC_OUT) != 0 && size != 0)
			error = copyout(data, uspc_data, (size_t)size);
		break;
	}
done:
	if (memp != NULL)
		kfree(memp, M_IOCTLOPS);
	fdrop(fp);
	return(error);
}

/*
 * MPSAFE
 */
int
mapped_ioctl_register_handler(struct ioctl_map_handler *he)
{
	struct ioctl_map_entry *ne;

	KKASSERT(he != NULL && he->map != NULL && he->cmd_ranges != NULL &&
		 he->subsys != NULL && *he->subsys != '\0');

	ne = kmalloc(sizeof(struct ioctl_map_entry), M_IOCTLMAP,
		     M_WAITOK | M_ZERO);

	ne->subsys = he->subsys;
	ne->cmd_ranges = he->cmd_ranges;

	lwkt_gettoken(&mioctl_token);
	LIST_INSERT_HEAD(&he->map->mapping, ne, entries);
	lwkt_reltoken(&mioctl_token);

	return(0);
}

/*
 * MPSAFE
 */
int
mapped_ioctl_unregister_handler(struct ioctl_map_handler *he)
{
	struct ioctl_map_entry *ne;
	int error = EINVAL;

	KKASSERT(he != NULL && he->map != NULL && he->cmd_ranges != NULL);

	lwkt_gettoken(&mioctl_token);
	LIST_FOREACH(ne, &he->map->mapping, entries) {
		if (ne->cmd_ranges == he->cmd_ranges) {
			LIST_REMOVE(ne, entries);
			kfree(ne, M_IOCTLMAP);
			error = 0;
			break;
		}
	}
	lwkt_reltoken(&mioctl_token);
	return(error);
}

static int	nselcoll;	/* Select collisions since boot */
int	selwait;
SYSCTL_INT(_kern, OID_AUTO, nselcoll, CTLFLAG_RD, &nselcoll, 0, "");
static int	nseldebug;
SYSCTL_INT(_kern, OID_AUTO, nseldebug, CTLFLAG_RW, &nseldebug, 0, "");

/*
 * Select system call.
 *
 * MPSAFE
 */
int
sys_select(struct select_args *uap)
{
	struct timeval ktv;
	struct timespec *ktsp, kts;
	int error;

	/*
	 * Get timeout if any.
	 */
	if (uap->tv != NULL) {
		error = copyin(uap->tv, &ktv, sizeof (ktv));
		if (error)
			return (error);
		TIMEVAL_TO_TIMESPEC(&ktv, &kts);
		ktsp = &kts;
	} else {
		ktsp = NULL;
	}

	/*
	 * Do real work.
	 */
	error = doselect(uap->nd, uap->in, uap->ou, uap->ex, ktsp,
			 &uap->sysmsg_result);

	return (error);
}


/*
 * Pselect system call.
 */
int
sys_pselect(struct pselect_args *uap)
{
	struct thread *td = curthread;
	struct lwp *lp = td->td_lwp;
	struct timespec *ktsp, kts;
	sigset_t sigmask;
	int error;

	/*
	 * Get timeout if any.
	 */
	if (uap->ts != NULL) {
		error = copyin(uap->ts, &kts, sizeof (kts));
		if (error)
			return (error);
		ktsp = &kts;
	} else {
		ktsp = NULL;
	}

	/*
	 * Install temporary signal mask if any provided.
	 */
	if (uap->sigmask != NULL) {
		error = copyin(uap->sigmask, &sigmask, sizeof(sigmask));
		if (error)
			return (error);
		lwkt_gettoken(&lp->lwp_proc->p_token);
		lp->lwp_oldsigmask = lp->lwp_sigmask;
		SIG_CANTMASK(sigmask);
		lp->lwp_sigmask = sigmask;
		lwkt_reltoken(&lp->lwp_proc->p_token);
	}

	/*
	 * Do real job.
	 */
	error = doselect(uap->nd, uap->in, uap->ou, uap->ex, ktsp,
			 &uap->sysmsg_result);

	if (uap->sigmask != NULL) {
		lwkt_gettoken(&lp->lwp_proc->p_token);
		/* doselect() responsible for turning ERESTART into EINTR */
		KKASSERT(error != ERESTART);
		if (error == EINTR) {
			/*
			 * We can't restore the previous signal mask now
			 * because it could block the signal that interrupted
			 * us.  So make a note to restore it after executing
			 * the handler.
			 */
			lp->lwp_flags |= LWP_OLDMASK;
		} else {
			/*
			 * No handler to run. Restore previous mask immediately.
			 */
			lp->lwp_sigmask = lp->lwp_oldsigmask;
		}
		lwkt_reltoken(&lp->lwp_proc->p_token);
	}

	return (error);
}

static int
select_copyin(void *arg, struct kevent *kevp, int maxevents, int *events)
{
	struct select_kevent_copyin_args *skap = NULL;
	struct kevent *kev;
	int fd;
	kfd_set *fdp = NULL;
	short filter = 0;
	u_int fflags = 0;

	skap = (struct select_kevent_copyin_args *)arg;

	if (*events == maxevents)
		return (0);

	while (skap->active_set < COPYIN_DONE) {
		switch (skap->active_set) {
		case COPYIN_READ:
			/*
			 * Register descriptors for the read filter
			 */
			fdp = skap->read_set;
			filter = EVFILT_READ;
			fflags = NOTE_OLDAPI;
			if (fdp)
				break;
			++skap->active_set;
			skap->proc_fds = 0;
			/* fall through */
		case COPYIN_WRITE:
			/*
			 * Register descriptors for the write filter
			 */
			fdp = skap->write_set;
			filter = EVFILT_WRITE;
			fflags = NOTE_OLDAPI;
			if (fdp)
				break;
			++skap->active_set;
			skap->proc_fds = 0;
			/* fall through */
		case COPYIN_EXCEPT:
			/*
			 * Register descriptors for the exception filter
			 */
			fdp = skap->except_set;
			filter = EVFILT_EXCEPT;
			fflags = NOTE_OLDAPI | NOTE_OOB;
			if (fdp)
				break;
			++skap->active_set;
			skap->proc_fds = 0;
			/* fall through */
		case COPYIN_DONE:
			/*
			 * Nothing left to register
			 */
			return(0);
			/* NOT REACHED */
		}

		while (skap->proc_fds < skap->num_fds) {
			fd = skap->proc_fds;
			if (FD_ISSET(fd, fdp)) {
				kev = &kevp[*events];
				EV_SET(kev, fd, filter,
				       EV_ADD|EV_ENABLE,
				       fflags, 0,
				       (void *)(uintptr_t)
					skap->lwp->lwp_kqueue_serial);
				FD_CLR(fd, fdp);
				++*events;

				if (nseldebug)
					kprintf("select fd %d filter %d serial %d\n",
						fd, filter, skap->lwp->lwp_kqueue_serial);
			}
			++skap->proc_fds;
			if (*events == maxevents)
				return (0);
		}
		skap->active_set++;
		skap->proc_fds = 0;
	}

	return (0);
}

static int
select_copyout(void *arg, struct kevent *kevp, int count, int *res)
{
	struct select_kevent_copyin_args *skap;
	struct kevent kev;
	int i = 0;

	skap = (struct select_kevent_copyin_args *)arg;

	for (i = 0; i < count; ++i) {
		/*
		 * Filter out and delete spurious events
		 */
		if ((u_int)(uintptr_t)kevp[i].udata !=
		    skap->lwp->lwp_kqueue_serial) {
			kev = kevp[i];
			kev.flags = EV_DISABLE|EV_DELETE;
			kqueue_register(&skap->lwp->lwp_kqueue, &kev);
			if (nseldebug)
				kprintf("select fd %ju mismatched serial %d\n",
					(uintmax_t)kevp[i].ident,
					skap->lwp->lwp_kqueue_serial);
			continue;
		}

		/*
		 * Handle errors
		 */
		if (kevp[i].flags & EV_ERROR) {
			int error = kevp[i].data;

			switch (error) {
			case EBADF:
				/*
				 * A bad file descriptor is considered a
				 * fatal error for select, bail out.
				 */
				skap->error = error;
				*res = -1;
				return error;

			default:
				/*
				 * Select silently swallows any unknown errors
				 * for descriptors in the read or write sets.
				 *
				 * ALWAYS filter out EOPNOTSUPP errors from
				 * filters (at least until all filters support
				 * EVFILT_EXCEPT)
				 *
				 * We also filter out ENODEV since dev_dkqfilter
				 * returns ENODEV if EOPNOTSUPP is returned in an
				 * inner call.
				 *
				 * XXX: fix this
				 */
				if (kevp[i].filter != EVFILT_READ &&
				    kevp[i].filter != EVFILT_WRITE &&
				    error != EOPNOTSUPP &&
				    error != ENODEV) {
					skap->error = error;
					*res = -1;
					return error;
				}
				break;
			}
			if (nseldebug)
				kprintf("select fd %ju filter %d error %d\n",
					(uintmax_t)kevp[i].ident,
					kevp[i].filter, error);
			continue;
		}

		switch (kevp[i].filter) {
		case EVFILT_READ:
			FD_SET(kevp[i].ident, skap->read_set);
			break;
		case EVFILT_WRITE:
			FD_SET(kevp[i].ident, skap->write_set);
			break;
		case EVFILT_EXCEPT:
			FD_SET(kevp[i].ident, skap->except_set);
			break;
		}

		++*res;
	}

	return (0);
}

/*
 * Copy select bits in from userland.  Allocate kernel memory if the
 * set is large.
 */
static int
getbits(int bytes, fd_set *in_set, kfd_set **out_set, kfd_set *tmp_set)
{
	int error;

	if (in_set) {
		if (bytes < sizeof(*tmp_set))
			*out_set = tmp_set;
		else
			*out_set = kmalloc(bytes, M_SELECT, M_WAITOK);
		error = copyin(in_set, *out_set, bytes);
	} else {
		*out_set = NULL;
		error = 0;
	}
	return (error);
}

/*
 * Copy returned select bits back out to userland.
 */
static int
putbits(int bytes, kfd_set *in_set, fd_set *out_set)
{
	int error;

	if (in_set) {
		error = copyout(in_set, out_set, bytes);
	} else {
		error = 0;
	}
	return (error);
}

static int
dotimeout_only(struct timespec *ts)
{
	return(nanosleep1(ts, NULL));
}

/*
 * Common code for sys_select() and sys_pselect().
 *
 * in, out and ex are userland pointers.  ts must point to validated
 * kernel-side timeout value or NULL for infinite timeout.  res must
 * point to syscall return value.
 */
static int
doselect(int nd, fd_set *read, fd_set *write, fd_set *except,
	 struct timespec *ts, int *res)
{
	struct proc *p = curproc;
	struct select_kevent_copyin_args *kap, ka;
	int bytes, error;
	kfd_set read_tmp;
	kfd_set write_tmp;
	kfd_set except_tmp;

	*res = 0;
	if (nd < 0)
		return (EINVAL);
	if (nd == 0 && ts)
		return (dotimeout_only(ts));

	if (nd > p->p_fd->fd_nfiles)		/* limit kmalloc */
		nd = p->p_fd->fd_nfiles;

	kap = &ka;
	kap->lwp = curthread->td_lwp;
	kap->num_fds = nd;
	kap->proc_fds = 0;
	kap->error = 0;
	kap->active_set = COPYIN_READ;

	/*
	 * Calculate bytes based on the number of __fd_mask[] array entries
	 * multiplied by the size of __fd_mask.
	 */
	bytes = howmany(nd, __NFDBITS) * sizeof(__fd_mask);

	/* kap->read_set = NULL; not needed */
	kap->write_set = NULL;
	kap->except_set = NULL;

	error = getbits(bytes, read, &kap->read_set, &read_tmp);
	if (error == 0)
		error = getbits(bytes, write, &kap->write_set, &write_tmp);
	if (error == 0)
		error = getbits(bytes, except, &kap->except_set, &except_tmp);
	if (error)
		goto done;

	/*
	 * NOTE: Make sure the max events passed to kern_kevent() is
	 *	 effectively unlimited.  (nd * 3) accomplishes this.
	 *
	 *	 (*res) continues to increment as returned events are
	 *	 loaded in.
	 */
	error = kern_kevent(&kap->lwp->lwp_kqueue, 0x7FFFFFFF, res, kap,
			    select_copyin, select_copyout, ts);
	if (error == 0)
		error = putbits(bytes, kap->read_set, read);
	if (error == 0)
		error = putbits(bytes, kap->write_set, write);
	if (error == 0)
		error = putbits(bytes, kap->except_set, except);

	/*
	 * An error from an individual event that should be passed
	 * back to userland (EBADF)
	 */
	if (kap->error)
		error = kap->error;

	/*
	 * Clean up.
	 */
done:
	if (kap->read_set && kap->read_set != &read_tmp)
		kfree(kap->read_set, M_SELECT);
	if (kap->write_set && kap->write_set != &write_tmp)
		kfree(kap->write_set, M_SELECT);
	if (kap->except_set && kap->except_set != &except_tmp)
		kfree(kap->except_set, M_SELECT);

	kap->lwp->lwp_kqueue_serial += kap->num_fds;

	return (error);
}

/*
 * Poll system call.
 *
 * MPSAFE
 */
int
sys_poll(struct poll_args *uap)
{
	struct timespec ts, *tsp;
	int error;

	if (uap->timeout != INFTIM) {
		ts.tv_sec = uap->timeout / 1000;
		ts.tv_nsec = (uap->timeout % 1000) * 1000 * 1000;
		tsp = &ts;
	} else {
		tsp = NULL;
	}

	error = dopoll(uap->nfds, uap->fds, tsp, &uap->sysmsg_result);

	return (error);
}

static int
poll_copyin(void *arg, struct kevent *kevp, int maxevents, int *events)
{
	struct poll_kevent_copyin_args *pkap;
	struct pollfd *pfd;
	struct kevent *kev;
	int kev_count;

	pkap = (struct poll_kevent_copyin_args *)arg;

	while (pkap->pfds < pkap->nfds) {
		pfd = &pkap->fds[pkap->pfds];

		/* Clear return events */
		pfd->revents = 0;

		/* Do not check if fd is equal to -1 */
		if (pfd->fd == -1) {
			++pkap->pfds;
			continue;
		}

		kev_count = 0;
		if (pfd->events & (POLLIN | POLLRDNORM))
			kev_count++;
		if (pfd->events & (POLLOUT | POLLWRNORM))
			kev_count++;
		if (pfd->events & (POLLPRI | POLLRDBAND))
			kev_count++;

		if (*events + kev_count > maxevents)
			return (0);

		/*
		 * NOTE: A combined serial number and poll array index is
		 * stored in kev->udata.
		 */
		kev = &kevp[*events];
		if (pfd->events & (POLLIN | POLLRDNORM)) {
			EV_SET(kev++, pfd->fd, EVFILT_READ, EV_ADD|EV_ENABLE,
			       NOTE_OLDAPI, 0, (void *)(uintptr_t)
				(pkap->lwp->lwp_kqueue_serial + pkap->pfds));
		}
		if (pfd->events & (POLLOUT | POLLWRNORM)) {
			EV_SET(kev++, pfd->fd, EVFILT_WRITE, EV_ADD|EV_ENABLE,
			       NOTE_OLDAPI, 0, (void *)(uintptr_t)
				(pkap->lwp->lwp_kqueue_serial + pkap->pfds));
		}
		if (pfd->events & (POLLPRI | POLLRDBAND)) {
			EV_SET(kev++, pfd->fd, EVFILT_EXCEPT, EV_ADD|EV_ENABLE,
			       NOTE_OLDAPI | NOTE_OOB, 0,
			       (void *)(uintptr_t)
				(pkap->lwp->lwp_kqueue_serial + pkap->pfds));
		}

		if (nseldebug) {
			kprintf("poll index %d/%d fd %d events %08x serial %d\n",
				pkap->pfds, pkap->nfds-1, pfd->fd, pfd->events,
				pkap->lwp->lwp_kqueue_serial);
		}

		++pkap->pfds;
		(*events) += kev_count;
	}

	return (0);
}

static int
poll_copyout(void *arg, struct kevent *kevp, int count, int *res)
{
	struct poll_kevent_copyin_args *pkap;
	struct pollfd *pfd;
	struct kevent kev;
	int count_res;
	int i;
	u_int pi;

	pkap = (struct poll_kevent_copyin_args *)arg;

	for (i = 0; i < count; ++i) {
		/*
		 * Extract the poll array index and delete spurious events.
		 * We can easily tell if the serial number is incorrect
		 * by checking whether the extracted index is out of range.
		 */
		pi = (u_int)(uintptr_t)kevp[i].udata -
		     (u_int)pkap->lwp->lwp_kqueue_serial;

		if (pi >= pkap->nfds) {
			kev = kevp[i];
			kev.flags = EV_DISABLE|EV_DELETE;
			kqueue_register(&pkap->lwp->lwp_kqueue, &kev);
			if (nseldebug)
				kprintf("poll index %d out of range against serial %d\n",
					pi, pkap->lwp->lwp_kqueue_serial);
			continue;
		}
		pfd = &pkap->fds[pi];
		if (kevp[i].ident == pfd->fd) {
			/*
			 * A single descriptor may generate an error against
			 * more than one filter, make sure to set the
			 * appropriate flags but do not increment (*res)
			 * more than once.
			 */
			count_res = (pfd->revents == 0);
			if (kevp[i].flags & EV_ERROR) {
				switch(kevp[i].data) {
				case EBADF:
				case POLLNVAL:
					/* Bad file descriptor */
					if (count_res)
						++*res;
					pfd->revents |= POLLNVAL;
					break;
				default:
					/*
					 * Poll silently swallows any unknown
					 * errors except in the case of POLLPRI
					 * (OOB/urgent data).
					 *
					 * ALWAYS filter out EOPNOTSUPP errors
					 * from filters, common applications
					 * set POLLPRI|POLLRDBAND and most
					 * filters do not support EVFILT_EXCEPT.
					 *
					 * We also filter out ENODEV since dev_dkqfilter
					 * returns ENODEV if EOPNOTSUPP is returned in an
					 * inner call.
					 *
					 * XXX: fix this
					 */
					if (kevp[i].filter != EVFILT_READ &&
					    kevp[i].filter != EVFILT_WRITE &&
					    kevp[i].data != EOPNOTSUPP &&
					    kevp[i].data != ENODEV) {
						if (count_res == 0)
							++*res;
						pfd->revents |= POLLERR;
					}
					break;
				}
				if (nseldebug) {
					kprintf("poll index %d fd %d "
						"filter %d error %jd\n",
						pi, pfd->fd,
						kevp[i].filter,
						(intmax_t)kevp[i].data);
				}
				continue;
			}

			switch (kevp[i].filter) {
			case EVFILT_READ:
#if 0
				/*
				 * NODATA on the read side can indicate a
				 * half-closed situation and not necessarily
				 * a disconnect, so depend on the user
				 * issuing a read() and getting 0 bytes back.
				 */
				if (kevp[i].flags & EV_NODATA)
					pfd->revents |= POLLHUP;
#endif
				if ((kevp[i].flags & EV_EOF) &&
				    kevp[i].fflags != 0)
					pfd->revents |= POLLERR;
				if (pfd->events & POLLIN)
					pfd->revents |= POLLIN;
				if (pfd->events & POLLRDNORM)
					pfd->revents |= POLLRDNORM;
				break;
			case EVFILT_WRITE:
				/*
				 * As per the OpenGroup POLLHUP is mutually
				 * exclusive with the writability flags.  I
				 * consider this a bit broken but...
				 *
				 * In this case a disconnect is implied even
				 * for a half-closed (write side) situation.
				 */
				if (kevp[i].flags & EV_EOF) {
					pfd->revents |= POLLHUP;
					if (kevp[i].fflags != 0)
						pfd->revents |= POLLERR;
				} else {
					if (pfd->events & POLLOUT)
						pfd->revents |= POLLOUT;
					if (pfd->events & POLLWRNORM)
						pfd->revents |= POLLWRNORM;
				}
				break;
			case EVFILT_EXCEPT:
				/*
				 * EV_NODATA should never be tagged for this
				 * filter.
				 */
				if (pfd->events & POLLPRI)
					pfd->revents |= POLLPRI;
				if (pfd->events & POLLRDBAND)
					pfd->revents |= POLLRDBAND;
				break;
			}

			if (nseldebug) {
				kprintf("poll index %d/%d fd %d revents %08x\n",
					pi, pkap->nfds, pfd->fd, pfd->revents);
			}

			if (count_res && pfd->revents)
				++*res;
		} else {
			if (nseldebug) {
				kprintf("poll index %d mismatch %ju/%d\n",
					pi, (uintmax_t)kevp[i].ident, pfd->fd);
			}
		}
	}

	return (0);
}

static int
dopoll(int nfds, struct pollfd *fds, struct timespec *ts, int *res)
{
	struct poll_kevent_copyin_args ka;
	struct pollfd sfds[64];
	int bytes;
	int error;

        *res = 0;
        if (nfds < 0)
                return (EINVAL);

	if (nfds == 0 && ts)
		return (dotimeout_only(ts));

	/*
	 * This is a bit arbitrary but we need to limit internal kmallocs.
	 */
        if (nfds > maxfilesperproc * 2)
                nfds = maxfilesperproc * 2;
	bytes = sizeof(struct pollfd) * nfds;

	ka.lwp = curthread->td_lwp;
	ka.nfds = nfds;
	ka.pfds = 0;
	ka.error = 0;

	if (ka.nfds < 64)
		ka.fds = sfds;
	else
		ka.fds = kmalloc(bytes, M_SELECT, M_WAITOK);

	error = copyin(fds, ka.fds, bytes);
	if (error == 0)
		error = kern_kevent(&ka.lwp->lwp_kqueue, 0x7FFFFFFF, res, &ka,
				    poll_copyin, poll_copyout, ts);

	if (error == 0)
		error = copyout(ka.fds, fds, bytes);

	if (ka.fds != sfds)
		kfree(ka.fds, M_SELECT);

	ka.lwp->lwp_kqueue_serial += nfds;

	return (error);
}

static int
socket_wait_copyin(void *arg, struct kevent *kevp, int maxevents, int *events)
{
	return (0);
}

static int
socket_wait_copyout(void *arg, struct kevent *kevp, int count, int *res)
{
	++*res;
	return (0);
}

extern	struct fileops socketops;

/*
 * NOTE: Callers of socket_wait() must already have a reference on the
 *	 socket.
 */
int
socket_wait(struct socket *so, struct timespec *ts, int *res)
{
	struct thread *td = curthread;
	struct file *fp;
	struct kqueue kq;
	struct kevent kev;
	int error, fd;

	if ((error = falloc(td->td_lwp, &fp, &fd)) != 0)
		return (error);

	fp->f_type = DTYPE_SOCKET;
	fp->f_flag = FREAD | FWRITE;
	fp->f_ops = &socketops;
	fp->f_data = so;
	fsetfd(td->td_lwp->lwp_proc->p_fd, fp, fd);

	kqueue_init(&kq, td->td_lwp->lwp_proc->p_fd);
	EV_SET(&kev, fd, EVFILT_READ, EV_ADD|EV_ENABLE, 0, 0, NULL);
	if ((error = kqueue_register(&kq, &kev)) != 0) {
		fdrop(fp);
		return (error);
	}

	error = kern_kevent(&kq, 1, res, NULL, socket_wait_copyin,
			    socket_wait_copyout, ts);

	EV_SET(&kev, fd, EVFILT_READ, EV_DELETE, 0, 0, NULL);
	kqueue_register(&kq, &kev);
	fp->f_ops = &badfileops;
	fdrop(fp);

	return (error);
}

/*
 * OpenBSD poll system call.
 * XXX this isn't quite a true representation..  OpenBSD uses select ops.
 *
 * MPSAFE
 */
int
sys_openbsd_poll(struct openbsd_poll_args *uap)
{
	return (sys_poll((struct poll_args *)uap));
}

/*ARGSUSED*/
int
seltrue(cdev_t dev, int events)
{
	return (events & (POLLIN | POLLOUT | POLLRDNORM | POLLWRNORM));
}
