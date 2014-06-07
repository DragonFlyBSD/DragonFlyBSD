/*-
 * Copyright (c) 1995 Søren Schmidt
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
 * $FreeBSD: src/sys/compat/linux/linux_socket.c,v 1.19.2.8 2001/11/07 20:33:55 marcel Exp $
 */

#include <sys/param.h>
#include <sys/proc.h>
#include <sys/systm.h>
#include <sys/sysproto.h>
#include <sys/fcntl.h>
#include <sys/file.h>
#include <sys/kern_syscall.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/uio.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/un.h>

#include <sys/mplock2.h>

#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>

#include <arch_linux/linux.h>
#include <arch_linux/linux_proto.h>
#include "linux_socket.h"
#include "linux_util.h"

/*
 * Copyin a sockaddr structure provided by a Linux binary.  Linux uses
 * the 4.3BSD sockaddr structure which has no sa_len field.  We must
 * pass 4.4BSD sockaddr structures when we call native functions in the
 * BSD kernel.  This function does the conversion for us.
 *
 * Also, our socket calls require the sockaddr structure length to agree
 * with the address family.  Linux does not, so we must force it.
 *
 * This function should only need to be called from linux_connect()
 * and linux_bind().
 */
static int
linux_getsockaddr(struct sockaddr **namp, struct sockaddr *uaddr, size_t len)
{
	struct sockaddr *sa;
	uint16_t family;	/* XXX: must match Linux sockaddr */
	int error;
	int sa_len;

	*namp = NULL;

	if (len > SOCK_MAXADDRLEN)
		return ENAMETOOLONG;
	error = copyin(uaddr, &family, sizeof(family));
	if (error)
		return (error);

	/*
	 * Force the sa_len field to match the address family.
	 */
	switch (family) {
	case AF_INET:
		sa_len = sizeof(struct sockaddr_in);
		break;
	case AF_INET6:
		sa_len = sizeof(struct sockaddr_in6);
		break;
	default:
		/*
		 * This is the default behavior of the old
		 * linux_to_bsd_namelen() function.  NOTE!  The
		 * minimum length we allocate must cover sa->sa_len and
		 * sa->sa_family.
		 */
		sa_len = offsetof(struct sockaddr, sa_data[0]);
		if (sa_len < len)
			sa_len = len;
		break;
	}

	sa = kmalloc(sa_len, M_SONAME, M_WAITOK);
	error = copyin(uaddr, sa, sa_len);
	if (error) {
		kfree(sa, M_SONAME);
	} else {
		/*
		 * Convert to the 4.4BSD sockaddr structure.
		 */
		sa->sa_family = *(sa_family_t *)sa;
		sa->sa_len = sa_len;
		*namp = sa;
	}

	return (error);
}

/*
 * Transform a 4.4BSD sockaddr structure into a Linux sockaddr structure
 * and copy it out to a user address.
 */
static int
linux_copyout_sockaddr(struct sockaddr *sa, struct sockaddr *uaddr, int sa_len)
{
	int error;

	if (sa_len < (int)sizeof(u_short))
		return (EINVAL);

	*(u_short *)sa = sa->sa_family;
	error = copyout(sa, uaddr, sa_len);

	return (error);
}
 
static int
linux_to_bsd_domain(int domain)
{

	switch (domain) {
	case LINUX_AF_UNSPEC:
		return (AF_UNSPEC);
	case LINUX_AF_UNIX:
		return (AF_LOCAL);
	case LINUX_AF_INET:
		return (AF_INET);
	case LINUX_AF_AX25:
		return (AF_CCITT);
	case LINUX_AF_IPX:
		return (AF_IPX);
	}
	return (-1);
}

static int
linux_to_bsd_sockopt_level(int level)
{

	switch (level) {
	case LINUX_SOL_SOCKET:
		return (SOL_SOCKET);
	}
	return (level);
}

static int
linux_to_bsd_ip_sockopt(int opt)
{

	switch (opt) {
	case LINUX_IP_TOS:
		return (IP_TOS);
	case LINUX_IP_TTL:
		return (IP_TTL);
	case LINUX_IP_OPTIONS:
		return (IP_OPTIONS);
	case LINUX_IP_MULTICAST_IF:
		return (IP_MULTICAST_IF);
	case LINUX_IP_MULTICAST_TTL:
		return (IP_MULTICAST_TTL);
	case LINUX_IP_MULTICAST_LOOP:
		return (IP_MULTICAST_LOOP);
	case LINUX_IP_ADD_MEMBERSHIP:
		return (IP_ADD_MEMBERSHIP);
	case LINUX_IP_DROP_MEMBERSHIP:
		return (IP_DROP_MEMBERSHIP);
	case LINUX_IP_HDRINCL:
		return (IP_HDRINCL);
	}
	return (-1);
}

