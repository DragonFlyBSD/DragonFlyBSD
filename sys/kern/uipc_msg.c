/*
 * Copyright (c) 2003, 2004 Jeffrey M. Hsu.  All rights reserved.
 * Copyright (c) 2003, 2004 The DragonFly Project.  All rights reserved.
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
 *
 * $DragonFly: src/sys/kern/uipc_msg.c,v 1.26 2008/10/27 02:56:30 sephe Exp $
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/msgport.h>
#include <sys/protosw.h>
#include <sys/mbuf.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/socketvar2.h>
#include <sys/socketops.h>
#include <sys/thread.h>
#include <sys/thread2.h>
#include <sys/msgport2.h>
#include <vm/pmap.h>
#include <net/netmsg2.h>

#include <net/netisr.h>
#include <net/netmsg.h>

static void netmsg_pru_abort(anynetmsg_t msg);
/*static void netmsg_pru_accept(anynetmsg_t msg);*/
static void netmsg_pru_attach(anynetmsg_t msg);
static void netmsg_pru_bind(anynetmsg_t msg);
static void netmsg_pru_connect(anynetmsg_t msg);
static void netmsg_pru_connect2(anynetmsg_t msg);
static void netmsg_pru_control(anynetmsg_t msg);
static void netmsg_pru_detach(anynetmsg_t msg);
static void netmsg_pru_disconnect(anynetmsg_t msg);
static void netmsg_pru_listen(anynetmsg_t msg);
static void netmsg_pru_peeraddr(anynetmsg_t msg);
static void netmsg_pru_rcvd(anynetmsg_t msg);
static void netmsg_pru_rcvoob(anynetmsg_t msg);
static void netmsg_pru_send(anynetmsg_t msg);
/*static void netmsg_pru_notify(anynetmsg_t msg);*/
static void netmsg_pru_sense(anynetmsg_t msg);
static void netmsg_pru_shutdown(anynetmsg_t msg);
static void netmsg_pru_sockaddr(anynetmsg_t msg);
static void netmsg_pru_poll(anynetmsg_t msg);
static void netmsg_pru_ctloutput(anynetmsg_t msg);
static void netmsg_pru_ctlinput(anynetmsg_t msg);

/*
 * Abort a socket and free it.  Called from soabort() only.
 *
 * The SS_ABORTING flag must already be set.
 */
void
so_pru_abort(struct socket *so)
{
	struct netmsg_pru_abort msg;
	lwkt_port_t port;

	KKASSERT(so->so_state & SS_ABORTING);
	port = so->so_proto->pr_mport(so, NULL, NULL, PRU_ABORT);
	netmsg_init(&msg.nm_netmsg, &curthread->td_msgport,
		    0, netmsg_pru_abort);
	msg.nm_prufn = so->so_proto->pr_usrreqs->pru_abort;
	msg.nm_so = so;
	(void)lwkt_domsg(port, &msg.nm_netmsg.nm_lmsg, 0);
}

/*
 * Abort a socket and free it, asynchronously.  Called from
 * soaborta() only.
 *
 * The SS_ABORTING flag must already be set.
 */
void
so_pru_aborta(struct socket *so)
{
	struct netmsg_pru_abort *msg;
	lwkt_port_t port;

	KKASSERT(so->so_state & SS_ABORTING);
	msg = kmalloc(sizeof(*msg), M_LWKTMSG, M_WAITOK | M_ZERO);
	port = so->so_proto->pr_mport(so, NULL, NULL, PRU_ABORT);
	netmsg_init(&msg->nm_netmsg, &netisr_afree_rport,
		    0, netmsg_pru_abort);
	msg->nm_prufn = so->so_proto->pr_usrreqs->pru_abort;
	msg->nm_so = so;
	lwkt_sendmsg(port, &msg->nm_netmsg.nm_lmsg);
}

int
so_pru_accept(struct socket *so, struct sockaddr **nam)
{
	int error;

	so_lock(so);
	/* Block (memory allocation) in process context. XXX JH */
	error = (*so->so_proto->pr_usrreqs->pru_accept)(so, nam);
	so_unlock(so);

	return error;

#ifdef notdef
	int error;
	struct netmsg_pru_accept msg;
	lwkt_port_t port;

	port = so->so_proto->pr_mport(so, NULL, NULL, PRU_ACCEPT);
	netmsg_init(&msg.nm_netmsg, &curthread->td_msgport, 0,
		    netmsg_pru_accept);
	msg.nm_prufn = so->so_proto->pr_usrreqs->pru_accept;
	msg.nm_so = so;
	msg.nm_nam = nam;
	error = lwkt_domsg(port, &msg.nm_netmsg.nm_lmsg, 0);
	return (error);
#endif
}

