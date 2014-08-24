/*
 * Copyright (c) 2004 Jeffrey M. Hsu.  All rights reserved.
 * Copyright (c) 2004 The DragonFly Project.  All rights reserved.
 * 
 * This code is derived from software contributed to The DragonFly Project
 * by Jeffrey M. Hsu.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of The DragonFly Project nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific, prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
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
 *	@(#)uipc_socket.c	8.3 (Berkeley) 4/15/94
 * $FreeBSD: src/sys/kern/uipc_socket.c,v 1.68.2.24 2003/11/11 17:18:18 silby Exp $
 */

#include "opt_inet.h"
#include "opt_sctp.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/fcntl.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/domain.h>
#include <sys/file.h>			/* for struct knote */
#include <sys/kernel.h>
#include <sys/event.h>
#include <sys/proc.h>
#include <sys/protosw.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/socketops.h>
#include <sys/resourcevar.h>
#include <sys/signalvar.h>
#include <sys/sysctl.h>
#include <sys/uio.h>
#include <sys/jail.h>
#include <vm/vm_zone.h>
#include <vm/pmap.h>
#include <net/netmsg2.h>
#include <net/netisr2.h>

#include <sys/thread2.h>
#include <sys/socketvar2.h>
#include <sys/spinlock2.h>

#include <machine/limits.h>

#ifdef INET
extern int tcp_sosend_agglim;
extern int tcp_sosend_async;
extern int tcp_sosend_jcluster;
extern int udp_sosend_async;
extern int udp_sosend_prepend;

static int	 do_setopt_accept_filter(struct socket *so, struct sockopt *sopt);
#endif /* INET */

static void 	filt_sordetach(struct knote *kn);
static int 	filt_soread(struct knote *kn, long hint);
static void 	filt_sowdetach(struct knote *kn);
static int	filt_sowrite(struct knote *kn, long hint);
static int	filt_solisten(struct knote *kn, long hint);

static void	sodiscard(struct socket *so);
static int	soclose_sync(struct socket *so, int fflag);
static void	soclose_fast(struct socket *so);

static struct filterops solisten_filtops = 
	{ FILTEROP_ISFD|FILTEROP_MPSAFE, NULL, filt_sordetach, filt_solisten };
static struct filterops soread_filtops =
	{ FILTEROP_ISFD|FILTEROP_MPSAFE, NULL, filt_sordetach, filt_soread };
static struct filterops sowrite_filtops = 
	{ FILTEROP_ISFD|FILTEROP_MPSAFE, NULL, filt_sowdetach, filt_sowrite };
static struct filterops soexcept_filtops =
	{ FILTEROP_ISFD|FILTEROP_MPSAFE, NULL, filt_sordetach, filt_soread };

MALLOC_DEFINE(M_SOCKET, "socket", "socket struct");
MALLOC_DEFINE(M_SONAME, "soname", "socket name");
MALLOC_DEFINE(M_PCB, "pcb", "protocol control block");


static int somaxconn = SOMAXCONN;
SYSCTL_INT(_kern_ipc, KIPC_SOMAXCONN, somaxconn, CTLFLAG_RW,
    &somaxconn, 0, "Maximum pending socket connection queue size");

static int use_soclose_fast = 1;
SYSCTL_INT(_kern_ipc, OID_AUTO, soclose_fast, CTLFLAG_RW,
    &use_soclose_fast, 0, "Fast socket close");

int use_soaccept_pred_fast = 1;
SYSCTL_INT(_kern_ipc, OID_AUTO, soaccept_pred_fast, CTLFLAG_RW,
    &use_soaccept_pred_fast, 0, "Fast socket accept predication");

int use_sendfile_async = 1;
SYSCTL_INT(_kern_ipc, OID_AUTO, sendfile_async, CTLFLAG_RW,
    &use_sendfile_async, 0, "sendfile uses asynchronized pru_send");

int use_soconnect_async = 1;
SYSCTL_INT(_kern_ipc, OID_AUTO, soconnect_async, CTLFLAG_RW,
    &use_soconnect_async, 0, "soconnect uses asynchronized pru_connect");

/*
 * Socket operation routines.
 * These routines are called by the routines in
 * sys_socket.c or from a system process, and
 * implement the semantics of socket operations by
 * switching out to the protocol specific routines.
 */

/*
 * Get a socket structure, and initialize it.
 * Note that it would probably be better to allocate socket
 * and PCB at the same time, but I'm not convinced that all
 * the protocols can be easily modified to do this.
 */
struct socket *
soalloc(int waitok, struct protosw *pr)
{
	struct socket *so;
	unsigned waitmask;

	waitmask = waitok ? M_WAITOK : M_NOWAIT;
	so = kmalloc(sizeof(struct socket), M_SOCKET, M_ZERO|waitmask);
	if (so) {
		/* XXX race condition for reentrant kernel */
		so->so_proto = pr;
		TAILQ_INIT(&so->so_aiojobq);
		TAILQ_INIT(&so->so_rcv.ssb_kq.ki_mlist);
		TAILQ_INIT(&so->so_snd.ssb_kq.ki_mlist);
		lwkt_token_init(&so->so_rcv.ssb_token, "rcvtok");
		lwkt_token_init(&so->so_snd.ssb_token, "sndtok");
		spin_init(&so->so_rcvd_spin, "soalloc");
		netmsg_init(&so->so_rcvd_msg.base, so, &netisr_adone_rport,
		    MSGF_DROPABLE | MSGF_PRIORITY,
		    so->so_proto->pr_usrreqs->pru_rcvd);
		so->so_rcvd_msg.nm_pru_flags |= PRUR_ASYNC;
		so->so_state = SS_NOFDREF;
		so->so_refs = 1;
	}
	return so;
}

int
socreate(int dom, struct socket **aso, int type,
	int proto, struct thread *td)
{
	struct proc *p = td->td_proc;
	struct protosw *prp;
	struct socket *so;
	struct pru_attach_info ai;
	int error;

	if (proto)
		prp = pffindproto(dom, proto, type);
	else
		prp = pffindtype(dom, type);

	if (prp == NULL || prp->pr_usrreqs->pru_attach == 0)
		return (EPROTONOSUPPORT);

	if (p->p_ucred->cr_prison && jail_socket_unixiproute_only &&
	    prp->pr_domain->dom_family != PF_LOCAL &&
	    prp->pr_domain->dom_family != PF_INET &&
	    prp->pr_domain->dom_family != PF_INET6 &&
	    prp->pr_domain->dom_family != PF_ROUTE) {
		return (EPROTONOSUPPORT);
	}

	if (prp->pr_type != type)
		return (EPROTOTYPE);
	so = soalloc(p != NULL, prp);
	if (so == NULL)
		return (ENOBUFS);

	/*
	 * Callers of socreate() presumably will connect up a descriptor
	 * and call soclose() if they cannot.  This represents our so_refs
	 * (which should be 1) from soalloc().
	 */
	soclrstate(so, SS_NOFDREF);

	/*
	 * Set a default port for protocol processing.  No action will occur
	 * on the socket on this port until an inpcb is attached to it and
	 * is able to match incoming packets, or until the socket becomes
	 * available to userland.
	 *
	 * We normally default the socket to the protocol thread on cpu 0,
	 * if protocol does not provide its own method to initialize the
	 * default port.
	 *
	 * If PR_SYNC_PORT is set (unix domain sockets) there is no protocol
	 * thread and all pr_*()/pru_*() calls are executed synchronously.
	 */
	if (prp->pr_flags & PR_SYNC_PORT)
		so->so_port = &netisr_sync_port;
	else if (prp->pr_initport != NULL)
		so->so_port = prp->pr_initport();
	else
		so->so_port = netisr_cpuport(0);

	TAILQ_INIT(&so->so_incomp);
	TAILQ_INIT(&so->so_comp);
	so->so_type = type;
	so->so_cred = crhold(p->p_ucred);
	ai.sb_rlimit = &p->p_rlimit[RLIMIT_SBSIZE];
	ai.p_ucred = p->p_ucred;
	ai.fd_rdir = p->p_fd->fd_rdir;

	/*
	 * Auto-sizing of socket buffers is managed by the protocols and
	 * the appropriate flags must be set in the pru_attach function.
	 */
	error = so_pru_attach(so, proto, &ai);
	if (error) {
		sosetstate(so, SS_NOFDREF);
		sofree(so);	/* from soalloc */
		return error;
	}

	/*
	 * NOTE: Returns referenced socket.
	 */
	*aso = so;
	return (0);
}

int
sobind(struct socket *so, struct sockaddr *nam, struct thread *td)
{
	int error;

	error = so_pru_bind(so, nam, td);
	return (error);
}

static void
sodealloc(struct socket *so)
{
	if (so->so_rcv.ssb_hiwat)
		(void)chgsbsize(so->so_cred->cr_uidinfo,
		    &so->so_rcv.ssb_hiwat, 0, RLIM_INFINITY);
	if (so->so_snd.ssb_hiwat)
		(void)chgsbsize(so->so_cred->cr_uidinfo,
		    &so->so_snd.ssb_hiwat, 0, RLIM_INFINITY);
#ifdef INET
	/* remove accept filter if present */
	if (so->so_accf != NULL)
		do_setopt_accept_filter(so, NULL);
#endif /* INET */
	crfree(so->so_cred);
	if (so->so_faddr != NULL)
		kfree(so->so_faddr, M_SONAME);
	kfree(so, M_SOCKET);
}

int
solisten(struct socket *so, int backlog, struct thread *td)
{
	int error;
#ifdef SCTP
	short oldopt, oldqlimit;
#endif /* SCTP */

	if (so->so_state & (SS_ISCONNECTED | SS_ISCONNECTING))
		return (EINVAL);

#ifdef SCTP
	oldopt = so->so_options;
	oldqlimit = so->so_qlimit;
#endif /* SCTP */

	lwkt_gettoken(&so->so_rcv.ssb_token);
	if (TAILQ_EMPTY(&so->so_comp))
		so->so_options |= SO_ACCEPTCONN;
	lwkt_reltoken(&so->so_rcv.ssb_token);
	if (backlog < 0 || backlog > somaxconn)
		backlog = somaxconn;
	so->so_qlimit = backlog;
	/* SCTP needs to look at tweak both the inbound backlog parameter AND
	 * the so_options (UDP model both connect's and gets inbound
	 * connections .. implicitly).
	 */
	error = so_pru_listen(so, td);
	if (error) {
#ifdef SCTP
		/* Restore the params */
		so->so_options = oldopt;
		so->so_qlimit = oldqlimit;
#endif /* SCTP */
		return (error);
	}
	return (0);
}

