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

/*
 * Copyright (c) 1982, 1986, 1988, 1993
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
 *	From: @(#)tcp_usrreq.c	8.2 (Berkeley) 1/3/94
 * $FreeBSD: src/sys/netinet/tcp_usrreq.c,v 1.51.2.17 2002/10/11 11:46:44 ume Exp $
 */

#include "opt_ipsec.h"
#include "opt_inet.h"
#include "opt_inet6.h"
#include "opt_tcpdebug.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/sysctl.h>
#include <sys/globaldata.h>
#include <sys/thread.h>

#include <sys/mbuf.h>
#ifdef INET6
#include <sys/domain.h>
#endif /* INET6 */
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/socketops.h>
#include <sys/protosw.h>

#include <sys/thread2.h>
#include <sys/msgport2.h>
#include <sys/socketvar2.h>

#include <net/if.h>
#include <net/netisr.h>
#include <net/route.h>

#include <net/netmsg2.h>
#include <net/netisr2.h>

#include <netinet/in.h>
#include <netinet/in_systm.h>
#ifdef INET6
#include <netinet/ip6.h>
#endif
#include <netinet/in_pcb.h>
#ifdef INET6
#include <netinet6/in6_pcb.h>
#endif
#include <netinet/in_var.h>
#include <netinet/ip_var.h>
#ifdef INET6
#include <netinet6/ip6_var.h>
#include <netinet6/tcp6_var.h>
#endif
#include <netinet/tcp.h>
#include <netinet/tcp_fsm.h>
#include <netinet/tcp_seq.h>
#include <netinet/tcp_timer.h>
#include <netinet/tcp_timer2.h>
#include <netinet/tcp_var.h>
#include <netinet/tcpip.h>
#ifdef TCPDEBUG
#include <netinet/tcp_debug.h>
#endif

#ifdef IPSEC
#include <netinet6/ipsec.h>
#endif /*IPSEC*/

/*
 * TCP protocol interface to socket abstraction.
 */
extern	char *tcpstates[];	/* XXX ??? */

static int	tcp_attach (struct socket *, struct pru_attach_info *);
static void	tcp_connect (netmsg_t msg);
#ifdef INET6
static void	tcp6_connect (netmsg_t msg);
static int	tcp6_connect_oncpu(struct tcpcb *tp, int flags,
				struct mbuf **mp,
				struct sockaddr_in6 *sin6,
				struct in6_addr *addr6);
#endif /* INET6 */
static struct tcpcb *
		tcp_disconnect (struct tcpcb *);
static struct tcpcb *
		tcp_usrclosed (struct tcpcb *);

#ifdef TCPDEBUG
#define	TCPDEBUG0	int ostate = 0
#define	TCPDEBUG1()	ostate = tp ? tp->t_state : 0
#define	TCPDEBUG2(req)	if (tp && (so->so_options & SO_DEBUG)) \
				tcp_trace(TA_USER, ostate, tp, 0, 0, req)
#else
#define	TCPDEBUG0
#define	TCPDEBUG1()
#define	TCPDEBUG2(req)
#endif

static int	tcp_lport_extension = 1;
SYSCTL_INT(_net_inet_tcp, OID_AUTO, lportext, CTLFLAG_RW,
    &tcp_lport_extension, 0, "");

/*
 * For some ill optimized programs, which try to use TCP_NOPUSH
 * to improve performance, will have small amount of data sits
 * in the sending buffer.  These small amount of data will _not_
 * be pushed into the network until more data are written into
 * the socket or the socket write side is shutdown.
 */ 
static int	tcp_disable_nopush = 1;
SYSCTL_INT(_net_inet_tcp, OID_AUTO, disable_nopush, CTLFLAG_RW,
    &tcp_disable_nopush, 0, "TCP_NOPUSH socket option will have no effect");

/*
 * TCP attaches to socket via pru_attach(), reserving space,
 * and an internet control block.  This is likely occuring on
 * cpu0 and may have to move later when we bind/connect.
 */
static void
tcp_usr_attach(netmsg_t msg)
{
	struct socket *so = msg->base.nm_so;
	struct pru_attach_info *ai = msg->attach.nm_ai;
	int error;
	struct inpcb *inp;
	struct tcpcb *tp = NULL;
	TCPDEBUG0;

	soreference(so);
	inp = so->so_pcb;
	TCPDEBUG1();
	if (inp) {
		error = EISCONN;
		goto out;
	}

	error = tcp_attach(so, ai);
	if (error)
		goto out;

	if ((so->so_options & SO_LINGER) && so->so_linger == 0)
		so->so_linger = TCP_LINGERTIME;
	tp = sototcpcb(so);
out:
	sofree(so);		/* from ref above */
	TCPDEBUG2(PRU_ATTACH);
	lwkt_replymsg(&msg->lmsg, error);
}

/*
 * pru_detach() detaches the TCP protocol from the socket.
 * If the protocol state is non-embryonic, then can't
 * do this directly: have to initiate a pru_disconnect(),
 * which may finish later; embryonic TCB's can just
 * be discarded here.
 */
static void
tcp_usr_detach(netmsg_t msg)
{
	struct socket *so = msg->base.nm_so;
	int error = 0;
	struct inpcb *inp;
	struct tcpcb *tp;
	TCPDEBUG0;

	inp = so->so_pcb;

	/*
	 * If the inp is already detached it may have been due to an async
	 * close.  Just return as if no error occured.
	 *
	 * It's possible for the tcpcb (tp) to disconnect from the inp due
	 * to tcp_drop()->tcp_close() being called.  This may occur *after*
	 * the detach message has been queued so we may find a NULL tp here.
	 */
	if (inp) {
		if ((tp = intotcpcb(inp)) != NULL) {
			TCPDEBUG1();
			tp = tcp_disconnect(tp);
			TCPDEBUG2(PRU_DETACH);
		}
	}
	lwkt_replymsg(&msg->lmsg, error);
}

/*
 * NOTE: ignore_error is non-zero for certain disconnection races
 * which we want to silently allow, otherwise close() may return
 * an unexpected error.
 *
 * NOTE: The variables (msg) and (tp) are assumed.
 */
#define	COMMON_START(so, inp, ignore_error)			\
	TCPDEBUG0; 						\
								\
	inp = so->so_pcb; 					\
	do {							\
		 if (inp == NULL) {				\
			error = ignore_error ? 0 : EINVAL;	\
			tp = NULL;				\
			goto out;				\
		 }						\
		 tp = intotcpcb(inp);				\
		 TCPDEBUG1();					\
	} while(0)

#define COMMON_END1(req, noreply)				\
	out: do {						\
		TCPDEBUG2(req);					\
		if (!(noreply))					\
			lwkt_replymsg(&msg->lmsg, error);	\
		return;						\
	} while(0)

#define COMMON_END(req)		COMMON_END1((req), 0)

/*
 * Give the socket an address.
 */
