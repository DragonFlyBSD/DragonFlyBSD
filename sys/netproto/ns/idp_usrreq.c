/*
 * Copyright (c) 1984, 1985, 1986, 1987, 1993
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
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
 *	@(#)idp_usrreq.c	8.1 (Berkeley) 6/10/93
 * $FreeBSD: src/sys/netns/idp_usrreq.c,v 1.9 1999/08/28 00:49:47 peter Exp $
 * $DragonFly: src/sys/netproto/ns/idp_usrreq.c,v 1.15 2008/03/07 11:34:21 sephe Exp $
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/protosw.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/errno.h>
#include <sys/stat.h>
#include <sys/thread2.h>

#include <net/if.h>
#include <net/route.h>

#include "ns.h"
#include "ns_pcb.h"
#include "ns_if.h"
#include "idp.h"
#include "idp_var.h"
#include "ns_error.h"

extern int idpcksum;	/* from ns_input.c */
extern long ns_pexseq;	/* from ns_input.c */
extern struct nspcb nsrawpcb; /* from ns_input.c */

struct  idpstat idpstat;

/*
 * IDP protocol implementation.
 */

struct	sockaddr_ns idp_ns = { sizeof(idp_ns), AF_NS };

/*
 *  This may also be called for raw listeners.
 */
void
idp_input(struct mbuf *m, ...)
{
	struct idp *idp = mtod(m, struct idp *);
	struct ifnet *ifp = m->m_pkthdr.rcvif;
	struct nspcb *nsp;
	__va_list ap;

	__va_start(ap, m);
	nsp = __va_arg(ap, struct nspcb *);
	__va_end(ap);

	if (nsp == NULL)
		panic("No nspcb");
	/*
	 * Construct sockaddr format source address.
	 * Stuff source address and datagram in user buffer.
	 */
	idp_ns.sns_addr = idp->idp_sna;
	if (ns_neteqnn(idp->idp_sna.x_net, ns_zeronet) && ifp) {
		struct ifaddr_container *ifac;

		TAILQ_FOREACH(ifac, &ifp->if_addrheads[mycpuid], ifa_link) {
			struct ifaddr *ifa = ifac->ifa;

			if (ifa->ifa_addr->sa_family == AF_NS) {
				idp_ns.sns_addr.x_net =
					IA_SNS(ifa)->sns_addr.x_net;
				break;
			}
		}
	}
	nsp->nsp_rpt = idp->idp_pt;
	if ( ! (nsp->nsp_flags & NSP_RAWIN) ) {
		m->m_len -= sizeof (struct idp);
		m->m_pkthdr.len -= sizeof (struct idp);
		m->m_data += sizeof (struct idp);
	}
	if (ssb_appendaddr(&nsp->nsp_socket->so_rcv, (struct sockaddr *)&idp_ns,
	    m, NULL) == 0)
		goto bad;
	sorwakeup(nsp->nsp_socket);
	return;
bad:
	m_freem(m);
}

void
idp_abort(struct nspcb *nsp)
{
	struct socket *so = nsp->nsp_socket;

	ns_pcbdisconnect(nsp);
	soisdisconnected(so);
}
/*
 * Drop connection, reporting
 * the specified error.
 */
void
idp_drop(struct nspcb *nsp, int errno)
{
	struct socket *so = nsp->nsp_socket;

	/*
	 * someday, in the xerox world
	 * we will generate error protocol packets
	 * announcing that the socket has gone away.
	 */
	/*if (TCPS_HAVERCVDSYN(tp->t_state)) {
		tp->t_state = TCPS_CLOSED;
		tcp_output(tp);
	}*/
	so->so_error = errno;
	ns_pcbdisconnect(nsp);
	soisdisconnected(so);
}

int noIdpRoute;

