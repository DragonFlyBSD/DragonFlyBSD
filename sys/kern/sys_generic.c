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
 * $DragonFly: src/sys/kern/sys_generic.c,v 1.17 2004/08/13 11:59:00 joerg Exp $
 */

#include "opt_ktrace.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/sysproto.h>
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

#include <machine/limits.h>

static MALLOC_DEFINE(M_IOCTLOPS, "ioctlops", "ioctl data buffer");
static MALLOC_DEFINE(M_IOCTLMAP, "ioctlmap", "mapped ioctl handler buffer");
static MALLOC_DEFINE(M_SELECT, "select", "select() buffer");
MALLOC_DEFINE(M_IOV, "iov", "large iov's");

static int	pollscan (struct proc *, struct pollfd *, u_int, int *);
static int	selscan (struct proc *, fd_mask **, fd_mask **,
			int, int *);

struct file*
holdfp(fdp, fd, flag)
	struct filedesc* fdp;
	int fd, flag;
{
	struct file* fp;

	if (((u_int)fd) >= fdp->fd_nfiles ||
	    (fp = fdp->fd_ofiles[fd]) == NULL ||
	    (fp->f_flag & flag) == 0) {
		return (NULL);
	}
	fhold(fp);
	return (fp);
}

/*
 * Read system call.
 */
int
read(struct read_args *uap)
{
	struct thread *td = curthread;
	struct uio auio;
	struct iovec aiov;
	int error;

	aiov.iov_base = uap->buf;
	aiov.iov_len = uap->nbyte;
	auio.uio_iov = &aiov;
	auio.uio_iovcnt = 1;
	auio.uio_offset = -1;
	auio.uio_resid = uap->nbyte;
	auio.uio_rw = UIO_READ;
	auio.uio_segflg = UIO_USERSPACE;
	auio.uio_td = td;

	error = kern_readv(uap->fd, &auio, 0, &uap->sysmsg_result);

	return(error);
}

/*
 * Pread system call
 */
int
pread(struct pread_args *uap)
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
readv(struct readv_args *uap)
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

	error = kern_readv(uap->fd, &auio, 0, &uap->sysmsg_result);

	iovec_free(&iov, aiov);
	return (error);
}

int
kern_readv(int fd, struct uio *auio, int flags, int *res)
{
	struct thread *td = curthread;
	struct proc *p = td->td_proc;
	struct file *fp;
	struct filedesc *fdp = p->p_fd;
	int len, error;
#ifdef KTRACE
	struct iovec *ktriov = NULL;
	struct uio ktruio;
#endif

	KKASSERT(p);

	fp = holdfp(fdp, fd, FREAD);
	if (fp == NULL)
		return (EBADF);
	if (flags & FOF_OFFSET && fp->f_type != DTYPE_VNODE) {
		error = ESPIPE;
		goto done;
	}
	if (auio->uio_resid < 0) {
		error = EINVAL;
		goto done;
	}
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
	error = fo_read(fp, auio, fp->f_cred, flags, td);
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
			ktrgenio(p->p_tracep, fd, UIO_READ, &ktruio, error);
		}
		FREE(ktriov, M_TEMP);
	}
#endif
	if (error == 0)
		*res = len - auio->uio_resid;
done:
	fdrop(fp, td);
	return (error);
}

/*
 * Write system call
 */
int
write(struct write_args *uap)
{
	struct thread *td = curthread;
	struct uio auio;
	struct iovec aiov;
	int error;

	aiov.iov_base = (void *)(uintptr_t)uap->buf;
	aiov.iov_len = uap->nbyte;
	auio.uio_iov = &aiov;
	auio.uio_iovcnt = 1;
	auio.uio_offset = -1;
	auio.uio_resid = uap->nbyte;
	auio.uio_rw = UIO_WRITE;
	auio.uio_segflg = UIO_USERSPACE;
	auio.uio_td = td;

	error = kern_writev(uap->fd, &auio, 0, &uap->sysmsg_result);

	return(error);
}

/*
 * Pwrite system call
 */
int
pwrite(struct pwrite_args *uap)
{
	struct thread *td = curthread;
	struct uio auio;
	struct iovec aiov;
	int error;

	aiov.iov_base = (void *)(uintptr_t)uap->buf;
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
writev(struct writev_args *uap)
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

	error = kern_writev(uap->fd, &auio, 0, &uap->sysmsg_result);

	iovec_free(&iov, aiov);
	return (error);
}

