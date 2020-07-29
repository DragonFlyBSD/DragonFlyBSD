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
#include <sys/kern_syscall.h>
#include <sys/taskqueue.h>

#include <sys/file2.h>
#include <sys/spinlock2.h>
#include <sys/socketvar2.h>
#include <sys/msgport2.h>

/*
 * Unix communications domain.
 *
 * TODO:
 *	RDM
 *	rethink name space problems
 *	need a proper out-of-band
 *	lock pushdown
 *
 *
 * Unix domain sockets GC.
 *
 * It was originally designed to address following three cases:
 * 1) Receiving unix domain socket can not accept the rights, e.g.
 *    when the so_rcv is full.
 * 2) Caller of recvmsg(2) does not pass buffer to receive rights.
 * 3) Unix domain sockets loop reference, e.g. s1 is on s2.so_rcv,
 *    while s2 on s1.so_rcv.
 *
 * Code under UNP_GC_ALLFILES is intended to address all above three
 * cases.  However, 1) was addressed a long time ago in uipc_send()
 * (we inheritted the fix from FreeBSD when DragonFly forked).  2)
 * was addressed in soreceive() by git-e62cfe62.  3) is the only
 * case that needs GC.  The new code (!UNP_GC_ALLFILES) addresses
 * case 3) in the following way:
 * - Record the struct file in unpcb, if the Unix domain socket is
 *   passed as one of the rights.
 * - At GC time, only unpcbs are scanned, and only Unix domain sockets
 *   that are still used as rights are potential GC targets.
 */

#define UNP_DETACHED		UNP_PRIVATE1
#define UNP_CONNECTING		UNP_PRIVATE2
#define UNP_DROPPED		UNP_PRIVATE3
#define UNP_MARKER		UNP_PRIVATE4

#define UNPGC_REF		0x1	/* unpcb has external ref. */
#define UNPGC_DEAD		0x2	/* unpcb might be dead. */
#define UNPGC_SCANNED		0x4	/* Has been scanned. */

#define UNP_GCFILE_MAX		256

/* For unp_internalize() and unp_externalize() */
CTASSERT(sizeof(struct file *) >= sizeof(int));

#define UNP_ISATTACHED(unp)	\
    ((unp) != NULL && ((unp)->unp_flags & UNP_DETACHED) == 0)

#ifdef INVARIANTS
#define UNP_ASSERT_TOKEN_HELD(unp) \
    ASSERT_LWKT_TOKEN_HELD(lwkt_token_pool_lookup((unp)))
#else	/* !INVARIANTS */
#define UNP_ASSERT_TOKEN_HELD(unp)
#endif	/* INVARIANTS */

struct unp_defdiscard {
	SLIST_ENTRY(unp_defdiscard) next;
	struct file *fp;
};
SLIST_HEAD(unp_defdiscard_list, unp_defdiscard);

TAILQ_HEAD(unpcb_qhead, unpcb);
struct unp_global_head {
	struct unpcb_qhead	list;
	int			count;
};

static	MALLOC_DEFINE(M_UNPCB, "unpcb", "unpcb struct");
static	unp_gen_t unp_gencnt;

static struct unp_global_head unp_stream_head;
static struct unp_global_head unp_dgram_head;
static struct unp_global_head unp_seqpkt_head;

static struct unp_global_head * const unp_heads[] =
    { &unp_stream_head, &unp_dgram_head, &unp_seqpkt_head, NULL };

static struct lwkt_token unp_token = LWKT_TOKEN_INITIALIZER(unp_token);
static struct taskqueue *unp_taskqueue;

static struct unp_defdiscard_list unp_defdiscard_head;
static struct spinlock unp_defdiscard_spin;
static struct task unp_defdiscard_task;

static struct	sockaddr sun_noname = { sizeof(sun_noname), AF_LOCAL };

static int     unp_attach (struct socket *, struct pru_attach_info *);
static void    unp_detach (struct unpcb *);
static int     unp_bind (struct unpcb *,struct sockaddr *, struct thread *);
static int     unp_connect (struct socket *,struct sockaddr *,
				struct thread *);
static void    unp_disconnect(struct unpcb *, int);
static void    unp_shutdown (struct unpcb *);
static void    unp_gc(void *, int);
#ifdef UNP_GC_ALLFILES
static int     unp_gc_clearmarks(struct file *, void *);
static int     unp_gc_checkmarks(struct file *, void *);
static int     unp_gc_checkrefs(struct file *, void *);
static void    unp_mark(struct file *, void *data);
#endif
static void    unp_scan (struct mbuf *, void (*)(struct file *, void *),
				void *data);
static void    unp_discard (struct file *, void *);
static int     unp_internalize (struct mbuf *, struct thread *);
static int     unp_listen (struct unpcb *, struct thread *);
static void    unp_fp_externalize(struct lwp *lp, struct file *fp, int fd,
		   int flags);
static int     unp_find_lockref(struct sockaddr *nam, struct thread *td,
		   short type, struct unpcb **unp_ret);
static int     unp_connect_pair(struct unpcb *unp, struct unpcb *unp2);
static void    unp_drop(struct unpcb *unp, int error);
static void    unp_defdiscard_taskfunc(void *, int);

static int	unp_rights;			/* file descriptors in flight */
static struct lwkt_token unp_rights_token =
    LWKT_TOKEN_INITIALIZER(unp_rights_token);
static struct task unp_gc_task;
static struct unpcb *unp_gc_marker;

SYSCTL_DECL(_net_local);
SYSCTL_INT(_net_local, OID_AUTO, inflight, CTLFLAG_RD, &unp_rights, 0,
   "File descriptors in flight");

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

static __inline void
unp_reference(struct unpcb *unp)
{
	/* 0->1 transition will not work */
	KKASSERT(unp->unp_refcnt > 0);
	atomic_add_int(&unp->unp_refcnt, 1);
}

static __inline void
unp_free(struct unpcb *unp)
{
	KKASSERT(unp->unp_refcnt > 0);
	if (atomic_fetchadd_int(&unp->unp_refcnt, -1) == 1)
		unp_detach(unp);
}

static __inline struct unpcb *
unp_getsocktoken(struct socket *so)
{
	struct unpcb *unp;

	/*
	 * The unp pointer is invalid until we verify that it is
	 * good by re-checking so_pcb AFTER obtaining the token.
	 */
	while ((unp = so->so_pcb) != NULL) {
		lwkt_getpooltoken(unp);
		if (unp == so->so_pcb)
			break;
		lwkt_relpooltoken(unp);
	}
	return unp;
}

static __inline void
unp_reltoken(struct unpcb *unp)
{
	if (unp != NULL)
		lwkt_relpooltoken(unp);
}

static __inline void
unp_setflags(struct unpcb *unp, int flags)
{
	atomic_set_int(&unp->unp_flags, flags);
}