/*
 * Destroy a disconnected socket.  This routine is a NOP if entities
 * still have a reference on the socket:
 *
 *	so_pcb -	The protocol stack still has a reference
 *	SS_NOFDREF -	There is no longer a file pointer reference
 */
void
sofree(struct socket *so)
{
	struct socket *head;

	/*
	 * This is a bit hackish at the moment.  We need to interlock
	 * any accept queue we are on before we potentially lose the
	 * last reference to avoid races against a re-reference from
	 * someone operating on the queue.
	 */
	while ((head = so->so_head) != NULL) {
		lwkt_getpooltoken(head);
		if (so->so_head == head)
			break;
		lwkt_relpooltoken(head);
	}

	/*
	 * Arbitrage the last free.
	 */
	KKASSERT(so->so_refs > 0);
	if (atomic_fetchadd_int(&so->so_refs, -1) != 1) {
		if (head)
			lwkt_relpooltoken(head);
		return;
	}

	KKASSERT(so->so_pcb == NULL && (so->so_state & SS_NOFDREF));
	KKASSERT((so->so_state & SS_ASSERTINPROG) == 0);

	/*
	 * We're done, remove ourselves from the accept queue we are
	 * on, if we are on one.
	 */
	if (head != NULL) {
		if (so->so_state & SS_INCOMP) {
			TAILQ_REMOVE(&head->so_incomp, so, so_list);
			head->so_incqlen--;
		} else if (so->so_state & SS_COMP) {
			/*
			 * We must not decommission a socket that's
			 * on the accept(2) queue.  If we do, then
			 * accept(2) may hang after select(2) indicated
			 * that the listening socket was ready.
			 */
			lwkt_relpooltoken(head);
			return;
		} else {
			panic("sofree: not queued");
		}
		soclrstate(so, SS_INCOMP);
		so->so_head = NULL;
		lwkt_relpooltoken(head);
	}
	ssb_release(&so->so_snd, so);
	sorflush(so);
	sodealloc(so);
}

/*
 * Close a socket on last file table reference removal.
 * Initiate disconnect if connected.
 * Free socket when disconnect complete.
 */
int
soclose(struct socket *so, int fflag)
{
	int error;

	funsetown(&so->so_sigio);
	if (!use_soclose_fast ||
	    (so->so_proto->pr_flags & PR_SYNC_PORT) ||
	    ((so->so_state & SS_ISCONNECTED) &&
	     (so->so_options & SO_LINGER))) {
		error = soclose_sync(so, fflag);
	} else {
		soclose_fast(so);
		error = 0;
	}
	return error;
}

static void
sodiscard(struct socket *so)
{
	lwkt_getpooltoken(so);
	if (so->so_options & SO_ACCEPTCONN) {
		struct socket *sp;

		while ((sp = TAILQ_FIRST(&so->so_incomp)) != NULL) {
			TAILQ_REMOVE(&so->so_incomp, sp, so_list);
			soclrstate(sp, SS_INCOMP);
			sp->so_head = NULL;
			so->so_incqlen--;
			soabort_async(sp);
		}
		while ((sp = TAILQ_FIRST(&so->so_comp)) != NULL) {
			TAILQ_REMOVE(&so->so_comp, sp, so_list);
			soclrstate(sp, SS_COMP);
			sp->so_head = NULL;
			so->so_qlen--;
			soabort_async(sp);
		}
	}
	lwkt_relpooltoken(so);

	if (so->so_state & SS_NOFDREF)
		panic("soclose: NOFDREF");
	sosetstate(so, SS_NOFDREF);	/* take ref */
}

void
soinherit(struct socket *so, struct socket *so_inh)
{
	TAILQ_HEAD(, socket) comp, incomp;
	struct socket *sp;
	int qlen, incqlen;

	KASSERT(so->so_options & SO_ACCEPTCONN,
	    ("so does not accept connection"));
	KASSERT(so_inh->so_options & SO_ACCEPTCONN,
	    ("so_inh does not accept connection"));

	TAILQ_INIT(&comp);
	TAILQ_INIT(&incomp);

	lwkt_getpooltoken(so);
	lwkt_getpooltoken(so_inh);

	/*
	 * Save completed queue and incompleted queue
	 */
	TAILQ_CONCAT(&comp, &so->so_comp, so_list);
	qlen = so->so_qlen;
	so->so_qlen = 0;

	TAILQ_CONCAT(&incomp, &so->so_incomp, so_list);
	incqlen = so->so_incqlen;
	so->so_incqlen = 0;

	/*
	 * Append the saved completed queue and incompleted
	 * queue to the socket inherits them.
	 *
	 * XXX
	 * This may temporarily break the inheriting socket's
	 * so_qlimit.
	 */
	TAILQ_FOREACH(sp, &comp, so_list) {
		sp->so_head = so_inh;
		crfree(sp->so_cred);
		sp->so_cred = crhold(so_inh->so_cred);
	}

	TAILQ_FOREACH(sp, &incomp, so_list) {
		sp->so_head = so_inh;
		crfree(sp->so_cred);
		sp->so_cred = crhold(so_inh->so_cred);
	}

	TAILQ_CONCAT(&so_inh->so_comp, &comp, so_list);
	so_inh->so_qlen += qlen;

	TAILQ_CONCAT(&so_inh->so_incomp, &incomp, so_list);
	so_inh->so_incqlen += incqlen;

	lwkt_relpooltoken(so_inh);
	lwkt_relpooltoken(so);

	if (qlen) {
		/*
		 * "New" connections have arrived
		 */
		sorwakeup(so_inh);
		wakeup(&so_inh->so_timeo);
	}
}

static int
soclose_sync(struct socket *so, int fflag)
{
	int error = 0;

	if (so->so_pcb == NULL)
		goto discard;
	if (so->so_state & SS_ISCONNECTED) {
		if ((so->so_state & SS_ISDISCONNECTING) == 0) {
			error = sodisconnect(so);
			if (error)
				goto drop;
		}
		if (so->so_options & SO_LINGER) {
			if ((so->so_state & SS_ISDISCONNECTING) &&
			    (fflag & FNONBLOCK))
				goto drop;
			while (so->so_state & SS_ISCONNECTED) {
				error = tsleep(&so->so_timeo, PCATCH,
					       "soclos", so->so_linger * hz);
				if (error)
					break;
			}
		}
	}
drop:
	if (so->so_pcb) {
		int error2;

		error2 = so_pru_detach(so);
		if (error == 0)
			error = error2;
	}
discard:
	sodiscard(so);
	so_pru_sync(so);	/* unpend async sending */
	sofree(so);		/* dispose of ref */

	return (error);
}

static void
soclose_sofree_async_handler(netmsg_t msg)
{
	sofree(msg->base.nm_so);
}

static void
soclose_sofree_async(struct socket *so)
{
	struct netmsg_base *base = &so->so_clomsg;

	netmsg_init(base, so, &netisr_apanic_rport, 0,
	    soclose_sofree_async_handler);
	lwkt_sendmsg(so->so_port, &base->lmsg);
}

static void
soclose_disconn_async_handler(netmsg_t msg)
{
	struct socket *so = msg->base.nm_so;

	if ((so->so_state & SS_ISCONNECTED) &&
	    (so->so_state & SS_ISDISCONNECTING) == 0)
		so_pru_disconnect_direct(so);

	if (so->so_pcb)
		so_pru_detach_direct(so);

	sodiscard(so);
	sofree(so);
}

static void
soclose_disconn_async(struct socket *so)
{
	struct netmsg_base *base = &so->so_clomsg;

	netmsg_init(base, so, &netisr_apanic_rport, 0,
	    soclose_disconn_async_handler);
	lwkt_sendmsg(so->so_port, &base->lmsg);
}

static void
soclose_detach_async_handler(netmsg_t msg)
{
	struct socket *so = msg->base.nm_so;

	if (so->so_pcb)
		so_pru_detach_direct(so);

	sodiscard(so);
	sofree(so);
}

static void
soclose_detach_async(struct socket *so)
{
	struct netmsg_base *base = &so->so_clomsg;

	netmsg_init(base, so, &netisr_apanic_rport, 0,
	    soclose_detach_async_handler);
	lwkt_sendmsg(so->so_port, &base->lmsg);
}

static void
soclose_fast(struct socket *so)
{
	if (so->so_pcb == NULL)
		goto discard;

	if ((so->so_state & SS_ISCONNECTED) &&
	    (so->so_state & SS_ISDISCONNECTING) == 0) {
		soclose_disconn_async(so);
		return;
	}

	if (so->so_pcb) {
		soclose_detach_async(so);
		return;
	}

discard:
	sodiscard(so);
	soclose_sofree_async(so);
}

/*
 * Abort and destroy a socket.  Only one abort can be in progress
 * at any given moment.
 */
void
soabort(struct socket *so)
{
	soreference(so);
	so_pru_abort(so);
}

void
soabort_async(struct socket *so)
{
	soreference(so);
	so_pru_abort_async(so);
}

void
soabort_oncpu(struct socket *so)
{
	soreference(so);
	so_pru_abort_direct(so);
}

/*
 * so is passed in ref'd, which becomes owned by
 * the cleared SS_NOFDREF flag.
 */
void
soaccept_generic(struct socket *so)
{
	if ((so->so_state & SS_NOFDREF) == 0)
		panic("soaccept: !NOFDREF");
	soclrstate(so, SS_NOFDREF);	/* owned by lack of SS_NOFDREF */
}

int
soaccept(struct socket *so, struct sockaddr **nam)
{
	int error;

	soaccept_generic(so);
	error = so_pru_accept(so, nam);
	return (error);
}

