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
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/msgport.h>
#include <sys/protosw.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/socketops.h>
#include <sys/thread.h>
#include <sys/thread2.h>
#include <sys/msgport2.h>
#include <sys/spinlock2.h>
#include <sys/sysctl.h>
#include <sys/mbuf.h>
#include <vm/pmap.h>

#include <net/netmsg2.h>
#include <sys/socketvar2.h>

#include <net/netisr.h>
#include <net/netmsg.h>

static int async_rcvd_drop_race = 0;
SYSCTL_INT(_kern_ipc, OID_AUTO, async_rcvd_drop_race, CTLFLAG_RW,
    &async_rcvd_drop_race, 0, "# of asynchronized pru_rcvd msg drop races");

/*
 * Abort a socket and free it.  Called from soabort() only.  soabort()
 * got a ref on the socket which we must free on reply.
 */
void
so_pru_abort(struct socket *so)
{
	struct netmsg_pru_abort msg;

	netmsg_init(&msg.base, so, &curthread->td_msgport,
		    0, so->so_proto->pr_usrreqs->pru_abort);
	(void)lwkt_domsg(so->so_port, &msg.base.lmsg, 0);
	sofree(msg.base.nm_so);
}

/*
 * Abort a socket and free it, asynchronously.  Called from
 * soaborta() only.  soaborta() got a ref on the socket which we must
 * free on reply.
 */
void
so_pru_aborta(struct socket *so)
{
	struct netmsg_pru_abort *msg;

	msg = kmalloc(sizeof(*msg), M_LWKTMSG, M_WAITOK | M_ZERO);
	netmsg_init(&msg->base, so, &netisr_afree_free_so_rport,
		    0, so->so_proto->pr_usrreqs->pru_abort);
	lwkt_sendmsg(so->so_port, &msg->base.lmsg);
}

/*
 * Abort a socket and free it.  Called from soabort_oncpu() only.
 * Caller must make sure that the current CPU is inpcb's owner CPU.
 */
void
so_pru_abort_oncpu(struct socket *so)
{
	struct netmsg_pru_abort msg;
	netisr_fn_t func = so->so_proto->pr_usrreqs->pru_abort;

	netmsg_init(&msg.base, so, &netisr_adone_rport, 0, func);
	msg.base.lmsg.ms_flags &= ~(MSGF_REPLY | MSGF_DONE);
	msg.base.lmsg.ms_flags |= MSGF_SYNC;
	func((netmsg_t)&msg);
	KKASSERT(msg.base.lmsg.ms_flags & MSGF_DONE);
	sofree(msg.base.nm_so);
}

int
so_pru_accept(struct socket *so, struct sockaddr **nam)
{
	struct netmsg_pru_accept msg;

	netmsg_init(&msg.base, so, &curthread->td_msgport,
	    0, so->so_proto->pr_usrreqs->pru_accept);
	msg.nm_nam = nam;

	return lwkt_domsg(so->so_port, &msg.base.lmsg, 0);
}

int
so_pru_attach(struct socket *so, int proto, struct pru_attach_info *ai)
{
	struct netmsg_pru_attach msg;
	int error;

	netmsg_init(&msg.base, so, &curthread->td_msgport,
		    0, so->so_proto->pr_usrreqs->pru_attach);
	msg.nm_proto = proto;
	msg.nm_ai = ai;
	error = lwkt_domsg(so->so_port, &msg.base.lmsg, 0);
	return (error);
}

int
so_pru_attach_direct(struct socket *so, int proto, struct pru_attach_info *ai)
{
	struct netmsg_pru_attach msg;
	netisr_fn_t func = so->so_proto->pr_usrreqs->pru_attach;

	netmsg_init(&msg.base, so, &netisr_adone_rport, 0, func);
	msg.base.lmsg.ms_flags &= ~(MSGF_REPLY | MSGF_DONE);
	msg.base.lmsg.ms_flags |= MSGF_SYNC;
	msg.nm_proto = proto;
	msg.nm_ai = ai;
	func((netmsg_t)&msg);
	KKASSERT(msg.base.lmsg.ms_flags & MSGF_DONE);
	return(msg.base.lmsg.ms_error);
}

