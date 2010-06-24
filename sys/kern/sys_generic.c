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
 *	@(#)sys_generic.c	8.5 (Berkeley) 1/21/94
 * $FreeBSD: src/sys/kern/sys_generic.c,v 1.55.2.10 2001/03/17 10:39:32 peter Exp $
 * $DragonFly: src/sys/kern/sys_generic.c,v 1.49 2008/05/05 22:09:44 dillon Exp $
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

static int 	doselect(int nd, fd_set *in, fd_set *ou, fd_set *ex,
			 struct timespec *ts, int *res);
static int	pollscan (struct proc *, struct pollfd *, u_int, int *);
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

		MALLOC(ktriov, struct iovec *, iovlen, M_TEMP, M_WAITOK);
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
		FREE(ktriov, M_TEMP);
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

		MALLOC(ktriov, struct iovec *, iovlen, M_TEMP, M_WAITOK);
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
		if (error == EPIPE) {
			get_mplock();
			lwpsignal(lp->lwp_proc, lp, SIGPIPE);
			rel_mplock();
		}
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
		FREE(ktriov, M_TEMP);
	}
#endif
	if (error == 0)
		*res = len - auio->uio_resid;

	return(error);
}

/*
 * Ioctl system call
 *
 * MPALMOSTSAFE
 */