int
idp_output(struct mbuf *m0, struct socket *so, ...)
{
	struct nspcb *nsp = sotonspcb(so);
	struct mbuf *m;
	struct idp *idp;
	int len = 0;
	struct route *ro;
	struct mbuf *mprev = NULL;

	/*
	 * Calculate data length.
	 */
	for (m = m0; m; m = m->m_next) {
		mprev = m;
		len += m->m_len;
	}
	/*
	 * Make sure packet is actually of even length.
	 */

	if (len & 1) {
		m = mprev;
		if ((m->m_flags & M_EXT) == 0 &&
			(m->m_len + m->m_data < &m->m_dat[MLEN])) {
			m->m_len++;
		} else {
			struct mbuf *m1 = m_get(MB_DONTWAIT, MT_DATA);

			if (m1 == 0) {
				m_freem(m0);
				return (ENOBUFS);
			}
			m1->m_len = 1;
			* mtod(m1, char *) = 0;
			m->m_next = m1;
		}
		m0->m_pkthdr.len++;
	}

	/*
	 * Fill in mbuf with extended IDP header
	 * and addresses and length put into network format.
	 */
	m = m0;
	if (nsp->nsp_flags & NSP_RAWOUT) {
		idp = mtod(m, struct idp *);
	} else {
		M_PREPEND(m, sizeof (struct idp), MB_DONTWAIT);
		if (m == 0)
			return (ENOBUFS);
		idp = mtod(m, struct idp *);
		idp->idp_tc = 0;
		idp->idp_pt = nsp->nsp_dpt;
		idp->idp_sna = nsp->nsp_laddr;
		idp->idp_dna = nsp->nsp_faddr;
		len += sizeof (struct idp);
	}

	idp->idp_len = htons((u_short)len);

	if (idpcksum) {
		idp->idp_sum = 0;
		len = ((len - 1) | 1) + 1;
		idp->idp_sum = ns_cksum(m, len);
	} else
		idp->idp_sum = 0xffff;

	/*
	 * Output datagram.
	 */
	if (so->so_options & SO_DONTROUTE)
		return (ns_output(m, NULL,
		    (so->so_options & SO_BROADCAST) | NS_ROUTETOIF));
	/*
	 * Use cached route for previous datagram if
	 * possible.  If the previous net was the same
	 * and the interface was a broadcast medium, or
	 * if the previous destination was identical,
	 * then we are ok.
	 *
	 * NB: We don't handle broadcasts because that
	 *     would require 3 subroutine calls.
	 */
	ro = &nsp->nsp_route;
#ifdef ancient_history
	/*
	 * I think that this will all be handled in ns_pcbconnect!
	 */
	if (ro->ro_rt) {
		if(ns_neteq(nsp->nsp_lastdst, idp->idp_dna)) {
			/*
			 * This assumes we have no GH type routes
			 */
			if (ro->ro_rt->rt_flags & RTF_HOST) {
				if (!ns_hosteq(nsp->nsp_lastdst, idp->idp_dna))
					goto re_route;

			}
			if ((ro->ro_rt->rt_flags & RTF_GATEWAY) == 0) {
				struct ns_addr *dst =
						&satons_addr(ro->ro_dst);
				dst->x_host = idp->idp_dna.x_host;
			}
			/*
			 * Otherwise, we go through the same gateway
			 * and dst is already set up.
			 */
		} else {
		re_route:
			RTFREE(ro->ro_rt);
			ro->ro_rt = NULL;
		}
	}
	nsp->nsp_lastdst = idp->idp_dna;
#endif /* ancient_history */
	if (noIdpRoute) ro = 0;
	return (ns_output(m, ro, so->so_options & SO_BROADCAST));
}

/* ARGSUSED */
int
idp_ctloutput(int req, struct socket *so, int level, int name,
	      struct mbuf **value)
{
	struct mbuf *m;
	struct nspcb *nsp = sotonspcb(so);
	int mask, error = 0;

	if (nsp == NULL)
		return (EINVAL);