/*
 * NOTE: If the target port changes the bind operation will deal with it.
 */
int
so_pru_bind(struct socket *so, struct sockaddr *nam, struct thread *td)
{
	struct netmsg_pru_bind msg;
	int error;

	netmsg_init(&msg.base, so, &curthread->td_msgport,
		    0, so->so_proto->pr_usrreqs->pru_bind);
	msg.nm_nam = nam;
	msg.nm_td = td;		/* used only for prison_ip() */
	error = lwkt_domsg(so->so_port, &msg.base.lmsg, 0);
	return (error);
}

int
so_pru_connect(struct socket *so, struct sockaddr *nam, struct thread *td)
{
	struct netmsg_pru_connect msg;
	int error;

	netmsg_init(&msg.base, so, &curthread->td_msgport,
		    0, so->so_proto->pr_usrreqs->pru_connect);
	msg.nm_nam = nam;
	msg.nm_td = td;
	msg.nm_m = NULL;
	msg.nm_flags = 0;
	msg.nm_reconnect = 0;
	error = lwkt_domsg(so->so_port, &msg.base.lmsg, 0);
	return (error);
}

int
so_pru_connect2(struct socket *so1, struct socket *so2)
{
	struct netmsg_pru_connect2 msg;
	int error;

	netmsg_init(&msg.base, so1, &curthread->td_msgport,
		    0, so1->so_proto->pr_usrreqs->pru_connect2);
	msg.nm_so1 = so1;
	msg.nm_so2 = so2;
	error = lwkt_domsg(so1->so_port, &msg.base.lmsg, 0);
	return (error);
}

/*
 * WARNING!  Synchronous call from user context.  Control function may do
 *	     copyin/copyout.
 */
int
so_pru_control_direct(struct socket *so, u_long cmd, caddr_t data,
		      struct ifnet *ifp)
{
	struct netmsg_pru_control msg;
	netisr_fn_t func = so->so_proto->pr_usrreqs->pru_control;

	netmsg_init(&msg.base, so, &netisr_adone_rport, 0, func);
	msg.base.lmsg.ms_flags &= ~(MSGF_REPLY | MSGF_DONE);
	msg.base.lmsg.ms_flags |= MSGF_SYNC;
	msg.nm_cmd = cmd;
	msg.nm_data = data;
	msg.nm_ifp = ifp;
	msg.nm_td = curthread;
	func((netmsg_t)&msg);
	KKASSERT(msg.base.lmsg.ms_flags & MSGF_DONE);
	return(msg.base.lmsg.ms_error);
}

int
so_pru_detach(struct socket *so)
{
	struct netmsg_pru_detach msg;
	int error;

	netmsg_init(&msg.base, so, &curthread->td_msgport,
		    0, so->so_proto->pr_usrreqs->pru_detach);
	error = lwkt_domsg(so->so_port, &msg.base.lmsg, 0);
	return (error);
}

void
so_pru_detach_direct(struct socket *so)
{
	struct netmsg_pru_detach msg;
	netisr_fn_t func = so->so_proto->pr_usrreqs->pru_detach;

	netmsg_init(&msg.base, so, &netisr_adone_rport, 0, func);
	msg.base.lmsg.ms_flags &= ~(MSGF_REPLY | MSGF_DONE);
	msg.base.lmsg.ms_flags |= MSGF_SYNC;
	func((netmsg_t)&msg);
	KKASSERT(msg.base.lmsg.ms_flags & MSGF_DONE);
}

int
so_pru_disconnect(struct socket *so)
{
	struct netmsg_pru_disconnect msg;
	int error;

	netmsg_init(&msg.base, so, &curthread->td_msgport,
		    0, so->so_proto->pr_usrreqs->pru_disconnect);
	error = lwkt_domsg(so->so_port, &msg.base.lmsg, 0);
	return (error);
}

