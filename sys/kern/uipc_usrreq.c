/*
 * Copyright (c) 1982, 1986, 1989, 1991, 1993
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
 *	From: @(#)uipc_usrreq.c	8.3 (Berkeley) 1/4/94
 * $FreeBSD: src/sys/kern/uipc_usrreq.c,v 1.54.2.10 2003/03/04 17:28:09 nectar Exp $
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/domain.h>
#include <sys/fcntl.h>
#include <sys/malloc.h>		/* XXX must be before <sys/file.h> */
#include <sys/proc.h>
#include <sys/file.h>
#include <sys/filedesc.h>
#include <sys/mbuf.h>
#include <sys/nlookup.h>
#include <sys/protosw.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/resourcevar.h>
#include <sys/stat.h>
#include <sys/mount.h>
#include <sys/sysctl.h>
#include <sys/un.h>
#include <sys/unpcb.h>
#include <sys/vnode.h>

#include <sys/file2.h>
#include <sys/spinlock2.h>
#include <sys/socketvar2.h>
#include <sys/msgport2.h>

typedef struct unp_defdiscard {
	struct unp_defdiscard *next;
	struct file *fp;
} *unp_defdiscard_t;

static	MALLOC_DEFINE(M_UNPCB, "unpcb", "unpcb struct");
static	unp_gen_t unp_gencnt;
static	u_int unp_count;

static	struct unp_head unp_shead, unp_dhead;

static struct lwkt_token unp_token = LWKT_TOKEN_INITIALIZER(unp_token);
static int unp_defdiscard_nest;
static unp_defdiscard_t unp_defdiscard_base;

/*
 * Unix communications domain.
 *
 * TODO:
 *	RDM
 *	rethink name space problems
 *	need a proper out-of-band
 *	lock pushdown
 */
static struct	sockaddr sun_noname = { sizeof(sun_noname), AF_LOCAL };
static ino_t	unp_ino = 1;		/* prototype for fake inode numbers */
static struct spinlock unp_ino_spin = SPINLOCK_INITIALIZER(&unp_ino_spin, "unp_ino_spin");

static int     unp_attach (struct socket *, struct pru_attach_info *);
static void    unp_detach (struct unpcb *);
static int     unp_bind (struct unpcb *,struct sockaddr *, struct thread *);
static int     unp_connect (struct socket *,struct sockaddr *,
				struct thread *);
static void    unp_disconnect (struct unpcb *);
static void    unp_shutdown (struct unpcb *);
static void    unp_drop (struct unpcb *, int);
static void    unp_gc (void);
static int     unp_gc_clearmarks(struct file *, void *);
static int     unp_gc_checkmarks(struct file *, void *);
static int     unp_gc_checkrefs(struct file *, void *);
static int     unp_revoke_gc_check(struct file *, void *);
static void    unp_scan (struct mbuf *, void (*)(struct file *, void *),
				void *data);
static void    unp_mark (struct file *, void *data);
static void    unp_discard (struct file *, void *);
static int     unp_internalize (struct mbuf *, struct thread *);
static int     unp_listen (struct unpcb *, struct thread *);
static void    unp_fp_externalize(struct lwp *lp, struct file *fp, int fd);

/*
 * SMP Considerations:
 *
 *	Since unp_token will be automaticly released upon execution of
 *	blocking code, we need to reference unp_conn before any possible
 *	blocking code to prevent it from being ripped behind our back.
 *
 *	Any adjustment to unp->unp_conn requires both the global unp_token
 *	AND the per-unp token (lwkt_token_pool_lookup(unp)) to be held.
 *
 *	Any access to so_pcb to obtain unp requires the pool token for
 *	unp to be held.
 */

/* NOTE: unp_token MUST be held */
static __inline void
unp_reference(struct unpcb *unp)
{
	atomic_add_int(&unp->unp_refcnt, 1);
}

/* NOTE: unp_token MUST be held */
static __inline void
unp_free(struct unpcb *unp)
{
	KKASSERT(unp->unp_refcnt > 0);
	if (atomic_fetchadd_int(&unp->unp_refcnt, -1) == 1)
		unp_detach(unp);
}

/*
 * NOTE: (so) is referenced from soabort*() and netmsg_pru_abort()
 *	 will sofree() it when we return.
 */
static void
uipc_abort(netmsg_t msg)
{
	struct unpcb *unp;
	int error;

	lwkt_gettoken(&unp_token);
	unp = msg->base.nm_so->so_pcb;
	if (unp) {
		unp_drop(unp, ECONNABORTED);
		unp_free(unp);
		error = 0;
	} else {
		error = EINVAL;
	}
	lwkt_reltoken(&unp_token);

	lwkt_replymsg(&msg->lmsg, error);
}

static void
uipc_accept(netmsg_t msg)
{
	struct unpcb *unp;
	int error;

	lwkt_gettoken(&unp_token);
	unp = msg->base.nm_so->so_pcb;
	if (unp == NULL) {
		error = EINVAL;
	} else {
		struct unpcb *unp2 = unp->unp_conn;

		/*
		 * Pass back name of connected socket,
		 * if it was bound and we are still connected
		 * (our peer may have closed already!).
		 */
		if (unp2 && unp2->unp_addr) {
			unp_reference(unp2);
			*msg->accept.nm_nam = dup_sockaddr(
				(struct sockaddr *)unp2->unp_addr);
			unp_free(unp2);
		} else {
			*msg->accept.nm_nam = dup_sockaddr(&sun_noname);
		}
		error = 0;
	}
	lwkt_reltoken(&unp_token);
	lwkt_replymsg(&msg->lmsg, error);
}

static void
uipc_attach(netmsg_t msg)
{
	struct unpcb *unp;
	int error;

	lwkt_gettoken(&unp_token);
	unp = msg->base.nm_so->so_pcb;
	if (unp)
		error = EISCONN;
	else
		error = unp_attach(msg->base.nm_so, msg->attach.nm_ai);
	lwkt_reltoken(&unp_token);
	lwkt_replymsg(&msg->lmsg, error);
}

static void
uipc_bind(netmsg_t msg)
{
	struct unpcb *unp;
	int error;

	lwkt_gettoken(&unp_token);
	unp = msg->base.nm_so->so_pcb;
	if (unp)
		error = unp_bind(unp, msg->bind.nm_nam, msg->bind.nm_td);
	else
		error = EINVAL;
	lwkt_reltoken(&unp_token);
	lwkt_replymsg(&msg->lmsg, error);
}

static void
uipc_connect(netmsg_t msg)
{
	struct unpcb *unp;
	int error;

	unp = msg->base.nm_so->so_pcb;
	if (unp) {
		error = unp_connect(msg->base.nm_so,
				    msg->connect.nm_nam,
				    msg->connect.nm_td);
	} else {
		error = EINVAL;
	}
	lwkt_replymsg(&msg->lmsg, error);
}

static void
uipc_connect2(netmsg_t msg)
{
	struct unpcb *unp;
	int error;

	unp = msg->connect2.nm_so1->so_pcb;
	if (unp) {
		error = unp_connect2(msg->connect2.nm_so1,
				     msg->connect2.nm_so2);
	} else {
		error = EINVAL;
	}
	lwkt_replymsg(&msg->lmsg, error);
}

/* control is EOPNOTSUPP */

static void
uipc_detach(netmsg_t msg)
{
	struct unpcb *unp;
	int error;

	lwkt_gettoken(&unp_token);
	unp = msg->base.nm_so->so_pcb;
	if (unp) {
		unp_free(unp);
		error = 0;
	} else {
		error = EINVAL;
	}
	lwkt_reltoken(&unp_token);
	lwkt_replymsg(&msg->lmsg, error);
}