/*
 * Gather write system call
 */
int
kern_writev(int fd, struct uio *auio, int flags, int *res)
{
	struct thread *td = curthread;
	struct proc *p = td->td_proc;
	struct file *fp;
	struct filedesc *fdp = p->p_fd;
	long len, error;
#ifdef KTRACE
	struct iovec *ktriov = NULL;
	struct uio ktruio;
#endif

	KKASSERT(p);

	fp = holdfp(fdp, fd, FWRITE);
	if (fp == NULL)
		return (EBADF);
	if ((flags & FOF_OFFSET) && fp->f_type != DTYPE_VNODE) {
		error = ESPIPE;
		goto done;
	}
	if (auio->uio_resid < 0) {
		error = EINVAL;
		goto done;
	}
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
	if (fp->f_type == DTYPE_VNODE)
		bwillwrite();
	error = fo_write(fp, auio, fp->f_cred, flags, td);
	if (error) {
		if (auio->uio_resid != len && (error == ERESTART ||
		    error == EINTR || error == EWOULDBLOCK))
			error = 0;
		if (error == EPIPE)
			psignal(p, SIGPIPE);
	}
#ifdef KTRACE
	if (ktriov != NULL) {
		if (error == 0) {
			ktruio.uio_iov = ktriov;
			ktruio.uio_resid = len - auio->uio_resid;
			ktrgenio(p->p_tracep, fd, UIO_WRITE, &ktruio, error);
		}
		FREE(ktriov, M_TEMP);
	}
#endif
	if (error == 0)
		*res = len - auio->uio_resid;
done:
	fdrop(fp, td);
	return (error);
}

/*
 * Ioctl system call
 */
/* ARGSUSED */
int
ioctl(struct ioctl_args *uap)
{
	return(mapped_ioctl(uap->fd, uap->com, uap->data, NULL));
}

struct ioctl_map_entry {
	const char *subsys;
	struct ioctl_map_range *cmd_ranges;
	LIST_ENTRY(ioctl_map_entry) entries;
};

int
mapped_ioctl(int fd, u_long com, caddr_t uspc_data, struct ioctl_map *map)
{
	struct thread *td = curthread;
	struct proc *p = td->td_proc;
	struct file *fp;
	struct filedesc *fdp;
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
	fdp = p->p_fd;
	if ((u_int)fd >= fdp->fd_nfiles ||
	    (fp = fdp->fd_ofiles[fd]) == NULL)
		return(EBADF);

	if ((fp->f_flag & (FREAD | FWRITE)) == 0)
		return(EBADF);

	if (map != NULL) {	/* obey translation map */
		u_long maskcmd;
		struct ioctl_map_entry *e;

		maskcmd = com & map->mask;

		LIST_FOREACH(e, &map->mapping, entries) {
			for (iomc = e->cmd_ranges; iomc->start != 0 ||
			     iomc->maptocmd != 0 || iomc->func != NULL;
			     iomc++) {
				if (maskcmd >= iomc->start &&
				    maskcmd <= iomc->end)
					break;
			}

			/* Did we find a match? */
			if (iomc->start != 0 || iomc->maptocmd != 0 ||
			    iomc->func != NULL)
				break;
		}

		if (iomc == NULL ||
		    (iomc->start == 0 && iomc->maptocmd == 0
		     && iomc->func == NULL)) {
			printf("%s: 'ioctl' fd=%d, cmd=0x%lx ('%c',%d) not implemented\n",
			       map->sys, fd, maskcmd,
			       (int)((maskcmd >> 8) & 0xff),
			       (int)(maskcmd & 0xff));
			return(EINVAL);
		}

		com = iomc->maptocmd;
	}

	switch (com) {
	case FIONCLEX:
		fdp->fd_ofileflags[fd] &= ~UF_EXCLOSE;
		return(0);
	case FIOCLEX:
		fdp->fd_ofileflags[fd] |= UF_EXCLOSE;
		return(0);
	}

	/*
	 * Interpret high order word to find amount of data to be
	 * copied to/from the user's address space.
	 */
	size = IOCPARM_LEN(com);
	if (size > IOCPARM_MAX)
		return(ENOTTY);

	fhold(fp);

	memp = NULL;
	if (size > sizeof (ubuf.stkbuf)) {
		memp = malloc(size, M_IOCTLOPS, M_WAITOK);
		data = memp;
	} else {
		data = ubuf.stkbuf;
	}
	if ((com & IOC_IN) != 0) {
		if (size != 0) {
			error = copyin(uspc_data, data, (u_int)size);
			if (error) {
				if (memp != NULL)
					free(memp, M_IOCTLOPS);
				fdrop(fp, td);
				return(error);
			}
		} else {
			*(caddr_t *)data = uspc_data;
		}
	} else if ((com & IOC_OUT) != 0 && size) {
		/*
		 * Zero the buffer so the user always
		 * gets back something deterministic.
		 */
		bzero(data, size);
	} else if ((com & IOC_VOID) != 0) {
		*(caddr_t *)data = uspc_data;
	}

	switch (com) {

	case FIONBIO:
		if ((tmp = *(int *)data))
			fp->f_flag |= FNONBLOCK;
		else
			fp->f_flag &= ~FNONBLOCK;
		error = fo_ioctl(fp, FIONBIO, (caddr_t)&tmp, td);
		break;

	case FIOASYNC:
		if ((tmp = *(int *)data))
			fp->f_flag |= FASYNC;
		else
			fp->f_flag &= ~FASYNC;
		error = fo_ioctl(fp, FIOASYNC, (caddr_t)&tmp, td);
		break;

	default:
		/*
		 *  If there is a override function,
		 *  call it instead of directly routing the call
		 */
		if (map != NULL && iomc->func != NULL)
			error = iomc->func(fp, com, ocom, data, td);
		else
			error = fo_ioctl(fp, com, data, td);
		/*
		 * Copy any data to user, size was
		 * already set and checked above.
		 */
		if (error == 0 && (com & IOC_OUT) != 0 && size != 0)
			error = copyout(data, uspc_data, (u_int)size);
		break;
	}
	if (memp != NULL)
		free(memp, M_IOCTLOPS);
	fdrop(fp, td);
	return(error);
}