static int
linux_to_bsd_so_sockopt(int opt)
{

	switch (opt) {
	case LINUX_SO_DEBUG:
		return (SO_DEBUG);
	case LINUX_SO_REUSEADDR:
		return (SO_REUSEADDR);
	case LINUX_SO_TYPE:
		return (SO_TYPE);
	case LINUX_SO_ERROR:
		return (SO_ERROR);
	case LINUX_SO_DONTROUTE:
		return (SO_DONTROUTE);
	case LINUX_SO_BROADCAST:
		return (SO_BROADCAST);
	case LINUX_SO_SNDBUF:
		return (SO_SNDBUF);
	case LINUX_SO_RCVBUF:
		return (SO_RCVBUF);
	case LINUX_SO_KEEPALIVE:
		return (SO_KEEPALIVE);
	case LINUX_SO_OOBINLINE:
		return (SO_OOBINLINE);
	case LINUX_SO_LINGER:
		return (SO_LINGER);
	case LINUX_SO_PEERCRED:
		return (LOCAL_PEERCRED);
	}
	return (-1);
}

static int
linux_to_bsd_msg_flags(int flags)
{
	int ret_flags = 0;

	if (flags & LINUX_MSG_OOB)
		ret_flags |= MSG_OOB;
	if (flags & LINUX_MSG_PEEK)
		ret_flags |= MSG_PEEK;
	if (flags & LINUX_MSG_DONTROUTE)
		ret_flags |= MSG_DONTROUTE;
	if (flags & LINUX_MSG_CTRUNC)
		ret_flags |= MSG_CTRUNC;
	if (flags & LINUX_MSG_TRUNC)
		ret_flags |= MSG_TRUNC;
	if (flags & LINUX_MSG_DONTWAIT)
		ret_flags |= MSG_DONTWAIT;
	if (flags & LINUX_MSG_EOR)
		ret_flags |= MSG_EOR;
	if (flags & LINUX_MSG_WAITALL)
		ret_flags |= MSG_WAITALL;
#if 0 /* not handled */
	if (flags & LINUX_MSG_PROXY)
		;
	if (flags & LINUX_MSG_FIN)
		;
	if (flags & LINUX_MSG_SYN)
		;
	if (flags & LINUX_MSG_CONFIRM)
		;
	if (flags & LINUX_MSG_RST)
		;
	if (flags & LINUX_MSG_ERRQUEUE)
		;
	if (flags & LINUX_MSG_NOSIGNAL)
		;
#endif
	return ret_flags;
}

struct linux_socket_args {
	int domain;
	int type;
	int protocol;
};

static int
linux_socket(struct linux_socket_args *args, int *res)
{
	struct linux_socket_args linux_args;
	struct sockopt sopt;
	int error, domain, optval;

	error = copyin(args, &linux_args, sizeof(linux_args));
	if (error)
		return (error);

	domain = linux_to_bsd_domain(linux_args.domain);
	if (domain == -1)
		return (EINVAL);

	error = kern_socket(domain, linux_args.type, linux_args.protocol, res);

	/* Copy back the return value from socket() */
	if (error == 0 && linux_args.type == SOCK_RAW &&
	    (linux_args.protocol == IPPROTO_RAW || linux_args.protocol == 0) &&
	    linux_args.domain == AF_INET) {
		/* It's a raw IP socket: set the IP_HDRINCL option. */
		optval = 1;
		sopt.sopt_dir = SOPT_SET;
		sopt.sopt_level = IPPROTO_IP;
		sopt.sopt_name = IP_HDRINCL;
		sopt.sopt_val = &optval;
		sopt.sopt_valsize = sizeof(optval);
		sopt.sopt_td = NULL;

		/* We ignore any error returned by setsockopt() */
		kern_setsockopt(*res, &sopt);
	}

	return (error);
}

struct linux_bind_args {
	int s;
	struct sockaddr *name;
	int namelen;
};

static int
linux_bind(struct linux_bind_args *args, int *res)
{
	struct linux_bind_args linux_args;
	struct sockaddr *sa;
	int error;

	error = copyin(args, &linux_args, sizeof(linux_args));
	if (error)
		return (error);
	error = linux_getsockaddr(&sa, linux_args.name, linux_args.namelen);
	if (error)
		return (error);

	error = kern_bind(linux_args.s, sa);
	kfree(sa, M_SONAME);

	return (error);
}

struct linux_connect_args {
	int s;
	struct sockaddr * name;
	int namelen;
};