static void
tcp_usr_bind(netmsg_t msg)
{
	struct socket *so = msg->bind.base.nm_so;
	struct sockaddr *nam = msg->bind.nm_nam;
	struct thread *td = msg->bind.nm_td;
	int error = 0;
	struct inpcb *inp;
	struct tcpcb *tp;
	struct sockaddr_in *sinp;

	COMMON_START(so, inp, 0);

	/*
	 * Must check for multicast addresses and disallow binding
	 * to them.
	 */
	sinp = (struct sockaddr_in *)nam;
	if (sinp->sin_family == AF_INET &&
	    IN_MULTICAST(ntohl(sinp->sin_addr.s_addr))) {
		error = EAFNOSUPPORT;
		goto out;
	}
	error = in_pcbbind(inp, nam, td);
	if (error)
		goto out;
	COMMON_END(PRU_BIND);

}

#ifdef INET6

static void
tcp6_usr_bind(netmsg_t msg)
{
	struct socket *so = msg->bind.base.nm_so;
	struct sockaddr *nam = msg->bind.nm_nam;
	struct thread *td = msg->bind.nm_td;
	int error = 0;
	struct inpcb *inp;
	struct tcpcb *tp;
	struct sockaddr_in6 *sin6p;

	COMMON_START(so, inp, 0);

	/*
	 * Must check for multicast addresses and disallow binding
	 * to them.
	 */
	sin6p = (struct sockaddr_in6 *)nam;
	if (sin6p->sin6_family == AF_INET6 &&
	    IN6_IS_ADDR_MULTICAST(&sin6p->sin6_addr)) {
		error = EAFNOSUPPORT;
		goto out;
	}
	inp->inp_vflag &= ~INP_IPV4;
	inp->inp_vflag |= INP_IPV6;
	if ((inp->inp_flags & IN6P_IPV6_V6ONLY) == 0) {
		if (IN6_IS_ADDR_UNSPECIFIED(&sin6p->sin6_addr))
			inp->inp_vflag |= INP_IPV4;
		else if (IN6_IS_ADDR_V4MAPPED(&sin6p->sin6_addr)) {
			struct sockaddr_in sin;

			in6_sin6_2_sin(&sin, sin6p);
			inp->inp_vflag |= INP_IPV4;
			inp->inp_vflag &= ~INP_IPV6;
			error = in_pcbbind(inp, (struct sockaddr *)&sin, td);
			goto out;
		}
	}
	error = in6_pcbbind(inp, nam, td);
	if (error)
		goto out;
	COMMON_END(PRU_BIND);
}
#endif /* INET6 */

struct netmsg_inswildcard {
	struct netmsg_base	base;
	struct inpcb		*nm_inp;
};

static void
in_pcbinswildcardhash_handler(netmsg_t msg)
{
	struct netmsg_inswildcard *nm = (struct netmsg_inswildcard *)msg;
	int cpu = mycpuid, nextcpu;

	in_pcbinswildcardhash_oncpu(nm->nm_inp, &tcbinfo[cpu]);

	nextcpu = cpu + 1;
	if (nextcpu < ncpus2)
		lwkt_forwardmsg(netisr_cpuport(nextcpu), &nm->base.lmsg);
	else
		lwkt_replymsg(&nm->base.lmsg, 0);
}

/*
 * Prepare to accept connections.
 */
static void
tcp_usr_listen(netmsg_t msg)
{
	struct socket *so = msg->listen.base.nm_so;
	struct thread *td = msg->listen.nm_td;
	int error = 0;
	struct inpcb *inp;
	struct tcpcb *tp;
	struct netmsg_inswildcard nm;

	COMMON_START(so, inp, 0);

	if (tp->t_flags & TF_LISTEN)
		goto out;

	if (inp->inp_lport == 0) {
		error = in_pcbbind(inp, NULL, td);
		if (error)
			goto out;
	}

	tp->t_state = TCPS_LISTEN;
	tp->t_flags |= TF_LISTEN;
	tp->tt_msg = NULL; /* Catch any invalid timer usage */

	if (ncpus > 1) {
		/*
		 * We have to set the flag because we can't have other cpus
		 * messing with our inp's flags.
		 */
		KASSERT(!(inp->inp_flags & INP_CONNECTED),
			("already on connhash"));
		KASSERT(!(inp->inp_flags & INP_WILDCARD),
			("already on wildcardhash"));
		KASSERT(!(inp->inp_flags & INP_WILDCARD_MP),
			("already on MP wildcardhash"));
		inp->inp_flags |= INP_WILDCARD_MP;

		KKASSERT(so->so_port == netisr_cpuport(0));
		KKASSERT(&curthread->td_msgport == netisr_cpuport(0));
		KKASSERT(inp->inp_pcbinfo == &tcbinfo[0]);

		netmsg_init(&nm.base, NULL, &curthread->td_msgport,
			    MSGF_PRIORITY, in_pcbinswildcardhash_handler);
		nm.nm_inp = inp;
		lwkt_domsg(netisr_cpuport(1), &nm.base.lmsg, 0);
	}
	in_pcbinswildcardhash(inp);
	COMMON_END(PRU_LISTEN);
}

#ifdef INET6

static void
tcp6_usr_listen(netmsg_t msg)
{
	struct socket *so = msg->listen.base.nm_so;
	struct thread *td = msg->listen.nm_td;
	int error = 0;
	struct inpcb *inp;
	struct tcpcb *tp;
	struct netmsg_inswildcard nm;

	COMMON_START(so, inp, 0);

	if (tp->t_flags & TF_LISTEN)
		goto out;

	if (inp->inp_lport == 0) {
		if (!(inp->inp_flags & IN6P_IPV6_V6ONLY))
			inp->inp_vflag |= INP_IPV4;
		else
			inp->inp_vflag &= ~INP_IPV4;
		error = in6_pcbbind(inp, NULL, td);
		if (error)
			goto out;
	}

	tp->t_state = TCPS_LISTEN;
	tp->t_flags |= TF_LISTEN;
	tp->tt_msg = NULL; /* Catch any invalid timer usage */

	if (ncpus > 1) {
		/*
		 * We have to set the flag because we can't have other cpus
		 * messing with our inp's flags.
		 */
		KASSERT(!(inp->inp_flags & INP_CONNECTED),
			("already on connhash"));
		KASSERT(!(inp->inp_flags & INP_WILDCARD),
			("already on wildcardhash"));
		KASSERT(!(inp->inp_flags & INP_WILDCARD_MP),
			("already on MP wildcardhash"));
		inp->inp_flags |= INP_WILDCARD_MP;

		KKASSERT(so->so_port == netisr_cpuport(0));
		KKASSERT(&curthread->td_msgport == netisr_cpuport(0));
		KKASSERT(inp->inp_pcbinfo == &tcbinfo[0]);

		netmsg_init(&nm.base, NULL, &curthread->td_msgport,
			    MSGF_PRIORITY, in_pcbinswildcardhash_handler);
		nm.nm_inp = inp;
		lwkt_domsg(netisr_cpuport(1), &nm.base.lmsg, 0);
	}
	in_pcbinswildcardhash(inp);
	COMMON_END(PRU_LISTEN);
}
#endif /* INET6 */

