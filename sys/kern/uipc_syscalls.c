/*
 * Copyright (c) 1982, 1986, 1989, 1990, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * sendfile(2) and related extensions:
 * Copyright (c) 1998, David Greenman. All rights reserved. 
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
 *	@(#)uipc_syscalls.c	8.4 (Berkeley) 2/21/94
 * $FreeBSD: src/sys/kern/uipc_syscalls.c,v 1.65.2.17 2003/04/04 17:11:16 tegge Exp $
 */

#include "opt_ktrace.h"
#include "opt_sctp.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/sysproto.h>
#include <sys/malloc.h>
#include <sys/filedesc.h>
#include <sys/event.h>
#include <sys/proc.h>
#include <sys/fcntl.h>
#include <sys/file.h>
#include <sys/filio.h>
#include <sys/kern_syscall.h>
#include <sys/mbuf.h>
#include <sys/protosw.h>
#include <sys/sfbuf.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/socketops.h>
#include <sys/uio.h>
#include <sys/vnode.h>
#include <sys/lock.h>
#include <sys/mount.h>
#ifdef KTRACE
#include <sys/ktrace.h>
#endif
#include <vm/vm.h>
#include <vm/vm_object.h>
#include <vm/vm_page.h>
#include <vm/vm_pageout.h>
#include <vm/vm_kern.h>
#include <vm/vm_extern.h>
#include <sys/file2.h>
#include <sys/signalvar.h>
#include <sys/serialize.h>

#include <sys/thread2.h>
#include <sys/msgport2.h>
#include <sys/socketvar2.h>
#include <net/netmsg2.h>
#include <vm/vm_page2.h>

#ifdef SCTP
#include <netinet/sctp_peeloff.h>
#endif /* SCTP */

extern int use_soaccept_pred_fast;
extern int use_sendfile_async;
extern int use_soconnect_async;

/*
 * System call interface to the socket abstraction.
 */

extern	struct fileops socketops;

/*
 * socket_args(int domain, int type, int protocol)
 */
int
kern_socket(int domain, int type, int protocol, int *res)
{
	struct thread *td = curthread;
	struct filedesc *fdp = td->td_proc->p_fd;
	struct socket *so;
	struct file *fp;
	int fd, error;

	KKASSERT(td->td_lwp);

	error = falloc(td->td_lwp, &fp, &fd);
	if (error)
		return (error);
	error = socreate(domain, &so, type, protocol, td);
	if (error) {
		fsetfd(fdp, NULL, fd);
	} else {
		fp->f_type = DTYPE_SOCKET;
		fp->f_flag = FREAD | FWRITE;
		fp->f_ops = &socketops;
		fp->f_data = so;
		*res = fd;
		fsetfd(fdp, fp, fd);
	}
	fdrop(fp);
	return (error);
}

/*
 * MPALMOSTSAFE
 */
int
sys_socket(struct socket_args *uap)
{
	int error;

	error = kern_socket(uap->domain, uap->type, uap->protocol,
			    &uap->sysmsg_iresult);

	return (error);
}

int
kern_bind(int s, struct sockaddr *sa)
{
	struct thread *td = curthread;
	struct proc *p = td->td_proc;
	struct file *fp;
	int error;

	KKASSERT(p);
	error = holdsock(p->p_fd, s, &fp);
	if (error)
		return (error);
	error = sobind((struct socket *)fp->f_data, sa, td);
	fdrop(fp);
	return (error);
}

/*
 * bind_args(int s, caddr_t name, int namelen)
 *
 * MPALMOSTSAFE
 */
int
sys_bind(struct bind_args *uap)
{
	struct sockaddr *sa;
	int error;

	error = getsockaddr(&sa, uap->name, uap->namelen);
	if (error)
		return (error);
	error = kern_bind(uap->s, sa);
	kfree(sa, M_SONAME);

	return (error);
}

int
kern_listen(int s, int backlog)
{
	struct thread *td = curthread;
	struct proc *p = td->td_proc;
	struct file *fp;
	int error;

	KKASSERT(p);
	error = holdsock(p->p_fd, s, &fp);
	if (error)
		return (error);
	error = solisten((struct socket *)fp->f_data, backlog, td);
	fdrop(fp);
	return(error);
}

/*
 * listen_args(int s, int backlog)
 *
 * MPALMOSTSAFE
 */
int
sys_listen(struct listen_args *uap)
{
	int error;

	error = kern_listen(uap->s, uap->backlog);
	return (error);
}

/*
 * Returns the accepted socket as well.
 *
 * NOTE!  The sockets sitting on so_comp/so_incomp might have 0 refs, the
 *	  pool token is absolutely required to avoid a sofree() race,
 *	  as well as to avoid tailq handling races.
 */
static boolean_t
soaccept_predicate(struct netmsg_so_notify *msg)
{
	struct socket *head = msg->base.nm_so;
	struct socket *so;

	if (head->so_error != 0) {
		msg->base.lmsg.ms_error = head->so_error;
		return (TRUE);
	}
	lwkt_getpooltoken(head);
	if (!TAILQ_EMPTY(&head->so_comp)) {
		/* Abuse nm_so field as copy in/copy out parameter. XXX JH */
		so = TAILQ_FIRST(&head->so_comp);
		TAILQ_REMOVE(&head->so_comp, so, so_list);
		head->so_qlen--;
		soclrstate(so, SS_COMP);
		so->so_head = NULL;
		soreference(so);

		lwkt_relpooltoken(head);

		msg->base.lmsg.ms_error = 0;
		msg->base.nm_so = so;
		return (TRUE);
	}
	lwkt_relpooltoken(head);
	if (head->so_state & SS_CANTRCVMORE) {
		msg->base.lmsg.ms_error = ECONNABORTED;
		return (TRUE);
	}
	if (msg->nm_fflags & FNONBLOCK) {
		msg->base.lmsg.ms_error = EWOULDBLOCK;
		return (TRUE);
	}

	return (FALSE);
}

/*
 * The second argument to kern_accept() is a handle to a struct sockaddr.
 * This allows kern_accept() to return a pointer to an allocated struct
 * sockaddr which must be freed later with FREE().  The caller must
 * initialize *name to NULL.
 */