static int
linux_connect(struct linux_connect_args *args, int *res)
{
	struct thread *td = curthread;	/* XXX */
	struct proc *p = td->td_proc;
	struct linux_connect_args linux_args;
	struct sockaddr *sa;
	struct socket *so;
	struct file *fp;
	int error;

	KKASSERT(p);

	error = copyin(args, &linux_args, sizeof(linux_args));
	if (error)
		return (error);
	error = linux_getsockaddr(&sa, linux_args.name, linux_args.namelen);
	if (error)
		return (error);

	error = kern_connect(linux_args.s, 0, sa);
	kfree(sa, M_SONAME);

	if (error != EISCONN)
		return (error);

	/*
	 * Linux doesn't return EISCONN the first time it occurs,
	 * when on a non-blocking socket. Instead it returns the
	 * error getsockopt(SOL_SOCKET, SO_ERROR) would return on BSD.
	 */
	error = holdsock(p->p_fd, linux_args.s, &fp);
	if (error)
		return (error);
	error = EISCONN;
	if (fp->f_flag & FNONBLOCK) {
		so = (struct socket *)fp->f_data;
		if (so->so_emuldata == 0)
			error = so->so_error;
		so->so_emuldata = (void *)1;
	}
	fdrop(fp);
	return (error);
}

struct linux_listen_args {
	int s;
	int backlog;
};

static int
linux_listen(struct linux_listen_args *args, int *res)
{
	struct linux_listen_args linux_args;
	int error;

	error = copyin(args, &linux_args, sizeof(linux_args));
	if (error)
		return (error);

	error = kern_listen(linux_args.s, linux_args.backlog);

	return(error);
}

struct linux_accept_args {
	int s;
	struct sockaddr *addr;
	int *namelen;
};

static int
linux_accept(struct linux_accept_args *args, int *res)
{
	struct thread *td = curthread;
	struct linux_accept_args linux_args;
	struct sockaddr *sa = NULL;
	union fcntl_dat dat = { 0 };
	int error, sa_len;

	error = copyin(args, &linux_args, sizeof(linux_args));
	if (error)
		return (error);

	if (linux_args.addr) {
		error = copyin(linux_args.namelen, &sa_len, sizeof(sa_len));
		if (error)
			return (error);

		error = kern_accept(linux_args.s, 0, &sa, &sa_len, res);

		if (error) {
			/*
			 * Return a namelen of zero for older code which
			 * might ignore the return value from accept().
			 */
			sa_len = 0;
			copyout(&sa_len, linux_args.namelen,
			    sizeof(*linux_args.namelen));
		} else {
			error = linux_copyout_sockaddr(sa, linux_args.addr,
			    sa_len);
			if (error == 0) {
				error = copyout(&sa_len, linux_args.namelen,
				    sizeof(*linux_args.namelen));
			}
		}
		if (sa)
			kfree(sa, M_SONAME);
	} else {
		error = kern_accept(linux_args.s, 0, NULL, 0, res);
	}

	if (error)
		return (error);

	/*
	 * linux appears not to copy flags from the parent socket to the
	 * accepted one, so we must clear the flags in the new descriptor.
	 * Ignore any errors, because we already have an open fd.
	 */
	kern_fcntl(*res, F_SETFL, &dat, td->td_ucred);
	return (0);
}

struct linux_getsockname_args {
	int s;
	struct sockaddr *addr;
	int *namelen;
};

static int
linux_getsockname(struct linux_getsockname_args *args, int *res)
{
	struct linux_getsockname_args linux_args;
	struct sockaddr *sa = NULL;
	int error, sa_len;
         

	error = copyin(args, &linux_args, sizeof(linux_args));
	if (error)
		return (error);
	error = copyin(linux_args.namelen, &sa_len, sizeof(sa_len));
	if (error)
		return (error);

	error = kern_getsockname(linux_args.s, &sa, &sa_len);

	if (error == 0)
		error = linux_copyout_sockaddr(sa, linux_args.addr, sa_len);
	if (error == 0)
		error = copyout(&sa_len, linux_args.namelen,
		    sizeof(*linux_args.namelen));
	if (sa)
		kfree(sa, M_SONAME);
	return(error);
}

struct linux_getpeername_args {
	int s;
	struct sockaddr *addr;
	int *namelen;
};

