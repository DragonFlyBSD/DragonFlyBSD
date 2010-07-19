/* $OpenBSD: sco_socket.c,v 1.1 2007/06/01 02:46:12 uwe Exp $ */
/* $NetBSD: sco_socket.c,v 1.9 2007/04/21 06:15:23 plunky Exp $ */

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
int
sco_ctloutput(struct socket *so, struct sockopt *sopt)
{
	struct sco_pcb *pcb = (struct sco_pcb *)so->so_pcb;
	struct mbuf *m;
	int err = 0;

#ifdef notyet			/* XXX */
	DPRINTFN(2, "req %s\n", prcorequests[req]);
#endif

	if (pcb == NULL)
		return EINVAL;

	if (sopt->sopt_level != BTPROTO_SCO)
		return ENOPROTOOPT;

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
		m = m_get(M_WAITOK, MT_DATA);
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

	return err;
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
static int
sco_sdetach(struct socket *so)
{
	return sco_detach((struct sco_pcb **)&so->so_pcb);
}

static int
sco_sabort (struct socket *so)
{
	struct sco_pcb *pcb = (struct sco_pcb *)so->so_pcb;

	sco_disconnect(pcb, 0);
	soisdisconnected(so);

	return sco_sdetach(so);
}

static int
sco_sdisconnect (struct socket *so)
{
 	struct sco_pcb *pcb = (struct sco_pcb *)so->so_pcb;

	soisdisconnecting(so);

	return sco_disconnect(pcb, so->so_linger);
}

static int
sco_sattach (struct socket *so, int proto,
    struct pru_attach_info *ai)
{
	struct sco_pcb *pcb = (struct sco_pcb *)so->so_pcb;
	int err=0;

	if (pcb)
		return EINVAL;

	err = soreserve(so, sco_sendspace, sco_recvspace,NULL);
	if (err)
		return err;

	return sco_attach((struct sco_pcb **)&so->so_pcb,
	    &sco_proto, so);
}

static int
sco_sbind (struct socket *so, struct sockaddr *nam,
				 struct thread *td)
{
	struct sco_pcb *pcb = (struct sco_pcb *)so->so_pcb;
	struct sockaddr_bt *sa;

	KKASSERT(nam != NULL);
	sa = (struct sockaddr_bt *)nam;

	if (sa->bt_len != sizeof(struct sockaddr_bt))
		return EINVAL;

	if (sa->bt_family != AF_BLUETOOTH)
		return EAFNOSUPPORT;

	return sco_bind(pcb, sa);
}

static int
sco_sconnect (struct socket *so, struct sockaddr *nam,
				    struct thread *td)
{
	struct sco_pcb *pcb = (struct sco_pcb *)so->so_pcb;
	struct sockaddr_bt *sa;

	KKASSERT(nam != NULL);
	sa = (struct sockaddr_bt *)nam;

	if (sa->bt_len != sizeof(struct sockaddr_bt))
		return EINVAL;

	if (sa->bt_family != AF_BLUETOOTH)
		return EAFNOSUPPORT;

	soisconnecting(so);
	return sco_connect(pcb, sa);
}

static int
sco_speeraddr (struct socket *so, struct sockaddr **nam)
{
	struct sco_pcb *pcb = (struct sco_pcb *)so->so_pcb;
	struct sockaddr_bt *sa, ssa;
	int e;

	sa = &ssa;
	bzero(sa, sizeof *sa);
	sa->bt_len = sizeof(struct sockaddr_bt);
	sa->bt_family = AF_BLUETOOTH;
	e = sco_peeraddr(pcb, sa);
	*nam = dup_sockaddr((struct sockaddr *)sa);

	return (e);
}

static int
sco_ssockaddr (struct socket *so, struct sockaddr **nam)
{
	struct sco_pcb *pcb = (struct sco_pcb *)so->so_pcb;
	struct sockaddr_bt *sa, ssa;
	int e;

	sa = &ssa;
	bzero(sa, sizeof *sa);
	sa->bt_len = sizeof(struct sockaddr_bt);
	sa->bt_family = AF_BLUETOOTH;
	e = sco_sockaddr(pcb, sa);
	*nam = dup_sockaddr((struct sockaddr *)sa);

	return (e);
}

static int
sco_sshutdown (struct socket *so)
{
	socantsendmore(so);
	return 0;
}

static int
sco_ssend (struct socket *so, int flags, struct mbuf *m,
    struct sockaddr *addr, struct mbuf *control, struct thread *td)
{
	struct sco_pcb *pcb = (struct sco_pcb *)so->so_pcb;
	struct mbuf *m0;
	int err = 0;

	KKASSERT(m != NULL);
	if (m->m_pkthdr.len == 0)
		goto error;

	if (m->m_pkthdr.len > pcb->sp_mtu) {
		err = EMSGSIZE;
		goto error;
	}

	m0 = m_copym(m, 0, M_COPYALL, MB_DONTWAIT);
	if (m0 == NULL) {
		err = ENOMEM;
		goto error;
	}

	if (control) /* no use for that */
		m_freem(control);

	sbappendrecord(&so->so_snd.sb, m);
	return sco_send(pcb, m0);

error:
	if (m) m_freem(m);
	if (control) m_freem(control);
	return err;
}

static int
sco_saccept(struct socket *so, struct sockaddr **nam)
{
	struct sco_pcb *pcb = (struct sco_pcb *)so->so_pcb;
	struct sockaddr_bt *sa, ssa;
	int e;

	sa = &ssa;
	bzero(sa, sizeof *sa);
	sa->bt_len = sizeof(struct sockaddr_bt);
	sa->bt_family = AF_BLUETOOTH;
	e = sco_peeraddr(pcb, sa);
	*nam = dup_sockaddr((struct sockaddr *)sa);

	return (e);
}

static int
sco_slisten(struct socket *so, struct thread *td)
{
	struct sco_pcb *pcb = (struct sco_pcb *)so->so_pcb;
	return sco_listen(pcb);
}


struct pr_usrreqs sco_usrreqs = {
        .pru_abort = sco_sabort,
        .pru_accept = sco_saccept,
        .pru_attach = sco_sattach,
        .pru_bind = sco_sbind,
        .pru_connect = sco_sconnect,
        .pru_connect2 = pru_connect2_notsupp,
        .pru_control = pru_control_notsupp,
        .pru_detach = sco_sdetach,
        .pru_disconnect = sco_sdisconnect,
        .pru_listen = sco_slisten,
        .pru_peeraddr = sco_speeraddr,
        .pru_rcvd = pru_rcvd_notsupp,
        .pru_rcvoob = pru_rcvoob_notsupp,
        .pru_send = sco_ssend,
        .pru_sense = pru_sense_null,
        .pru_shutdown = sco_sshutdown,
        .pru_sockaddr = sco_ssockaddr,
        .pru_sosend = sosend,
        .pru_soreceive = soreceive
};
