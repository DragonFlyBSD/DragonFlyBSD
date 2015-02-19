/*
 * Copyright (c) 1980, 1986, 1993
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
 *	@(#)raw_usrreq.c	8.1 (Berkeley) 6/10/93
 * $FreeBSD: src/sys/net/raw_usrreq.c,v 1.18 1999/08/28 00:48:28 peter Exp $
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/mbuf.h>
#include <sys/proc.h>
#include <sys/priv.h>
#include <sys/protosw.h>
#include <sys/socket.h>
#include <sys/socketvar.h>

#include <sys/socketvar2.h>
#include <sys/msgport2.h>

#include <net/raw_cb.h>


static struct lwkt_token raw_token = LWKT_TOKEN_INITIALIZER(raw_token);

/*
 * Initialize raw connection block q.
 */
void
raw_init(void)
{
	LIST_INIT(&rawcb_list);
}

/************************************************************************
 *			 RAW PROTOCOL INTERFACE				*
 ************************************************************************/

/*
 * Raw protocol input routine.  Find the socket associated with the packet(s)
 * and move them over.  If nothing exists for this packet, drop it.  This
 * routine is indirect called via rts_input() and will be serialized on
 * cpu 0.
 *
 * Most other raw protocol interface functions are also serialized XXX.
 */
void
raw_input(struct mbuf *m0, const struct sockproto *proto,
	  const struct sockaddr *src, const struct sockaddr *dst,
	  const struct rawcb *skip)
{
	struct rawcb *rp;
	struct mbuf *m = m0;
	struct socket *last;

	lwkt_gettoken(&raw_token);

	last = NULL;
	LIST_FOREACH(rp, &rawcb_list, list) {
		if (rp == skip)
			continue;
		if (rp->rcb_proto.sp_family != proto->sp_family)
			continue;
		if (rp->rcb_proto.sp_protocol  &&
		    rp->rcb_proto.sp_protocol != proto->sp_protocol)
			continue;
		/*
		 * We assume the lower level routines have
		 * placed the address in a canonical format
		 * suitable for a structure comparison.
		 *
		 * Note that if the lengths are not the same
		 * the comparison will fail at the first byte.
		 */
		if (rp->rcb_laddr && !sa_equal(rp->rcb_laddr, dst))
			continue;
		if (rp->rcb_faddr && !sa_equal(rp->rcb_faddr, src))
			continue;
		if (last) {
			struct mbuf *n;

			n = m_copypacket(m, M_NOWAIT);
			if (n != NULL) {
				lwkt_gettoken(&last->so_rcv.ssb_token);
				if (ssb_appendaddr(&last->so_rcv, src, n,
						 NULL) == 0) {
					/* should notify about lost packet */
					m_freem(n);
				} else {
					sorwakeup(last);
				}
				lwkt_reltoken(&last->so_rcv.ssb_token);
			}
		}
		last = rp->rcb_socket;
	}
	if (last) {
		lwkt_gettoken(&last->so_rcv.ssb_token);
		if (ssb_appendaddr(&last->so_rcv, src, m, NULL) == 0)
			m_freem(m);
		else
			sorwakeup(last);
		lwkt_reltoken(&last->so_rcv.ssb_token);
	} else {
		m_freem(m);
	}
	lwkt_reltoken(&raw_token);
}

/*
 * nm_cmd, nm_arg, nm_extra
 */
void
raw_ctlinput(netmsg_t msg)
{
	int error = 0;

	if (msg->ctlinput.nm_cmd < 0 || msg->ctlinput.nm_cmd > PRC_NCMDS)
		;
	lwkt_replymsg(&msg->lmsg, error);
}

/*
 * NOTE: (so) is referenced from soabort*() and netmsg_pru_abort()
 *	 will sofree() it when we return.
 */
static void
raw_uabort(netmsg_t msg)
{
	struct rawcb *rp = sotorawcb(msg->base.nm_so);
	int error;

	if (rp) {
		raw_disconnect(rp);
		soisdisconnected(msg->base.nm_so);
		error = 0;
	} else {
		error = EINVAL;
	}
	lwkt_replymsg(&msg->lmsg, error);
}

/* pru_accept is EOPNOTSUPP */

static void
raw_uattach(netmsg_t msg)
{
	struct socket *so = msg->base.nm_so;
	int proto = msg->attach.nm_proto;
	struct pru_attach_info *ai = msg->attach.nm_ai;
	struct rawcb *rp;
	int error;

	rp = sotorawcb(so);
	if (rp) {
		error = priv_check_cred(ai->p_ucred, PRIV_ROOT, NULL_CRED_OKAY);
		if (error == 0)
			error = raw_attach(so, proto, ai->sb_rlimit);
	} else {
		error = EINVAL;
	}
	lwkt_replymsg(&msg->lmsg, error);
}