int
kern_accept(int s, int fflags, struct sockaddr **name, int *namelen, int *res)
{
	struct thread *td = curthread;
	struct filedesc *fdp = td->td_proc->p_fd;
	struct file *lfp = NULL;
	struct file *nfp = NULL;
	struct sockaddr *sa;
	struct socket *head, *so;
	struct netmsg_so_notify msg;
	int fd;
	u_int fflag;		/* type must match fp->f_flag */
	int error, tmp;

	*res = -1;
	if (name && namelen && *namelen < 0)
		return (EINVAL);

	error = holdsock(td->td_proc->p_fd, s, &lfp);
	if (error)
		return (error);

	error = falloc(td->td_lwp, &nfp, &fd);
	if (error) {		/* Probably ran out of file descriptors. */
		fdrop(lfp);
		return (error);
	}
	head = (struct socket *)lfp->f_data;
	if ((head->so_options & SO_ACCEPTCONN) == 0) {
		error = EINVAL;
		goto done;
	}

	if (fflags & O_FBLOCKING)
		fflags |= lfp->f_flag & ~FNONBLOCK;
	else if (fflags & O_FNONBLOCKING)
		fflags |= lfp->f_flag | FNONBLOCK;
	else
		fflags = lfp->f_flag;

	if (use_soaccept_pred_fast) {
		boolean_t pred;

		/* Initialize necessary parts for soaccept_predicate() */
		netmsg_init(&msg.base, head, &netisr_apanic_rport, 0, NULL);
		msg.nm_fflags = fflags;

		lwkt_getpooltoken(head);
		pred = soaccept_predicate(&msg);
		lwkt_relpooltoken(head);

		if (pred) {
			error = msg.base.lmsg.ms_error;
			if (error)
				goto done;
			else
				goto accepted;
		}
	}

	/* optimize for uniprocessor case later XXX JH */
	netmsg_init_abortable(&msg.base, head, &curthread->td_msgport,
			      0, netmsg_so_notify, netmsg_so_notify_doabort);
	msg.nm_predicate = soaccept_predicate;
	msg.nm_fflags = fflags;
	msg.nm_etype = NM_REVENT;
	error = lwkt_domsg(head->so_port, &msg.base.lmsg, PCATCH);
	if (error)
		goto done;

accepted:
	/*
	 * At this point we have the connection that's ready to be accepted.
	 *
	 * NOTE! soaccept_predicate() ref'd so for us, and soaccept() expects
	 * 	 to eat the ref and turn it into a descriptor.
	 */
	so = msg.base.nm_so;

	fflag = lfp->f_flag;

	/* connection has been removed from the listen queue */
	KNOTE(&head->so_rcv.ssb_kq.ki_note, 0);

	if (head->so_sigio != NULL)
		fsetown(fgetown(&head->so_sigio), &so->so_sigio);

	nfp->f_type = DTYPE_SOCKET;
	nfp->f_flag = fflag;
	nfp->f_ops = &socketops;
	nfp->f_data = so;
	/* Sync socket nonblocking/async state with file flags */
	tmp = fflag & FNONBLOCK;
	fo_ioctl(nfp, FIONBIO, (caddr_t)&tmp, td->td_ucred, NULL);
	tmp = fflag & FASYNC;
	fo_ioctl(nfp, FIOASYNC, (caddr_t)&tmp, td->td_ucred, NULL);

	sa = NULL;
	if (so->so_faddr != NULL) {
		sa = so->so_faddr;
		so->so_faddr = NULL;

		soaccept_generic(so);
		error = 0;
	} else {
		error = soaccept(so, &sa);
	}

	/*
	 * Set the returned name and namelen as applicable.  Set the returned
	 * namelen to 0 for older code which might ignore the return value
	 * from accept.
	 */
	if (error == 0) {
		if (sa && name && namelen) {
			if (*namelen > sa->sa_len)
				*namelen = sa->sa_len;
			*name = sa;
		} else {
			if (sa)
				kfree(sa, M_SONAME);
		}
	}

done:
	/*
	 * If an error occured clear the reserved descriptor, else associate
	 * nfp with it.
	 *
	 * Note that *res is normally ignored if an error is returned but
	 * a syscall message will still have access to the result code.
	 */
	if (error) {
		fsetfd(fdp, NULL, fd);
	} else {
		*res = fd;
		fsetfd(fdp, nfp, fd);
	}
	fdrop(nfp);
	fdrop(lfp);
	return (error);
}

/*
 * accept(int s, caddr_t name, int *anamelen)
 *
 * MPALMOSTSAFE
 */
int
sys_accept(struct accept_args *uap)
{
	struct sockaddr *sa = NULL;
	int sa_len;
	int error;

	if (uap->name) {
		error = copyin(uap->anamelen, &sa_len, sizeof(sa_len));
		if (error)
			return (error);

		error = kern_accept(uap->s, 0, &sa, &sa_len,
				    &uap->sysmsg_iresult);

		if (error == 0)
			error = copyout(sa, uap->name, sa_len);
		if (error == 0) {
			error = copyout(&sa_len, uap->anamelen,
			    sizeof(*uap->anamelen));
		}
		if (sa)
			kfree(sa, M_SONAME);
	} else {
		error = kern_accept(uap->s, 0, NULL, 0,
				    &uap->sysmsg_iresult);
	}
	return (error);
}

/*
 * extaccept(int s, int fflags, caddr_t name, int *anamelen)
 *
 * MPALMOSTSAFE
 */
int
sys_extaccept(struct extaccept_args *uap)
{
	struct sockaddr *sa = NULL;
	int sa_len;
	int error;
	int fflags = uap->flags & O_FMASK;

	if (uap->name) {
		error = copyin(uap->anamelen, &sa_len, sizeof(sa_len));
		if (error)
			return (error);

		error = kern_accept(uap->s, fflags, &sa, &sa_len,
				    &uap->sysmsg_iresult);

		if (error == 0)
			error = copyout(sa, uap->name, sa_len);
		if (error == 0) {
			error = copyout(&sa_len, uap->anamelen,
			    sizeof(*uap->anamelen));
		}
		if (sa)
			kfree(sa, M_SONAME);
	} else {
		error = kern_accept(uap->s, fflags, NULL, 0,
				    &uap->sysmsg_iresult);
	}
	return (error);
}


/*
 * Returns TRUE if predicate satisfied.
 */
static boolean_t
soconnected_predicate(struct netmsg_so_notify *msg)
{
	struct socket *so = msg->base.nm_so;

	/* check predicate */
	if (!(so->so_state & SS_ISCONNECTING) || so->so_error != 0) {
		msg->base.lmsg.ms_error = so->so_error;
		return (TRUE);
	}

	return (FALSE);
}

