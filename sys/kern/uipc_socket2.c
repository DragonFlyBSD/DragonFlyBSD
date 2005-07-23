/*
 * Copyright (c) 2005 Jeffrey M. Hsu.  All rights reserved.
 * Copyright (c) 1982, 1986, 1988, 1990, 1993
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
 *	@(#)uipc_socket2.c	8.1 (Berkeley) 6/10/93
 * $FreeBSD: src/sys/kern/uipc_socket2.c,v 1.55.2.17 2002/08/31 19:04:55 dwmalone Exp $
 * $DragonFly: src/sys/kern/uipc_socket2.c,v 1.22 2005/07/23 07:28:34 dillon Exp $
 */

#include "opt_param.h"
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/domain.h>
#include <sys/file.h>	/* for maxfiles */
#include <sys/kernel.h>
#include <sys/proc.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/protosw.h>
#include <sys/resourcevar.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/signalvar.h>
#include <sys/sysctl.h>
#include <sys/aio.h> /* for aio_swake proto */
#include <sys/event.h>

#include <sys/thread2.h>
#include <sys/msgport2.h>

int	maxsockets;

/*
 * Primitive routines for operating on sockets and socket buffers
 */

u_long	sb_max = SB_MAX;
u_long	sb_max_adj =
    SB_MAX * MCLBYTES / (MSIZE + MCLBYTES); /* adjusted sb_max */

static	u_long sb_efficiency = 8;	/* parameter for sbreserve() */

/*
 * Procedures to manipulate state flags of socket
 * and do appropriate wakeups.  Normal sequence from the
 * active (originating) side is that soisconnecting() is
 * called during processing of connect() call,
 * resulting in an eventual call to soisconnected() if/when the
 * connection is established.  When the connection is torn down
 * soisdisconnecting() is called during processing of disconnect() call,
 * and soisdisconnected() is called when the connection to the peer
 * is totally severed.  The semantics of these routines are such that
 * connectionless protocols can call soisconnected() and soisdisconnected()
 * only, bypassing the in-progress calls when setting up a ``connection''
 * takes no time.
 *
 * From the passive side, a socket is created with
 * two queues of sockets: so_incomp for connections in progress
 * and so_comp for connections already made and awaiting user acceptance.
 * As a protocol is preparing incoming connections, it creates a socket
 * structure queued on so_incomp by calling sonewconn().  When the connection
 * is established, soisconnected() is called, and transfers the
 * socket structure to so_comp, making it available to accept().
 *
 * If a socket is closed with sockets on either
 * so_incomp or so_comp, these sockets are dropped.
 *
 * If higher level protocols are implemented in
 * the kernel, the wakeups done here will sometimes
 * cause software-interrupt process scheduling.
 */

void
soisconnecting(so)
	struct socket *so;
{

	so->so_state &= ~(SS_ISCONNECTED|SS_ISDISCONNECTING);
	so->so_state |= SS_ISCONNECTING;
}

void
soisconnected(so)
	struct socket *so;
{
	struct socket *head = so->so_head;

	so->so_state &= ~(SS_ISCONNECTING|SS_ISDISCONNECTING|SS_ISCONFIRMING);
	so->so_state |= SS_ISCONNECTED;
	if (head && (so->so_state & SS_INCOMP)) {
		if ((so->so_options & SO_ACCEPTFILTER) != 0) {
			so->so_upcall = head->so_accf->so_accept_filter->accf_callback;
			so->so_upcallarg = head->so_accf->so_accept_filter_arg;
			so->so_rcv.sb_flags |= SB_UPCALL;
			so->so_options &= ~SO_ACCEPTFILTER;
			so->so_upcall(so, so->so_upcallarg, 0);
			return;
		}
		TAILQ_REMOVE(&head->so_incomp, so, so_list);
		head->so_incqlen--;
		so->so_state &= ~SS_INCOMP;
		TAILQ_INSERT_TAIL(&head->so_comp, so, so_list);
		head->so_qlen++;
		so->so_state |= SS_COMP;
		sorwakeup(head);
		wakeup_one(&head->so_timeo);
	} else {
		wakeup(&so->so_timeo);
		sorwakeup(so);
		sowwakeup(so);
	}
}

void
soisdisconnecting(so)
	struct socket *so;
{

	so->so_state &= ~SS_ISCONNECTING;
	so->so_state |= (SS_ISDISCONNECTING|SS_CANTRCVMORE|SS_CANTSENDMORE);
	wakeup((caddr_t)&so->so_timeo);
	sowwakeup(so);
	sorwakeup(so);
}

void
soisdisconnected(so)
	struct socket *so;
{

