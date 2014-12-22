/*-
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
 *
 * $OpenBSD: sco_socket.c,v 1.1 2007/06/01 02:46:12 uwe Exp $
 * $NetBSD: sco_socket.c,v 1.9 2007/04/21 06:15:23 plunky Exp $
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
#include <sys/bus.h>

#include <sys/msgport2.h>

#include <netbt/bluetooth.h>
#include <netbt/hci.h>
#include <netbt/sco.h>

/*******************************************************************************
 *
 * SCO SOCK_SEQPACKET sockets - low latency audio data
 */

static void sco_connecting(void *);
static void sco_connected(void *);
static void sco_disconnected(void *, int);
static void *sco_newconn(void *, struct sockaddr_bt *, struct sockaddr_bt *);
static void sco_complete(void *, int);
static void sco_linkmode(void *, int);
static void sco_input(void *, struct mbuf *);

static const struct btproto sco_proto = {
	sco_connecting,
	sco_connected,
	sco_disconnected,
	sco_newconn,
	sco_complete,
	sco_linkmode,
	sco_input,
};

int sco_sendspace = 4096;
int sco_recvspace = 4096;

/*
 * get/set socket options
 */
void
sco_ctloutput(netmsg_t msg)
{
	struct socket *so = msg->ctloutput.base.nm_so;
	struct sockopt *sopt = msg->ctloutput.nm_sopt;
	struct sco_pcb *pcb = (struct sco_pcb *)so->so_pcb;
	struct mbuf *m;
	int err = 0;

#ifdef notyet			/* XXX */
	DPRINTFN(2, "req %s\n", prcorequests[req]);
#endif

	if (pcb == NULL) {
		err = EINVAL;
		goto out;
	}

	if (sopt->sopt_level != BTPROTO_SCO) {
		err = ENOPROTOOPT;
		goto out;
	}

	switch(sopt->sopt_dir) {
	case PRCO_GETOPT:
		m = m_get(MB_WAIT, MT_DATA);
		m->m_len = sco_getopt(pcb, sopt->sopt_name, mtod(m, uint8_t *));
		if (m->m_len == 0) {
			m_freem(m);
			m = NULL;
			err = ENOPROTOOPT;
		}
		/* *opt = m; */
		/* XXX There are possible memory leaks (Griffin) */
		soopt_from_kbuf(sopt, mtod(m, void *), m->m_len);
		break;

	case PRCO_SETOPT:
		m = m_get(MB_WAIT, MT_DATA);
		KKASSERT(m != NULL);
		err = soopt_to_kbuf(sopt, mtod(m,void*), m->m_len, m->m_len); 

		if (m->m_len == 0) {
			m_freem(m);
			m = NULL;
			err = EIO;
		}

		err = sco_setopt(pcb, sopt->sopt_name, mtod(m, uint8_t *));
		m_freem(m);
		break;

	default:
		err = ENOPROTOOPT;
		break;
	}
out:
	lwkt_replymsg(&msg->ctloutput.base.lmsg, err);
}

/*****************************************************************************
 *
 *	SCO Protocol socket callbacks
 *
 */
static void
sco_connecting(void *arg)
{
	struct socket *so = arg;

	DPRINTF("Connecting\n");
	soisconnecting(so);
}

static void
sco_connected(void *arg)
{
	struct socket *so = arg;

	DPRINTF("Connected\n");
	soisconnected(so);
}

static void
sco_disconnected(void *arg, int err)
{
	struct socket *so = arg;

	DPRINTF("Disconnected (%d)\n", err);

	so->so_error = err;
	soisdisconnected(so);
}

