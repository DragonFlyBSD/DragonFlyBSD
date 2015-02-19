/*
 * 43BSD_SOCKET.C	- 4.3BSD compatibility socket syscalls
 *
 * Copyright (c) 1982, 1986, 1989, 1990, 1993
 *      The Regents of the University of California.  All rights reserved.
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
 *      This product includes software developed by the University of
 *      California, Berkeley and its contributors.
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
 * The original versions of these syscalls used to live in
 * kern/uipc_syscalls.c.  These are heavily modified to use the
 * new split syscalls.
 */

#include "opt_compat.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/sysproto.h>
#include <sys/kern_syscall.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/proc.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/uio.h>

#include <sys/mplock2.h>

#include "43bsd_socket.h"

/*
 * System call interface to the socket abstraction.
 */

static int
compat_43_getsockaddr(struct sockaddr **namp, caddr_t uaddr, size_t len)
{
	struct sockaddr *sa;
	int error;

	*namp = NULL;
	if (len > SOCK_MAXADDRLEN)
		return ENAMETOOLONG;
	if (len < offsetof(struct sockaddr, sa_data[0]))
		return EDOM;
	sa = kmalloc(len, M_SONAME, M_WAITOK);
	error = copyin(uaddr, sa, len);
	if (error) {
		kfree(sa, M_SONAME);
	} else {
		/*
		 * Convert to the 4.4BSD sockaddr structure.
		 */
		sa->sa_family = sa->sa_len;
		sa->sa_len = len;
		*namp = sa;
	}
	return error;
}

static int
compat_43_copyout_sockaddr(struct sockaddr *sa, caddr_t uaddr, int sa_len)
{
	int error;

	((struct osockaddr *)sa)->sa_family = sa->sa_family;
	error = copyout(sa, uaddr, sa_len);

	return (error);
}

/*
 * MPALMOSTSAFE
 */
int
sys_oaccept(struct accept_args *uap)
{
	struct sockaddr *sa = NULL;
	int sa_len;
	int error;

	if (uap->name) {
		error = copyin(uap->anamelen, &sa_len, sizeof(sa_len));
		if (error)
			return (error);

		get_mplock();
		error = kern_accept(uap->s, 0, &sa, &sa_len,
				    &uap->sysmsg_iresult);
		rel_mplock();

		if (error) {
			/*
			 * return a namelen of zero for older code which
			 * might ignore the return value from accept.
			 */
			sa_len = 0;
			copyout(&sa_len, uap->anamelen, sizeof(*uap->anamelen));
		} else {
			compat_43_copyout_sockaddr(sa, uap->name, sa_len);
			if (error == 0) {
				error = copyout(&sa_len, uap->anamelen,
				    sizeof(*uap->anamelen));
			}
		}
		if (sa)
			kfree(sa, M_SONAME);
	} else {
		get_mplock();
		error = kern_accept(uap->s, 0, NULL, 0, &uap->sysmsg_iresult);
		rel_mplock();
	}
	return (error);
}

/*
 * MPALMOSTSAFE
 */
int
sys_ogetsockname(struct getsockname_args *uap)
{
	struct sockaddr *sa = NULL;
	int error, sa_len;

	error = copyin(uap->alen, &sa_len, sizeof(sa_len));
	if (error)
		return (error);

	get_mplock();
	error = kern_getsockname(uap->fdes, &sa, &sa_len);
	rel_mplock();

	if (error == 0)
		error = compat_43_copyout_sockaddr(sa, uap->asa, sa_len);
	if (error == 0) {
		error = copyout(&sa_len, uap->alen, sizeof(*uap->alen));
	}
	if (sa)
		kfree(sa, M_SONAME);
	return (error);
}

/*
 * MPALMOSTSAFE
 */
int
sys_ogetpeername(struct ogetpeername_args *uap)
{
	struct sockaddr *sa = NULL;
	int error, sa_len;

	error = copyin(uap->alen, &sa_len, sizeof(sa_len));
	if (error)
		return (error);

	get_mplock();
	error = kern_getpeername(uap->fdes, &sa, &sa_len);
	rel_mplock();

	if (error == 0) {
		error = compat_43_copyout_sockaddr(sa, uap->asa, sa_len);
	}
	if (error == 0)
		error = copyout(&sa_len, uap->alen, sizeof(*uap->alen));
	if (sa)
		kfree(sa, M_SONAME);
	return (error);
}

/*
 * MPALMOSTSAFE
 */
int
sys_osend(struct osend_args *uap)
{
	struct thread *td = curthread;
	struct uio auio;
	struct iovec aiov;
	int error;

	aiov.iov_base = uap->buf;
	aiov.iov_len = uap->len;
	auio.uio_iov = &aiov;
	auio.uio_iovcnt = 1;
	auio.uio_offset = 0;
	auio.uio_resid = uap->len;
	auio.uio_segflg = UIO_USERSPACE;
	auio.uio_rw = UIO_WRITE;
	auio.uio_td = td;

	get_mplock();
	error = kern_sendmsg(uap->s, NULL, &auio, NULL, uap->flags,
			     &uap->sysmsg_szresult);
	rel_mplock();

	return (error);
}