int
soconnect(struct socket *so, struct sockaddr *nam, struct thread *td,
    boolean_t sync)
{
	int error;

	if (so->so_options & SO_ACCEPTCONN)
		return (EOPNOTSUPP);
	/*
	 * If protocol is connection-based, can only connect once.
	 * Otherwise, if connected, try to disconnect first.
	 * This allows user to disconnect by connecting to, e.g.,
	 * a null address.
	 */
	if (so->so_state & (SS_ISCONNECTED|SS_ISCONNECTING) &&
	    ((so->so_proto->pr_flags & PR_CONNREQUIRED) ||
	    (error = sodisconnect(so)))) {
		error = EISCONN;
	} else {
		/*
		 * Prevent accumulated error from previous connection
		 * from biting us.
		 */
		so->so_error = 0;
		if (!sync && so->so_proto->pr_usrreqs->pru_preconnect)
			error = so_pru_connect_async(so, nam, td);
		else
			error = so_pru_connect(so, nam, td);
	}
	return (error);
}

int
soconnect2(struct socket *so1, struct socket *so2)
{
	int error;

	error = so_pru_connect2(so1, so2);
	return (error);
}

int
sodisconnect(struct socket *so)
{
	int error;

	if ((so->so_state & SS_ISCONNECTED) == 0) {
		error = ENOTCONN;
		goto bad;
	}
	if (so->so_state & SS_ISDISCONNECTING) {
		error = EALREADY;
		goto bad;
	}
	error = so_pru_disconnect(so);
bad:
	return (error);
}

#define	SBLOCKWAIT(f)	(((f) & MSG_DONTWAIT) ? M_NOWAIT : M_WAITOK)
/*
 * Send on a socket.
 * If send must go all at once and message is larger than
 * send buffering, then hard error.
 * Lock against other senders.
 * If must go all at once and not enough room now, then
 * inform user that this would block and do nothing.
 * Otherwise, if nonblocking, send as much as possible.
 * The data to be sent is described by "uio" if nonzero,
 * otherwise by the mbuf chain "top" (which must be null
 * if uio is not).  Data provided in mbuf chain must be small
 * enough to send all at once.
 *
 * Returns nonzero on error, timeout or signal; callers
 * must check for short counts if EINTR/ERESTART are returned.
 * Data and control buffers are freed on return.
 */
int
sosend(struct socket *so, struct sockaddr *addr, struct uio *uio,
	struct mbuf *top, struct mbuf *control, int flags,
	struct thread *td)
{
	struct mbuf **mp;
	struct mbuf *m;
	size_t resid;
	int space, len;
	int clen = 0, error, dontroute, mlen;
	int atomic = sosendallatonce(so) || top;
	int pru_flags;

	if (uio) {
		resid = uio->uio_resid;
	} else {
		resid = (size_t)top->m_pkthdr.len;
#ifdef INVARIANTS
		len = 0;
		for (m = top; m; m = m->m_next)
			len += m->m_len;
		KKASSERT(top->m_pkthdr.len == len);
#endif
	}

	/*
	 * WARNING!  resid is unsigned, space and len are signed.  space
	 * 	     can wind up negative if the sockbuf is overcommitted.
	 *
	 * Also check to make sure that MSG_EOR isn't used on SOCK_STREAM
	 * type sockets since that's an error.
	 */
	if (so->so_type == SOCK_STREAM && (flags & MSG_EOR)) {
		error = EINVAL;
		goto out;
	}

	dontroute =
	    (flags & MSG_DONTROUTE) && (so->so_options & SO_DONTROUTE) == 0 &&
	    (so->so_proto->pr_flags & PR_ATOMIC);
	if (td->td_lwp != NULL)
		td->td_lwp->lwp_ru.ru_msgsnd++;
	if (control)
		clen = control->m_len;
#define	gotoerr(errcode)	{ error = errcode; goto release; }

restart:
	error = ssb_lock(&so->so_snd, SBLOCKWAIT(flags));
	if (error)
		goto out;

	do {
		if (so->so_state & SS_CANTSENDMORE)
			gotoerr(EPIPE);
		if (so->so_error) {
			error = so->so_error;
			so->so_error = 0;
			goto release;
		}
		if ((so->so_state & SS_ISCONNECTED) == 0) {
			/*
			 * `sendto' and `sendmsg' is allowed on a connection-
			 * based socket if it supports implied connect.
			 * Return ENOTCONN if not connected and no address is
			 * supplied.
			 */
			if ((so->so_proto->pr_flags & PR_CONNREQUIRED) &&
			    (so->so_proto->pr_flags & PR_IMPLOPCL) == 0) {
				if ((so->so_state & SS_ISCONFIRMING) == 0 &&
				    !(resid == 0 && clen != 0))
					gotoerr(ENOTCONN);
			} else if (addr == NULL)
			    gotoerr(so->so_proto->pr_flags & PR_CONNREQUIRED ?
				   ENOTCONN : EDESTADDRREQ);
		}
		if ((atomic && resid > so->so_snd.ssb_hiwat) ||
		    clen > so->so_snd.ssb_hiwat) {
			gotoerr(EMSGSIZE);
		}
		space = ssb_space(&so->so_snd);
		if (flags & MSG_OOB)
			space += 1024;
		if ((space < 0 || (size_t)space < resid + clen) && uio &&
		    (atomic || space < so->so_snd.ssb_lowat || space < clen)) {
			if (flags & (MSG_FNONBLOCKING|MSG_DONTWAIT))
				gotoerr(EWOULDBLOCK);
			ssb_unlock(&so->so_snd);
			error = ssb_wait(&so->so_snd);
			if (error)
				goto out;
			goto restart;
		}
		mp = &top;
		space -= clen;
		do {
		    if (uio == NULL) {
			/*
			 * Data is prepackaged in "top".
			 */
			resid = 0;
			if (flags & MSG_EOR)
				top->m_flags |= M_EOR;
		    } else do {
			if (resid > INT_MAX)
				resid = INT_MAX;
			m = m_getl((int)resid, MB_WAIT, MT_DATA,
				   top == NULL ? M_PKTHDR : 0, &mlen);
			if (top == NULL) {
				m->m_pkthdr.len = 0;
				m->m_pkthdr.rcvif = NULL;
			}
			len = imin((int)szmin(mlen, resid), space);
			if (resid < MINCLSIZE) {
				/*
				 * For datagram protocols, leave room
				 * for protocol headers in first mbuf.
				 */
				if (atomic && top == NULL && len < mlen)
					MH_ALIGN(m, len);
			}
			space -= len;
			error = uiomove(mtod(m, caddr_t), (size_t)len, uio);
			resid = uio->uio_resid;
			m->m_len = len;
			*mp = m;
			top->m_pkthdr.len += len;
			if (error)
				goto release;
			mp = &m->m_next;
			if (resid == 0) {
				if (flags & MSG_EOR)
					top->m_flags |= M_EOR;
				break;
			}
		    } while (space > 0 && atomic);
		    if (dontroute)
			    so->so_options |= SO_DONTROUTE;
		    if (flags & MSG_OOB) {
		    	    pru_flags = PRUS_OOB;
		    } else if ((flags & MSG_EOF) &&
		    	       (so->so_proto->pr_flags & PR_IMPLOPCL) &&
			       (resid == 0)) {
			    /*
			     * If the user set MSG_EOF, the protocol
			     * understands this flag and nothing left to
			     * send then use PRU_SEND_EOF instead of PRU_SEND.
			     */
		    	    pru_flags = PRUS_EOF;
		    } else if (resid > 0 && space > 0) {
			    /* If there is more to send, set PRUS_MORETOCOME */
		    	    pru_flags = PRUS_MORETOCOME;
		    } else {
		    	    pru_flags = 0;
		    }
		    /*
		     * XXX all the SS_CANTSENDMORE checks previously
		     * done could be out of date.  We could have recieved
		     * a reset packet in an interrupt or maybe we slept
		     * while doing page faults in uiomove() etc. We could
		     * probably recheck again inside the splnet() protection
		     * here, but there are probably other places that this
		     * also happens.  We must rethink this.
		     */
		    error = so_pru_send(so, pru_flags, top, addr, control, td);
		    if (dontroute)
			    so->so_options &= ~SO_DONTROUTE;
		    clen = 0;
		    control = NULL;
		    top = NULL;
		    mp = &top;
		    if (error)
			    goto release;
		} while (resid && space > 0);
	} while (resid);

release:
	ssb_unlock(&so->so_snd);
out:
	if (top)
		m_freem(top);
	if (control)
		m_freem(control);
	return (error);
}

#ifdef INET
/*
 * A specialization of sosend() for UDP based on protocol-specific knowledge:
 *   so->so_proto->pr_flags has the PR_ATOMIC field set.  This means that
 *	sosendallatonce() returns true,
 *	the "atomic" variable is true,
 *	and sosendudp() blocks until space is available for the entire send.
 *   so->so_proto->pr_flags does not have the PR_CONNREQUIRED or
 *	PR_IMPLOPCL flags set.
 *   UDP has no out-of-band data.
 *   UDP has no control data.
 *   UDP does not support MSG_EOR.
 */
int
sosendudp(struct socket *so, struct sockaddr *addr, struct uio *uio,
	  struct mbuf *top, struct mbuf *control, int flags, struct thread *td)
{
	size_t resid;
	int error, pru_flags = 0;
	int space;

	if (td->td_lwp != NULL)
		td->td_lwp->lwp_ru.ru_msgsnd++;
	if (control)
		m_freem(control);

	KASSERT((uio && !top) || (top && !uio), ("bad arguments to sosendudp"));
	resid = uio ? uio->uio_resid : (size_t)top->m_pkthdr.len;

restart:
	error = ssb_lock(&so->so_snd, SBLOCKWAIT(flags));
	if (error)
		goto out;

	if (so->so_state & SS_CANTSENDMORE)
		gotoerr(EPIPE);
	if (so->so_error) {
		error = so->so_error;
		so->so_error = 0;
		goto release;
	}
	if (!(so->so_state & SS_ISCONNECTED) && addr == NULL)
		gotoerr(EDESTADDRREQ);
	if (resid > so->so_snd.ssb_hiwat)
		gotoerr(EMSGSIZE);
	space = ssb_space(&so->so_snd);
	if (uio && (space < 0 || (size_t)space < resid)) {
		if (flags & (MSG_FNONBLOCKING|MSG_DONTWAIT))
			gotoerr(EWOULDBLOCK);
		ssb_unlock(&so->so_snd);
		error = ssb_wait(&so->so_snd);
		if (error)
			goto out;
		goto restart;
	}