int
so_pru_attach(struct socket *so, int proto, struct pru_attach_info *ai)
{
	int error;
	struct netmsg_pru_attach msg;
	lwkt_port_t port;

	so_lock(so);
	port = so->so_proto->pr_mport(NULL, NULL, NULL, PRU_ATTACH);
	netmsg_init(&msg.nm_netmsg, &curthread->td_msgport, 0,
		    netmsg_pru_attach);
	msg.nm_prufn = so->so_proto->pr_usrreqs->pru_attach;
	msg.nm_so = so;
	msg.nm_proto = proto;
	msg.nm_ai = ai;
	error = lwkt_domsg(port, &msg.nm_netmsg.nm_lmsg, 0);
	so_unlock(so);
	return (error);
}

int
so_pru_bind(struct socket *so, struct sockaddr *nam, struct thread *td)
{
	int error;
	struct netmsg_pru_bind msg;
	lwkt_port_t port;

	so_lock(so);
	/* Send mesg to thread for new address. */
	port = so->so_proto->pr_mport(NULL, nam, NULL, PRU_BIND);
	netmsg_init(&msg.nm_netmsg, &curthread->td_msgport, 0,
		    netmsg_pru_bind);
	msg.nm_prufn = so->so_proto->pr_usrreqs->pru_bind;
	msg.nm_so = so;
	msg.nm_nam = nam;
	msg.nm_td = td;		/* used only for prison_ip() XXX JH */
	error = lwkt_domsg(port, &msg.nm_netmsg.nm_lmsg, 0);
	so_unlock(so);
	return (error);
}

int
so_pru_connect(struct socket *so, struct sockaddr *nam, struct thread *td)
{
	int error;
	struct netmsg_pru_connect msg;
	lwkt_port_t port;

	so_lock(so);
	port = so->so_proto->pr_mport(so, nam, NULL, PRU_CONNECT);
	netmsg_init(&msg.nm_netmsg, &curthread->td_msgport, 0,
		    netmsg_pru_connect);
	msg.nm_prufn = so->so_proto->pr_usrreqs->pru_connect;
	msg.nm_so = so;
	msg.nm_nam = nam;
	msg.nm_td = td;
	error = lwkt_domsg(port, &msg.nm_netmsg.nm_lmsg, 0);
	so_unlock(so);
	return (error);
}

int
so_pru_connect2(struct socket *so1, struct socket *so2)
{
	int error;
	struct netmsg_pru_connect2 msg;
	lwkt_port_t port;

	so_lock(so1);
	port = so1->so_proto->pr_mport(so1, NULL, NULL, PRU_CONNECT2);
	netmsg_init(&msg.nm_netmsg, &curthread->td_msgport, 0,
		    netmsg_pru_connect2);
	msg.nm_prufn = so1->so_proto->pr_usrreqs->pru_connect2;
	msg.nm_so1 = so1;
	msg.nm_so2 = so2;
	error = lwkt_domsg(port, &msg.nm_netmsg.nm_lmsg, 0);
	so_unlock(so1);
	return (error);
}

int
so_pru_control(struct socket *so, u_long cmd, caddr_t data, struct ifnet *ifp)
{
	int error;

	so_lock(so);
	error = (*so->so_proto->pr_usrreqs->pru_control)(so, cmd, data, ifp,
	    curthread);
	so_unlock(so);

	return error;
#ifdef gag	/* does copyin and copyout deep inside stack XXX JH */
	int error;
	struct netmsg_pru_control msg;
	lwkt_port_t port;

	port = so->so_proto->pr_mport(so, NULL, NULL, PRU_CONTROL);
	netmsg_init(&msg.nm_netmsg, &curthread->td_msgport, 0,
		    netmsg_pru_control);
	msg.nm_prufn = so->so_proto->pr_usrreqs->pru_control;
	msg.nm_so = so;
	msg.nm_cmd = cmd;
	msg.nm_data = data;
	msg.nm_ifp = ifp;
	msg.nm_td = td;
	error = lwkt_domsg(port, &msg.nm_netmsg.nm_lmsg, 0);
	return (error);
#endif
}