int
sys_ioctl(struct ioctl_args *uap)
{
	int error;

	get_mplock();
	error = mapped_ioctl(uap->fd, uap->com, uap->data, NULL, &uap->sysmsg);
	rel_mplock();
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

	fp = holdfp(p->p_fd, fd, FREAD|FWRITE);
	if (fp == NULL)
		return(EBADF);

	if (map != NULL) {	/* obey translation map */
		u_long maskcmd;
		struct ioctl_map_entry *e;

		maskcmd = com & map->mask;

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

	memp = NULL;
	if (size > sizeof (ubuf.stkbuf)) {
		memp = kmalloc(size, M_IOCTLOPS, M_WAITOK);
		data = memp;
	} else {
		data = ubuf.stkbuf;
	}
	if ((com & IOC_IN) != 0) {
		if (size != 0) {
			error = copyin(uspc_data, data, (size_t)size);
			if (error) {
				if (memp != NULL)
					kfree(memp, M_IOCTLOPS);
				goto done;
			}
		} else {
			*(caddr_t *)data = uspc_data;
		}
	} else if ((com & IOC_OUT) != 0 && size) {
		/*
		 * Zero the buffer so the user always
		 * gets back something deterministic.
		 */
		bzero(data, (size_t)size);
	} else if ((com & IOC_VOID) != 0) {
		*(caddr_t *)data = uspc_data;
	}

	switch (com) {
	case FIONBIO:
		if ((tmp = *(int *)data))
			fp->f_flag |= FNONBLOCK;
		else
			fp->f_flag &= ~FNONBLOCK;
		error = 0;
		break;

	case FIOASYNC:
		if ((tmp = *(int *)data))
			fp->f_flag |= FASYNC;
		else
			fp->f_flag &= ~FASYNC;
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
	if (memp != NULL)
		kfree(memp, M_IOCTLOPS);
done:
	fdrop(fp);
	return(error);
}

int
mapped_ioctl_register_handler(struct ioctl_map_handler *he)
{
	struct ioctl_map_entry *ne;

	KKASSERT(he != NULL && he->map != NULL && he->cmd_ranges != NULL &&
		 he->subsys != NULL && *he->subsys != '\0');

	ne = kmalloc(sizeof(struct ioctl_map_entry), M_IOCTLMAP, M_WAITOK);

	ne->subsys = he->subsys;
	ne->cmd_ranges = he->cmd_ranges;

	LIST_INSERT_HEAD(&he->map->mapping, ne, entries);

	return(0);
}

int
mapped_ioctl_unregister_handler(struct ioctl_map_handler *he)
{
	struct ioctl_map_entry *ne;

	KKASSERT(he != NULL && he->map != NULL && he->cmd_ranges != NULL);

	LIST_FOREACH(ne, &he->map->mapping, entries) {
		if (ne->cmd_ranges != he->cmd_ranges)
			continue;
		LIST_REMOVE(ne, entries);
		kfree(ne, M_IOCTLMAP);
		return(0);
	}
	return(EINVAL);
}

static int	nselcoll;	/* Select collisions since boot */
int	selwait;
SYSCTL_INT(_kern, OID_AUTO, nselcoll, CTLFLAG_RD, &nselcoll, 0, "");

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
 *
 * MPALMOSTSAFE
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
		get_mplock();
		lp->lwp_oldsigmask = lp->lwp_sigmask;
		SIG_CANTMASK(sigmask);
		lp->lwp_sigmask = sigmask;
	} else {
		get_mplock();
	}

	/*
	 * Do real job.
	 */
	error = doselect(uap->nd, uap->in, uap->ou, uap->ex, ktsp,
			 &uap->sysmsg_result);

	if (uap->sigmask != NULL) {
		/* doselect() responsible for turning ERESTART into EINTR */
		KKASSERT(error != ERESTART);
		if (error == EINTR) {
			/*
			 * We can't restore the previous signal mask now
			 * because it could block the signal that interrupted
			 * us.  So make a note to restore it after executing
			 * the handler.
			 */
			lp->lwp_flag |= LWP_OLDMASK;
		} else {
			/*
			 * No handler to run. Restore previous mask immediately.
			 */
			lp->lwp_sigmask = lp->lwp_oldsigmask;
		}
	}
	rel_mplock();

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
			fflags = 0;
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
			fflags = 0;
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
			fflags = NOTE_OOB;
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
				       (void *)skap->lwp->lwp_kqueue_serial);
				FD_CLR(fd, fdp);
				++*events;
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

	if (kevp[0].flags & EV_ERROR) {
		skap->error = kevp[0].data;
		return (0);
	}

	for (i = 0; i < count; ++i) {
		if ((u_int)kevp[i].udata != skap->lwp->lwp_kqueue_serial) {
			kev = kevp[i];
			kev.flags = EV_DISABLE|EV_DELETE;
			kqueue_register(&skap->lwp->lwp_kqueue, &kev);
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
	if (nd > p->p_fd->fd_nfiles)
		nd = p->p_fd->fd_nfiles;   /* forgiving; slightly wrong */

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
	error = kern_kevent(&kap->lwp->lwp_kqueue, nd * 3, res, kap,
			    select_copyin, select_copyout, ts);
	if (error == 0)
		error = putbits(bytes, kap->read_set, read);
	if (error == 0)
		error = putbits(bytes, kap->write_set, write);
	if (error == 0)
		error = putbits(bytes, kap->except_set, except);

	/*
	 * Cumulative error from individual events (EBADFD?)
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

	kap->lwp->lwp_kqueue_serial++;

	return (error);
}

/*
 * Poll system call.
 *
 * MPALMOSTSAFE
 */
int
sys_poll(struct poll_args *uap)
{
	struct pollfd *bits;
	struct pollfd smallbits[32];
	struct timeval atv, rtv, ttv;
	int ncoll, error = 0, timo;
	u_int nfds;
	size_t ni;
	struct lwp *lp = curthread->td_lwp;
	struct proc *p = curproc;

	nfds = uap->nfds;
	/*
	 * This is kinda bogus.  We have fd limits, but that is not
	 * really related to the size of the pollfd array.  Make sure
	 * we let the process use at least FD_SETSIZE entries and at
	 * least enough for the current limits.  We want to be reasonably
	 * safe, but not overly restrictive.
	 */
	if (nfds > p->p_rlimit[RLIMIT_NOFILE].rlim_cur && nfds > FD_SETSIZE)
		return (EINVAL);
	ni = nfds * sizeof(struct pollfd);
	if (ni > sizeof(smallbits))
		bits = kmalloc(ni, M_TEMP, M_WAITOK);
	else
		bits = smallbits;
	error = copyin(uap->fds, bits, ni);
	if (error)
		goto done2;
	if (uap->timeout != INFTIM) {
		atv.tv_sec = uap->timeout / 1000;
		atv.tv_usec = (uap->timeout % 1000) * 1000;
		if (itimerfix(&atv)) {
			error = EINVAL;
			goto done2;
		}
		getmicrouptime(&rtv);
		timevaladd(&atv, &rtv);
	} else {
		atv.tv_sec = 0;
		atv.tv_usec = 0;
	}
	timo = 0;
	get_mplock();
retry:
	ncoll = nselcoll;
	lp->lwp_flag |= LWP_SELECT;
	error = pollscan(p, bits, nfds, &uap->sysmsg_result);
	if (error || uap->sysmsg_result)
		goto done1;
	if (atv.tv_sec || atv.tv_usec) {
		getmicrouptime(&rtv);
		if (timevalcmp(&rtv, &atv, >=))
			goto done1;
		ttv = atv;
		timevalsub(&ttv, &rtv);
		timo = ttv.tv_sec > 24 * 60 * 60 ?
		    24 * 60 * 60 * hz : tvtohz_high(&ttv);
	} 
	crit_enter();
	tsleep_interlock(&selwait, PCATCH);
	if ((lp->lwp_flag & LWP_SELECT) == 0 || nselcoll != ncoll) {
		crit_exit();
		goto retry;
	}
	lp->lwp_flag &= ~LWP_SELECT;
	error = tsleep(&selwait, PCATCH | PINTERLOCKED, "poll", timo);
	crit_exit();

	if (error == 0)
		goto retry;
done1:
	rel_mplock();
done2:
	lp->lwp_flag &= ~LWP_SELECT;
	/* poll is not restarted after signals... */
	if (error == ERESTART)
		error = EINTR;
	if (error == EWOULDBLOCK)
		error = 0;
	if (error == 0) {
		error = copyout(bits, uap->fds, ni);
		if (error)
			goto out;
	}
out:
	if (ni > sizeof(smallbits))
		kfree(bits, M_TEMP);
	return (error);
}

static int
pollscan(struct proc *p, struct pollfd *fds, u_int nfd, int *res)
{
	int i;
	struct file *fp;
	int n = 0;

	for (i = 0; i < nfd; i++, fds++) {
		if (fds->fd >= p->p_fd->fd_nfiles) {
			fds->revents = POLLNVAL;
			n++;
		} else if (fds->fd < 0) {
			fds->revents = 0;
		} else {
			fp = holdfp(p->p_fd, fds->fd, -1);
			if (fp == NULL) {
				fds->revents = POLLNVAL;
				n++;
			} else {
				/*
				 * Note: backend also returns POLLHUP and
				 * POLLERR if appropriate.
				 */
				fds->revents = fo_poll(fp, fds->events,
							fp->f_cred);
				if (fds->revents != 0)
					n++;
				fdrop(fp);
			}
		}
	}
	*res = n;
	return (0);
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

/*
 * Record a select request.  A global wait must be used since a process/thread
 * might go away after recording its request.
 */
void
selrecord(struct thread *selector, struct selinfo *sip)
{
	struct proc *p;
	struct lwp *lp = NULL;

	if (selector->td_lwp == NULL)
		panic("selrecord: thread needs a process");

	if (sip->si_pid == selector->td_proc->p_pid &&
	    sip->si_tid == selector->td_lwp->lwp_tid)
		return;
	if (sip->si_pid && (p = pfind(sip->si_pid)))
		lp = lwp_rb_tree_RB_LOOKUP(&p->p_lwp_tree, sip->si_tid);
	if (lp != NULL && lp->lwp_wchan == (caddr_t)&selwait) {
		sip->si_flags |= SI_COLL;
	} else {
		sip->si_pid = selector->td_proc->p_pid;
		sip->si_tid = selector->td_lwp->lwp_tid;
	}
}

/*
 * Do a wakeup when a selectable event occurs.
 */
void
selwakeup(struct selinfo *sip)
{
	struct proc *p;
	struct lwp *lp = NULL;

	if (sip->si_pid == 0)
		return;
	if (sip->si_flags & SI_COLL) {
		nselcoll++;
		sip->si_flags &= ~SI_COLL;
		wakeup((caddr_t)&selwait);	/* YYY fixable */
	}
	p = pfind(sip->si_pid);
	sip->si_pid = 0;
	if (p == NULL)
		return;
	lp = lwp_rb_tree_RB_LOOKUP(&p->p_lwp_tree, sip->si_tid);
	if (lp == NULL)
		return;

	/*
	 * This is a temporary hack until the code can be rewritten.
	 * Check LWP_SELECT before assuming we can setrunnable().
	 * Otherwise we might catch the lwp before it actually goes to
	 * sleep.
	 */
	crit_enter();
	if (lp->lwp_flag & LWP_SELECT) {
		lp->lwp_flag &= ~LWP_SELECT;
	} else if (lp->lwp_wchan == (caddr_t)&selwait) {
		/*
		 * Flag the process to break the tsleep when
		 * setrunnable is called, but only call setrunnable
		 * here if the process is not in a stopped state.
		 */
		lp->lwp_flag |= LWP_BREAKTSLEEP;
		if (p->p_stat != SSTOP)
			setrunnable(lp);
	}
	crit_exit();

	kqueue_wakeup(&lp->lwp_kqueue);
}

