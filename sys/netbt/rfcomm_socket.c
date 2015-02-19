/* $OpenBSD: src/sys/netbt/rfcomm_socket.c,v 1.2 2008/02/24 21:34:48 uwe Exp $ */
/* $NetBSD: rfcomm_socket.c,v 1.8 2007/10/15 18:04:34 plunky Exp $ */

/*-
 * Copyright (c) 2006 Itronix Inc.
 * All rights reserved.
 *
 * Written by Iain Hibbert for Itronix Inc.
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
#include <netbt/rfcomm.h>

/****************************************************************************
 *
 *	RFCOMM SOCK_STREAM Sockets - serial line emulation
 *
 */

static void rfcomm_connecting(void *);
static void rfcomm_connected(void *);
static void rfcomm_disconnected(void *, int);
static void *rfcomm_newconn(void *, struct sockaddr_bt *, struct sockaddr_bt *);
static void rfcomm_complete(void *, int);
static void rfcomm_linkmode(void *, int);
static void rfcomm_input(void *, struct mbuf *);

static const struct btproto rfcomm_proto = {
	rfcomm_connecting,
	rfcomm_connected,
	rfcomm_disconnected,
	rfcomm_newconn,
	rfcomm_complete,
	rfcomm_linkmode,
	rfcomm_input,
};

/* sysctl variables */
int rfcomm_sendspace = 4096;
int rfcomm_recvspace = 4096;

/*
 * rfcomm_ctloutput(request, socket, level, optname, opt)
 *
 */