static int
linux_getpeername(struct linux_getpeername_args *args, int *res)
{
	struct linux_getpeername_args linux_args;
	struct sockaddr *sa = NULL;
	int error, sa_len;

	error = copyin(args, &linux_args, sizeof(linux_args));
	if (error)
		return (error);
	error = copyin(linux_args.namelen, &sa_len, sizeof(sa_len));
	if (error)
		return (error);

	error = kern_getpeername(linux_args.s, &sa, &sa_len);

	if (error == 0)
		error = linux_copyout_sockaddr(sa, linux_args.addr, sa_len);
	if (error == 0)
		error = copyout(&sa_len, linux_args.namelen,
		    sizeof(*linux_args.namelen));
	if (sa)
		kfree(sa, M_SONAME);
	return(error);
}

struct linux_socketpair_args {
	int domain;
	int type;
	int protocol;
	int *rsv;
};

static int
linux_socketpair(struct linux_socketpair_args *args, int *res)
{
	struct linux_socketpair_args linux_args;
	int error, domain, sockv[2];

	error = copyin(args, &linux_args, sizeof(linux_args));
	if (error)
		return (error);

	domain = linux_to_bsd_domain(linux_args.domain);
	if (domain == -1)
		return (EINVAL);
	error = kern_socketpair(domain, linux_args.type, linux_args.protocol,
	    sockv);

	if (error == 0)
		error = copyout(sockv, linux_args.rsv, sizeof(sockv));
	return(error);
}

struct linux_send_args {
	int s;
	void *msg;
	int len;
	int flags;
};

static int
linux_send(struct linux_send_args *args, size_t *res)
{
	struct linux_send_args linux_args;
	struct thread *td = curthread;
	struct uio auio;
	struct iovec aiov;
	int error;

	error = copyin(args, &linux_args, sizeof(linux_args));
	if (error)
		return (error);

	aiov.iov_base = linux_args.msg;
	aiov.iov_len = linux_args.len;
	auio.uio_iov = &aiov;
	auio.uio_iovcnt = 1;
	auio.uio_offset = 0;
	auio.uio_resid = linux_args.len;
	auio.uio_segflg = UIO_USERSPACE;
	auio.uio_rw = UIO_WRITE;
	auio.uio_td = td;

	error = kern_sendmsg(linux_args.s, NULL, &auio, NULL,
			     linux_args.flags, res);

	return(error);
}

struct linux_recv_args {
	int s;
	void *msg;
	int len;
	int flags;
};

static int
linux_recv(struct linux_recv_args *args, size_t *res)
{
	struct linux_recv_args linux_args;
	struct thread *td = curthread;
	struct uio auio;
	struct iovec aiov;
	int error;

	error = copyin(args, &linux_args, sizeof(linux_args));
	if (error)
		return (error);

	aiov.iov_base = linux_args.msg;
	aiov.iov_len = linux_args.len;
	auio.uio_iov = &aiov;
	auio.uio_iovcnt = 1;
	auio.uio_offset = 0;
	auio.uio_resid = linux_args.len;
	auio.uio_segflg = UIO_USERSPACE;
	auio.uio_rw = UIO_READ;
	auio.uio_td = td;

	error = kern_recvmsg(linux_args.s, NULL, &auio, NULL,
			     &linux_args.flags, res);

	return(error);
}

struct linux_sendto_args {
	int s;
	void *msg;
	int len;
	int flags;
	struct sockaddr *to;
	int tolen;
};

