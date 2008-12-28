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
 *	@(#)sys_socket.c	8.1 (Berkeley) 6/10/93
 * $FreeBSD: src/sys/kern/sys_socket.c,v 1.28.2.2 2001/02/26 04:23:16 jlemon Exp $
 * $DragonFly: src/sys/kern/sys_socket.c,v 1.14 2007/04/22 01:13:10 dillon Exp $
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/file.h>
#include <sys/lock.h>
#include <sys/protosw.h>
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

#include <net/if.h>
#include <net/netisr.h>
#include <net/netmsg.h>
#include <net/netmsg2.h>
#include <net/route.h>

struct	fileops socketops = {
	.fo_read = soo_read,
	.fo_write = soo_write,
	.fo_ioctl = soo_ioctl,
	.fo_poll = soo_poll,
	.fo_kqfilter = sokqfilter,
	.fo_stat = soo_stat,
	.fo_close = soo_close,
	.fo_shutdown = soo_shutdown
};

/*
 * MPALMOSTSAFE - acquires mplock
 */
int
soo_read(struct file *fp, struct uio *uio, struct ucred *cred, int fflags)
{
	struct socket *so;
	int error, msgflags;

	so = (struct socket *)fp->f_data;

	if (fflags & O_FBLOCKING)
		msgflags = 0;
	else if (fflags & O_FNONBLOCKING)
		msgflags = MSG_FNONBLOCKING;
	else if (fp->f_flag & FNONBLOCK)
		msgflags = MSG_FNONBLOCKING;
	else
		msgflags = 0;

	error = so_pru_soreceive(so, NULL, uio, NULL, -1, NULL, &msgflags);
	return (error);
}

/*
 * MPALMOSTSAFE - acquires mplock
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
	return (error);
}

static void
netmsg_sigio_setval(anynetmsg_t msg)
{
	struct netmsg_so_op *nm = &msg->so_op;

	nm->nm_so->so_sigio = nm->nm_val;
	lwkt_replymsg(&nm->nm_netmsg.nm_lmsg, 0);
}
	
/*
 * MPALMOSTSAFE - acquires mplock
 */
int
soo_ioctl(struct file *fp, u_long cmd, caddr_t data, struct ucred *cred)
{
	struct socket *so;
	struct netmsg_so_op msg;
	lwkt_port_t port;
	int error;

	so = (struct socket *)fp->f_data;

	switch (cmd) {
	case FIOASYNC:
		if (*(int *)data) {
			atomic_set_short(&so->so_state, SS_ASYNC);
			so->so_rcv.ssb_flags |= SSB_ASYNC;
			so->so_snd.ssb_flags |= SSB_ASYNC;
		} else {
			atomic_clear_short(&so->so_state, SS_ASYNC);
			so->so_rcv.ssb_flags &= ~SSB_ASYNC;
			so->so_snd.ssb_flags &= ~SSB_ASYNC;
		}
		error = 0;
		break;
	case FIONREAD:
		*(int *)data = ssb_reader_cc_est(&so->so_rcv);
		error = 0;
		break;
	case FIOSETOWN:
		port = so->so_proto->pr_mport(so, NULL, NULL, PRU_OP);
		netmsg_init(&msg.nm_netmsg, &curthread->td_msgport, 0,
			    netmsg_sigio_setval);
		msg.nm_so = so;
		get_mplock();	/* XXX: maybe needed for fsetown()? -- agg */
		error = fsetown(*(int *)data, (struct sigio **)&msg.nm_val);
		rel_mplock();
		if (error)
			break;
		error = lwkt_domsg(port, &msg.nm_netmsg.nm_lmsg, 0);
		break;
	case FIOGETOWN:
		get_mplock();	/* XXX: maybe needed for fgetown()? -- agg */
		*(int *)data = fgetown(so->so_sigio);
		rel_mplock();
		error = 0;
		break;
	case SIOCSPGRP:
		port = so->so_proto->pr_mport(so, NULL, NULL, PRU_OP);
		netmsg_init(&msg.nm_netmsg, &curthread->td_msgport, 0,
			    netmsg_sigio_setval);
		msg.nm_so = so;
		get_mplock();	/* XXX: maybe needed for fsetown()? -- agg */
		error = fsetown(-(*(int *)data), (struct sigio **)&msg.nm_val);
		rel_mplock();
		if (error)
			break;
		error = lwkt_domsg(port, &msg.nm_netmsg.nm_lmsg, 0);
		break;
	case SIOCGPGRP:
		get_mplock();	/* XXX: maybe needed for fgetown()? -- agg */
		*(int *)data = -fgetown(so->so_sigio);
		rel_mplock();
		error = 0;
		break;
	case SIOCATMARK:
		*(int *)data = (so->so_state&SS_RCVATMARK) != 0;
		error = 0;
		break;
	default:
		get_mplock();
		/*
		 * Interface/routing/protocol specific ioctls:
		 * interface and routing ioctls should have a
		 * different entry since a socket's unnecessary
		 */
		if (IOCGROUP(cmd) == 'i')
			error = ifioctl(so, cmd, data, cred);
		else if (IOCGROUP(cmd) == 'r')
			error = rtioctl(cmd, data, cred);
		else
			error = so_pru_control(so, cmd, data, NULL);
		rel_mplock();
		break;
	}
	return (error);
}

/*
 * MPSAFE - acquires mplock
 */
int
soo_poll(struct file *fp, int events, struct ucred *cred)
{
	struct socket *so;
	int error;

	so = (struct socket *)fp->f_data;
	error = so_pru_poll(so, events, cred);
	return (error);
}

/*
 * MPSAFE - acquires mplock
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
	ub->st_size = ssb_reader_cc_est(&so->so_rcv);
	if ((so->so_state & SS_CANTRCVMORE) == 0 ||
					ub->st_size != 0)
		ub->st_mode |= S_IRUSR | S_IRGRP | S_IROTH;
	if ((so->so_state & SS_CANTSENDMORE) == 0)
		ub->st_mode |= S_IWUSR | S_IWGRP | S_IWOTH;
	ub->st_uid = so->so_cred->cr_uid;
	ub->st_gid = so->so_cred->cr_gid;
	error = so_pru_sense(so, ub);
	return (error);
}

/*
 * MPALMOSTSAFE - acquires mplock
 */
int
soo_close(struct file *fp)
{
	int error;

	get_mplock();	/* XXX: remove? lock socket? -- agg */
	fp->f_ops = &badfileops;
	if (fp->f_data)
		error = soclose((struct socket *)fp->f_data, fp->f_flag);
	else
		error = 0;
	fp->f_data = NULL;
	rel_mplock();
	return (error);
}

/*
 * MPALMOSTSAFE - acquires mplock
 */
int
soo_shutdown(struct file *fp, int how)
{
	int error;

	get_mplock();	/* XXX: remove? lock socket for soshutdown? */
	if (fp->f_data)	/* how can this not be != NULL? */
		error = soshutdown((struct socket *)fp->f_data, how);
	else
		error = 0;
	rel_mplock();
	return (error);
}