	switch (req) {

	case PRCO_GETOPT:
		if (value==NULL)
			return (EINVAL);
		m = m_get(MB_DONTWAIT, MT_DATA);
		if (m==NULL)
			return (ENOBUFS);
		switch (name) {

		case SO_ALL_PACKETS:
			mask = NSP_ALL_PACKETS;
			goto get_flags;

		case SO_HEADERS_ON_INPUT:
			mask = NSP_RAWIN;
			goto get_flags;

		case SO_HEADERS_ON_OUTPUT:
			mask = NSP_RAWOUT;
		get_flags:
			m->m_len = sizeof(short);
			*mtod(m, short *) = nsp->nsp_flags & mask;
			break;

		case SO_DEFAULT_HEADERS:
			m->m_len = sizeof(struct idp);
			{
				struct idp *idp = mtod(m, struct idp *);
				idp->idp_len = 0;
				idp->idp_sum = 0;
				idp->idp_tc = 0;
				idp->idp_pt = nsp->nsp_dpt;
				idp->idp_dna = nsp->nsp_faddr;
				idp->idp_sna = nsp->nsp_laddr;
			}
			break;

		case SO_SEQNO:
			m->m_len = sizeof(long);
			*mtod(m, long *) = ns_pexseq++;
			break;

		default:
			error = EINVAL;
		}
		*value = m;
		break;

	case PRCO_SETOPT:
		switch (name) {
			int *ok;

		case SO_ALL_PACKETS:
			mask = NSP_ALL_PACKETS;
			goto set_head;

		case SO_HEADERS_ON_INPUT:
			mask = NSP_RAWIN;
			goto set_head;

		case SO_HEADERS_ON_OUTPUT:
			mask = NSP_RAWOUT;
		set_head:
			if (value && *value) {
				ok = mtod(*value, int *);
				if (*ok)
					nsp->nsp_flags |= mask;
				else
					nsp->nsp_flags &= ~mask;
			} else error = EINVAL;
			break;

		case SO_DEFAULT_HEADERS:
			{
				struct idp *idp
				    = mtod(*value, struct idp *);
				nsp->nsp_dpt = idp->idp_pt;
			}
			break;
#ifdef NSIP

		case SO_NSIP_ROUTE:
			error = nsip_route(*value);
			break;
#endif /* NSIP */
		default:
			error = EINVAL;
		}
		if (value && *value)
			m_freem(*value);
		break;
	}
	return (error);
}


/*
 *  IDP_USRREQ PROCEDURES
 */

static int
idp_usr_abort(struct socket *so)
{
	struct nspcb *nsp = sotonspcb(so);
	int error;

	if (nsp) {
		ns_pcbdetach(nsp);
		sofree(so);
		soisdisconnected(so);
		error = 0;
	} else {
		error = EINVAL;
	}
	return(error);
}

static int
idp_attach(struct socket *so, int proto, struct pru_attach_info *ai)
{
	struct nspcb *nsp = sotonspcb(so);
	int error;

	if (nsp != NULL)
		return(EINVAL);
	if ((error = ns_pcballoc(so, &nspcb)) != 0)
		return(error);
	error = soreserve(so, 2048, 2048, ai->sb_rlimit);
	return(error);
}

static int
idp_raw_attach(struct socket *so, int proto, struct pru_attach_info *ai)
{
	struct nspcb *nsp = sotonspcb(so);
	int error;

#ifdef NS_PRIV_SOCKETS
	if ((so->so_state & SS_PRIV) == 0)
		return(EINVAL);
#endif
	if (nsp != NULL)
		return(EINVAL);
	if ((error = ns_pcballoc(so, &nsrawpcb)) != 0)
		return(error);
	if ((error = soreserve(so, 2048, 2048, ai->sb_rlimit)) != 0)
		return(error);
	nsp = sotonspcb(so);
	nsp->nsp_faddr.x_host = ns_broadhost;
	nsp->nsp_flags = NSP_RAWIN | NSP_RAWOUT;
	return(0);
}

static int
idp_bind(struct socket *so, struct sockaddr *nam, struct thread *td)
{
	struct nspcb *nsp = sotonspcb(so);
	int error;

	if (nsp)
		error = ns_pcbbind(nsp, nam);
	else
		error = EINVAL;
	return(error);
}

static int
idp_connect(struct socket *so, struct sockaddr *nam, struct thread *td)
{
	struct nspcb *nsp = sotonspcb(so);
	int error;
	
	if (nsp) {
		if (!ns_nullhost(nsp->nsp_faddr))
			error = EISCONN;
		else if ((error = ns_pcbconnect(nsp, nam)) == 0)
			soisconnected(so);
	} else {
		error = EINVAL;
	}
	return(error);
}

static int
idp_detach(struct socket *so)
{
	struct nspcb *nsp = sotonspcb(so);
	int error;

	if (nsp == NULL) {
		error = ENOTCONN;
	} else {
		ns_pcbdetach(nsp);
		error = 0;
	}
	return(error);
}