static void *
sco_newconn(void *arg, struct sockaddr_bt *laddr,
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
sco_complete(void *arg, int num)
{
	struct socket *so = arg;

	while (num-- > 0)
		sbdroprecord(&so->so_snd.sb);

	sowwakeup(so);
}

static void
sco_linkmode(void *arg, int mode)
{
}

static void
sco_input(void *arg, struct mbuf *m)
{
	struct socket *so = arg;

	/*
	 * since this data is time sensitive, if the buffer
	 * is full we just dump data until the latest one
	 * will fit.
	 */

	while (m->m_pkthdr.len > sbspace(&so->so_rcv))
		sbdroprecord(&so->so_rcv.sb);

	DPRINTFN(10, "received %d bytes\n", m->m_pkthdr.len);

	sbappendrecord(&so->so_rcv.sb, m);
	sorwakeup(so);
}

/*
 * Implementation of usrreqs.
 */
static void
sco_sdetach(netmsg_t msg)
{
	struct socket *so = msg->detach.base.nm_so;
	int error;

	error = sco_detach((struct sco_pcb **)&so->so_pcb);
	lwkt_replymsg(&msg->detach.base.lmsg, error);
}

/*
 * NOTE: (so) is referenced from soabort*() and netmsg_pru_abort()
 *	 will sofree() it when we return.
 */
static void
sco_sabort (netmsg_t msg)
{
	struct socket *so = msg->abort.base.nm_so;
	struct sco_pcb *pcb = (struct sco_pcb *)so->so_pcb;

	sco_disconnect(pcb, 0);
	soisdisconnected(so);
	sco_sdetach(msg);
	/* msg invalid now */
}

static void
sco_sdisconnect (netmsg_t msg)
{
	struct socket *so = msg->abort.base.nm_so;
 	struct sco_pcb *pcb = (struct sco_pcb *)so->so_pcb;
	int error;

	soisdisconnecting(so);

	error = sco_disconnect(pcb, so->so_linger);
	lwkt_replymsg(&msg->disconnect.base.lmsg, error);
}

static void
sco_sattach(netmsg_t msg)
{
	struct socket *so = msg->attach.base.nm_so;
	struct sco_pcb *pcb = (struct sco_pcb *)so->so_pcb;
	int error;

	if (pcb) {
		error = EINVAL;
	} else {
		error = soreserve(so, sco_sendspace, sco_recvspace,NULL);
		if (error == 0) {
			error = sco_attach((struct sco_pcb **)&so->so_pcb,
					   &sco_proto, so);
		}
	}
	lwkt_replymsg(&msg->attach.base.lmsg, error);
}

static void
sco_sbind(netmsg_t msg)
{
	struct socket *so = msg->bind.base.nm_so;
	struct sockaddr *nam = msg->bind.nm_nam;
	struct sco_pcb *pcb = (struct sco_pcb *)so->so_pcb;
	struct sockaddr_bt *sa;
	int error;

	KKASSERT(nam != NULL);
	sa = (struct sockaddr_bt *)nam;

	if (sa->bt_len != sizeof(struct sockaddr_bt)) {
		error = EINVAL;
	} else if (sa->bt_family != AF_BLUETOOTH) {
		error = EAFNOSUPPORT;
	} else {
		error = sco_bind(pcb, sa);
	}
	lwkt_replymsg(&msg->bind.base.lmsg, error);
}

static void
sco_sconnect(netmsg_t msg)
{
	struct socket *so = msg->connect.base.nm_so;
	struct sockaddr *nam = msg->connect.nm_nam;
	struct sco_pcb *pcb = (struct sco_pcb *)so->so_pcb;
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
		error = sco_connect(pcb, sa);
	}
	lwkt_replymsg(&msg->connect.base.lmsg, error);
}

static void
sco_speeraddr(netmsg_t msg)
{
	struct socket *so = msg->peeraddr.base.nm_so;
	struct sockaddr **nam = msg->peeraddr.nm_nam;
	struct sco_pcb *pcb = (struct sco_pcb *)so->so_pcb;
	struct sockaddr_bt *sa, ssa;
	int error;

	sa = &ssa;
	bzero(sa, sizeof *sa);
	sa->bt_len = sizeof(struct sockaddr_bt);
	sa->bt_family = AF_BLUETOOTH;
	error = sco_peeraddr(pcb, sa);
	*nam = dup_sockaddr((struct sockaddr *)sa);
	lwkt_replymsg(&msg->peeraddr.base.lmsg, error);
}