	so->so_state &= ~(SS_ISCONNECTING|SS_ISCONNECTED|SS_ISDISCONNECTING);
	so->so_state |= (SS_CANTRCVMORE|SS_CANTSENDMORE|SS_ISDISCONNECTED);
	wakeup((caddr_t)&so->so_timeo);
	sbdrop(&so->so_snd, so->so_snd.sb_cc);
	sowwakeup(so);
	sorwakeup(so);
}

/*
 * When an attempt at a new connection is noted on a socket
 * which accepts connections, sonewconn is called.  If the
 * connection is possible (subject to space constraints, etc.)
 * then we allocate a new structure, propoerly linked into the
 * data structure of the original socket, and return this.
 * Connstatus may be 0, or SO_ISCONFIRMING, or SO_ISCONNECTED.
 */
struct socket *
sonewconn(struct socket *head, int connstatus)
{
	struct socket *so;
	struct pru_attach_info ai;

	if (head->so_qlen > 3 * head->so_qlimit / 2)
		return ((struct socket *)0);
	so = soalloc(0);
	if (so == NULL)
		return ((struct socket *)0);
	if ((head->so_options & SO_ACCEPTFILTER) != 0)
		connstatus = 0;
	so->so_head = head;
	so->so_type = head->so_type;
	so->so_options = head->so_options &~ SO_ACCEPTCONN;
	so->so_linger = head->so_linger;
	so->so_state = head->so_state | SS_NOFDREF;
	so->so_proto = head->so_proto;
	so->so_timeo = head->so_timeo;
	so->so_cred = crhold(head->so_cred);
	ai.sb_rlimit = NULL;
	ai.p_ucred = NULL;
	ai.fd_rdir = NULL;		/* jail code cruft XXX JH */
	if (soreserve(so, head->so_snd.sb_hiwat, head->so_rcv.sb_hiwat, NULL) ||
	    /* Directly call function since we're already at protocol level. */
	    (*so->so_proto->pr_usrreqs->pru_attach)(so, 0, &ai)) {
		sodealloc(so);
		return ((struct socket *)0);
	}

	if (connstatus) {
		TAILQ_INSERT_TAIL(&head->so_comp, so, so_list);
		so->so_state |= SS_COMP;
		head->so_qlen++;
	} else {
		if (head->so_incqlen > head->so_qlimit) {
			struct socket *sp;
			sp = TAILQ_FIRST(&head->so_incomp);
			(void) soabort(sp);
		}
		TAILQ_INSERT_TAIL(&head->so_incomp, so, so_list);
		so->so_state |= SS_INCOMP;
		head->so_incqlen++;
	}
	if (connstatus) {
		sorwakeup(head);
		wakeup((caddr_t)&head->so_timeo);
		so->so_state |= connstatus;
	}
	return (so);
}

/*
 * Socantsendmore indicates that no more data will be sent on the
 * socket; it would normally be applied to a socket when the user
 * informs the system that no more data is to be sent, by the protocol
 * code (in case PRU_SHUTDOWN).  Socantrcvmore indicates that no more data
 * will be received, and will normally be applied to the socket by a
 * protocol when it detects that the peer will send no more data.
 * Data queued for reading in the socket may yet be read.
 */

void
socantsendmore(so)
	struct socket *so;
{

	so->so_state |= SS_CANTSENDMORE;
	sowwakeup(so);
}

void
socantrcvmore(so)
	struct socket *so;
{

	so->so_state |= SS_CANTRCVMORE;
	sorwakeup(so);
}

/*
 * Wait for data to arrive at/drain from a socket buffer.
 */
int
sbwait(sb)
	struct sockbuf *sb;
{

	sb->sb_flags |= SB_WAIT;
	return (tsleep((caddr_t)&sb->sb_cc,
			((sb->sb_flags & SB_NOINTR) ? 0 : PCATCH),
			"sbwait",
			sb->sb_timeo));
}

/*
 * Lock a sockbuf already known to be locked;
 * return any error returned from sleep (EINTR).
 */
int
sb_lock(sb)
	struct sockbuf *sb;
{
	int error;

	while (sb->sb_flags & SB_LOCK) {
		sb->sb_flags |= SB_WANT;
		error = tsleep((caddr_t)&sb->sb_flags,
			    ((sb->sb_flags & SB_NOINTR) ? 0 : PCATCH),
			    "sblock", 0);
		if (error)
			return (error);
	}
	sb->sb_flags |= SB_LOCK;
	return (0);
}

/*
 * Wakeup processes waiting on a socket buffer.  Do asynchronous notification
 * via SIGIO if the socket has the SS_ASYNC flag set.
 */
void
sowakeup(so, sb)
	struct socket *so;
	struct sockbuf *sb;
{
	struct selinfo *selinfo = &sb->sb_sel;

