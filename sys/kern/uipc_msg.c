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
 * $DragonFly: src/sys/kern/uipc_msg.c,v 1.13 2004/07/08 22:07:34 hsu Exp $
 */

/*
 * Copyright (c) 2003, 2004 Jeffrey M. Hsu.  All rights reserved.
 *
 * License terms: all terms for the DragonFly license above plus the following:
 *
 * 4. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *
 *	This product includes software developed by Jeffrey M. Hsu
 *	for the DragonFly Project.
 *
 *    This requirement may be waived with permission from Jeffrey Hsu.
 *    This requirement will sunset and may be removed on July 8 2005,
 *    after which the standard DragonFly license (as shown above) will
 *    apply.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/msgport.h>
#include <sys/protosw.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/socketops.h>
#include <sys/thread.h>
#include <sys/thread2.h>
#include <sys/msgport2.h>

#include <net/netisr.h>
#include <net/netmsg.h>

int
so_pru_abort(struct socket *so)
{
	int error;
	struct netmsg_pru_abort msg;
	lwkt_port_t port;

	port = so->so_proto->pr_mport(so, NULL, PRU_ABORT);
	lwkt_initmsg(&msg.nm_lmsg, &curthread->td_msgport, 0,
		lwkt_cmd_func(netmsg_pru_abort), lwkt_cmd_op_none);
	msg.nm_prufn = so->so_proto->pr_usrreqs->pru_abort;
	msg.nm_so = so;
	error = lwkt_domsg(port, &msg.nm_lmsg);
	return (error);
}

int
so_pru_accept(struct socket *so, struct sockaddr **nam)
{
	/* Block (memory allocation) in process context. XXX JH */
	return ((*so->so_proto->pr_usrreqs->pru_accept)(so, nam));

#ifdef notdef
	int error;
	struct netmsg_pru_accept msg;
	lwkt_port_t port;

	port = so->so_proto->pr_mport(so, NULL, PRU_ACCEPT);
	lwkt_initmsg(&msg.nm_lmsg, &curthread->td_msgport, 0,
		lwkt_cmd_func(netmsg_pru_accept), lwkt_cmd_op_none);
	msg.nm_prufn = so->so_proto->pr_usrreqs->pru_accept;
	msg.nm_so = so;
	msg.nm_nam = nam;
	error = lwkt_domsg(port, &msg.nm_lmsg);
	return (error);
#endif
}

int
so_pru_attach(struct socket *so, int proto, struct pru_attach_info *ai)
{
	int error;
	struct netmsg_pru_attach msg;
	lwkt_port_t port;

	port = so->so_proto->pr_mport(NULL, NULL, PRU_ATTACH);
	lwkt_initmsg(&msg.nm_lmsg, &curthread->td_msgport, 0,
		lwkt_cmd_func(netmsg_pru_attach), lwkt_cmd_op_none);
	msg.nm_prufn = so->so_proto->pr_usrreqs->pru_attach;
	msg.nm_so = so;
	msg.nm_proto = proto;
	msg.nm_ai = ai;
	error = lwkt_domsg(port, &msg.nm_lmsg);
	return (error);
}

int
so_pru_bind(struct socket *so, struct sockaddr *nam, struct thread *td)
{
	int error;
	struct netmsg_pru_bind msg;
	lwkt_port_t port;

	/* Send mesg to thread for new address. */
	port = so->so_proto->pr_mport(NULL, nam, PRU_BIND);
	lwkt_initmsg(&msg.nm_lmsg, &curthread->td_msgport, 0,
		lwkt_cmd_func(netmsg_pru_bind), lwkt_cmd_op_none);
	msg.nm_prufn = so->so_proto->pr_usrreqs->pru_bind;
	msg.nm_so = so;
	msg.nm_nam = nam;
	msg.nm_td = td;		/* used only for prison_ip() XXX JH */
	error = lwkt_domsg(port, &msg.nm_lmsg);
	return (error);
}

int
so_pru_connect(struct socket *so, struct sockaddr *nam, struct thread *td)
{
	int error;
	struct netmsg_pru_connect msg;
	lwkt_port_t port;

	port = so->so_proto->pr_mport(so, nam, PRU_CONNECT);
	lwkt_initmsg(&msg.nm_lmsg, &curthread->td_msgport, 0,
		lwkt_cmd_func(netmsg_pru_connect), lwkt_cmd_op_none);
	msg.nm_prufn = so->so_proto->pr_usrreqs->pru_connect;
	msg.nm_so = so;
	msg.nm_nam = nam;
	msg.nm_td = td;
	error = lwkt_domsg(port, &msg.nm_lmsg);
	return (error);
}