/*
 * Initiate connection to peer.
 * Create a template for use in transmissions on this connection.
 * Enter SYN_SENT state, and mark socket as connecting.
 * Start keep-alive timer, and seed output sequence space.
 * Send initial segment on connection.
 */
static void
tcp_usr_connect(netmsg_t msg)
{
	struct socket *so = msg->connect.base.nm_so;
	struct sockaddr *nam = msg->connect.nm_nam;
	struct thread *td = msg->connect.nm_td;
	int error = 0;
	struct inpcb *inp;
	struct tcpcb *tp;
	struct sockaddr_in *sinp;

	COMMON_START(so, inp, 0);

	/*
	 * Must disallow TCP ``connections'' to multicast addresses.
	 */
	sinp = (struct sockaddr_in *)nam;
	if (sinp->sin_family == AF_INET
	    && IN_MULTICAST(ntohl(sinp->sin_addr.s_addr))) {
		error = EAFNOSUPPORT;
		goto out;
	}

	if (!prison_remote_ip(td, (struct sockaddr*)sinp)) {
		error = EAFNOSUPPORT; /* IPv6 only jail */
		goto out;
	}

	tcp_connect(msg);
	/* msg is invalid now */
	return;
out:
	if (msg->connect.nm_m) {
		m_freem(msg->connect.nm_m);
		msg->connect.nm_m = NULL;
	}
	if (msg->connect.nm_flags & PRUC_HELDTD)
		lwkt_rele(td);
	if (error && (msg->connect.nm_flags & PRUC_ASYNC)) {
		so->so_error = error;
		soisdisconnected(so);
	}
	lwkt_replymsg(&msg->lmsg, error);
}

#ifdef INET6

static void
tcp6_usr_connect(netmsg_t msg)
{
	struct socket *so = msg->connect.base.nm_so;
	struct sockaddr *nam = msg->connect.nm_nam;
	struct thread *td = msg->connect.nm_td;
	int error = 0;
	struct inpcb *inp;
	struct tcpcb *tp;
	struct sockaddr_in6 *sin6p;

	COMMON_START(so, inp, 0);

	/*
	 * Must disallow TCP ``connections'' to multicast addresses.
	 */
	sin6p = (struct sockaddr_in6 *)nam;
	if (sin6p->sin6_family == AF_INET6
	    && IN6_IS_ADDR_MULTICAST(&sin6p->sin6_addr)) {
		error = EAFNOSUPPORT;
		goto out;
	}

	if (!prison_remote_ip(td, nam)) {
		error = EAFNOSUPPORT; /* IPv4 only jail */
		goto out;
	}

	if (IN6_IS_ADDR_V4MAPPED(&sin6p->sin6_addr)) {
		struct sockaddr_in *sinp;

		if ((inp->inp_flags & IN6P_IPV6_V6ONLY) != 0) {
			error = EINVAL;
			goto out;
		}
		sinp = kmalloc(sizeof(*sinp), M_LWKTMSG, M_INTWAIT);
		in6_sin6_2_sin(sinp, sin6p);
		inp->inp_vflag |= INP_IPV4;
		inp->inp_vflag &= ~INP_IPV6;
		msg->connect.nm_nam = (struct sockaddr *)sinp;
		msg->connect.nm_flags |= PRUC_NAMALLOC;
		tcp_connect(msg);
		/* msg is invalid now */
		return;
	}
	inp->inp_vflag &= ~INP_IPV4;
	inp->inp_vflag |= INP_IPV6;
	inp->inp_inc.inc_isipv6 = 1;

	msg->connect.nm_flags |= PRUC_FALLBACK;
	tcp6_connect(msg);
	/* msg is invalid now */
	return;
out:
	if (msg->connect.nm_m) {
		m_freem(msg->connect.nm_m);
		msg->connect.nm_m = NULL;
	}
	lwkt_replymsg(&msg->lmsg, error);
}

#endif /* INET6 */

/*
 * Initiate disconnect from peer.
 * If connection never passed embryonic stage, just drop;
 * else if don't need to let data drain, then can just drop anyways,
 * else have to begin TCP shutdown process: mark socket disconnecting,
 * drain unread data, state switch to reflect user close, and
 * send segment (e.g. FIN) to peer.  Socket will be really disconnected
 * when peer sends FIN and acks ours.
 *
 * SHOULD IMPLEMENT LATER PRU_CONNECT VIA REALLOC TCPCB.
 */
static void
tcp_usr_disconnect(netmsg_t msg)
{
	struct socket *so = msg->disconnect.base.nm_so;
	int error = 0;
	struct inpcb *inp;
	struct tcpcb *tp;

	COMMON_START(so, inp, 1);
	tp = tcp_disconnect(tp);
	COMMON_END(PRU_DISCONNECT);
}

/*
 * Accept a connection.  Essentially all the work is
 * done at higher levels; just return the address
 * of the peer, storing through addr.
 */
static void
tcp_usr_accept(netmsg_t msg)
{
	struct socket *so = msg->accept.base.nm_so;
	struct sockaddr **nam = msg->accept.nm_nam;
	int error = 0;
	struct inpcb *inp;
	struct tcpcb *tp = NULL;
	TCPDEBUG0;

	inp = so->so_pcb;
	if (so->so_state & SS_ISDISCONNECTED) {
		error = ECONNABORTED;
		goto out;
	}
	if (inp == 0) {
		error = EINVAL;
		goto out;
	}

	tp = intotcpcb(inp);
	TCPDEBUG1();
	in_setpeeraddr(so, nam);
	COMMON_END(PRU_ACCEPT);
}

#ifdef INET6
static void
tcp6_usr_accept(netmsg_t msg)
{
	struct socket *so = msg->accept.base.nm_so;
	struct sockaddr **nam = msg->accept.nm_nam;
	int error = 0;
	struct inpcb *inp;
	struct tcpcb *tp = NULL;
	TCPDEBUG0;

	inp = so->so_pcb;

	if (so->so_state & SS_ISDISCONNECTED) {
		error = ECONNABORTED;
		goto out;
	}
	if (inp == 0) {
		error = EINVAL;
		goto out;
	}
	tp = intotcpcb(inp);
	TCPDEBUG1();
	in6_mapped_peeraddr(so, nam);
	COMMON_END(PRU_ACCEPT);
}
#endif /* INET6 */
/*
 * Mark the connection as being incapable of further output.
 */