static void
raw_ubind(netmsg_t msg)
{
	lwkt_replymsg(&msg->lmsg, EINVAL);
}

static void
raw_uconnect(netmsg_t msg)
{
	lwkt_replymsg(&msg->lmsg, EINVAL);
}

/* pru_connect2 is EOPNOTSUPP */
/* pru_control is EOPNOTSUPP */

static void
raw_udetach(netmsg_t msg)
{
	struct rawcb *rp = sotorawcb(msg->base.nm_so);
	int error;

	if (rp) {
		raw_detach(rp);
		error = 0;
	} else {
		error = EINVAL;
	}
	lwkt_replymsg(&msg->lmsg, error);
}

static void
raw_udisconnect(netmsg_t msg)
{
	struct socket *so = msg->base.nm_so;
	struct rawcb *rp;
	int error;

	rp = sotorawcb(so);
	if (rp == NULL) {
		error = EINVAL;
	} else if (rp->rcb_faddr == NULL) {
		error = ENOTCONN;
	} else {
		soreference(so);
		raw_disconnect(rp);
		soisdisconnected(so);
		sofree(so);
		error = 0;
	}
	lwkt_replymsg(&msg->lmsg, error);
}

/* pru_listen is EOPNOTSUPP */

static void
raw_upeeraddr(netmsg_t msg)
{
	struct rawcb *rp = sotorawcb(msg->base.nm_so);
	int error;

	if (rp == NULL) {
		error = EINVAL;
	} else if (rp->rcb_faddr == NULL) {
		error = ENOTCONN;
	} else {
		*msg->peeraddr.nm_nam = dup_sockaddr(rp->rcb_faddr);
		error = 0;
	}
	lwkt_replymsg(&msg->lmsg, error);
}

/* pru_rcvd is EOPNOTSUPP */
/* pru_rcvoob is EOPNOTSUPP */

static void
raw_usend(netmsg_t msg)
{
	struct socket *so = msg->base.nm_so;
	struct mbuf *m = msg->send.nm_m;
	struct mbuf *control = msg->send.nm_control;
	struct rawcb *rp = sotorawcb(so);
	struct pr_output_info oi;
	int flags = msg->send.nm_flags;
	int error;

	if (rp == NULL) {
		error = EINVAL;
		goto release;
	}

	if (flags & PRUS_OOB) {
		error = EOPNOTSUPP;
		goto release;
	}

	if (control && control->m_len) {
		error = EOPNOTSUPP;
		goto release;
	}
	if (msg->send.nm_addr) {
		if (rp->rcb_faddr) {
			error = EISCONN;
			goto release;
		}
		rp->rcb_faddr = msg->send.nm_addr;
	} else if (rp->rcb_faddr == NULL) {
		error = ENOTCONN;
		goto release;
	}
	oi.p_pid = msg->send.nm_td->td_proc->p_pid;
	error = (*so->so_proto->pr_output)(m, so, &oi);
	m = NULL;
	if (msg->send.nm_addr)
		rp->rcb_faddr = NULL;
release:
	if (m != NULL)
		m_freem(m);
	lwkt_replymsg(&msg->lmsg, error);
}

/* pru_sense is null */

static void
raw_ushutdown(netmsg_t msg)
{
	struct rawcb *rp = sotorawcb(msg->base.nm_so);
	int error;

	if (rp) {
		socantsendmore(msg->base.nm_so);
		error = 0;
	} else {
		error = EINVAL;
	}
	lwkt_replymsg(&msg->lmsg, error);
}

static void
raw_usockaddr(netmsg_t msg)
{
	struct rawcb *rp = sotorawcb(msg->base.nm_so);
	int error;

	if (rp == NULL) {
		error = EINVAL;
	} else if (rp->rcb_laddr == NULL) {
		error = EINVAL;
	} else {
		*msg->sockaddr.nm_nam = dup_sockaddr(rp->rcb_laddr);
		error = 0;
	}
	lwkt_replymsg(&msg->lmsg, error);
}

struct pr_usrreqs raw_usrreqs = {
	.pru_abort = raw_uabort,
	.pru_accept = pr_generic_notsupp,
	.pru_attach = raw_uattach,
	.pru_bind = raw_ubind,
	.pru_connect = raw_uconnect,
	.pru_connect2 = pr_generic_notsupp,
	.pru_control = pr_generic_notsupp,
	.pru_detach = raw_udetach, 
	.pru_disconnect = raw_udisconnect,
	.pru_listen = pr_generic_notsupp,
	.pru_peeraddr = raw_upeeraddr,
	.pru_rcvd = pr_generic_notsupp,
	.pru_rcvoob = pr_generic_notsupp,
	.pru_send = raw_usend,
	.pru_sense = pru_sense_null,
	.pru_shutdown = raw_ushutdown,
	.pru_sockaddr = raw_usockaddr,
	.pru_sosend = sosend,
	.pru_soreceive = soreceive
};
