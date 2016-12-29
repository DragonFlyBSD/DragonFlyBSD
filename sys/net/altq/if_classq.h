/*
 * Copyright (c) 1991-1997 Regents of the University of California.
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
 *	This product includes software developed by the Network Research
 *	Group at Lawrence Berkeley Laboratory.
 * 4. Neither the name of the University nor of the Laboratory may be used
 *    to endorse or promote products derived from this software without
 *    specific prior written permission.
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
 */
/*
 * class queue definitions extracted from altq_classq.h.
 */

#ifndef _NET_IF_CLASSQ_H_
#define _NET_IF_CLASSQ_H_

struct if_classq {
	struct mbuf	*clq_tail;	/* Tail of packet queue */
};

#ifdef _KERNEL

#define classq_tail(q)	(q)->clq_tail
#define classq_head(q)	((q)->clq_tail ? (q)->clq_tail->m_nextpkt : NULL)

static __inline void
classq_add(struct if_classq *q, struct mbuf *m)
{
        struct mbuf *m0;

	if ((m0 = classq_tail(q)) != NULL)
		m->m_nextpkt = m0->m_nextpkt;
	else
		m0 = m;
	m0->m_nextpkt = m;
	classq_tail(q) = m;
}

static __inline struct mbuf *
classq_get(struct if_classq *q)
{
	struct mbuf *m, *m0;

	if ((m = classq_tail(q)) == NULL)
		return (NULL);
	if ((m0 = m->m_nextpkt) != m)
		m->m_nextpkt = m0->m_nextpkt;
	else
		classq_tail(q) = NULL;
	m0->m_nextpkt = NULL;
	return (m0);
}

#endif	/* _KERNEL */

#endif	/* !_NET_IF_CLASSQ_H_ */