int
so_pru_detach(struct socket *so)
{
	int error;
	struct netmsg_pru_detach msg;
	lwkt_port_t port;

	so_lock(so);
	port = so->so_proto->pr_mport(so, NULL, NULL, PRU_DETACH);
	netmsg_init(&msg.nm_netmsg, &curthread->td_msgport, 0,
		    netmsg_pru_detach);
	msg.nm_prufn = so->so_proto->pr_usrreqs->pru_detach;
	msg.nm_so = so;
	error = lwkt_domsg(port, &msg.nm_netmsg.nm_lmsg, 0);
	so_unlock(so);
	return (error);
}

int
so_pru_disconnect(struct socket *so)
{
	int error;
	struct netmsg_pru_disconnect msg;
	lwkt_port_t port;

	so_lock(so);
	port = so->so_proto->pr_mport(so, NULL, NULL, PRU_DISCONNECT);
	netmsg_init(&msg.nm_netmsg, &curthread->td_msgport, 0,
		    netmsg_pru_disconnect);
	msg.nm_prufn = so->so_proto->pr_usrreqs->pru_disconnect;
	msg.nm_so = so;
	error = lwkt_domsg(port, &msg.nm_netmsg.nm_lmsg, 0);
	so_unlock(so);
	return (error);
}

int
so_pru_listen(struct socket *so, struct thread *td)
{
	int error;
	struct netmsg_pru_listen msg;
	lwkt_port_t port;

	so_lock(so);
	port = so->so_proto->pr_mport(so, NULL, NULL, PRU_LISTEN);
	netmsg_init(&msg.nm_netmsg, &curthread->td_msgport, 0,
		    netmsg_pru_listen);
	msg.nm_prufn = so->so_proto->pr_usrreqs->pru_listen;
	msg.nm_so = so;
	msg.nm_td = td;		/* used only for prison_ip() XXX JH */
	error = lwkt_domsg(port, &msg.nm_netmsg.nm_lmsg, 0);
	so_unlock(so);
	return (error);
}

int
so_pru_peeraddr(struct socket *so, struct sockaddr **nam)
{
	int error;
	struct netmsg_pru_peeraddr msg;
	lwkt_port_t port;

	so_lock(so);
	port = so->so_proto->pr_mport(so, NULL, NULL, PRU_PEERADDR);
	netmsg_init(&msg.nm_netmsg, &curthread->td_msgport, 0,
		    netmsg_pru_peeraddr);
	msg.nm_prufn = so->so_proto->pr_usrreqs->pru_peeraddr;
	msg.nm_so = so;
	msg.nm_nam = nam;
	error = lwkt_domsg(port, &msg.nm_netmsg.nm_lmsg, 0);
	so_unlock(so);
	return (error);
}

int
so_pru_rcvd(struct socket *so, int flags)
{
	int error;
	struct netmsg_pru_rcvd msg;
	lwkt_port_t port;

	port = so->so_proto->pr_mport(so, NULL, NULL, PRU_RCVD);
	netmsg_init(&msg.nm_netmsg, &curthread->td_msgport, 0,
		    netmsg_pru_rcvd);
	msg.nm_prufn = so->so_proto->pr_usrreqs->pru_rcvd;
	msg.nm_so = so;
	msg.nm_flags = flags;
	error = lwkt_domsg(port, &msg.nm_netmsg.nm_lmsg, 0);
	return (error);
}

int
so_pru_rcvoob(struct socket *so, struct mbuf *m, int flags)
{
	int error;
	struct netmsg_pru_rcvoob msg;
	lwkt_port_t port;

	port = so->so_proto->pr_mport(so, NULL, NULL, PRU_RCVOOB);
	netmsg_init(&msg.nm_netmsg, &curthread->td_msgport, 0,
		    netmsg_pru_rcvoob);
	msg.nm_prufn = so->so_proto->pr_usrreqs->pru_rcvoob;
	msg.nm_so = so;
	msg.nm_m = m;
	msg.nm_flags = flags;
	error = lwkt_domsg(port, &msg.nm_netmsg.nm_lmsg, 0);
	return (error);
}

