/*
 * Copyright (C) 1995, 1996, 1997, and 1998 WIDE Project.
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
 * 3. Neither the name of the project nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE PROJECT AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE PROJECT OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD: src/sys/netkey/keysock.c,v 1.1.2.4 2003/01/11 19:10:59 ume Exp $
 * $KAME: keysock.c,v 1.25 2001/08/13 20:07:41 itojun Exp $
 */

#include "opt_ipsec.h"

/* This code has derived from sys/net/rtsock.c on FreeBSD2.2.5 */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/sysctl.h>
#include <sys/mbuf.h>
#include <sys/malloc.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/domain.h>
#include <sys/protosw.h>
#include <sys/errno.h>
#include <sys/socketvar2.h>

#include <sys/thread2.h>
#include <sys/msgport2.h>

#include <net/raw_cb.h>
#include <net/route.h>
#include <net/netmsg.h>
#include <netinet/in.h>

#include <net/pfkeyv2.h>
#include <net/netmsg2.h>

#include "keydb.h"
#include "key.h"
#include "keysock.h"
#include "key_debug.h"

#include <machine/stdarg.h>

struct sockaddr key_dst = { 2, PF_KEY, };
struct sockaddr key_src = { 2, PF_KEY, };

static int key_sendup0 (struct rawcb *, struct mbuf *, int);

struct pfkeystat pfkeystat;

/*
 * key_output()
 */
int
key_output(struct mbuf *m, struct socket *so, ...)
{
	struct sadb_msg *msg;
	int len, error = 0;

	if (m == NULL)
		panic("key_output: NULL pointer was passed.");

	pfkeystat.out_total++;
	pfkeystat.out_bytes += m->m_pkthdr.len;

	len = m->m_pkthdr.len;
	if (len < sizeof(struct sadb_msg)) {
		pfkeystat.out_tooshort++;
		error = EINVAL;
		goto end;
	}

	if (m->m_len < sizeof(struct sadb_msg)) {
		if ((m = m_pullup(m, sizeof(struct sadb_msg))) == NULL) {
			pfkeystat.out_nomem++;
			error = ENOBUFS;
			goto end;
		}
	}

	if ((m->m_flags & M_PKTHDR) == 0)
		panic("key_output: not M_PKTHDR ??");

	KEYDEBUG(KEYDEBUG_KEY_DUMP, kdebug_mbuf(m));

	msg = mtod(m, struct sadb_msg *);
	pfkeystat.out_msgtype[msg->sadb_msg_type]++;
	if (len != PFKEY_UNUNIT64(msg->sadb_msg_len)) {
		pfkeystat.out_invlen++;
		error = EINVAL;
		goto end;
	}

	/*XXX giant lock*/
	lwkt_gettoken(&key_token);
	error = key_parse(m, so);
	m = NULL;
	lwkt_reltoken(&key_token);
end:
	if (m)
		m_freem(m);
	return error;
}

/*
 * send message to the socket.
 */
static int
key_sendup0(struct rawcb *rp, struct mbuf *m, int promisc)
{
	int error;

	if (promisc) {
		struct sadb_msg *pmsg;

		M_PREPEND(m, sizeof(struct sadb_msg), MB_DONTWAIT);
		if (m && m->m_len < sizeof(struct sadb_msg))
			m = m_pullup(m, sizeof(struct sadb_msg));
		if (!m) {
			pfkeystat.in_nomem++;
			m_freem(m);
			return ENOBUFS;
		}
		m->m_pkthdr.len += sizeof(*pmsg);

		pmsg = mtod(m, struct sadb_msg *);
		bzero(pmsg, sizeof(*pmsg));
		pmsg->sadb_msg_version = PF_KEY_V2;
		pmsg->sadb_msg_type = SADB_X_PROMISC;
		pmsg->sadb_msg_len = PFKEY_UNIT64(m->m_pkthdr.len);
		/* pid and seq? */

		pfkeystat.in_msgtype[pmsg->sadb_msg_type]++;
	}

	lwkt_gettoken(&key_token);
	if (!ssb_appendaddr(&rp->rcb_socket->so_rcv, &key_src, m, NULL)) {
		pfkeystat.in_nomem++;
		m_freem(m);
		error = ENOBUFS;
	} else {
		error = 0;
	}
	lwkt_reltoken(&key_token);
	sorwakeup(rp->rcb_socket);
	return error;
}

/*
 * XXX this interface should be obsoleted.
 *
 * Parameters:
 *	target:	target of the resulting message
 */