	if (uio) {
		int hdrlen = max_hdr;

		/*
		 * We try to optimize out the additional mbuf
		 * allocations in M_PREPEND() on output path, e.g.
		 * - udp_output(), when it tries to prepend protocol
		 *   headers.
		 * - Link layer output function, when it tries to
		 *   prepend link layer header.
		 *
		 * This probably will not benefit any data that will
		 * be fragmented, so this optimization is only performed
		 * when the size of data and max size of protocol+link
		 * headers fit into one mbuf cluster.
		 */
		if (uio->uio_resid > MCLBYTES - hdrlen ||
		    !udp_sosend_prepend) {
			top = m_uiomove(uio);
			if (top == NULL)
				goto release;
		} else {
			int nsize;

			top = m_getl(uio->uio_resid + hdrlen, MB_WAIT,
			    MT_DATA, M_PKTHDR, &nsize);
			KASSERT(nsize >= uio->uio_resid + hdrlen,
			    ("sosendudp invalid nsize %d, "
			     "resid %zu, hdrlen %d",
			     nsize, uio->uio_resid, hdrlen));

			top->m_len = uio->uio_resid;
			top->m_pkthdr.len = uio->uio_resid;
			top->m_data += hdrlen;

			error = uiomove(mtod(top, caddr_t), top->m_len, uio);
			if (error)
				goto out;
		}
	}

	if (flags & MSG_DONTROUTE)
		pru_flags |= PRUS_DONTROUTE;

	if (udp_sosend_async && (flags & MSG_SYNC) == 0) {
		so_pru_send_async(so, pru_flags, top, addr, NULL, td);
		error = 0;
	} else {
		error = so_pru_send(so, pru_flags, top, addr, NULL, td);
	}
	top = NULL;		/* sent or freed in lower layer */

release:
	ssb_unlock(&so->so_snd);
out:
	if (top)
		m_freem(top);
	return (error);
}

int
sosendtcp(struct socket *so, struct sockaddr *addr, struct uio *uio,
	struct mbuf *top, struct mbuf *control, int flags,
	struct thread *td)
{
	struct mbuf **mp;
	struct mbuf *m;
	size_t resid;
	int space, len;
	int error, mlen;
	int allatonce;
	int pru_flags;

	if (uio) {
		KKASSERT(top == NULL);
		allatonce = 0;
		resid = uio->uio_resid;
	} else {
		allatonce = 1;
		resid = (size_t)top->m_pkthdr.len;
#ifdef INVARIANTS
		len = 0;
		for (m = top; m; m = m->m_next)
			len += m->m_len;
		KKASSERT(top->m_pkthdr.len == len);
#endif
	}

	/*
	 * WARNING!  resid is unsigned, space and len are signed.  space
	 * 	     can wind up negative if the sockbuf is overcommitted.
	 *
	 * Also check to make sure that MSG_EOR isn't used on TCP
	 */
	if (flags & MSG_EOR) {
		error = EINVAL;
		goto out;
	}

	if (control) {
		/* TCP doesn't do control messages (rights, creds, etc) */
		if (control->m_len) {
			error = EINVAL;
			goto out;
		}
		m_freem(control);	/* empty control, just free it */
		control = NULL;
	}

	if (td->td_lwp != NULL)
		td->td_lwp->lwp_ru.ru_msgsnd++;

#define	gotoerr(errcode)	{ error = errcode; goto release; }

restart:
	error = ssb_lock(&so->so_snd, SBLOCKWAIT(flags));
	if (error)
		goto out;

	do {
		if (so->so_state & SS_CANTSENDMORE)
			gotoerr(EPIPE);
		if (so->so_error) {
			error = so->so_error;
			so->so_error = 0;
			goto release;
		}
		if ((so->so_state & SS_ISCONNECTED) == 0 &&
		    (so->so_state & SS_ISCONFIRMING) == 0)
			gotoerr(ENOTCONN);
		if (allatonce && resid > so->so_snd.ssb_hiwat)
			gotoerr(EMSGSIZE);

		space = ssb_space_prealloc(&so->so_snd);
		if (flags & MSG_OOB)
			space += 1024;
		if ((space < 0 || (size_t)space < resid) && !allatonce &&
		    space < so->so_snd.ssb_lowat) {
			if (flags & (MSG_FNONBLOCKING|MSG_DONTWAIT))
				gotoerr(EWOULDBLOCK);
			ssb_unlock(&so->so_snd);
			error = ssb_wait(&so->so_snd);
			if (error)
				goto out;
			goto restart;
		}
		mp = &top;
		do {
		    int cnt = 0, async = 0;

		    if (uio == NULL) {
			/*
			 * Data is prepackaged in "top".
			 */
			resid = 0;
		    } else do {
			if (resid > INT_MAX)
				resid = INT_MAX;
			if (tcp_sosend_jcluster) {
				m = m_getlj((int)resid, MB_WAIT, MT_DATA,
					   top == NULL ? M_PKTHDR : 0, &mlen);
			} else {
				m = m_getl((int)resid, MB_WAIT, MT_DATA,
					   top == NULL ? M_PKTHDR : 0, &mlen);
			}
			if (top == NULL) {
				m->m_pkthdr.len = 0;
				m->m_pkthdr.rcvif = NULL;
			}
			len = imin((int)szmin(mlen, resid), space);
			space -= len;
			error = uiomove(mtod(m, caddr_t), (size_t)len, uio);
			resid = uio->uio_resid;
			m->m_len = len;
			*mp = m;
			top->m_pkthdr.len += len;
			if (error)
				goto release;
			mp = &m->m_next;
			if (resid == 0)
				break;
			++cnt;
		    } while (space > 0 && cnt < tcp_sosend_agglim);

		    if (tcp_sosend_async)
			    async = 1;

		    if (flags & MSG_OOB) {
		    	    pru_flags = PRUS_OOB;
			    async = 0;
		    } else if ((flags & MSG_EOF) && resid == 0) {
			    pru_flags = PRUS_EOF;
		    } else if (resid > 0 && space > 0) {
			    /* If there is more to send, set PRUS_MORETOCOME */
		    	    pru_flags = PRUS_MORETOCOME;
			    async = 1;
		    } else {
		    	    pru_flags = 0;
		    }

		    if (flags & MSG_SYNC)
			    async = 0;

		    /*
		     * XXX all the SS_CANTSENDMORE checks previously
		     * done could be out of date.  We could have recieved
		     * a reset packet in an interrupt or maybe we slept
		     * while doing page faults in uiomove() etc. We could
		     * probably recheck again inside the splnet() protection
		     * here, but there are probably other places that this
		     * also happens.  We must rethink this.
		     */
		    for (m = top; m; m = m->m_next)
			    ssb_preallocstream(&so->so_snd, m);
		    if (!async) {
			    error = so_pru_send(so, pru_flags, top,
			        NULL, NULL, td);
		    } else {
			    so_pru_send_async(so, pru_flags, top,
			        NULL, NULL, td);
			    error = 0;
		    }

		    top = NULL;
		    mp = &top;
		    if (error)
			    goto release;
		} while (resid && space > 0);
	} while (resid);

release:
	ssb_unlock(&so->so_snd);
out:
	if (top)
		m_freem(top);
	if (control)
		m_freem(control);
	return (error);
}
#endif

/*
 * Implement receive operations on a socket.
 *
 * We depend on the way that records are added to the signalsockbuf
 * by sbappend*.  In particular, each record (mbufs linked through m_next)
 * must begin with an address if the protocol so specifies,
 * followed by an optional mbuf or mbufs containing ancillary data,
 * and then zero or more mbufs of data.
 *
 * Although the signalsockbuf is locked, new data may still be appended.
 * A token inside the ssb_lock deals with MP issues and still allows
 * the network to access the socket if we block in a uio.
 *
 * The caller may receive the data as a single mbuf chain by supplying
 * an mbuf **mp0 for use in returning the chain.  The uio is then used
 * only for the count in uio_resid.
 */
int
soreceive(struct socket *so, struct sockaddr **psa, struct uio *uio,
	  struct sockbuf *sio, struct mbuf **controlp, int *flagsp)
{
	struct mbuf *m, *n;
	struct mbuf *free_chain = NULL;
	int flags, len, error, offset;
	struct protosw *pr = so->so_proto;
	int moff, type = 0;
	size_t resid, orig_resid;

	if (uio)
		resid = uio->uio_resid;
	else
		resid = (size_t)(sio->sb_climit - sio->sb_cc);
	orig_resid = resid;

	if (psa)
		*psa = NULL;
	if (controlp)
		*controlp = NULL;
	if (flagsp)
		flags = *flagsp &~ MSG_EOR;
	else
		flags = 0;
	if (flags & MSG_OOB) {
		m = m_get(MB_WAIT, MT_DATA);
		if (m == NULL)
			return (ENOBUFS);
		error = so_pru_rcvoob(so, m, flags & MSG_PEEK);
		if (error)
			goto bad;
		if (sio) {
			do {
				sbappend(sio, m);
				KKASSERT(resid >= (size_t)m->m_len);
				resid -= (size_t)m->m_len;
			} while (resid > 0 && m);
		} else {
			do {
				uio->uio_resid = resid;
				error = uiomove(mtod(m, caddr_t),
						(int)szmin(resid, m->m_len),
						uio);
				resid = uio->uio_resid;
				m = m_free(m);
			} while (uio->uio_resid && error == 0 && m);
		}
bad:
		if (m)
			m_freem(m);
		return (error);
	}
	if ((so->so_state & SS_ISCONFIRMING) && resid)
		so_pru_rcvd(so, 0);

	/*
	 * The token interlocks against the protocol thread while
	 * ssb_lock is a blocking lock against other userland entities.
	 */
	lwkt_gettoken(&so->so_rcv.ssb_token);
restart:
	error = ssb_lock(&so->so_rcv, SBLOCKWAIT(flags));
	if (error)
		goto done;