static void
tcp_usr_shutdown(netmsg_t msg)
{
	struct socket *so = msg->shutdown.base.nm_so;
	int error = 0;
	struct inpcb *inp;
	struct tcpcb *tp;

	COMMON_START(so, inp, 0);
	socantsendmore(so);
	tp = tcp_usrclosed(tp);
	if (tp)
		error = tcp_output(tp);
	COMMON_END(PRU_SHUTDOWN);
}

/*
 * After a receive, possibly send window update to peer.
 */
static void
tcp_usr_rcvd(netmsg_t msg)
{
	struct socket *so = msg->rcvd.base.nm_so;
	int error = 0, noreply = 0;
	struct inpcb *inp;
	struct tcpcb *tp;

	COMMON_START(so, inp, 0);

	if (msg->rcvd.nm_pru_flags & PRUR_ASYNC) {
		noreply = 1;
		so_async_rcvd_reply(so);
	}
	tcp_output(tp);

	COMMON_END1(PRU_RCVD, noreply);
}

/*
 * Do a send by putting data in output queue and updating urgent
 * marker if URG set.  Possibly send more data.  Unlike the other
 * pru_*() routines, the mbuf chains are our responsibility.  We
 * must either enqueue them or free them.  The other pru_* routines
 * generally are caller-frees.
 */
static void
tcp_usr_send(netmsg_t msg)
{
	struct socket *so = msg->send.base.nm_so;
	int flags = msg->send.nm_flags;
	struct mbuf *m = msg->send.nm_m;
	int error = 0;
	struct inpcb *inp;
	struct tcpcb *tp;
	TCPDEBUG0;

	KKASSERT(msg->send.nm_control == NULL);
	KKASSERT(msg->send.nm_addr == NULL);
	KKASSERT((flags & PRUS_FREEADDR) == 0);

	inp = so->so_pcb;

	if (inp == NULL) {
		/*
		 * OOPS! we lost a race, the TCP session got reset after
		 * we checked SS_CANTSENDMORE, eg: while doing uiomove or a
		 * network interrupt in the non-critical section of sosend().
		 */
		m_freem(m);
		error = ECONNRESET;	/* XXX EPIPE? */
		tp = NULL;
		TCPDEBUG1();
		goto out;
	}
	tp = intotcpcb(inp);
	TCPDEBUG1();

#ifdef foo
	/*
	 * This is no longer necessary, since:
	 * - sosendtcp() has already checked it for us
	 * - It does not work with asynchronized send
	 */

	/*
	 * Don't let too much OOB data build up
	 */
	if (flags & PRUS_OOB) {
		if (ssb_space(&so->so_snd) < -512) {
			m_freem(m);
			error = ENOBUFS;
			goto out;
		}
	}
#endif

	/*
	 * Pump the data into the socket.
	 */
	if (m)
		ssb_appendstream(&so->so_snd, m);
	if (flags & PRUS_OOB) {
		/*
		 * According to RFC961 (Assigned Protocols),
		 * the urgent pointer points to the last octet
		 * of urgent data.  We continue, however,
		 * to consider it to indicate the first octet
		 * of data past the urgent section.
		 * Otherwise, snd_up should be one lower.
		 */
		tp->snd_up = tp->snd_una + so->so_snd.ssb_cc;
		tp->t_flags |= TF_FORCE;
		error = tcp_output(tp);
		tp->t_flags &= ~TF_FORCE;
	} else {
		if (flags & PRUS_EOF) {
			/*
			 * Close the send side of the connection after
			 * the data is sent.
			 */
			socantsendmore(so);
			tp = tcp_usrclosed(tp);
		}
		if (tp != NULL && !tcp_output_pending(tp)) {
			if (flags & PRUS_MORETOCOME)
				tp->t_flags |= TF_MORETOCOME;
			error = tcp_output_fair(tp);
			if (flags & PRUS_MORETOCOME)
				tp->t_flags &= ~TF_MORETOCOME;
		}
	}
	COMMON_END1((flags & PRUS_OOB) ? PRU_SENDOOB :
		   ((flags & PRUS_EOF) ? PRU_SEND_EOF : PRU_SEND),
		   (flags & PRUS_NOREPLY));
}

/*
 * NOTE: (so) is referenced from soabort*() and netmsg_pru_abort()
 *	 will sofree() it when we return.
 */
static void
tcp_usr_abort(netmsg_t msg)
{
	struct socket *so = msg->abort.base.nm_so;
	int error = 0;
	struct inpcb *inp;
	struct tcpcb *tp;

	COMMON_START(so, inp, 1);
	tp = tcp_drop(tp, ECONNABORTED);
	COMMON_END(PRU_ABORT);
}

/*
 * Receive out-of-band data.
 */
static void
tcp_usr_rcvoob(netmsg_t msg)
{
	struct socket *so = msg->rcvoob.base.nm_so;
	struct mbuf *m = msg->rcvoob.nm_m;
	int flags = msg->rcvoob.nm_flags;
	int error = 0;
	struct inpcb *inp;
	struct tcpcb *tp;

	COMMON_START(so, inp, 0);
	if ((so->so_oobmark == 0 &&
	     (so->so_state & SS_RCVATMARK) == 0) ||
	    so->so_options & SO_OOBINLINE ||
	    tp->t_oobflags & TCPOOB_HADDATA) {
		error = EINVAL;
		goto out;
	}
	if ((tp->t_oobflags & TCPOOB_HAVEDATA) == 0) {
		error = EWOULDBLOCK;
		goto out;
	}
	m->m_len = 1;
	*mtod(m, caddr_t) = tp->t_iobc;
	if ((flags & MSG_PEEK) == 0)
		tp->t_oobflags ^= (TCPOOB_HAVEDATA | TCPOOB_HADDATA);
	COMMON_END(PRU_RCVOOB);
}

static void
tcp_usr_savefaddr(struct socket *so, const struct sockaddr *faddr)
{
	in_savefaddr(so, faddr);
}

#ifdef INET6
static void
tcp6_usr_savefaddr(struct socket *so, const struct sockaddr *faddr)
{
	in6_mapped_savefaddr(so, faddr);
}
#endif

static int
tcp_usr_preconnect(struct socket *so, const struct sockaddr *nam,
    struct thread *td __unused)
{
	const struct sockaddr_in *sinp;

	sinp = (const struct sockaddr_in *)nam;
	if (sinp->sin_family == AF_INET &&
	    IN_MULTICAST(ntohl(sinp->sin_addr.s_addr)))
		return EAFNOSUPPORT;

	soisconnecting(so);
	return 0;
}