void
rfcomm_ctloutput(netmsg_t msg)
{
	struct socket *so = msg->ctloutput.base.nm_so;
	struct sockopt *sopt = msg->ctloutput.nm_sopt;
	struct rfcomm_dlc *pcb = (struct rfcomm_dlc *) so->so_pcb;
	struct mbuf *m;
	int error = 0;

#ifdef notyet			/* XXX */
	DPRINTFN(2, "%s\n", prcorequests[sopt->sopt_dir]);
#endif

	if (pcb == NULL) {
		error = EINVAL;
		goto out;
	}

	if (sopt->sopt_level != BTPROTO_RFCOMM) {
		error = ENOPROTOOPT;
		goto out;
	}

	switch(sopt->sopt_dir) {
	case PRCO_GETOPT:
		m = m_get(M_WAITOK, MT_DATA);
		crit_enter();
		m->m_len = rfcomm_getopt(pcb, sopt->sopt_name, mtod(m, void *));
		crit_exit();		
		if (m->m_len == 0) {
			m_freem(m);
			m = NULL;
			error = ENOPROTOOPT;
		}
		soopt_from_kbuf(sopt, mtod(m, void *), m->m_len);
		break;

	case PRCO_SETOPT:
		error = rfcomm_setopt2(pcb, sopt->sopt_name, so, sopt);

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
 * RFCOMM callbacks
 */

static void
rfcomm_connecting(void *arg)
{
	/* struct socket *so = arg; */

	KKASSERT(arg != NULL);
	DPRINTF("Connecting\n");
}

static void
rfcomm_connected(void *arg)
{
	struct socket *so = arg;

	KKASSERT(so != NULL);
	DPRINTF("Connected\n");
	soisconnected(so);
}

static void
rfcomm_disconnected(void *arg, int err)
{
	struct socket *so = arg;

	KKASSERT(so != NULL);
	DPRINTF("Disconnected\n");

	so->so_error = err;
	soisdisconnected(so);
}

static void *
rfcomm_newconn(void *arg, struct sockaddr_bt *laddr,
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

/*
 * rfcomm_complete(rfcomm_dlc, length)
 *
 * length bytes are sent and may be removed from socket buffer
 */
static void
rfcomm_complete(void *arg, int length)
{
	struct socket *so = arg;

	sbdrop(&so->so_snd.sb, length);
	sowwakeup(so);
}

/*
 * rfcomm_linkmode(rfcomm_dlc, new)
 *
 * link mode change notification.
 */
static void
rfcomm_linkmode(void *arg, int new)
{
	struct socket *so = arg;
	int mode;

	DPRINTF("auth %s, encrypt %s, secure %s\n",
		(new & RFCOMM_LM_AUTH ? "on" : "off"),
		(new & RFCOMM_LM_ENCRYPT ? "on" : "off"),
		(new & RFCOMM_LM_SECURE ? "on" : "off"));

	(void)rfcomm_getopt(so->so_pcb, SO_RFCOMM_LM, &mode);
	if (((mode & RFCOMM_LM_AUTH) && !(new & RFCOMM_LM_AUTH))
	    || ((mode & RFCOMM_LM_ENCRYPT) && !(new & RFCOMM_LM_ENCRYPT))
	    || ((mode & RFCOMM_LM_SECURE) && !(new & RFCOMM_LM_SECURE)))
		rfcomm_disconnect(so->so_pcb, 0);
}

/*
 * rfcomm_input(rfcomm_dlc, mbuf)
 */
static void
rfcomm_input(void *arg, struct mbuf *m)
{
	struct socket *so = arg;

	KKASSERT(so != NULL);

	if (m->m_pkthdr.len > sbspace(&so->so_rcv)) {
		kprintf("%s: %d bytes dropped (socket buffer full)\n",
			__func__, m->m_pkthdr.len);
		m_freem(m);
		return;
	}

	DPRINTFN(10, "received %d bytes\n", m->m_pkthdr.len);

	sbappendstream(&so->so_rcv.sb, m);
	sorwakeup(so);
}

/*
 * Implementation of usrreqs.
 */
static void
rfcomm_sdetach(netmsg_t msg)
{
	struct socket *so = msg->detach.base.nm_so;
	int error;

	error = rfcomm_detach((struct rfcomm_dlc **)&so->so_pcb);
	lwkt_replymsg(&msg->detach.base.lmsg, error);
}

/*
 * NOTE: (so) is referenced from soabort*() and netmsg_pru_abort()
 *	 will sofree() it when we return.
 */
static void
rfcomm_sabort(netmsg_t msg)
{
	struct socket *so = msg->abort.base.nm_so;
	struct rfcomm_dlc *pcb = (struct rfcomm_dlc *) so->so_pcb;

	rfcomm_disconnect(pcb, 0);
	soisdisconnected(so);
	rfcomm_sdetach(msg);
	/* msg invalid now */
}

static void
rfcomm_sdisconnect(netmsg_t msg)
{
	struct socket *so = msg->abort.base.nm_so;
	struct rfcomm_dlc *pcb = (struct rfcomm_dlc *) so->so_pcb;
	int error;

	soisdisconnecting(so);
	error = rfcomm_disconnect(pcb, so->so_linger);
	lwkt_replymsg(&msg->disconnect.base.lmsg, error);
}

static void
rfcomm_scontrol(netmsg_t msg)
{
	lwkt_replymsg(&msg->control.base.lmsg, EPASSTHROUGH);
}

static void
rfcomm_sattach(netmsg_t msg)
{
	struct socket *so = msg->attach.base.nm_so;
	struct rfcomm_dlc *pcb = (struct rfcomm_dlc *) so->so_pcb;
	int error;

	if (pcb != NULL) {
		error = EINVAL;
		goto out;
	}

	/*
	 * Since we have nothing to add, we attach the DLC
	 * structure directly to our PCB pointer.
	 */
	error = soreserve(so, rfcomm_sendspace, rfcomm_recvspace, NULL);
	if (error)
		goto out;

	error = rfcomm_attach((struct rfcomm_dlc **)&so->so_pcb,
			      &rfcomm_proto, so);
	if (error)
		goto out;

	error = rfcomm_rcvd(so->so_pcb, sbspace(&so->so_rcv));
	if (error)
		rfcomm_detach((struct rfcomm_dlc **)&so->so_pcb);
out:
	lwkt_replymsg(&msg->attach.base.lmsg, error);
}

static void
rfcomm_sbind(netmsg_t msg)
{
	struct socket *so = msg->bind.base.nm_so;
	struct sockaddr *nam = msg->bind.nm_nam;
	struct rfcomm_dlc *pcb = (struct rfcomm_dlc *) so->so_pcb;
	struct sockaddr_bt *sa;
	int error;

	KKASSERT(nam != NULL);
	sa = (struct sockaddr_bt *)nam;

	if (sa->bt_len != sizeof(struct sockaddr_bt)) {
		error = EINVAL;
	} else if (sa->bt_family != AF_BLUETOOTH) {
		error = EAFNOSUPPORT;
	} else {
		error = rfcomm_bind(pcb, sa);
	}
	lwkt_replymsg(&msg->bind.base.lmsg, error);
}

static void
rfcomm_sconnect(netmsg_t msg)
{
	struct socket *so = msg->connect.base.nm_so;
	struct sockaddr *nam = msg->connect.nm_nam;
	struct rfcomm_dlc *pcb = (struct rfcomm_dlc *) so->so_pcb;
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
		error = rfcomm_connect(pcb, sa);
	}
	lwkt_replymsg(&msg->connect.base.lmsg, error);
}

static void
rfcomm_speeraddr(netmsg_t msg)
{
	struct socket *so = msg->peeraddr.base.nm_so;
	struct sockaddr **nam = msg->peeraddr.nm_nam;
	struct rfcomm_dlc *pcb = (struct rfcomm_dlc *) so->so_pcb;
	struct sockaddr_bt *sa, ssa;
	int error;

	sa = &ssa;
	bzero(sa, sizeof *sa);
	sa->bt_len = sizeof(struct sockaddr_bt);
	sa->bt_family = AF_BLUETOOTH;
	error = rfcomm_peeraddr(pcb, sa);
	*nam = dup_sockaddr((struct sockaddr *)sa);

	lwkt_replymsg(&msg->peeraddr.base.lmsg, error);
}

static void
rfcomm_ssockaddr(netmsg_t msg)
{
	struct socket *so = msg->sockaddr.base.nm_so;
	struct sockaddr **nam = msg->sockaddr.nm_nam;
	struct rfcomm_dlc *pcb = (struct rfcomm_dlc *) so->so_pcb;
	struct sockaddr_bt *sa, ssa;
	int error;

	sa = &ssa;
	bzero(sa, sizeof *sa);
	sa->bt_len = sizeof(struct sockaddr_bt);
	sa->bt_family = AF_BLUETOOTH;
	error = rfcomm_sockaddr(pcb, sa);
	*nam = dup_sockaddr((struct sockaddr *)sa);

	lwkt_replymsg(&msg->sockaddr.base.lmsg, error);
}

static void
rfcomm_sshutdown(netmsg_t msg)
{
	struct socket *so = msg->shutdown.base.nm_so;

	socantsendmore(so);
	lwkt_replymsg(&msg->shutdown.base.lmsg, 0);
}

static void
rfcomm_ssend(netmsg_t msg)
{
	struct socket *so = msg->send.base.nm_so;
	struct mbuf *m = msg->send.nm_m;
	struct mbuf *control = msg->send.nm_control;
	struct rfcomm_dlc *pcb = (struct rfcomm_dlc *) so->so_pcb;
	struct mbuf *m0;
	int error;

	KKASSERT(m != NULL);

	/* no use for that */
	if (control) {
		m_freem(control);
		control = NULL;
	}

	m0 = m_copym(m, 0, M_COPYALL, M_NOWAIT);
	if (m0) {
		sbappendstream(&so->so_snd.sb, m);
		error = rfcomm_send(pcb, m0);
	} else {
		error = ENOMEM;
	}
	lwkt_replymsg(&msg->send.base.lmsg, error);
}

static void
rfcomm_saccept(netmsg_t msg)
{
	struct socket *so = msg->accept.base.nm_so;
	struct sockaddr **nam = msg->accept.nm_nam;
	struct rfcomm_dlc *pcb = (struct rfcomm_dlc *) so->so_pcb;
	struct sockaddr_bt *sa, ssa;
	int error;

	sa = &ssa;
	bzero(sa, sizeof *sa);
	sa->bt_len = sizeof(struct sockaddr_bt);
	sa->bt_family = AF_BLUETOOTH;
	error = rfcomm_peeraddr(pcb, sa);
	*nam = dup_sockaddr((struct sockaddr *)sa);

	lwkt_replymsg(&msg->accept.base.lmsg, error);
}

static void
rfcomm_slisten(netmsg_t msg)
{
	struct socket *so = msg->listen.base.nm_so;
	struct rfcomm_dlc *pcb = (struct rfcomm_dlc *)so->so_pcb;
	int error;

	error = rfcomm_listen(pcb);
	lwkt_replymsg(&msg->listen.base.lmsg, error);
}

static void
rfcomm_srcvd(netmsg_t msg)
{
	struct socket *so = msg->rcvd.base.nm_so;
	struct rfcomm_dlc *pcb = (struct rfcomm_dlc *) so->so_pcb; 
	int error;

	error = rfcomm_rcvd(pcb, sbspace(&so->so_rcv));
	lwkt_replymsg(&msg->rcvd.base.lmsg, error);
}

struct pr_usrreqs rfcomm_usrreqs = {
        .pru_abort = rfcomm_sabort,
        .pru_accept = rfcomm_saccept,
        .pru_attach = rfcomm_sattach,
        .pru_bind = rfcomm_sbind,
        .pru_connect = rfcomm_sconnect,
        .pru_connect2 = pr_generic_notsupp,
        .pru_control = rfcomm_scontrol,
        .pru_detach = rfcomm_sdetach,
        .pru_disconnect = rfcomm_sdisconnect,
        .pru_listen = rfcomm_slisten,
        .pru_peeraddr = rfcomm_speeraddr,
        .pru_rcvd = rfcomm_srcvd,
        .pru_rcvoob = pr_generic_notsupp,
        .pru_send = rfcomm_ssend,
        .pru_sense = pru_sense_null,
        .pru_shutdown = rfcomm_sshutdown,
        .pru_sockaddr = rfcomm_ssockaddr,
        .pru_sosend = sosend,
        .pru_soreceive = soreceive
};