int
kern_connect(int s, int fflags, struct sockaddr *sa)
{
	struct thread *td = curthread;
	struct proc *p = td->td_proc;
	struct file *fp;
	struct socket *so;
	int error, interrupted = 0;

	error = holdsock(p->p_fd, s, &fp);
	if (error)
		return (error);
	so = (struct socket *)fp->f_data;

	if (fflags & O_FBLOCKING)
		/* fflags &= ~FNONBLOCK; */;
	else if (fflags & O_FNONBLOCKING)
		fflags |= FNONBLOCK;
	else
		fflags = fp->f_flag;

	if (so->so_state & SS_ISCONNECTING) {
		error = EALREADY;
		goto done;
	}
	error = soconnect(so, sa, td, use_soconnect_async ? FALSE : TRUE);
	if (error)
		goto bad;
	if ((fflags & FNONBLOCK) && (so->so_state & SS_ISCONNECTING)) {
		error = EINPROGRESS;
		goto done;
	}
	if ((so->so_state & SS_ISCONNECTING) && so->so_error == 0) {
		struct netmsg_so_notify msg;

		netmsg_init_abortable(&msg.base, so,
				      &curthread->td_msgport,
				      0,
				      netmsg_so_notify,
				      netmsg_so_notify_doabort);
		msg.nm_predicate = soconnected_predicate;
		msg.nm_etype = NM_REVENT;
		error = lwkt_domsg(so->so_port, &msg.base.lmsg, PCATCH);
		if (error == EINTR || error == ERESTART)
			interrupted = 1;
	}
	if (error == 0) {
		error = so->so_error;
		so->so_error = 0;
	}
bad:
	if (!interrupted)
		soclrstate(so, SS_ISCONNECTING);
	if (error == ERESTART)
		error = EINTR;
done:
	fdrop(fp);
	return (error);
}

/*
 * connect_args(int s, caddr_t name, int namelen)
 *
 * MPALMOSTSAFE
 */
int
sys_connect(struct connect_args *uap)
{
	struct sockaddr *sa;
	int error;

	error = getsockaddr(&sa, uap->name, uap->namelen);
	if (error)
		return (error);
	error = kern_connect(uap->s, 0, sa);
	kfree(sa, M_SONAME);

	return (error);
}

/*
 * connect_args(int s, int fflags, caddr_t name, int namelen)
 *
 * MPALMOSTSAFE
 */
int
sys_extconnect(struct extconnect_args *uap)
{
	struct sockaddr *sa;
	int error;
	int fflags = uap->flags & O_FMASK;

	error = getsockaddr(&sa, uap->name, uap->namelen);
	if (error)
		return (error);
	error = kern_connect(uap->s, fflags, sa);
	kfree(sa, M_SONAME);

	return (error);
}

int
kern_socketpair(int domain, int type, int protocol, int *sv)
{
	struct thread *td = curthread;
	struct filedesc *fdp;
	struct file *fp1, *fp2;
	struct socket *so1, *so2;
	int fd1, fd2, error;

	fdp = td->td_proc->p_fd;
	error = socreate(domain, &so1, type, protocol, td);
	if (error)
		return (error);
	error = socreate(domain, &so2, type, protocol, td);
	if (error)
		goto free1;
	error = falloc(td->td_lwp, &fp1, &fd1);
	if (error)
		goto free2;
	sv[0] = fd1;
	fp1->f_data = so1;
	error = falloc(td->td_lwp, &fp2, &fd2);
	if (error)
		goto free3;
	fp2->f_data = so2;
	sv[1] = fd2;
	error = soconnect2(so1, so2);
	if (error)
		goto free4;
	if (type == SOCK_DGRAM) {
		/*
		 * Datagram socket connection is asymmetric.
		 */
		 error = soconnect2(so2, so1);
		 if (error)
			goto free4;
	}
	fp1->f_type = fp2->f_type = DTYPE_SOCKET;
	fp1->f_flag = fp2->f_flag = FREAD|FWRITE;
	fp1->f_ops = fp2->f_ops = &socketops;
	fsetfd(fdp, fp1, fd1);
	fsetfd(fdp, fp2, fd2);
	fdrop(fp1);
	fdrop(fp2);
	return (error);
free4:
	fsetfd(fdp, NULL, fd2);
	fdrop(fp2);
free3:
	fsetfd(fdp, NULL, fd1);
	fdrop(fp1);
free2:
	(void)soclose(so2, 0);
free1:
	(void)soclose(so1, 0);
	return (error);
}

/*
 * socketpair(int domain, int type, int protocol, int *rsv)
 */
int
sys_socketpair(struct socketpair_args *uap)
{
	int error, sockv[2];

	error = kern_socketpair(uap->domain, uap->type, uap->protocol, sockv);

	if (error == 0) {
		error = copyout(sockv, uap->rsv, sizeof(sockv));

		if (error != 0) {
			kern_close(sockv[0]);
			kern_close(sockv[1]);
		}
	}

	return (error);
}

int
kern_sendmsg(int s, struct sockaddr *sa, struct uio *auio,
	     struct mbuf *control, int flags, size_t *res)
{
	struct thread *td = curthread;
	struct lwp *lp = td->td_lwp;
	struct proc *p = td->td_proc;
	struct file *fp;
	size_t len;
	int error;
	struct socket *so;
#ifdef KTRACE
	struct iovec *ktriov = NULL;
	struct uio ktruio;
#endif

	error = holdsock(p->p_fd, s, &fp);
	if (error)
		return (error);
#ifdef KTRACE
	if (KTRPOINT(td, KTR_GENIO)) {
		int iovlen = auio->uio_iovcnt * sizeof (struct iovec);

		ktriov = kmalloc(iovlen, M_TEMP, M_WAITOK);
		bcopy((caddr_t)auio->uio_iov, (caddr_t)ktriov, iovlen);
		ktruio = *auio;
	}
#endif
	len = auio->uio_resid;
	so = (struct socket *)fp->f_data;
	if ((flags & (MSG_FNONBLOCKING|MSG_FBLOCKING)) == 0) {
		if (fp->f_flag & FNONBLOCK)
			flags |= MSG_FNONBLOCKING;
	}
	error = so_pru_sosend(so, sa, auio, NULL, control, flags, td);
	if (error) {
		if (auio->uio_resid != len && (error == ERESTART ||
		    error == EINTR || error == EWOULDBLOCK))
			error = 0;
		if (error == EPIPE && !(flags & MSG_NOSIGNAL) &&
		    !(so->so_options & SO_NOSIGPIPE))
			lwpsignal(p, lp, SIGPIPE);
	}
#ifdef KTRACE
	if (ktriov != NULL) {
		if (error == 0) {
			ktruio.uio_iov = ktriov;
			ktruio.uio_resid = len - auio->uio_resid;
			ktrgenio(lp, s, UIO_WRITE, &ktruio, error);
		}
		kfree(ktriov, M_TEMP);
	}
#endif
	if (error == 0)
		*res  = len - auio->uio_resid;
	fdrop(fp);
	return (error);
}

