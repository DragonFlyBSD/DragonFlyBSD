/* $OpenBSD: l2cap_socket.c,v 1.1 2007/06/01 02:46:11 uwe Exp $ */
/* $NetBSD: l2cap_socket.c,v 1.7 2007/04/21 06:15:23 plunky Exp $ */

/*-
 * Copyright (c) 2005 Iain Hibbert.
 * Copyright (c) 2006 Itronix Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of Itronix Inc. may not be used to endorse
 *    or promote products derived from this software without specific
 *    prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY ITRONIX INC. ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL ITRONIX INC. BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/* load symbolic names */
#ifdef BLUETOOTH_DEBUG
#define PRUREQUESTS
#define PRCOREQUESTS
#endif

#include <sys/param.h>
#include <sys/domain.h>
#include <sys/kernel.h>
#include <sys/mbuf.h>
#include <sys/proc.h>
#include <sys/protosw.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/systm.h>

#include <sys/msgport2.h>

#include <vm/vm_zone.h>

#include <netbt/bluetooth.h>
#include <netbt/hci.h>		/* XXX for EPASSTHROUGH */
#include <netbt/l2cap.h>

/*
 * L2CAP Sockets
 *
 *	SOCK_SEQPACKET - normal L2CAP connection
 *
 *	SOCK_DGRAM - connectionless L2CAP - XXX not yet
 */

static void l2cap_connecting(void *);
static void l2cap_connected(void *);
static void l2cap_disconnected(void *, int);
static void *l2cap_newconn(void *, struct sockaddr_bt *, struct sockaddr_bt *);
static void l2cap_complete(void *, int);
static void l2cap_linkmode(void *, int);
static void l2cap_input(void *, struct mbuf *);

static const struct btproto l2cap_proto = {
	l2cap_connecting,
	l2cap_connected,
	l2cap_disconnected,
	l2cap_newconn,
	l2cap_complete,
	l2cap_linkmode,
	l2cap_input,
};

/* sysctl variables */
int l2cap_sendspace = 4096;
int l2cap_recvspace = 4096;

/*
 * l2cap_ctloutput(request, socket, level, optname, opt)
 *
 *	Apply configuration commands to channel. This corresponds to
 *	"Reconfigure Channel Request" in the L2CAP specification.
 */
void
l2cap_ctloutput(netmsg_t msg)
{
	struct socket *so = msg->ctloutput.base.nm_so;
	struct sockopt *sopt = msg->ctloutput.nm_sopt;
	struct l2cap_channel *pcb = (struct l2cap_channel *) so->so_pcb;
	struct mbuf *m;
	int error = 0;

#ifdef notyet			/* XXX */
	DPRINTFN(2, "%s\n", prcorequests[req]);
#endif

	if (pcb == NULL) {
		error = EINVAL;
		goto out;
	}

	if (sopt->sopt_level != BTPROTO_L2CAP) {
		error = ENOPROTOOPT;
		goto out;
	}

	switch(sopt->sopt_dir) {
	case PRCO_GETOPT:
		m = m_get(M_NOWAIT, MT_DATA);
		if (m == NULL) {
		    error = ENOMEM;
		    break;
		}
		m->m_len = l2cap_getopt(pcb, sopt->sopt_name, mtod(m, void *));
		if (m->m_len == 0) {
			m_freem(m);
			m = NULL;
			error = ENOPROTOOPT;
		}
		soopt_from_kbuf(sopt, mtod(m, void *), m->m_len);
		break;

	case PRCO_SETOPT:
		error = l2cap_setopt2(pcb, sopt->sopt_name, so, sopt);
		break;

	default:
		error = ENOPROTOOPT;
		break;
	}
out:
	lwkt_replymsg(&msg->ctloutput.base.lmsg, error);
}

/**********************************************************************
 *
 *	L2CAP Protocol socket callbacks
 *
 */

static void
l2cap_connecting(void *arg)
{
	struct socket *so = arg;

	DPRINTF("Connecting\n");
	soisconnecting(so);
}

static void
l2cap_connected(void *arg)
{
	struct socket *so = arg;

	DPRINTF("Connected\n");
	soisconnected(so);
}

static void
l2cap_disconnected(void *arg, int err)
{
	struct socket *so = arg;

	DPRINTF("Disconnected (%d)\n", err);

	so->so_error = err;
	soisdisconnected(so);
}

static void *
l2cap_newconn(void *arg, struct sockaddr_bt *laddr,
    struct sockaddr_bt *raddr)
{
	struct socket *so = arg;

	DPRINTF("New Connection\n");
	so = sonewconn(so, 0);
	if (so == NULL)
		return NULL;

	soisconnecting(so);

	return so->so_pcb;
}

static void
l2cap_complete(void *arg, int count)
{
	struct socket *so = arg;

	while (count-- > 0)
		sbdroprecord(&so->so_snd.sb);

	sowwakeup(so);
}