/* xxx - should be const */
struct pr_usrreqs tcp_usrreqs = {
	.pru_abort = tcp_usr_abort,
	.pru_accept = tcp_usr_accept,
	.pru_attach = tcp_usr_attach,
	.pru_bind = tcp_usr_bind,
	.pru_connect = tcp_usr_connect,
	.pru_connect2 = pr_generic_notsupp,
	.pru_control = in_control_dispatch,
	.pru_detach = tcp_usr_detach,
	.pru_disconnect = tcp_usr_disconnect,
	.pru_listen = tcp_usr_listen,
	.pru_peeraddr = in_setpeeraddr_dispatch,
	.pru_rcvd = tcp_usr_rcvd,
	.pru_rcvoob = tcp_usr_rcvoob,
	.pru_send = tcp_usr_send,
	.pru_sense = pru_sense_null,
	.pru_shutdown = tcp_usr_shutdown,
	.pru_sockaddr = in_setsockaddr_dispatch,
	.pru_sosend = sosendtcp,
	.pru_soreceive = sorecvtcp,
	.pru_savefaddr = tcp_usr_savefaddr,
	.pru_preconnect = tcp_usr_preconnect
};

#ifdef INET6
struct pr_usrreqs tcp6_usrreqs = {
	.pru_abort = tcp_usr_abort,
	.pru_accept = tcp6_usr_accept,
	.pru_attach = tcp_usr_attach,
	.pru_bind = tcp6_usr_bind,
	.pru_connect = tcp6_usr_connect,
	.pru_connect2 = pr_generic_notsupp,
	.pru_control = in6_control_dispatch,
	.pru_detach = tcp_usr_detach,
	.pru_disconnect = tcp_usr_disconnect,
	.pru_listen = tcp6_usr_listen,
	.pru_peeraddr = in6_mapped_peeraddr_dispatch,
	.pru_rcvd = tcp_usr_rcvd,
	.pru_rcvoob = tcp_usr_rcvoob,
	.pru_send = tcp_usr_send,
	.pru_sense = pru_sense_null,
	.pru_shutdown = tcp_usr_shutdown,
	.pru_sockaddr = in6_mapped_sockaddr_dispatch,
	.pru_sosend = sosendtcp,
	.pru_soreceive = sorecvtcp,
	.pru_savefaddr = tcp6_usr_savefaddr
};
#endif /* INET6 */

static int
tcp_connect_oncpu(struct tcpcb *tp, int flags, struct mbuf *m,
		  struct sockaddr_in *sin, struct sockaddr_in *if_sin)
{
	struct inpcb *inp = tp->t_inpcb, *oinp;
	struct socket *so = inp->inp_socket;
	struct route *ro = &inp->inp_route;

	oinp = in_pcblookup_hash(&tcbinfo[mycpu->gd_cpuid],
				 sin->sin_addr, sin->sin_port,
				 (inp->inp_laddr.s_addr != INADDR_ANY ?
				  inp->inp_laddr : if_sin->sin_addr),
				inp->inp_lport, 0, NULL);
	if (oinp != NULL) {
		m_freem(m);
		return (EADDRINUSE);
	}
	if (inp->inp_laddr.s_addr == INADDR_ANY)
		inp->inp_laddr = if_sin->sin_addr;
	inp->inp_faddr = sin->sin_addr;
	inp->inp_fport = sin->sin_port;
	inp->inp_cpcbinfo = &tcbinfo[mycpu->gd_cpuid];
	in_pcbinsconnhash(inp);

	/*
	 * We are now on the inpcb's owner CPU, if the cached route was
	 * freed because the rtentry's owner CPU is not the current CPU
	 * (e.g. in tcp_connect()), then we try to reallocate it here with
	 * the hope that a rtentry may be cloned from a RTF_PRCLONING
	 * rtentry.
	 */
	if (!(inp->inp_socket->so_options & SO_DONTROUTE) && /*XXX*/
	    ro->ro_rt == NULL) {
		bzero(&ro->ro_dst, sizeof(struct sockaddr_in));
		ro->ro_dst.sa_family = AF_INET;
		ro->ro_dst.sa_len = sizeof(struct sockaddr_in);
		((struct sockaddr_in *)&ro->ro_dst)->sin_addr =
			sin->sin_addr;
		rtalloc(ro);
	}

	/*
	 * Now that no more errors can occur, change the protocol processing
	 * port to the current thread (which is the correct thread).
	 *
	 * Create TCP timer message now; we are on the tcpcb's owner
	 * CPU/thread.
	 */
	tcp_create_timermsg(tp, &curthread->td_msgport);

	/*
	 * Compute window scaling to request.  Use a larger scaling then
	 * needed for the initial receive buffer in case the receive buffer
	 * gets expanded.
	 */
	if (tp->request_r_scale < TCP_MIN_WINSHIFT)
		tp->request_r_scale = TCP_MIN_WINSHIFT;
	while (tp->request_r_scale < TCP_MAX_WINSHIFT &&
	       (TCP_MAXWIN << tp->request_r_scale) < so->so_rcv.ssb_hiwat
	) {
		tp->request_r_scale++;
	}

	soisconnecting(so);
	tcpstat.tcps_connattempt++;
	tp->t_state = TCPS_SYN_SENT;
	tcp_callout_reset(tp, tp->tt_keep, tp->t_keepinit, tcp_timer_keep);
	tp->iss = tcp_new_isn(tp);
	tcp_sendseqinit(tp);
	if (m) {
		ssb_appendstream(&so->so_snd, m);
		m = NULL;
		if (flags & PRUS_OOB)
			tp->snd_up = tp->snd_una + so->so_snd.ssb_cc;
	}

	/*
	 * Close the send side of the connection after
	 * the data is sent if flagged.
	 */
	if ((flags & (PRUS_OOB|PRUS_EOF)) == PRUS_EOF) {
		socantsendmore(so);
		tp = tcp_usrclosed(tp);
	}
	return (tcp_output(tp));
}

/*
 * Common subroutine to open a TCP connection to remote host specified
 * by struct sockaddr_in in mbuf *nam.  Call in_pcbbind to assign a local
 * port number if needed.  Call in_pcbladdr to do the routing and to choose
 * a local host address (interface).
 * Initialize connection parameters and enter SYN-SENT state.
 */