static void
sco_ssockaddr(netmsg_t msg)
{
	struct socket *so = msg->sockaddr.base.nm_so;
	struct sockaddr **nam = msg->sockaddr.nm_nam;
	struct sco_pcb *pcb = (struct sco_pcb *)so->so_pcb;
	struct sockaddr_bt *sa, ssa;
	int error;

	sa = &ssa;
	bzero(sa, sizeof *sa);
	sa->bt_len = sizeof(struct sockaddr_bt);
	sa->bt_family = AF_BLUETOOTH;
	error = sco_sockaddr(pcb, sa);
	*nam = dup_sockaddr((struct sockaddr *)sa);

	lwkt_replymsg(&msg->sockaddr.base.lmsg, error);
}

static void
sco_sshutdown(netmsg_t msg)
{
	socantsendmore(msg->shutdown.base.nm_so);
	lwkt_replymsg(&msg->shutdown.base.lmsg, 0);
}

static void
sco_ssend(netmsg_t msg)
{
	struct socket *so = msg->send.base.nm_so;
	struct mbuf *m = msg->send.nm_m;
	struct mbuf *control = msg->send.nm_control;
	struct sco_pcb *pcb = (struct sco_pcb *)so->so_pcb;
	struct mbuf *m0;
	int error = 0;

	KKASSERT(m != NULL);
	if (m->m_pkthdr.len == 0)
		goto out;

	if (m->m_pkthdr.len > pcb->sp_mtu) {
		error = EMSGSIZE;
		goto out;
	}

	m0 = m_copym(m, 0, M_COPYALL, MB_DONTWAIT);
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
	error = sco_send(pcb, m0);
	m = NULL;
out:
	if (m)
		m_freem(m);
	if (control)
		m_freem(control);
	lwkt_replymsg(&msg->send.base.lmsg, error);
}

static void
sco_saccept(netmsg_t msg)
{
	struct socket *so = msg->accept.base.nm_so;
	struct sockaddr **nam = msg->accept.nm_nam;
	struct sco_pcb *pcb = (struct sco_pcb *)so->so_pcb;
	struct sockaddr_bt *sa, ssa;
	int error;

	sa = &ssa;
	bzero(sa, sizeof *sa);
	sa->bt_len = sizeof(struct sockaddr_bt);
	sa->bt_family = AF_BLUETOOTH;
	error = sco_peeraddr(pcb, sa);
	*nam = dup_sockaddr((struct sockaddr *)sa);

	lwkt_replymsg(&msg->accept.base.lmsg, error);
}

static void
sco_slisten(netmsg_t msg)
{
	struct socket *so = msg->listen.base.nm_so;
	struct sco_pcb *pcb = (struct sco_pcb *)so->so_pcb;
	int error;

	error = sco_listen(pcb);
	lwkt_replymsg(&msg->accept.base.lmsg, error);
}

struct pr_usrreqs sco_usrreqs = {
        .pru_abort = sco_sabort,
        .pru_accept = sco_saccept,
        .pru_attach = sco_sattach,
        .pru_bind = sco_sbind,
        .pru_connect = sco_sconnect,
        .pru_connect2 = pr_generic_notsupp,
        .pru_control = pr_generic_notsupp,
        .pru_detach = sco_sdetach,
        .pru_disconnect = sco_sdisconnect,
        .pru_listen = sco_slisten,
        .pru_peeraddr = sco_speeraddr,
        .pru_rcvd = pr_generic_notsupp,
        .pru_rcvoob = pr_generic_notsupp,
        .pru_send = sco_ssend,
        .pru_sense = pru_sense_null,
        .pru_shutdown = sco_sshutdown,
        .pru_sockaddr = sco_ssockaddr,
        .pru_sosend = sosend,
        .pru_soreceive = soreceive
};