	m = so->so_rcv.ssb_mb;
	/*
	 * If we have less data than requested, block awaiting more
	 * (subject to any timeout) if:
	 *   1. the current count is less than the low water mark, or
	 *   2. MSG_WAITALL is set, and it is possible to do the entire
	 *	receive operation at once if we block (resid <= hiwat).
	 *   3. MSG_DONTWAIT is not set
	 * If MSG_WAITALL is set but resid is larger than the receive buffer,
	 * we have to do the receive in sections, and thus risk returning
	 * a short count if a timeout or signal occurs after we start.
	 */
	if (m == NULL || (((flags & MSG_DONTWAIT) == 0 &&
	    (size_t)so->so_rcv.ssb_cc < resid) &&
	    (so->so_rcv.ssb_cc < so->so_rcv.ssb_lowat ||
	    ((flags & MSG_WAITALL) && resid <= (size_t)so->so_rcv.ssb_hiwat)) &&
	    m->m_nextpkt == 0 && (pr->pr_flags & PR_ATOMIC) == 0)) {
		KASSERT(m != NULL || !so->so_rcv.ssb_cc, ("receive 1"));
		if (so->so_error) {
			if (m)
				goto dontblock;
			error = so->so_error;
			if ((flags & MSG_PEEK) == 0)
				so->so_error = 0;
			goto release;
		}
		if (so->so_state & SS_CANTRCVMORE) {
			if (m)
				goto dontblock;
			else
				goto release;
		}
		for (; m; m = m->m_next) {
			if (m->m_type == MT_OOBDATA  || (m->m_flags & M_EOR)) {
				m = so->so_rcv.ssb_mb;
				goto dontblock;
			}
		}
		if ((so->so_state & (SS_ISCONNECTED|SS_ISCONNECTING)) == 0 &&
		    (pr->pr_flags & PR_CONNREQUIRED)) {
			error = ENOTCONN;
			goto release;
		}
		if (resid == 0)
			goto release;
		if (flags & (MSG_FNONBLOCKING|MSG_DONTWAIT)) {
			error = EWOULDBLOCK;
			goto release;
		}
		ssb_unlock(&so->so_rcv);
		error = ssb_wait(&so->so_rcv);
		if (error)
			goto done;
		goto restart;
	}
dontblock:
	if (uio && uio->uio_td && uio->uio_td->td_proc)
		uio->uio_td->td_lwp->lwp_ru.ru_msgrcv++;

	/*
	 * note: m should be == sb_mb here.  Cache the next record while
	 * cleaning up.  Note that calling m_free*() will break out critical
	 * section.
	 */
	KKASSERT(m == so->so_rcv.ssb_mb);

	/*
	 * Skip any address mbufs prepending the record.
	 */
	if (pr->pr_flags & PR_ADDR) {
		KASSERT(m->m_type == MT_SONAME, ("receive 1a"));
		orig_resid = 0;
		if (psa)
			*psa = dup_sockaddr(mtod(m, struct sockaddr *));
		if (flags & MSG_PEEK)
			m = m->m_next;
		else
			m = sbunlinkmbuf(&so->so_rcv.sb, m, &free_chain);
	}

	/*
	 * Skip any control mbufs prepending the record.
	 */
#ifdef SCTP
	if (pr->pr_flags & PR_ADDR_OPT) {
		/*
		 * For SCTP we may be getting a
		 * whole message OR a partial delivery.
		 */
		if (m && m->m_type == MT_SONAME) {
			orig_resid = 0;
			if (psa)
				*psa = dup_sockaddr(mtod(m, struct sockaddr *));
			if (flags & MSG_PEEK)
				m = m->m_next;
			else
				m = sbunlinkmbuf(&so->so_rcv.sb, m, &free_chain);
		}
	}
#endif /* SCTP */
	while (m && m->m_type == MT_CONTROL && error == 0) {
		if (flags & MSG_PEEK) {
			if (controlp)
				*controlp = m_copy(m, 0, m->m_len);
			m = m->m_next;	/* XXX race */
		} else {
			if (controlp) {
				n = sbunlinkmbuf(&so->so_rcv.sb, m, NULL);
				if (pr->pr_domain->dom_externalize &&
				    mtod(m, struct cmsghdr *)->cmsg_type ==
				    SCM_RIGHTS)
				   error = (*pr->pr_domain->dom_externalize)(m);
				*controlp = m;
				m = n;
			} else {
				m = sbunlinkmbuf(&so->so_rcv.sb, m, &free_chain);
			}
		}
		if (controlp && *controlp) {
			orig_resid = 0;
			controlp = &(*controlp)->m_next;
		}
	}

	/*
	 * flag OOB data.
	 */
	if (m) {
		type = m->m_type;
		if (type == MT_OOBDATA)
			flags |= MSG_OOB;
	}

	/*
	 * Copy to the UIO or mbuf return chain (*mp).
	 */
	moff = 0;
	offset = 0;
	while (m && resid > 0 && error == 0) {
		if (m->m_type == MT_OOBDATA) {
			if (type != MT_OOBDATA)
				break;
		} else if (type == MT_OOBDATA)
			break;
		else
		    KASSERT(m->m_type == MT_DATA || m->m_type == MT_HEADER,
			("receive 3"));
		soclrstate(so, SS_RCVATMARK);
		len = (resid > INT_MAX) ? INT_MAX : resid;
		if (so->so_oobmark && len > so->so_oobmark - offset)
			len = so->so_oobmark - offset;
		if (len > m->m_len - moff)
			len = m->m_len - moff;

		/*
		 * Copy out to the UIO or pass the mbufs back to the SIO.
		 * The SIO is dealt with when we eat the mbuf, but deal
		 * with the resid here either way.
		 */
		if (uio) {
			uio->uio_resid = resid;
			error = uiomove(mtod(m, caddr_t) + moff, len, uio);
			resid = uio->uio_resid;
			if (error)
				goto release;
		} else {
			resid -= (size_t)len;
		}

		/*
		 * Eat the entire mbuf or just a piece of it
		 */
		if (len == m->m_len - moff) {
			if (m->m_flags & M_EOR)
				flags |= MSG_EOR;
#ifdef SCTP
			if (m->m_flags & M_NOTIFICATION)
				flags |= MSG_NOTIFICATION;
#endif /* SCTP */
			if (flags & MSG_PEEK) {
				m = m->m_next;
				moff = 0;
			} else {
				if (sio) {
					n = sbunlinkmbuf(&so->so_rcv.sb, m, NULL);
					sbappend(sio, m);
					m = n;
				} else {
					m = sbunlinkmbuf(&so->so_rcv.sb, m, &free_chain);
				}
			}
		} else {
			if (flags & MSG_PEEK) {
				moff += len;
			} else {
				if (sio) {
					n = m_copym(m, 0, len, MB_WAIT);
					if (n)
						sbappend(sio, n);
				}
				m->m_data += len;
				m->m_len -= len;
				so->so_rcv.ssb_cc -= len;
			}
		}
		if (so->so_oobmark) {
			if ((flags & MSG_PEEK) == 0) {
				so->so_oobmark -= len;
				if (so->so_oobmark == 0) {
					sosetstate(so, SS_RCVATMARK);
					break;
				}
			} else {
				offset += len;
				if (offset == so->so_oobmark)
					break;
			}
		}
		if (flags & MSG_EOR)
			break;
		/*
		 * If the MSG_WAITALL flag is set (for non-atomic socket),
		 * we must not quit until resid == 0 or an error
		 * termination.  If a signal/timeout occurs, return
		 * with a short count but without error.
		 * Keep signalsockbuf locked against other readers.
		 */
		while ((flags & MSG_WAITALL) && m == NULL && 
		       resid > 0 && !sosendallatonce(so) && 
		       so->so_rcv.ssb_mb == NULL) {
			if (so->so_error || so->so_state & SS_CANTRCVMORE)
				break;
			/*
			 * The window might have closed to zero, make
			 * sure we send an ack now that we've drained
			 * the buffer or we might end up blocking until
			 * the idle takes over (5 seconds).
			 */
			if (pr->pr_flags & PR_WANTRCVD && so->so_pcb)
				so_pru_rcvd(so, flags);
			error = ssb_wait(&so->so_rcv);
			if (error) {
				ssb_unlock(&so->so_rcv);
				error = 0;
				goto done;
			}
			m = so->so_rcv.ssb_mb;
		}
	}

	/*
	 * If an atomic read was requested but unread data still remains
	 * in the record, set MSG_TRUNC.
	 */
	if (m && pr->pr_flags & PR_ATOMIC)
		flags |= MSG_TRUNC;

	/*
	 * Cleanup.  If an atomic read was requested drop any unread data.
	 */
	if ((flags & MSG_PEEK) == 0) {
		if (m && (pr->pr_flags & PR_ATOMIC))
			sbdroprecord(&so->so_rcv.sb);
		if ((pr->pr_flags & PR_WANTRCVD) && so->so_pcb)
			so_pru_rcvd(so, flags);
	}

	if (orig_resid == resid && orig_resid &&
	    (flags & MSG_EOR) == 0 && (so->so_state & SS_CANTRCVMORE) == 0) {
		ssb_unlock(&so->so_rcv);
		goto restart;
	}

	if (flagsp)
		*flagsp |= flags;
release:
	ssb_unlock(&so->so_rcv);
done:
	lwkt_reltoken(&so->so_rcv.ssb_token);
	if (free_chain)
		m_freem(free_chain);
	return (error);
}

int
sorecvtcp(struct socket *so, struct sockaddr **psa, struct uio *uio,
	  struct sockbuf *sio, struct mbuf **controlp, int *flagsp)
{
	struct mbuf *m, *n;
	struct mbuf *free_chain = NULL;
	int flags, len, error, offset;
	struct protosw *pr = so->so_proto;
	int moff;
	int didoob;
	size_t resid, orig_resid, restmp;

	if (uio)
		resid = uio->uio_resid;
	else
		resid = (size_t)(sio->sb_climit - sio->sb_cc);
	orig_resid = resid;

	if (psa)
		*psa = NULL;
	if (controlp)
		*controlp = NULL;
	if (flagsp)
		flags = *flagsp &~ MSG_EOR;
	else
		flags = 0;
	if (flags & MSG_OOB) {
		m = m_get(MB_WAIT, MT_DATA);
		if (m == NULL)
			return (ENOBUFS);
		error = so_pru_rcvoob(so, m, flags & MSG_PEEK);
		if (error)
			goto bad;
		if (sio) {
			do {
				sbappend(sio, m);
				KKASSERT(resid >= (size_t)m->m_len);
				resid -= (size_t)m->m_len;
			} while (resid > 0 && m);
		} else {
			do {
				uio->uio_resid = resid;
				error = uiomove(mtod(m, caddr_t),
						(int)szmin(resid, m->m_len),
						uio);
				resid = uio->uio_resid;
				m = m_free(m);
			} while (uio->uio_resid && error == 0 && m);
		}
bad:
		if (m)
			m_freem(m);
		return (error);
	}