/*
 * sendto_args(int s, caddr_t buf, size_t len, int flags, caddr_t to, int tolen)
 *
 * MPALMOSTSAFE
 */
int
sys_sendto(struct sendto_args *uap)
{
	struct thread *td = curthread;
	struct uio auio;
	struct iovec aiov;
	struct sockaddr *sa = NULL;
	int error;

	if (uap->to) {
		error = getsockaddr(&sa, uap->to, uap->tolen);
		if (error)
			return (error);
	}
	aiov.iov_base = uap->buf;
	aiov.iov_len = uap->len;
	auio.uio_iov = &aiov;
	auio.uio_iovcnt = 1;
	auio.uio_offset = 0;
	auio.uio_resid = uap->len;
	auio.uio_segflg = UIO_USERSPACE;
	auio.uio_rw = UIO_WRITE;
	auio.uio_td = td;

	error = kern_sendmsg(uap->s, sa, &auio, NULL, uap->flags,
			     &uap->sysmsg_szresult);

	if (sa)
		kfree(sa, M_SONAME);
	return (error);
}

/*
 * sendmsg_args(int s, caddr_t msg, int flags)
 *
 * MPALMOSTSAFE
 */
int
sys_sendmsg(struct sendmsg_args *uap)
{
	struct thread *td = curthread;
	struct msghdr msg;
	struct uio auio;
	struct iovec aiov[UIO_SMALLIOV], *iov = NULL;
	struct sockaddr *sa = NULL;
	struct mbuf *control = NULL;
	int error;

	error = copyin(uap->msg, (caddr_t)&msg, sizeof(msg));
	if (error)
		return (error);

	/*
	 * Conditionally copyin msg.msg_name.
	 */
	if (msg.msg_name) {
		error = getsockaddr(&sa, msg.msg_name, msg.msg_namelen);
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
	}

	error = kern_sendmsg(uap->s, sa, &auio, control, uap->flags,
			     &uap->sysmsg_szresult);

cleanup:
	iovec_free(&iov, aiov);
cleanup2:
	if (sa)
		kfree(sa, M_SONAME);
	return (error);
}

/*
 * kern_recvmsg() takes a handle to sa and control.  If the handle is non-
 * null, it returns a dynamically allocated struct sockaddr and an mbuf.
 * Don't forget to FREE() and m_free() these if they are returned.
 */
int
kern_recvmsg(int s, struct sockaddr **sa, struct uio *auio,
	     struct mbuf **control, int *flags, size_t *res)
{
	struct thread *td = curthread;
	struct proc *p = td->td_proc;
	struct file *fp;
	size_t len;
	int error;
	int lflags;
	struct socket *so;
#ifdef KTRACE
	struct iovec *ktriov = NULL;
	struct uio ktruio;
#endif

	error = holdsock(p->p_fd, s, &fp);
	if (error)
		return (error);
#ifdef KTRACE
	if (KTRPOINT(td, KTR_GENIO)) {
		int iovlen = auio->uio_iovcnt * sizeof (struct iovec);

		ktriov = kmalloc(iovlen, M_TEMP, M_WAITOK);
		bcopy(auio->uio_iov, ktriov, iovlen);
		ktruio = *auio;
	}
#endif
	len = auio->uio_resid;
	so = (struct socket *)fp->f_data;

	if (flags == NULL || (*flags & (MSG_FNONBLOCKING|MSG_FBLOCKING)) == 0) {
		if (fp->f_flag & FNONBLOCK) {
			if (flags) {
				*flags |= MSG_FNONBLOCKING;
			} else {
				lflags = MSG_FNONBLOCKING;
				flags = &lflags;
			}
		}
	}

	error = so_pru_soreceive(so, sa, auio, NULL, control, flags);
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
			ktrgenio(td->td_lwp, s, UIO_READ, &ktruio, error);
		}
		kfree(ktriov, M_TEMP);
	}
#endif
	if (error == 0)
		*res = len - auio->uio_resid;
	fdrop(fp);
	return (error);
}

/*
 * recvfrom_args(int s, caddr_t buf, size_t len, int flags, 
 *			caddr_t from, int *fromlenaddr)
 *
 * MPALMOSTSAFE
 */
int
sys_recvfrom(struct recvfrom_args *uap)
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

	error = kern_recvmsg(uap->s, uap->from ? &sa : NULL, &auio, NULL,
			     &uap->flags, &uap->sysmsg_szresult);

	if (error == 0 && uap->from) {
		/* note: sa may still be NULL */
		if (sa) {
			fromlen = MIN(fromlen, sa->sa_len);
			error = copyout(sa, uap->from, fromlen);
		} else {
			fromlen = 0;
		}
		if (error == 0) {
			error = copyout(&fromlen, uap->fromlenaddr,
					sizeof(fromlen));
		}
	}
	if (sa)
		kfree(sa, M_SONAME);

	return (error);
}

/*
 * recvmsg_args(int s, struct msghdr *msg, int flags)
 *
 * MPALMOSTSAFE
 */