int
key_sendup(struct socket *so, struct sadb_msg *msg, u_int len,
	   int target)
{
	struct mbuf *m, *n, *mprev;
	int tlen;

	/* sanity check */
	if (so == NULL || msg == NULL)
		panic("key_sendup: NULL pointer was passed.");

	KEYDEBUG(KEYDEBUG_KEY_DUMP,
		kprintf("key_sendup: \n");
		kdebug_sadb(msg));

	/*
	 * we increment statistics here, just in case we have ENOBUFS
	 * in this function.
	 */
	pfkeystat.in_total++;
	pfkeystat.in_bytes += len;
	pfkeystat.in_msgtype[msg->sadb_msg_type]++;

	/*
	 * Get mbuf chain whenever possible (not clusters),
	 * to save socket buffer.  We'll be generating many SADB_ACQUIRE
	 * messages to listening key sockets.  If we simply allocate clusters,
	 * ssb_appendaddr() will raise ENOBUFS due to too little ssb_space().
	 * ssb_space() computes # of actual data bytes AND mbuf region.
	 *
	 * TODO: SADB_ACQUIRE filters should be implemented.
	 */
	tlen = len;
	m = mprev = NULL;
	while (tlen > 0) {
		if (tlen == len) {
			MGETHDR(n, MB_DONTWAIT, MT_DATA);
			n->m_len = MHLEN;
		} else {
			MGET(n, MB_DONTWAIT, MT_DATA);
			n->m_len = MLEN;
		}
		if (!n) {
			pfkeystat.in_nomem++;
			return ENOBUFS;
		}
		if (tlen >= MCLBYTES) {	/*XXX better threshold? */
			MCLGET(n, MB_DONTWAIT);
			if ((n->m_flags & M_EXT) == 0) {
				m_free(n);
				m_freem(m);
				pfkeystat.in_nomem++;
				return ENOBUFS;
			}
			n->m_len = MCLBYTES;
		}

		if (tlen < n->m_len)
			n->m_len = tlen;
		n->m_next = NULL;
		if (m == NULL)
			m = mprev = n;
		else {
			mprev->m_next = n;
			mprev = n;
		}
		tlen -= n->m_len;
		n = NULL;
	}
	m->m_pkthdr.len = len;
	m->m_pkthdr.rcvif = NULL;
	m_copyback(m, 0, len, (caddr_t)msg);

	/* avoid duplicated statistics */
	pfkeystat.in_total--;
	pfkeystat.in_bytes -= len;
	pfkeystat.in_msgtype[msg->sadb_msg_type]--;

	return key_sendup_mbuf(so, m, target);
}

/* so can be NULL if target != KEY_SENDUP_ONE */
int
key_sendup_mbuf(struct socket *so, struct mbuf *m, int target)
{
	struct mbuf *n;
	struct keycb *kp;
	int sendup;
	struct rawcb *rp;
	int error = 0;

	if (m == NULL)
		panic("key_sendup_mbuf: NULL pointer was passed.");
	if (so == NULL && target == KEY_SENDUP_ONE)
		panic("key_sendup_mbuf: NULL pointer was passed.");

	pfkeystat.in_total++;
	pfkeystat.in_bytes += m->m_pkthdr.len;
	if (m->m_len < sizeof(struct sadb_msg)) {
#if 1
		m = m_pullup(m, sizeof(struct sadb_msg));
		if (m == NULL) {
			pfkeystat.in_nomem++;
			return ENOBUFS;
		}
#else
		/* don't bother pulling it up just for stats */
#endif
	}
	if (m->m_len >= sizeof(struct sadb_msg)) {
		struct sadb_msg *msg;
		msg = mtod(m, struct sadb_msg *);
		pfkeystat.in_msgtype[msg->sadb_msg_type]++;
	}

	lwkt_gettoken(&key_token);

	LIST_FOREACH(rp, &rawcb_list, list)
	{
		if (rp->rcb_proto.sp_family != PF_KEY)
			continue;
		if (rp->rcb_proto.sp_protocol
		 && rp->rcb_proto.sp_protocol != PF_KEY_V2) {
			continue;
		}

		kp = (struct keycb *)rp;

		/*
		 * If you are in promiscuous mode, and when you get broadcasted
		 * reply, you'll get two PF_KEY messages.
		 * (based on pf_key@inner.net message on 14 Oct 1998)
		 */
		if (((struct keycb *)rp)->kp_promisc) {
			if ((n = m_copy(m, 0, (int)M_COPYALL)) != NULL) {
				key_sendup0(rp, n, 1);
				n = NULL;
			}
		}

		/* the exact target will be processed later */
		if (so && sotorawcb(so) == rp)
			continue;

		sendup = 0;
		switch (target) {
		case KEY_SENDUP_ONE:
			/* the statement has no effect */
			if (so && sotorawcb(so) == rp)
				sendup++;
			break;
		case KEY_SENDUP_ALL:
			sendup++;
			break;
		case KEY_SENDUP_REGISTERED:
			if (kp->kp_registered)
				sendup++;
			break;
		}
		pfkeystat.in_msgtarget[target]++;

		if (!sendup)
			continue;

		if ((n = m_copy(m, 0, (int)M_COPYALL)) == NULL) {
			m_freem(m);
			pfkeystat.in_nomem++;
			lwkt_reltoken(&key_token);
			return ENOBUFS;
		}

		if ((error = key_sendup0(rp, n, 0)) != 0) {
			lwkt_reltoken(&key_token);
			m_freem(m);
			return error;
		}

		n = NULL;
	}
	lwkt_reltoken(&key_token);

	if (so) {
		error = key_sendup0(sotorawcb(so), m, 0);
		m = NULL;
	} else {
		error = 0;
		m_freem(m);
	}
	return error;
}