int
mapped_ioctl_register_handler(struct ioctl_map_handler *he)
{
	struct ioctl_map_entry *ne;

	KKASSERT(he != NULL && he->map != NULL && he->cmd_ranges != NULL &&
		 he->subsys != NULL && *he->subsys != '\0');

	ne = malloc(sizeof(struct ioctl_map_entry), M_IOCTLMAP, M_WAITOK);

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
		free(ne, M_IOCTLMAP);
		return(0);
	}
	return(EINVAL);
}

static int	nselcoll;	/* Select collisions since boot */
int	selwait;
SYSCTL_INT(_kern, OID_AUTO, nselcoll, CTLFLAG_RD, &nselcoll, 0, "");

/*
 * Select system call.
 */
int
select(struct select_args *uap)
{
	struct proc *p = curproc;

	/*
	 * The magic 2048 here is chosen to be just enough for FD_SETSIZE
	 * infds with the new FD_SETSIZE of 1024, and more than enough for
	 * FD_SETSIZE infds, outfds and exceptfds with the old FD_SETSIZE
	 * of 256.
	 */
	fd_mask s_selbits[howmany(2048, NFDBITS)];
	fd_mask *ibits[3], *obits[3], *selbits, *sbp;
	struct timeval atv, rtv, ttv;
	int s, ncoll, error, timo;
	u_int nbufbytes, ncpbytes, nfdbits;

	if (uap->nd < 0)
		return (EINVAL);
	if (uap->nd > p->p_fd->fd_nfiles)
		uap->nd = p->p_fd->fd_nfiles;   /* forgiving; slightly wrong */

	/*
	 * Allocate just enough bits for the non-null fd_sets.  Use the
	 * preallocated auto buffer if possible.
	 */
	nfdbits = roundup(uap->nd, NFDBITS);
	ncpbytes = nfdbits / NBBY;
	nbufbytes = 0;
	if (uap->in != NULL)
		nbufbytes += 2 * ncpbytes;
	if (uap->ou != NULL)
		nbufbytes += 2 * ncpbytes;
	if (uap->ex != NULL)
		nbufbytes += 2 * ncpbytes;
	if (nbufbytes <= sizeof s_selbits)
		selbits = &s_selbits[0];
	else
		selbits = malloc(nbufbytes, M_SELECT, M_WAITOK);

	/*
	 * Assign pointers into the bit buffers and fetch the input bits.
	 * Put the output buffers together so that they can be bzeroed
	 * together.
	 */
	sbp = selbits;
#define	getbits(name, x) \
	do {								\
		if (uap->name == NULL)					\
			ibits[x] = NULL;				\
		else {							\
			ibits[x] = sbp + nbufbytes / 2 / sizeof *sbp;	\
			obits[x] = sbp;					\
			sbp += ncpbytes / sizeof *sbp;			\
			error = copyin(uap->name, ibits[x], ncpbytes);	\
			if (error != 0)					\
				goto done;				\
		}							\
	} while (0)
	getbits(in, 0);
	getbits(ou, 1);
	getbits(ex, 2);
#undef	getbits
	if (nbufbytes != 0)
		bzero(selbits, nbufbytes / 2);

	if (uap->tv) {
		error = copyin((caddr_t)uap->tv, (caddr_t)&atv,
			sizeof (atv));
		if (error)
			goto done;
		if (itimerfix(&atv)) {
			error = EINVAL;
			goto done;
		}
		getmicrouptime(&rtv);
		timevaladd(&atv, &rtv);
	} else {
		atv.tv_sec = 0;
		atv.tv_usec = 0;
	}
	timo = 0;
retry:
	ncoll = nselcoll;
	p->p_flag |= P_SELECT;
	error = selscan(p, ibits, obits, uap->nd, &uap->sysmsg_result);
	if (error || uap->sysmsg_result)
		goto done;
	if (atv.tv_sec || atv.tv_usec) {
		getmicrouptime(&rtv);
		if (timevalcmp(&rtv, &atv, >=)) 
			goto done;
		ttv = atv;
		timevalsub(&ttv, &rtv);
		timo = ttv.tv_sec > 24 * 60 * 60 ?
		    24 * 60 * 60 * hz : tvtohz_high(&ttv);
	}
	s = splhigh();
	if ((p->p_flag & P_SELECT) == 0 || nselcoll != ncoll) {
		splx(s);
		goto retry;
	}
	p->p_flag &= ~P_SELECT;

	error = tsleep((caddr_t)&selwait, PCATCH, "select", timo);
	
	splx(s);
	if (error == 0)
		goto retry;
done:
	p->p_flag &= ~P_SELECT;
	/* select is not restarted after signals... */
	if (error == ERESTART)
		error = EINTR;
	if (error == EWOULDBLOCK)
		error = 0;
#define	putbits(name, x) \
	if (uap->name && (error2 = copyout(obits[x], uap->name, ncpbytes))) \
		error = error2;
	if (error == 0) {
		int error2;

		putbits(in, 0);
		putbits(ou, 1);
		putbits(ex, 2);
#undef putbits
	}
	if (selbits != &s_selbits[0])
		free(selbits, M_SELECT);
	return (error);
}