static void
uipc_disconnect(netmsg_t msg)
{
	struct unpcb *unp;
	int error;

	lwkt_gettoken(&unp_token);
	unp = msg->base.nm_so->so_pcb;
	if (unp) {
		unp_disconnect(unp);
		error = 0;
	} else {
		error = EINVAL;
	}
	lwkt_reltoken(&unp_token);
	lwkt_replymsg(&msg->lmsg, error);
}

static void
uipc_listen(netmsg_t msg)
{
	struct unpcb *unp;
	int error;

	lwkt_gettoken(&unp_token);
	unp = msg->base.nm_so->so_pcb;
	if (unp == NULL || unp->unp_vnode == NULL)
		error = EINVAL;
	else
		error = unp_listen(unp, msg->listen.nm_td);
	lwkt_reltoken(&unp_token);
	lwkt_replymsg(&msg->lmsg, error);
}

static void
uipc_peeraddr(netmsg_t msg)
{
	struct unpcb *unp;
	int error;

	lwkt_gettoken(&unp_token);
	unp = msg->base.nm_so->so_pcb;
	if (unp == NULL) {
		error = EINVAL;
	} else if (unp->unp_conn && unp->unp_conn->unp_addr) {
		struct unpcb *unp2 = unp->unp_conn;

		unp_reference(unp2);
		*msg->peeraddr.nm_nam = dup_sockaddr(
				(struct sockaddr *)unp2->unp_addr);
		unp_free(unp2);
		error = 0;
	} else {
		/*
		 * XXX: It seems that this test always fails even when
		 * connection is established.  So, this else clause is
		 * added as workaround to return PF_LOCAL sockaddr.
		 */
		*msg->peeraddr.nm_nam = dup_sockaddr(&sun_noname);
		error = 0;
	}
	lwkt_reltoken(&unp_token);
	lwkt_replymsg(&msg->lmsg, error);
}

static void
uipc_rcvd(netmsg_t msg)
{
	struct unpcb *unp, *unp2;
	struct socket *so;
	struct socket *so2;
	int error;

	/*
	 * so_pcb is only modified with both the global and the unp
	 * pool token held.  The unp pointer is invalid until we verify
	 * that it is good by re-checking so_pcb AFTER obtaining the token.
	 */
	so = msg->base.nm_so;
	while ((unp = so->so_pcb) != NULL) {
		lwkt_getpooltoken(unp);
		if (unp == so->so_pcb)
			break;
		lwkt_relpooltoken(unp);
	}
	if (unp == NULL) {
		error = EINVAL;
		goto done;
	}
	/* pool token held */

	switch (so->so_type) {
	case SOCK_DGRAM:
		panic("uipc_rcvd DGRAM?");
		/*NOTREACHED*/
	case SOCK_STREAM:
	case SOCK_SEQPACKET:
		if (unp->unp_conn == NULL)
			break;
		unp2 = unp->unp_conn;	/* protected by pool token */

		/*
		 * Because we are transfering mbufs directly to the
		 * peer socket we have to use SSB_STOP on the sender
		 * to prevent it from building up infinite mbufs.
		 *
		 * As in several places in this module w ehave to ref unp2
		 * to ensure that it does not get ripped out from under us
		 * if we block on the so2 token or in sowwakeup().
		 */
		so2 = unp2->unp_socket;
		unp_reference(unp2);
		lwkt_gettoken(&so2->so_rcv.ssb_token);
		if (so->so_rcv.ssb_cc < so2->so_snd.ssb_hiwat &&
		    so->so_rcv.ssb_mbcnt < so2->so_snd.ssb_mbmax
		) {
			atomic_clear_int(&so2->so_snd.ssb_flags, SSB_STOP);

			sowwakeup(so2);
		}
		lwkt_reltoken(&so2->so_rcv.ssb_token);
		unp_free(unp2);
		break;
	default:
		panic("uipc_rcvd unknown socktype");
		/*NOTREACHED*/
	}
	error = 0;
	lwkt_relpooltoken(unp);
done:
	lwkt_replymsg(&msg->lmsg, error);
}

/* pru_rcvoob is EOPNOTSUPP */

static void
uipc_send(netmsg_t msg)
{
	struct unpcb *unp, *unp2;
	struct socket *so;
	struct socket *so2;
	struct mbuf *control;
	struct mbuf *m;
	int error = 0;

	so = msg->base.nm_so;
	control = msg->send.nm_control;
	m = msg->send.nm_m;

	/*
	 * so_pcb is only modified with both the global and the unp
	 * pool token held.  The unp pointer is invalid until we verify
	 * that it is good by re-checking so_pcb AFTER obtaining the token.
	 */
	so = msg->base.nm_so;
	while ((unp = so->so_pcb) != NULL) {
		lwkt_getpooltoken(unp);
		if (unp == so->so_pcb)
			break;
		lwkt_relpooltoken(unp);
	}
	if (unp == NULL) {
		error = EINVAL;
		goto done;
	}
	/* pool token held */

	if (msg->send.nm_flags & PRUS_OOB) {
		error = EOPNOTSUPP;
		goto release;
	}

	wakeup_start_delayed();

	if (control && (error = unp_internalize(control, msg->send.nm_td)))
		goto release;

	switch (so->so_type) {
	case SOCK_DGRAM: 
	{
		struct sockaddr *from;

		if (msg->send.nm_addr) {
			if (unp->unp_conn) {
				error = EISCONN;
				break;
			}
			error = unp_connect(so,
					    msg->send.nm_addr,
					    msg->send.nm_td);
			if (error)
				break;
		} else {
			if (unp->unp_conn == NULL) {
				error = ENOTCONN;
				break;
			}
		}
		unp2 = unp->unp_conn;
		so2 = unp2->unp_socket;
		if (unp->unp_addr)
			from = (struct sockaddr *)unp->unp_addr;
		else
			from = &sun_noname;

		unp_reference(unp2);

		lwkt_gettoken(&so2->so_rcv.ssb_token);
		if (ssb_appendaddr(&so2->so_rcv, from, m, control)) {
			sorwakeup(so2);
			m = NULL;
			control = NULL;
		} else {
			error = ENOBUFS;
		}
		if (msg->send.nm_addr)
			unp_disconnect(unp);
		lwkt_reltoken(&so2->so_rcv.ssb_token);

		unp_free(unp2);
		break;
	}

	case SOCK_STREAM:
	case SOCK_SEQPACKET:
		/* Connect if not connected yet. */
		/*
		 * Note: A better implementation would complain
		 * if not equal to the peer's address.
		 */
		if (!(so->so_state & SS_ISCONNECTED)) {
			if (msg->send.nm_addr) {
				error = unp_connect(so,
						    msg->send.nm_addr,
						    msg->send.nm_td);
				if (error)
					break;	/* XXX */
			} else {
				error = ENOTCONN;
				break;
			}
		}

		if (so->so_state & SS_CANTSENDMORE) {
			error = EPIPE;
			break;
		}
		if (unp->unp_conn == NULL)
			panic("uipc_send connected but no connection?");
		unp2 = unp->unp_conn;
		so2 = unp2->unp_socket;

		unp_reference(unp2);

		/*
		 * Send to paired receive port, and then reduce
		 * send buffer hiwater marks to maintain backpressure.
		 * Wake up readers.
		 */
		lwkt_gettoken(&so2->so_rcv.ssb_token);
		if (control) {
			if (ssb_appendcontrol(&so2->so_rcv, m, control)) {
				control = NULL;
				m = NULL;
			}
		} else if (so->so_type == SOCK_SEQPACKET) {
			sbappendrecord(&so2->so_rcv.sb, m);
			m = NULL;
		} else {
			sbappend(&so2->so_rcv.sb, m);
			m = NULL;
		}

		/*
		 * Because we are transfering mbufs directly to the
		 * peer socket we have to use SSB_STOP on the sender
		 * to prevent it from building up infinite mbufs.
		 */
		if (so2->so_rcv.ssb_cc >= so->so_snd.ssb_hiwat ||
		    so2->so_rcv.ssb_mbcnt >= so->so_snd.ssb_mbmax
		) {
			atomic_set_int(&so->so_snd.ssb_flags, SSB_STOP);
		}
		lwkt_reltoken(&so2->so_rcv.ssb_token);
		sorwakeup(so2);

		unp_free(unp2);
		break;

	default:
		panic("uipc_send unknown socktype");
	}

	/*
	 * SEND_EOF is equivalent to a SEND followed by a SHUTDOWN.
	 */
	if (msg->send.nm_flags & PRUS_EOF) {
		socantsendmore(so);
		unp_shutdown(unp);
	}

	if (control && error != 0)
		unp_dispose(control);
release:
	lwkt_relpooltoken(unp);
	wakeup_end_delayed();
done:

	if (control)
		m_freem(control);
	if (m)
		m_freem(m);
	lwkt_replymsg(&msg->lmsg, error);
}

