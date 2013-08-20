/*
 * Copyright (c) 1999, Boris Popov
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *    This product includes software developed by Boris Popov.
 * 4. Neither the name of the author nor the names of any co-contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD: src/sys/netncp/ncp_sock.c,v 1.2 1999/10/12 10:36:59 bp Exp $
 *
 * Low level socket routines
 */

#include "opt_inet.h"
#include "opt_ipx.h"

#if !defined(INET) && !defined(IPX)
#error "NCP requires either INET of IPX protocol family"
#endif

#include <sys/param.h>
#include <sys/errno.h>
#include <sys/malloc.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/protosw.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/socketvar2.h>
#include <sys/socketops.h>
#include <sys/kernel.h>
#include <sys/uio.h>
#include <sys/fcntl.h>
#include <sys/syslog.h>
#include <sys/mbuf.h>
#include <net/route.h>
#include <sys/thread2.h>

#ifdef IPX
#include <netproto/ipx/ipx.h>
#include <netproto/ipx/ipx_pcb.h>
#endif

#include "ncp.h"
#include "ncp_conn.h"
#include "ncp_sock.h"
#include "ncp_subr.h"
#include "ncp_rq.h"

#ifdef IPX
#define ipx_setnullnet(x) ((x).x_net.s_net[0]=0); ((x).x_net.s_net[1]=0);
#define ipx_setnullhost(x) ((x).x_host.s_host[0] = 0); \
	((x).x_host.s_host[1] = 0); ((x).x_host.s_host[2] = 0);
#endif

/*int ncp_poll(struct socket *so, int events);*/
/*static int ncp_getsockname(struct socket *so, caddr_t asa, int *alen);*/
static int ncp_soconnect(struct socket *so,struct sockaddr *target, struct thread *p);


/* This will need only if native IP used, or (unlikely) NCP will be
 * implemented on the socket level
 */
static int
ncp_soconnect(struct socket *so,struct sockaddr *target, struct thread *td) {
	int error;

	error = soconnect(so, target, td, TRUE);
	if (error)
		return error;
	/*
	 * Wait for the connection to complete. Cribbed from the
	 * connect system call but with the wait timing out so
	 * that interruptible mounts don't hang here for a long time.
	 */
	error = EIO;
	crit_enter();
	while ((so->so_state & SS_ISCONNECTING) && so->so_error == 0) {
		tsleep((caddr_t)&so->so_timeo, 0, "ncpcon", 2 * hz);
		if ((so->so_state & SS_ISCONNECTING) &&
		    so->so_error == 0 /*&& rep &&*/) {
			soclrstate(so, SS_ISCONNECTING);
			crit_exit();
			goto bad;
		}
	}
	if (so->so_error) {
		error = so->so_error;
		so->so_error = 0;
		crit_exit();
		goto bad;
	}
	crit_exit();
	error=0;
bad:
	return error;
}
#ifdef notyet
static int
ncp_getsockname(struct socket *so, caddr_t asa, int *alen) {
	struct sockaddr *sa;
	int len=0, error;

	sa = NULL;
	error = so_pru_sockaddr(so, &sa);
	if (error==0) {
		if (sa) {
			len = min(len, sa->sa_len);
			bcopy(sa, asa, (u_int)len);
		}
		*alen=len;
	}
	if (sa)
		kfree(sa, M_SONAME);
	return (error);
}
#endif
int
ncp_sock_recv(struct socket *so, struct sockbuf *sio)
{
	int error, flags;

	sbinit(sio, 1000000);	/* limit data returned (inexact, hint only) */
	flags = MSG_DONTWAIT;

	error = so_pru_soreceive(so, NULL, NULL, sio, NULL, &flags);
#ifdef NCP_SOCKET_DEBUG
	if (error)
		kprintf("ncp_recv: err=%d\n", error);
#endif
	return (error);
}