static void
l2cap_linkmode(void *arg, int new)
{
	struct socket *so = arg;
	int mode;

	DPRINTF("auth %s, encrypt %s, secure %s\n",
		(new & L2CAP_LM_AUTH ? "on" : "off"),
		(new & L2CAP_LM_ENCRYPT ? "on" : "off"),
		(new & L2CAP_LM_SECURE ? "on" : "off"));

	(void)l2cap_getopt(so->so_pcb, SO_L2CAP_LM, &mode);
	if (((mode & L2CAP_LM_AUTH) && !(new & L2CAP_LM_AUTH))
	    || ((mode & L2CAP_LM_ENCRYPT) && !(new & L2CAP_LM_ENCRYPT))
	    || ((mode & L2CAP_LM_SECURE) && !(new & L2CAP_LM_SECURE)))
		l2cap_disconnect(so->so_pcb, 0);
}

static void
l2cap_input(void *arg, struct mbuf *m)
{
	struct socket *so = arg;

	if (m->m_pkthdr.len > sbspace(&so->so_rcv)) {
		kprintf("%s: packet (%d bytes) dropped (socket buffer full)\n",
			__func__, m->m_pkthdr.len);
		m_freem(m);
		return;
	}

	DPRINTFN(10, "received %d bytes\n", m->m_pkthdr.len);

	sbappendrecord(&so->so_rcv.sb, m);
	sorwakeup(so);
}


/*
 * Implementation of usrreqs.
 */
static void
l2cap_sdetach(netmsg_t msg)
{
	struct socket *so = msg->detach.base.nm_so;
	int error;

	error = l2cap_detach((struct l2cap_channel **)&so->so_pcb);
	lwkt_replymsg(&msg->detach.base.lmsg, error);
}

/*
 * NOTE: (so) is referenced from soabort*() and netmsg_pru_abort()
 *	 will sofree() it when we return.
 */
static void
l2cap_sabort(netmsg_t msg)
{
	struct socket *so = msg->abort.base.nm_so;
	struct l2cap_channel *pcb = so->so_pcb;
	
	l2cap_disconnect(pcb, 0);
	soisdisconnected(so);
	
	l2cap_sdetach(msg);
	/* msg invalid now */
}

static void
l2cap_sdisconnect(netmsg_t msg)
{
	struct socket *so = msg->disconnect.base.nm_so;
	struct l2cap_channel *pcb = so->so_pcb;
	int error;
	
	soisdisconnecting(so);
	
	error = l2cap_disconnect(pcb, so->so_linger);
	lwkt_replymsg(&msg->disconnect.base.lmsg, error);
}

static void
l2cap_scontrol(netmsg_t msg)
{
	lwkt_replymsg(&msg->control.base.lmsg, EPASSTHROUGH);
}

static void
l2cap_sattach(netmsg_t msg)
{
	struct socket *so = msg->attach.base.nm_so;
	struct l2cap_channel *pcb = so->so_pcb;
	int error;

	if (pcb != NULL) {
		error = EINVAL;
		goto out;
	}

	/*
	 * For L2CAP socket PCB we just use an l2cap_channel structure
	 * since we have nothing to add..
	 */
	error = soreserve(so, l2cap_sendspace, l2cap_recvspace, NULL);
	if (error == 0) {
		error = l2cap_attach((struct l2cap_channel **)&so->so_pcb,
				     &l2cap_proto, so);
	}
out:
	lwkt_replymsg(&msg->attach.base.lmsg, error);
}

static void
l2cap_sbind (netmsg_t msg)
{
	struct socket *so = msg->bind.base.nm_so;
	struct sockaddr *nam = msg->bind.nm_nam;
	struct l2cap_channel *pcb = so->so_pcb;
	struct sockaddr_bt *sa;
	int error;

	KKASSERT(nam != NULL);
	sa = (struct sockaddr_bt *)nam;

	if (sa->bt_len != sizeof(struct sockaddr_bt)) {
		error = EINVAL;
	} else if (sa->bt_family != AF_BLUETOOTH) {
		error = EAFNOSUPPORT;
	} else {
		error = l2cap_bind(pcb, sa);
	}
	lwkt_replymsg(&msg->bind.base.lmsg, error);
}

static void
l2cap_sconnect(netmsg_t msg)
{
	struct socket *so = msg->connect.base.nm_so;
	struct sockaddr *nam = msg->connect.nm_nam;
	struct l2cap_channel *pcb = so->so_pcb;
	struct sockaddr_bt *sa;
	int error;

	KKASSERT(nam != NULL);
	sa = (struct sockaddr_bt *)nam;

	if (sa->bt_len != sizeof(struct sockaddr_bt)) {
		error = EINVAL;
	} else if (sa->bt_family != AF_BLUETOOTH) {
		error = EAFNOSUPPORT;
	} else {
		soisconnecting(so);
		error = l2cap_connect(pcb, sa);
	}
	lwkt_replymsg(&msg->connect.base.lmsg, error);
}