int
so_pru_send(struct socket *so, int flags, struct mbuf *m,
	    struct sockaddr *addr, struct mbuf *control, struct thread *td)
{
	int error;
	struct netmsg_pru_send msg;
	lwkt_port_t port;

	port = so->so_proto->pr_mport(so, addr, &m, PRU_SEND);
	if (port == NULL) {
		KKASSERT(m == NULL);
		return EINVAL;
	}
	netmsg_init(&msg.nm_netmsg, &curthread->td_msgport, 0, netmsg_pru_send);
	msg.nm_prufn = so->so_proto->pr_usrreqs->pru_send;
	msg.nm_so = so;
	msg.nm_flags = flags;
	msg.nm_m = m;
	msg.nm_addr = addr;
	msg.nm_control = control;
	msg.nm_td = td;
	error = lwkt_domsg(port, &msg.nm_netmsg.nm_lmsg, 0);

	return (error);
}

int
so_pru_notify(struct socket *so)
{
	lwkt_port_t port;
	struct netmsg_pru_notify *msg = &so->so_notify_msg;

	/* XXX PRU_xxx request */
	/* XXX if TCP isn't connected port0? what about sendto? */
	port = so->so_proto->pr_mport(so, NULL, NULL, PRU_SEND);
	KKASSERT(port != NULL);

	/*
	 * Reissue the message if it isn't busy
	 */
	if (msg->nm_netmsg.nm_lmsg.ms_flags & MSGF_DONE) {
		spin_lock_wr(&so->so_lock.lk_spinlock);
		if (msg->nm_netmsg.nm_lmsg.ms_flags & MSGF_DONE) {
			msg->nm_netmsg.nm_dispatch = 
				so->so_proto->pr_usrreqs->pru_notify;
			msg->nm_so = so;
			lwkt_sendmsg(port, &msg->nm_netmsg.nm_lmsg);
		}
		spin_unlock_wr(&so->so_lock.lk_spinlock);
	}
	return(0);
}

int
so_pru_sense(struct socket *so, struct stat *sb)
{
	int error;
	struct netmsg_pru_sense msg;
	lwkt_port_t port;

	port = so->so_proto->pr_mport(so, NULL, NULL, PRU_SENSE);
	netmsg_init(&msg.nm_netmsg, &curthread->td_msgport, 0,
		    netmsg_pru_sense);
	msg.nm_prufn = so->so_proto->pr_usrreqs->pru_sense;
	msg.nm_so = so;
	msg.nm_stat = sb;
	error = lwkt_domsg(port, &msg.nm_netmsg.nm_lmsg, 0);
	return (error);
}

int
so_pru_shutdown(struct socket *so)
{
	int error;
	struct netmsg_pru_shutdown msg;
	lwkt_port_t port;

	so_lock(so);
	port = so->so_proto->pr_mport(so, NULL, NULL, PRU_SHUTDOWN);
	netmsg_init(&msg.nm_netmsg, &curthread->td_msgport, 0,
		    netmsg_pru_shutdown);
	msg.nm_prufn = so->so_proto->pr_usrreqs->pru_shutdown;
	msg.nm_so = so;
	error = lwkt_domsg(port, &msg.nm_netmsg.nm_lmsg, 0);
	so_unlock(so);
	return (error);
}

int
so_pru_sockaddr(struct socket *so, struct sockaddr **nam)
{
	int error;
	struct netmsg_pru_sockaddr msg;
	lwkt_port_t port;

	port = so->so_proto->pr_mport(so, NULL, NULL, PRU_SOCKADDR);
	netmsg_init(&msg.nm_netmsg, &curthread->td_msgport, 0,
		    netmsg_pru_sockaddr);
	msg.nm_prufn = so->so_proto->pr_usrreqs->pru_sockaddr;
	msg.nm_so = so;
	msg.nm_nam = nam;
	error = lwkt_domsg(port, &msg.nm_netmsg.nm_lmsg, 0);
	return (error);
}

int
so_pru_poll(struct socket *so, int events, struct ucred *cred)
{
	int error;
	struct netmsg_pru_poll msg;
	lwkt_port_t port;

	port = so->so_proto->pr_mport(so, NULL, NULL, PRU_SOPOLL);
	netmsg_init(&msg.nm_netmsg, &curthread->td_msgport, 0,
		    netmsg_pru_poll);
	msg.nm_prufn = so->so_proto->pr_usrreqs->pru_poll;
	msg.nm_so = so;
	msg.nm_events = events;
	msg.nm_cred = cred;
	msg.nm_td = curthread;
	error = lwkt_domsg(port, &msg.nm_netmsg.nm_lmsg, 0);
	return (error);
}

