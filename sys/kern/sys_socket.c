/*
 * Copyright (c) 1982, 1986, 1990, 1993
 *	The Regents of the University of California.  All rights reserved.
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
 *	@(#)sys_socket.c	8.1 (Berkeley) 6/10/93
 * $FreeBSD: src/sys/kern/sys_socket.c,v 1.28.2.2 2001/02/26 04:23:16 jlemon Exp $
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/file.h>
#include <sys/lock.h>
#include <sys/protosw.h>
#include <sys/signalvar.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/socketops.h>
#include <sys/filio.h>			/* XXX */
#include <sys/sockio.h>
#include <sys/vnode.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <sys/filedesc.h>
#include <sys/ucred.h>

#include <sys/socketvar2.h>

#include <net/if.h>
#include <net/route.h>

struct	fileops socketops = {
	.fo_read = soo_read,
	.fo_write = soo_write,
	.fo_ioctl = soo_ioctl,
	.fo_kqfilter = sokqfilter,
	.fo_stat = soo_stat,
	.fo_close = soo_close,
	.fo_shutdown = soo_shutdown
};

/*
 * MPSAFE
 */
int
soo_read(struct file *fp, struct uio *uio, struct ucred *cred, int fflags)
{
	struct socket *so;
	int error;
	int msgflags;

	atomic_set_int(&curthread->td_mpflags, TDF_MP_BATCH_DEMARC);

	so = (struct socket *)fp->f_data;

	if (fflags & O_FBLOCKING)
		msgflags = 0;
	else if (fflags & O_FNONBLOCKING)
		msgflags = MSG_FNONBLOCKING;
	else if (fp->f_flag & FNONBLOCK)
		msgflags = MSG_FNONBLOCKING;
	else
		msgflags = 0;

	error = so_pru_soreceive(so, NULL, uio, NULL, NULL, &msgflags);
	return (error);
}

/*
 * MPSAFE
 */
int
soo_write(struct file *fp, struct uio *uio, struct ucred *cred, int fflags)
{
	struct socket *so;
	int error;
	int msgflags;

	so = (struct socket *)fp->f_data;

	if (fflags & O_FBLOCKING)
		msgflags = 0;
	else if (fflags & O_FNONBLOCKING)
		msgflags = MSG_FNONBLOCKING;
	else if (fp->f_flag & FNONBLOCK)
		msgflags = MSG_FNONBLOCKING;
	else
		msgflags = 0;

	error = so_pru_sosend(so, NULL, uio, NULL, NULL, msgflags, uio->uio_td);
	if (error == EPIPE && !(fflags & MSG_NOSIGNAL) &&
	    !(so->so_options & SO_NOSIGPIPE) &&
	    uio->uio_td->td_lwp) {
		lwpsignal(uio->uio_td->td_proc, uio->uio_td->td_lwp, SIGPIPE);
	}
	return (error);
}

/*
 * MPSAFE
 */
int
soo_ioctl(struct file *fp, u_long cmd, caddr_t data,
	  struct ucred *cred, struct sysmsg *msg)
{
	struct socket *so;
	int error;

	so = (struct socket *)fp->f_data;

	switch (cmd) {
	case FIOASYNC:
		if (*(int *)data) {
			sosetstate(so, SS_ASYNC);
			atomic_set_int(&so->so_rcv.ssb_flags,  SSB_ASYNC);
			atomic_set_int(&so->so_snd.ssb_flags, SSB_ASYNC);
		} else {
			soclrstate(so, SS_ASYNC);
			atomic_clear_int(&so->so_rcv.ssb_flags, SSB_ASYNC);
			atomic_clear_int(&so->so_snd.ssb_flags, SSB_ASYNC);
		}
		error = 0;
		break;
	case FIONREAD:
		*(int *)data = so->so_rcv.ssb_cc;
		error = 0;
		break;
	case FIOSETOWN:
		error = fsetown(*(int *)data, &so->so_sigio);
		break;
	case FIOGETOWN:
		*(int *)data = fgetown(&so->so_sigio);
		error = 0;
		break;
	case SIOCSPGRP:
		error = fsetown(-(*(int *)data), &so->so_sigio);
		break;
	case SIOCGPGRP:
		*(int *)data = -fgetown(&so->so_sigio);
		error = 0;
		break;
	case SIOCATMARK:
		*(int *)data = (so->so_state&SS_RCVATMARK) != 0;
		error = 0;
		break;
	default:
		/*
		 * Interface/routing/protocol specific ioctls:
		 * interface and routing ioctls should have a
		 * different entry since a socket's unnecessary
		 */
		if (IOCGROUP(cmd) == 'i') {
			error = ifioctl(so, cmd, data, cred);
		} else if (IOCGROUP(cmd) == 'r') {
			error = rtioctl(cmd, data, cred);
		} else {
			error = so_pru_control_direct(so, cmd, data, NULL);
		}
		break;
	}
	return (error);
}

/*
 * MPSAFE
 */
int
soo_stat(struct file *fp, struct stat *ub, struct ucred *cred)
{
	struct socket *so;
	int error;

	bzero((caddr_t)ub, sizeof (*ub));
	ub->st_mode = S_IFSOCK;
	so = (struct socket *)fp->f_data;

	/*
	 * If SS_CANTRCVMORE is set, but there's still data left in the
	 * receive buffer, the socket is still readable.
	 */
	if ((so->so_state & SS_CANTRCVMORE) == 0 ||
	    so->so_rcv.ssb_cc != 0)
		ub->st_mode |= S_IRUSR | S_IRGRP | S_IROTH;
	if ((so->so_state & SS_CANTSENDMORE) == 0)
		ub->st_mode |= S_IWUSR | S_IWGRP | S_IWOTH;
	ub->st_size = so->so_rcv.ssb_cc;
	ub->st_uid = so->so_cred->cr_uid;
	ub->st_gid = so->so_cred->cr_gid;
	error = so_pru_sense(so, ub);
	return (error);
}

/*
 * MPSAFE
 */
int
soo_close(struct file *fp)
{
	int error;

	fp->f_ops = &badfileops;
	if (fp->f_data)
		error = soclose((struct socket *)fp->f_data, fp->f_flag);
	else
		error = 0;
	fp->f_data = NULL;
	return (error);
}

/*
 * MPSAFE
 */
int
soo_shutdown(struct file *fp, int how)
{
	int error;

	if (fp->f_data)
		error = soshutdown((struct socket *)fp->f_data, how);
	else
		error = 0;
	return (error);
}