	selwakeup(selinfo);
	sb->sb_flags &= ~SB_SEL;
	if (sb->sb_flags & SB_WAIT) {
		sb->sb_flags &= ~SB_WAIT;
		wakeup((caddr_t)&sb->sb_cc);
	}
	if ((so->so_state & SS_ASYNC) && so->so_sigio != NULL)
		pgsigio(so->so_sigio, SIGIO, 0);
	if (sb->sb_flags & SB_UPCALL)
		(*so->so_upcall)(so, so->so_upcallarg, MB_DONTWAIT);
	if (sb->sb_flags & SB_AIO)
		aio_swake(so, sb);
	KNOTE(&selinfo->si_note, 0);
	if (sb->sb_flags & SB_MEVENT) {
		struct netmsg_so_notify *msg, *nmsg;

		TAILQ_FOREACH_MUTABLE(msg, &selinfo->si_mlist, nm_list, nmsg) {
			if (msg->nm_predicate((struct netmsg *)msg)) {
				TAILQ_REMOVE(&selinfo->si_mlist, msg, nm_list);
				lwkt_replymsg(&msg->nm_lmsg, 
						msg->nm_lmsg.ms_error);
			}
		}
		if (TAILQ_EMPTY(&sb->sb_sel.si_mlist))
			sb->sb_flags &= ~SB_MEVENT;
	}
}

/*
 * Socket buffer (struct sockbuf) utility routines.
 *
 * Each socket contains two socket buffers: one for sending data and
 * one for receiving data.  Each buffer contains a queue of mbufs,
 * information about the number of mbufs and amount of data in the
 * queue, and other fields allowing select() statements and notification
 * on data availability to be implemented.
 *
 * Data stored in a socket buffer is maintained as a list of records.
 * Each record is a list of mbufs chained together with the m_next
 * field.  Records are chained together with the m_nextpkt field. The upper
 * level routine soreceive() expects the following conventions to be
 * observed when placing information in the receive buffer:
 *
 * 1. If the protocol requires each message be preceded by the sender's
 *    name, then a record containing that name must be present before
 *    any associated data (mbuf's must be of type MT_SONAME).
 * 2. If the protocol supports the exchange of ``access rights'' (really
 *    just additional data associated with the message), and there are
 *    ``rights'' to be received, then a record containing this data
 *    should be present (mbuf's must be of type MT_RIGHTS).
 * 3. If a name or rights record exists, then it must be followed by
 *    a data record, perhaps of zero length.
 *
 * Before using a new socket structure it is first necessary to reserve
 * buffer space to the socket, by calling sbreserve().  This should commit
 * some of the available buffer space in the system buffer pool for the
 * socket (currently, it does nothing but enforce limits).  The space
 * should be released by calling sbrelease() when the socket is destroyed.
 */

int
soreserve(struct socket *so, u_long sndcc, u_long rcvcc, struct rlimit *rl)
{
	if (sbreserve(&so->so_snd, sndcc, so, rl) == 0)
		goto bad;
	if (sbreserve(&so->so_rcv, rcvcc, so, rl) == 0)
		goto bad2;
	if (so->so_rcv.sb_lowat == 0)
		so->so_rcv.sb_lowat = 1;
	if (so->so_snd.sb_lowat == 0)
		so->so_snd.sb_lowat = MCLBYTES;
	if (so->so_snd.sb_lowat > so->so_snd.sb_hiwat)
		so->so_snd.sb_lowat = so->so_snd.sb_hiwat;
	return (0);
bad2:
	sbrelease(&so->so_snd, so);
bad:
	return (ENOBUFS);
}

static int
sysctl_handle_sb_max(SYSCTL_HANDLER_ARGS)
{
	int error = 0;
	u_long old_sb_max = sb_max;

	error = SYSCTL_OUT(req, arg1, sizeof(int));
	if (error || !req->newptr)
		return (error);
	error = SYSCTL_IN(req, arg1, sizeof(int));
	if (error)
		return (error);
	if (sb_max < MSIZE + MCLBYTES) {
		sb_max = old_sb_max;
		return (EINVAL);
	}
	sb_max_adj = (u_quad_t)sb_max * MCLBYTES / (MSIZE + MCLBYTES);
	return (0);
}
	
/*
 * Allot mbufs to a sockbuf.
 * Attempt to scale mbmax so that mbcnt doesn't become limiting
 * if buffering efficiency is near the normal case.
 */
int
sbreserve(struct sockbuf *sb, u_long cc, struct socket *so, struct rlimit *rl)
{

	/*
	 * rl will only be NULL when we're in an interrupt (eg, in tcp_input)
	 * or when called from netgraph (ie, ngd_attach)
	 */
	if (cc > sb_max_adj)
		return (0);
	if (!chgsbsize(so->so_cred->cr_uidinfo, &sb->sb_hiwat, cc,
		       rl ? rl->rlim_cur : RLIM_INFINITY)) {
		return (0);
	}
	sb->sb_mbmax = min(cc * sb_efficiency, sb_max);
	if (sb->sb_lowat > sb->sb_hiwat)
		sb->sb_lowat = sb->sb_hiwat;
	return (1);
}