/*
 * MPSAFE
 */
static void
uipc_sense(netmsg_t msg)
{
	struct unpcb *unp;
	struct socket *so;
	struct stat *sb;
	int error;

	so = msg->base.nm_so;
	sb = msg->sense.nm_stat;

	/*
	 * so_pcb is only modified with both the global and the unp
	 * pool token held.  The unp pointer is invalid until we verify
	 * that it is good by re-checking so_pcb AFTER obtaining the token.
	 */
	while ((unp = so->so_pcb) != NULL) {
		lwkt_getpooltoken(unp);
		if (unp == so->so_pcb)
			break;
		lwkt_relpooltoken(unp);
	}
	if (unp == NULL) {
		error = EINVAL;
		goto done;
	}
	/* pool token held */

	sb->st_blksize = so->so_snd.ssb_hiwat;
	sb->st_dev = NOUDEV;
	if (unp->unp_ino == 0) {	/* make up a non-zero inode number */
		spin_lock(&unp_ino_spin);
		unp->unp_ino = unp_ino++;
		spin_unlock(&unp_ino_spin);
	}
	sb->st_ino = unp->unp_ino;
	error = 0;
	lwkt_relpooltoken(unp);
done:
	lwkt_replymsg(&msg->lmsg, error);
}

static void
uipc_shutdown(netmsg_t msg)
{
	struct socket *so;
	struct unpcb *unp;
	int error;

	/*
	 * so_pcb is only modified with both the global and the unp
	 * pool token held.  The unp pointer is invalid until we verify
	 * that it is good by re-checking so_pcb AFTER obtaining the token.
	 */
	so = msg->base.nm_so;
	while ((unp = so->so_pcb) != NULL) {
		lwkt_getpooltoken(unp);
		if (unp == so->so_pcb)
			break;
		lwkt_relpooltoken(unp);
	}
	if (unp) {
		/* pool token held */
		socantsendmore(so);
		unp_shutdown(unp);
		lwkt_relpooltoken(unp);
		error = 0;
	} else {
		error = EINVAL;
	}
	lwkt_replymsg(&msg->lmsg, error);
}

static void
uipc_sockaddr(netmsg_t msg)
{
	struct socket *so;
	struct unpcb *unp;
	int error;

	/*
	 * so_pcb is only modified with both the global and the unp
	 * pool token held.  The unp pointer is invalid until we verify
	 * that it is good by re-checking so_pcb AFTER obtaining the token.
	 */
	so = msg->base.nm_so;
	while ((unp = so->so_pcb) != NULL) {
		lwkt_getpooltoken(unp);
		if (unp == so->so_pcb)
			break;
		lwkt_relpooltoken(unp);
	}
	if (unp) {
		/* pool token held */
		if (unp->unp_addr) {
			*msg->sockaddr.nm_nam =
				dup_sockaddr((struct sockaddr *)unp->unp_addr);
		}
		lwkt_relpooltoken(unp);
		error = 0;
	} else {
		error = EINVAL;
	}
	lwkt_replymsg(&msg->lmsg, error);
}

struct pr_usrreqs uipc_usrreqs = {
	.pru_abort = uipc_abort,
	.pru_accept = uipc_accept,
	.pru_attach = uipc_attach,
	.pru_bind = uipc_bind,
	.pru_connect = uipc_connect,
	.pru_connect2 = uipc_connect2,
	.pru_control = pr_generic_notsupp,
	.pru_detach = uipc_detach,
	.pru_disconnect = uipc_disconnect,
	.pru_listen = uipc_listen,
	.pru_peeraddr = uipc_peeraddr,
	.pru_rcvd = uipc_rcvd,
	.pru_rcvoob = pr_generic_notsupp,
	.pru_send = uipc_send,
	.pru_sense = uipc_sense,
	.pru_shutdown = uipc_shutdown,
	.pru_sockaddr = uipc_sockaddr,
	.pru_sosend = sosend,
	.pru_soreceive = soreceive
};

void
uipc_ctloutput(netmsg_t msg)
{
	struct socket *so;
	struct sockopt *sopt;
	struct unpcb *unp;
	int error = 0;

	lwkt_gettoken(&unp_token);
	so = msg->base.nm_so;
	sopt = msg->ctloutput.nm_sopt;
	unp = so->so_pcb;

	switch (sopt->sopt_dir) {
	case SOPT_GET:
		switch (sopt->sopt_name) {
		case LOCAL_PEERCRED:
			if (unp->unp_flags & UNP_HAVEPC)
				soopt_from_kbuf(sopt, &unp->unp_peercred,
						sizeof(unp->unp_peercred));
			else {
				if (so->so_type == SOCK_STREAM)
					error = ENOTCONN;
				else if (so->so_type == SOCK_SEQPACKET)
					error = ENOTCONN;
				else
					error = EINVAL;
			}
			break;
		default:
			error = EOPNOTSUPP;
			break;
		}
		break;
	case SOPT_SET:
	default:
		error = EOPNOTSUPP;
		break;
	}
	lwkt_reltoken(&unp_token);
	lwkt_replymsg(&msg->lmsg, error);
}
	
/*
 * Both send and receive buffers are allocated PIPSIZ bytes of buffering
 * for stream sockets, although the total for sender and receiver is
 * actually only PIPSIZ.
 *
 * Datagram sockets really use the sendspace as the maximum datagram size,
 * and don't really want to reserve the sendspace.  Their recvspace should
 * be large enough for at least one max-size datagram plus address.
 *
 * We want the local send/recv space to be significant larger then lo0's
 * mtu of 16384.
 */
#ifndef PIPSIZ
#define	PIPSIZ	57344
#endif
static u_long	unpst_sendspace = PIPSIZ;
static u_long	unpst_recvspace = PIPSIZ;
static u_long	unpdg_sendspace = 2*1024;	/* really max datagram size */
static u_long	unpdg_recvspace = 4*1024;

static int	unp_rights;			/* file descriptors in flight */
static struct spinlock unp_spin = SPINLOCK_INITIALIZER(&unp_spin, "unp_spin");

SYSCTL_DECL(_net_local_seqpacket);
SYSCTL_DECL(_net_local_stream);
SYSCTL_INT(_net_local_stream, OID_AUTO, sendspace, CTLFLAG_RW, 
    &unpst_sendspace, 0, "Size of stream socket send buffer");
SYSCTL_INT(_net_local_stream, OID_AUTO, recvspace, CTLFLAG_RW,
    &unpst_recvspace, 0, "Size of stream socket receive buffer");

SYSCTL_DECL(_net_local_dgram);
SYSCTL_INT(_net_local_dgram, OID_AUTO, maxdgram, CTLFLAG_RW,
    &unpdg_sendspace, 0, "Max datagram socket size");
SYSCTL_INT(_net_local_dgram, OID_AUTO, recvspace, CTLFLAG_RW,
    &unpdg_recvspace, 0, "Size of datagram socket receive buffer");