static __inline void
unp_clrflags(struct unpcb *unp, int flags)
{
	atomic_clear_int(&unp->unp_flags, flags);
}

static __inline struct unp_global_head *
unp_globalhead(short type)
{
	switch (type) {
	case SOCK_STREAM:
		return &unp_stream_head;
	case SOCK_DGRAM:
		return &unp_dgram_head;
	case SOCK_SEQPACKET:
		return &unp_seqpkt_head;
	default:
		panic("unknown socket type %d", type);
	}
}

static __inline struct unpcb *
unp_fp2unpcb(struct file *fp)
{
	struct socket *so;

	if (fp->f_type != DTYPE_SOCKET)
		return NULL;

	so = fp->f_data;
	if (so == NULL)
		return NULL;

	if (so->so_proto->pr_domain != &localdomain)
		return NULL;

	return so->so_pcb;
}

static __inline void
unp_add_right(struct file *fp)
{
	struct unpcb *unp;

	ASSERT_LWKT_TOKEN_HELD(&unp_rights_token);
	KASSERT(fp->f_count > 0, ("invalid f_count %d", fp->f_count));

	unp = unp_fp2unpcb(fp);
	if (unp != NULL) {
		unp->unp_fp = fp;
		unp->unp_msgcount++;
	}
	fp->f_msgcount++;
	unp_rights++;
}

static __inline void
unp_del_right(struct file *fp)
{
	struct unpcb *unp;

	ASSERT_LWKT_TOKEN_HELD(&unp_rights_token);
	KASSERT(fp->f_count > 0, ("invalid f_count %d", fp->f_count));

	unp = unp_fp2unpcb(fp);
	if (unp != NULL) {
		KASSERT(unp->unp_msgcount > 0,
		    ("invalid unp msgcount %d", unp->unp_msgcount));
		unp->unp_msgcount--;
		if (unp->unp_msgcount == 0)
			unp->unp_fp = NULL;
	}
	fp->f_msgcount--;
	unp_rights--;
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
	unp = unp_getsocktoken(msg->base.nm_so);

	if (UNP_ISATTACHED(unp)) {
		unp_drop(unp, ECONNABORTED);
		error = 0;
	} else {
		error = EINVAL;
	}

	unp_reltoken(unp);
	lwkt_reltoken(&unp_token);

	lwkt_replymsg(&msg->lmsg, error);
}