/*
 * Free mbufs held by a socket, and reserved mbuf space.
 */
void
sbrelease(sb, so)
	struct sockbuf *sb;
	struct socket *so;
{

	sbflush(sb);
	(void)chgsbsize(so->so_cred->cr_uidinfo, &sb->sb_hiwat, 0,
	    RLIM_INFINITY);
	sb->sb_mbmax = 0;
}

/*
 * Routines to add and remove
 * data from an mbuf queue.
 *
 * The routines sbappend() or sbappendrecord() are normally called to
 * append new mbufs to a socket buffer, after checking that adequate
 * space is available, comparing the function sbspace() with the amount
 * of data to be added.  sbappendrecord() differs from sbappend() in
 * that data supplied is treated as the beginning of a new record.
 * To place a sender's address, optional access rights, and data in a
 * socket receive buffer, sbappendaddr() should be used.  To place
 * access rights and data in a socket receive buffer, sbappendrights()
 * should be used.  In either case, the new data begins a new record.
 * Note that unlike sbappend() and sbappendrecord(), these routines check
 * for the caller that there will be enough space to store the data.
 * Each fails if there is not enough space, or if it cannot find mbufs
 * to store additional information in.
 *
 * Reliable protocols may use the socket send buffer to hold data
 * awaiting acknowledgement.  Data is normally copied from a socket
 * send buffer in a protocol with m_copy for output to a peer,
 * and then removing the data from the socket buffer with sbdrop()
 * or sbdroprecord() when the data is acknowledged by the peer.
 */

/*
 * Append mbuf chain m to the last record in the
 * socket buffer sb.  The additional space associated
 * the mbuf chain is recorded in sb.  Empty mbufs are
 * discarded and mbufs are compacted where possible.
 */
void
sbappend(struct sockbuf *sb, struct mbuf *m)
{
	struct mbuf *n;

	if (m) {
		n = sb->sb_mb;
		if (n) {
			while (n->m_nextpkt)
				n = n->m_nextpkt;
			do {
				if (n->m_flags & M_EOR) {
					/* XXXXXX!!!! */
					sbappendrecord(sb, m);
					return;
				}
			} while (n->m_next && (n = n->m_next));
		}
		sbcompress(sb, m, n);
	}
}

/*
 * sbappendstream() is an optimized form of sbappend() for protocols
 * such as TCP that only have one record in the socket buffer, are
 * not PR_ATOMIC, nor allow MT_CONTROL data.  A protocol that uses
 * sbappendstream() must use sbappendstream() exclusively.
 */
void
sbappendstream(struct sockbuf *sb, struct mbuf *m)
{
	KKASSERT(m->m_nextpkt == NULL);
	sbcompress(sb, m, sb->sb_lastmbuf);
}

#ifdef SOCKBUF_DEBUG

void
_sbcheck(struct sockbuf *sb)
{
	struct mbuf *m;
	struct mbuf *n = NULL;
	u_long len = 0, mbcnt = 0;

	for (m = sb->sb_mb; m; m = n) {
	    n = m->m_nextpkt;
	    if (n == NULL && sb->sb_lastrecord != m) {
		    printf("sockbuf %p mismatched lastrecord %p vs %p\n", sb, sb->sb_lastrecord, m);
		    panic("sbcheck1");
		
	    }
	    for (; m; m = m->m_next) {
		len += m->m_len;
		mbcnt += MSIZE;
		if (m->m_flags & M_EXT) /*XXX*/ /* pretty sure this is bogus */
			mbcnt += m->m_ext.ext_size;
		if (n == NULL && m->m_next == NULL) {
			if (sb->sb_lastmbuf != m) {
				printf("sockbuf %p mismatched lastmbuf %p vs %p\n", sb, sb->sb_lastmbuf, m);
				panic("sbcheck2");
			}
		}
	    }
	}
	if (sb->sb_mb == NULL) {
	    if (sb->sb_lastrecord != NULL) {
		printf("sockbuf %p is empty, lastrecord not NULL: %p\n",
			sb, sb->sb_lastrecord);
		panic("sbcheck3");
	    }
	    if (sb->sb_lastmbuf != NULL) {
		printf("sockbuf %p is empty, lastmbuf not NULL: %p\n",
			sb, sb->sb_lastmbuf);
		panic("sbcheck4");
	    }
	}
	if (len != sb->sb_cc || mbcnt != sb->sb_mbcnt) {
		printf("sockbuf %p cc %ld != %ld || mbcnt %ld != %ld\n",
		    sb, len, sb->sb_cc, mbcnt, sb->sb_mbcnt);
		panic("sbcheck5");
	}
}