void
so_pru_disconnect_direct(struct socket *so)
{
	struct netmsg_pru_disconnect msg;
	netisr_fn_t func = so->so_proto->pr_usrreqs->pru_disconnect;

	netmsg_init(&msg.base, so, &netisr_adone_rport, 0, func);
	msg.base.lmsg.ms_flags &= ~(MSGF_REPLY | MSGF_DONE);
	msg.base.lmsg.ms_flags |= MSGF_SYNC;
	func((netmsg_t)&msg);
	KKASSERT(msg.base.lmsg.ms_flags & MSGF_DONE);
}

int
so_pru_listen(struct socket *so, struct thread *td)
{
	struct netmsg_pru_listen msg;
	int error;

	netmsg_init(&msg.base, so, &curthread->td_msgport,
		    0, so->so_proto->pr_usrreqs->pru_listen);
	msg.nm_td = td;		/* used only for prison_ip() XXX JH */
	error = lwkt_domsg(so->so_port, &msg.base.lmsg, 0);
	return (error);
}

int
so_pru_peeraddr(struct socket *so, struct sockaddr **nam)
{
	struct netmsg_pru_peeraddr msg;
	int error;

	netmsg_init(&msg.base, so, &curthread->td_msgport,
		    0, so->so_proto->pr_usrreqs->pru_peeraddr);
	msg.nm_nam = nam;
	error = lwkt_domsg(so->so_port, &msg.base.lmsg, 0);
	return (error);
}

int
so_pru_rcvd(struct socket *so, int flags)
{
	struct netmsg_pru_rcvd msg;
	int error;

	netmsg_init(&msg.base, so, &curthread->td_msgport,
		    0, so->so_proto->pr_usrreqs->pru_rcvd);
	msg.nm_flags = flags;
	msg.nm_pru_flags = 0;
	error = lwkt_domsg(so->so_port, &msg.base.lmsg, 0);
	return (error);
}

void
so_pru_rcvd_async(struct socket *so)
{
	lwkt_msg_t lmsg = &so->so_rcvd_msg.base.lmsg;

	KASSERT(so->so_proto->pr_flags & PR_ASYNC_RCVD,
	    ("async pru_rcvd is not supported"));

	/*
	 * WARNING!  Spinlock is a bit dodgy, use hacked up sendmsg
	 *	     to avoid deadlocking.
	 */
	spin_lock(&so->so_rcvd_spin);
	if ((so->so_rcvd_msg.nm_pru_flags & PRUR_DEAD) == 0) {
		if (lmsg->ms_flags & MSGF_DONE) {
			lwkt_sendmsg_stage1(so->so_port, lmsg);
			spin_unlock(&so->so_rcvd_spin);
			lwkt_sendmsg_stage2(so->so_port, lmsg);
		} else {
			spin_unlock(&so->so_rcvd_spin);
		}
	} else {
		spin_unlock(&so->so_rcvd_spin);
	}
}

int
so_pru_rcvoob(struct socket *so, struct mbuf *m, int flags)
{
	struct netmsg_pru_rcvoob msg;
	int error;

	netmsg_init(&msg.base, so, &curthread->td_msgport,
		    0, so->so_proto->pr_usrreqs->pru_rcvoob);
	msg.nm_m = m;
	msg.nm_flags = flags;
	error = lwkt_domsg(so->so_port, &msg.base.lmsg, 0);
	return (error);
}

/*
 * NOTE: If the target port changes the implied connect will deal with it.
 */
int
so_pru_send(struct socket *so, int flags, struct mbuf *m,
	    struct sockaddr *addr, struct mbuf *control, struct thread *td)
{
	struct netmsg_pru_send msg;
	int error;

	netmsg_init(&msg.base, so, &curthread->td_msgport,
		    0, so->so_proto->pr_usrreqs->pru_send);
	msg.nm_flags = flags;
	msg.nm_m = m;
	msg.nm_addr = addr;
	msg.nm_control = control;
	msg.nm_td = td;
	error = lwkt_domsg(so->so_port, &msg.base.lmsg, 0);
	return (error);
}

void
so_pru_sync(struct socket *so)
{
	struct netmsg_base msg;

	netmsg_init(&msg, so, &curthread->td_msgport, 0,
	    netmsg_sync_handler);
	lwkt_domsg(so->so_port, &msg.lmsg, 0);
}

