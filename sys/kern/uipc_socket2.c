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
 *	@(#)uipc_socket2.c	8.1 (Berkeley) 6/10/93
 * $FreeBSD: src/sys/kern/uipc_socket2.c,v 1.55.2.17 2002/08/31 19:04:55 dwmalone Exp $
 * $DragonFly: src/sys/kern/uipc_socket2.c,v 1.33 2008/09/02 16:17:52 dillon Exp $
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
#include <sys/socketops.h>
#include <sys/signalvar.h>
#include <sys/sysctl.h>
#include <sys/event.h>

#include <sys/thread2.h>
#include <sys/msgport2.h>
#include <sys/socketvar2.h>

#include <net/netisr2.h>

int	maxsockets;

/*
 * Primitive routines for operating on sockets and socket buffers
 */

u_long	sb_max = SB_MAX;
u_long	sb_max_adj =
    SB_MAX * MCLBYTES / (MSIZE + MCLBYTES); /* adjusted sb_max */

static	u_long sb_efficiency = 8;	/* parameter for sbreserve() */

/************************************************************************
 * signalsockbuf procedures						*
 ************************************************************************/

/*
 * Wait for data to arrive at/drain from a socket buffer.
 *
 * NOTE: Caller must generally hold the ssb_lock (client side lock) since
 *	 WAIT/WAKEUP only works for one client at a time.
 *
 * NOTE: Caller always retries whatever operation it was waiting on.
 */
int
ssb_wait(struct signalsockbuf *ssb)
{
	uint32_t flags;
	int pflags;
	int error;

	pflags = (ssb->ssb_flags & SSB_NOINTR) ? 0 : PCATCH;

	for (;;) {
		flags = ssb->ssb_flags;
		cpu_ccfence();

		/*
		 * WAKEUP and WAIT interlock eachother.  We can catch the
		 * race by checking to see if WAKEUP has already been set,
		 * and only setting WAIT if WAKEUP is clear.
		 */
		if (flags & SSB_WAKEUP) {
			if (atomic_cmpset_int(&ssb->ssb_flags, flags,
					      flags & ~SSB_WAKEUP)) {
				error = 0;
				break;
			}
			continue;
		}

		/*
		 * Only set WAIT if WAKEUP is clear.
		 */
		tsleep_interlock(&ssb->ssb_cc, pflags);
		if (atomic_cmpset_int(&ssb->ssb_flags, flags,
				      flags | SSB_WAIT)) {
			error = tsleep(&ssb->ssb_cc, pflags | PINTERLOCKED,
				       "sbwait", ssb->ssb_timeo);
			break;
		}
	}
	return (error);
}

/*
 * Lock a sockbuf already known to be locked;
 * return any error returned from sleep (EINTR).
 */
int
_ssb_lock(struct signalsockbuf *ssb)
{
	uint32_t flags;
	int pflags;
	int error;

	pflags = (ssb->ssb_flags & SSB_NOINTR) ? 0 : PCATCH;

	for (;;) {
		flags = ssb->ssb_flags;
		cpu_ccfence();
		if (flags & SSB_LOCK) {
			tsleep_interlock(&ssb->ssb_flags, pflags);
			if (atomic_cmpset_int(&ssb->ssb_flags, flags,
					      flags | SSB_WANT)) {
				error = tsleep(&ssb->ssb_flags,
					       pflags | PINTERLOCKED,
					       "sblock", 0);
				if (error)
					break;
			}
		} else {
			if (atomic_cmpset_int(&ssb->ssb_flags, flags,
					      flags | SSB_LOCK)) {
				lwkt_gettoken(&ssb->ssb_token);
				error = 0;
				break;
			}
		}
	}
	return (error);
}

/*
 * This does the same for sockbufs.  Note that the xsockbuf structure,
 * since it is always embedded in a socket, does not include a self
 * pointer nor a length.  We make this entry point public in case
 * some other mechanism needs it.
 */
void
ssbtoxsockbuf(struct signalsockbuf *ssb, struct xsockbuf *xsb)
{
	xsb->sb_cc = ssb->ssb_cc;
	xsb->sb_hiwat = ssb->ssb_hiwat;
	xsb->sb_mbcnt = ssb->ssb_mbcnt;
	xsb->sb_mbmax = ssb->ssb_mbmax;
	xsb->sb_lowat = ssb->ssb_lowat;
	xsb->sb_flags = ssb->ssb_flags;
	xsb->sb_timeo = ssb->ssb_timeo;
}


/************************************************************************
 * Procedures which manipulate socket state flags, wakeups, etc.	*
 ************************************************************************
 *
 * Normal sequence from the active (originating) side is that
 * soisconnecting() is called during processing of connect() call, resulting
 * in an eventual call to soisconnected() if/when the connection is
 * established.  When the connection is torn down soisdisconnecting() is
 * called during processing of disconnect() call, and soisdisconnected() is
 * called when the connection to the peer is totally severed.
 *
 * The semantics of these routines are such that connectionless protocols
 * can call soisconnected() and soisdisconnected() only, bypassing the
 * in-progress calls when setting up a ``connection'' takes no time.
 *
 * From the passive side, a socket is created with two queues of sockets:
 * so_incomp for connections in progress and so_comp for connections
 * already made and awaiting user acceptance.  As a protocol is preparing
 * incoming connections, it creates a socket structure queued on so_incomp
 * by calling sonewconn().  When the connection is established,
 * soisconnected() is called, and transfers the socket structure to so_comp,
 * making it available to accept().
 *
 * If a socket is closed with sockets on either so_incomp or so_comp, these
 * sockets are dropped.
 *
 * If higher level protocols are implemented in the kernel, the wakeups
 * done here will sometimes cause software-interrupt process scheduling.
 */

void
soisconnecting(struct socket *so)
{
	soclrstate(so, SS_ISCONNECTED | SS_ISDISCONNECTING);
	sosetstate(so, SS_ISCONNECTING);
}

void
soisconnected(struct socket *so)
{
	struct socket *head;

	while ((head = so->so_head) != NULL) {
		lwkt_getpooltoken(head);
		if (so->so_head == head)
			break;
		lwkt_relpooltoken(head);
	}

	soclrstate(so, SS_ISCONNECTING | SS_ISDISCONNECTING | SS_ISCONFIRMING);
	sosetstate(so, SS_ISCONNECTED);
	if (head && (so->so_state & SS_INCOMP)) {
		if ((so->so_options & SO_ACCEPTFILTER) != 0) {
			so->so_upcall = head->so_accf->so_accept_filter->accf_callback;
			so->so_upcallarg = head->so_accf->so_accept_filter_arg;
			atomic_set_int(&so->so_rcv.ssb_flags, SSB_UPCALL);
			so->so_options &= ~SO_ACCEPTFILTER;
			so->so_upcall(so, so->so_upcallarg, 0);
			lwkt_relpooltoken(head);
			return;
		}

		/*
		 * Listen socket are not per-cpu.
		 */
		TAILQ_REMOVE(&head->so_incomp, so, so_list);
		head->so_incqlen--;
		TAILQ_INSERT_TAIL(&head->so_comp, so, so_list);
		head->so_qlen++;
		sosetstate(so, SS_COMP);
		soclrstate(so, SS_INCOMP);

		/*
		 * XXX head may be on a different protocol thread.
		 *     sorwakeup()->sowakeup() is hacked atm.
		 */
		sorwakeup(head);
		wakeup_one(&head->so_timeo);
	} else {
		wakeup(&so->so_timeo);
		sorwakeup(so);
		sowwakeup(so);
	}
	if (head)
		lwkt_relpooltoken(head);
}

void
soisdisconnecting(struct socket *so)
{
	soclrstate(so, SS_ISCONNECTING);
	sosetstate(so, SS_ISDISCONNECTING | SS_CANTRCVMORE | SS_CANTSENDMORE);
	wakeup((caddr_t)&so->so_timeo);
	sowwakeup(so);
	sorwakeup(so);
}

void
soisdisconnected(struct socket *so)
{
	soclrstate(so, SS_ISCONNECTING | SS_ISCONNECTED | SS_ISDISCONNECTING);
	sosetstate(so, SS_CANTRCVMORE | SS_CANTSENDMORE | SS_ISDISCONNECTED);
	wakeup((caddr_t)&so->so_timeo);
	sbdrop(&so->so_snd.sb, so->so_snd.ssb_cc);
	sowwakeup(so);
	sorwakeup(so);
}

void
soisreconnecting(struct socket *so)
{
        soclrstate(so, SS_ISDISCONNECTING | SS_ISDISCONNECTED |
		       SS_CANTRCVMORE | SS_CANTSENDMORE);
	sosetstate(so, SS_ISCONNECTING);
}

void
soisreconnected(struct socket *so)
{
	soclrstate(so, SS_ISDISCONNECTED | SS_CANTRCVMORE | SS_CANTSENDMORE);
	soisconnected(so);
}

/*
 * Set or change the message port a socket receives commands on.
 *
 * XXX
 */
void
sosetport(struct socket *so, lwkt_port_t port)
{
	so->so_port = port;
}

/*
 * When an attempt at a new connection is noted on a socket
 * which accepts connections, sonewconn is called.  If the
 * connection is possible (subject to space constraints, etc.)
 * then we allocate a new structure, propoerly linked into the
 * data structure of the original socket, and return this.
 * Connstatus may be 0, or SO_ISCONFIRMING, or SO_ISCONNECTED.
 *
 * The new socket is returned with one ref and so_pcb assigned.
 * The reference is implied by so_pcb.
 */
struct socket *
sonewconn_faddr(struct socket *head, int connstatus,
    const struct sockaddr *faddr)
{
	struct socket *so;
	struct socket *sp;
	struct pru_attach_info ai;

	if (head->so_qlen > 3 * head->so_qlimit / 2)
		return (NULL);
	so = soalloc(1, head->so_proto);
	if (so == NULL)
		return (NULL);

	/*
	 * Set the port prior to attaching the inpcb to the current
	 * cpu's protocol thread (which should be the current thread
	 * but might not be in all cases).  This serializes any pcb ops
	 * which occur to our cpu allowing us to complete the attachment
	 * without racing anything.
	 */
	if (head->so_proto->pr_flags & PR_SYNC_PORT)
		sosetport(so, &netisr_sync_port);
	else
		sosetport(so, netisr_cpuport(mycpuid));
	if ((head->so_options & SO_ACCEPTFILTER) != 0)
		connstatus = 0;
	so->so_head = head;
	so->so_type = head->so_type;
	so->so_options = head->so_options &~ SO_ACCEPTCONN;
	so->so_linger = head->so_linger;

	/*
	 * NOTE: Clearing NOFDREF implies referencing the so with
	 *	 soreference().
	 */
	so->so_state = head->so_state | SS_NOFDREF | SS_ASSERTINPROG;
	so->so_cred = crhold(head->so_cred);
	ai.sb_rlimit = NULL;
	ai.p_ucred = NULL;
	ai.fd_rdir = NULL;		/* jail code cruft XXX JH */

	/*
	 * Reserve space and call pru_attach.  We can direct-call the
	 * function since we're already in the protocol thread.
	 */
	if (soreserve(so, head->so_snd.ssb_hiwat,
		      head->so_rcv.ssb_hiwat, NULL) ||
	    so_pru_attach_direct(so, 0, &ai)) {
		so->so_head = NULL;
		soclrstate(so, SS_ASSERTINPROG);
		sofree(so);		/* remove implied pcb ref */
		return (NULL);
	}
	KKASSERT(((so->so_proto->pr_flags & PR_ASYNC_RCVD) == 0 &&
	    so->so_refs == 2) ||	/* attach + our base ref */
	   ((so->so_proto->pr_flags & PR_ASYNC_RCVD) &&
	    so->so_refs == 3));		/* + async rcvd ref */
	sofree(so);
	KKASSERT(so->so_port != NULL);
	so->so_rcv.ssb_lowat = head->so_rcv.ssb_lowat;
	so->so_snd.ssb_lowat = head->so_snd.ssb_lowat;
	so->so_rcv.ssb_timeo = head->so_rcv.ssb_timeo;
	so->so_snd.ssb_timeo = head->so_snd.ssb_timeo;

	if (head->so_rcv.ssb_flags & SSB_AUTOLOWAT)
		so->so_rcv.ssb_flags |= SSB_AUTOLOWAT;
	else
		so->so_rcv.ssb_flags &= ~SSB_AUTOLOWAT;

	if (head->so_snd.ssb_flags & SSB_AUTOLOWAT)
		so->so_snd.ssb_flags |= SSB_AUTOLOWAT;
	else
		so->so_snd.ssb_flags &= ~SSB_AUTOLOWAT;

	if (head->so_rcv.ssb_flags & SSB_AUTOSIZE)
		so->so_rcv.ssb_flags |= SSB_AUTOSIZE;
	else
		so->so_rcv.ssb_flags &= ~SSB_AUTOSIZE;

	if (head->so_snd.ssb_flags & SSB_AUTOSIZE)
		so->so_snd.ssb_flags |= SSB_AUTOSIZE;
	else
		so->so_snd.ssb_flags &= ~SSB_AUTOSIZE;

	/*
	 * Save the faddr, if the information is provided and
	 * the protocol can perform the saving opertation.
	 */
	if (faddr != NULL && so->so_proto->pr_usrreqs->pru_savefaddr != NULL)
		so->so_proto->pr_usrreqs->pru_savefaddr(so, faddr);

	lwkt_getpooltoken(head);
	if (connstatus) {
		TAILQ_INSERT_TAIL(&head->so_comp, so, so_list);
		sosetstate(so, SS_COMP);
		head->so_qlen++;
	} else {
		if (head->so_incqlen > head->so_qlimit) {
			sp = TAILQ_FIRST(&head->so_incomp);
			TAILQ_REMOVE(&head->so_incomp, sp, so_list);
			head->so_incqlen--;
			soclrstate(sp, SS_INCOMP);
			sp->so_head = NULL;
			soabort_async(sp);
		}
		TAILQ_INSERT_TAIL(&head->so_incomp, so, so_list);
		sosetstate(so, SS_INCOMP);
		head->so_incqlen++;
	}
	lwkt_relpooltoken(head);
	if (connstatus) {
		/*
		 * XXX head may be on a different protocol thread.
		 *     sorwakeup()->sowakeup() is hacked atm.
		 */
		sorwakeup(head);
		wakeup((caddr_t)&head->so_timeo);
		sosetstate(so, connstatus);
	}
	soclrstate(so, SS_ASSERTINPROG);
	return (so);
}

struct socket *
sonewconn(struct socket *head, int connstatus)
{
	return sonewconn_faddr(head, connstatus, NULL);
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
socantsendmore(struct socket *so)
{
	sosetstate(so, SS_CANTSENDMORE);
	sowwakeup(so);
}

void
socantrcvmore(struct socket *so)
{
	sosetstate(so, SS_CANTRCVMORE);
	sorwakeup(so);
}

/*
 * Wakeup processes waiting on a socket buffer.  Do asynchronous notification
 * via SIGIO if the socket has the SS_ASYNC flag set.
 *
 * For users waiting on send/recv try to avoid unnecessary context switch
 * thrashing.  Particularly for senders of large buffers (needs to be
 * extended to sel and aio? XXX)
 *
 * WARNING!  Can be called on a foreign socket from the wrong protocol
 *	     thread.  aka is called on the 'head' listen socket when
 *	     a new connection comes in.
 */
void
sowakeup(struct socket *so, struct signalsockbuf *ssb)
{
	struct kqinfo *kqinfo = &ssb->ssb_kq;
	uint32_t flags;

	/*
	 * Check conditions, set the WAKEUP flag, and clear and signal if
	 * the WAIT flag is found to be set.  This interlocks against the
	 * client side.
	 */
	for (;;) {
		flags = ssb->ssb_flags;
		cpu_ccfence();

		if ((ssb == &so->so_snd && ssb_space(ssb) >= ssb->ssb_lowat) ||
		    (ssb == &so->so_rcv && ssb->ssb_cc >= ssb->ssb_lowat) ||
		    (ssb == &so->so_snd && (so->so_state & SS_CANTSENDMORE)) ||
		    (ssb == &so->so_rcv && (so->so_state & SS_CANTRCVMORE))
		) {
			if (atomic_cmpset_int(&ssb->ssb_flags, flags,
					  (flags | SSB_WAKEUP) & ~SSB_WAIT)) {
				if (flags & SSB_WAIT)
					wakeup(&ssb->ssb_cc);
				break;
			}
		} else {
			break;
		}
	}

	/*
	 * Misc other events
	 */
	if ((so->so_state & SS_ASYNC) && so->so_sigio != NULL)
		pgsigio(so->so_sigio, SIGIO, 0);
	if (ssb->ssb_flags & SSB_UPCALL)
		(*so->so_upcall)(so, so->so_upcallarg, MB_DONTWAIT);
	KNOTE(&kqinfo->ki_note, 0);

	/*
	 * This is a bit of a hack.  Multiple threads can wind up scanning
	 * ki_mlist concurrently due to the fact that this function can be
	 * called on a foreign socket, so we can't afford to block here.
	 *
	 * We need the pool token for (so) (likely the listne socket if
	 * SSB_MEVENT is set) because the predicate function may have
	 * to access the accept queue.
	 */
	if (ssb->ssb_flags & SSB_MEVENT) {
		struct netmsg_so_notify *msg, *nmsg;

		lwkt_getpooltoken(so);
		TAILQ_FOREACH_MUTABLE(msg, &kqinfo->ki_mlist, nm_list, nmsg) {
			if (msg->nm_predicate(msg)) {
				TAILQ_REMOVE(&kqinfo->ki_mlist, msg, nm_list);
				lwkt_replymsg(&msg->base.lmsg,
					      msg->base.lmsg.ms_error);
			}
		}
		if (TAILQ_EMPTY(&ssb->ssb_kq.ki_mlist))
			atomic_clear_int(&ssb->ssb_flags, SSB_MEVENT);
		lwkt_relpooltoken(so);
	}
}

/*
 * Socket buffer (struct signalsockbuf) utility routines.
 *
 * Each socket contains two socket buffers: one for sending data and
 * one for receiving data.  Each buffer contains a queue of mbufs,
 * information about the number of mbufs and amount of data in the
 * queue, and other fields allowing kevent()/select()/poll() statements
 * and notification on data availability to be implemented.
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
 * should be released by calling ssb_release() when the socket is destroyed.
 */
int
soreserve(struct socket *so, u_long sndcc, u_long rcvcc, struct rlimit *rl)
{
	if (so->so_snd.ssb_lowat == 0)
		atomic_set_int(&so->so_snd.ssb_flags, SSB_AUTOLOWAT);
	if (ssb_reserve(&so->so_snd, sndcc, so, rl) == 0)
		goto bad;
	if (ssb_reserve(&so->so_rcv, rcvcc, so, rl) == 0)
		goto bad2;
	if (so->so_rcv.ssb_lowat == 0)
		so->so_rcv.ssb_lowat = 1;
	if (so->so_snd.ssb_lowat == 0)
		so->so_snd.ssb_lowat = MCLBYTES;
	if (so->so_snd.ssb_lowat > so->so_snd.ssb_hiwat)
		so->so_snd.ssb_lowat = so->so_snd.ssb_hiwat;
	return (0);
bad2:
	ssb_release(&so->so_snd, so);
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
 * Allot mbufs to a signalsockbuf.
 *
 * Attempt to scale mbmax so that mbcnt doesn't become limiting
 * if buffering efficiency is near the normal case.
 *
 * sb_max only applies to user-sockets (where rl != NULL).  It does
 * not apply to kernel sockets or kernel-controlled sockets.  Note
 * that NFS overrides the sockbuf limits created when nfsd creates
 * a socket.
 */
int
ssb_reserve(struct signalsockbuf *ssb, u_long cc, struct socket *so,
	    struct rlimit *rl)
{
	/*
	 * rl will only be NULL when we're in an interrupt (eg, in tcp_input)
	 * or when called from netgraph (ie, ngd_attach)
	 */
	if (rl && cc > sb_max_adj)
		cc = sb_max_adj;
	if (!chgsbsize(so->so_cred->cr_uidinfo, &ssb->ssb_hiwat, cc,
		       rl ? rl->rlim_cur : RLIM_INFINITY)) {
		return (0);
	}
	if (rl)
		ssb->ssb_mbmax = min(cc * sb_efficiency, sb_max);
	else
		ssb->ssb_mbmax = cc * sb_efficiency;

	/*
	 * AUTOLOWAT is set on send buffers and prevents large writes
	 * from generating a huge number of context switches.
	 */
	if (ssb->ssb_flags & SSB_AUTOLOWAT) {
		ssb->ssb_lowat = ssb->ssb_hiwat / 2;
		if (ssb->ssb_lowat < MCLBYTES)
			ssb->ssb_lowat = MCLBYTES;
	}
	if (ssb->ssb_lowat > ssb->ssb_hiwat)
		ssb->ssb_lowat = ssb->ssb_hiwat;
	return (1);
}

/*
 * Free mbufs held by a socket, and reserved mbuf space.
 */
void
ssb_release(struct signalsockbuf *ssb, struct socket *so)
{
	sbflush(&ssb->sb);
	(void)chgsbsize(so->so_cred->cr_uidinfo, &ssb->ssb_hiwat, 0,
	    RLIM_INFINITY);
	ssb->ssb_mbmax = 0;
}

/*
 * Some routines that return EOPNOTSUPP for entry points that are not
 * supported by a protocol.  Fill in as needed.
 */
void
pr_generic_notsupp(netmsg_t msg)
{
	lwkt_replymsg(&msg->lmsg, EOPNOTSUPP);
}

int
pru_sosend_notsupp(struct socket *so, struct sockaddr *addr, struct uio *uio,
	   struct mbuf *top, struct mbuf *control, int flags,
	   struct thread *td)
{
	if (top)
		m_freem(top);
	if (control)
		m_freem(control);
	return (EOPNOTSUPP);
}

int
pru_soreceive_notsupp(struct socket *so, struct sockaddr **paddr,
		      struct uio *uio, struct sockbuf *sio,
		      struct mbuf **controlp, int *flagsp)
{
	return (EOPNOTSUPP);
}

/*
 * This isn't really a ``null'' operation, but it's the default one
 * and doesn't do anything destructive.
 */
void
pru_sense_null(netmsg_t msg)
{
	msg->sense.nm_stat->st_blksize = msg->base.nm_so->so_snd.ssb_hiwat;
	lwkt_replymsg(&msg->lmsg, 0);
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

	sa2 = kmalloc(sa->sa_len, M_SONAME, M_INTWAIT);
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
	ssbtoxsockbuf(&so->so_snd, &xso->so_snd);
	ssbtoxsockbuf(&so->so_rcv, &xso->so_rcv);
	xso->so_uid = so->so_cred->cr_uid;
}

/*
 * Here is the definition of some of the basic objects in the kern.ipc
 * branch of the MIB.
 */
SYSCTL_NODE(_kern, KERN_IPC, ipc, CTLFLAG_RW, 0, "IPC");

/*
 * This takes the place of kern.maxsockbuf, which moved to kern.ipc.
 *
 * NOTE! sb_max only applies to user-created socket buffers.
 */
static int dummy;
SYSCTL_INT(_kern, KERN_DUMMY, dummy, CTLFLAG_RW, &dummy, 0, "");
SYSCTL_OID(_kern_ipc, KIPC_MAXSOCKBUF, maxsockbuf, CTLTYPE_INT|CTLFLAG_RW, 
    &sb_max, 0, sysctl_handle_sb_max, "I", "Maximum socket buffer size");
SYSCTL_INT(_kern_ipc, OID_AUTO, maxsockets, CTLFLAG_RD, 
    &maxsockets, 0, "Maximum number of sockets available");
SYSCTL_INT(_kern_ipc, KIPC_SOCKBUF_WASTE, sockbuf_waste_factor, CTLFLAG_RW,
    &sb_efficiency, 0,
    "Socket buffer limit scaler");

/*
 * Initialize maxsockets 
 */
static void
init_maxsockets(void *ignored)
{
    TUNABLE_INT_FETCH("kern.ipc.maxsockets", &maxsockets);
    maxsockets = imax(maxsockets, imax(maxfiles, nmbclusters));
}
SYSINIT(param, SI_BOOT1_TUNABLES, SI_ORDER_ANY,
	init_maxsockets, NULL);