int
ncp_sock_send(struct socket *so, struct mbuf *top, struct ncp_rq *rqp)
{
	struct thread *td = curthread; /* XXX */
	struct sockaddr *to = NULL;
	struct ncp_conn *conn = rqp->conn;
	struct mbuf *m;
	int error, flags=0;
	int sendwait;

	for(;;) {
		m = m_copym(top, 0, M_COPYALL, MB_WAIT);
/*		NCPDDEBUG(m);*/
		error = so_pru_sosend(so, to, NULL, m, NULL, flags, td);
		if (error == 0 || error == EINTR || error == ENETDOWN)
			break;
		if (rqp->rexmit == 0) break;
		rqp->rexmit--;
		tsleep(&sendwait, 0, "ncprsn", conn->li.timeout * hz);
		error = ncp_chkintr(conn, td);
		if (error == EINTR) break;
	}
	if (error) {
		log(LOG_INFO, "ncp_send: error %d for server %s", error, conn->li.server);
	}
	return error;
}

#ifdef IPX
/* 
 * Connect to specified server via IPX
 */
int
ncp_sock_connect_ipx(struct ncp_conn *conn) {
	struct sockaddr_ipx sipx;
	struct ipxpcb *npcb;
	struct thread *td = conn->td;
	int addrlen, error, count;

	sipx.sipx_port = htons(0);

	for (count = 0;;count++) {
		if (count > (IPXPORT_WELLKNOWN-IPXPORT_RESERVED)*2) {
			error = EADDRINUSE;
			goto bad;
		}
		conn->ncp_so = conn->wdg_so = NULL;
		checkbad(socreate(AF_IPX, &conn->ncp_so, SOCK_DGRAM, 0, td));
		if (conn->li.opt & NCP_OPT_WDOG)
			checkbad(socreate(AF_IPX, &conn->wdg_so, SOCK_DGRAM,0,td));
		addrlen = sizeof(sipx);
		sipx.sipx_family = AF_IPX;
		ipx_setnullnet(sipx.sipx_addr);
		ipx_setnullhost(sipx.sipx_addr);
		sipx.sipx_len = addrlen;
		error = sobind(conn->ncp_so, (struct sockaddr *)&sipx, td);
		if (error == 0) {
			if ((conn->li.opt & NCP_OPT_WDOG) == 0)
				break;
			sipx.sipx_addr = sotoipxpcb(conn->ncp_so)->ipxp_laddr;
			sipx.sipx_port = htons(ntohs(sipx.sipx_port) + 1);
			ipx_setnullnet(sipx.sipx_addr);
			ipx_setnullhost(sipx.sipx_addr);
			error = sobind(conn->wdg_so, (struct sockaddr *)&sipx, td);
		}
		if (!error) break;
		if (error != EADDRINUSE) goto bad;
		sipx.sipx_port = htons((ntohs(sipx.sipx_port)+4) & 0xfff8);
		soclose(conn->ncp_so, FNONBLOCK);
		if (conn->wdg_so)
			soclose(conn->wdg_so, FNONBLOCK);
	}
	npcb = sotoipxpcb(conn->ncp_so);
	npcb->ipxp_dpt = IPXPROTO_NCP;
	/* IPXrouted must be running, i.e. route must be presented */
	conn->li.addr.ipxaddr.sipx_len = sizeof(struct sockaddr_ipx);
	checkbad(ncp_soconnect(conn->ncp_so, &conn->li.addr.addr, td));
	if (conn->wdg_so) {
		sotoipxpcb(conn->wdg_so)->ipxp_laddr.x_net = npcb->ipxp_laddr.x_net;
		sotoipxpcb(conn->wdg_so)->ipxp_laddr.x_host= npcb->ipxp_laddr.x_host;
	}
	if (!error) {
		conn->flags |= NCPFL_SOCONN;
	}
#ifdef NCPBURST
	if (ncp_burst_enabled) {
		checkbad(socreate(AF_IPX, &conn->bc_so, SOCK_DGRAM, 0, td));
		bzero(&sipx, sizeof(sipx));
		sipx.sipx_len = sizeof(sipx);
		checkbad(sobind(conn->bc_so, (struct sockaddr *)&sipx, td));
		checkbad(ncp_soconnect(conn->bc_so, &conn->li.addr.addr, td));
	}
#endif
	if (!error) {
		conn->flags |= NCPFL_SOCONN;
		ncp_sock_checksum(conn, 0);
	}
	return error;
bad:
	ncp_sock_disconnect(conn);
	return (error);
}