/*
 * key_abort()
 * derived from net/rtsock.c:rts_abort()
 */
static void
key_abort(netmsg_t msg)
{
	lwkt_gettoken(&key_token);

	raw_usrreqs.pru_abort(msg);
	/* msg invalid now */

	lwkt_reltoken(&key_token);
}

/*
 * key_attach()
 * derived from net/rtsock.c:rts_attach()
 */
static void
key_attach(netmsg_t msg)
{
	struct socket *so = msg->attach.base.nm_so;
	int proto = msg->attach.nm_proto;
	struct pru_attach_info *ai = msg->attach.nm_ai;
	struct keycb *kp;
	struct netmsg_pru_attach smsg;
	int error;

	if (sotorawcb(so) != NULL) {
		error = EISCONN;	/* XXX panic? */
		goto out;
	}

	/* XXX */
	kp = kmalloc(sizeof *kp, M_PCB, M_WAITOK | M_ZERO);

	/*
	 * The critical section is necessary to block protocols from sending
	 * error notifications (like RTM_REDIRECT or RTM_LOSING) while
	 * this PCB is extant but incompletely initialized.
	 * Probably we should try to do more of this work beforehand and
	 * eliminate the critical section.
	 */
	lwkt_gettoken(&key_token);
	so->so_pcb = (caddr_t)kp;
	soreference(so);	/* so_pcb assignment */

	netmsg_init(&smsg.base, so, &netisr_adone_rport, 0,
		    raw_usrreqs.pru_attach);
	smsg.base.lmsg.ms_flags &= ~(MSGF_REPLY | MSGF_DONE);
	smsg.base.lmsg.ms_flags |= MSGF_SYNC;
	smsg.nm_proto = proto;
	smsg.nm_ai = ai;
	raw_usrreqs.pru_attach((netmsg_t)&smsg);
	error = smsg.base.lmsg.ms_error;

	kp = (struct keycb *)sotorawcb(so);
	if (error) {
		kfree(kp, M_PCB);
		atomic_add_int(&so->so_refs, -1);
		so->so_pcb = (caddr_t) 0;
		lwkt_reltoken(&key_token);
		goto out;
	}

	kp->kp_promisc = kp->kp_registered = 0;

	if (kp->kp_raw.rcb_proto.sp_protocol == PF_KEY) /* XXX: AF_KEY */
		key_cb.key_count++;
	key_cb.any_count++;
	kp->kp_raw.rcb_laddr = &key_src;
	kp->kp_raw.rcb_faddr = &key_dst;
	soisconnected(so);
	so->so_options |= SO_USELOOPBACK;

	lwkt_reltoken(&key_token);
	error = 0;
out:
	lwkt_replymsg(&msg->attach.base.lmsg, error);
}

/*
 * key_bind()
 * derived from net/rtsock.c:rts_bind()
 */
static void
key_bind(netmsg_t msg)
{
	lwkt_gettoken(&key_token);

	raw_usrreqs.pru_bind(msg); /* xxx just EINVAL */
	/* msg invalid now */

	lwkt_reltoken(&key_token);
}

