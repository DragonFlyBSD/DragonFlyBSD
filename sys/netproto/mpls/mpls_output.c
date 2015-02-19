/*
 * Copyright (c) 2007 The DragonFly Project.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name of The DragonFly Project nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific, prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/param.h>
#include <sys/mbuf.h>
#include <sys/systm.h>

#include <net/if_var.h>

#include <netinet/ip.h>

#include <netproto/mpls/mpls.h>
#include <netproto/mpls/mpls_var.h>

static int mpls_push(struct mbuf **, mpls_label_t,
		     mpls_s_t, mpls_exp_t, mpls_ttl_t);
static int mpls_swap(struct mbuf *, mpls_label_t);
static int mpls_pop(struct mbuf *, mpls_s_t *);

int
mpls_output(struct mbuf *m, struct rtentry *rt)
{
	struct sockaddr_mpls *smpls = NULL;
	int error = 0, i;
	mpls_s_t stackempty;
	mpls_ttl_t ttl = 255;
	struct ip *ip;

	M_ASSERTPKTHDR(m);

	/*
	 * Check if we are coming from an MPLS routing table lookup.
	 * The rt_key of this rtentry will have a family AF_MPLS if so.
	 */
	stackempty = rt_key(rt)->sa_family != AF_MPLS ? 1 : 0;
	if (stackempty) {
		switch (rt_key(rt)->sa_family) {
		case AF_INET:
			ip = mtod(m, struct ip *);
			ttl = ip->ip_ttl;
			break;
		}
	}

	for (i=0; i < MPLS_MAXLOPS && rt->rt_shim[i] != NULL; ++i) {
		smpls = (struct sockaddr_mpls *)rt->rt_shim[i];
		switch (smpls->smpls_op) {
		case MPLSLOP_PUSH:
			error = mpls_push(&m,
				  ntohl(smpls->smpls_label),
				  /*
				   * If we are the first label push, then
				   * set the bottom-of-stack bit.
				   */
				  (stackempty && i == 0) ? 1 : 0,
				  0,
				  ttl);
			if (error)
				return (error);
			stackempty = 0;
			m->m_flags |= M_MPLSLABELED;
			break;
		case MPLSLOP_SWAP:
			/*
			 * Operation is only permmited if label stack
			 * is not empty.
			 */
			if (stackempty)
				return (ENOTSUP);
			KKASSERT(m->m_flags & M_MPLSLABELED);
			error = mpls_swap(m, ntohl(smpls->smpls_label));
			if (error)
				return (error);
			break;
		case MPLSLOP_POP:
			/*
			 * Operation is only permmited if label stack
			 * is not empty.
			 */
			if (stackempty)
				return (ENOTSUP);
			KKASSERT(m->m_flags & M_MPLSLABELED);
			error = mpls_pop(m, &stackempty);
			if (error)
				return (error);
			/*
			 * If we are popping out the last label then
			 * mark the mbuf as ~M_MPLSLABELED.
			 */
			if (stackempty)
				m->m_flags &= ~M_MPLSLABELED;
			break;
		default:
			/* Unknown label operation */
			return (ENOTSUP);
		}
	}
	
	return (error);
}

/*
 * Returns FALSE if no further output processing required.
 */
boolean_t
mpls_output_process(struct mbuf *m, struct rtentry *rt)
{
	int error;

	/* Does this route have MPLS label operations? */
	if (!(rt->rt_flags & RTF_MPLSOPS))
		return TRUE;

	error = mpls_output(m, rt);
	if (error) {
		m_freem(m);
		return FALSE;
	}

	return TRUE;
}

static int
mpls_push(struct mbuf **m, mpls_label_t label, mpls_s_t s, mpls_exp_t exp, mpls_ttl_t ttl) {
	struct mpls *mpls;
	u_int32_t buf = 0;	/* Silence warning */

	M_PREPEND(*m, sizeof(struct mpls), M_NOWAIT);
	if (*m == NULL)
		return (ENOBUFS);

	MPLS_SET_LABEL(buf, label);
	MPLS_SET_STACK(buf, s);
	MPLS_SET_EXP(buf, exp);
	MPLS_SET_TTL(buf, ttl);
	mpls = mtod(*m, struct mpls *);
	mpls->mpls_shim = htonl(buf);
	
	return (0);
}

static int
mpls_swap(struct mbuf *m, mpls_label_t label) {
	struct mpls *mpls;
	u_int32_t buf;
	mpls_ttl_t ttl;

	if (m->m_len < sizeof(struct mpls) &&
	   (m = m_pullup(m, sizeof(struct mpls))) == NULL)
		return (ENOBUFS);

	mpls = mtod(m, struct mpls *);
	buf = ntohl(mpls->mpls_shim);
	ttl = MPLS_TTL(buf);
	if (--ttl <= 0) {
		/* XXX: should send icmp ttl expired. */
		mplsstat.mplss_ttlexpired++;
		return (ETIMEDOUT);
	}
	MPLS_SET_LABEL(buf, label);
	MPLS_SET_TTL(buf, ttl); /* XXX tunnel mode: uniform, pipe, short pipe */
	mpls->mpls_shim = htonl(buf);
	
	return (0);
}

static int
mpls_pop(struct mbuf *m, mpls_s_t *sbit) {
	struct mpls *mpls;
	u_int32_t buf;

	if (m->m_len < sizeof(struct mpls)) {
		m = m_pullup(m, sizeof(struct mpls));
		if (m == NULL)
			return (ENOBUFS);
	}
	mpls = mtod(m, struct mpls *);
	buf = ntohl(mpls->mpls_shim);
	*sbit = MPLS_STACK(buf);

	m_adj(m, sizeof(struct mpls));

	return (0);
}
