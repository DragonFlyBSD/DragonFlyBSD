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
 *	@(#)raw_usrreq.c	8.1 (Berkeley) 6/10/93
 * $FreeBSD: src/sys/net/raw_usrreq.c,v 1.18 1999/08/28 00:48:28 peter Exp $
 * $DragonFly: src/sys/net/raw_usrreq.c,v 1.14 2007/06/24 20:00:00 dillon Exp $
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/mbuf.h>
#include <sys/proc.h>
#include <sys/priv.h>
#include <sys/protosw.h>
#include <sys/socket.h>
#include <sys/socketvar.h>

#include <net/raw_cb.h>

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

			n = m_copypacket(m, MB_DONTWAIT);
			if (n != NULL) {
				if (ssb_appendaddr(&last->so_rcv, src, n,
						 NULL) == 0) {
					/* should notify about lost packet */
					m_freem(n);
				} else {
					sorwakeup(last);
				}
			}
		}
		last = rp->rcb_socket;
	}
	if (last) {
		if (ssb_appendaddr(&last->so_rcv, src, m, NULL) == 0)
			m_freem(m);
		else
			sorwakeup(last);
	} else {
		m_freem(m);
	}
}

/*ARGSUSED*/
void
raw_ctlinput(int cmd, struct sockaddr *arg, void *dummy)
{

	if (cmd < 0 || cmd > PRC_NCMDS)
		return;
	/* INCOMPLETE */
}

static int
raw_uabort(struct socket *so)
{
	struct rawcb *rp = sotorawcb(so);

	if (rp == NULL)
		return EINVAL;
	raw_disconnect(rp);
	sofree(so);
	soisdisconnected(so);
	return 0;
}

/* pru_accept is EOPNOTSUPP */

static int
raw_uattach(struct socket *so, int proto, struct pru_attach_info *ai)
{
	struct rawcb *rp = sotorawcb(so);
	int error;

	if (rp == NULL)
		return EINVAL;
	if ((error = priv_check_cred(ai->p_ucred, PRIV_ROOT, NULL_CRED_OKAY)) != 0)
		return error;
	return raw_attach(so, proto, ai->sb_rlimit);
}

static int
raw_ubind(struct socket *so, struct sockaddr *nam, struct thread *td)
{
	return EINVAL;
}

static int
raw_uconnect(struct socket *so, struct sockaddr *nam, struct thread *td)
{
	return EINVAL;
}

/* pru_connect2 is EOPNOTSUPP */
/* pru_control is EOPNOTSUPP */

static int
raw_udetach(struct socket *so)
{
	struct rawcb *rp = sotorawcb(so);

	if (rp == NULL)
		return EINVAL;

	raw_detach(rp);
	return 0;
}

static int
raw_udisconnect(struct socket *so)
{
	struct rawcb *rp = sotorawcb(so);

	if (rp == NULL)
		return EINVAL;
	if (rp->rcb_faddr == NULL) {
		return ENOTCONN;
	}
	raw_disconnect(rp);
	soisdisconnected(so);
	return 0;
}

/* pru_listen is EOPNOTSUPP */

static int
raw_upeeraddr(struct socket *so, struct sockaddr **nam)
{
	struct rawcb *rp = sotorawcb(so);

	if (rp == NULL)
		return EINVAL;
	if (rp->rcb_faddr == NULL) {
		return ENOTCONN;
	}
	*nam = dup_sockaddr(rp->rcb_faddr);
	return 0;
}

/* pru_rcvd is EOPNOTSUPP */
/* pru_rcvoob is EOPNOTSUPP */

static int
raw_usend(struct socket *so, int flags, struct mbuf *m,
	  struct sockaddr *nam, struct mbuf *control, struct thread *td)
{
	int error;
	struct rawcb *rp = sotorawcb(so);
	struct pr_output_info oi;

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
	if (nam) {
		if (rp->rcb_faddr) {
			error = EISCONN;
			goto release;
		}
		rp->rcb_faddr = nam;
	} else if (rp->rcb_faddr == NULL) {
		error = ENOTCONN;
		goto release;
	}
	oi.p_pid = td->td_proc->p_pid;
	error = (*so->so_proto->pr_output)(m, so, &oi);
	m = NULL;
	if (nam)
		rp->rcb_faddr = NULL;
release:
	if (m != NULL)
		m_freem(m);
	return (error);
}

/* pru_sense is null */

static int
raw_ushutdown(struct socket *so)
{
	struct rawcb *rp = sotorawcb(so);

	if (rp == NULL)
		return EINVAL;
	socantsendmore(so);
	return 0;
}

static int
raw_usockaddr(struct socket *so, struct sockaddr **nam)
{
	struct rawcb *rp = sotorawcb(so);

	if (rp == NULL)
		return EINVAL;
	if (rp->rcb_laddr == NULL)
		return EINVAL;
	*nam = dup_sockaddr(rp->rcb_laddr);
	return 0;
}

struct pr_usrreqs raw_usrreqs = {
	.pru_abort = raw_uabort,
	.pru_accept = pru_accept_notsupp,
	.pru_attach = raw_uattach,
	.pru_bind = raw_ubind,
	.pru_connect = raw_uconnect,
	.pru_connect2 = pru_connect2_notsupp,
	.pru_control = pru_control_notsupp,
	.pru_detach = raw_udetach, 
	.pru_disconnect = raw_udisconnect,
	.pru_listen = pru_listen_notsupp,
	.pru_peeraddr = raw_upeeraddr,
	.pru_rcvd = pru_rcvd_notsupp,
	.pru_rcvoob = pru_rcvoob_notsupp,
	.pru_send = raw_usend,
	.pru_sense = pru_sense_null,
	.pru_shutdown = raw_ushutdown,
	.pru_sockaddr = raw_usockaddr,
	.pru_sosend = sosend,
	.pru_soreceive = soreceive
};