int
sys_recvmsg(struct recvmsg_args *uap)
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
	error = copyin(uap->msg, &msg, sizeof(msg));
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

	flags = uap->flags;

	error = kern_recvmsg(uap->s,
			     (msg.msg_name ? &sa : NULL), &auio,
			     (msg.msg_control ? &control : NULL), &flags,
			     &uap->sysmsg_szresult);

	/*
	 * Conditionally copyout the name and populate the namelen field.
	 */
	if (error == 0 && msg.msg_name) {
		/* note: sa may still be NULL */
		if (sa != NULL) {
			fromlen = MIN(msg.msg_namelen, sa->sa_len);
			error = copyout(sa, msg.msg_name, fromlen);
		} else {
			fromlen = 0;
		}
		if (error == 0)
			error = copyout(&fromlen, ufromlenp,
			    sizeof(*ufromlenp));
	}

	/*
	 * Copyout msg.msg_control and msg.msg_controllen.
	 */
	if (error == 0 && msg.msg_control) {
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

			error = copyout(mtod(m, caddr_t), ctlbuf, tocopy);
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

/*
 * If sopt->sopt_td == NULL, then sopt->sopt_val is treated as an
 * in kernel pointer instead of a userland pointer.  This allows us
 * to manipulate socket options in the emulation code.
 */
int
kern_setsockopt(int s, struct sockopt *sopt)
{
	struct thread *td = curthread;
	struct proc *p = td->td_proc;
	struct file *fp;
	int error;

	if (sopt->sopt_val == NULL && sopt->sopt_valsize != 0)
		return (EFAULT);
	if (sopt->sopt_val != NULL && sopt->sopt_valsize == 0)
		return (EINVAL);
	if (sopt->sopt_valsize > SOMAXOPT_SIZE)	/* unsigned */
		return (EINVAL);

	error = holdsock(p->p_fd, s, &fp);
	if (error)
		return (error);

	error = sosetopt((struct socket *)fp->f_data, sopt);
	fdrop(fp);
	return (error);
}

/*
 * setsockopt_args(int s, int level, int name, caddr_t val, int valsize)
 *
 * MPALMOSTSAFE
 */
int
sys_setsockopt(struct setsockopt_args *uap)
{
	struct thread *td = curthread;
	struct sockopt sopt;
	int error;

	sopt.sopt_level = uap->level;
	sopt.sopt_name = uap->name;
	sopt.sopt_valsize = uap->valsize;
	sopt.sopt_td = td;
	sopt.sopt_val = NULL;

	if (sopt.sopt_valsize > SOMAXOPT_SIZE) /* unsigned */
		return (EINVAL);
	if (uap->val) {
		sopt.sopt_val = kmalloc(sopt.sopt_valsize, M_TEMP, M_WAITOK);
		error = copyin(uap->val, sopt.sopt_val, sopt.sopt_valsize);
		if (error)
			goto out;
	}

	error = kern_setsockopt(uap->s, &sopt);
out:
	if (uap->val)
		kfree(sopt.sopt_val, M_TEMP);
	return(error);
}

/*
 * If sopt->sopt_td == NULL, then sopt->sopt_val is treated as an
 * in kernel pointer instead of a userland pointer.  This allows us
 * to manipulate socket options in the emulation code.
 */
int
kern_getsockopt(int s, struct sockopt *sopt)
{
	struct thread *td = curthread;
	struct proc *p = td->td_proc;
	struct file *fp;
	int error;

	if (sopt->sopt_val == NULL && sopt->sopt_valsize != 0)
		return (EFAULT);
	if (sopt->sopt_val != NULL && sopt->sopt_valsize == 0)
		return (EINVAL);
	if (sopt->sopt_valsize > SOMAXOPT_SIZE) /* unsigned */
		return (EINVAL);

	error = holdsock(p->p_fd, s, &fp);
	if (error)
		return (error);

	error = sogetopt((struct socket *)fp->f_data, sopt);
	fdrop(fp);
	return (error);
}

/*
 * getsockopt_args(int s, int level, int name, caddr_t val, int *avalsize)
 *
 * MPALMOSTSAFE
 */
int
sys_getsockopt(struct getsockopt_args *uap)
{
	struct thread *td = curthread;
	struct	sockopt sopt;
	int	error, valsize;

	if (uap->val) {
		error = copyin(uap->avalsize, &valsize, sizeof(valsize));
		if (error)
			return (error);
	} else {
		valsize = 0;
	}

	sopt.sopt_level = uap->level;
	sopt.sopt_name = uap->name;
	sopt.sopt_valsize = valsize;
	sopt.sopt_td = td;
	sopt.sopt_val = NULL;

	if (sopt.sopt_valsize > SOMAXOPT_SIZE) /* unsigned */
		return (EINVAL);
	if (uap->val) {
		sopt.sopt_val = kmalloc(sopt.sopt_valsize, M_TEMP, M_WAITOK);
		error = copyin(uap->val, sopt.sopt_val, sopt.sopt_valsize);
		if (error)
			goto out;
	}

	error = kern_getsockopt(uap->s, &sopt);
	if (error)
		goto out;
	valsize = sopt.sopt_valsize;
	error = copyout(&valsize, uap->avalsize, sizeof(valsize));
	if (error)
		goto out;
	if (uap->val)
		error = copyout(sopt.sopt_val, uap->val, sopt.sopt_valsize);
out:
	if (uap->val)
		kfree(sopt.sopt_val, M_TEMP);
	return (error);
}

/*
 * The second argument to kern_getsockname() is a handle to a struct sockaddr.
 * This allows kern_getsockname() to return a pointer to an allocated struct
 * sockaddr which must be freed later with FREE().  The caller must
 * initialize *name to NULL.
 */
int
kern_getsockname(int s, struct sockaddr **name, int *namelen)
{
	struct thread *td = curthread;
	struct proc *p = td->td_proc;
	struct file *fp;
	struct socket *so;
	struct sockaddr *sa = NULL;
	int error;

	error = holdsock(p->p_fd, s, &fp);
	if (error)
		return (error);
	if (*namelen < 0) {
		fdrop(fp);
		return (EINVAL);
	}
	so = (struct socket *)fp->f_data;
	error = so_pru_sockaddr(so, &sa);
	if (error == 0) {
		if (sa == NULL) {
			*namelen = 0;
		} else {
			*namelen = MIN(*namelen, sa->sa_len);
			*name = sa;
		}
	}

	fdrop(fp);
	return (error);
}

/*
 * getsockname_args(int fdes, caddr_t asa, int *alen)
 *
 * Get socket name.
 *
 * MPALMOSTSAFE
 */
int
sys_getsockname(struct getsockname_args *uap)
{
	struct sockaddr *sa = NULL;
	int error, sa_len;

	error = copyin(uap->alen, &sa_len, sizeof(sa_len));
	if (error)
		return (error);

	error = kern_getsockname(uap->fdes, &sa, &sa_len);

	if (error == 0)
		error = copyout(sa, uap->asa, sa_len);
	if (error == 0)
		error = copyout(&sa_len, uap->alen, sizeof(*uap->alen));
	if (sa)
		kfree(sa, M_SONAME);
	return (error);
}

/*
 * The second argument to kern_getpeername() is a handle to a struct sockaddr.
 * This allows kern_getpeername() to return a pointer to an allocated struct
 * sockaddr which must be freed later with FREE().  The caller must
 * initialize *name to NULL.
 */
int
kern_getpeername(int s, struct sockaddr **name, int *namelen)
{
	struct thread *td = curthread;
	struct proc *p = td->td_proc;
	struct file *fp;
	struct socket *so;
	struct sockaddr *sa = NULL;
	int error;

	error = holdsock(p->p_fd, s, &fp);
	if (error)
		return (error);
	if (*namelen < 0) {
		fdrop(fp);
		return (EINVAL);
	}
	so = (struct socket *)fp->f_data;
	if ((so->so_state & (SS_ISCONNECTED|SS_ISCONFIRMING)) == 0) {
		fdrop(fp);
		return (ENOTCONN);
	}
	error = so_pru_peeraddr(so, &sa);
	if (error == 0) {
		if (sa == NULL) {
			*namelen = 0;
		} else {
			*namelen = MIN(*namelen, sa->sa_len);
			*name = sa;
		}
	}

	fdrop(fp);
	return (error);
}

/*
 * getpeername_args(int fdes, caddr_t asa, int *alen)
 *
 * Get name of peer for connected socket.
 *
 * MPALMOSTSAFE
 */
int
sys_getpeername(struct getpeername_args *uap)
{
	struct sockaddr *sa = NULL;
	int error, sa_len;

	error = copyin(uap->alen, &sa_len, sizeof(sa_len));
	if (error)
		return (error);

	error = kern_getpeername(uap->fdes, &sa, &sa_len);

	if (error == 0)
		error = copyout(sa, uap->asa, sa_len);
	if (error == 0)
		error = copyout(&sa_len, uap->alen, sizeof(*uap->alen));
	if (sa)
		kfree(sa, M_SONAME);
	return (error);
}

int
getsockaddr(struct sockaddr **namp, caddr_t uaddr, size_t len)
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
#if BYTE_ORDER != BIG_ENDIAN
		/*
		 * The bind(), connect(), and sendto() syscalls were not
		 * versioned for COMPAT_43.  Thus, this check must stay.
		 */
		if (sa->sa_family == 0 && sa->sa_len < AF_MAX)
			sa->sa_family = sa->sa_len;
#endif
		sa->sa_len = len;
		*namp = sa;
	}
	return error;
}