/*
 * MPALMOSTSAFE
 */
int
sys_osendmsg(struct osendmsg_args *uap)
{
	struct thread *td = curthread;
	struct msghdr msg;
	struct uio auio;
	struct iovec aiov[UIO_SMALLIOV], *iov = NULL;
	struct sockaddr *sa = NULL;
	struct mbuf *control = NULL;
	struct cmsghdr *cm;
	int error;

	error = copyin(uap->msg, (caddr_t)&msg, sizeof (msg));
	if (error)
		return (error);

	/*
	 * Conditionally copyin msg.msg_name.
	 */
	if (msg.msg_name) {
		error = compat_43_getsockaddr(&sa, msg.msg_name,
		    msg.msg_namelen);
		if (error)
			return (error);
	}

	/*
	 * Populate auio.
	 */
	error = iovec_copyin(msg.msg_iov, &iov, aiov, msg.msg_iovlen,
			     &auio.uio_resid);
	if (error)
		goto cleanup2;
	auio.uio_iov = iov;
	auio.uio_iovcnt = msg.msg_iovlen;
	auio.uio_offset = 0;
	auio.uio_segflg = UIO_USERSPACE;
	auio.uio_rw = UIO_WRITE;
	auio.uio_td = td;

	/*
	 * Conditionally copyin msg.msg_control.
	 */
	if (msg.msg_control) {
		if (msg.msg_controllen < 0 || msg.msg_controllen > MLEN) {
			error = EINVAL;
			goto cleanup;
		}
		control = m_get(M_WAITOK, MT_CONTROL);
		if (control == NULL) {
			error = ENOBUFS;
			goto cleanup;
		}
		control->m_len = msg.msg_controllen;
		error = copyin(msg.msg_control, mtod(control, caddr_t),
		    msg.msg_controllen);
		if (error) {
			m_free(control);
			goto cleanup;
		}
		/*
		 * In 4.3BSD, the only type of ancillary data was
		 * access rights and this data did not use a header
		 * to identify it's type.  Thus, we must prepend the
		 * control data with the proper cmsghdr structure
		 * so that the kernel recognizes it as access rights.
		 */
		M_PREPEND(control, sizeof(*cm), M_WAITOK);
		if (control == NULL) {
			error = ENOBUFS;
			goto cleanup;
		} else {
			cm = mtod(control, struct cmsghdr *);
			cm->cmsg_len = control->m_len;
			cm->cmsg_level = SOL_SOCKET;
			cm->cmsg_type = SCM_RIGHTS;
		}
	}

	get_mplock();
	error = kern_sendmsg(uap->s, sa, &auio, control, uap->flags,
			     &uap->sysmsg_szresult);
	rel_mplock();

cleanup:
	iovec_free(&iov, aiov);
cleanup2:
	if (sa)
		kfree(sa, M_SONAME);
	return (error);
}

/*
 * MPALMOSTSAFE
 */
int
sys_orecv(struct orecv_args *uap)
{
	struct thread *td = curthread;
	struct uio auio;
	struct iovec aiov;
	int error;

	aiov.iov_base = uap->buf;
	aiov.iov_len = uap->len;
	auio.uio_iov = &aiov;
	auio.uio_iovcnt = 1;
	auio.uio_offset = 0;
	auio.uio_resid = uap->len;
	auio.uio_segflg = UIO_USERSPACE;
	auio.uio_rw = UIO_READ;
	auio.uio_td = td;

	get_mplock();
	error = kern_recvmsg(uap->s, NULL, &auio, NULL, &uap->flags,
			     &uap->sysmsg_szresult);
	rel_mplock();

	return (error);
}

/*
 * MPALMOSTSAFE
 */
int
sys_orecvfrom(struct recvfrom_args *uap)
{
	struct thread *td = curthread;
	struct uio auio;
	struct iovec aiov;
	struct sockaddr *sa = NULL;
	int error, fromlen;

	if (uap->from && uap->fromlenaddr) {
		error = copyin(uap->fromlenaddr, &fromlen, sizeof(fromlen));
		if (error)
			return (error);
		if (fromlen < 0)
			return (EINVAL);
	} else {
		fromlen = 0;
	}
	aiov.iov_base = uap->buf;
	aiov.iov_len = uap->len;
	auio.uio_iov = &aiov;
	auio.uio_iovcnt = 1;
	auio.uio_offset = 0;
	auio.uio_resid = uap->len;
	auio.uio_segflg = UIO_USERSPACE;
	auio.uio_rw = UIO_READ;
	auio.uio_td = td;

	get_mplock();
	error = kern_recvmsg(uap->s, uap->from ? &sa : NULL, &auio, NULL,
			     &uap->flags, &uap->sysmsg_szresult);
	rel_mplock();

	if (error == 0 && uap->from) {
		if (sa != NULL) {
			fromlen = MIN(fromlen, sa->sa_len);
			error = compat_43_copyout_sockaddr(sa, uap->from,
							   fromlen);
		} else
			fromlen = 0;
		if (error == 0)
			/*
			 * Old recvfrom didn't signal an error if this
			 * next copyout failed.
			 */
			copyout(&fromlen, uap->fromlenaddr, sizeof(fromlen));
	}
	if (sa)
		kfree(sa, M_SONAME);

	return (error);
}

