/* $DragonFly: src/sys/netbt/rfcomm_socket.c,v 1.3 2008/06/20 20:52:29 aggelos Exp $ */
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
int
rfcomm_ctloutput(struct socket *so, struct sockopt *sopt)
{
	struct rfcomm_dlc *pcb = (struct rfcomm_dlc *) so->so_pcb;
	struct mbuf *m;
	int err = 0;

#ifdef notyet			/* XXX */
	DPRINTFN(2, "%s\n", prcorequests[sopt->sopt_dir]);
#endif

	if (pcb == NULL)
		return EINVAL;

	if (sopt->sopt_level != BTPROTO_RFCOMM)
		return ENOPROTOOPT;

	switch(sopt->sopt_dir) {
	case PRCO_GETOPT:
		m = m_get(M_WAITOK, MT_DATA);
		crit_enter();
		m->m_len = rfcomm_getopt(pcb, sopt->sopt_name, mtod(m, void *));
		crit_exit();		
		if (m->m_len == 0) {
			m_freem(m);
			m = NULL;
			err = ENOPROTOOPT;
		}
		soopt_from_kbuf(sopt, mtod(m, void *), m->m_len);
		break;

	case PRCO_SETOPT:
		err = rfcomm_setopt2(pcb, sopt->sopt_name, so, sopt);

		break;

	default:
		err = ENOPROTOOPT;
		break;
	}

	return err;
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
static int
rfcomm_sdetach(struct socket *so)
{
	return rfcomm_detach((struct rfcomm_dlc **)&so->so_pcb);
}

static int
rfcomm_sabort (struct socket *so)
{
	struct rfcomm_dlc *pcb = (struct rfcomm_dlc *) so->so_pcb;

	rfcomm_disconnect(pcb, 0);
	soisdisconnected(so);
	return rfcomm_sdetach(so);
}

static int
rfcomm_sdisconnect (struct socket *so)
{
	struct rfcomm_dlc *pcb = (struct rfcomm_dlc *) so->so_pcb;

	soisdisconnecting(so);
	return rfcomm_disconnect(pcb, so->so_linger);
}

static int
rfcomm_scontrol (struct socket *so, u_long cmd, caddr_t data,
				    struct ifnet *ifp, struct thread *td)
{
	return EPASSTHROUGH;
}

static int
rfcomm_sattach (struct socket *so, int proto,
			       struct pru_attach_info *ai)
{
	struct rfcomm_dlc *pcb = (struct rfcomm_dlc *) so->so_pcb;

	int err=0;
	if (pcb != NULL)
		return EINVAL;

	/*
	 * Since we have nothing to add, we attach the DLC
	 * structure directly to our PCB pointer.
	 */
	err = soreserve(so, rfcomm_sendspace, rfcomm_recvspace, NULL);
	if (err)
		return err;

	err = rfcomm_attach((struct rfcomm_dlc **)&so->so_pcb,
	    &rfcomm_proto, so);
	if (err)
		return err;

	err = rfcomm_rcvd(so->so_pcb, sbspace(&so->so_rcv));
	if (err) {
		rfcomm_detach((struct rfcomm_dlc **)&so->so_pcb);
		return err;
	}

	return 0;
}

static int
rfcomm_sbind (struct socket *so, struct sockaddr *nam,
				 struct thread *td)
{
	struct rfcomm_dlc *pcb = (struct rfcomm_dlc *) so->so_pcb;
	struct sockaddr_bt *sa;

	KKASSERT(nam != NULL);
	sa = (struct sockaddr_bt *)nam;

	if (sa->bt_len != sizeof(struct sockaddr_bt))
		return EINVAL;

	if (sa->bt_family != AF_BLUETOOTH)
		return EAFNOSUPPORT;

	return rfcomm_bind(pcb, sa);
}

static int
rfcomm_sconnect (struct socket *so, struct sockaddr *nam,
				    struct thread *td)
{
	struct rfcomm_dlc *pcb = (struct rfcomm_dlc *) so->so_pcb;
	struct sockaddr_bt *sa;

	KKASSERT(nam != NULL);
	sa = (struct sockaddr_bt *)nam;

	if (sa->bt_len != sizeof(struct sockaddr_bt))
		return EINVAL;

	if (sa->bt_family != AF_BLUETOOTH)
		return EAFNOSUPPORT;

	soisconnecting(so);
	return rfcomm_connect(pcb, sa);
}

static int
rfcomm_speeraddr (struct socket *so, struct sockaddr **nam)
{
	struct rfcomm_dlc *pcb = (struct rfcomm_dlc *) so->so_pcb;
	struct sockaddr_bt *sa, ssa;
	int e;

	sa = &ssa;
	bzero(sa, sizeof *sa);
	sa->bt_len = sizeof(struct sockaddr_bt);
	sa->bt_family = AF_BLUETOOTH;
	e = rfcomm_peeraddr(pcb, sa);;
	*nam = dup_sockaddr((struct sockaddr *)sa);
	return (e);
}

static int
rfcomm_ssockaddr (struct socket *so, struct sockaddr **nam)
{
	struct rfcomm_dlc *pcb = (struct rfcomm_dlc *) so->so_pcb;
	struct sockaddr_bt *sa, ssa;
	int e;

	sa = &ssa;
	bzero(sa, sizeof *sa);
	sa->bt_len = sizeof(struct sockaddr_bt);
	sa->bt_family = AF_BLUETOOTH;
	e = rfcomm_sockaddr(pcb, sa);;
	*nam = dup_sockaddr((struct sockaddr *)sa);
	return (e);
}

static int
rfcomm_sshutdown (struct socket *so)
{
	socantsendmore(so);
	return 0;
}

static int
rfcomm_ssend (struct socket *so, int flags, struct mbuf *m,
    struct sockaddr *addr, struct mbuf *control, struct thread *td)
{
	struct rfcomm_dlc *pcb = (struct rfcomm_dlc *) so->so_pcb;
	struct mbuf *m0;

	KKASSERT(m != NULL);

	if (control)	/* no use for that */
		m_freem(control);

	m0 = m_copym(m, 0, M_COPYALL, MB_DONTWAIT);
	if (m0 == NULL)
		return ENOMEM;

	sbappendstream(&so->so_snd.sb, m);

	return rfcomm_send(pcb, m0);
}

static int
rfcomm_saccept(struct socket *so, struct sockaddr **nam)
{
	struct rfcomm_dlc *pcb = (struct rfcomm_dlc *) so->so_pcb;
	struct sockaddr_bt *sa, ssa;
	int e;

	sa = &ssa;
	bzero(sa, sizeof *sa);
	sa->bt_len = sizeof(struct sockaddr_bt);
	sa->bt_family = AF_BLUETOOTH;
	e = rfcomm_peeraddr(pcb, sa);;
	*nam = dup_sockaddr((struct sockaddr *)sa);
	return (e);
}

static int
rfcomm_slisten(struct socket *so, struct thread *td)
{
	struct rfcomm_dlc *pcb = (struct rfcomm_dlc *) so->so_pcb;
	return rfcomm_listen(pcb);
}

static int
rfcomm_srcvd(struct socket *so, int flags)
{
	struct rfcomm_dlc *pcb = (struct rfcomm_dlc *) so->so_pcb; 
	return rfcomm_rcvd(pcb, sbspace(&so->so_rcv));
}

struct pr_usrreqs rfcomm_usrreqs = {
        .pru_abort = rfcomm_sabort,
        .pru_accept = rfcomm_saccept,
        .pru_attach = rfcomm_sattach,
        .pru_bind = rfcomm_sbind,
        .pru_connect = rfcomm_sconnect,
        .pru_connect2 = pru_connect2_notsupp,
        .pru_control = rfcomm_scontrol,
        .pru_detach = rfcomm_sdetach,
        .pru_disconnect = rfcomm_sdisconnect,
        .pru_listen = rfcomm_slisten,
        .pru_peeraddr = rfcomm_speeraddr,
        .pru_rcvd = rfcomm_srcvd,
        .pru_rcvoob = pru_rcvoob_notsupp,
        .pru_send = rfcomm_ssend,
        .pru_sense = pru_sense_null,
        .pru_shutdown = rfcomm_sshutdown,
        .pru_sockaddr = rfcomm_ssockaddr,
        .pru_sosend = sosend,
        .pru_soreceive = soreceive
};