int
ncp_sock_checksum(struct ncp_conn *conn, int enable) {

#ifdef SO_IPX_CHECKSUM
	if (enable) {
		sotoipxpcb(conn->ncp_so)->ipxp_flags |= IPXP_CHECKSUM;
	} else {
		sotoipxpcb(conn->ncp_so)->ipxp_flags &= ~IPXP_CHECKSUM;
	}
#endif
	return 0;
}
#endif

#ifdef INET
/* 
 * Connect to specified server via IP
 */
int
ncp_sock_connect_in(struct ncp_conn *conn) {
	struct sockaddr_in sin;
	struct thread *td = conn->td;
	int addrlen=sizeof(sin), error;

	conn->flags = 0;
	bzero(&sin,addrlen);
	conn->ncp_so = conn->wdg_so = NULL;
	checkbad(socreate(AF_INET, &conn->ncp_so, SOCK_DGRAM, IPPROTO_UDP, td));
	sin.sin_family = AF_INET;
	sin.sin_len = addrlen;
	checkbad(sobind(conn->ncp_so, (struct sockaddr *)&sin, td));
	checkbad(ncp_soconnect(conn->ncp_so,(struct sockaddr*)&conn->li.addr, td));
	if  (!error)
		conn->flags |= NCPFL_SOCONN;
	return error;
bad:
	ncp_sock_disconnect(conn);
	return (error);
}
#endif


/*
 * Connection expected to be locked
 */
int
ncp_sock_disconnect(struct ncp_conn *conn) {
	struct socket *so;
	conn->flags &= ~(NCPFL_SOCONN | NCPFL_ATTACHED | NCPFL_LOGGED);
	if (conn->ncp_so) {
		so = conn->ncp_so;
		conn->ncp_so = NULL;
		soshutdown(so, SHUT_RDWR);
		soclose(so, FNONBLOCK);
	}
	if (conn->wdg_so) {
		so = conn->wdg_so;
		conn->wdg_so = NULL;
		soshutdown(so, SHUT_RDWR);
		soclose(so, FNONBLOCK);
	}
#ifdef NCPBURST
	if (conn->bc_so) {
		so = conn->bc_so;
		conn->bc_so = NULL;
		soshutdown(so, SHUT_RDWR);
		soclose(so, FNONBLOCK);
	}
#endif
	return 0;
}

#ifdef IPX
static void
ncp_watchdog(struct ncp_conn *conn) {
	char *buf;
	int error, len, flags;
	struct socket *so;
	struct sockaddr *sa;
	struct sockbuf sio;

	sa = NULL;
	while (conn->wdg_so) { /* not a loop */
		so = conn->wdg_so;
		sbinit(&sio, 1000000);
		flags = MSG_DONTWAIT;
		error = so_pru_soreceive(so, &sa, NULL, &sio, NULL, &flags);
		if (error)
			break;
		len = sio.sb_cc;
		NCPSDEBUG("got watch dog %d\n",len);
		if (len != 2) {
			m_freem(sio.sb_mb);
			break;
		}
		buf = mtod(sio.sb_mb, char *);
		if (buf[1] != '?') {
			m_freem(sio.sb_mb);
			break;
		}
		buf[1] = 'Y';
		error = so_pru_sosend(so, sa, NULL, sio.sb_mb, NULL, 0, curthread);
		NCPSDEBUG("send watch dog %d\n",error);
		break;
	}
	if (sa)
		kfree(sa, M_SONAME);
	return;
}
#endif /* IPX */

void
ncp_check_conn(struct ncp_conn *conn) {
	if (conn == NULL || !(conn->flags & NCPFL_ATTACHED))
	        return;
	crit_enter();
	ncp_check_rq(conn);
	crit_exit();
#ifdef IPX
	ncp_watchdog(conn);
#endif
}
