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
 * $DragonFly: src/sys/emulation/43bsd/43bsd_socket.c,v 1.2 2003/09/19 08:02:27 daver Exp $
 *	from: DragonFly kern/uipc_syscalls.c,v 1.13
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
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/kern_syscall.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/uio.h>

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
	MALLOC(sa, struct sockaddr *, len, M_SONAME, M_WAITOK);
	error = copyin(uaddr, sa, len);
	if (error) {
		FREE(sa, M_SONAME);
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

int
oaccept(struct accept_args *uap)
{
	struct sockaddr *sa = NULL;
	int sa_len;
	int error;

	if (uap->name) {
		error = copyin(uap->anamelen, &sa_len, sizeof(sa_len));
		if (error)
			return (error);

		error = kern_accept(uap->s, &sa, &sa_len, &uap->sysmsg_result);

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
			FREE(sa, M_SONAME);
	} else {
		error = kern_accept(uap->s, NULL, 0, &uap->sysmsg_result);
	}
	return (error);
}

int
ogetsockname(struct getsockname_args *uap)
{
	struct sockaddr *sa = NULL;
	int error, sa_len;

	error = copyin(uap->alen, &sa_len, sizeof(sa_len));
	if (error)
		return (error);

	error = kern_getsockname(uap->fdes, &sa, &sa_len);

	if (error == 0)
		error = compat_43_copyout_sockaddr(sa, uap->asa, sa_len);
	if (error == 0) {
		error = copyout(&sa_len, uap->alen, sizeof(*uap->alen));
	}
	if (sa)
		FREE(sa, M_SONAME);
	return (error);
}

int
ogetpeername(struct ogetpeername_args *uap)
{
	struct sockaddr *sa = NULL;
	int error, sa_len;

	error = copyin(uap->alen, &sa_len, sizeof(sa_len));
	if (error)
		return (error);

	error = kern_getpeername(uap->fdes, &sa, &sa_len);

	if (error == 0) {
		error = compat_43_copyout_sockaddr(sa, uap->asa, sa_len);
	}
	if (error == 0)
		error = copyout(&sa_len, uap->alen, sizeof(*uap->alen));
	if (sa)
		FREE(sa, M_SONAME);
	return (error);
}

int
osend(struct osend_args *uap)
{
	struct msghdr msg;
	struct iovec aiov;
	int error;

	msg.msg_name = NULL;
	msg.msg_namelen = 0;
	msg.msg_iov = &aiov;
	msg.msg_iovlen = 1;
	msg.msg_control = NULL;
	msg.msg_flags = 0;
	aiov.iov_base = uap->buf;
	aiov.iov_len = uap->len;

	error = kern_sendmsg(uap->s, &msg, &uap->sysmsg_result);

	return (error);
}

int
osendmsg(struct osendmsg_args *uap)
{
	struct msghdr msg;
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
		msg.msg_name = sa;
	}

	/*
	 * Conditionally copyin msg.msg_control.
	 */
	if (msg.msg_control) {
		if (msg.msg_controllen < sizeof(struct cmsghdr)) {
			error = EINVAL;
			goto cleanup;
		}
		error = sockargs(&control, msg.msg_control,
		    msg.msg_controllen, MT_CONTROL);
		if (error)
			goto cleanup;
		/*
		 * In 4.3BSD, the only type of ancillary data was
		 * access rights and this data did not use a header
		 * to identify it's type.  Thus, we must prepend the
		 * control data with the proper cmsghdr structure
		 * so that the kernel recognizes it as access rights.
		 */
		M_PREPEND(control, sizeof(*cm), M_WAIT);
		if (control == NULL) {
			error = ENOBUFS;
			goto cleanup;
		} else {
			cm = mtod(control, struct cmsghdr *);
			cm->cmsg_len = control->m_len;
			cm->cmsg_level = SOL_SOCKET;
			cm->cmsg_type = SCM_RIGHTS;
		}
		msg.msg_control = control;
	}

	/*
	 * We always copyin msg.msg_iov.
	 */
	if (msg.msg_iovlen >= UIO_MAXIOV) {
		error =  EMSGSIZE;
		goto cleanup;
	}
	if (msg.msg_iovlen >= UIO_SMALLIOV) {
		MALLOC(iov, struct iovec *,
		    sizeof(struct iovec) * msg.msg_iovlen, M_IOV,
		    M_WAITOK);
	} else {
		iov = aiov;
	}
	error = copyin(msg.msg_iov, iov,
	    msg.msg_iovlen * sizeof (struct iovec));
	if (error)
		goto cleanup;
	msg.msg_iov = iov;

	error = kern_sendmsg(uap->s, &msg, &uap->sysmsg_result);

cleanup:
	if (sa)
		FREE(sa, M_SONAME);
	if (iov != aiov)
		FREE(iov, M_IOV);
	return (error);
}