static int
linux_sendto(struct linux_sendto_args *args, size_t *res)
{
	struct linux_sendto_args linux_args;
	struct thread *td = curthread;
	struct uio auio;
	struct iovec aiov;
	struct sockopt sopt;
	struct sockaddr *sa = NULL;
	caddr_t msg = NULL;
	int error, optval;

	error = copyin(args, &linux_args, sizeof(linux_args));
	if (error)
		return (error);

	if (linux_args.to) {
		error = linux_getsockaddr(&sa, linux_args.to,
		    linux_args.tolen);
		if (error)
			return (error);
	}

	/*
	 * Check to see if the IP_HDRINCL option is set.
	 */
	sopt.sopt_dir = SOPT_GET;
	sopt.sopt_level = IPPROTO_IP;
	sopt.sopt_name = IP_HDRINCL;
	sopt.sopt_val = &optval;
	sopt.sopt_valsize = sizeof(optval);
	sopt.sopt_td = NULL;

	if (kern_getsockopt(linux_args.s, &sopt) != 0)
		optval = 0;

	if (optval == 0) {
		/*
		 * IP_HDRINCL is not set.  Package the message as usual.
		 */
		aiov.iov_base = linux_args.msg;
		aiov.iov_len = linux_args.len;
		auio.uio_iov = &aiov;
		auio.uio_iovcnt = 1;
		auio.uio_offset = 0;
		auio.uio_resid = linux_args.len;
		auio.uio_segflg = UIO_USERSPACE;
		auio.uio_rw = UIO_WRITE;
		auio.uio_td = td;
	} else {
		/*
		 * IP_HDRINCL is set.  We must convert the beginning of
		 * the packet header so we can feed it to the BSD kernel.
		 */

		/*
		 * Check that the packet header is long enough to contain
		 * the fields of interest.  This relies on the fact that
		 * the fragment offset field comes after the length field.
		 */
		if (linux_args.len < offsetof(struct ip, ip_off))
			return (EINVAL);

		msg = kmalloc(linux_args.len, M_LINUX, M_WAITOK);
		error = copyin(linux_args.msg, msg, linux_args.len);
		if (error)
			goto cleanup;

		/* Fix the ip_len and ip_off fields.  */
		((struct ip *)msg)->ip_len = linux_args.len;
		((struct ip *)msg)->ip_off = ntohs(((struct ip *)msg)->ip_off);

		aiov.iov_base = msg;
		aiov.iov_len = linux_args.len;
		auio.uio_iov = &aiov;
		auio.uio_iovcnt = 1;
		auio.uio_offset = 0;
		auio.uio_resid = linux_args.len;
		auio.uio_segflg = UIO_SYSSPACE;
		auio.uio_rw = UIO_WRITE;
		auio.uio_td = td;
	}

	error = kern_sendmsg(linux_args.s, sa, &auio, NULL,
			     linux_args.flags, res);

cleanup:
	if (sa)
		kfree(sa, M_SONAME);
	if (msg)
		kfree(msg, M_LINUX);
	return(error);
}

struct linux_recvfrom_args {
	int s;
	void *buf;
	int len;
	int flags;
	struct sockaddr *from;
	int *fromlen;
};

static int
linux_recvfrom(struct linux_recvfrom_args *args, size_t *res)
{
	struct linux_recvfrom_args linux_args;
	struct thread *td = curthread;
	struct uio auio;
	struct iovec aiov;
	struct sockaddr *sa = NULL;
	int error, fromlen, flags;

	error = copyin(args, &linux_args, sizeof(linux_args));
	if (error)
		return (error);

	if (linux_args.from && linux_args.fromlen) {
		error = copyin(linux_args.fromlen, &fromlen, sizeof(fromlen));
		if (error)
			return (error);
		if (fromlen < 0)
			return (EINVAL);
	} else {
		fromlen = 0;
	}
	aiov.iov_base = linux_args.buf;
	aiov.iov_len = linux_args.len;
	auio.uio_iov = &aiov;
	auio.uio_iovcnt = 1;
	auio.uio_offset = 0;
	auio.uio_resid = linux_args.len;
	auio.uio_segflg = UIO_USERSPACE;
	auio.uio_rw = UIO_READ;
	auio.uio_td = td;

	flags = linux_to_bsd_msg_flags(linux_args.flags);

	error = kern_recvmsg(linux_args.s, linux_args.from ? &sa : NULL, &auio,
			     NULL, &flags, res);

	if (error == 0 && linux_args.from) {
		if (sa != NULL) {
			fromlen = MIN(fromlen, sa->sa_len);
			error = linux_copyout_sockaddr(sa, linux_args.from,
							fromlen);
		} else
			fromlen = 0;
		if (error == 0)
			copyout(&fromlen, linux_args.fromlen,
			    sizeof(fromlen));
	}
	if (sa)
		kfree(sa, M_SONAME);

	return(error);
}

struct linux_sendmsg_args {
	int s;
	struct msghdr *msg;
	int flags;
};

static int
linux_sendmsg(struct linux_sendmsg_args *args, size_t *res)
{
	struct linux_sendmsg_args linux_args;
	struct thread *td = curthread;
	struct msghdr msg;
	struct uio auio;
	struct iovec aiov[UIO_SMALLIOV], *iov = NULL;
	struct sockaddr *sa = NULL;
	struct mbuf *control = NULL;
	int error;

	error = copyin(args, &linux_args, sizeof(linux_args));
	if (error)
		return (error);

	error = copyin(linux_args.msg, &msg, sizeof(msg));
	if (error)
		return (error);

	/* 
	 * XXX: I'm not sure atm how this relates to dragonfly, but 
	 *	just in case, I put it in.
	 * Ping on linux does pass 0 in controllen which is forbidden
	 * by FreeBSD but seems to be ok on Linux. This needs some
	 * checking but now it lets ping work.
	 */
	if (msg.msg_control && msg.msg_controllen == 0)
		msg.msg_control = NULL;

	/*
	 * Conditionally copyin msg.msg_name.
	 */
	if (msg.msg_name) {
		error = linux_getsockaddr(&sa, msg.msg_name, msg.msg_namelen);
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
		if (msg.msg_controllen < sizeof(struct cmsghdr) ||
		    msg.msg_controllen > MLEN) {
			error = EINVAL;
			goto cleanup;
		}
		control = m_get(MB_WAIT, MT_CONTROL);
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
		 * Linux and BSD both support SCM_RIGHTS.  If a linux binary
		 * wants anything else with an option level of SOL_SOCKET,
		 * we don't support it.
		 */
		if (mtod(control, struct cmsghdr *)->cmsg_level ==
		    SOL_SOCKET &&
		    mtod(control, struct cmsghdr *)->cmsg_type !=
		    SCM_RIGHTS) {
			m_free(control);
			error = EINVAL;
			goto cleanup;
		}
	}

	error = kern_sendmsg(linux_args.s, sa, &auio, control,
			     linux_args.flags, res);

cleanup:
	iovec_free(&iov, aiov);
cleanup2:
	if (sa)
		kfree(sa, M_SONAME);
	return (error);
}