#endif

/*
 * Same as sbappend(), except the mbuf chain begins a new record.
 */
void
sbappendrecord(struct sockbuf *sb, struct mbuf *m0)
{
	struct mbuf *firstmbuf;
	struct mbuf *secondmbuf;

	if (m0 == NULL)
		return;

	sbcheck(sb);

	/*
	 * Break the first mbuf off from the rest of the mbuf chain.
	 */
	firstmbuf = m0;
	secondmbuf = m0->m_next;
	m0->m_next = NULL;

	/*
	 * Insert the first mbuf of the m0 mbuf chain as the last record of
	 * the sockbuf.  Note this permits zero length records!  Keep the
	 * sockbuf state consistent.
	 */
	if (sb->sb_mb == NULL)
		sb->sb_mb = firstmbuf;
	else
		sb->sb_lastrecord->m_nextpkt = firstmbuf;
	sb->sb_lastrecord = firstmbuf;	/* update hint for new last record */
	sb->sb_lastmbuf = firstmbuf;	/* update hint for new last mbuf */

	if ((firstmbuf->m_flags & M_EOR) && (secondmbuf != NULL)) {
		/* propagate the EOR flag */
		firstmbuf->m_flags &= ~M_EOR;
		secondmbuf->m_flags |= M_EOR;
	}

	/*
	 * The succeeding call to sbcompress() omits accounting for
	 * the first mbuf, so do it here.
	 */
	sballoc(sb, firstmbuf);

	/* Compact the rest of the mbuf chain in after the first mbuf. */
	sbcompress(sb, secondmbuf, firstmbuf);
}

#if 0
/*
 * As above except that OOB data is inserted at the beginning of the sockbuf,
 * but after any other OOB data.
 */
void
sbinsertoob(struct sockbuf *sb, struct mbuf *m0)
{
	struct mbuf *m;
	struct mbuf **mp;

	if (m0 == NULL)
		return;
	for (mp = &sb->sb_mb; *mp ; mp = &((*mp)->m_nextpkt)) {
	    m = *mp;
	    again:
		switch (m->m_type) {

		case MT_OOBDATA:
			continue;		/* WANT next train */

		case MT_CONTROL:
			m = m->m_next;
			if (m)
				goto again;	/* inspect THIS train further */
		}
		break;
	}
	/*
	 * Put the first mbuf on the queue.
	 * Note this permits zero length records.
	 */
	sballoc(sb, m0);
	m0->m_nextpkt = *mp;
	*mp = m0;
	if (m0->m_nextpkt == NULL)
		sb->sb_lastrecord = m0;

	m = m0->m_next;
	m0->m_next = NULL;
	if (m && (m0->m_flags & M_EOR)) {
		m0->m_flags &= ~M_EOR;
		m->m_flags |= M_EOR;
	}
	sbcompress(sb, m, m0);
}
#endif

/*
 * Append address and data, and optionally, control (ancillary) data
 * to the receive queue of a socket.  If present,
 * m0 must include a packet header with total length.
 * Returns 0 if no space in sockbuf or insufficient mbufs.
 */
int
sbappendaddr(sb, asa, m0, control)
	struct sockbuf *sb;
	const struct sockaddr *asa;
	struct mbuf *m0, *control;
{
	struct mbuf *m, *n;
	int space = asa->sa_len;

	if (m0 && (m0->m_flags & M_PKTHDR) == 0)
		panic("sbappendaddr");
	sbcheck(sb);

	if (m0)
		space += m0->m_pkthdr.len;
	for (n = control; n; n = n->m_next) {
		space += n->m_len;
		if (n->m_next == 0)	/* keep pointer to last control buf */
			break;
	}
	if (space > sbspace(sb))
		return (0);
	if (asa->sa_len > MLEN)
		return (0);
	MGET(m, MB_DONTWAIT, MT_SONAME);
	if (m == NULL)
		return (0);
	KKASSERT(m->m_nextpkt == NULL);
	m->m_len = asa->sa_len;
	bcopy(asa, mtod(m, caddr_t), asa->sa_len);
	if (n)
		n->m_next = m0;		/* concatenate data to control */
	else
		control = m0;
	m->m_next = control;
	for (n = m; n; n = n->m_next)
		sballoc(sb, n);

	if (sb->sb_mb == NULL)
		sb->sb_mb = m;
	else
		sb->sb_lastrecord->m_nextpkt = m;
	sb->sb_lastrecord = m;
	while (m->m_next)
		m = m->m_next;
	sb->sb_lastmbuf = m;

	return (1);
}

/*
 * Append control information followed by data.
 * control must be non-null.
 */
