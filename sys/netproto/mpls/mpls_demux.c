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
 * $DragonFly: src/sys/netproto/mpls/mpls_demux.c,v 1.2 2008/08/05 15:11:32 nant Exp $
 */

#include <sys/mbuf.h>
#include <sys/systm.h>		/* ncpus2_mask */
#include <sys/types.h>

#include <net/netisr.h>

#include <netinet/in_var.h>

#include <netproto/mpls/mpls.h>
#include <netproto/mpls/mpls_var.h>

extern struct thread netisr_cpu[];

static __inline int
MPLSP_MPORT_HASH(mpls_label_t label, u_short if_index)
{
	/* Use low order byte (demux up to 256 cpus) */
	KASSERT(ncpus2 < 256, ("need different hash function"));  /* XXX */
#if BYTE_ORDER == LITTLE_ENDIAN
	label &= 0x00ff0000;
	label = label >> 16;
#endif
        return ((label ^ if_index) & ncpus2_mask);
}

static void
mpls_lengthcheck(struct mbuf **mp, int hoff)
{
	struct mbuf *m = *mp;
	int hlen = hoff + sizeof(struct mpls);

	/* The packet must be at least the size of an MPLS header. */
	if (m->m_pkthdr.len < hlen) {
		mplsstat.mplss_tooshort++;
		m_free(m);
		*mp = NULL;
		return;
	}

	/* The MPLS header must reside completely in the first mbuf. */
	if (m->m_len < hlen) {
		m = m_pullup(m, hlen);
		if (m == NULL) {
			mplsstat.mplss_toosmall++;
			*mp = NULL;
			return;
		}
	}
	*mp = m;
}

void
mpls_hashfn(struct mbuf **mp, int hoff)
{
	struct mbuf *m = *mp;
	struct mpls *mpls;
	mpls_label_t label;
	struct ifnet *ifp;

	mpls_lengthcheck(mp, hoff);
	if ((m = *mp) == NULL)
		return;

	mpls = mtodoff(m, struct mpls *, hoff);

	label = MPLS_LABEL(ntohl(mpls->mpls_shim));
	ifp = m->m_pkthdr.rcvif;
	m->m_pkthdr.hash = MPLSP_MPORT_HASH(label, ifp->if_index);
	m->m_flags |= M_HASH;
}