/*
 * MPALMOSTSAFE
 */
int
sys_orecvmsg(struct orecvmsg_args *uap)
{
	struct thread *td = curthread;
	struct msghdr msg;
	struct uio auio;
	struct iovec aiov[UIO_SMALLIOV], *iov = NULL;
	struct mbuf *m, *control = NULL;
	struct sockaddr *sa = NULL;
	caddr_t ctlbuf;
	socklen_t *ufromlenp, *ucontrollenp;
	int error, fromlen, controllen, len, flags, *uflagsp;

	/*
	 * This copyin handles everything except the iovec.
	 */
	error = copyin(uap->msg, &msg, sizeof(struct omsghdr));
	if (error)
		return (error);

	if (msg.msg_name && msg.msg_namelen < 0)
		return (EINVAL);
	if (msg.msg_control && msg.msg_controllen < 0)
		return (EINVAL);

	ufromlenp = (socklen_t *)((caddr_t)uap->msg + offsetof(struct msghdr,
	    msg_namelen));
	ucontrollenp = (socklen_t *)((caddr_t)uap->msg + offsetof(struct msghdr,
	    msg_controllen));
	uflagsp = (int *)((caddr_t)uap->msg + offsetof(struct msghdr,
	    msg_flags));

	/*
	 * Populate auio.
	 */
	error = iovec_copyin(msg.msg_iov, &iov, aiov, msg.msg_iovlen,
			     &auio.uio_resid);
	if (error)
		return (error);
	auio.uio_iov = iov;
	auio.uio_iovcnt = msg.msg_iovlen;
	auio.uio_offset = 0;
	auio.uio_segflg = UIO_USERSPACE;
	auio.uio_rw = UIO_READ;
	auio.uio_td = td;

	flags = msg.msg_flags;

	get_mplock();
	error = kern_recvmsg(uap->s, (msg.msg_name ? &sa : NULL), &auio,
			     (msg.msg_control ? &control : NULL), &flags,
			     &uap->sysmsg_szresult);
	rel_mplock();

	/*
	 * Copyout msg.msg_name and msg.msg_namelen.
	 */
	if (error == 0 && msg.msg_name) {
		if (sa != NULL) {
			fromlen = MIN(msg.msg_namelen, sa->sa_len);
			error = compat_43_copyout_sockaddr(sa, msg.msg_name,
							   fromlen);
		} else
			fromlen = 0;
		if (error == 0)
			/*
			 * Old recvfrom didn't signal an error if this
			 * next copyout failed.
			 */
			copyout(&fromlen, ufromlenp, sizeof(*ufromlenp));
	}

	/*
	 * Copyout msg.msg_control and msg.msg_controllen.
	 */
	if (error == 0 && msg.msg_control) {
		/*
		 * If we receive access rights, trim the cmsghdr; anything
		 * else is tossed.
		 */
		if (mtod((struct mbuf *)msg.msg_control,
		    struct cmsghdr *)->cmsg_level != SOL_SOCKET ||
		    mtod((struct mbuf *)msg.msg_control,
		    struct cmsghdr *)->cmsg_type != SCM_RIGHTS) {
			int temp = 0;
			error = copyout(&temp, ucontrollenp,
			    sizeof(*ucontrollenp));
			goto cleanup;
		}
		((struct mbuf *)msg.msg_control)->m_len -=
		    sizeof(struct cmsghdr);
		((struct mbuf *)msg.msg_control)->m_data +=
		    sizeof(struct cmsghdr);

		len = msg.msg_controllen;
		m = control;
		ctlbuf = (caddr_t)msg.msg_control;

		while(m && len > 0) {
			unsigned int tocopy;

			if (len >= m->m_len) {
				tocopy = m->m_len;
			} else {
				msg.msg_flags |= MSG_CTRUNC;
				tocopy = len;
			}

			error = copyout(mtod(m, caddr_t), ctlbuf,
			    tocopy);
			if (error)
				goto cleanup;

			ctlbuf += tocopy;
			len -= tocopy;
			m = m->m_next;
		}
		controllen = ctlbuf - (caddr_t)msg.msg_control;
		error = copyout(&controllen, ucontrollenp,
		    sizeof(*ucontrollenp));
	}

	if (error == 0)
		error = copyout(&flags, uflagsp, sizeof(*uflagsp));

cleanup:
	if (sa)
		kfree(sa, M_SONAME);
	iovec_free(&iov, aiov);
	if (control)
		m_freem(control);
	return (error);
}