static void
tcp_connect(netmsg_t msg)
{
	struct socket *so = msg->connect.base.nm_so;
	struct sockaddr *nam = msg->connect.nm_nam;
	struct thread *td = msg->connect.nm_td;
	struct sockaddr_in *sin = (struct sockaddr_in *)nam;
	struct sockaddr_in *if_sin;
	struct inpcb *inp;
	struct tcpcb *tp;
	int error, calc_laddr = 1;
	lwkt_port_t port;

	COMMON_START(so, inp, 0);

	/*
	 * Reconnect our pcb if we have to
	 */
	if (msg->connect.nm_flags & PRUC_RECONNECT) {
		msg->connect.nm_flags &= ~PRUC_RECONNECT;
		in_pcblink(so->so_pcb, &tcbinfo[mycpu->gd_cpuid]);
	}

	/*
	 * Bind if we have to
	 */
	if (inp->inp_lport == 0) {
		if (tcp_lport_extension) {
			KKASSERT(inp->inp_laddr.s_addr == INADDR_ANY);

			error = in_pcbladdr(inp, nam, &if_sin, td);
			if (error)
				goto out;
			inp->inp_laddr.s_addr = if_sin->sin_addr.s_addr;

			error = in_pcbconn_bind(inp, nam, td);
			if (error)
				goto out;

			calc_laddr = 0;
		} else {
			error = in_pcbbind(inp, NULL, td);
			if (error)
				goto out;
		}
	}

	if (calc_laddr) {
		/*
		 * Calculate the correct protocol processing thread.  The
		 * connect operation must run there.  Set the forwarding
		 * port before we forward the message or it will get bounced
		 * right back to us.
		 */
		error = in_pcbladdr(inp, nam, &if_sin, td);
		if (error)
			goto out;
	}
	KKASSERT(inp->inp_socket == so);

	port = tcp_addrport(sin->sin_addr.s_addr, sin->sin_port,
			    (inp->inp_laddr.s_addr ?
			     inp->inp_laddr.s_addr : if_sin->sin_addr.s_addr),
			    inp->inp_lport);

	if (port != &curthread->td_msgport) {
		struct route *ro = &inp->inp_route;

		/*
		 * in_pcbladdr() may have allocated a route entry for us
		 * on the current CPU, but we need a route entry on the
		 * inpcb's owner CPU, so free it here.
		 */
		if (ro->ro_rt != NULL)
			RTFREE(ro->ro_rt);
		bzero(ro, sizeof(*ro));

		/*
		 * We are moving the protocol processing port the socket
		 * is on, we have to unlink here and re-link on the
		 * target cpu.
		 */
		in_pcbunlink(so->so_pcb, &tcbinfo[mycpu->gd_cpuid]);
		sosetport(so, port);
		msg->connect.nm_flags |= PRUC_RECONNECT;
		msg->connect.base.nm_dispatch = tcp_connect;

		lwkt_forwardmsg(port, &msg->connect.base.lmsg);
		/* msg invalid now */
		return;
	} else if (msg->connect.nm_flags & PRUC_HELDTD) {
		/*
		 * The original thread is no longer needed; release it.
		 */
		lwkt_rele(td);
		msg->connect.nm_flags &= ~PRUC_HELDTD;
	}
	error = tcp_connect_oncpu(tp, msg->connect.nm_sndflags,
				  msg->connect.nm_m, sin, if_sin);
	msg->connect.nm_m = NULL;
out:
	if (msg->connect.nm_m) {
		m_freem(msg->connect.nm_m);
		msg->connect.nm_m = NULL;
	}
	if (msg->connect.nm_flags & PRUC_NAMALLOC) {
		kfree(msg->connect.nm_nam, M_LWKTMSG);
		msg->connect.nm_nam = NULL;
	}
	if (msg->connect.nm_flags & PRUC_HELDTD)
		lwkt_rele(td);
	if (error && (msg->connect.nm_flags & PRUC_ASYNC)) {
		so->so_error = error;
		soisdisconnected(so);
	}
	lwkt_replymsg(&msg->connect.base.lmsg, error);
	/* msg invalid now */
}

#ifdef INET6

static void
tcp6_connect(netmsg_t msg)
{
	struct tcpcb *tp;
	struct socket *so = msg->connect.base.nm_so;
	struct sockaddr *nam = msg->connect.nm_nam;
	struct thread *td = msg->connect.nm_td;
	struct inpcb *inp;
	struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *)nam;
	struct in6_addr *addr6;
	lwkt_port_t port;
	int error;

	COMMON_START(so, inp, 0);

	/*
	 * Reconnect our pcb if we have to
	 */
	if (msg->connect.nm_flags & PRUC_RECONNECT) {
		msg->connect.nm_flags &= ~PRUC_RECONNECT;
		in_pcblink(so->so_pcb, &tcbinfo[mycpu->gd_cpuid]);
	}

	/*
	 * Bind if we have to
	 */
	if (inp->inp_lport == 0) {
		error = in6_pcbbind(inp, NULL, td);
		if (error)
			goto out;
	}

	/*
	 * Cannot simply call in_pcbconnect, because there might be an
	 * earlier incarnation of this same connection still in
	 * TIME_WAIT state, creating an ADDRINUSE error.
	 */
	error = in6_pcbladdr(inp, nam, &addr6, td);
	if (error)
		goto out;

	port = tcp6_addrport();	/* XXX hack for now, always cpu0 */

	if (port != &curthread->td_msgport) {
		struct route *ro = &inp->inp_route;

		/*
		 * in_pcbladdr() may have allocated a route entry for us
		 * on the current CPU, but we need a route entry on the
		 * inpcb's owner CPU, so free it here.
		 */
		if (ro->ro_rt != NULL)
			RTFREE(ro->ro_rt);
		bzero(ro, sizeof(*ro));

		in_pcbunlink(so->so_pcb, &tcbinfo[mycpu->gd_cpuid]);
		sosetport(so, port);
		msg->connect.nm_flags |= PRUC_RECONNECT;
		msg->connect.base.nm_dispatch = tcp6_connect;

		lwkt_forwardmsg(port, &msg->connect.base.lmsg);
		/* msg invalid now */
		return;
	}
	error = tcp6_connect_oncpu(tp, msg->connect.nm_sndflags,
				   &msg->connect.nm_m, sin6, addr6);
	/* nm_m may still be intact */
out:
	if (error && (msg->connect.nm_flags & PRUC_FALLBACK)) {
		tcp_connect(msg);
		/* msg invalid now */
	} else {
		if (msg->connect.nm_m) {
			m_freem(msg->connect.nm_m);
			msg->connect.nm_m = NULL;
		}
		if (msg->connect.nm_flags & PRUC_NAMALLOC) {
			kfree(msg->connect.nm_nam, M_LWKTMSG);
			msg->connect.nm_nam = NULL;
		}
		lwkt_replymsg(&msg->connect.base.lmsg, error);
		/* msg invalid now */
	}
}

static int
tcp6_connect_oncpu(struct tcpcb *tp, int flags, struct mbuf **mp,
		   struct sockaddr_in6 *sin6, struct in6_addr *addr6)
{
	struct mbuf *m = *mp;
	struct inpcb *inp = tp->t_inpcb;
	struct socket *so = inp->inp_socket;
	struct inpcb *oinp;

	/*
	 * Cannot simply call in_pcbconnect, because there might be an
	 * earlier incarnation of this same connection still in
	 * TIME_WAIT state, creating an ADDRINUSE error.
	 */
	oinp = in6_pcblookup_hash(inp->inp_cpcbinfo,
				  &sin6->sin6_addr, sin6->sin6_port,
				  (IN6_IS_ADDR_UNSPECIFIED(&inp->in6p_laddr) ?
				      addr6 : &inp->in6p_laddr),
				  inp->inp_lport,  0, NULL);
	if (oinp)
		return (EADDRINUSE);