SYSCTL_DECL(_net_local);
SYSCTL_INT(_net_local, OID_AUTO, inflight, CTLFLAG_RD, &unp_rights, 0,
   "File descriptors in flight");

static int
unp_attach(struct socket *so, struct pru_attach_info *ai)
{
	struct unpcb *unp;
	int error;

	lwkt_gettoken(&unp_token);

	if (so->so_snd.ssb_hiwat == 0 || so->so_rcv.ssb_hiwat == 0) {
		switch (so->so_type) {
		case SOCK_STREAM:
		case SOCK_SEQPACKET:
			error = soreserve(so, unpst_sendspace, unpst_recvspace,
					  ai->sb_rlimit);
			break;

		case SOCK_DGRAM:
			error = soreserve(so, unpdg_sendspace, unpdg_recvspace,
					  ai->sb_rlimit);
			break;

		default:
			panic("unp_attach");
		}
		if (error)
			goto failed;
	}

	/*
	 * In order to support sendfile we have to set either SSB_STOPSUPP
	 * or SSB_PREALLOC.  Unix domain sockets use the SSB_STOP flow
	 * control mechanism.
	 */
	if (so->so_type == SOCK_STREAM) {
		atomic_set_int(&so->so_rcv.ssb_flags, SSB_STOPSUPP);
		atomic_set_int(&so->so_snd.ssb_flags, SSB_STOPSUPP);
	}

	unp = kmalloc(sizeof(*unp), M_UNPCB, M_WAITOK | M_ZERO | M_NULLOK);
	if (unp == NULL) {
		error = ENOBUFS;
		goto failed;
	}
	unp->unp_refcnt = 1;
	unp->unp_gencnt = ++unp_gencnt;
	unp_count++;
	LIST_INIT(&unp->unp_refs);
	unp->unp_socket = so;
	unp->unp_rvnode = ai->fd_rdir;		/* jail cruft XXX JH */
	LIST_INSERT_HEAD(so->so_type == SOCK_DGRAM ? &unp_dhead
			 : &unp_shead, unp, unp_link);
	so->so_pcb = (caddr_t)unp;
	soreference(so);
	error = 0;
failed:
	lwkt_reltoken(&unp_token);
	return error;
}

static void
unp_detach(struct unpcb *unp)
{
	struct socket *so;

	lwkt_gettoken(&unp_token);
	lwkt_getpooltoken(unp);

	LIST_REMOVE(unp, unp_link);	/* both tokens required */
	unp->unp_gencnt = ++unp_gencnt;
	--unp_count;
	if (unp->unp_vnode) {
		unp->unp_vnode->v_socket = NULL;
		vrele(unp->unp_vnode);
		unp->unp_vnode = NULL;
	}
	if (unp->unp_conn)
		unp_disconnect(unp);
	while (!LIST_EMPTY(&unp->unp_refs))
		unp_drop(LIST_FIRST(&unp->unp_refs), ECONNRESET);
	soisdisconnected(unp->unp_socket);
	so = unp->unp_socket;
	soreference(so);		/* for delayed sorflush */
	KKASSERT(so->so_pcb == unp);
	so->so_pcb = NULL;		/* both tokens required */
	unp->unp_socket = NULL;
	sofree(so);		/* remove pcb ref */

	if (unp_rights) {
		/*
		 * Normally the receive buffer is flushed later,
		 * in sofree, but if our receive buffer holds references
		 * to descriptors that are now garbage, we will dispose
		 * of those descriptor references after the garbage collector
		 * gets them (resulting in a "panic: closef: count < 0").
		 */
		sorflush(so);
		unp_gc();
	}
	sofree(so);
	lwkt_relpooltoken(unp);
	lwkt_reltoken(&unp_token);

	if (unp->unp_addr)
		kfree(unp->unp_addr, M_SONAME);
	kfree(unp, M_UNPCB);
}

static int
unp_bind(struct unpcb *unp, struct sockaddr *nam, struct thread *td)
{
	struct proc *p = td->td_proc;
	struct sockaddr_un *soun = (struct sockaddr_un *)nam;
	struct vnode *vp;
	struct vattr vattr;
	int error, namelen;
	struct nlookupdata nd;
	char buf[SOCK_MAXADDRLEN];

	lwkt_gettoken(&unp_token);
	if (unp->unp_vnode != NULL) {
		error = EINVAL;
		goto failed;
	}
	namelen = soun->sun_len - offsetof(struct sockaddr_un, sun_path);
	if (namelen <= 0) {
		error = EINVAL;
		goto failed;
	}
	strncpy(buf, soun->sun_path, namelen);
	buf[namelen] = 0;	/* null-terminate the string */
	error = nlookup_init(&nd, buf, UIO_SYSSPACE,
			     NLC_LOCKVP | NLC_CREATE | NLC_REFDVP);
	if (error == 0)
		error = nlookup(&nd);
	if (error == 0 && nd.nl_nch.ncp->nc_vp != NULL)
		error = EADDRINUSE;
	if (error)
		goto done;

	VATTR_NULL(&vattr);
	vattr.va_type = VSOCK;
	vattr.va_mode = (ACCESSPERMS & ~p->p_fd->fd_cmask);
	error = VOP_NCREATE(&nd.nl_nch, nd.nl_dvp, &vp, nd.nl_cred, &vattr);
	if (error == 0) {
		if (unp->unp_vnode == NULL) {
			vp->v_socket = unp->unp_socket;
			unp->unp_vnode = vp;
			unp->unp_addr = (struct sockaddr_un *)dup_sockaddr(nam);
			vn_unlock(vp);
		} else {
			vput(vp);		/* late race */
			error = EINVAL;
		}
	}
done:
	nlookup_done(&nd);
failed:
	lwkt_reltoken(&unp_token);
	return (error);
}

static int
unp_connect(struct socket *so, struct sockaddr *nam, struct thread *td)
{
	struct proc *p = td->td_proc;
	struct sockaddr_un *soun = (struct sockaddr_un *)nam;
	struct vnode *vp;
	struct socket *so2, *so3;
	struct unpcb *unp, *unp2, *unp3;
	int error, len;
	struct nlookupdata nd;
	char buf[SOCK_MAXADDRLEN];

	lwkt_gettoken(&unp_token);

	len = nam->sa_len - offsetof(struct sockaddr_un, sun_path);
	if (len <= 0) {
		error = EINVAL;
		goto failed;
	}
	strncpy(buf, soun->sun_path, len);
	buf[len] = 0;

	vp = NULL;
	error = nlookup_init(&nd, buf, UIO_SYSSPACE, NLC_FOLLOW);
	if (error == 0)
		error = nlookup(&nd);
	if (error == 0)
		error = cache_vget(&nd.nl_nch, nd.nl_cred, LK_EXCLUSIVE, &vp);
	nlookup_done(&nd);
	if (error)
		goto failed;

	if (vp->v_type != VSOCK) {
		error = ENOTSOCK;
		goto bad;
	}
	error = VOP_EACCESS(vp, VWRITE, p->p_ucred);
	if (error)
		goto bad;
	so2 = vp->v_socket;
	if (so2 == NULL) {
		error = ECONNREFUSED;
		goto bad;
	}
	if (so->so_type != so2->so_type) {
		error = EPROTOTYPE;
		goto bad;
	}
	if (so->so_proto->pr_flags & PR_CONNREQUIRED) {
		if (!(so2->so_options & SO_ACCEPTCONN) ||
		    (so3 = sonewconn(so2, 0)) == NULL) {
			error = ECONNREFUSED;
			goto bad;
		}
		unp = so->so_pcb;
		if (unp->unp_conn) {	/* race, already connected! */
			error = EISCONN;
			sofree(so3);
			goto bad;
		}
		unp2 = so2->so_pcb;
		unp3 = so3->so_pcb;
		if (unp2->unp_addr)
			unp3->unp_addr = (struct sockaddr_un *)
				dup_sockaddr((struct sockaddr *)unp2->unp_addr);

		/*
		 * unp_peercred management:
		 *
		 * The connecter's (client's) credentials are copied
		 * from its process structure at the time of connect()
		 * (which is now).
		 */
		cru2x(p->p_ucred, &unp3->unp_peercred);
		unp3->unp_flags |= UNP_HAVEPC;
		/*
		 * The receiver's (server's) credentials are copied
		 * from the unp_peercred member of socket on which the
		 * former called listen(); unp_listen() cached that
		 * process's credentials at that time so we can use
		 * them now.
		 */
		KASSERT(unp2->unp_flags & UNP_HAVEPCCACHED,
		    ("unp_connect: listener without cached peercred"));
		memcpy(&unp->unp_peercred, &unp2->unp_peercred,
		    sizeof(unp->unp_peercred));
		unp->unp_flags |= UNP_HAVEPC;

		so2 = so3;
	}
	error = unp_connect2(so, so2);
bad:
	vput(vp);
failed:
	lwkt_reltoken(&unp_token);
	return (error);
}