int
orecv(struct orecv_args *uap)
{
	struct msghdr msg;
	struct iovec aiov;
	int error;

	msg.msg_name = NULL;
	msg.msg_namelen = 0;
	msg.msg_iov = &aiov;
	msg.msg_iovlen = 1;
	msg.msg_control = NULL;
	msg.msg_flags = uap->flags;
	aiov.iov_base = uap->buf;
	aiov.iov_len = uap->len;

	error = kern_recvmsg(uap->s, &msg, &uap->sysmsg_result);

	return (error);
}

int
orecvfrom(struct recvfrom_args *uap)
{
	struct msghdr msg;
	struct iovec aiov;
	int error, fromlen;

	if (uap->fromlenaddr) {
		error = copyin(uap->fromlenaddr, &fromlen, sizeof(fromlen));
		if (error)
			return (error);
	} else {
		fromlen = 0;
	}

	msg.msg_name = NULL;
	msg.msg_namelen = fromlen;
	msg.msg_iov = &aiov;
	msg.msg_iovlen = 1;
	msg.msg_control = NULL;
	msg.msg_flags = uap->flags;
	aiov.iov_base = uap->buf;
	aiov.iov_len = uap->len;

	error = kern_recvmsg(uap->s, &msg, &uap->sysmsg_result);

	fromlen = MIN(msg.msg_namelen, fromlen);
	if (uap->from) {
		if (error == 0)
			error = compat_43_copyout_sockaddr(msg.msg_name,
			    uap->from, fromlen);
		if (error == 0)
			/*
			 * Old recvfrom didn't signal an error if this
			 * next copyout failed.
			 */
			copyout(&fromlen, uap->fromlenaddr, sizeof(fromlen));
	}
	if (msg.msg_name)
		FREE(msg.msg_name, M_SONAME);

	return (error);
}

int
orecvmsg(struct orecvmsg_args *uap)
{
	struct msghdr msg;
	struct iovec aiov[UIO_SMALLIOV], *iov = NULL;
	struct mbuf *m, *ucontrol;
	caddr_t uname;
	caddr_t ctlbuf;
	socklen_t *unamelenp, *ucontrollenp;
	int error, fromlen, len;

	/*
	 * This copyin handles everything except the iovec.
	 */
	error = copyin(uap->msg, &msg, sizeof(struct omsghdr));
	if (error)
		return (error);

	/*
	 * Save some userland pointers for the copyouts.
	 */
	uname = msg.msg_name;
	unamelenp = (socklen_t *)((caddr_t)uap->msg + offsetof(struct msghdr,
	    msg_namelen));
	ucontrol = msg.msg_control;
	ucontrollenp = (socklen_t *)((caddr_t)uap->msg + offsetof(struct msghdr,
	    msg_controllen));

	fromlen = msg.msg_namelen;

	/*
	 * Copyin msg.msg_iov.
	 */
	if (msg.msg_iovlen >= UIO_MAXIOV)
		return (EMSGSIZE);
	if (msg.msg_iovlen >= UIO_SMALLIOV) {
		MALLOC(iov, struct iovec *,
		    sizeof(struct iovec) * msg.msg_iovlen, M_IOV, M_WAITOK);
	} else {
		iov = aiov;
	}
	error = copyin(msg.msg_iov, iov, msg.msg_iovlen * sizeof(struct iovec));
	if (error)
		goto cleanup;
	msg.msg_iov = iov;

	/* Don't forget the flags. */
	msg.msg_flags = uap->flags;

	error = kern_recvmsg(uap->s, &msg, &uap->sysmsg_result);

	/*
	 * Copyout msg.msg_name and msg.msg_namelen.
	 */
	if (error == 0 && uname) {
		fromlen = MIN(msg.msg_namelen, fromlen);
		error = compat_43_copyout_sockaddr(msg.msg_name, uname,
		    fromlen);
		if (error == 0)
			/*
			 * Old recvfrom didn't signal an error if this
			 * next copyout failed.
			 */
			copyout(&fromlen, unamelenp, sizeof(*unamelenp));
	}

	/*
	 * Copyout msg.msg_control and msg.msg_controllen.
	 */
	if (error == 0 && ucontrol) {
		/*
		 * If we receive access rights, trim the cmsghdr; anything
		 * else is tossed.
		 */
		if (msg.msg_control) {
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
		}

		len = msg.msg_controllen;
		msg.msg_controllen = 0;
		m = msg.msg_control;
		ctlbuf = (caddr_t)ucontrol;

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
		msg.msg_controllen = ctlbuf - (caddr_t)ucontrol;
		error = copyout(&msg.msg_controllen, ucontrollenp,
		    sizeof(*ucontrollenp));
	}

cleanup:
	if (msg.msg_name)
		FREE(msg.msg_name, M_SONAME);
	if (iov != aiov)
		FREE(iov, M_IOV);
	if (msg.msg_control)
		m_freem(msg.msg_control);
	return (error);
}