/*
 * Detach a mapped page and release resources back to the system.
 * We must release our wiring and if the object is ripped out
 * from under the vm_page we become responsible for freeing the
 * page.
 *
 * MPSAFE
 */
static void
sf_buf_mfree(void *arg)
{
	struct sf_buf *sf = arg;
	vm_page_t m;

	m = sf_buf_page(sf);
	if (sf_buf_free(sf)) {
		/* sf invalid now */
		vm_page_busy_wait(m, FALSE, "sockpgf");
		vm_page_unwire(m, 0);
		if (m->object == NULL &&
		    m->wire_count == 0 &&
		    (m->flags & PG_NEED_COMMIT) == 0) {
			vm_page_free(m);
		} else {
			vm_page_wakeup(m);
		}
	}
}

/*
 * sendfile(2).
 * int sendfile(int fd, int s, off_t offset, size_t nbytes,
 *	 struct sf_hdtr *hdtr, off_t *sbytes, int flags)
 *
 * Send a file specified by 'fd' and starting at 'offset' to a socket
 * specified by 's'. Send only 'nbytes' of the file or until EOF if
 * nbytes == 0. Optionally add a header and/or trailer to the socket
 * output. If specified, write the total number of bytes sent into *sbytes.
 *
 * In FreeBSD kern/uipc_syscalls.c,v 1.103, a bug was fixed that caused
 * the headers to count against the remaining bytes to be sent from
 * the file descriptor.  We may wish to implement a compatibility syscall
 * in the future.
 *
 * MPALMOSTSAFE
 */
int
sys_sendfile(struct sendfile_args *uap)
{
	struct thread *td = curthread;
	struct proc *p = td->td_proc;
	struct file *fp;
	struct vnode *vp = NULL;
	struct sf_hdtr hdtr;
	struct iovec aiov[UIO_SMALLIOV], *iov = NULL;
	struct uio auio;
	struct mbuf *mheader = NULL;
	size_t hbytes = 0;
	size_t tbytes;
	off_t hdtr_size = 0;
	off_t sbytes;
	int error;

	KKASSERT(p);

	/*
	 * Do argument checking. Must be a regular file in, stream
	 * type and connected socket out, positive offset.
	 */
	fp = holdfp(p->p_fd, uap->fd, FREAD);
	if (fp == NULL) {
		return (EBADF);
	}
	if (fp->f_type != DTYPE_VNODE) {
		fdrop(fp);
		return (EINVAL);
	}
	vp = (struct vnode *)fp->f_data;
	vref(vp);
	fdrop(fp);

	/*
	 * If specified, get the pointer to the sf_hdtr struct for
	 * any headers/trailers.
	 */
	if (uap->hdtr) {
		error = copyin(uap->hdtr, &hdtr, sizeof(hdtr));
		if (error)
			goto done;
		/*
		 * Send any headers.
		 */
		if (hdtr.headers) {
			error = iovec_copyin(hdtr.headers, &iov, aiov,
					     hdtr.hdr_cnt, &hbytes);
			if (error)
				goto done;
			auio.uio_iov = iov;
			auio.uio_iovcnt = hdtr.hdr_cnt;
			auio.uio_offset = 0;
			auio.uio_segflg = UIO_USERSPACE;
			auio.uio_rw = UIO_WRITE;
			auio.uio_td = td;
			auio.uio_resid = hbytes;

			mheader = m_uiomove(&auio);

			iovec_free(&iov, aiov);
			if (mheader == NULL)
				goto done;
		}
	}

	error = kern_sendfile(vp, uap->s, uap->offset, uap->nbytes, mheader,
			      &sbytes, uap->flags);
	if (error)
		goto done;

	/*
	 * Send trailers. Wimp out and use writev(2).
	 */
	if (uap->hdtr != NULL && hdtr.trailers != NULL) {
		error = iovec_copyin(hdtr.trailers, &iov, aiov,
				     hdtr.trl_cnt, &auio.uio_resid);
		if (error)
			goto done;
		auio.uio_iov = iov;
		auio.uio_iovcnt = hdtr.trl_cnt;
		auio.uio_offset = 0;
		auio.uio_segflg = UIO_USERSPACE;
		auio.uio_rw = UIO_WRITE;
		auio.uio_td = td;

		error = kern_sendmsg(uap->s, NULL, &auio, NULL, 0, &tbytes);

		iovec_free(&iov, aiov);
		if (error)
			goto done;
		hdtr_size += tbytes;	/* trailer bytes successfully sent */
	}

done:
	if (vp)
		vrele(vp);
	if (uap->sbytes != NULL) {
		sbytes += hdtr_size;
		copyout(&sbytes, uap->sbytes, sizeof(off_t));
	}
	return (error);
}