int
sbappendcontrol(struct sockbuf *sb, struct mbuf *m0, struct mbuf *control)
{
	struct mbuf *n;
	u_int length, cmbcnt, m0mbcnt;

	KASSERT(control != NULL, ("sbappendcontrol"));
	KKASSERT(control->m_nextpkt == NULL);
	sbcheck(sb);

	length = m_countm(control, &n, &cmbcnt) + m_countm(m0, NULL, &m0mbcnt);
	if (length > sbspace(sb))
		return (0);

	n->m_next = m0;			/* concatenate data to control */

	if (sb->sb_mb == NULL)
		sb->sb_mb = control;
	else
		sb->sb_lastrecord->m_nextpkt = control;
	sb->sb_lastrecord = control;
	sb->sb_lastmbuf = m0;

	sb->sb_cc += length;
	sb->sb_mbcnt += cmbcnt + m0mbcnt;

	return (1);
}

/*
 * Compress mbuf chain m into the socket buffer sb following mbuf tailm.
 * If tailm is null, the buffer is presumed empty.  Also, as a side-effect,
 * increment the sockbuf counts for each mbuf in the chain.
 */
void
sbcompress(struct sockbuf *sb, struct mbuf *m, struct mbuf *tailm)
{
	int eor = 0;
	struct mbuf *free_chain = NULL;

	sbcheck(sb);
	while (m) {
		struct mbuf *o;

		eor |= m->m_flags & M_EOR;
		/*
		 * Disregard empty mbufs as long as we don't encounter
		 * an end-of-record or there is a trailing mbuf of
		 * the same type to propagate the EOR flag to.
		 *
		 * Defer the m_free() call because it can block and break
		 * the atomicy of the sockbuf.
		 */
		if (m->m_len == 0 &&
		    (eor == 0 ||
		     (((o = m->m_next) || (o = tailm)) &&
		      o->m_type == m->m_type))) {
			o = m->m_next;
			m->m_next = free_chain;
			free_chain = m;
			m = o;
			continue;
		}

		/* See if we can coalesce with preceding mbuf. */
		if (tailm && !(tailm->m_flags & M_EOR) && M_WRITABLE(tailm) &&
		    m->m_len <= MCLBYTES / 4 && /* XXX: Don't copy too much */
		    m->m_len <= M_TRAILINGSPACE(tailm) &&
		    tailm->m_type == m->m_type) {
			bcopy(mtod(m, caddr_t),
			      mtod(tailm, caddr_t) + tailm->m_len,
			      (unsigned)m->m_len);
			tailm->m_len += m->m_len;
			sb->sb_cc += m->m_len;		/* update sb counter */
			o = m->m_next;
			m->m_next = free_chain;
			free_chain = m;
			m = o;
			continue;
		}

		/* Insert whole mbuf. */
		if (tailm == NULL) {
			KASSERT(sb->sb_mb == NULL,
				("sbcompress: sb_mb not NULL"));
			sb->sb_mb = m;		/* only mbuf in sockbuf */
			sb->sb_lastrecord = m;	/* new last record */
		} else {
			tailm->m_next = m;	/* tack m on following tailm */
		}
		sb->sb_lastmbuf = m;	/* update last mbuf hint */

		tailm = m;	/* just inserted mbuf becomes the new tail */
		m = m->m_next;		/* advance to next mbuf */
		tailm->m_next = NULL;	/* split inserted mbuf off from chain */

		/* update sb counters for just added mbuf */
		sballoc(sb, tailm);

		/* clear EOR on intermediate mbufs */
		tailm->m_flags &= ~M_EOR;
	}

	/*
	 * Propogate EOR to the last mbuf
	 */
	if (eor) {
		if (tailm)
			tailm->m_flags |= eor;
		else
			printf("semi-panic: sbcompress");
	}

	/*
	 * Clean up any defered frees.
	 */
	while (free_chain)
		free_chain = m_free(free_chain);

	sbcheck(sb);
}

/*
 * Free all mbufs in a sockbuf.
 * Check that all resources are reclaimed.
 */
void
sbflush(sb)
	struct sockbuf *sb;
{

	if (sb->sb_flags & SB_LOCK)
		panic("sbflush: locked");
	while (sb->sb_mbcnt) {
		/*
		 * Don't call sbdrop(sb, 0) if the leading mbuf is non-empty:
		 * we would loop forever. Panic instead.
		 */
		if (!sb->sb_cc && (sb->sb_mb == NULL || sb->sb_mb->m_len))
			break;
		sbdrop(sb, (int)sb->sb_cc);
	}
	KASSERT(!(sb->sb_cc || sb->sb_mb || sb->sb_mbcnt || sb->sb_lastmbuf),
	    ("sbflush: cc %ld || mb %p || mbcnt %ld || lastmbuf %p",
	    sb->sb_cc, sb->sb_mb, sb->sb_mbcnt, sb->sb_lastmbuf));
}