/*
 * Connect two unix domain sockets together.
 *
 * NOTE: Semantics for any change to unp_conn requires that the per-unp
 *	 pool token also be held.
 */
int
unp_connect2(struct socket *so, struct socket *so2)
{
	struct unpcb *unp;
	struct unpcb *unp2;

	lwkt_gettoken(&unp_token);
	unp = so->so_pcb;
	if (so2->so_type != so->so_type) {
		lwkt_reltoken(&unp_token);
		return (EPROTOTYPE);
	}
	unp2 = so2->so_pcb;
	lwkt_getpooltoken(unp);
	lwkt_getpooltoken(unp2);

	unp->unp_conn = unp2;

	switch (so->so_type) {
	case SOCK_DGRAM:
		LIST_INSERT_HEAD(&unp2->unp_refs, unp, unp_reflink);
		soisconnected(so);
		break;

	case SOCK_STREAM:
	case SOCK_SEQPACKET:
		unp2->unp_conn = unp;
		soisconnected(so);
		soisconnected(so2);
		break;

	default:
		panic("unp_connect2");
	}
	lwkt_relpooltoken(unp2);
	lwkt_relpooltoken(unp);
	lwkt_reltoken(&unp_token);
	return (0);
}

/*
 * Disconnect a unix domain socket pair.
 *
 * NOTE: Semantics for any change to unp_conn requires that the per-unp
 *	 pool token also be held.
 */
static void
unp_disconnect(struct unpcb *unp)
{
	struct unpcb *unp2;

	lwkt_gettoken(&unp_token);
	lwkt_getpooltoken(unp);

	while ((unp2 = unp->unp_conn) != NULL) {
		lwkt_getpooltoken(unp2);
		if (unp2 == unp->unp_conn)
			break;
		lwkt_relpooltoken(unp2);
	}
	if (unp2 == NULL)
		goto done;

	unp->unp_conn = NULL;

	switch (unp->unp_socket->so_type) {
	case SOCK_DGRAM:
		LIST_REMOVE(unp, unp_reflink);
		soclrstate(unp->unp_socket, SS_ISCONNECTED);
		break;

	case SOCK_STREAM:
	case SOCK_SEQPACKET:
		unp_reference(unp2);
		unp2->unp_conn = NULL;

		soisdisconnected(unp->unp_socket);
		soisdisconnected(unp2->unp_socket);

		unp_free(unp2);
		break;
	}
	lwkt_relpooltoken(unp2);
done:
	lwkt_relpooltoken(unp);
	lwkt_reltoken(&unp_token);
}

#ifdef notdef
void
unp_abort(struct unpcb *unp)
{
	lwkt_gettoken(&unp_token);
	unp_free(unp);
	lwkt_reltoken(&unp_token);
}
#endif

static int
prison_unpcb(struct thread *td, struct unpcb *unp)
{
	struct proc *p;

	if (td == NULL)
		return (0);
	if ((p = td->td_proc) == NULL)
		return (0);
	if (!p->p_ucred->cr_prison)
		return (0);
	if (p->p_fd->fd_rdir == unp->unp_rvnode)
		return (0);
	return (1);
}

static int
unp_pcblist(SYSCTL_HANDLER_ARGS)
{
	int error, i, n;
	struct unpcb *unp, **unp_list;
	unp_gen_t gencnt;
	struct unp_head *head;

	head = ((intptr_t)arg1 == SOCK_DGRAM ? &unp_dhead : &unp_shead);

	KKASSERT(curproc != NULL);

	/*
	 * The process of preparing the PCB list is too time-consuming and
	 * resource-intensive to repeat twice on every request.
	 */
	if (req->oldptr == NULL) {
		n = unp_count;
		req->oldidx = (n + n/8) * sizeof(struct xunpcb);
		return 0;
	}

	if (req->newptr != NULL)
		return EPERM;

	lwkt_gettoken(&unp_token);

	/*
	 * OK, now we're committed to doing something.
	 */
	gencnt = unp_gencnt;
	n = unp_count;

	unp_list = kmalloc(n * sizeof *unp_list, M_TEMP, M_WAITOK);
	
	for (unp = LIST_FIRST(head), i = 0; unp && i < n;
	     unp = LIST_NEXT(unp, unp_link)) {
		if (unp->unp_gencnt <= gencnt && !prison_unpcb(req->td, unp))
			unp_list[i++] = unp;
	}
	n = i;			/* in case we lost some during malloc */

	error = 0;
	for (i = 0; i < n; i++) {
		unp = unp_list[i];
		if (unp->unp_gencnt <= gencnt) {
			struct xunpcb xu;
			xu.xu_len = sizeof xu;
			xu.xu_unpp = unp;
			/*
			 * XXX - need more locking here to protect against
			 * connect/disconnect races for SMP.
			 */
			if (unp->unp_addr)
				bcopy(unp->unp_addr, &xu.xu_addr, 
				      unp->unp_addr->sun_len);
			if (unp->unp_conn && unp->unp_conn->unp_addr)
				bcopy(unp->unp_conn->unp_addr,
				      &xu.xu_caddr,
				      unp->unp_conn->unp_addr->sun_len);
			bcopy(unp, &xu.xu_unp, sizeof *unp);
			sotoxsocket(unp->unp_socket, &xu.xu_socket);
			error = SYSCTL_OUT(req, &xu, sizeof xu);
		}
	}
	lwkt_reltoken(&unp_token);
	kfree(unp_list, M_TEMP);

	return error;
}

SYSCTL_PROC(_net_local_dgram, OID_AUTO, pcblist, CTLFLAG_RD, 
	    (caddr_t)(long)SOCK_DGRAM, 0, unp_pcblist, "S,xunpcb",
	    "List of active local datagram sockets");
SYSCTL_PROC(_net_local_stream, OID_AUTO, pcblist, CTLFLAG_RD, 
	    (caddr_t)(long)SOCK_STREAM, 0, unp_pcblist, "S,xunpcb",
	    "List of active local stream sockets");
SYSCTL_PROC(_net_local_seqpacket, OID_AUTO, pcblist, CTLFLAG_RD, 
	    (caddr_t)(long)SOCK_SEQPACKET, 0, unp_pcblist, "S,xunpcb",
	    "List of active local seqpacket stream sockets");

static void
unp_shutdown(struct unpcb *unp)
{
	struct socket *so;

	if ((unp->unp_socket->so_type == SOCK_STREAM ||
	     unp->unp_socket->so_type == SOCK_SEQPACKET) &&
	    unp->unp_conn != NULL && (so = unp->unp_conn->unp_socket)) {
		socantrcvmore(so);
	}
}

static void
unp_drop(struct unpcb *unp, int err)
{
	struct socket *so = unp->unp_socket;

	so->so_error = err;
	unp_disconnect(unp);
}