int
so_pru_ctloutput(struct socket *so, struct sockopt *sopt)
{
	struct netmsg_pru_ctloutput msg;
	lwkt_port_t port;
	int error;

	KKASSERT(!sopt->sopt_val || kva_p(sopt->sopt_val));
	port = so->so_proto->pr_mport(so, NULL, NULL, PRU_CTLOUTPUT);
	netmsg_init(&msg.nm_netmsg, &curthread->td_msgport, 0,
		    netmsg_pru_ctloutput);
	/* TBD: move pr_ctloutput to pr_usrreqs */
	msg.nm_prufn = so->so_proto->pr_ctloutput;
	msg.nm_so = so;
	msg.nm_sopt = sopt;
	error = lwkt_domsg(port, &msg.nm_netmsg.nm_lmsg, 0);
	return (error);
}

/*
 * Protocol control input, typically via icmp.
 *
 * If the protocol pr_ctlport is not NULL we call it to figure out the
 * protocol port.  If NULL is returned we can just return, otherwise
 * we issue a netmsg to call pr_ctlinput in the proper thread.
 *
 * This must be done synchronously as arg and/or extra may point to
 * temporary data.
 */
void
so_pru_ctlinput(struct protosw *pr, int cmd, struct sockaddr *arg, void *extra)
{
	struct netmsg_pru_ctlinput msg;
	lwkt_port_t port;

	if (pr->pr_ctlport == NULL)
		return;
	KKASSERT(pr->pr_ctlinput != NULL);
	port = pr->pr_ctlport(cmd, arg, extra);
	if (port == NULL)
		return;
	netmsg_init(&msg.nm_netmsg, &curthread->td_msgport, 0,
		    netmsg_pru_ctlinput);
	msg.nm_prufn = pr->pr_ctlinput;
	msg.nm_cmd = cmd;
	msg.nm_arg = arg;
	msg.nm_extra = extra;
	lwkt_domsg(port, &msg.nm_netmsg.nm_lmsg, 0);
}

/*
 * If we convert all the protosw pr_ functions for all the protocols
 * to take a message directly, this layer can go away.  For the moment
 * our dispatcher ignores the return value, but since we are handling
 * the replymsg ourselves we return EASYNC by convention.
 */

/*
 * Abort and destroy a socket.
 */
static void
netmsg_pru_abort(anynetmsg_t msg)
{
	struct netmsg_pru_abort *nm = &msg->pru_abort;
	struct socket *so = nm->nm_so;
	int error;

	KKASSERT(so->so_state & SS_ABORTING);
	so->so_state &= ~SS_ABORTING;
	error = nm->nm_prufn(so);
	if (error)
		sofree(so);
	lwkt_replymsg(&msg->netmsg.nm_lmsg, error);
}

#ifdef notused
static void
netmsg_pru_accept(anynetmsg_t msg)
{
	struct netmsg_pru_accept *nm = &msg->pru_accept;

	lwkt_replymsg(&msg->netmsg.nm_lmsg,
		      nm->nm_prufn(nm->nm_so, nm->nm_nam));
}
#endif

static void
netmsg_pru_attach(anynetmsg_t msg)
{
	struct netmsg_pru_attach *nm = &msg->pru_attach;

	lwkt_replymsg(&msg->netmsg.nm_lmsg,
		      nm->nm_prufn(nm->nm_so, nm->nm_proto, nm->nm_ai));
}

static void
netmsg_pru_bind(anynetmsg_t msg)
{
	struct netmsg_pru_bind *nm = &msg->pru_bind;

	lwkt_replymsg(&msg->netmsg.nm_lmsg,
		      nm->nm_prufn(nm->nm_so, nm->nm_nam, nm->nm_td));
}

static void
netmsg_pru_connect(anynetmsg_t msg)
{
	struct netmsg_pru_connect *nm = &msg->pru_connect;

	lwkt_replymsg(&msg->netmsg.nm_lmsg,
		      nm->nm_prufn(nm->nm_so, nm->nm_nam, nm->nm_td));
}