void
so_pru_send_async(struct socket *so, int flags, struct mbuf *m,
    struct sockaddr *addr0, struct mbuf *control, struct thread *td)
{
	struct netmsg_pru_send *msg;
	struct sockaddr *addr = NULL;

	KASSERT(so->so_proto->pr_flags & PR_ASYNC_SEND,
	    ("async pru_send is not supported"));

	if (addr0 != NULL) {
		addr = kmalloc(addr0->sa_len, M_SONAME, M_NOWAIT);
		if (addr == NULL) {
			/*
			 * Fail to allocate address w/o waiting;
			 * fallback to synchronized pru_send.
			 */
			so_pru_send(so, flags, m, addr0, control, td);
			return;
		}
		memcpy(addr, addr0, addr0->sa_len);
		flags |= PRUS_FREEADDR;
	}
	flags |= PRUS_NOREPLY;

	if (td != NULL && (so->so_proto->pr_flags & PR_ASEND_HOLDTD)) {
		lwkt_hold(td);
		flags |= PRUS_HELDTD;
	}

	msg = &m->m_hdr.mh_sndmsg;
	netmsg_init(&msg->base, so, &netisr_apanic_rport,
		    0, so->so_proto->pr_usrreqs->pru_send);
	msg->nm_flags = flags;
	msg->nm_m = m;
	msg->nm_addr = addr;
	msg->nm_control = control;
	msg->nm_td = td;
	lwkt_sendmsg(so->so_port, &msg->base.lmsg);
}

int
so_pru_sense(struct socket *so, struct stat *sb)
{
	struct netmsg_pru_sense msg;
	int error;

	netmsg_init(&msg.base, so, &curthread->td_msgport,
		    0, so->so_proto->pr_usrreqs->pru_sense);
	msg.nm_stat = sb;
	error = lwkt_domsg(so->so_port, &msg.base.lmsg, 0);
	return (error);
}

int
so_pru_shutdown(struct socket *so)
{
	struct netmsg_pru_shutdown msg;
	int error;

	netmsg_init(&msg.base, so, &curthread->td_msgport,
		    0, so->so_proto->pr_usrreqs->pru_shutdown);
	error = lwkt_domsg(so->so_port, &msg.base.lmsg, 0);
	return (error);
}

int
so_pru_sockaddr(struct socket *so, struct sockaddr **nam)
{
	struct netmsg_pru_sockaddr msg;
	int error;

	netmsg_init(&msg.base, so, &curthread->td_msgport,
		    0, so->so_proto->pr_usrreqs->pru_sockaddr);
	msg.nm_nam = nam;
	error = lwkt_domsg(so->so_port, &msg.base.lmsg, 0);
	return (error);
}

int
so_pr_ctloutput(struct socket *so, struct sockopt *sopt)
{
	struct netmsg_pr_ctloutput msg;
	int error;

	KKASSERT(!sopt->sopt_val || kva_p(sopt->sopt_val));
	netmsg_init(&msg.base, so, &curthread->td_msgport,
		    0, so->so_proto->pr_ctloutput);
	msg.nm_sopt = sopt;
	error = lwkt_domsg(so->so_port, &msg.base.lmsg, 0);
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
	netmsg_init(&msg.base, NULL, &curthread->td_msgport,
		    0, pr->pr_ctlinput);
	msg.nm_cmd = cmd;
	msg.nm_arg = arg;
	msg.nm_extra = extra;
	lwkt_domsg(port, &msg.base.lmsg, 0);
}

/*
 * If we convert all the protosw pr_ functions for all the protocols
 * to take a message directly, this layer can go away.  For the moment
 * our dispatcher ignores the return value, but since we are handling
 * the replymsg ourselves we return EASYNC by convention.
 */

/*
 * Handle a predicate event request.  This function is only called once
 * when the predicate message queueing request is received.
 */