int
so_pru_connect2(struct socket *so1, struct socket *so2)
{
	int error;
	struct netmsg_pru_connect2 msg;
	lwkt_port_t port;

	port = so1->so_proto->pr_mport(so1, NULL, PRU_CONNECT2);
	lwkt_initmsg(&msg.nm_lmsg, &curthread->td_msgport, 0,
		lwkt_cmd_func(netmsg_pru_connect2), lwkt_cmd_op_none);
	msg.nm_prufn = so1->so_proto->pr_usrreqs->pru_connect2;
	msg.nm_so1 = so1;
	msg.nm_so2 = so2;
	error = lwkt_domsg(port, &msg.nm_lmsg);
	return (error);
}

int
so_pru_control(struct socket *so, u_long cmd, caddr_t data, struct ifnet *ifp,
    struct thread *td)
{
	return ((*so->so_proto->pr_usrreqs->pru_control)(so, cmd, data, ifp,
	    td));
#ifdef gag	/* does copyin and copyout deep inside stack XXX JH */
	int error;
	struct netmsg_pru_control msg;
	lwkt_port_t port;

	port = so->so_proto->pr_mport(so, NULL, PRU_CONTROL);
	lwkt_initmsg(&msg.nm_lmsg, &curthread->td_msgport, 0,
		lwkt_cmd_func(netmsg_pru_control), lwkt_cmd_op_none);
	msg.nm_prufn = so->so_proto->pr_usrreqs->pru_control;
	msg.nm_so = so;
	msg.nm_cmd = cmd;
	msg.nm_data = data;
	msg.nm_ifp = ifp;
	msg.nm_td = td;
	error = lwkt_domsg(port, &msg.nm_lmsg);
	return (error);
#endif
}

int
so_pru_detach(struct socket *so)
{
	int error;
	struct netmsg_pru_detach msg;
	lwkt_port_t port;

	port = so->so_proto->pr_mport(so, NULL, PRU_DETACH);
	lwkt_initmsg(&msg.nm_lmsg, &curthread->td_msgport, 0,
		lwkt_cmd_func(netmsg_pru_detach), lwkt_cmd_op_none);
	msg.nm_prufn = so->so_proto->pr_usrreqs->pru_detach;
	msg.nm_so = so;
	error = lwkt_domsg(port, &msg.nm_lmsg);
	return (error);
}

int
so_pru_disconnect(struct socket *so)
{
	int error;
	struct netmsg_pru_disconnect msg;
	lwkt_port_t port;

	port = so->so_proto->pr_mport(so, NULL, PRU_DISCONNECT);
	lwkt_initmsg(&msg.nm_lmsg, &curthread->td_msgport, 0,
		lwkt_cmd_func(netmsg_pru_disconnect), lwkt_cmd_op_none);
	msg.nm_prufn = so->so_proto->pr_usrreqs->pru_disconnect;
	msg.nm_so = so;
	error = lwkt_domsg(port, &msg.nm_lmsg);
	return (error);
}

int
so_pru_listen(struct socket *so, struct thread *td)
{
	int error;
	struct netmsg_pru_listen msg;
	lwkt_port_t port;

	port = so->so_proto->pr_mport(so, NULL, PRU_LISTEN);
	lwkt_initmsg(&msg.nm_lmsg, &curthread->td_msgport, 0,
		lwkt_cmd_func(netmsg_pru_listen), lwkt_cmd_op_none);
	msg.nm_prufn = so->so_proto->pr_usrreqs->pru_listen;
	msg.nm_so = so;
	msg.nm_td = td;		/* used only for prison_ip() XXX JH */
	error = lwkt_domsg(port, &msg.nm_lmsg);
	return (error);
}

int
so_pru_peeraddr(struct socket *so, struct sockaddr **nam)
{
	int error;
	struct netmsg_pru_peeraddr msg;
	lwkt_port_t port;

	port = so->so_proto->pr_mport(so, NULL, PRU_PEERADDR);
	lwkt_initmsg(&msg.nm_lmsg, &curthread->td_msgport, 0,
		lwkt_cmd_func(netmsg_pru_peeraddr), lwkt_cmd_op_none);
	msg.nm_prufn = so->so_proto->pr_usrreqs->pru_peeraddr;
	msg.nm_so = so;
	msg.nm_nam = nam;
	error = lwkt_domsg(port, &msg.nm_lmsg);
	return (error);
}