	/*
	 * The token interlocks against the protocol thread while
	 * ssb_lock is a blocking lock against other userland entities.
	 *
	 * Lock a limited number of mbufs (not all, so sbcompress() still
	 * works well).  The token is used as an interlock for sbwait() so
	 * release it afterwords.
	 */
restart:
	error = ssb_lock(&so->so_rcv, SBLOCKWAIT(flags));
	if (error)
		goto done;

	lwkt_gettoken(&so->so_rcv.ssb_token);
	m = so->so_rcv.ssb_mb;

	/*
	 * If we have less data than requested, block awaiting more
	 * (subject to any timeout) if:
	 *   1. the current count is less than the low water mark, or
	 *   2. MSG_WAITALL is set, and it is possible to do the entire
	 *	receive operation at once if we block (resid <= hiwat).
	 *   3. MSG_DONTWAIT is not set
	 * If MSG_WAITALL is set but resid is larger than the receive buffer,
	 * we have to do the receive in sections, and thus risk returning
	 * a short count if a timeout or signal occurs after we start.
	 */
	if (m == NULL || (((flags & MSG_DONTWAIT) == 0 &&
	    (size_t)so->so_rcv.ssb_cc < resid) &&
	    (so->so_rcv.ssb_cc < so->so_rcv.ssb_lowat ||
	   ((flags & MSG_WAITALL) && resid <= (size_t)so->so_rcv.ssb_hiwat)))) {
		KASSERT(m != NULL || !so->so_rcv.ssb_cc, ("receive 1"));
		if (so->so_error) {
			if (m)
				goto dontblock;
			lwkt_reltoken(&so->so_rcv.ssb_token);
			error = so->so_error;
			if ((flags & MSG_PEEK) == 0)
				so->so_error = 0;
			goto release;
		}
		if (so->so_state & SS_CANTRCVMORE) {
			if (m)
				goto dontblock;
			lwkt_reltoken(&so->so_rcv.ssb_token);
			goto release;
		}
		if ((so->so_state & (SS_ISCONNECTED|SS_ISCONNECTING)) == 0 &&
		    (pr->pr_flags & PR_CONNREQUIRED)) {
			lwkt_reltoken(&so->so_rcv.ssb_token);
			error = ENOTCONN;
			goto release;
		}
		if (resid == 0) {
			lwkt_reltoken(&so->so_rcv.ssb_token);
			goto release;
		}
		if (flags & (MSG_FNONBLOCKING|MSG_DONTWAIT)) {
			lwkt_reltoken(&so->so_rcv.ssb_token);
			error = EWOULDBLOCK;
			goto release;
		}
		ssb_unlock(&so->so_rcv);
		error = ssb_wait(&so->so_rcv);
		lwkt_reltoken(&so->so_rcv.ssb_token);
		if (error)
			goto done;
		goto restart;
	}

	/*
	 * Token still held
	 */
dontblock:
	n = m;
	restmp = 0;
	while (n && restmp < resid) {
		n->m_flags |= M_SOLOCKED;
		restmp += n->m_len;
		if (n->m_next == NULL)
			n = n->m_nextpkt;
		else
			n = n->m_next;
	}

	/*
	 * Release token for loop
	 */
	lwkt_reltoken(&so->so_rcv.ssb_token);
	if (uio && uio->uio_td && uio->uio_td->td_proc)
		uio->uio_td->td_lwp->lwp_ru.ru_msgrcv++;

	/*
	 * note: m should be == sb_mb here.  Cache the next record while
	 * cleaning up.  Note that calling m_free*() will break out critical
	 * section.
	 */
	KKASSERT(m == so->so_rcv.ssb_mb);

	/*
	 * Copy to the UIO or mbuf return chain (*mp).
	 *
	 * NOTE: Token is not held for loop
	 */
	moff = 0;
	offset = 0;
	didoob = 0;

	while (m && (m->m_flags & M_SOLOCKED) && resid > 0 && error == 0) {
		KASSERT(m->m_type == MT_DATA || m->m_type == MT_HEADER,
		    ("receive 3"));

		soclrstate(so, SS_RCVATMARK);
		len = (resid > INT_MAX) ? INT_MAX : resid;
		if (so->so_oobmark && len > so->so_oobmark - offset)
			len = so->so_oobmark - offset;
		if (len > m->m_len - moff)
			len = m->m_len - moff;

		/*
		 * Copy out to the UIO or pass the mbufs back to the SIO.
		 * The SIO is dealt with when we eat the mbuf, but deal
		 * with the resid here either way.
		 */
		if (uio) {
			uio->uio_resid = resid;
			error = uiomove(mtod(m, caddr_t) + moff, len, uio);
			resid = uio->uio_resid;
			if (error)
				goto release;
		} else {
			resid -= (size_t)len;
		}

		/*
		 * Eat the entire mbuf or just a piece of it
		 */
		offset += len;
		if (len == m->m_len - moff) {
			m = m->m_next;
			moff = 0;
		} else {
			moff += len;
		}

		/*
		 * Check oobmark
		 */
		if (so->so_oobmark && offset == so->so_oobmark) {
			didoob = 1;
			break;
		}
	}

	/*
	 * Synchronize sockbuf with data we read.
	 *
	 * NOTE: (m) is junk on entry (it could be left over from the
	 *	 previous loop).
	 */
	if ((flags & MSG_PEEK) == 0) {
		lwkt_gettoken(&so->so_rcv.ssb_token);
		m = so->so_rcv.ssb_mb;
		while (m && offset >= m->m_len) {
			if (so->so_oobmark) {
				so->so_oobmark -= m->m_len;
				if (so->so_oobmark == 0) {
					sosetstate(so, SS_RCVATMARK);
					didoob = 1;
				}
			}
			offset -= m->m_len;
			if (sio) {
				n = sbunlinkmbuf(&so->so_rcv.sb, m, NULL);
				sbappend(sio, m);
				m = n;
			} else {
				m = sbunlinkmbuf(&so->so_rcv.sb,
						 m, &free_chain);
			}
		}
		if (offset) {
			KKASSERT(m);
			if (sio) {
				n = m_copym(m, 0, offset, MB_WAIT);
				if (n)
					sbappend(sio, n);
			}
			m->m_data += offset;
			m->m_len -= offset;
			so->so_rcv.ssb_cc -= offset;
			if (so->so_oobmark) {
				so->so_oobmark -= offset;
				if (so->so_oobmark == 0) {
					sosetstate(so, SS_RCVATMARK);
					didoob = 1;
				}
			}
			offset = 0;
		}
		lwkt_reltoken(&so->so_rcv.ssb_token);
	}

	/*
	 * If the MSG_WAITALL flag is set (for non-atomic socket),
	 * we must not quit until resid == 0 or an error termination.
	 *
	 * If a signal/timeout occurs, return with a short count but without
	 * error.
	 *
	 * Keep signalsockbuf locked against other readers.
	 *
	 * XXX if MSG_PEEK we currently do quit.
	 */
	if ((flags & MSG_WAITALL) && !(flags & MSG_PEEK) &&
	    didoob == 0 && resid > 0 &&
	    !sosendallatonce(so)) {
		lwkt_gettoken(&so->so_rcv.ssb_token);
		error = 0;
		while ((m = so->so_rcv.ssb_mb) == NULL) {
			if (so->so_error || (so->so_state & SS_CANTRCVMORE)) {
				error = so->so_error;
				break;
			}
			/*
			 * The window might have closed to zero, make
			 * sure we send an ack now that we've drained
			 * the buffer or we might end up blocking until
			 * the idle takes over (5 seconds).
			 */
			if (so->so_pcb)
				so_pru_rcvd_async(so);
			if (so->so_rcv.ssb_mb == NULL)
				error = ssb_wait(&so->so_rcv);
			if (error) {
				lwkt_reltoken(&so->so_rcv.ssb_token);
				ssb_unlock(&so->so_rcv);
				error = 0;
				goto done;
			}
		}
		if (m && error == 0)
			goto dontblock;
		lwkt_reltoken(&so->so_rcv.ssb_token);
	}

	/*
	 * Token not held here.
	 *
	 * Cleanup.  If an atomic read was requested drop any unread data XXX
	 */
	if ((flags & MSG_PEEK) == 0) {
		if (so->so_pcb)
			so_pru_rcvd_async(so);
	}

	if (orig_resid == resid && orig_resid &&
	    (so->so_state & SS_CANTRCVMORE) == 0) {
		ssb_unlock(&so->so_rcv);
		goto restart;
	}

	if (flagsp)
		*flagsp |= flags;
release:
	ssb_unlock(&so->so_rcv);
done:
	if (free_chain)
		m_freem(free_chain);
	return (error);
}

/*
 * Shut a socket down.  Note that we do not get a frontend lock as we
 * want to be able to shut the socket down even if another thread is
 * blocked in a read(), thus waking it up.
 */
int
soshutdown(struct socket *so, int how)
{
	if (!(how == SHUT_RD || how == SHUT_WR || how == SHUT_RDWR))
		return (EINVAL);

	if (how != SHUT_WR) {
		/*ssb_lock(&so->so_rcv, M_WAITOK);*/
		sorflush(so);
		/*ssb_unlock(&so->so_rcv);*/
	}
	if (how != SHUT_RD)
		return (so_pru_shutdown(so));
	return (0);
}

void
sorflush(struct socket *so)
{
	struct signalsockbuf *ssb = &so->so_rcv;
	struct protosw *pr = so->so_proto;
	struct signalsockbuf asb;

	atomic_set_int(&ssb->ssb_flags, SSB_NOINTR);

	lwkt_gettoken(&ssb->ssb_token);
	socantrcvmore(so);
	asb = *ssb;

	/*
	 * Can't just blow up the ssb structure here
	 */
	bzero(&ssb->sb, sizeof(ssb->sb));
	ssb->ssb_timeo = 0;
	ssb->ssb_lowat = 0;
	ssb->ssb_hiwat = 0;
	ssb->ssb_mbmax = 0;
	atomic_clear_int(&ssb->ssb_flags, SSB_CLEAR_MASK);

	if ((pr->pr_flags & PR_RIGHTS) && pr->pr_domain->dom_dispose)
		(*pr->pr_domain->dom_dispose)(asb.ssb_mb);
	ssb_release(&asb, so);

	lwkt_reltoken(&ssb->ssb_token);
}