#ifdef notdef
void
unp_drain(void)
{
	lwkt_gettoken(&unp_token);
	lwkt_reltoken(&unp_token);
}
#endif

int
unp_externalize(struct mbuf *rights)
{
	struct thread *td = curthread;
	struct proc *p = td->td_proc;		/* XXX */
	struct lwp *lp = td->td_lwp;
	struct cmsghdr *cm = mtod(rights, struct cmsghdr *);
	int *fdp;
	int i;
	struct file **rp;
	struct file *fp;
	int newfds = (cm->cmsg_len - (CMSG_DATA(cm) - (u_char *)cm))
		/ sizeof (struct file *);
	int f;

	lwkt_gettoken(&unp_token);

	/*
	 * if the new FD's will not fit, then we free them all
	 */
	if (!fdavail(p, newfds)) {
		rp = (struct file **)CMSG_DATA(cm);
		for (i = 0; i < newfds; i++) {
			fp = *rp;
			/*
			 * zero the pointer before calling unp_discard,
			 * since it may end up in unp_gc()..
			 */
			*rp++ = NULL;
			unp_discard(fp, NULL);
		}
		lwkt_reltoken(&unp_token);
		return (EMSGSIZE);
	}

	/*
	 * now change each pointer to an fd in the global table to 
	 * an integer that is the index to the local fd table entry
	 * that we set up to point to the global one we are transferring.
	 * If sizeof (struct file *) is bigger than or equal to sizeof int,
	 * then do it in forward order. In that case, an integer will
	 * always come in the same place or before its corresponding
	 * struct file pointer.
	 * If sizeof (struct file *) is smaller than sizeof int, then
	 * do it in reverse order.
	 */
	if (sizeof (struct file *) >= sizeof (int)) {
		fdp = (int *)CMSG_DATA(cm);
		rp = (struct file **)CMSG_DATA(cm);
		for (i = 0; i < newfds; i++) {
			if (fdalloc(p, 0, &f))
				panic("unp_externalize");
			fp = *rp++;
			unp_fp_externalize(lp, fp, f);
			*fdp++ = f;
		}
	} else {
		fdp = (int *)CMSG_DATA(cm) + newfds - 1;
		rp = (struct file **)CMSG_DATA(cm) + newfds - 1;
		for (i = 0; i < newfds; i++) {
			if (fdalloc(p, 0, &f))
				panic("unp_externalize");
			fp = *rp--;
			unp_fp_externalize(lp, fp, f);
			*fdp-- = f;
		}
	}

	/*
	 * Adjust length, in case sizeof(struct file *) and sizeof(int)
	 * differs.
	 */
	cm->cmsg_len = CMSG_LEN(newfds * sizeof(int));
	rights->m_len = cm->cmsg_len;

	lwkt_reltoken(&unp_token);
	return (0);
}

static void
unp_fp_externalize(struct lwp *lp, struct file *fp, int fd)
{
	struct file *fx;
	int error;

	lwkt_gettoken(&unp_token);

	if (lp) {
		KKASSERT(fd >= 0);
		if (fp->f_flag & FREVOKED) {
			kprintf("Warning: revoked fp exiting unix socket\n");
			fx = NULL;
			error = falloc(lp, &fx, NULL);
			if (error == 0)
				fsetfd(lp->lwp_proc->p_fd, fx, fd);
			else
				fsetfd(lp->lwp_proc->p_fd, NULL, fd);
			fdrop(fx);
		} else {
			fsetfd(lp->lwp_proc->p_fd, fp, fd);
		}
	}
	spin_lock(&unp_spin);
	fp->f_msgcount--;
	unp_rights--;
	spin_unlock(&unp_spin);
	fdrop(fp);

	lwkt_reltoken(&unp_token);
}


void
unp_init(void)
{
	LIST_INIT(&unp_dhead);
	LIST_INIT(&unp_shead);
	spin_init(&unp_spin, "unpinit");
}

static int
unp_internalize(struct mbuf *control, struct thread *td)
{
	struct proc *p = td->td_proc;
	struct filedesc *fdescp;
	struct cmsghdr *cm = mtod(control, struct cmsghdr *);
	struct file **rp;
	struct file *fp;
	int i, fd, *fdp;
	struct cmsgcred *cmcred;
	int oldfds;
	u_int newlen;
	int error;

	KKASSERT(p);
	lwkt_gettoken(&unp_token);

	fdescp = p->p_fd;
	if ((cm->cmsg_type != SCM_RIGHTS && cm->cmsg_type != SCM_CREDS) ||
	    cm->cmsg_level != SOL_SOCKET ||
	    CMSG_ALIGN(cm->cmsg_len) != control->m_len) {
		error = EINVAL;
		goto done;
	}

	/*
	 * Fill in credential information.
	 */
	if (cm->cmsg_type == SCM_CREDS) {
		cmcred = (struct cmsgcred *)CMSG_DATA(cm);
		cmcred->cmcred_pid = p->p_pid;
		cmcred->cmcred_uid = p->p_ucred->cr_ruid;
		cmcred->cmcred_gid = p->p_ucred->cr_rgid;
		cmcred->cmcred_euid = p->p_ucred->cr_uid;
		cmcred->cmcred_ngroups = MIN(p->p_ucred->cr_ngroups,
							CMGROUP_MAX);
		for (i = 0; i < cmcred->cmcred_ngroups; i++)
			cmcred->cmcred_groups[i] = p->p_ucred->cr_groups[i];
		error = 0;
		goto done;
	}

	/*
	 * cmsghdr may not be aligned, do not allow calculation(s) to
	 * go negative.
	 */
	if (cm->cmsg_len < CMSG_LEN(0)) {
		error = EINVAL;
		goto done;
	}

	oldfds = (cm->cmsg_len - CMSG_LEN(0)) / sizeof (int);

	/*
	 * check that all the FDs passed in refer to legal OPEN files
	 * If not, reject the entire operation.
	 */
	fdp = (int *)CMSG_DATA(cm);
	for (i = 0; i < oldfds; i++) {
		fd = *fdp++;
		if ((unsigned)fd >= fdescp->fd_nfiles ||
		    fdescp->fd_files[fd].fp == NULL) {
			error = EBADF;
			goto done;
		}
		if (fdescp->fd_files[fd].fp->f_type == DTYPE_KQUEUE) {
			error = EOPNOTSUPP;
			goto done;
		}
	}
	/*
	 * Now replace the integer FDs with pointers to
	 * the associated global file table entry..
	 * Allocate a bigger buffer as necessary. But if an cluster is not
	 * enough, return E2BIG.
	 */
	newlen = CMSG_LEN(oldfds * sizeof(struct file *));
	if (newlen > MCLBYTES) {
		error = E2BIG;
		goto done;
	}
	if (newlen - control->m_len > M_TRAILINGSPACE(control)) {
		if (control->m_flags & M_EXT) {
			error = E2BIG;
			goto done;
		}
		MCLGET(control, M_WAITOK);
		if (!(control->m_flags & M_EXT)) {
			error = ENOBUFS;
			goto done;
		}

		/* copy the data to the cluster */
		memcpy(mtod(control, char *), cm, cm->cmsg_len);
		cm = mtod(control, struct cmsghdr *);
	}

	/*
	 * Adjust length, in case sizeof(struct file *) and sizeof(int)
	 * differs.
	 */
	cm->cmsg_len = newlen;
	control->m_len = CMSG_ALIGN(newlen);

	/*
	 * Transform the file descriptors into struct file pointers.
	 * If sizeof (struct file *) is bigger than or equal to sizeof int,
	 * then do it in reverse order so that the int won't get until
	 * we're done.
	 * If sizeof (struct file *) is smaller than sizeof int, then
	 * do it in forward order.
	 */
	if (sizeof (struct file *) >= sizeof (int)) {
		fdp = (int *)CMSG_DATA(cm) + oldfds - 1;
		rp = (struct file **)CMSG_DATA(cm) + oldfds - 1;
		for (i = 0; i < oldfds; i++) {
			fp = fdescp->fd_files[*fdp--].fp;
			*rp-- = fp;
			fhold(fp);
			spin_lock(&unp_spin);
			fp->f_msgcount++;
			unp_rights++;
			spin_unlock(&unp_spin);
		}
	} else {
		fdp = (int *)CMSG_DATA(cm);
		rp = (struct file **)CMSG_DATA(cm);
		for (i = 0; i < oldfds; i++) {
			fp = fdescp->fd_files[*fdp++].fp;
			*rp++ = fp;
			fhold(fp);
			spin_lock(&unp_spin);
			fp->f_msgcount++;
			unp_rights++;
			spin_unlock(&unp_spin);
		}
	}
	error = 0;
done:
	lwkt_reltoken(&unp_token);
	return error;
}