/*
 * Drop data from (the front of) a sockbuf.
 */
void
sbdrop(sb, len)
	struct sockbuf *sb;
	int len;
{
	struct mbuf *m;
	struct mbuf *free_chain = NULL;

	sbcheck(sb);
	crit_enter();

	/*
	 * Remove mbufs from multiple records until the count is exhausted.
	 */
	m = sb->sb_mb;
	while (m && len > 0) {
		if (m->m_len > len) {
			m->m_len -= len;
			m->m_data += len;
			sb->sb_cc -= len;
			break;
		}
		len -= m->m_len;
		m = sbunlinkmbuf(sb, m, &free_chain);
		if (m == NULL && len)
			m = sb->sb_mb;
	}

	/*
	 * Remove any trailing 0-length mbufs in the current record.  If
	 * the last record for which data was removed is now empty, m will be
	 * NULL.
	 */
	while (m && m->m_len == 0) {
		m = sbunlinkmbuf(sb, m, &free_chain);
	}
	crit_exit();
	if (free_chain)
		m_freem(free_chain);
	sbcheck(sb);
}

/*
 * Drop a record off the front of a sockbuf and move the next record
 * to the front.
 *
 * Must be called while holding a critical section.
 */
void
sbdroprecord(sb)
	struct sockbuf *sb;
{
	struct mbuf *m;
	struct mbuf *n;

	sbcheck(sb);
	m = sb->sb_mb;
	if (m) {
		if ((sb->sb_mb = m->m_nextpkt) == NULL) {
			sb->sb_lastrecord = NULL;
			sb->sb_lastmbuf = NULL;
		}
		m->m_nextpkt = NULL;
		for (n = m; n; n = n->m_next)
			sbfree(sb, n);
		m_freem(m);
		sbcheck(sb);
	}
}

/*
 * Drop the first mbuf off the sockbuf and move the next mbuf to the front.
 * Currently only the head mbuf of the sockbuf may be dropped this way.
 *
 * The next mbuf in the same record as the mbuf being removed is returned
 * or NULL if the record is exhausted.  Note that other records may remain
 * in the sockbuf when NULL is returned.
 *
 * Must be called while holding a critical section.
 */
struct mbuf *
sbunlinkmbuf(struct sockbuf *sb, struct mbuf *m, struct mbuf **free_chain)
{
	struct mbuf *n;

	KKASSERT(sb->sb_mb == m);
	sbfree(sb, m);
	n = m->m_next;
	if (n) {
		sb->sb_mb = n;
		if (sb->sb_lastrecord == m)
			sb->sb_lastrecord = n;
		KKASSERT(sb->sb_lastmbuf != m);
		n->m_nextpkt = m->m_nextpkt;
	} else {
		sb->sb_mb = m->m_nextpkt;
		if (sb->sb_lastrecord == m) {
			KKASSERT(sb->sb_mb == NULL);
			sb->sb_lastrecord = NULL;
		}
		if (sb->sb_mb == NULL)
			sb->sb_lastmbuf = NULL;
	}
	m->m_nextpkt = NULL;
	if (free_chain) {
		m->m_next = *free_chain;
		*free_chain = m;
	} else {
		m->m_next = NULL;
	}
	return(n);
}

/*
 * Create a "control" mbuf containing the specified data
 * with the specified type for presentation on a socket buffer.
 */
struct mbuf *
sbcreatecontrol(p, size, type, level)
	caddr_t p;
	int size;
	int type, level;
{
	struct cmsghdr *cp;
	struct mbuf *m;

	if (CMSG_SPACE((u_int)size) > MCLBYTES)
		return (NULL);
	m = m_getl(CMSG_SPACE((u_int)size), MB_DONTWAIT, MT_CONTROL, 0, NULL);
	if (m == NULL)
		return (NULL);
	m->m_len = CMSG_SPACE(size);
	cp = mtod(m, struct cmsghdr *);
	if (p != NULL)
		memcpy(CMSG_DATA(cp), p, size);
	cp->cmsg_len = CMSG_LEN(size);
	cp->cmsg_level = level;
	cp->cmsg_type = type;
	return (m);
}

/*
 * Some routines that return EOPNOTSUPP for entry points that are not
 * supported by a protocol.  Fill in as needed.
 */
int
pru_accept_notsupp(struct socket *so, struct sockaddr **nam)
{
	return EOPNOTSUPP;
}

int
pru_connect_notsupp(struct socket *so, struct sockaddr *nam, struct thread *td)
{
	return EOPNOTSUPP;
}

int
pru_connect2_notsupp(struct socket *so1, struct socket *so2)
{
	return EOPNOTSUPP;
}

int
pru_control_notsupp(struct socket *so, u_long cmd, caddr_t data,
		    struct ifnet *ifp, struct thread *td)
{
	return EOPNOTSUPP;
}