#ifdef INET
static int
do_setopt_accept_filter(struct socket *so, struct sockopt *sopt)
{
	struct accept_filter_arg	*afap = NULL;
	struct accept_filter	*afp;
	struct so_accf	*af = so->so_accf;
	int	error = 0;

	/* do not set/remove accept filters on non listen sockets */
	if ((so->so_options & SO_ACCEPTCONN) == 0) {
		error = EINVAL;
		goto out;
	}

	/* removing the filter */
	if (sopt == NULL) {
		if (af != NULL) {
			if (af->so_accept_filter != NULL && 
				af->so_accept_filter->accf_destroy != NULL) {
				af->so_accept_filter->accf_destroy(so);
			}
			if (af->so_accept_filter_str != NULL) {
				kfree(af->so_accept_filter_str, M_ACCF);
			}
			kfree(af, M_ACCF);
			so->so_accf = NULL;
		}
		so->so_options &= ~SO_ACCEPTFILTER;
		return (0);
	}
	/* adding a filter */
	/* must remove previous filter first */
	if (af != NULL) {
		error = EINVAL;
		goto out;
	}
	/* don't put large objects on the kernel stack */
	afap = kmalloc(sizeof(*afap), M_TEMP, M_WAITOK);
	error = sooptcopyin(sopt, afap, sizeof *afap, sizeof *afap);
	afap->af_name[sizeof(afap->af_name)-1] = '\0';
	afap->af_arg[sizeof(afap->af_arg)-1] = '\0';
	if (error)
		goto out;
	afp = accept_filt_get(afap->af_name);
	if (afp == NULL) {
		error = ENOENT;
		goto out;
	}
	af = kmalloc(sizeof(*af), M_ACCF, M_WAITOK | M_ZERO);
	if (afp->accf_create != NULL) {
		if (afap->af_name[0] != '\0') {
			int len = strlen(afap->af_name) + 1;

			af->so_accept_filter_str = kmalloc(len, M_ACCF,
							   M_WAITOK);
			strcpy(af->so_accept_filter_str, afap->af_name);
		}
		af->so_accept_filter_arg = afp->accf_create(so, afap->af_arg);
		if (af->so_accept_filter_arg == NULL) {
			kfree(af->so_accept_filter_str, M_ACCF);
			kfree(af, M_ACCF);
			so->so_accf = NULL;
			error = EINVAL;
			goto out;
		}
	}
	af->so_accept_filter = afp;
	so->so_accf = af;
	so->so_options |= SO_ACCEPTFILTER;
out:
	if (afap != NULL)
		kfree(afap, M_TEMP);
	return (error);
}
#endif /* INET */

/*
 * Perhaps this routine, and sooptcopyout(), below, ought to come in
 * an additional variant to handle the case where the option value needs
 * to be some kind of integer, but not a specific size.
 * In addition to their use here, these functions are also called by the
 * protocol-level pr_ctloutput() routines.
 */
int
sooptcopyin(struct sockopt *sopt, void *buf, size_t len, size_t minlen)
{
	return soopt_to_kbuf(sopt, buf, len, minlen);
}

int
soopt_to_kbuf(struct sockopt *sopt, void *buf, size_t len, size_t minlen)
{
	size_t	valsize;

	KKASSERT(!sopt->sopt_val || kva_p(sopt->sopt_val));
	KKASSERT(kva_p(buf));

	/*
	 * If the user gives us more than we wanted, we ignore it,
	 * but if we don't get the minimum length the caller
	 * wants, we return EINVAL.  On success, sopt->sopt_valsize
	 * is set to however much we actually retrieved.
	 */
	if ((valsize = sopt->sopt_valsize) < minlen)
		return EINVAL;
	if (valsize > len)
		sopt->sopt_valsize = valsize = len;

	bcopy(sopt->sopt_val, buf, valsize);
	return 0;
}


int
sosetopt(struct socket *so, struct sockopt *sopt)
{
	int	error, optval;
	struct	linger l;
	struct	timeval tv;
	u_long  val;
	struct signalsockbuf *sotmp;

	error = 0;
	sopt->sopt_dir = SOPT_SET;
	if (sopt->sopt_level != SOL_SOCKET) {
		if (so->so_proto && so->so_proto->pr_ctloutput) {
			return (so_pr_ctloutput(so, sopt));
		}
		error = ENOPROTOOPT;
	} else {
		switch (sopt->sopt_name) {
#ifdef INET
		case SO_ACCEPTFILTER:
			error = do_setopt_accept_filter(so, sopt);
			if (error)
				goto bad;
			break;
#endif /* INET */
		case SO_LINGER:
			error = sooptcopyin(sopt, &l, sizeof l, sizeof l);
			if (error)
				goto bad;

			so->so_linger = l.l_linger;
			if (l.l_onoff)
				so->so_options |= SO_LINGER;
			else
				so->so_options &= ~SO_LINGER;
			break;

		case SO_DEBUG:
		case SO_KEEPALIVE:
		case SO_DONTROUTE:
		case SO_USELOOPBACK:
		case SO_BROADCAST:
		case SO_REUSEADDR:
		case SO_REUSEPORT:
		case SO_OOBINLINE:
		case SO_TIMESTAMP:
		case SO_NOSIGPIPE:
			error = sooptcopyin(sopt, &optval, sizeof optval,
					    sizeof optval);
			if (error)
				goto bad;
			if (optval)
				so->so_options |= sopt->sopt_name;
			else
				so->so_options &= ~sopt->sopt_name;
			break;

		case SO_SNDBUF:
		case SO_RCVBUF:
		case SO_SNDLOWAT:
		case SO_RCVLOWAT:
			error = sooptcopyin(sopt, &optval, sizeof optval,
					    sizeof optval);
			if (error)
				goto bad;

			/*
			 * Values < 1 make no sense for any of these
			 * options, so disallow them.
			 */
			if (optval < 1) {
				error = EINVAL;
				goto bad;
			}

			switch (sopt->sopt_name) {
			case SO_SNDBUF:
			case SO_RCVBUF:
				if (ssb_reserve(sopt->sopt_name == SO_SNDBUF ?
				    &so->so_snd : &so->so_rcv, (u_long)optval,
				    so,
				    &curproc->p_rlimit[RLIMIT_SBSIZE]) == 0) {
					error = ENOBUFS;
					goto bad;
				}
				sotmp = (sopt->sopt_name == SO_SNDBUF) ?
						&so->so_snd : &so->so_rcv;
				atomic_clear_int(&sotmp->ssb_flags,
						 SSB_AUTOSIZE);
				break;

			/*
			 * Make sure the low-water is never greater than
			 * the high-water.
			 */
			case SO_SNDLOWAT:
				so->so_snd.ssb_lowat =
				    (optval > so->so_snd.ssb_hiwat) ?
				    so->so_snd.ssb_hiwat : optval;
				atomic_clear_int(&so->so_snd.ssb_flags,
						 SSB_AUTOLOWAT);
				break;
			case SO_RCVLOWAT:
				so->so_rcv.ssb_lowat =
				    (optval > so->so_rcv.ssb_hiwat) ?
				    so->so_rcv.ssb_hiwat : optval;
				atomic_clear_int(&so->so_rcv.ssb_flags,
						 SSB_AUTOLOWAT);
				break;
			}
			break;

		case SO_SNDTIMEO:
		case SO_RCVTIMEO:
			error = sooptcopyin(sopt, &tv, sizeof tv,
					    sizeof tv);
			if (error)
				goto bad;

			/* assert(hz > 0); */
			if (tv.tv_sec < 0 || tv.tv_sec > INT_MAX / hz ||
			    tv.tv_usec < 0 || tv.tv_usec >= 1000000) {
				error = EDOM;
				goto bad;
			}
			/* assert(tick > 0); */
			/* assert(ULONG_MAX - INT_MAX >= 1000000); */
			val = (u_long)(tv.tv_sec * hz) + tv.tv_usec / ustick;
			if (val > INT_MAX) {
				error = EDOM;
				goto bad;
			}
			if (val == 0 && tv.tv_usec != 0)
				val = 1;

			switch (sopt->sopt_name) {
			case SO_SNDTIMEO:
				so->so_snd.ssb_timeo = val;
				break;
			case SO_RCVTIMEO:
				so->so_rcv.ssb_timeo = val;
				break;
			}
			break;
		default:
			error = ENOPROTOOPT;
			break;
		}
		if (error == 0 && so->so_proto && so->so_proto->pr_ctloutput) {
			(void) so_pr_ctloutput(so, sopt);
		}
	}
bad:
	return (error);
}

/* Helper routine for getsockopt */
int
sooptcopyout(struct sockopt *sopt, const void *buf, size_t len)
{
	soopt_from_kbuf(sopt, buf, len);
	return 0;
}

void
soopt_from_kbuf(struct sockopt *sopt, const void *buf, size_t len)
{
	size_t	valsize;

	if (len == 0) {
		sopt->sopt_valsize = 0;
		return;
	}

	KKASSERT(!sopt->sopt_val || kva_p(sopt->sopt_val));
	KKASSERT(kva_p(buf));

	/*
	 * Documented get behavior is that we always return a value,
	 * possibly truncated to fit in the user's buffer.
	 * Traditional behavior is that we always tell the user
	 * precisely how much we copied, rather than something useful
	 * like the total amount we had available for her.
	 * Note that this interface is not idempotent; the entire answer must
	 * generated ahead of time.
	 */
	valsize = szmin(len, sopt->sopt_valsize);
	sopt->sopt_valsize = valsize;
	if (sopt->sopt_val != 0) {
		bcopy(buf, sopt->sopt_val, valsize);
	}
}