static void
uipc_accept(netmsg_t msg)
{
	struct unpcb *unp;
	int error;

	lwkt_gettoken(&unp_token);
	unp = unp_getsocktoken(msg->base.nm_so);

	if (!UNP_ISATTACHED(unp)) {
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

	unp_reltoken(unp);
	lwkt_reltoken(&unp_token);

	lwkt_replymsg(&msg->lmsg, error);
}

static void
uipc_attach(netmsg_t msg)
{
	int error;

	lwkt_gettoken(&unp_token);

	KASSERT(msg->base.nm_so->so_pcb == NULL, ("double unp attach"));
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
	unp = unp_getsocktoken(msg->base.nm_so);

	if (UNP_ISATTACHED(unp))
		error = unp_bind(unp, msg->bind.nm_nam, msg->bind.nm_td);
	else
		error = EINVAL;

	unp_reltoken(unp);
	lwkt_reltoken(&unp_token);

	lwkt_replymsg(&msg->lmsg, error);
}

static void
uipc_connect(netmsg_t msg)
{
	int error;

	error = unp_connect(msg->base.nm_so, msg->connect.nm_nam,
	    msg->connect.nm_td);
	lwkt_replymsg(&msg->lmsg, error);
}

static void
uipc_connect2(netmsg_t msg)
{
	int error;

	error = unp_connect2(msg->connect2.nm_so1, msg->connect2.nm_so2);
	lwkt_replymsg(&msg->lmsg, error);
}

/* control is EOPNOTSUPP */

static void
uipc_detach(netmsg_t msg)
{
	struct unpcb *unp;
	int error;

	lwkt_gettoken(&unp_token);
	unp = unp_getsocktoken(msg->base.nm_so);

	if (UNP_ISATTACHED(unp)) {
		unp_drop(unp, 0);
		error = 0;
	} else {
		error = EINVAL;
	}

	unp_reltoken(unp);
	lwkt_reltoken(&unp_token);

	lwkt_replymsg(&msg->lmsg, error);
}

static void
uipc_disconnect(netmsg_t msg)
{
	struct unpcb *unp;
	int error;

	lwkt_gettoken(&unp_token);
	unp = unp_getsocktoken(msg->base.nm_so);

	if (UNP_ISATTACHED(unp)) {
		unp_disconnect(unp, 0);
		error = 0;
	} else {
		error = EINVAL;
	}

	unp_reltoken(unp);
	lwkt_reltoken(&unp_token);

	lwkt_replymsg(&msg->lmsg, error);
}

static void
uipc_listen(netmsg_t msg)
{
	struct unpcb *unp;
	int error;

	lwkt_gettoken(&unp_token);
	unp = unp_getsocktoken(msg->base.nm_so);

	if (!UNP_ISATTACHED(unp) || unp->unp_vnode == NULL)
		error = EINVAL;
	else
		error = unp_listen(unp, msg->listen.nm_td);

	unp_reltoken(unp);
	lwkt_reltoken(&unp_token);

	lwkt_replymsg(&msg->lmsg, error);
}

static void
uipc_peeraddr(netmsg_t msg)
{
	struct unpcb *unp;
	int error;

	lwkt_gettoken(&unp_token);
	unp = unp_getsocktoken(msg->base.nm_so);

	if (!UNP_ISATTACHED(unp)) {
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

	unp_reltoken(unp);
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
	 * pool token held.
	 */
	so = msg->base.nm_so;
	unp = unp_getsocktoken(so);

	if (!UNP_ISATTACHED(unp)) {
		error = EINVAL;
		goto done;
	}

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
done:
	unp_reltoken(unp);
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
	 * pool token held.
	 */
	so = msg->base.nm_so;
	unp = unp_getsocktoken(so);

	if (!UNP_ISATTACHED(unp)) {
		error = EINVAL;
		goto release;
	}

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
			lwkt_gettoken(&unp_token);
			error = unp_find_lockref(msg->send.nm_addr,
			    msg->send.nm_td, so->so_type, &unp2);
			if (error) {
				lwkt_reltoken(&unp_token);
				break;
			}
			/*
			 * NOTE:
			 * unp2 is locked and referenced.
			 *
			 * We could unlock unp2 now, since it was checked
			 * and referenced.
			 */
			unp_reltoken(unp2);
			lwkt_reltoken(&unp_token);
		} else {
			if (unp->unp_conn == NULL) {
				error = ENOTCONN;
				break;
			}
			unp2 = unp->unp_conn;
			unp_reference(unp2);
		}
		/* NOTE: unp2 is referenced. */
		so2 = unp2->unp_socket;

		/*
		 * Include creds if the receive side wants them, even if
		 * the send side did not send them.
		 */
		if (so2->so_options & SO_PASSCRED) {
			struct mbuf **mp;
			struct cmsghdr *cm;
			struct cmsgcred cred;
			struct mbuf *ncon;

			mp = &control;
			while ((ncon = *mp) != NULL) {
				cm = mtod(ncon, struct cmsghdr *);
				if (cm->cmsg_type == SCM_CREDS &&
				    cm->cmsg_level == SOL_SOCKET)
					break;
				mp = &ncon->m_next;
			}
			if (ncon == NULL) {
				ncon = sbcreatecontrol((caddr_t)&cred,
						       sizeof(cred),
						       SCM_CREDS, SOL_SOCKET);
				unp_internalize(ncon, msg->send.nm_td);
				*mp = ncon;
			}
		}

		if (unp->unp_addr)
			from = (struct sockaddr *)unp->unp_addr;
		else
			from = &sun_noname;

		lwkt_gettoken(&so2->so_rcv.ssb_token);
		if (ssb_appendaddr(&so2->so_rcv, from, m, control)) {
			sorwakeup(so2);
			m = NULL;
			control = NULL;
		} else {
			error = ENOBUFS;
		}
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
		if (unp->unp_conn == NULL) {
			if (msg->send.nm_addr) {
				error = unp_connect(so,
						    msg->send.nm_addr,
						    msg->send.nm_td);
				if (error)
					break;	/* XXX */
			}
			/*
			 * NOTE:
			 * unp_conn still could be NULL, even if the
			 * above unp_connect() succeeds; since the
			 * current unp's token could be released due
			 * to blocking operations after unp_conn is
			 * assigned.
			 */
			if (unp->unp_conn == NULL) {
				error = ENOTCONN;
				break;
			}
		}
		if (so->so_state & SS_CANTSENDMORE) {
			error = EPIPE;
			break;
		}

		unp2 = unp->unp_conn;
		KASSERT(unp2 != NULL, ("unp is not connected"));
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
	unp_reltoken(unp);
	wakeup_end_delayed();

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
	 * pool token held.
	 */
	unp = unp_getsocktoken(so);

	if (!UNP_ISATTACHED(unp)) {
		error = EINVAL;
		goto done;
	}

	sb->st_blksize = so->so_snd.ssb_hiwat;
	sb->st_dev = NOUDEV;
	error = 0;
done:
	unp_reltoken(unp);
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
	 * pool token held.
	 */
	so = msg->base.nm_so;
	unp = unp_getsocktoken(so);

	if (UNP_ISATTACHED(unp)) {
		socantsendmore(so);
		unp_shutdown(unp);
		error = 0;
	} else {
		error = EINVAL;
	}

	unp_reltoken(unp);
	lwkt_replymsg(&msg->lmsg, error);
}

static void
uipc_sockaddr(netmsg_t msg)
{
	struct unpcb *unp;
	int error;

	/*
	 * so_pcb is only modified with both the global and the unp
	 * pool token held.
	 */
	unp = unp_getsocktoken(msg->base.nm_so);

	if (UNP_ISATTACHED(unp)) {
		if (unp->unp_addr) {
			*msg->sockaddr.nm_nam =
				dup_sockaddr((struct sockaddr *)unp->unp_addr);
		}
		error = 0;
	} else {
		error = EINVAL;
	}

	unp_reltoken(unp);
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

	so = msg->base.nm_so;
	sopt = msg->ctloutput.nm_sopt;

	lwkt_gettoken(&unp_token);
	unp = unp_getsocktoken(so);

	if (!UNP_ISATTACHED(unp)) {
		error = EINVAL;
		goto done;
	}

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

done:
	unp_reltoken(unp);
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
 *
 * We no longer need to worry about avoiding the windows scaling option.
 * Programs which use unix domain sockets expect larger defaults these days.
 */
#ifndef PIPSIZ
#define	PIPSIZ	65536
#endif
static u_long	unpst_sendspace = PIPSIZ;
static u_long	unpst_recvspace = PIPSIZ;
static u_long	unpdg_sendspace = PIPSIZ;	/* really max datagram size */
static u_long	unpdg_recvspace = PIPSIZ;
static u_long	unpsp_sendspace = PIPSIZ;	/* really max datagram size */
static u_long	unpsp_recvspace = PIPSIZ;

SYSCTL_DECL(_net_local_stream);
SYSCTL_DECL(_net_local_dgram);
SYSCTL_DECL(_net_local_seqpacket);

SYSCTL_ULONG(_net_local_stream, OID_AUTO, sendspace, CTLFLAG_RW,
    &unpst_sendspace, 0, "Size of stream socket send buffer");
SYSCTL_ULONG(_net_local_stream, OID_AUTO, recvspace, CTLFLAG_RW,
    &unpst_recvspace, 0, "Size of stream socket receive buffer");

SYSCTL_ULONG(_net_local_dgram, OID_AUTO, maxdgram, CTLFLAG_RW,
    &unpdg_sendspace, 0, "Max datagram socket size");
SYSCTL_ULONG(_net_local_dgram, OID_AUTO, recvspace, CTLFLAG_RW,
    &unpdg_recvspace, 0, "Size of datagram socket receive buffer");

SYSCTL_ULONG(_net_local_seqpacket, OID_AUTO, maxseqpacket, CTLFLAG_RW,
    &unpsp_sendspace, 0, "Default seqpacket send space.");
SYSCTL_ULONG(_net_local_seqpacket, OID_AUTO, recvspace, CTLFLAG_RW,
    &unpsp_recvspace, 0, "Default seqpacket receive space.");


static int
unp_attach(struct socket *so, struct pru_attach_info *ai)
{
	struct unp_global_head *head;
	struct unpcb *unp;
	int error;

	lwkt_gettoken(&unp_token);

	if (so->so_snd.ssb_hiwat == 0 || so->so_rcv.ssb_hiwat == 0) {
		switch (so->so_type) {
		case SOCK_STREAM:
			error = soreserve(so, unpst_sendspace, unpst_recvspace,
					  ai->sb_rlimit);
			break;
		case SOCK_DGRAM:
			error = soreserve(so, unpdg_sendspace, unpdg_recvspace,
					  ai->sb_rlimit);
			break;
		case SOCK_SEQPACKET:
			error = soreserve(so, unpsp_sendspace, unpsp_recvspace,
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
	LIST_INIT(&unp->unp_refs);
	unp->unp_socket = so;
	unp->unp_rvnode = ai->fd_rdir;		/* jail cruft XXX JH */
	so->so_pcb = (caddr_t)unp;
	soreference(so);

	head = unp_globalhead(so->so_type);
	TAILQ_INSERT_TAIL(&head->list, unp, unp_link);
	head->count++;
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

	so = unp->unp_socket;

	unp->unp_gencnt = ++unp_gencnt;
	if (unp->unp_vnode) {
		unp->unp_vnode->v_socket = NULL;
		vrele(unp->unp_vnode);
		unp->unp_vnode = NULL;
	}
	soisdisconnected(so);
	KKASSERT(so->so_pcb == unp);
	so->so_pcb = NULL;		/* both tokens required */
	unp->unp_socket = NULL;

	lwkt_relpooltoken(unp);
	lwkt_reltoken(&unp_token);

	sofree(so);

	KASSERT(unp->unp_conn == NULL, ("unp is still connected"));
	KASSERT(LIST_EMPTY(&unp->unp_refs), ("unp still has references"));

	if (unp->unp_addr)
		kfree(unp->unp_addr, M_SONAME);
	kfree(unp, M_UNPCB);

	if (unp_rights)
		taskqueue_enqueue(unp_taskqueue, &unp_gc_task);
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

	ASSERT_LWKT_TOKEN_HELD(&unp_token);
	UNP_ASSERT_TOKEN_HELD(unp);

	if (unp->unp_vnode != NULL)
		return EINVAL;

	namelen = soun->sun_len - offsetof(struct sockaddr_un, sun_path);
	if (namelen <= 0)
		return EINVAL;
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
	return (error);
}

static int
unp_connect(struct socket *so, struct sockaddr *nam, struct thread *td)
{
	struct unpcb *unp, *unp2;
	int error, flags = 0;

	lwkt_gettoken(&unp_token);

	unp = unp_getsocktoken(so);
	if (!UNP_ISATTACHED(unp)) {
		error = EINVAL;
		goto failed;
	}

	if ((unp->unp_flags & UNP_CONNECTING) || unp->unp_conn != NULL) {
		error = EISCONN;
		goto failed;
	}

	flags = UNP_CONNECTING;
	unp_setflags(unp, flags);

	error = unp_find_lockref(nam, td, so->so_type, &unp2);
	if (error)
		goto failed;
	/*
	 * NOTE:
	 * unp2 is locked and referenced.
	 */

	if (so->so_proto->pr_flags & PR_CONNREQUIRED) {
		struct socket *so2, *so3;
		struct unpcb *unp3;

		so2 = unp2->unp_socket;
		if (!(so2->so_options & SO_ACCEPTCONN) ||
		    /* listen is not completed yet */
		    !(unp2->unp_flags & UNP_HAVEPCCACHED) ||
		    (so3 = sonewconn_faddr(so2, 0, NULL,
		     TRUE /* keep ref */)) == NULL) {
			error = ECONNREFUSED;
			goto done;
		}
		/* so3 has a socket reference. */

		unp3 = unp_getsocktoken(so3);
		if (!UNP_ISATTACHED(unp3)) {
			unp_reltoken(unp3);
			/*
			 * Already aborted; we only need to drop the
			 * socket reference held by sonewconn_faddr().
			 */
			sofree(so3);
			error = ECONNREFUSED;
			goto done;
		}
		unp_reference(unp3);
		/*
		 * NOTE:
		 * unp3 is locked and referenced.
		 */

		/*
		 * Release so3 socket reference held by sonewconn_faddr().
		 * Since we have referenced unp3, neither unp3 nor so3 will
		 * be destroyed here.
		 */
		sofree(so3);

		if (unp2->unp_addr != NULL) {
			unp3->unp_addr = (struct sockaddr_un *)
			    dup_sockaddr((struct sockaddr *)unp2->unp_addr);
		}

		/*
		 * unp_peercred management:
		 *
		 * The connecter's (client's) credentials are copied
		 * from its process structure at the time of connect()
		 * (which is now).
		 */
		cru2x(td->td_proc->p_ucred, &unp3->unp_peercred);
		unp_setflags(unp3, UNP_HAVEPC);
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
		unp_setflags(unp, UNP_HAVEPC);

		error = unp_connect_pair(unp, unp3);
		if (error)
			soabort_direct(so3);

		/* Done with unp3 */
		unp_free(unp3);
		unp_reltoken(unp3);
	} else {
		error = unp_connect_pair(unp, unp2);
	}
done:
	unp_free(unp2);
	unp_reltoken(unp2);
failed:
	if (flags)
		unp_clrflags(unp, flags);
	unp_reltoken(unp);

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
	struct unpcb *unp, *unp2;
	int error;

	lwkt_gettoken(&unp_token);
	if (so2->so_type != so->so_type) {
		lwkt_reltoken(&unp_token);
		return (EPROTOTYPE);
	}
	unp = unp_getsocktoken(so);
	unp2 = unp_getsocktoken(so2);

	if (!UNP_ISATTACHED(unp)) {
		error = EINVAL;
		goto done;
	}
	if (!UNP_ISATTACHED(unp2)) {
		error = ECONNREFUSED;
		goto done;
	}

	if (unp->unp_conn != NULL) {
		error = EISCONN;
		goto done;
	}
	if ((so->so_type == SOCK_STREAM || so->so_type == SOCK_SEQPACKET) &&
	    unp2->unp_conn != NULL) {
		error = EISCONN;
		goto done;
	}

	error = unp_connect_pair(unp, unp2);
done:
	unp_reltoken(unp2);
	unp_reltoken(unp);
	lwkt_reltoken(&unp_token);
	return (error);
}

/*
 * Disconnect a unix domain socket pair.
 *
 * NOTE: Semantics for any change to unp_conn requires that the per-unp
 *	 pool token also be held.
 */
static void
unp_disconnect(struct unpcb *unp, int error)
{
	struct socket *so = unp->unp_socket;
	struct unpcb *unp2;

	ASSERT_LWKT_TOKEN_HELD(&unp_token);
	UNP_ASSERT_TOKEN_HELD(unp);

	if (error)
		so->so_error = error;

	while ((unp2 = unp->unp_conn) != NULL) {
		lwkt_getpooltoken(unp2);
		if (unp2 == unp->unp_conn)
			break;
		lwkt_relpooltoken(unp2);
	}
	if (unp2 == NULL)
		return;
	/* unp2 is locked. */

	KASSERT((unp2->unp_flags & UNP_DROPPED) == 0, ("unp2 was dropped"));

	unp->unp_conn = NULL;

	switch (so->so_type) {
	case SOCK_DGRAM:
		LIST_REMOVE(unp, unp_reflink);
		soclrstate(so, SS_ISCONNECTED);
		break;

	case SOCK_STREAM:
	case SOCK_SEQPACKET:
		/*
		 * Keep a reference before clearing the unp_conn
		 * to avoid racing uipc_detach()/uipc_abort() in
		 * other thread.
		 */
		unp_reference(unp2);
		KASSERT(unp2->unp_conn == unp, ("unp_conn mismatch"));
		unp2->unp_conn = NULL;

		soisdisconnected(so);
		soisdisconnected(unp2->unp_socket);

		unp_free(unp2);
		break;
	}

	lwkt_relpooltoken(unp2);
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
	struct unp_global_head *head = arg1;
	int error, i, n;
	struct unpcb *unp, *marker;

	KKASSERT(curproc != NULL);

	/*
	 * The process of preparing the PCB list is too time-consuming and
	 * resource-intensive to repeat twice on every request.
	 */
	if (req->oldptr == NULL) {
		n = head->count;
		req->oldidx = (n + n/8) * sizeof(struct xunpcb);
		return 0;
	}

	if (req->newptr != NULL)
		return EPERM;

	marker = kmalloc(sizeof(*marker), M_UNPCB, M_WAITOK | M_ZERO);
	marker->unp_flags |= UNP_MARKER;

	lwkt_gettoken(&unp_token);

	n = head->count;
	i = 0;
	error = 0;

	TAILQ_INSERT_HEAD(&head->list, marker, unp_link);
	while ((unp = TAILQ_NEXT(marker, unp_link)) != NULL && i < n) {
		struct xunpcb xu;

		TAILQ_REMOVE(&head->list, marker, unp_link);
		TAILQ_INSERT_AFTER(&head->list, unp, marker, unp_link);

		if (unp->unp_flags & UNP_MARKER)
			continue;
		if (prison_unpcb(req->td, unp))
			continue;

		xu.xu_len = sizeof(xu);
		xu.xu_unpp = unp;

		/*
		 * NOTE:
		 * unp->unp_addr and unp->unp_conn are protected by
		 * unp_token.  So if we want to get rid of unp_token
		 * or reduce the coverage of unp_token, care must be
		 * taken.
		 */
		if (unp->unp_addr) {
			bcopy(unp->unp_addr, &xu.xu_addr, 
			      unp->unp_addr->sun_len);
		}
		if (unp->unp_conn && unp->unp_conn->unp_addr) {
			bcopy(unp->unp_conn->unp_addr,
			      &xu.xu_caddr,
			      unp->unp_conn->unp_addr->sun_len);
		}
		bcopy(unp, &xu.xu_unp, sizeof(*unp));
		sotoxsocket(unp->unp_socket, &xu.xu_socket);

		/* NOTE: This could block and temporarily release unp_token */
		error = SYSCTL_OUT(req, &xu, sizeof(xu));
		if (error)
			break;
		++i;
	}
	TAILQ_REMOVE(&head->list, marker, unp_link);

	lwkt_reltoken(&unp_token);

	kfree(marker, M_UNPCB);
	return error;
}

SYSCTL_PROC(_net_local_dgram, OID_AUTO, pcblist, CTLFLAG_RD, 
	    &unp_dgram_head, 0, unp_pcblist, "S,xunpcb",
	    "List of active local datagram sockets");
SYSCTL_PROC(_net_local_stream, OID_AUTO, pcblist, CTLFLAG_RD, 
	    &unp_stream_head, 0, unp_pcblist, "S,xunpcb",
	    "List of active local stream sockets");
SYSCTL_PROC(_net_local_seqpacket, OID_AUTO, pcblist, CTLFLAG_RD, 
	    &unp_seqpkt_head, 0, unp_pcblist, "S,xunpcb",
	    "List of active local seqpacket sockets");

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

#ifdef notdef
void
unp_drain(void)
{
	lwkt_gettoken(&unp_token);
	lwkt_reltoken(&unp_token);
}
#endif

int
unp_externalize(struct mbuf *rights, int flags)
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
		/ sizeof(struct file *);
	int f;

	lwkt_gettoken(&unp_rights_token);

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
		lwkt_reltoken(&unp_rights_token);
		return (EMSGSIZE);
	}

	/*
	 * now change each pointer to an fd in the global table to 
	 * an integer that is the index to the local fd table entry
	 * that we set up to point to the global one we are transferring.
	 * Since the sizeof(struct file *) is bigger than or equal to
	 * the sizeof(int), we do it in forward order.  In that case,
	 * an integer will always come in the same place or before its
	 * corresponding struct file pointer.
	 *
	 * Hold revoke_token in 'shared' mode, so that we won't miss
	 * the FREVOKED update on fps being externalized (fsetfd).
	 */
	lwkt_gettoken_shared(&revoke_token);
	fdp = (int *)CMSG_DATA(cm);
	rp = (struct file **)CMSG_DATA(cm);
	for (i = 0; i < newfds; i++) {
		if (fdalloc(p, 0, &f)) {
			int j;

			/*
			 * Previous fdavail() can't garantee
			 * fdalloc() success due to SMP race.
			 * Just clean up and return the same
			 * error value as if fdavail() failed.
			 */
			lwkt_reltoken(&revoke_token);

			/* Close externalized files */
			for (j = 0; j < i; j++)
				kern_close(fdp[j]);
			/* Discard the rest of internal files */
			for (; i < newfds; i++)
				unp_discard(rp[i], NULL);
			/* Wipe out the control message */
			for (i = 0; i < newfds; i++)
				rp[i] = NULL;

			lwkt_reltoken(&unp_rights_token);
			return (EMSGSIZE);
		}
		fp = rp[i];
		unp_fp_externalize(lp, fp, f, flags);
		fdp[i] = f;
	}
	lwkt_reltoken(&revoke_token);

	lwkt_reltoken(&unp_rights_token);

	/*
	 * Adjust length, in case sizeof(struct file *) and sizeof(int)
	 * differs.
	 */
	cm->cmsg_len = CMSG_LEN(newfds * sizeof(int));
	rights->m_len = cm->cmsg_len;

	return (0);
}

static void
unp_fp_externalize(struct lwp *lp, struct file *fp, int fd, int flags)
{
	if (lp) {
		struct filedesc *fdp = lp->lwp_proc->p_fd;

		KKASSERT(fd >= 0);
		if (fp->f_flag & FREVOKED) {
			struct file *fx;
			int error;

			kprintf("Warning: revoked fp exiting unix socket\n");
			error = falloc(lp, &fx, NULL);
			if (error == 0) {
				if (flags & MSG_CMSG_CLOEXEC)
					fdp->fd_files[fd].fileflags |= UF_EXCLOSE;
				fsetfd(fdp, fx, fd);
				fdrop(fx);
			} else {
				fsetfd(fdp, NULL, fd);
			}
		} else {
			if (flags & MSG_CMSG_CLOEXEC)
				fdp->fd_files[fd].fileflags |= UF_EXCLOSE;
			fsetfd(fdp, fp, fd);
		}
	}
	unp_del_right(fp);
	fdrop(fp);
}

void
unp_init(void)
{
	TAILQ_INIT(&unp_stream_head.list);
	TAILQ_INIT(&unp_dgram_head.list);
	TAILQ_INIT(&unp_seqpkt_head.list);

	SLIST_INIT(&unp_defdiscard_head);
	spin_init(&unp_defdiscard_spin, "unpdisc");
	TASK_INIT(&unp_defdiscard_task, 0, unp_defdiscard_taskfunc, NULL);

	/*
	 * This implies that only one gc can be in-progress at any
	 * given moment.
	 */
	TASK_INIT(&unp_gc_task, 0, unp_gc, NULL);

	unp_gc_marker = kmalloc(sizeof(*unp_gc_marker), M_UNPCB,
	    M_WAITOK | M_ZERO);
	unp_gc_marker->unp_flags |= UNP_MARKER;

	/*
	 * Create taskqueue for defered discard, and stick it to
	 * the last CPU.
	 */
	unp_taskqueue = taskqueue_create("unp_taskq", M_WAITOK,
	    taskqueue_thread_enqueue, &unp_taskqueue);
	taskqueue_start_threads(&unp_taskqueue, 1, TDPRI_KERN_DAEMON,
	    ncpus - 1, "unp taskq");
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

	/*
	 * Make sure the message is reasonable, and either CREDS or RIGHTS.
	 *
	 * NOTE: overall message length does not have to be aligned, but the
	 *	 data start does.
	 */
	if ((cm->cmsg_type != SCM_RIGHTS && cm->cmsg_type != SCM_CREDS) ||
	    cm->cmsg_level != SOL_SOCKET ||
	    control->m_len < sizeof(*cm) ||	/* control too small */
	    cm->cmsg_len < sizeof(*cm) ||	/* cmsg_len too small */
	    cm->cmsg_len > control->m_len) {	/* cmsg_len too big */
		return EINVAL;
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
		return 0;
	}

	/*
	 * cmsghdr may not be aligned, do not allow calculation(s) to
	 * go negative.
	 *
	 * Data must be aligned but the data length does not have to be.
	 *
	 * If there are multiple headers (XXX not supported) then the
	 * next header will be aligned after the end of the possibly
	 * unaligned data.
	 */
	if (cm->cmsg_len < CMSG_LEN(0)) {
		return EINVAL;
	}

	oldfds = (cm->cmsg_len - CMSG_LEN(0)) / sizeof(int);

	/*
	 * Now replace the integer FDs with pointers to
	 * the associated global file table entry..
	 * Allocate a bigger buffer as necessary. But if an cluster is not
	 * enough, return E2BIG.
	 */
	newlen = CMSG_LEN(oldfds * sizeof(struct file *));
	if (newlen > MCLBYTES)
		return E2BIG;
	if (newlen - control->m_len > M_TRAILINGSPACE(control)) {
		if (control->m_flags & M_EXT)
			return E2BIG;
		MCLGET(control, M_WAITOK);
		if (!(control->m_flags & M_EXT))
			return ENOBUFS;

		/* copy the data to the cluster */
		memcpy(mtod(control, char *), cm, cm->cmsg_len);
		cm = mtod(control, struct cmsghdr *);
	}

	lwkt_gettoken(&unp_rights_token);

	fdescp = p->p_fd;
	spin_lock_shared(&fdescp->fd_spin);

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
	 * Adjust length, in case sizeof(struct file *) and sizeof(int)
	 * differs.
	 */
	cm->cmsg_len = newlen;
	control->m_len = CMSG_ALIGN(newlen);

	/*
	 * Transform the file descriptors into struct file pointers.
	 * Since the sizeof(struct file *) is bigger than or equal to
	 * the sizeof(int), we do it in reverse order so that the int
	 * won't get trashed until we're done.
	 */
	fdp = (int *)CMSG_DATA(cm) + oldfds - 1;
	rp = (struct file **)CMSG_DATA(cm) + oldfds - 1;
	for (i = 0; i < oldfds; i++) {
		fp = fdescp->fd_files[*fdp--].fp;
		*rp-- = fp;
		fhold(fp);
		unp_add_right(fp);
	}
	error = 0;
done:
	spin_unlock_shared(&fdescp->fd_spin);
	lwkt_reltoken(&unp_rights_token);
	return error;
}

#ifdef UNP_GC_ALLFILES

/*
 * Garbage collect in-transit file descriptors that get lost due to
 * loops (i.e. when a socket is sent to another process over itself,
 * and more complex situations).
 *
 * NOT MPSAFE - TODO socket flush code and maybe fdrop.  Rest is MPSAFE.
 */

struct unp_gc_info {
	struct file **extra_ref;
	struct file *locked_fp;
	int defer;
	int index;
	int maxindex;
};

static void
unp_gc(void *arg __unused, int pending __unused)
{
	struct unp_gc_info info;
	struct file **fpp;
	int i;

	lwkt_gettoken(&unp_rights_token);

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
	 * is non-zero.  If during the sweep phase the gc code unp_discards,
	 * we end up doing a (full) fdrop on the descriptor.  A fdrop on A
	 * results in the following chain.  Closef calls soo_close, which
	 * calls soclose.   Soclose calls first (through the switch
	 * uipc_usrreq) unp_detach, which re-invokes unp_gc.  Unp_gc simply
	 * returns because the previous instance had set unp_gcing, and
	 * we return all the way back to soclose, which marks the socket
	 * with SS_NOFDREF, and then calls sofree.  Sofree calls sorflush
	 * to free up the rights that are queued in messages on the socket A,
	 * i.e., the reference on B.  The sorflush calls via the dom_dispose
	 * switch unp_dispose, which unp_scans with unp_discard.  This second
	 * instance of unp_discard just calls fdrop on B.
	 *
	 * Well, a similar chain occurs on B, resulting in a sorflush on B,
	 * which results in another fdrop on A.  Unfortunately, A is already
	 * being closed, and the descriptor has already been marked with
	 * SS_NOFDREF, and soclose panics at this point.
	 *
	 * Here, we first take an extra reference to each inaccessible
	 * descriptor.  Then, we call sorflush ourself, since we know
	 * it is a Unix domain socket anyhow.  After we destroy all the
	 * rights carried in messages, we do a last fdrop to get rid
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
			fdrop(*fpp);
	} while (info.index == info.maxindex);

	kfree((caddr_t)info.extra_ref, M_FILE);

	lwkt_reltoken(&unp_rights_token);
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

#else	/* !UNP_GC_ALLFILES */

/*
 * They are thread local and do not require explicit synchronization.
 */
static int	unp_marked;
static int	unp_unreachable;

static void
unp_accessable(struct file *fp, void *data __unused)
{
	struct unpcb *unp;

	if ((unp = unp_fp2unpcb(fp)) == NULL)
		return;
	if (unp->unp_gcflags & UNPGC_REF)
		return;
	unp->unp_gcflags &= ~UNPGC_DEAD;
	unp->unp_gcflags |= UNPGC_REF;
	unp_marked++;
}

static void
unp_gc_process(struct unpcb *unp)
{
	struct file *fp;

	/* Already processed. */
	if (unp->unp_gcflags & UNPGC_SCANNED)
		return;
	fp = unp->unp_fp;

	/*
	 * Check for a socket potentially in a cycle.  It must be in a
	 * queue as indicated by msgcount, and this must equal the file
	 * reference count.  Note that when msgcount is 0 the file is NULL.
	 */
	if ((unp->unp_gcflags & UNPGC_REF) == 0 && fp &&
	    unp->unp_msgcount != 0 && fp->f_count == unp->unp_msgcount) {
		unp->unp_gcflags |= UNPGC_DEAD;
		unp_unreachable++;
		return;
	}

	/*
	 * Mark all sockets we reference with RIGHTS.
	 */
	if (UNP_ISATTACHED(unp)) {
		struct signalsockbuf *ssb = &unp->unp_socket->so_rcv;

		unp_reference(unp);
		lwkt_gettoken(&ssb->ssb_token);
		/*
		 * unp_token would be temporarily dropped, if getting
		 * so_rcv token blocks, so we need to check unp state
		 * here again.
		 */
		if (UNP_ISATTACHED(unp))
			unp_scan(ssb->ssb_mb, unp_accessable, NULL);
		lwkt_reltoken(&ssb->ssb_token);
		unp->unp_gcflags |= UNPGC_SCANNED;
		unp_free(unp);
	} else {
		unp->unp_gcflags |= UNPGC_SCANNED;
	}
}

static void
unp_gc(void *arg __unused, int pending __unused)
{
	struct unp_global_head *head;
	int h, filemax, fileidx, filetot;
	struct file **unref;
	struct unpcb *unp;

	lwkt_gettoken(&unp_rights_token);
	lwkt_gettoken(&unp_token);

	/*
	 * First clear all gc flags from previous runs.
	 */
	for (h = 0; unp_heads[h] != NULL; ++h) {
		/* 
		 * NOTE: This loop does not block, so it is safe
		 * to use TAILQ_FOREACH here.
		 */
		head = unp_heads[h];
		TAILQ_FOREACH(unp, &head->list, unp_link)
			unp->unp_gcflags = 0;
	}

	/*
	 * Scan marking all reachable sockets with UNPGC_REF.  Once a socket
	 * is reachable all of the sockets it references are reachable.
	 * Stop the scan once we do a complete loop without discovering
	 * a new reachable socket.
	 */
	do {
		unp_unreachable = 0;
		unp_marked = 0;
		for (h = 0; unp_heads[h] != NULL; ++h) {
			head = unp_heads[h];
			TAILQ_INSERT_HEAD(&head->list, unp_gc_marker, unp_link);
			while ((unp = TAILQ_NEXT(unp_gc_marker, unp_link))
			    != NULL) {
				TAILQ_REMOVE(&head->list, unp_gc_marker,
				    unp_link);
				TAILQ_INSERT_AFTER(&head->list, unp,
				    unp_gc_marker, unp_link);

				if (unp->unp_flags & UNP_MARKER)
					continue;
				unp_gc_process(unp);
			}
			TAILQ_REMOVE(&head->list, unp_gc_marker, unp_link);
		}
	} while (unp_marked);

	if (unp_unreachable == 0)
		goto done;

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
	 * is non-zero.  If during the sweep phase the gc code unp_discards,
	 * we end up doing a (full) fdrop on the descriptor.  A fdrop on A
	 * results in the following chain.  Closef calls soo_close, which
	 * calls soclose.   Soclose calls first (through the switch
	 * uipc_usrreq) unp_detach, which re-invokes unp_gc.  Unp_gc simply
	 * returns because the previous instance had set unp_gcing, and
	 * we return all the way back to soclose, which marks the socket
	 * with SS_NOFDREF, and then calls sofree.  Sofree calls sorflush
	 * to free up the rights that are queued in messages on the socket A,
	 * i.e., the reference on B.  The sorflush calls via the dom_dispose
	 * switch unp_dispose, which unp_scans with unp_discard.  This second
	 * instance of unp_discard just calls fdrop on B.
	 *
	 * Well, a similar chain occurs on B, resulting in a sorflush on B,
	 * which results in another fdrop on A.  Unfortunately, A is already
	 * being closed, and the descriptor has already been marked with
	 * SS_NOFDREF, and soclose panics at this point.
	 *
	 * Here, we first take an extra reference to each inaccessible
	 * descriptor.  Then, we call sorflush ourself, since we know
	 * it is a Unix domain socket anyhow.  After we destroy all the
	 * rights carried in messages, we do a last fdrop to get rid
	 * of our extra reference.  This is the last close, and the
	 * unp_detach etc will shut down the socket.
	 *
	 * 91/09/19, bsy@cs.cmu.edu
	 */

	filemax = unp_unreachable;
	if (filemax > UNP_GCFILE_MAX)
		filemax = UNP_GCFILE_MAX;
	unref = kmalloc(filemax * sizeof(struct file *), M_TEMP, M_WAITOK);

	filetot = 0;
	do {
		int i;

		/*
		 * Iterate looking for sockets which have been specifically
		 * marked as as unreachable and store them locally.
		 */
		fileidx = 0;
		for (h = 0; unp_heads[h] != NULL; ++h) {
			/*
			 * NOTE: This loop does not block, so it is safe
			 * to use TAILQ_FOREACH here.
			 */
			head = unp_heads[h];
			TAILQ_FOREACH(unp, &head->list, unp_link) {
				struct file *fp;

				if ((unp->unp_gcflags & UNPGC_DEAD) == 0)
					continue;
				unp->unp_gcflags &= ~UNPGC_DEAD;

				fp = unp->unp_fp;
				if (unp->unp_msgcount == 0 || fp == NULL ||
				    fp->f_count != unp->unp_msgcount)
					continue;
				fhold(fp);

				KASSERT(fileidx < filemax,
				    ("invalid fileidx %d, filemax %d",
				     fileidx, filemax));
				unref[fileidx++] = fp;

				KASSERT(filetot < unp_unreachable,
				    ("invalid filetot %d and "
				     "unp_unreachable %d",
				     filetot, unp_unreachable));
				++filetot;

				if (fileidx == filemax ||
				    filetot == unp_unreachable)
					goto dogc;
			}
		}
dogc:
		/*
		 * For each Unix domain socket on our hit list, do the
		 * following two things.
		 */
		for (i = 0; i < fileidx; ++i)
			sorflush(unref[i]->f_data);
		for (i = 0; i < fileidx; ++i)
			fdrop(unref[i]);
	} while (fileidx == filemax && filetot < unp_unreachable);
	kfree(unref, M_TEMP);
done:
	lwkt_reltoken(&unp_token);
	lwkt_reltoken(&unp_rights_token);
}

#endif	/* UNP_GC_ALLFILES */

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
	lwkt_gettoken(&unp_rights_token);
	if (m)
		unp_scan(m, unp_discard, NULL);
	lwkt_reltoken(&unp_rights_token);
}

static int
unp_listen(struct unpcb *unp, struct thread *td)
{
	struct proc *p = td->td_proc;

	ASSERT_LWKT_TOKEN_HELD(&unp_token);
	UNP_ASSERT_TOKEN_HELD(unp);

	KKASSERT(p);
	cru2x(p->p_ucred, &unp->unp_peercred);
	unp_setflags(unp, UNP_HAVEPCCACHED);
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
 * Discard a fp previously held in a unix domain socket mbuf.  To
 * avoid blowing out the kernel stack due to contrived chain-reactions
 * we may have to defer the operation to a dedicated taskqueue.
 *
 * Caller holds unp_rights_token.
 */
static void
unp_discard(struct file *fp, void *data __unused)
{
	unp_del_right(fp);
	if (unp_fp2unpcb(fp) != NULL) {
		struct unp_defdiscard *d;

		/*
		 * This fp is a Unix domain socket itself and fdrop()
		 * it here directly may cause deep unp_discard()
		 * recursion, so the fdrop() is defered to the
		 * dedicated taskqueue.
		 */
		d = kmalloc(sizeof(*d), M_UNPCB, M_WAITOK);
		d->fp = fp;

		spin_lock(&unp_defdiscard_spin);
		SLIST_INSERT_HEAD(&unp_defdiscard_head, d, next);
		spin_unlock(&unp_defdiscard_spin);

		taskqueue_enqueue(unp_taskqueue, &unp_defdiscard_task);
	} else {
		/* This fp is not a Unix domain socket */
		fdrop(fp);
	}
}

/*
 * NOTE:
 * unp_token must be held before calling this function to avoid name
 * resolution and v_socket accessing races, especially racing against
 * the unp_detach().
 *
 * NOTE:
 * For anyone caring about unconnected Unix domain socket sending
 * performance, other approach could be taken...
 */
static int
unp_find_lockref(struct sockaddr *nam, struct thread *td, short type,
    struct unpcb **unp_ret)
{
	struct proc *p = td->td_proc;
	struct sockaddr_un *soun = (struct sockaddr_un *)nam;
	struct vnode *vp = NULL;
	struct socket *so;
	struct unpcb *unp;
	int error, len;
	struct nlookupdata nd;
	char buf[SOCK_MAXADDRLEN];

	ASSERT_LWKT_TOKEN_HELD(&unp_token);

	*unp_ret = NULL;

	len = nam->sa_len - offsetof(struct sockaddr_un, sun_path);
	if (len <= 0) {
		error = EINVAL;
		goto failed;
	}
	strncpy(buf, soun->sun_path, len);
	buf[len] = 0;

	error = nlookup_init(&nd, buf, UIO_SYSSPACE, NLC_FOLLOW);
	if (error == 0)
		error = nlookup(&nd);
	if (error == 0)
		error = cache_vget(&nd.nl_nch, nd.nl_cred, LK_EXCLUSIVE, &vp);
	nlookup_done(&nd);
	if (error) {
		vp = NULL;
		goto failed;
	}

	if (vp->v_type != VSOCK) {
		error = ENOTSOCK;
		goto failed;
	}
	error = VOP_EACCESS(vp, VWRITE, p->p_ucred);
	if (error)
		goto failed;
	so = vp->v_socket;
	if (so == NULL) {
		error = ECONNREFUSED;
		goto failed;
	}
	if (so->so_type != type) {
		error = EPROTOTYPE;
		goto failed;
	}

	/* Lock this unp. */
	unp = unp_getsocktoken(so);
	if (!UNP_ISATTACHED(unp)) {
		unp_reltoken(unp);
		error = ECONNREFUSED;
		goto failed;
	}
	/* And keep this unp referenced. */
	unp_reference(unp);

	/* Done! */
	*unp_ret = unp;
	error = 0;
failed:
	if (vp != NULL)
		vput(vp);
	return error;
}

static int
unp_connect_pair(struct unpcb *unp, struct unpcb *unp2)
{
	struct socket *so = unp->unp_socket;
	struct socket *so2 = unp2->unp_socket;

	ASSERT_LWKT_TOKEN_HELD(&unp_token);
	UNP_ASSERT_TOKEN_HELD(unp);
	UNP_ASSERT_TOKEN_HELD(unp2);

	KASSERT(so->so_type == so2->so_type,
	    ("socket type mismatch, so %d, so2 %d", so->so_type, so2->so_type));

	if (!UNP_ISATTACHED(unp))
		return EINVAL;
	if (!UNP_ISATTACHED(unp2))
		return ECONNREFUSED;

	KASSERT(unp->unp_conn == NULL, ("unp is already connected"));
	unp->unp_conn = unp2;

	switch (so->so_type) {
	case SOCK_DGRAM:
		LIST_INSERT_HEAD(&unp2->unp_refs, unp, unp_reflink);
		soisconnected(so);
		break;

	case SOCK_STREAM:
	case SOCK_SEQPACKET:
		KASSERT(unp2->unp_conn == NULL, ("unp2 is already connected"));
		unp2->unp_conn = unp;
		soisconnected(so);
		soisconnected(so2);
		break;

	default:
		panic("unp_connect_pair: unknown socket type %d", so->so_type);
	}
	return 0;
}

static void
unp_drop(struct unpcb *unp, int error)
{
	struct unp_global_head *head;
	struct unpcb *unp2;

	ASSERT_LWKT_TOKEN_HELD(&unp_token);
	UNP_ASSERT_TOKEN_HELD(unp);

	KASSERT((unp->unp_flags & (UNP_DETACHED | UNP_DROPPED)) == 0,
	    ("unp is dropped"));

	/* Mark this unp as detached. */
	unp_setflags(unp, UNP_DETACHED);

	/* Remove this unp from the global unp list. */
	head = unp_globalhead(unp->unp_socket->so_type);
	KASSERT(head->count > 0, ("invalid unp count"));
	TAILQ_REMOVE(&head->list, unp, unp_link);
	head->count--;

	/* Disconnect all. */
	unp_disconnect(unp, error);
	while ((unp2 = LIST_FIRST(&unp->unp_refs)) != NULL) {
		lwkt_getpooltoken(unp2);
		unp_disconnect(unp2, ECONNRESET);
		lwkt_relpooltoken(unp2);
	}
	unp_setflags(unp, UNP_DROPPED);

	/* Try freeing this unp. */
	unp_free(unp);
}

static void
unp_defdiscard_taskfunc(void *arg __unused, int pending __unused)
{
	struct unp_defdiscard *d;

	spin_lock(&unp_defdiscard_spin);
	while ((d = SLIST_FIRST(&unp_defdiscard_head)) != NULL) {
		SLIST_REMOVE_HEAD(&unp_defdiscard_head, next);
		spin_unlock(&unp_defdiscard_spin);

		fdrop(d->fp);
		kfree(d, M_UNPCB);

		spin_lock(&unp_defdiscard_spin);
	}
	spin_unlock(&unp_defdiscard_spin);
}