int
pru_listen_notsupp(struct socket *so, struct thread *td)
{
	return EOPNOTSUPP;
}

int
pru_rcvd_notsupp(struct socket *so, int flags)
{
	return EOPNOTSUPP;
}

int
pru_rcvoob_notsupp(struct socket *so, struct mbuf *m, int flags)
{
	return EOPNOTSUPP;
}

/*
 * This isn't really a ``null'' operation, but it's the default one
 * and doesn't do anything destructive.
 */
int
pru_sense_null(struct socket *so, struct stat *sb)
{
	sb->st_blksize = so->so_snd.sb_hiwat;
	return 0;
}

/*
 * Make a copy of a sockaddr in a malloced buffer of type M_SONAME.  Callers
 * of this routine assume that it always succeeds, so we have to use a 
 * blockable allocation even though we might be called from a critical thread.
 */
struct sockaddr *
dup_sockaddr(const struct sockaddr *sa)
{
	struct sockaddr *sa2;

	sa2 = malloc(sa->sa_len, M_SONAME, M_INTWAIT);
	bcopy(sa, sa2, sa->sa_len);
	return (sa2);
}

/*
 * Create an external-format (``xsocket'') structure using the information
 * in the kernel-format socket structure pointed to by so.  This is done
 * to reduce the spew of irrelevant information over this interface,
 * to isolate user code from changes in the kernel structure, and
 * potentially to provide information-hiding if we decide that
 * some of this information should be hidden from users.
 */
void
sotoxsocket(struct socket *so, struct xsocket *xso)
{
	xso->xso_len = sizeof *xso;
	xso->xso_so = so;
	xso->so_type = so->so_type;
	xso->so_options = so->so_options;
	xso->so_linger = so->so_linger;
	xso->so_state = so->so_state;
	xso->so_pcb = so->so_pcb;
	xso->xso_protocol = so->so_proto->pr_protocol;
	xso->xso_family = so->so_proto->pr_domain->dom_family;
	xso->so_qlen = so->so_qlen;
	xso->so_incqlen = so->so_incqlen;
	xso->so_qlimit = so->so_qlimit;
	xso->so_timeo = so->so_timeo;
	xso->so_error = so->so_error;
	xso->so_pgid = so->so_sigio ? so->so_sigio->sio_pgid : 0;
	xso->so_oobmark = so->so_oobmark;
	sbtoxsockbuf(&so->so_snd, &xso->so_snd);
	sbtoxsockbuf(&so->so_rcv, &xso->so_rcv);
	xso->so_uid = so->so_cred->cr_uid;
}

/*
 * This does the same for sockbufs.  Note that the xsockbuf structure,
 * since it is always embedded in a socket, does not include a self
 * pointer nor a length.  We make this entry point public in case
 * some other mechanism needs it.
 */
void
sbtoxsockbuf(struct sockbuf *sb, struct xsockbuf *xsb)
{
	xsb->sb_cc = sb->sb_cc;
	xsb->sb_hiwat = sb->sb_hiwat;
	xsb->sb_mbcnt = sb->sb_mbcnt;
	xsb->sb_mbmax = sb->sb_mbmax;
	xsb->sb_lowat = sb->sb_lowat;
	xsb->sb_flags = sb->sb_flags;
	xsb->sb_timeo = sb->sb_timeo;
}

/*
 * Here is the definition of some of the basic objects in the kern.ipc
 * branch of the MIB.
 */
SYSCTL_NODE(_kern, KERN_IPC, ipc, CTLFLAG_RW, 0, "IPC");

/* This takes the place of kern.maxsockbuf, which moved to kern.ipc. */
static int dummy;
SYSCTL_INT(_kern, KERN_DUMMY, dummy, CTLFLAG_RW, &dummy, 0, "");
SYSCTL_OID(_kern_ipc, KIPC_MAXSOCKBUF, maxsockbuf, CTLTYPE_INT|CTLFLAG_RW, 
    &sb_max, 0, sysctl_handle_sb_max, "I", "Maximum socket buffer size");
SYSCTL_INT(_kern_ipc, OID_AUTO, maxsockets, CTLFLAG_RD, 
    &maxsockets, 0, "Maximum number of sockets avaliable");
SYSCTL_INT(_kern_ipc, KIPC_SOCKBUF_WASTE, sockbuf_waste_factor, CTLFLAG_RW,
    &sb_efficiency, 0, "");

/*
 * Initialise maxsockets 
 */
static void init_maxsockets(void *ignored)
{
    TUNABLE_INT_FETCH("kern.ipc.maxsockets", &maxsockets);
    maxsockets = imax(maxsockets, imax(maxfiles, nmbclusters));
}
SYSINIT(param, SI_SUB_TUNABLES, SI_ORDER_ANY, init_maxsockets, NULL);