static int
selscan(struct proc *p, fd_mask **ibits, fd_mask **obits, int nfd, int *res)
{
	struct thread *td = p->p_thread;
	struct filedesc *fdp = p->p_fd;
	int msk, i, fd;
	fd_mask bits;
	struct file *fp;
	int n = 0;
	/* Note: backend also returns POLLHUP/POLLERR if appropriate. */
	static int flag[3] = { POLLRDNORM, POLLWRNORM, POLLRDBAND };

	for (msk = 0; msk < 3; msk++) {
		if (ibits[msk] == NULL)
			continue;
		for (i = 0; i < nfd; i += NFDBITS) {
			bits = ibits[msk][i/NFDBITS];
			/* ffs(int mask) not portable, fd_mask is long */
			for (fd = i; bits && fd < nfd; fd++, bits >>= 1) {
				if (!(bits & 1))
					continue;
				fp = fdp->fd_ofiles[fd];
				if (fp == NULL)
					return (EBADF);
				if (fo_poll(fp, flag[msk], fp->f_cred, td)) {
					obits[msk][(fd)/NFDBITS] |=
					    ((fd_mask)1 << ((fd) % NFDBITS));
					n++;
				}
			}
		}
	}
	*res = n;
	return (0);
}

/*
 * Poll system call.
 */