int
kern_sendfile(struct vnode *vp, int sfd, off_t offset, size_t nbytes,
	      struct mbuf *mheader, off_t *sbytes, int flags)
{
	struct thread *td = curthread;
	struct proc *p = td->td_proc;
	struct vm_object *obj;
	struct socket *so;
	struct file *fp;
	struct mbuf *m, *mp;
	struct sf_buf *sf;
	struct vm_page *pg;
	off_t off, xfsize;
	off_t hbytes = 0;
	int error = 0;

	if (vp->v_type != VREG) {
		error = EINVAL;
		goto done0;
	}
	if ((obj = vp->v_object) == NULL) {
		error = EINVAL;
		goto done0;
	}
	error = holdsock(p->p_fd, sfd, &fp);
	if (error)
		goto done0;
	so = (struct socket *)fp->f_data;
	if (so->so_type != SOCK_STREAM) {
		error = EINVAL;
		goto done;
	}
	if ((so->so_state & SS_ISCONNECTED) == 0) {
		error = ENOTCONN;
		goto done;
	}
	if (offset < 0) {
		error = EINVAL;
		goto done;
	}

	*sbytes = 0;
	/*
	 * Protect against multiple writers to the socket.
	 */
	ssb_lock(&so->so_snd, M_WAITOK);

	/*
	 * Loop through the pages in the file, starting with the requested
	 * offset. Get a file page (do I/O if necessary), map the file page
	 * into an sf_buf, attach an mbuf header to the sf_buf, and queue
	 * it on the socket.
	 */
	for (off = offset; ; off += xfsize, *sbytes += xfsize + hbytes) {
		vm_pindex_t pindex;
		vm_offset_t pgoff;
		int space;

		pindex = OFF_TO_IDX(off);
retry_lookup:
		/*
		 * Calculate the amount to transfer. Not to exceed a page,
		 * the EOF, or the passed in nbytes.
		 */
		xfsize = vp->v_filesize - off;
		if (xfsize > PAGE_SIZE)
			xfsize = PAGE_SIZE;
		pgoff = (vm_offset_t)(off & PAGE_MASK);
		if (PAGE_SIZE - pgoff < xfsize)
			xfsize = PAGE_SIZE - pgoff;
		if (nbytes && xfsize > (nbytes - *sbytes))
			xfsize = nbytes - *sbytes;
		if (xfsize <= 0)
			break;
		/*
		 * Optimize the non-blocking case by looking at the socket space
		 * before going to the extra work of constituting the sf_buf.
		 */
		if ((fp->f_flag & FNONBLOCK) &&
		    ssb_space_prealloc(&so->so_snd) <= 0) {
			if (so->so_state & SS_CANTSENDMORE)
				error = EPIPE;
			else
				error = EAGAIN;
			ssb_unlock(&so->so_snd);
			goto done;
		}
		/*
		 * Attempt to look up the page.  
		 *
		 *	Allocate if not found, wait and loop if busy, then
		 *	wire the page.  critical section protection is
		 * 	required to maintain the object association (an
		 *	interrupt can free the page) through to the
		 *	vm_page_wire() call.
		 */
		vm_object_hold(obj);
		pg = vm_page_lookup_busy_try(obj, pindex, TRUE, &error);
		if (error) {
			vm_page_sleep_busy(pg, TRUE, "sfpbsy");
			vm_object_drop(obj);
			goto retry_lookup;
		}
		if (pg == NULL) {
			pg = vm_page_alloc(obj, pindex, VM_ALLOC_NORMAL |
							VM_ALLOC_NULL_OK);
			if (pg == NULL) {
				vm_wait(0);
				vm_object_drop(obj);
				goto retry_lookup;
			}
		}
		vm_page_wire(pg);
		vm_object_drop(obj);

		/*
		 * If page is not valid for what we need, initiate I/O
		 */

		if (!pg->valid || !vm_page_is_valid(pg, pgoff, xfsize)) {
			struct uio auio;
			struct iovec aiov;
			int bsize;

			/*
			 * Ensure that our page is still around when the I/O 
			 * completes.
			 *
			 * Ensure that our page is not modified while part of
			 * a mbuf as this could mess up tcp checksums, DMA,
			 * etc (XXX NEEDS WORK).  The softbusy is supposed to
			 * help here but it actually doesn't.
			 *
			 * XXX THIS HAS MULTIPLE PROBLEMS.  The underlying
			 *     VM pages are not protected by the soft-busy
			 *     unless we vm_page_protect... READ them, and
			 *     they STILL aren't protected against
			 *     modification via the buffer cache (VOP_WRITE).
			 *
			 *     Fixing the second issue is particularly
			 *     difficult.
			 *
			 * XXX We also can't soft-busy anyway because it can
			 *     deadlock against the syncer doing a vfs_msync(),
			 *     vfs_msync->vmntvnodesca->vfs_msync_scan2->
			 *     vm_object_page_clean->(scan)-> ... page
			 *     busy-wait.
			 */
			/*vm_page_io_start(pg);*/
			vm_page_wakeup(pg);

			/*
			 * Get the page from backing store.
			 */
			bsize = vp->v_mount->mnt_stat.f_iosize;
			auio.uio_iov = &aiov;
			auio.uio_iovcnt = 1;
			aiov.iov_base = 0;
			aiov.iov_len = MAXBSIZE;
			auio.uio_resid = MAXBSIZE;
			auio.uio_offset = trunc_page(off);
			auio.uio_segflg = UIO_NOCOPY;
			auio.uio_rw = UIO_READ;
			auio.uio_td = td;
			vn_lock(vp, LK_SHARED | LK_RETRY);
			error = VOP_READ(vp, &auio, 
				    IO_VMIO | ((MAXBSIZE / bsize) << 16),
				    td->td_ucred);
			vn_unlock(vp);
			vm_page_flag_clear(pg, PG_ZERO);
			vm_page_busy_wait(pg, FALSE, "sockpg");
			/*vm_page_io_finish(pg);*/
			if (error) {
				vm_page_unwire(pg, 0);
				vm_page_wakeup(pg);
				vm_page_try_to_free(pg);
				ssb_unlock(&so->so_snd);
				goto done;
			}
		}


		/*
		 * Get a sendfile buf. We usually wait as long as necessary,
		 * but this wait can be interrupted.
		 */
		if ((sf = sf_buf_alloc(pg)) == NULL) {
			vm_page_unwire(pg, 0);
			vm_page_wakeup(pg);
			vm_page_try_to_free(pg);
			ssb_unlock(&so->so_snd);
			error = EINTR;
			goto done;
		}
		vm_page_wakeup(pg);

		/*
		 * Get an mbuf header and set it up as having external storage.
		 */
		MGETHDR(m, MB_WAIT, MT_DATA);
		if (m == NULL) {
			error = ENOBUFS;
			sf_buf_free(sf);
			ssb_unlock(&so->so_snd);
			goto done;
		}

		m->m_ext.ext_free = sf_buf_mfree;
		m->m_ext.ext_ref = sf_buf_ref;
		m->m_ext.ext_arg = sf;
		m->m_ext.ext_buf = (void *)sf_buf_kva(sf);
		m->m_ext.ext_size = PAGE_SIZE;
		m->m_data = (char *)sf_buf_kva(sf) + pgoff;
		m->m_flags |= M_EXT;
		m->m_pkthdr.len = m->m_len = xfsize;
		KKASSERT((m->m_flags & (M_EXT_CLUSTER)) == 0);

		if (mheader != NULL) {
			hbytes = mheader->m_pkthdr.len;
			mheader->m_pkthdr.len += m->m_pkthdr.len;
			m_cat(mheader, m);
			m = mheader;
			mheader = NULL;
		} else
			hbytes = 0;

		/*
		 * Add the buffer to the socket buffer chain.
		 */
		crit_enter();
retry_space:
		/*
		 * Make sure that the socket is still able to take more data.
		 * CANTSENDMORE being true usually means that the connection
		 * was closed. so_error is true when an error was sensed after
		 * a previous send.
		 * The state is checked after the page mapping and buffer
		 * allocation above since those operations may block and make
		 * any socket checks stale. From this point forward, nothing
		 * blocks before the pru_send (or more accurately, any blocking
		 * results in a loop back to here to re-check).
		 */
		if ((so->so_state & SS_CANTSENDMORE) || so->so_error) {
			if (so->so_state & SS_CANTSENDMORE) {
				error = EPIPE;
			} else {
				error = so->so_error;
				so->so_error = 0;
			}
			m_freem(m);
			ssb_unlock(&so->so_snd);
			crit_exit();
			goto done;
		}
		/*
		 * Wait for socket space to become available. We do this just
		 * after checking the connection state above in order to avoid
		 * a race condition with ssb_wait().
		 */
		space = ssb_space_prealloc(&so->so_snd);
		if (space < m->m_pkthdr.len && space < so->so_snd.ssb_lowat) {
			if (fp->f_flag & FNONBLOCK) {
				m_freem(m);
				ssb_unlock(&so->so_snd);
				crit_exit();
				error = EAGAIN;
				goto done;
			}
			error = ssb_wait(&so->so_snd);
			/*
			 * An error from ssb_wait usually indicates that we've
			 * been interrupted by a signal. If we've sent anything
			 * then return bytes sent, otherwise return the error.
			 */
			if (error) {
				m_freem(m);
				ssb_unlock(&so->so_snd);
				crit_exit();
				goto done;
			}
			goto retry_space;
		}

		for (mp = m; mp != NULL; mp = mp->m_next)
			ssb_preallocstream(&so->so_snd, mp);
		if (use_sendfile_async)
			error = so_pru_senda(so, 0, m, NULL, NULL, td);
		else
			error = so_pru_send(so, 0, m, NULL, NULL, td);

		crit_exit();
		if (error) {
			ssb_unlock(&so->so_snd);
			goto done;
		}
	}
	if (mheader != NULL) {
		*sbytes += mheader->m_pkthdr.len;

		for (mp = mheader; mp != NULL; mp = mp->m_next)
			ssb_preallocstream(&so->so_snd, mp);
		if (use_sendfile_async)
			error = so_pru_senda(so, 0, mheader, NULL, NULL, td);
		else
			error = so_pru_send(so, 0, mheader, NULL, NULL, td);

		mheader = NULL;
	}
	ssb_unlock(&so->so_snd);

done:
	fdrop(fp);
done0:
	if (mheader != NULL)
		m_freem(mheader);
	return (error);
}

