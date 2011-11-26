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
 *
 * $DragonFly: src/sys/netproto/mpls/mpls_input.c,v 1.4 2008/09/24 14:26:39 sephe Exp $
 */

#include <sys/globaldata.h>
#include <sys/kernel.h>
#include <sys/mbuf.h>
#include <sys/param.h>
#include <sys/sysctl.h>
#include <sys/systm.h>

#include <net/if_var.h>
#include <net/netisr.h>
#include <net/route.h>

#include <sys/thread2.h>
#include <sys/mplock2.h>

#include <netproto/mpls/mpls.h>
#include <netproto/mpls/mpls_var.h>

struct mpls_stats	mplsstats_percpu[MAXCPU];
struct route		mplsforward_rt[MAXCPU];

int mplsforwarding = 1;
/*
SYSCTL_INT(_net_mpls, OID_AUTO, forwarding, CTLFLAG_RW,
    &mplsforwarding, 0, "Enable MPLS forwarding between interfaces");
*/

static void	mpls_input_handler(netmsg_t);
static void	mpls_forward(struct mbuf *);

void
mpls_init(void)
{
#ifdef SMP
	int cpu;
#endif

	/*
	 * Initialize MPLS statistics counters for each CPU.
	 *
	 */
#ifdef SMP
	for (cpu = 0; cpu < ncpus; ++cpu) {
		bzero(&mplsstats_percpu[cpu], sizeof(struct mpls_stats));
	}
#else
	bzero(&mplsstat, sizeof(struct mpls_stats));
#endif

	netisr_register(NETISR_MPLS, mpls_input_handler, mpls_cpufn);
}

static void
mpls_input_handler(netmsg_t msg)
{
        struct mbuf *m = msg->packet.nm_packet;

	get_mplock();
        mpls_input(m);
	rel_mplock();
	/* do not reply, msg embedded in mbuf */
}

void
mpls_input(struct mbuf *m)
{
	struct mpls *mpls = NULL;
	mpls_label_t label;

	M_ASSERTPKTHDR(m);

	mplsstat.mplss_total++;

	/* length checks already performed at mpls_demux() */
	KASSERT(m->m_pkthdr.len >= sizeof(struct mpls),
	    ("mpls_input: mpls header too small"));
	
again:
	if (m->m_len < sizeof(struct mpls)) {
		m = m_pullup(m, sizeof(struct mpls));
		if (m == NULL) {
			mplsstat.mplss_toosmall++;
			return;
		}
	}

	mpls = mtod(m, struct mpls*);
	label = MPLS_LABEL(ntohl(mpls->mpls_shim));
	switch (label) {
	case 0:
		/* 
		 * Label 0: represents "IPv4 Explicit NULL Label".
		 */
		if (MPLS_STACK(ntohl(mpls->mpls_shim))) {
			/* Decapsulate the ip datagram from the mpls frame. */
			m_adj(m, sizeof(struct mpls));
			netisr_queue(NETISR_IP, m);
			return;
		}
		goto again; /* If not the bottom label, per RFC4182. */

	case 1:
		/*
		 * Label 1: represents "Router Alert Label" and is valid 
		 * anywhere except at the bottom of the stack.
		 */
		break;

	case 2:
		/* 
		 * Label 2: represents "IPv6 Explicit NULL Label".
		 */
		if (MPLS_STACK(ntohl(mpls->mpls_shim))) {
			/* Decapsulate the ip datagram from the mpls frame. */
			m_adj(m, sizeof(struct mpls));
			netisr_queue(NETISR_IPV6, m);
			return;
		}
		goto again; /* If not the bottom label, per RFC4182. */

	case 3:
		/*
		 * Label 3: represents the "Implicit NULL Label" and must not 
		 * appear on the wire.
		 */
		break;
	default:
		/*
		 * Labels 4 - 15: reserved, drop them.
		 */
		if (label <= 15) {
			mplsstat.mplss_reserved++;
			m_freem(m);
			return;
		}
		if (mplsforwarding) {
			mpls_forward(m);
			return;
		} else {
			mplsstat.mplss_cantforward++;
			m_freem(m);
			return;
		}
	}

	mplsstat.mplss_invalid++;
	m_freem(m);
}

static void
mpls_forward(struct mbuf *m)
{
	struct sockaddr_mpls *smpls;
	struct mpls *mpls;
	struct route *cache_rt = &mplsforward_rt[mycpuid];
	mpls_label_t label;
	struct ifnet *ifp;
	struct sockaddr *dst;
	int error;

	KASSERT(m->m_len >= sizeof(struct mpls),
	    ("mpls_forward: mpls header not in one mbuf"));

	mpls = mtod(m, struct mpls *);
	label = MPLS_LABEL(ntohl(mpls->mpls_shim));

	smpls = (struct sockaddr_mpls *) &cache_rt->ro_dst;
	if (cache_rt->ro_rt == NULL || smpls->smpls_label != label) {
		if (cache_rt->ro_rt != NULL) {
			RTFREE(cache_rt->ro_rt);
			cache_rt->ro_rt = NULL;
		}
		smpls->smpls_family = AF_MPLS;
		smpls->smpls_len = sizeof(struct sockaddr_mpls);
		smpls->smpls_label = htonl(label);
		rtalloc(cache_rt);
		if (cache_rt->ro_rt == NULL) {
			/* route not found */
			return;
		}
	}

	ifp = cache_rt->ro_rt->rt_ifp;
	dst = cache_rt->ro_rt->rt_gateway;
	error = mpls_output(m, cache_rt->ro_rt);
	if (error)
		goto bad;
	error = (*ifp->if_output)(ifp, m, dst, cache_rt->ro_rt);
	if (error)
		goto bad;
	mplsstat.mplss_forwarded++;

	return;
bad:
	m_freem(m);
}