int
poll(struct poll_args *uap)
{
	caddr_t bits;
	char smallbits[32 * sizeof(struct pollfd)];
	struct timeval atv, rtv, ttv;
	int s, ncoll, error = 0, timo;
	u_int nfds;
	size_t ni;
	struct proc *p = curproc;

	nfds = SCARG(uap, nfds);
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
		bits = malloc(ni, M_TEMP, M_WAITOK);
	else
		bits = smallbits;
	error = copyin(SCARG(uap, fds), bits, ni);
	if (error)
		goto done;
	if (SCARG(uap, timeout) != INFTIM) {
		atv.tv_sec = SCARG(uap, timeout) / 1000;
		atv.tv_usec = (SCARG(uap, timeout) % 1000) * 1000;
		if (itimerfix(&atv)) {
			error = EINVAL;
			goto done;
		}
		getmicrouptime(&rtv);
		timevaladd(&atv, &rtv);
	} else {
		atv.tv_sec = 0;
		atv.tv_usec = 0;
	}
	timo = 0;
retry:
	ncoll = nselcoll;
	p->p_flag |= P_SELECT;
	error = pollscan(p, (struct pollfd *)bits, nfds, &uap->sysmsg_result);
	if (error || uap->sysmsg_result)
		goto done;
	if (atv.tv_sec || atv.tv_usec) {
		getmicrouptime(&rtv);
		if (timevalcmp(&rtv, &atv, >=))
			goto done;
		ttv = atv;
		timevalsub(&ttv, &rtv);
		timo = ttv.tv_sec > 24 * 60 * 60 ?
		    24 * 60 * 60 * hz : tvtohz_high(&ttv);
	} 
	s = splhigh(); 
	if ((p->p_flag & P_SELECT) == 0 || nselcoll != ncoll) {
		splx(s);
		goto retry;
	}
	p->p_flag &= ~P_SELECT;
	error = tsleep((caddr_t)&selwait, PCATCH, "poll", timo);
	splx(s);
	if (error == 0)
		goto retry;
done:
	p->p_flag &= ~P_SELECT;
	/* poll is not restarted after signals... */
	if (error == ERESTART)
		error = EINTR;
	if (error == EWOULDBLOCK)
		error = 0;
	if (error == 0) {
		error = copyout(bits, SCARG(uap, fds), ni);
		if (error)
			goto out;
	}
out:
	if (ni > sizeof(smallbits))
		free(bits, M_TEMP);
	return (error);
}

static int
pollscan(struct proc *p, struct pollfd *fds, u_int nfd, int *res)
{
	struct thread *td = p->p_thread;
	struct filedesc *fdp = p->p_fd;
	int i;
	struct file *fp;
	int n = 0;

	for (i = 0; i < nfd; i++, fds++) {
		if (fds->fd >= fdp->fd_nfiles) {
			fds->revents = POLLNVAL;
			n++;
		} else if (fds->fd < 0) {
			fds->revents = 0;
		} else {
			fp = fdp->fd_ofiles[fds->fd];
			if (fp == NULL) {
				fds->revents = POLLNVAL;
				n++;
			} else {
				/*
				 * Note: backend also returns POLLHUP and
				 * POLLERR if appropriate.
				 */
				fds->revents = fo_poll(fp, fds->events,
				    fp->f_cred, td);
				if (fds->revents != 0)
					n++;
			}
		}
	}
	*res = n;
	return (0);
}

/*
 * OpenBSD poll system call.
 * XXX this isn't quite a true representation..  OpenBSD uses select ops.
 */
int
openbsd_poll(struct openbsd_poll_args *uap)
{
	return (poll((struct poll_args *)uap));
}

/*ARGSUSED*/
int
seltrue(dev_t dev, int events, struct thread *td)
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
	pid_t mypid;

	if ((p = selector->td_proc) == NULL)
		panic("selrecord: thread needs a process");

	mypid = p->p_pid;
	if (sip->si_pid == mypid)
		return;
	if (sip->si_pid && (p = pfind(sip->si_pid)) &&
	    p->p_wchan == (caddr_t)&selwait) {
		sip->si_flags |= SI_COLL;
	} else {
		sip->si_pid = mypid;
	}
}

/*
 * Do a wakeup when a selectable event occurs.
 */
void
selwakeup(struct selinfo *sip)
{
	struct proc *p;
	int s;

	if (sip->si_pid == 0)
		return;
	if (sip->si_flags & SI_COLL) {
		nselcoll++;
		sip->si_flags &= ~SI_COLL;
		wakeup((caddr_t)&selwait);	/* YYY fixable */
	}
	p = pfind(sip->si_pid);
	sip->si_pid = 0;
	if (p != NULL) {
		s = splhigh();
		if (p->p_wchan == (caddr_t)&selwait) {
			if (p->p_stat == SSLEEP)
				setrunnable(p);
			else
				unsleep(p->p_thread);
		} else if (p->p_flag & P_SELECT)
			p->p_flag &= ~P_SELECT;
		splx(s);
	}
}