static int
idp_usr_disconnect(struct socket *so)
{
	struct nspcb *nsp = sotonspcb(so);
	int error;

	if (nsp) {
		if (ns_nullhost(nsp->nsp_faddr)) {
			error = ENOTCONN;
		} else {
			error = 0;
			ns_pcbdisconnect(nsp);
			soisdisconnected(so);
		}
	} else {
		error = EINVAL;
	}
	return(error);
}

static int
idp_peeraddr(struct socket *so, struct sockaddr **pnam)
{
	struct nspcb *nsp = sotonspcb(so);
	int error;

	if (nsp) {
		ns_setpeeraddr(nsp, pnam);
		error = 0;
	} else {
		error = EINVAL;
	}
	return(error);
}

static int
idp_send(struct socket *so, int flags, struct mbuf *m,
	struct sockaddr *addr, struct mbuf *control,
	struct thread *td)
{
	struct nspcb *nsp = sotonspcb(so);
	struct ns_addr laddr;
	int error;

	if (nsp == NULL)
		return(EINVAL);
	if (control && control->m_len) {
		error = EINVAL;
		goto release;
	}

	crit_enter();
	if (addr) {
		laddr = nsp->nsp_laddr;
		if (!ns_nullhost(nsp->nsp_faddr))
			error = EISCONN;
		else
			error = ns_pcbconnect(nsp, addr);
	} else {
		if (ns_nullhost(nsp->nsp_faddr))
			error = ENOTCONN;
		else
			error = 0;
	}
	if (error == 0) {
		error = idp_output(m, so);
		m = NULL;
		if (addr) {
			ns_pcbdisconnect(nsp);
			nsp->nsp_laddr.x_host = laddr.x_host;
			nsp->nsp_laddr.x_port = laddr.x_port;
		}
	}
	crit_exit();
release:
	if (control)
		m_freem(control);
	if (m)
		m_freem(m);
	return(error);
}

static int
idp_sockaddr(struct socket *so, struct sockaddr **pnam)
{
	struct nspcb *nsp = sotonspcb(so);
	int error;

	if (nsp) {
		ns_setsockaddr(nsp, pnam);
		error = 0;
	} else {
		error = EINVAL;
	}
	return(error);
}

static int
idp_shutdown(struct socket *so)
{
	socantsendmore(so);
	return(0);
}

struct pr_usrreqs idp_usrreqs = {
	.pru_abort = idp_usr_abort,
	.pru_accept = pru_accept_notsupp,
	.pru_attach = idp_attach,
	.pru_bind = idp_bind,
	.pru_connect = idp_connect,
	.pru_connect2 = pru_connect2_notsupp,
	.pru_control = ns_control,
	.pru_detach = idp_detach,
	.pru_disconnect = idp_usr_disconnect,
	.pru_listen = pru_listen_notsupp,
	.pru_peeraddr = idp_peeraddr,
	.pru_rcvd = pru_rcvd_notsupp,
	.pru_rcvoob = pru_rcvoob_notsupp,
	.pru_send = idp_send,
	.pru_sense = pru_sense_null,
	.pru_shutdown = idp_shutdown,
	.pru_sockaddr = idp_sockaddr,
	.pru_sosend = sosend,
	.pru_soreceive = soreceive
};

struct pr_usrreqs idp_raw_usrreqs = {
	.pru_abort = idp_usr_abort,
	.pru_accept = pru_accept_notsupp,
	.pru_attach = idp_raw_attach,
	.pru_bind = idp_bind,
	.pru_connect = idp_connect,
	.pru_connect2 = pru_connect2_notsupp,
	.pru_control = ns_control,
	.pru_detach = idp_detach,
	.pru_disconnect = idp_usr_disconnect,
	.pru_listen = pru_listen_notsupp,
	.pru_peeraddr = idp_peeraddr,
	.pru_rcvd = pru_rcvd_notsupp,
	.pru_rcvoob = pru_rcvoob_notsupp,
	.pru_send = idp_send,
	.pru_sense = pru_sense_null,
	.pru_shutdown = idp_shutdown,
	.pru_sockaddr = idp_sockaddr,
	.pru_sosend = sosend,
	.pru_soreceive = soreceive
};