int
so_pru_rcvd(struct socket *so, int flags)
{
	int error;
	struct netmsg_pru_rcvd msg;
	lwkt_port_t port;

	port = so->so_proto->pr_mport(so, NULL, PRU_RCVD);
	lwkt_initmsg(&msg.nm_lmsg, &curthread->td_msgport, 0,
		lwkt_cmd_func(netmsg_pru_rcvd), lwkt_cmd_op_none);
	msg.nm_prufn = so->so_proto->pr_usrreqs->pru_rcvd;
	msg.nm_so = so;
	msg.nm_flags = flags;
	error = lwkt_domsg(port, &msg.nm_lmsg);
	return (error);
}

int
so_pru_rcvoob(struct socket *so, struct mbuf *m, int flags)
{
	int error;
	struct netmsg_pru_rcvoob msg;
	lwkt_port_t port;

	port = so->so_proto->pr_mport(so, NULL, PRU_RCVOOB);
	lwkt_initmsg(&msg.nm_lmsg, &curthread->td_msgport, 0,
		lwkt_cmd_func(netmsg_pru_rcvoob), lwkt_cmd_op_none);
	msg.nm_prufn = so->so_proto->pr_usrreqs->pru_rcvoob;
	msg.nm_so = so;
	msg.nm_m = m;
	msg.nm_flags = flags;
	error = lwkt_domsg(port, &msg.nm_lmsg);
	return (error);
}

int
so_pru_send(struct socket *so, int flags, struct mbuf *m, struct sockaddr *addr,
    struct mbuf *control, struct thread *td)
{
	int error;
	struct netmsg_pru_send msg;
	lwkt_port_t port;

	port = so->so_proto->pr_mport(so, NULL, PRU_SEND);
	lwkt_initmsg(&msg.nm_lmsg, &curthread->td_msgport, 0,
		lwkt_cmd_func(netmsg_pru_send), lwkt_cmd_op_none);
	msg.nm_prufn = so->so_proto->pr_usrreqs->pru_send;
	msg.nm_so = so;
	msg.nm_flags = flags;
	msg.nm_m = m;
	msg.nm_addr = addr;
	msg.nm_control = control;
	msg.nm_td = td;
	error = lwkt_domsg(port, &msg.nm_lmsg);
	return (error);
}

int
so_pru_sense(struct socket *so, struct stat *sb)
{
	int error;
	struct netmsg_pru_sense msg;
	lwkt_port_t port;

	port = so->so_proto->pr_mport(so, NULL, PRU_SENSE);
	lwkt_initmsg(&msg.nm_lmsg, &curthread->td_msgport, 0,
		lwkt_cmd_func(netmsg_pru_sense), lwkt_cmd_op_none);
	msg.nm_prufn = so->so_proto->pr_usrreqs->pru_sense;
	msg.nm_so = so;
	msg.nm_stat = sb;
	error = lwkt_domsg(port, &msg.nm_lmsg);
	return (error);
}

int
so_pru_shutdown(struct socket *so)
{
	int error;
	struct netmsg_pru_shutdown msg;
	lwkt_port_t port;

	port = so->so_proto->pr_mport(so, NULL, PRU_SHUTDOWN);
	lwkt_initmsg(&msg.nm_lmsg, &curthread->td_msgport, 0,
		lwkt_cmd_func(netmsg_pru_shutdown), lwkt_cmd_op_none);
	msg.nm_prufn = so->so_proto->pr_usrreqs->pru_shutdown;
	msg.nm_so = so;
	error = lwkt_domsg(port, &msg.nm_lmsg);
	return (error);
}

int
so_pru_sockaddr(struct socket *so, struct sockaddr **nam)
{
	int error;
	struct netmsg_pru_sockaddr msg;
	lwkt_port_t port;

	port = so->so_proto->pr_mport(so, NULL, PRU_SOCKADDR);
	lwkt_initmsg(&msg.nm_lmsg, &curthread->td_msgport, 0,
		lwkt_cmd_func(netmsg_pru_sockaddr), lwkt_cmd_op_none);
	msg.nm_prufn = so->so_proto->pr_usrreqs->pru_sockaddr;
	msg.nm_so = so;
	msg.nm_nam = nam;
	error = lwkt_domsg(port, &msg.nm_lmsg);
	return (error);
}

int
so_pru_sopoll(struct socket *so, int events, struct ucred *cred,
    struct thread *td)
{
	int error;
	struct netmsg_pru_sopoll msg;
	lwkt_port_t port;