/*
 * Garbage collect in-transit file descriptors that get lost due to
 * loops (i.e. when a socket is sent to another process over itself,
 * and more complex situations).
 *
 * NOT MPSAFE - TODO socket flush code and maybe closef.  Rest is MPSAFE.
 */

struct unp_gc_info {
	struct file **extra_ref;
	struct file *locked_fp;
	int defer;
	int index;
	int maxindex;
};

static void
unp_gc(void)
{
	struct unp_gc_info info;
	static boolean_t unp_gcing;
	struct file **fpp;
	int i;

	/*
	 * Only one gc can be in-progress at any given moment
	 */
	spin_lock(&unp_spin);
	if (unp_gcing) {
		spin_unlock(&unp_spin);
		return;
	}
	unp_gcing = TRUE;
	spin_unlock(&unp_spin);

	lwkt_gettoken(&unp_token);

	/* 
	 * Before going through all this, set all FDs to be NOT defered
	 * and NOT externally accessible (not marked).  During the scan
	 * a fd can be marked externally accessible but we may or may not
	 * be able to immediately process it (controlled by FDEFER).
	 *
	 * If we loop sleep a bit.  The complexity of the topology can cause
	 * multiple loops.  Also failure to acquire the socket's so_rcv
	 * token can cause us to loop.
	 */
	allfiles_scan_exclusive(unp_gc_clearmarks, NULL);
	do {
		info.defer = 0;
		allfiles_scan_exclusive(unp_gc_checkmarks, &info);
		if (info.defer)
			tsleep(&info, 0, "gcagain", 1);
	} while (info.defer);

	/*
	 * We grab an extra reference to each of the file table entries
	 * that are not otherwise accessible and then free the rights
	 * that are stored in messages on them.
	 *
	 * The bug in the orginal code is a little tricky, so I'll describe
	 * what's wrong with it here.
	 *
	 * It is incorrect to simply unp_discard each entry for f_msgcount
	 * times -- consider the case of sockets A and B that contain
	 * references to each other.  On a last close of some other socket,
	 * we trigger a gc since the number of outstanding rights (unp_rights)
	 * is non-zero.  If during the sweep phase the gc code un_discards,
	 * we end up doing a (full) closef on the descriptor.  A closef on A
	 * results in the following chain.  Closef calls soo_close, which
	 * calls soclose.   Soclose calls first (through the switch
	 * uipc_usrreq) unp_detach, which re-invokes unp_gc.  Unp_gc simply
	 * returns because the previous instance had set unp_gcing, and
	 * we return all the way back to soclose, which marks the socket
	 * with SS_NOFDREF, and then calls sofree.  Sofree calls sorflush
	 * to free up the rights that are queued in messages on the socket A,
	 * i.e., the reference on B.  The sorflush calls via the dom_dispose
	 * switch unp_dispose, which unp_scans with unp_discard.  This second
	 * instance of unp_discard just calls closef on B.
	 *
	 * Well, a similar chain occurs on B, resulting in a sorflush on B,
	 * which results in another closef on A.  Unfortunately, A is already
	 * being closed, and the descriptor has already been marked with
	 * SS_NOFDREF, and soclose panics at this point.
	 *
	 * Here, we first take an extra reference to each inaccessible
	 * descriptor.  Then, we call sorflush ourself, since we know
	 * it is a Unix domain socket anyhow.  After we destroy all the
	 * rights carried in messages, we do a last closef to get rid
	 * of our extra reference.  This is the last close, and the
	 * unp_detach etc will shut down the socket.
	 *
	 * 91/09/19, bsy@cs.cmu.edu
	 */
	info.extra_ref = kmalloc(256 * sizeof(struct file *), M_FILE, M_WAITOK);
	info.maxindex = 256;

	do {
		/*
		 * Look for matches
		 */
		info.index = 0;
		allfiles_scan_exclusive(unp_gc_checkrefs, &info);

		/* 
		 * For each FD on our hit list, do the following two things
		 */
		for (i = info.index, fpp = info.extra_ref; --i >= 0; ++fpp) {
			struct file *tfp = *fpp;
			if (tfp->f_type == DTYPE_SOCKET && tfp->f_data != NULL)
				sorflush((struct socket *)(tfp->f_data));
		}
		for (i = info.index, fpp = info.extra_ref; --i >= 0; ++fpp)
			closef(*fpp, NULL);
	} while (info.index == info.maxindex);

	lwkt_reltoken(&unp_token);

	kfree((caddr_t)info.extra_ref, M_FILE);
	unp_gcing = FALSE;
}

/*
 * MPSAFE - NOTE: filehead list and file pointer spinlocked on entry
 */
static int
unp_gc_checkrefs(struct file *fp, void *data)
{
	struct unp_gc_info *info = data;

	if (fp->f_count == 0)
		return(0);
	if (info->index == info->maxindex)
		return(-1);

	/* 
	 * If all refs are from msgs, and it's not marked accessible
	 * then it must be referenced from some unreachable cycle
	 * of (shut-down) FDs, so include it in our
	 * list of FDs to remove
	 */
	if (fp->f_count == fp->f_msgcount && !(fp->f_flag & FMARK)) {
		info->extra_ref[info->index++] = fp;
		fhold(fp);
	}
	return(0);
}

/*
 * MPSAFE - NOTE: filehead list and file pointer spinlocked on entry
 */
static int
unp_gc_clearmarks(struct file *fp, void *data __unused)
{
	atomic_clear_int(&fp->f_flag, FMARK | FDEFER);
	return(0);
}

/*
 * MPSAFE - NOTE: filehead list and file pointer spinlocked on entry
 */
static int
unp_gc_checkmarks(struct file *fp, void *data)
{
	struct unp_gc_info *info = data;
	struct socket *so;

	/*
	 * If the file is not open, skip it.  Make sure it isn't marked
	 * defered or we could loop forever, in case we somehow race
	 * something.
	 */
	if (fp->f_count == 0) {
		if (fp->f_flag & FDEFER)
			atomic_clear_int(&fp->f_flag, FDEFER);
		return(0);
	}
	/*
	 * If we already marked it as 'defer'  in a
	 * previous pass, then try process it this time
	 * and un-mark it
	 */
	if (fp->f_flag & FDEFER) {
		atomic_clear_int(&fp->f_flag, FDEFER);
	} else {
		/*
		 * if it's not defered, then check if it's
		 * already marked.. if so skip it
		 */
		if (fp->f_flag & FMARK)
			return(0);
		/* 
		 * If all references are from messages
		 * in transit, then skip it. it's not 
		 * externally accessible.
		 */ 
		if (fp->f_count == fp->f_msgcount)
			return(0);
		/* 
		 * If it got this far then it must be
		 * externally accessible.
		 */
		atomic_set_int(&fp->f_flag, FMARK);
	}

	/*
	 * either it was defered, or it is externally 
	 * accessible and not already marked so.
	 * Now check if it is possibly one of OUR sockets.
	 */ 
	if (fp->f_type != DTYPE_SOCKET ||
	    (so = (struct socket *)fp->f_data) == NULL) {
		return(0);
	}
	if (so->so_proto->pr_domain != &localdomain ||
	    !(so->so_proto->pr_flags & PR_RIGHTS)) {
		return(0);
	}

	/*
	 * So, Ok, it's one of our sockets and it IS externally accessible
	 * (or was defered).  Now we look to see if we hold any file
	 * descriptors in its message buffers.  Follow those links and mark
	 * them as accessible too.
	 *
	 * We are holding multiple spinlocks here, if we cannot get the
	 * token non-blocking defer until the next loop.
	 */
	info->locked_fp = fp;
	if (lwkt_trytoken(&so->so_rcv.ssb_token)) {
		unp_scan(so->so_rcv.ssb_mb, unp_mark, info);
		lwkt_reltoken(&so->so_rcv.ssb_token);
	} else {
		atomic_set_int(&fp->f_flag, FDEFER);
		++info->defer;
	}
	return (0);
}