static void
l2cap_speeraddr(netmsg_t msg)
{
	struct socket *so = msg->peeraddr.base.nm_so;
	struct sockaddr **nam = msg->peeraddr.nm_nam;
	struct l2cap_channel *pcb = so->so_pcb;
	struct sockaddr_bt *sa, ssa;
	int error;

	sa = &ssa;
	bzero(sa, sizeof *sa);
	sa->bt_len = sizeof(struct sockaddr_bt);
	sa->bt_family = AF_BLUETOOTH;
	error = l2cap_peeraddr(pcb, sa);
	*nam = dup_sockaddr((struct sockaddr *)sa);

	lwkt_replymsg(&msg->peeraddr.base.lmsg, error);
}

static void
l2cap_ssockaddr(netmsg_t msg)
{
	struct socket *so = msg->sockaddr.base.nm_so;
	struct sockaddr **nam = msg->sockaddr.nm_nam;
	struct l2cap_channel *pcb = so->so_pcb;
	struct sockaddr_bt *sa, ssa;
	int error;

	sa = &ssa;
	bzero(sa, sizeof *sa);
	sa->bt_len = sizeof(struct sockaddr_bt);
	sa->bt_family = AF_BLUETOOTH;
	error = l2cap_sockaddr(pcb, sa);
	*nam = dup_sockaddr((struct sockaddr *)sa);

	lwkt_replymsg(&msg->sockaddr.base.lmsg, error);
}

static void
l2cap_sshutdown(netmsg_t msg)
{
	struct socket *so = msg->shutdown.base.nm_so;

	socantsendmore(so);

	lwkt_replymsg(&msg->shutdown.base.lmsg, 0);
}

static void
l2cap_ssend(netmsg_t msg)
{
	struct socket *so = msg->send.base.nm_so;
	struct mbuf *m = msg->send.nm_m;
	struct mbuf *control = msg->send.nm_control;
	struct l2cap_channel *pcb = so->so_pcb;
	struct mbuf *m0;
	int error;

	KKASSERT(m != NULL);
	if (m->m_pkthdr.len == 0) {
		error = 0;
		goto out;
	}

	if (m->m_pkthdr.len > pcb->lc_omtu) {
		error = EMSGSIZE;
		goto out;
	}

	m0 = m_copym(m, 0, M_COPYALL, M_NOWAIT);
	if (m0 == NULL) {
		error = ENOMEM;
		goto out;
	}

	m0 = m_copym(m, 0, M_COPYALL, M_NOWAIT);
	if (m0 == NULL) {
		error = ENOMEM;
		goto out;
	}

	/* no use for that */
	if (control) {
		m_freem(control);
		control = NULL;
	}
	sbappendrecord(&so->so_snd.sb, m);
	error = l2cap_send(pcb, m0);
	m = NULL;
out:
	if (m)
		m_freem(m);
	if (control)
		m_freem(control);

	lwkt_replymsg(&msg->send.base.lmsg, error);
}

static void
l2cap_saccept(netmsg_t msg)
{
	struct socket *so = msg->accept.base.nm_so;
	struct sockaddr **nam = msg->accept.nm_nam;
	struct l2cap_channel *pcb = so->so_pcb;
	struct sockaddr_bt sa;
	int error;
		
	KKASSERT(nam != NULL);
	
	bzero(&sa, sizeof (sa) );
	sa.bt_len = sizeof(struct sockaddr_bt);
	sa.bt_family = AF_BLUETOOTH;

	error = l2cap_peeraddr(pcb, &sa);
	*nam = dup_sockaddr((struct sockaddr *)&sa);

	lwkt_replymsg(&msg->accept.base.lmsg, error);
}

static void
l2cap_slisten(netmsg_t msg)
{
	struct socket *so = msg->listen.base.nm_so;
	struct l2cap_channel *pcb = so->so_pcb;
	int error;

	error = l2cap_listen(pcb);
	lwkt_replymsg(&msg->accept.base.lmsg, error);
}


struct pr_usrreqs l2cap_usrreqs = {
        .pru_abort = l2cap_sabort,
        .pru_accept = l2cap_saccept,
        .pru_attach = l2cap_sattach,
        .pru_bind = l2cap_sbind,
        .pru_connect = l2cap_sconnect,
        .pru_connect2 = pr_generic_notsupp,
        .pru_control = l2cap_scontrol,
        .pru_detach = l2cap_sdetach,
        .pru_disconnect = l2cap_sdisconnect,
        .pru_listen = l2cap_slisten,
        .pru_peeraddr = l2cap_speeraddr,
        .pru_rcvd = pr_generic_notsupp,
        .pru_rcvoob = pr_generic_notsupp,
        .pru_send = l2cap_ssend,
        .pru_sense = pru_sense_null,
        .pru_shutdown = l2cap_sshutdown,
        .pru_sockaddr = l2cap_ssockaddr,
        .pru_sosend = sosend,
        .pru_soreceive = soreceive
};