int
sogetopt(struct socket *so, struct sockopt *sopt)
{
	int	error, optval;
	long	optval_l;
	struct	linger l;
	struct	timeval tv;
#ifdef INET
	struct accept_filter_arg *afap;
#endif

	error = 0;
	sopt->sopt_dir = SOPT_GET;
	if (sopt->sopt_level != SOL_SOCKET) {
		if (so->so_proto && so->so_proto->pr_ctloutput) {
			return (so_pr_ctloutput(so, sopt));
		} else
			return (ENOPROTOOPT);
	} else {
		switch (sopt->sopt_name) {
#ifdef INET
		case SO_ACCEPTFILTER:
			if ((so->so_options & SO_ACCEPTCONN) == 0)
				return (EINVAL);
			afap = kmalloc(sizeof(*afap), M_TEMP,
				       M_WAITOK | M_ZERO);
			if ((so->so_options & SO_ACCEPTFILTER) != 0) {
				strcpy(afap->af_name, so->so_accf->so_accept_filter->accf_name);
				if (so->so_accf->so_accept_filter_str != NULL)
					strcpy(afap->af_arg, so->so_accf->so_accept_filter_str);
			}
			error = sooptcopyout(sopt, afap, sizeof(*afap));
			kfree(afap, M_TEMP);
			break;
#endif /* INET */
			
		case SO_LINGER:
			l.l_onoff = so->so_options & SO_LINGER;
			l.l_linger = so->so_linger;
			error = sooptcopyout(sopt, &l, sizeof l);
			break;

		case SO_USELOOPBACK:
		case SO_DONTROUTE:
		case SO_DEBUG:
		case SO_KEEPALIVE:
		case SO_REUSEADDR:
		case SO_REUSEPORT:
		case SO_BROADCAST:
		case SO_OOBINLINE:
		case SO_TIMESTAMP:
		case SO_NOSIGPIPE:
			optval = so->so_options & sopt->sopt_name;
integer:
			error = sooptcopyout(sopt, &optval, sizeof optval);
			break;

		case SO_TYPE:
			optval = so->so_type;
			goto integer;

		case SO_ERROR:
			optval = so->so_error;
			so->so_error = 0;
			goto integer;

		case SO_SNDBUF:
			optval = so->so_snd.ssb_hiwat;
			goto integer;

		case SO_RCVBUF:
			optval = so->so_rcv.ssb_hiwat;
			goto integer;

		case SO_SNDLOWAT:
			optval = so->so_snd.ssb_lowat;
			goto integer;

		case SO_RCVLOWAT:
			optval = so->so_rcv.ssb_lowat;
			goto integer;

		case SO_SNDTIMEO:
		case SO_RCVTIMEO:
			optval = (sopt->sopt_name == SO_SNDTIMEO ?
				  so->so_snd.ssb_timeo : so->so_rcv.ssb_timeo);

			tv.tv_sec = optval / hz;
			tv.tv_usec = (optval % hz) * ustick;
			error = sooptcopyout(sopt, &tv, sizeof tv);
			break;			

		case SO_SNDSPACE:
			optval_l = ssb_space(&so->so_snd);
			error = sooptcopyout(sopt, &optval_l, sizeof(optval_l));
			break;

		default:
			error = ENOPROTOOPT;
			break;
		}
		return (error);
	}
}

/* XXX; prepare mbuf for (__FreeBSD__ < 3) routines. */
int
soopt_getm(struct sockopt *sopt, struct mbuf **mp)
{
	struct mbuf *m, *m_prev;
	int sopt_size = sopt->sopt_valsize, msize;

	m = m_getl(sopt_size, sopt->sopt_td ? MB_WAIT : MB_DONTWAIT, MT_DATA,
		   0, &msize);
	if (m == NULL)
		return (ENOBUFS);
	m->m_len = min(msize, sopt_size);
	sopt_size -= m->m_len;
	*mp = m;
	m_prev = m;

	while (sopt_size > 0) {
		m = m_getl(sopt_size, sopt->sopt_td ? MB_WAIT : MB_DONTWAIT,
			   MT_DATA, 0, &msize);
		if (m == NULL) {
			m_freem(*mp);
			return (ENOBUFS);
		}
		m->m_len = min(msize, sopt_size);
		sopt_size -= m->m_len;
		m_prev->m_next = m;
		m_prev = m;
	}
	return (0);
}

/* XXX; copyin sopt data into mbuf chain for (__FreeBSD__ < 3) routines. */
int
soopt_mcopyin(struct sockopt *sopt, struct mbuf *m)
{
	soopt_to_mbuf(sopt, m);
	return 0;
}

void
soopt_to_mbuf(struct sockopt *sopt, struct mbuf *m)
{
	size_t valsize;
	void *val;

	KKASSERT(!sopt->sopt_val || kva_p(sopt->sopt_val));
	KKASSERT(kva_p(m));
	if (sopt->sopt_val == NULL)
		return;
	val = sopt->sopt_val;
	valsize = sopt->sopt_valsize;
	while (m != NULL && valsize >= m->m_len) {
		bcopy(val, mtod(m, char *), m->m_len);
		valsize -= m->m_len;
		val = (caddr_t)val + m->m_len;
		m = m->m_next;
	}
	if (m != NULL) /* should be allocated enoughly at ip6_sooptmcopyin() */
		panic("ip6_sooptmcopyin");
}

/* XXX; copyout mbuf chain data into soopt for (__FreeBSD__ < 3) routines. */
int
soopt_mcopyout(struct sockopt *sopt, struct mbuf *m)
{
	return soopt_from_mbuf(sopt, m);
}

int
soopt_from_mbuf(struct sockopt *sopt, struct mbuf *m)
{
	struct mbuf *m0 = m;
	size_t valsize = 0;
	size_t maxsize;
	void *val;

	KKASSERT(!sopt->sopt_val || kva_p(sopt->sopt_val));
	KKASSERT(kva_p(m));
	if (sopt->sopt_val == NULL)
		return 0;
	val = sopt->sopt_val;
	maxsize = sopt->sopt_valsize;
	while (m != NULL && maxsize >= m->m_len) {
		bcopy(mtod(m, char *), val, m->m_len);
	       maxsize -= m->m_len;
	       val = (caddr_t)val + m->m_len;
	       valsize += m->m_len;
	       m = m->m_next;
	}
	if (m != NULL) {
		/* enough soopt buffer should be given from user-land */
		m_freem(m0);
		return (EINVAL);
	}
	sopt->sopt_valsize = valsize;
	return 0;
}

void
sohasoutofband(struct socket *so)
{
	if (so->so_sigio != NULL)
		pgsigio(so->so_sigio, SIGURG, 0);
	KNOTE(&so->so_rcv.ssb_kq.ki_note, NOTE_OOB);
}

int
sokqfilter(struct file *fp, struct knote *kn)
{
	struct socket *so = (struct socket *)kn->kn_fp->f_data;
	struct signalsockbuf *ssb;

	switch (kn->kn_filter) {
	case EVFILT_READ:
		if (so->so_options & SO_ACCEPTCONN)
			kn->kn_fop = &solisten_filtops;
		else
			kn->kn_fop = &soread_filtops;
		ssb = &so->so_rcv;
		break;
	case EVFILT_WRITE:
		kn->kn_fop = &sowrite_filtops;
		ssb = &so->so_snd;
		break;
	case EVFILT_EXCEPT:
		kn->kn_fop = &soexcept_filtops;
		ssb = &so->so_rcv;
		break;
	default:
		return (EOPNOTSUPP);
	}

	knote_insert(&ssb->ssb_kq.ki_note, kn);
	atomic_set_int(&ssb->ssb_flags, SSB_KNOTE);
	return (0);
}

static void
filt_sordetach(struct knote *kn)
{
	struct socket *so = (struct socket *)kn->kn_fp->f_data;

	knote_remove(&so->so_rcv.ssb_kq.ki_note, kn);
	if (SLIST_EMPTY(&so->so_rcv.ssb_kq.ki_note))
		atomic_clear_int(&so->so_rcv.ssb_flags, SSB_KNOTE);
}

/*ARGSUSED*/
static int
filt_soread(struct knote *kn, long hint)
{
	struct socket *so = (struct socket *)kn->kn_fp->f_data;

	if (kn->kn_sfflags & NOTE_OOB) {
		if ((so->so_oobmark || (so->so_state & SS_RCVATMARK))) {
			kn->kn_fflags |= NOTE_OOB;
			return (1);
		}
		return (0);
	}
	kn->kn_data = so->so_rcv.ssb_cc;

	if (so->so_state & SS_CANTRCVMORE) {
		/*
		 * Only set NODATA if all data has been exhausted.
		 */
		if (kn->kn_data == 0)
			kn->kn_flags |= EV_NODATA;
		kn->kn_flags |= EV_EOF; 
		kn->kn_fflags = so->so_error;
		return (1);
	}
	if (so->so_error)	/* temporary udp error */
		return (1);
	if (kn->kn_sfflags & NOTE_LOWAT)
		return (kn->kn_data >= kn->kn_sdata);
	return ((kn->kn_data >= so->so_rcv.ssb_lowat) ||
		!TAILQ_EMPTY(&so->so_comp));
}

static void
filt_sowdetach(struct knote *kn)
{
	struct socket *so = (struct socket *)kn->kn_fp->f_data;

	knote_remove(&so->so_snd.ssb_kq.ki_note, kn);
	if (SLIST_EMPTY(&so->so_snd.ssb_kq.ki_note))
		atomic_clear_int(&so->so_snd.ssb_flags, SSB_KNOTE);
}

/*ARGSUSED*/
static int
filt_sowrite(struct knote *kn, long hint)
{
	struct socket *so = (struct socket *)kn->kn_fp->f_data;

	kn->kn_data = ssb_space(&so->so_snd);
	if (so->so_state & SS_CANTSENDMORE) {
		kn->kn_flags |= (EV_EOF | EV_NODATA);
		kn->kn_fflags = so->so_error;
		return (1);
	}
	if (so->so_error)	/* temporary udp error */
		return (1);
	if (((so->so_state & SS_ISCONNECTED) == 0) &&
	    (so->so_proto->pr_flags & PR_CONNREQUIRED))
		return (0);
	if (kn->kn_sfflags & NOTE_LOWAT)
		return (kn->kn_data >= kn->kn_sdata);
	return (kn->kn_data >= so->so_snd.ssb_lowat);
}

/*ARGSUSED*/
static int
filt_solisten(struct knote *kn, long hint)
{
	struct socket *so = (struct socket *)kn->kn_fp->f_data;

	kn->kn_data = so->so_qlen;
	return (! TAILQ_EMPTY(&so->so_comp));
}