/*
 * MPALMOSTSAFE
 */
int
sys_sctp_peeloff(struct sctp_peeloff_args *uap)
{
#ifdef SCTP
	struct thread *td = curthread;
	struct filedesc *fdp = td->td_proc->p_fd;
	struct file *lfp = NULL;
	struct file *nfp = NULL;
	int error;
	struct socket *head, *so;
	caddr_t assoc_id;
	int fd;
	short fflag;		/* type must match fp->f_flag */

	assoc_id = uap->name;
	error = holdsock(td->td_proc->p_fd, uap->sd, &lfp);
	if (error)
		return (error);

	crit_enter();
	head = (struct socket *)lfp->f_data;
	error = sctp_can_peel_off(head, assoc_id);
	if (error) {
		crit_exit();
		goto done;
	}
	/*
	 * At this point we know we do have a assoc to pull
	 * we proceed to get the fd setup. This may block
	 * but that is ok.
	 */

	fflag = lfp->f_flag;
	error = falloc(td->td_lwp, &nfp, &fd);
	if (error) {
		/*
		 * Probably ran out of file descriptors. Put the
		 * unaccepted connection back onto the queue and
		 * do another wakeup so some other process might
		 * have a chance at it.
		 */
		crit_exit();
		goto done;
	}
	uap->sysmsg_iresult = fd;

	so = sctp_get_peeloff(head, assoc_id, &error);
	if (so == NULL) {
		/*
		 * Either someone else peeled it off OR
		 * we can't get a socket.
		 */
		goto noconnection;
	}
	soreference(so);			/* reference needed */
	soclrstate(so, SS_NOFDREF | SS_COMP);	/* when clearing NOFDREF */
	so->so_head = NULL;
	if (head->so_sigio != NULL)
		fsetown(fgetown(&head->so_sigio), &so->so_sigio);

	nfp->f_type = DTYPE_SOCKET;
	nfp->f_flag = fflag;
	nfp->f_ops = &socketops;
	nfp->f_data = so;

noconnection:
	/*
	 * Assign the file pointer to the reserved descriptor, or clear
	 * the reserved descriptor if an error occured.
	 */
	if (error)
		fsetfd(fdp, NULL, fd);
	else
		fsetfd(fdp, nfp, fd);
	crit_exit();
	/*
	 * Release explicitly held references before returning.
	 */
done:
	if (nfp != NULL)
		fdrop(nfp);
	fdrop(lfp);
	return (error);
#else /* SCTP */
	return(EOPNOTSUPP);
#endif /* SCTP */
}