	port = so->so_proto->pr_mport(so, NULL, PRU_SOPOLL);
	lwkt_initmsg(&msg.nm_lmsg, &curthread->td_msgport, 0,
		lwkt_cmd_func(netmsg_pru_sopoll), lwkt_cmd_op_none);
	msg.nm_prufn = so->so_proto->pr_usrreqs->pru_sopoll;
	msg.nm_so = so;
	msg.nm_events = events;
	msg.nm_cred = cred;
	msg.nm_td = td;
	error = lwkt_domsg(port, &msg.nm_lmsg);
	return (error);
}

int
so_pr_ctloutput(struct socket *so, struct sockopt *sopt)
{
	return ((*so->so_proto->pr_ctloutput)(so, sopt));
#ifdef gag	/* does copyin and copyout deep inside stack XXX JH */
	struct netmsg_pr_ctloutput msg;
	lwkt_port_t port;
	int error;

	port = so->so_proto->pr_mport(so, NULL);
	lwkt_initmsg(&msg.nm_lmsg, &curthread->td_msgport, 0,
		lwkt_cmd_func(netmsg_pru_ctloutput), lwkt_cmd_op_none);
	msg.nm_prfn = so->so_proto->pr_ctloutput;
	msg.nm_so = so;
	msg.nm_sopt = sopt;
	error = lwkt_domsg(port, &msg.nm_lmsg);
	return (error);
#endif
}

/*
 * If we convert all the protosw pr_ functions for all the protocols
 * to take a message directly, this layer can go away.  For the moment
 * our dispatcher ignores the return value, but since we are handling
 * the replymsg ourselves we return EASYNC by convention.
 */
int
netmsg_pru_abort(lwkt_msg_t msg)
{
	struct netmsg_pru_abort *nm = (void *)msg;

	lwkt_replymsg(msg, nm->nm_prufn(nm->nm_so));
	return(EASYNC);
}

#ifdef notused
int
netmsg_pru_accept(lwkt_msg_t msg)
{
	struct netmsg_pru_accept *nm = (void *)msg;

	lwkt_replymsg(msg, nm->nm_prufn(nm->nm_so, nm->nm_nam));
	return(EASYNC);
}
#endif

int
netmsg_pru_attach(lwkt_msg_t msg)
{
	struct netmsg_pru_attach *nm = (void *)msg;

	lwkt_replymsg(msg, nm->nm_prufn(nm->nm_so, nm->nm_proto, nm->nm_ai));
	return(EASYNC);
}

int
netmsg_pru_bind(lwkt_msg_t msg)
{
	struct netmsg_pru_bind *nm = (void *)msg;

	lwkt_replymsg(msg, nm->nm_prufn(nm->nm_so, nm->nm_nam, nm->nm_td));
	return(EASYNC);
}

int
netmsg_pru_connect(lwkt_msg_t msg)
{
	struct netmsg_pru_connect *nm = (void *)msg;

	lwkt_replymsg(msg, nm->nm_prufn(nm->nm_so, nm->nm_nam, nm->nm_td));
	return(EASYNC);
}

int
netmsg_pru_connect2(lwkt_msg_t msg)
{
	struct netmsg_pru_connect2 *nm = (void *)msg;

	lwkt_replymsg(msg, nm->nm_prufn(nm->nm_so1, nm->nm_so2));
	return(EASYNC);
}

int
netmsg_pru_control(lwkt_msg_t msg)
{
	struct netmsg_pru_control *nm = (void *)msg;
	int error;

	error = nm->nm_prufn(nm->nm_so, nm->nm_cmd, nm->nm_data,
				nm->nm_ifp, nm->nm_td);
	lwkt_replymsg(msg, error);
	return(EASYNC);
}

int
netmsg_pru_detach(lwkt_msg_t msg)
{
	struct netmsg_pru_detach *nm = (void *)msg;

	lwkt_replymsg(msg, nm->nm_prufn(nm->nm_so));
	return(EASYNC);
}

int
netmsg_pru_disconnect(lwkt_msg_t msg)
{
	struct netmsg_pru_disconnect *nm = (void *)msg;

	lwkt_replymsg(msg, nm->nm_prufn(nm->nm_so));
	return(EASYNC);
}

int
netmsg_pru_listen(lwkt_msg_t msg)
{
	struct netmsg_pru_listen *nm = (void *)msg;

	lwkt_replymsg(msg, nm->nm_prufn(nm->nm_so, nm->nm_td));
	return(EASYNC);
}