/*
 * key_connect()
 * derived from net/rtsock.c:rts_connect()
 */
static void
key_connect(netmsg_t msg)
{
	lwkt_gettoken(&key_token);

	raw_usrreqs.pru_connect(msg); /* XXX just EINVAL */
	/* msg invalid now */

	lwkt_reltoken(&key_token);
}

/*
 * key_detach()
 * derived from net/rtsock.c:rts_detach()
 */
static void
key_detach(netmsg_t msg)
{
	struct socket *so = msg->detach.base.nm_so;
	struct keycb *kp = (struct keycb *)sotorawcb(so);

	lwkt_gettoken(&key_token);

	if (kp != NULL) {
		if (kp->kp_raw.rcb_proto.sp_protocol == PF_KEY) {
			/* XXX: AF_KEY */
			key_cb.key_count--;
		}
		key_cb.any_count--;

		key_freereg(so);
	}
	raw_usrreqs.pru_detach(msg);
	/* msg invalid now */

	lwkt_reltoken(&key_token);
}

/*
 * key_disconnect()
 * derived from net/rtsock.c:key_disconnect()
 */
static void
key_disconnect(netmsg_t msg)
{
	lwkt_gettoken(&key_token);

	raw_usrreqs.pru_disconnect(msg);
	/* msg invalid now */

	lwkt_reltoken(&key_token);
}

/*
 * key_peeraddr()
 * derived from net/rtsock.c:rts_peeraddr()
 */
static void
key_peeraddr(netmsg_t msg)
{
	lwkt_gettoken(&key_token);

	raw_usrreqs.pru_peeraddr(msg);
	/* msg invalid now */

	lwkt_reltoken(&key_token);
}

/*
 * key_send()
 * derived from net/rtsock.c:rts_send()
 */
static void
key_send(netmsg_t msg)
{
	lwkt_gettoken(&key_token);

	raw_usrreqs.pru_send(msg);
	/* msg invalid now */

	lwkt_reltoken(&key_token);
}

/*
 * key_shutdown()
 * derived from net/rtsock.c:rts_shutdown()
 */
static void
key_shutdown(netmsg_t msg)
{
	lwkt_gettoken(&key_token);

	raw_usrreqs.pru_shutdown(msg);
	/* msg invalid now */

	lwkt_reltoken(&key_token);
}

/*
 * key_sockaddr()
 * derived from net/rtsock.c:rts_sockaddr()
 */
static void
key_sockaddr(netmsg_t msg)
{
	lwkt_gettoken(&key_token);

	raw_usrreqs.pru_sockaddr(msg);
	/* msg invalid now */

	lwkt_reltoken(&key_token);
}

struct pr_usrreqs key_usrreqs = {
	.pru_abort = key_abort,
	.pru_accept = pr_generic_notsupp,
	.pru_attach = key_attach,
	.pru_bind = key_bind,
	.pru_connect = key_connect,
	.pru_connect2 = pr_generic_notsupp,
	.pru_control = pr_generic_notsupp,
	.pru_detach = key_detach,
	.pru_disconnect = key_disconnect,
	.pru_listen = pr_generic_notsupp,
	.pru_peeraddr = key_peeraddr,
	.pru_rcvd =  pr_generic_notsupp,
	.pru_rcvoob = pr_generic_notsupp,
	.pru_send = key_send,
	.pru_sense = pru_sense_null,
	.pru_shutdown = key_shutdown,
	.pru_sockaddr = key_sockaddr,
	.pru_sosend = sosend,
	.pru_soreceive = soreceive
};

/* sysctl */
SYSCTL_NODE(_net, PF_KEY, key, CTLFLAG_RW, 0, "Key Family");

/*
 * Definitions of protocols supported in the KEY domain.
 */

extern struct domain keydomain;

struct protosw keysw[] = {
    {
	.pr_type = SOCK_RAW,
	.pr_domain = &keydomain,
	.pr_protocol = PF_KEY_V2,
	.pr_flags = PR_ATOMIC|PR_ADDR,

	.pr_input = NULL,
	.pr_output = key_output,
	.pr_ctlinput = raw_ctlinput,
	.pr_ctloutput = NULL,

	.pr_ctlport = cpu0_ctlport,
	.pr_init = raw_init,
	.pr_usrreqs = &key_usrreqs
    }
};

struct domain keydomain = {
	PF_KEY, "key", key_init, NULL, NULL,
	keysw, &keysw[NELEM(keysw)],
};

DOMAIN_SET(key);