	if (IN6_IS_ADDR_UNSPECIFIED(&inp->in6p_laddr))
		inp->in6p_laddr = *addr6;
	inp->in6p_faddr = sin6->sin6_addr;
	inp->inp_fport = sin6->sin6_port;
	if ((sin6->sin6_flowinfo & IPV6_FLOWINFO_MASK) != 0)
		inp->in6p_flowinfo = sin6->sin6_flowinfo;
	in_pcbinsconnhash(inp);

	/*
	 * Now that no more errors can occur, change the protocol processing
	 * port to the current thread (which is the correct thread).
	 *
	 * Create TCP timer message now; we are on the tcpcb's owner
	 * CPU/thread.
	 */
	tcp_create_timermsg(tp, &curthread->td_msgport);

	/* Compute window scaling to request.  */
	if (tp->request_r_scale < TCP_MIN_WINSHIFT)
		tp->request_r_scale = TCP_MIN_WINSHIFT;
	while (tp->request_r_scale < TCP_MAX_WINSHIFT &&
	    (TCP_MAXWIN << tp->request_r_scale) < so->so_rcv.ssb_hiwat) {
		tp->request_r_scale++;
	}

	soisconnecting(so);
	tcpstat.tcps_connattempt++;
	tp->t_state = TCPS_SYN_SENT;
	tcp_callout_reset(tp, tp->tt_keep, tp->t_keepinit, tcp_timer_keep);
	tp->iss = tcp_new_isn(tp);
	tcp_sendseqinit(tp);
	if (m) {
		ssb_appendstream(&so->so_snd, m);
		*mp = NULL;
		if (flags & PRUS_OOB)
			tp->snd_up = tp->snd_una + so->so_snd.ssb_cc;
	}

	/*
	 * Close the send side of the connection after
	 * the data is sent if flagged.
	 */
	if ((flags & (PRUS_OOB|PRUS_EOF)) == PRUS_EOF) {
		socantsendmore(so);
		tp = tcp_usrclosed(tp);
	}
	return (tcp_output(tp));
}

#endif /* INET6 */

/*
 * The new sockopt interface makes it possible for us to block in the
 * copyin/out step (if we take a page fault).  Taking a page fault while
 * in a critical section is probably a Bad Thing.  (Since sockets and pcbs
 * both now use TSM, there probably isn't any need for this function to 
 * run in a critical section any more.  This needs more examination.)
 */
void
tcp_ctloutput(netmsg_t msg)
{
	struct socket *so = msg->base.nm_so;
	struct sockopt *sopt = msg->ctloutput.nm_sopt;
	int	error, opt, optval, opthz;
	struct	inpcb *inp;
	struct	tcpcb *tp;

	error = 0;
	inp = so->so_pcb;
	if (inp == NULL) {
		error = ECONNRESET;
		goto done;
	}

	if (sopt->sopt_level != IPPROTO_TCP) {
#ifdef INET6
		if (INP_CHECK_SOCKAF(so, AF_INET6))
			ip6_ctloutput_dispatch(msg);
		else
#endif /* INET6 */
		ip_ctloutput(msg);
		/* msg invalid now */
		return;
	}
	tp = intotcpcb(inp);

	switch (sopt->sopt_dir) {
	case SOPT_SET:
		error = soopt_to_kbuf(sopt, &optval, sizeof optval,
				      sizeof optval);
		if (error)
			break;
		switch (sopt->sopt_name) {
		case TCP_FASTKEEP:
			if (optval > 0)
				tp->t_keepidle = tp->t_keepintvl;
			else
				tp->t_keepidle = tcp_keepidle;
			tcp_timer_keep_activity(tp, 0);
			break;
#ifdef TCP_SIGNATURE
		case TCP_SIGNATURE_ENABLE:
			if (tp->t_state == TCPS_CLOSED) {
				/*
				 * This is the only safe state that this
				 * option could be changed.  Some segments
				 * could already have been sent in other
				 * states.
				 */
				if (optval > 0)
					tp->t_flags |= TF_SIGNATURE;
				else
					tp->t_flags &= ~TF_SIGNATURE;
			} else {
				error = EOPNOTSUPP;
			}
			break;
#endif /* TCP_SIGNATURE */
		case TCP_NODELAY:
		case TCP_NOOPT:
			switch (sopt->sopt_name) {
			case TCP_NODELAY:
				opt = TF_NODELAY;
				break;
			case TCP_NOOPT:
				opt = TF_NOOPT;
				break;
			default:
				opt = 0; /* dead code to fool gcc */
				break;
			}

			if (optval)
				tp->t_flags |= opt;
			else
				tp->t_flags &= ~opt;
			break;

		case TCP_NOPUSH:
			if (tcp_disable_nopush)
				break;
			if (optval)
				tp->t_flags |= TF_NOPUSH;
			else {
				tp->t_flags &= ~TF_NOPUSH;
				error = tcp_output(tp);
			}
			break;

		case TCP_MAXSEG:
			/*
			 * Must be between 0 and maxseg.  If the requested
			 * maxseg is too small to satisfy the desired minmss,
			 * pump it up (silently so sysctl modifications of
			 * minmss do not create unexpected program failures).
			 * Handle degenerate cases.
			 */
			if (optval > 0 && optval <= tp->t_maxseg) {
				if (optval + 40 < tcp_minmss) {
					optval = tcp_minmss - 40;
					if (optval < 0)
						optval = 1;
				}
				tp->t_maxseg = optval;
			} else {
				error = EINVAL;
			}
			break;

		case TCP_KEEPINIT:
			opthz = ((int64_t)optval * hz) / 1000;
			if (opthz >= 1)
				tp->t_keepinit = opthz;
			else
				error = EINVAL;
			break;

		case TCP_KEEPIDLE:
			opthz = ((int64_t)optval * hz) / 1000;
			if (opthz >= 1) {
				tp->t_keepidle = opthz;
				tcp_timer_keep_activity(tp, 0);
			} else {
				error = EINVAL;
			}
			break;

		case TCP_KEEPINTVL:
			opthz = ((int64_t)optval * hz) / 1000;
			if (opthz >= 1) {
				tp->t_keepintvl = opthz;
				tp->t_maxidle = tp->t_keepintvl * tp->t_keepcnt;
			} else {
				error = EINVAL;
			}
			break;

		case TCP_KEEPCNT:
			if (optval > 0) {
				tp->t_keepcnt = optval;
				tp->t_maxidle = tp->t_keepintvl * tp->t_keepcnt;
			} else {
				error = EINVAL;
			}
			break;

		default:
			error = ENOPROTOOPT;
			break;
		}
		break;

	case SOPT_GET:
		switch (sopt->sopt_name) {
#ifdef TCP_SIGNATURE
		case TCP_SIGNATURE_ENABLE:
			optval = (tp->t_flags & TF_SIGNATURE) ? 1 : 0;
			break;
#endif /* TCP_SIGNATURE */
		case TCP_NODELAY:
			optval = tp->t_flags & TF_NODELAY;
			break;
		case TCP_MAXSEG:
			optval = tp->t_maxseg;
			break;
		case TCP_NOOPT:
			optval = tp->t_flags & TF_NOOPT;
			break;
		case TCP_NOPUSH:
			optval = tp->t_flags & TF_NOPUSH;
			break;
		case TCP_KEEPINIT:
			optval = ((int64_t)tp->t_keepinit * 1000) / hz;
			break;
		case TCP_KEEPIDLE:
			optval = ((int64_t)tp->t_keepidle * 1000) / hz;
			break;
		case TCP_KEEPINTVL:
			optval = ((int64_t)tp->t_keepintvl * 1000) / hz;
			break;
		case TCP_KEEPCNT:
			optval = tp->t_keepcnt;
			break;
		default:
			error = ENOPROTOOPT;
			break;
		}
		if (error == 0)
			soopt_from_kbuf(sopt, &optval, sizeof optval);
		break;
	}
done:
	lwkt_replymsg(&msg->lmsg, error);
}