static void
netmsg_pru_connect2(anynetmsg_t msg)
{
	struct netmsg_pru_connect2 *nm = &msg->pru_connect2;

	lwkt_replymsg(&msg->netmsg.nm_lmsg,
		      nm->nm_prufn(nm->nm_so1, nm->nm_so2));
}

static void
netmsg_pru_control(anynetmsg_t msg)
{
	struct netmsg_pru_control *nm = &msg->pru_control;

	lwkt_replymsg(&msg->netmsg.nm_lmsg,
		      nm->nm_prufn(nm->nm_so, nm->nm_cmd, nm->nm_data,
				   nm->nm_ifp, nm->nm_td));
}

static void
netmsg_pru_detach(anynetmsg_t msg)
{
	struct netmsg_pru_detach *nm = &msg->pru_detach;

	lwkt_replymsg(&msg->netmsg.nm_lmsg, nm->nm_prufn(nm->nm_so));
}

static void
netmsg_pru_disconnect(anynetmsg_t msg)
{
	struct netmsg_pru_disconnect *nm = (void *)msg;

	lwkt_replymsg(&msg->netmsg.nm_lmsg, nm->nm_prufn(nm->nm_so));
}

static void
netmsg_pru_listen(anynetmsg_t msg)
{
	struct netmsg_pru_listen *nm = &msg->pru_listen;

	lwkt_replymsg(&msg->netmsg.nm_lmsg, nm->nm_prufn(nm->nm_so, nm->nm_td));
}

static void
netmsg_pru_peeraddr(anynetmsg_t msg)
{
	struct netmsg_pru_peeraddr *nm = &msg->pru_peeraddr;

	lwkt_replymsg(&msg->netmsg.nm_lmsg, nm->nm_prufn(nm->nm_so, nm->nm_nam));
}

static void
netmsg_pru_rcvd(anynetmsg_t msg)
{
	struct netmsg_pru_rcvd *nm = &msg->pru_rcvd;

	lwkt_replymsg(&msg->netmsg.nm_lmsg, nm->nm_prufn(nm->nm_so, nm->nm_flags));
}

static void
netmsg_pru_rcvoob(anynetmsg_t msg)
{
	struct netmsg_pru_rcvoob *nm = &msg->pru_rcvoob;

	lwkt_replymsg(&msg->netmsg.nm_lmsg,
		      nm->nm_prufn(nm->nm_so, nm->nm_m, nm->nm_flags));
}

static void
netmsg_pru_send(anynetmsg_t msg)
{
	struct netmsg_pru_send *nm = &msg->pru_send;

	lwkt_replymsg(&msg->netmsg.nm_lmsg,
		      nm->nm_prufn(nm->nm_so, nm->nm_flags, nm->nm_m,
				   nm->nm_addr, nm->nm_control, nm->nm_td));
}

static void
netmsg_pru_sense(anynetmsg_t msg)
{
	struct netmsg_pru_sense *nm = &msg->pru_sense;

	lwkt_replymsg(&msg->netmsg.nm_lmsg,
		      nm->nm_prufn(nm->nm_so, nm->nm_stat));
}

static void
netmsg_pru_shutdown(anynetmsg_t msg)
{
	struct netmsg_pru_shutdown *nm = &msg->pru_shutdown;

	lwkt_replymsg(&msg->netmsg.nm_lmsg, nm->nm_prufn(nm->nm_so));
}

static void
netmsg_pru_sockaddr(anynetmsg_t msg)
{
	struct netmsg_pru_sockaddr *nm = &msg->pru_sockaddr;

	lwkt_replymsg(&msg->netmsg.nm_lmsg, nm->nm_prufn(nm->nm_so, nm->nm_nam));
}

static void
netmsg_pru_poll(anynetmsg_t msg)
{
	struct netmsg_pru_poll *nm = &msg->pru_poll;

	lwkt_replymsg(&msg->netmsg.nm_lmsg,
		      nm->nm_prufn(nm->nm_so, nm->nm_events,
				   nm->nm_cred, nm->nm_td));
}

static void
netmsg_pru_ctloutput(anynetmsg_t msg)
{
	struct netmsg_pru_ctloutput *nm = &msg->pru_ctloutput;

	lwkt_replymsg(&msg->netmsg.nm_lmsg, nm->nm_prufn(nm->nm_so, nm->nm_sopt));
}