struct linux_recvmsg_args {
	int s;
	struct msghdr *msg;
	int flags;
};

static int
linux_recvmsg(struct linux_recvmsg_args *args, size_t *res)
{
	struct linux_recvmsg_args linux_args;
	struct thread *td = curthread;
	struct msghdr msg;
	struct uio auio;
	struct iovec aiov[UIO_SMALLIOV], *iov = NULL;
	struct mbuf *m, *control = NULL;
	struct sockaddr *sa = NULL;
	caddr_t ctlbuf;
	socklen_t *ufromlenp, *ucontrollenp;
	int error, fromlen, controllen, len, flags, *uflagsp;

	error = copyin(args, &linux_args, sizeof(linux_args));
	if (error)
		return (error);

	error = copyin(linux_args.msg, &msg, sizeof(struct msghdr));
	if (error)
		return (error);

	if (msg.msg_name && msg.msg_namelen < 0)
		return (EINVAL);
	if (msg.msg_control && msg.msg_controllen < 0)
		return (EINVAL);

	ufromlenp = (socklen_t *)((caddr_t)linux_args.msg +
	    offsetof(struct msghdr, msg_namelen));
	ucontrollenp = (socklen_t *)((caddr_t)linux_args.msg +
	    offsetof(struct msghdr, msg_controllen));
	uflagsp = (int *)((caddr_t)linux_args.msg +
	    offsetof(struct msghdr, msg_flags));

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

	flags = linux_to_bsd_msg_flags(linux_args.flags);

	error = kern_recvmsg(linux_args.s, msg.msg_name ? &sa : NULL, &auio,
			     msg.msg_control ? &control : NULL, &flags, res);

	/*
	 * Copyout msg.msg_name and msg.msg_namelen.
	 */
	if (error == 0 && msg.msg_name) {
		if (sa != NULL) {
			fromlen = MIN(msg.msg_namelen, sa->sa_len);
			error = linux_copyout_sockaddr(sa, msg.msg_name,
							fromlen);
		} else
			fromlen = 0;
		if (error == 0)
			error = copyout(&fromlen, ufromlenp,
			    sizeof(*ufromlenp));
	}

	/*
	 * Copyout msg.msg_control and msg.msg_controllen.
	 */
	if (error == 0 && msg.msg_control) {
		/*
		 * Linux and BSD both support SCM_RIGHTS.  If a linux binary
		 * wants anything else with an option level of SOL_SOCKET,
		 * we don't support it.
		 */
		if (mtod((struct mbuf *)msg.msg_control,
		    struct cmsghdr *)->cmsg_level == SOL_SOCKET &&
		    mtod((struct mbuf *)msg.msg_control,
		    struct cmsghdr *)->cmsg_type != SCM_RIGHTS) {
			error = EINVAL;
			goto cleanup;
		}

		len = msg.msg_controllen;
		m = control;
		ctlbuf = (caddr_t)msg.msg_control;

		while (m && len > 0) {
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

struct linux_shutdown_args {
	int s;
	int how;
};

static int
linux_shutdown(struct linux_shutdown_args *args, int *res)
{
	struct linux_shutdown_args linux_args;
	int error;

	error = copyin(args, &linux_args, sizeof(linux_args));
	if (error)
		return (error);

	error = kern_shutdown(linux_args.s, linux_args.how);

	return (error);
}

struct linux_setsockopt_args {
	int s;
	int level;
	int optname;
	void *optval;
	int optlen;
};

static int
linux_setsockopt(struct linux_setsockopt_args *args, int *res)
{
	struct linux_setsockopt_args linux_args;
	struct thread *td = curthread;
	struct sockopt sopt;
	l_timeval linux_tv;
	struct timeval tv;
	int error, name, level;

	error = copyin(args, &linux_args, sizeof(linux_args));
	if (error)
		return (error);

	level = linux_to_bsd_sockopt_level(linux_args.level);
	switch (level) {
	case SOL_SOCKET:
		name = linux_to_bsd_so_sockopt(linux_args.optname);
		switch (name) {
		case SO_RCVTIMEO:
			/* FALLTHROUGH */
		case SO_SNDTIMEO:
			error = copyin(linux_args.optval, &linux_tv,
			    sizeof(linux_tv));
			if (error)
				return (error);
			tv.tv_sec = linux_tv.tv_sec;
			tv.tv_usec = linux_tv.tv_usec;
			sopt.sopt_dir = SOPT_SET;
			sopt.sopt_level = level;
			sopt.sopt_name = name;
			sopt.sopt_valsize = sizeof(tv);
			sopt.sopt_val = &tv;
			sopt.sopt_td = td;
			return (kern_setsockopt(linux_args.s, &sopt));
			/* NOTREACHED */
			break;
		default:
			break;
		}
		break;
	case IPPROTO_IP:
		name = linux_to_bsd_ip_sockopt(linux_args.optname);
		break;
	case IPPROTO_TCP:
		/* Linux TCP option values match BSD's */
		name = linux_args.optname;
		break;
	default:
		name = -1;
		break;
	}
	if (name == -1)
		return (ENOPROTOOPT);

	if (linux_args.optlen < 0 || linux_args.optlen > SOMAXOPT_SIZE)
		return (EINVAL);
	if (linux_args.optval != NULL && linux_args.optlen == 0)
		return (EINVAL);
	if (linux_args.optval == NULL && linux_args.optlen != 0)
		return (EFAULT);

	sopt.sopt_dir = SOPT_SET;
	sopt.sopt_level = level;
	sopt.sopt_name = name;
	sopt.sopt_valsize = linux_args.optlen;
	sopt.sopt_td = td;

	if (linux_args.optval) {
		sopt.sopt_val = kmalloc(sopt.sopt_valsize, M_TEMP, M_WAITOK);
		error = copyin(linux_args.optval, sopt.sopt_val, sopt.sopt_valsize);
		if (error)
			goto out;
	} else {
		sopt.sopt_val = NULL;
	}
	error = kern_setsockopt(linux_args.s, &sopt);
	if (error)
		goto out;
	if (linux_args.optval)
		error = copyout(sopt.sopt_val, linux_args.optval,
				sopt.sopt_valsize);
out:
	if (linux_args.optval)
		kfree(sopt.sopt_val, M_TEMP);
	return(error);
}

struct linux_getsockopt_args {
	int s;
	int level;
	int optname;
	void *optval;
	int *optlen;
};

static int
linux_getsockopt(struct linux_getsockopt_args *args, int *res)
{
	struct linux_getsockopt_args linux_args;
	struct thread *td = curthread;
	struct sockopt sopt;
	l_timeval linux_tv;
	struct timeval tv;
	struct xucred xu;
	struct l_ucred lxu;
	int error, name, valsize, level;

	error = copyin(args, &linux_args, sizeof(linux_args));
	if (error)
		return (error);

	if (linux_args.optlen) {
		error = copyin(linux_args.optlen, &valsize, sizeof(valsize));
		if (error)
			return (error);
	} else {
		valsize = 0;
	}

	if (valsize < 0 || valsize > SOMAXOPT_SIZE)
		return (EINVAL);
	if (linux_args.optval != NULL && valsize == 0)
		return (EFAULT);
	if (linux_args.optval == NULL && valsize != 0)
		return (EFAULT);
		
	level = linux_to_bsd_sockopt_level(linux_args.level);
	switch (level) {
	case SOL_SOCKET:
		name = linux_to_bsd_so_sockopt(linux_args.optname);
		switch (name) {
		case SO_RCVTIMEO:
			/* FALLTHROUGH */
		case SO_SNDTIMEO:
			sopt.sopt_dir = SOPT_GET;
			sopt.sopt_level = level;
			sopt.sopt_name = name;
			sopt.sopt_valsize = sizeof(tv);
			sopt.sopt_td = td;
			sopt.sopt_val = &tv;
			error = kern_getsockopt(linux_args.s, &sopt);
			if (error)
				return (error);
			linux_tv.tv_sec = tv.tv_sec;
			linux_tv.tv_usec = tv.tv_usec;
			return (copyout(&linux_tv, linux_args.optval,
			    sizeof(linux_tv)));
			/* NOTREACHED */
			break;
		case LOCAL_PEERCRED:
			if (valsize != sizeof(lxu))
				return (EINVAL);
			sopt.sopt_dir = SOPT_GET;
			sopt.sopt_level = level;
			sopt.sopt_name = name;
			sopt.sopt_valsize = sizeof(xu);
			sopt.sopt_td = td;
			sopt.sopt_val = &xu;
			error = kern_getsockopt(linux_args.s, &sopt);
			if (error)
				return (error);
			/*
			 * XXX Use 0 for pid as the FreeBSD does not cache peer pid.
			 */
			lxu.pid = 0;
			lxu.uid = xu.cr_uid;
			lxu.gid = xu.cr_gid;
			return (copyout(&lxu, linux_args.optval, sizeof(lxu)));
			/* NOTREACHED */
			break;
		default:
			break;
		}
		break;
	case IPPROTO_IP:
		name = linux_to_bsd_ip_sockopt(linux_args.optname);
		break;
	case IPPROTO_TCP:
		/* Linux TCP option values match BSD's */
		name = linux_args.optname;
		break;
	default:
		name = -1;
		break;
	}
	if (name == -1)
		return (EOPNOTSUPP);



	sopt.sopt_dir = SOPT_GET;
	sopt.sopt_level = level;
	sopt.sopt_name = name;
	sopt.sopt_valsize = valsize;
	sopt.sopt_td = td;

	if (linux_args.optval) {
		sopt.sopt_val = kmalloc(sopt.sopt_valsize, M_TEMP, M_WAITOK);
		error = copyin(linux_args.optval, sopt.sopt_val, sopt.sopt_valsize);
		if (error)
			goto out;
	} else {
		sopt.sopt_val = NULL;
	}
	error = kern_getsockopt(linux_args.s, &sopt);
	if (error) {
		if (error == EINVAL)
			error = ENOPROTOOPT;
		goto out;
	}
	valsize = sopt.sopt_valsize;
	error = copyout(&valsize, linux_args.optlen, sizeof(valsize));
	if (error)
		goto out;
	if (linux_args.optval)
		error = copyout(sopt.sopt_val, linux_args.optval, sopt.sopt_valsize);
out:
	if (linux_args.optval)
		kfree(sopt.sopt_val, M_TEMP);
	return(error);
}

/*
 * MPALMOSTSAFE
 */
int
sys_linux_socketcall(struct linux_socketcall_args *args)
{
	void *arg = (void *)args->args;
	int error;

	get_mplock();

	switch (args->what) {
	case LINUX_SOCKET:
		error = linux_socket(arg, &args->sysmsg_result);
		break;
	case LINUX_BIND:
		error = linux_bind(arg, &args->sysmsg_result);
		break;
	case LINUX_CONNECT:
		error = linux_connect(arg, &args->sysmsg_result);
		break;
	case LINUX_LISTEN:
		error = linux_listen(arg, &args->sysmsg_result);
		break;
	case LINUX_ACCEPT:
		error = linux_accept(arg, &args->sysmsg_result);
		break;
	case LINUX_GETSOCKNAME:
		error = linux_getsockname(arg, &args->sysmsg_result);
		break;
	case LINUX_GETPEERNAME:
		error = linux_getpeername(arg, &args->sysmsg_result);
		break;
	case LINUX_SOCKETPAIR:
		error = linux_socketpair(arg, &args->sysmsg_result);
		break;
	case LINUX_SEND:
		error = linux_send(arg, &args->sysmsg_szresult);
		break;
	case LINUX_RECV:
		error = linux_recv(arg, &args->sysmsg_szresult);
		break;
	case LINUX_SENDTO:
		error = linux_sendto(arg, &args->sysmsg_szresult);
		break;
	case LINUX_RECVFROM:
		error = linux_recvfrom(arg, &args->sysmsg_szresult);
		break;
	case LINUX_SHUTDOWN:
		error = linux_shutdown(arg, &args->sysmsg_result);
		break;
	case LINUX_SETSOCKOPT:
		error = linux_setsockopt(arg, &args->sysmsg_result);
		break;
	case LINUX_GETSOCKOPT:
		error = linux_getsockopt(arg, &args->sysmsg_result);
		break;
	case LINUX_SENDMSG:
		error = linux_sendmsg(arg, &args->sysmsg_szresult);
		break;
	case LINUX_RECVMSG:
		error = linux_recvmsg(arg, &args->sysmsg_szresult);
		break;
	default:
		uprintf("LINUX: 'socket' typ=%d not implemented\n",
			args->what);
		error = ENOSYS;
		break;
	}
	rel_mplock();

	return (error);
}