/*
 * Scan all unix domain sockets and replace any revoked file pointers
 * found with the dummy file pointer fx.  We don't worry about races
 * against file pointers being read out as those are handled in the
 * externalize code.
 */

#define REVOKE_GC_MAXFILES	32

struct unp_revoke_gc_info {
	struct file	*fx;
	struct file	*fary[REVOKE_GC_MAXFILES];
	int		fcount;
};

void
unp_revoke_gc(struct file *fx)
{
	struct unp_revoke_gc_info info;
	int i;

	lwkt_gettoken(&unp_token);
	info.fx = fx;
	do {
		info.fcount = 0;
		allfiles_scan_exclusive(unp_revoke_gc_check, &info);
		for (i = 0; i < info.fcount; ++i)
			unp_fp_externalize(NULL, info.fary[i], -1);
	} while (info.fcount == REVOKE_GC_MAXFILES);
	lwkt_reltoken(&unp_token);
}

/*
 * Check for and replace revoked descriptors.
 *
 * WARNING:  This routine is not allowed to block.
 */
static int
unp_revoke_gc_check(struct file *fps, void *vinfo)
{
	struct unp_revoke_gc_info *info = vinfo;
	struct file *fp;
	struct socket *so;
	struct mbuf *m0;
	struct mbuf *m;
	struct file **rp;
	struct cmsghdr *cm;
	int i;
	int qfds;

	/*
	 * Is this a unix domain socket with rights-passing abilities?
	 */
	if (fps->f_type != DTYPE_SOCKET)
		return (0);
	if ((so = (struct socket *)fps->f_data) == NULL)
		return(0);
	if (so->so_proto->pr_domain != &localdomain)
		return(0);
	if ((so->so_proto->pr_flags & PR_RIGHTS) == 0)
		return(0);

	/*
	 * Scan the mbufs for control messages and replace any revoked
	 * descriptors we find.
	 */
	lwkt_gettoken(&so->so_rcv.ssb_token);
	m0 = so->so_rcv.ssb_mb;
	while (m0) {
		for (m = m0; m; m = m->m_next) {
			if (m->m_type != MT_CONTROL)
				continue;
			if (m->m_len < sizeof(*cm))
				continue;
			cm = mtod(m, struct cmsghdr *);
			if (cm->cmsg_level != SOL_SOCKET ||
			    cm->cmsg_type != SCM_RIGHTS) {
				continue;
			}
			qfds = (cm->cmsg_len - CMSG_LEN(0)) / sizeof(void *);
			rp = (struct file **)CMSG_DATA(cm);
			for (i = 0; i < qfds; i++) {
				fp = rp[i];
				if (fp->f_flag & FREVOKED) {
					kprintf("Warning: Removing revoked fp from unix domain socket queue\n");
					fhold(info->fx);
					info->fx->f_msgcount++;
					unp_rights++;
					rp[i] = info->fx;
					info->fary[info->fcount++] = fp;
				}
				if (info->fcount == REVOKE_GC_MAXFILES)
					break;
			}
			if (info->fcount == REVOKE_GC_MAXFILES)
				break;
		}
		m0 = m0->m_nextpkt;
		if (info->fcount == REVOKE_GC_MAXFILES)
			break;
	}
	lwkt_reltoken(&so->so_rcv.ssb_token);

	/*
	 * Stop the scan if we filled up our array.
	 */
	if (info->fcount == REVOKE_GC_MAXFILES)
		return(-1);
	return(0);
}

/*
 * Dispose of the fp's stored in a mbuf.
 *
 * The dds loop can cause additional fps to be entered onto the
 * list while it is running, flattening out the operation and avoiding
 * a deep kernel stack recursion.
 */
void
unp_dispose(struct mbuf *m)
{
	unp_defdiscard_t dds;

	lwkt_gettoken(&unp_token);
	++unp_defdiscard_nest;
	if (m) {
		unp_scan(m, unp_discard, NULL);
	}
	if (unp_defdiscard_nest == 1) {
		while ((dds = unp_defdiscard_base) != NULL) {
			unp_defdiscard_base = dds->next;
			closef(dds->fp, NULL);
			kfree(dds, M_UNPCB);
		}
	}
	--unp_defdiscard_nest;
	lwkt_reltoken(&unp_token);
}

static int
unp_listen(struct unpcb *unp, struct thread *td)
{
	struct proc *p = td->td_proc;

	KKASSERT(p);
	lwkt_gettoken(&unp_token);
	cru2x(p->p_ucred, &unp->unp_peercred);
	unp->unp_flags |= UNP_HAVEPCCACHED;
	lwkt_reltoken(&unp_token);
	return (0);
}

static void
unp_scan(struct mbuf *m0, void (*op)(struct file *, void *), void *data)
{
	struct mbuf *m;
	struct file **rp;
	struct cmsghdr *cm;
	int i;
	int qfds;

	while (m0) {
		for (m = m0; m; m = m->m_next) {
			if (m->m_type == MT_CONTROL &&
			    m->m_len >= sizeof(*cm)) {
				cm = mtod(m, struct cmsghdr *);
				if (cm->cmsg_level != SOL_SOCKET ||
				    cm->cmsg_type != SCM_RIGHTS)
					continue;
				qfds = (cm->cmsg_len - CMSG_LEN(0)) /
					sizeof(void *);
				rp = (struct file **)CMSG_DATA(cm);
				for (i = 0; i < qfds; i++)
					(*op)(*rp++, data);
				break;		/* XXX, but saves time */
			}
		}
		m0 = m0->m_nextpkt;
	}
}

/*
 * Mark visibility.  info->defer is recalculated on every pass.
 */
static void
unp_mark(struct file *fp, void *data)
{
	struct unp_gc_info *info = data;

	if ((fp->f_flag & FMARK) == 0) {
		++info->defer;
		atomic_set_int(&fp->f_flag, FMARK | FDEFER);
	} else if (fp->f_flag & FDEFER) {
		++info->defer;
	}
}

/*
 * Discard a fp previously held in a unix domain socket mbuf.  To
 * avoid blowing out the kernel stack due to contrived chain-reactions
 * we may have to defer the operation to a higher procedural level.
 *
 * Caller holds unp_token
 */
static void
unp_discard(struct file *fp, void *data __unused)
{
	unp_defdiscard_t dds;

	spin_lock(&unp_spin);
	fp->f_msgcount--;
	unp_rights--;
	spin_unlock(&unp_spin);

	if (unp_defdiscard_nest) {
		dds = kmalloc(sizeof(*dds), M_UNPCB, M_WAITOK|M_ZERO);
		dds->fp = fp;
		dds->next = unp_defdiscard_base;
		unp_defdiscard_base = dds;
	} else {
		closef(fp, NULL);
	}
}