void
netmsg_so_notify(netmsg_t msg)
{
	struct lwkt_token *tok;
	struct signalsockbuf *ssb;

	ssb = (msg->notify.nm_etype & NM_REVENT) ?
			&msg->base.nm_so->so_rcv :
			&msg->base.nm_so->so_snd;

	/*
	 * Reply immediately if the event has occured, otherwise queue the
	 * request.
	 *
	 * NOTE: Socket can change if this is an accept predicate so cache
	 *	 the token.
	 */
	tok = lwkt_token_pool_lookup(msg->base.nm_so);
	lwkt_gettoken(tok);
	atomic_set_int(&ssb->ssb_flags, SSB_MEVENT);
	if (msg->notify.nm_predicate(&msg->notify)) {
		if (TAILQ_EMPTY(&ssb->ssb_kq.ki_mlist))
			atomic_clear_int(&ssb->ssb_flags, SSB_MEVENT);
		lwkt_reltoken(tok);
		lwkt_replymsg(&msg->base.lmsg,
			      msg->base.lmsg.ms_error);
	} else {
		TAILQ_INSERT_TAIL(&ssb->ssb_kq.ki_mlist, &msg->notify, nm_list);
		/*
		 * NOTE:
		 * If predict ever blocks, 'tok' will be released, so
		 * SSB_MEVENT set beforehand could have been cleared
		 * when we reach here.  In case that happens, we set
		 * SSB_MEVENT again, after the notify has been queued.
		 */
		atomic_set_int(&ssb->ssb_flags, SSB_MEVENT);
		lwkt_reltoken(tok);
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
		const struct netmsg_base *nmsg =
		    (const struct netmsg_base *)lmsg;

		netmsg_init(&msg.base, nmsg->nm_so, &curthread->td_msgport,
			    0, netmsg_so_notify_abort);
		msg.nm_notifymsg = (void *)lmsg;
		lwkt_domsg(lmsg->ms_target_port, &msg.base.lmsg, 0);
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
netmsg_so_notify_abort(netmsg_t msg)
{
	struct netmsg_so_notify_abort *abrtmsg = &msg->notify_abort;
	struct netmsg_so_notify *nmsg = abrtmsg->nm_notifymsg;
	struct signalsockbuf *ssb;

	/*
	 * The original notify message is not destroyed until after the
	 * abort request is returned, so we can check its state.
	 */
	lwkt_getpooltoken(nmsg->base.nm_so);
	if ((nmsg->base.lmsg.ms_flags & (MSGF_DONE | MSGF_REPLY)) == 0) {
		ssb = (nmsg->nm_etype & NM_REVENT) ?
				&nmsg->base.nm_so->so_rcv :
				&nmsg->base.nm_so->so_snd;
		TAILQ_REMOVE(&ssb->ssb_kq.ki_mlist, nmsg, nm_list);
		lwkt_relpooltoken(nmsg->base.nm_so);
		lwkt_replymsg(&nmsg->base.lmsg, EINTR);
	} else {
		lwkt_relpooltoken(nmsg->base.nm_so);
	}

	/*
	 * Reply to the abort message
	 */
	lwkt_replymsg(&abrtmsg->base.lmsg, 0);
}

void
so_async_rcvd_reply(struct socket *so)
{
	/*
	 * Spinlock safe, reply runs to degenerate lwkt_null_replyport()
	 */
	spin_lock(&so->so_rcvd_spin);
	lwkt_replymsg(&so->so_rcvd_msg.base.lmsg, 0);
	spin_unlock(&so->so_rcvd_spin);
}

void
so_async_rcvd_drop(struct socket *so)
{
	lwkt_msg_t lmsg = &so->so_rcvd_msg.base.lmsg;

	/*
	 * Spinlock safe, drop runs to degenerate lwkt_spin_dropmsg()
	 */
	spin_lock(&so->so_rcvd_spin);
	so->so_rcvd_msg.nm_pru_flags |= PRUR_DEAD;
again:
	lwkt_dropmsg(lmsg);
	if ((lmsg->ms_flags & MSGF_DONE) == 0) {
		++async_rcvd_drop_race;
		ssleep(so, &so->so_rcvd_spin, 0, "soadrop", 1);
		goto again;
	}
	spin_unlock(&so->so_rcvd_spin);
}
