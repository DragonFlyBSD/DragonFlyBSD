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
int
l2cap_ctloutput(struct socket *so, struct sockopt *sopt)
{
	struct l2cap_channel *pcb = (struct l2cap_channel *) so->so_pcb;
	struct mbuf *m;
	int err = 0;

#ifdef notyet			/* XXX */
	DPRINTFN(2, "%s\n", prcorequests[req]);
#endif

	if (pcb == NULL)
		return EINVAL;

	if (sopt->sopt_level != BTPROTO_L2CAP)
		return ENOPROTOOPT;

	switch(sopt->sopt_dir) {
	case PRCO_GETOPT:
		m = m_get(M_NOWAIT, MT_DATA);
		if (m == NULL) {
		    err = ENOMEM;
		    break;
		}
		m->m_len = l2cap_getopt(pcb, sopt->sopt_name, mtod(m, void *));
		if (m->m_len == 0) {
			m_freem(m);
			m = NULL;
			err = ENOPROTOOPT;
		}
		soopt_from_kbuf(sopt, mtod(m, void *), m->m_len);
		break;

	case PRCO_SETOPT:
		err = l2cap_setopt2(pcb, sopt->sopt_name, so, sopt);
		break;

	default:
		err = ENOPROTOOPT;
		break;
	}

	return err;
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
static int
l2cap_sdetach(struct socket *so)
{
	return l2cap_detach((struct l2cap_channel **)&so->so_pcb);
}

static int
l2cap_sabort (struct socket *so)
{
	struct l2cap_channel *pcb = so->so_pcb;
	
	l2cap_disconnect(pcb, 0);
	soisdisconnected(so);
	
	return l2cap_sdetach(so);
}

static int
l2cap_sdisconnect (struct socket *so)
{
	struct l2cap_channel *pcb = so->so_pcb;
	
	soisdisconnecting(so);
	
	return l2cap_disconnect(pcb, so->so_linger);
}

static int
l2cap_scontrol (struct socket *so, u_long cmd, caddr_t data,
    struct ifnet *ifp, struct thread *td)
{
	return EPASSTHROUGH;
}

static int
l2cap_sattach (struct socket *so, int proto,
			       struct pru_attach_info *ai)
{
	struct l2cap_channel *pcb = so->so_pcb;
	int err = 0;

	if (pcb != NULL)
		return EINVAL;

	/*
	 * For L2CAP socket PCB we just use an l2cap_channel structure
	 * since we have nothing to add..
	 */
	err = soreserve(so, l2cap_sendspace, l2cap_recvspace, NULL);
	if (err)
		return err;

	return l2cap_attach((struct l2cap_channel **)&so->so_pcb,
	    &l2cap_proto, so);
}

static int
l2cap_sbind (struct socket *so, struct sockaddr *nam,
				 struct thread *td)
{
	struct l2cap_channel *pcb = so->so_pcb;
	struct sockaddr_bt *sa;

	KKASSERT(nam != NULL);
	sa = (struct sockaddr_bt *)nam;

	if (sa->bt_len != sizeof(struct sockaddr_bt))
		return EINVAL;

	if (sa->bt_family != AF_BLUETOOTH)
		return EAFNOSUPPORT;

	return l2cap_bind(pcb, sa);
}

static int
l2cap_sconnect (struct socket *so, struct sockaddr *nam,
				    struct thread *td)
{
	struct l2cap_channel *pcb = so->so_pcb;
	struct sockaddr_bt *sa;

	KKASSERT(nam != NULL);
	sa = (struct sockaddr_bt *)nam;

	if (sa->bt_len != sizeof(struct sockaddr_bt))
		return EINVAL;

	if (sa->bt_family != AF_BLUETOOTH)
		return EAFNOSUPPORT;

	soisconnecting(so);
	return l2cap_connect(pcb, sa);
}

static int
l2cap_speeraddr (struct socket *so, struct sockaddr **nam)
{
	struct l2cap_channel *pcb = so->so_pcb;
	struct sockaddr_bt *sa, ssa;
	int e;

	sa = &ssa;
	bzero(sa, sizeof *sa);
	sa->bt_len = sizeof(struct sockaddr_bt);
	sa->bt_family = AF_BLUETOOTH;
	e = l2cap_peeraddr(pcb, sa);
	*nam = dup_sockaddr((struct sockaddr *)sa);

	return (e);	
}

static int
l2cap_ssockaddr (struct socket *so, struct sockaddr **nam)
{
	struct l2cap_channel *pcb = so->so_pcb;
	struct sockaddr_bt *sa, ssa;
	int e;

	sa = &ssa;
	bzero(sa, sizeof *sa);
	sa->bt_len = sizeof(struct sockaddr_bt);
	sa->bt_family = AF_BLUETOOTH;
	e = l2cap_sockaddr(pcb, sa);
	*nam = dup_sockaddr((struct sockaddr *)sa);

	return (e);
}

static int
l2cap_sshutdown (struct socket *so)
{
	socantsendmore(so);
	return 0;
}

static int
l2cap_ssend (struct socket *so, int flags, struct mbuf *m,
    struct sockaddr *addr, struct mbuf *control, struct thread *td)
{
	struct l2cap_channel *pcb = so->so_pcb;
	struct mbuf *m0;

	int err = 0;

	KKASSERT(m != NULL);
	if (m->m_pkthdr.len == 0)
		goto error;

	if (m->m_pkthdr.len > pcb->lc_omtu) {
		err = EMSGSIZE;
		goto error;
	}

	m0 = m_copym(m, 0, M_COPYALL, MB_DONTWAIT);
	if (m0 == NULL) {
		err = ENOMEM;
		goto error;
	}

	if (control)	/* no use for that */
		m_freem(control);

	sbappendrecord(&so->so_snd.sb, m);
	return l2cap_send(pcb, m0);

error:
	if (m)
		m_freem(m);
	if (control)
		m_freem(control);

	return err;
}

static int
l2cap_saccept(struct socket *so, struct sockaddr **nam)
{
	struct l2cap_channel *pcb = so->so_pcb;
	struct sockaddr_bt sa;
	int e;
		
	KKASSERT(nam != NULL);
	
	bzero(&sa, sizeof (sa) );
	sa.bt_len = sizeof(struct sockaddr_bt);
	sa.bt_family = AF_BLUETOOTH;

	e = l2cap_peeraddr(pcb, &sa);
	*nam = dup_sockaddr((struct sockaddr *)&sa);

	return e;	
}

static int
l2cap_slisten(struct socket *so, struct thread *td)
{
	struct l2cap_channel *pcb = so->so_pcb;
	return l2cap_listen(pcb);
}


struct pr_usrreqs l2cap_usrreqs = {
        .pru_abort = l2cap_sabort,
        .pru_accept = l2cap_saccept,
        .pru_attach = l2cap_sattach,
        .pru_bind = l2cap_sbind,
        .pru_connect = l2cap_sconnect,
        .pru_connect2 = pru_connect2_notsupp,
        .pru_control = l2cap_scontrol,
        .pru_detach = l2cap_sdetach,
        .pru_disconnect = l2cap_sdisconnect,
        .pru_listen = l2cap_slisten,
        .pru_peeraddr = l2cap_speeraddr,
        .pru_rcvd = pru_rcvd_notsupp,
        .pru_rcvoob = pru_rcvoob_notsupp,
        .pru_send = l2cap_ssend,
        .pru_sense = pru_sense_null,
        .pru_shutdown = l2cap_sshutdown,
        .pru_sockaddr = l2cap_ssockaddr,
        .pru_sosend = sosend,
        .pru_soreceive = soreceive
};