static void
netmsg_pru_ctlinput(anynetmsg_t msg)
{
	struct netmsg_pru_ctlinput *nm = &msg->pru_ctlinput;

	nm->nm_prufn(nm->nm_cmd, nm->nm_arg, nm->nm_extra);
	lwkt_replymsg(&msg->netmsg.nm_lmsg, 0);
}

#if 0
static void
netmsg_pr_timeout(anynetmsg_t netmsg)
{
	struct netmsg_pr_timeout *nm = &netmsg->timeout;

	lwkt_replymsg(&msg->netmsg.nm_lmsg, nm->nm_prfn());
}
#endif

/*
 * Handle a predicate event request.  This function is only called once
 * when the predicate message queueing request is received.
 */
void
netmsg_so_notify(anynetmsg_t netmsg)
{
	struct netmsg_so_notify *nm = &netmsg->so_notify;
	struct signalsockbuf *ssb;

	ssb = (nm->nm_etype & NM_REVENT) ?
			&nm->nm_so->so_rcv :
			&nm->nm_so->so_snd;

	/*
	 * Reply immediately if the event has occured, otherwise queue the
	 * request.
	 */
	if (nm->nm_predicate(&nm->nm_netmsg)) {
		lwkt_replymsg(&nm->nm_netmsg.nm_lmsg,
			      nm->nm_netmsg.nm_lmsg.ms_error);
	} else {
		TAILQ_INSERT_TAIL(&ssb->ssb_sel.si_mlist, nm, nm_list);
		ssb->ssb_flags |= SSB_MEVENT;
	}
}

/*
 * Called by doio when trying to abort a netmsg_so_notify message.
 * Unlike the other functions this one is dispatched directly by
 * the LWKT subsystem, so it takes a lwkt_msg_t as an argument.
 *
 * The original message, lmsg, is under the control of the caller and
 * will not be destroyed until we return so we can safely reference it
 * in our synchronous abort request.
 *
 * This part of the abort request occurs on the originating cpu which
 * means we may race the message flags and the original message may
 * not even have been processed by the target cpu yet.
 */
void
netmsg_so_notify_doabort(lwkt_msg_t lmsg)
{
	struct netmsg_so_notify_abort msg;

	if ((lmsg->ms_flags & (MSGF_DONE | MSGF_REPLY)) == 0) {
		netmsg_init(&msg.nm_netmsg, &curthread->td_msgport, 0,
			    netmsg_so_notify_abort);
		msg.nm_notifymsg = (void *)lmsg;
		lwkt_domsg(lmsg->ms_target_port, &msg.nm_netmsg.nm_lmsg, 0);
	}
}

/*
 * Predicate requests can be aborted.  This function is only called once
 * and will interlock against processing/reply races (since such races
 * occur on the same thread that controls the port where the abort is 
 * requeued).
 *
 * This part of the abort request occurs on the target cpu.  The message
 * flags must be tested again in case the test that we did on the
 * originating cpu raced.  Since messages are handled in sequence, the
 * original message will have already been handled by the loop and either
 * replied to or queued.
 *
 * We really only need to interlock with MSGF_REPLY (a bit that is set on
 * our cpu when we reply).  Note that MSGF_DONE is not set until the
 * reply reaches the originating cpu.  Test both bits anyway.
 */
void
netmsg_so_notify_abort(anynetmsg_t netmsg)
{
	struct netmsg_so_notify_abort *abrtmsg = &netmsg->so_notify_abort;
	struct netmsg_so_notify *msg = abrtmsg->nm_notifymsg;
	struct signalsockbuf *ssb;

	/*
	 * The original notify message is not destroyed until after the
	 * abort request is returned, so we can check its state.
	 */
	if ((msg->nm_netmsg.nm_lmsg.ms_flags & (MSGF_DONE | MSGF_REPLY)) == 0) {
		ssb = (msg->nm_etype & NM_REVENT) ?
				&msg->nm_so->so_rcv :
				&msg->nm_so->so_snd;
		TAILQ_REMOVE(&ssb->ssb_sel.si_mlist, msg, nm_list);
		lwkt_replymsg(&msg->nm_netmsg.nm_lmsg, EINTR);
	}

	/*
	 * Reply to the abort message
	 */
	lwkt_replymsg(&abrtmsg->nm_netmsg.nm_lmsg, 0);
}