int
netmsg_pru_peeraddr(lwkt_msg_t msg)
{
	struct netmsg_pru_peeraddr *nm = (void *)msg;

	lwkt_replymsg(msg, nm->nm_prufn(nm->nm_so, nm->nm_nam));
	return(EASYNC);
}

int
netmsg_pru_rcvd(lwkt_msg_t msg)
{
	struct netmsg_pru_rcvd *nm = (void *)msg;

	lwkt_replymsg(msg, nm->nm_prufn(nm->nm_so, nm->nm_flags));
	return(EASYNC);
}

int
netmsg_pru_rcvoob(lwkt_msg_t msg)
{
	struct netmsg_pru_rcvoob *nm = (void *)msg;

	lwkt_replymsg(msg, nm->nm_prufn(nm->nm_so, nm->nm_m, nm->nm_flags));
	return(EASYNC);
}

int
netmsg_pru_send(lwkt_msg_t msg)
{
	struct netmsg_pru_send *nm = (void *)msg;
	int error;

	error = nm->nm_prufn(nm->nm_so, nm->nm_flags, nm->nm_m,
				nm->nm_addr, nm->nm_control, nm->nm_td);
	lwkt_replymsg(msg, error);
	return(EASYNC);
}

int
netmsg_pru_sense(lwkt_msg_t msg)
{
	struct netmsg_pru_sense *nm = (void *)msg;

	lwkt_replymsg(msg, nm->nm_prufn(nm->nm_so, nm->nm_stat));
	return(EASYNC);
}

int
netmsg_pru_shutdown(lwkt_msg_t msg)
{
	struct netmsg_pru_shutdown *nm = (void *)msg;

	lwkt_replymsg(msg, nm->nm_prufn(nm->nm_so));
	return(EASYNC);
}

int
netmsg_pru_sockaddr(lwkt_msg_t msg)
{
	struct netmsg_pru_sockaddr *nm = (void *)msg;

	lwkt_replymsg(msg, nm->nm_prufn(nm->nm_so, nm->nm_nam));
	return(EASYNC);
}

int
netmsg_pru_sopoll(lwkt_msg_t msg)
{
	struct netmsg_pru_sopoll *nm = (void *)msg;
	int error;

	error = nm->nm_prufn(nm->nm_so, nm->nm_events, nm->nm_cred, nm->nm_td);
	lwkt_replymsg(msg, error);
	return(EASYNC);
}

int
netmsg_pr_ctloutput(lwkt_msg_t msg)
{
	struct netmsg_pr_ctloutput *nm = (void *)msg;

	lwkt_replymsg(msg, nm->nm_prfn(nm->nm_so, nm->nm_sopt));
	return(EASYNC);
}

int
netmsg_pr_timeout(lwkt_msg_t msg)
{
	struct netmsg_pr_timeout *nm = (void *)msg;

	lwkt_replymsg(msg, nm->nm_prfn());
	return(EASYNC);
}

/*
 * Handle a predicate event request.  This function is only called once
 * when the predicate message queueing request is received.
 */
int
netmsg_so_notify(lwkt_msg_t lmsg)
{
	struct netmsg_so_notify *msg = (void *)lmsg;
	struct sockbuf *sb;

	sb = (msg->nm_etype & NM_REVENT) ?
			&msg->nm_so->so_rcv :
			&msg->nm_so->so_snd;

	/*
	 * Reply immediately if the event has occured, otherwise queue the
	 * request.
	 */
	if (msg->nm_predicate((struct netmsg *)msg)) {
		lwkt_replymsg(lmsg, lmsg->ms_error);
	} else {
		TAILQ_INSERT_TAIL(&sb->sb_sel.si_mlist, msg, nm_list);
		sb->sb_flags |= SB_MEVENT;
	}
	return(EASYNC);
}

/*
 * Predicate requests can be aborted.  This function is only called once
 * and will interlock against processing/reply races (since such races
 * occur on the same thread that controls the port where the abort is 
 * requeued).
 */
int
netmsg_so_notify_abort(lwkt_msg_t lmsg)
{
	struct netmsg_so_notify *msg = (void *)lmsg;
	struct sockbuf *sb;

	sb = (msg->nm_etype & NM_REVENT) ?
			&msg->nm_so->so_rcv :
			&msg->nm_so->so_snd;
	TAILQ_REMOVE(&sb->sb_sel.si_mlist, msg, nm_list);
	lwkt_replymsg(lmsg, EINTR);
	return(EASYNC);
}