/*
 * tcp_sendspace and tcp_recvspace are the default send and receive window
 * sizes, respectively.  These are obsolescent (this information should
 * be set by the route).
 *
 * Use a default that does not require tcp window scaling to be turned
 * on.  Individual programs or the administrator can increase the default.
 */
u_long	tcp_sendspace = 57344;	/* largest multiple of PAGE_SIZE < 64k */
SYSCTL_INT(_net_inet_tcp, TCPCTL_SENDSPACE, sendspace, CTLFLAG_RW,
    &tcp_sendspace , 0, "Maximum outgoing TCP datagram size");
u_long	tcp_recvspace = 57344;	/* largest multiple of PAGE_SIZE < 64k */
SYSCTL_INT(_net_inet_tcp, TCPCTL_RECVSPACE, recvspace, CTLFLAG_RW,
    &tcp_recvspace , 0, "Maximum incoming TCP datagram size");

/*
 * Attach TCP protocol to socket, allocating internet protocol control
 * block, tcp control block, bufer space, and entering LISTEN state
 * if to accept connections.
 */
static int
tcp_attach(struct socket *so, struct pru_attach_info *ai)
{
	struct tcpcb *tp;
	struct inpcb *inp;
	int error;
	int cpu;
#ifdef INET6
	int isipv6 = INP_CHECK_SOCKAF(so, AF_INET6) != 0;
#endif

	if (so->so_snd.ssb_hiwat == 0 || so->so_rcv.ssb_hiwat == 0) {
		lwkt_gettoken(&so->so_rcv.ssb_token);
		error = soreserve(so, tcp_sendspace, tcp_recvspace,
				  ai->sb_rlimit);
		lwkt_reltoken(&so->so_rcv.ssb_token);
		if (error)
			return (error);
	}
	atomic_set_int(&so->so_rcv.ssb_flags, SSB_AUTOSIZE);
	atomic_set_int(&so->so_snd.ssb_flags, SSB_AUTOSIZE);
	cpu = mycpu->gd_cpuid;

	/*
	 * Set the default port for protocol processing. This will likely
	 * change when we connect.
	 */
	error = in_pcballoc(so, &tcbinfo[cpu]);
	if (error)
		return (error);
	inp = so->so_pcb;
#ifdef INET6
	if (isipv6) {
		inp->inp_vflag |= INP_IPV6;
		inp->in6p_hops = -1;	/* use kernel default */
	}
	else
#endif
	inp->inp_vflag |= INP_IPV4;
	tp = tcp_newtcpcb(inp);
	if (tp == NULL) {
		/*
		 * Make sure the socket is destroyed by the pcbdetach.
		 */
		soreference(so);
#ifdef INET6
		if (isipv6)
			in6_pcbdetach(inp);
		else
#endif
		in_pcbdetach(inp);
		sofree(so);	/* from ref above */
		return (ENOBUFS);
	}
	tp->t_state = TCPS_CLOSED;
	/* Keep a reference for asynchronized pru_rcvd */
	soreference(so);
	return (0);
}

/*
 * Initiate (or continue) disconnect.
 * If embryonic state, just send reset (once).
 * If in ``let data drain'' option and linger null, just drop.
 * Otherwise (hard), mark socket disconnecting and drop
 * current input data; switch states based on user close, and
 * send segment to peer (with FIN).
 */
static struct tcpcb *
tcp_disconnect(struct tcpcb *tp)
{
	struct socket *so = tp->t_inpcb->inp_socket;

	if (tp->t_state < TCPS_ESTABLISHED) {
		tp = tcp_close(tp);
	} else if ((so->so_options & SO_LINGER) && so->so_linger == 0) {
		tp = tcp_drop(tp, 0);
	} else {
		lwkt_gettoken(&so->so_rcv.ssb_token);
		soisdisconnecting(so);
		sbflush(&so->so_rcv.sb);
		tp = tcp_usrclosed(tp);
		if (tp)
			tcp_output(tp);
		lwkt_reltoken(&so->so_rcv.ssb_token);
	}
	return (tp);
}

/*
 * User issued close, and wish to trail through shutdown states:
 * if never received SYN, just forget it.  If got a SYN from peer,
 * but haven't sent FIN, then go to FIN_WAIT_1 state to send peer a FIN.
 * If already got a FIN from peer, then almost done; go to LAST_ACK
 * state.  In all other cases, have already sent FIN to peer (e.g.
 * after PRU_SHUTDOWN), and just have to play tedious game waiting
 * for peer to send FIN or not respond to keep-alives, etc.
 * We can let the user exit from the close as soon as the FIN is acked.
 */
static struct tcpcb *
tcp_usrclosed(struct tcpcb *tp)
{

	switch (tp->t_state) {

	case TCPS_CLOSED:
	case TCPS_LISTEN:
		tp->t_state = TCPS_CLOSED;
		tp = tcp_close(tp);
		break;

	case TCPS_SYN_SENT:
	case TCPS_SYN_RECEIVED:
		tp->t_flags |= TF_NEEDFIN;
		break;

	case TCPS_ESTABLISHED:
		tp->t_state = TCPS_FIN_WAIT_1;
		break;

	case TCPS_CLOSE_WAIT:
		tp->t_state = TCPS_LAST_ACK;
		break;
	}
	if (tp && tp->t_state >= TCPS_FIN_WAIT_2) {
		soisdisconnected(tp->t_inpcb->inp_socket);
		/* To prevent the connection hanging in FIN_WAIT_2 forever. */
		if (tp->t_state == TCPS_FIN_WAIT_2) {
			tcp_callout_reset(tp, tp->tt_2msl, tp->t_maxidle,
			    tcp_timer_2msl);
		}
	}
	return (tp);
}
