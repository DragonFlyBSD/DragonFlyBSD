/*-
 * Copyright (c) 2005 The DragonFly Project.  All rights reserved.
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
 * $DragonFly: src/sys/net/ifq_var.h,v 1.1 2005/02/11 22:25:57 joerg Exp $
 */
#ifndef _NET_IFQ_VAR_H
#define _NET_IFQ_VAR_H

#ifdef ALTQ
static __inline int
ifq_is_enabled(struct ifaltq *_ifq)
{
	return(_ifq->altq_flags & ALTQF_ENABLED);
}

static __inline int
ifq_is_attached(struct ifaltq *_ifq)
{
	return(_ifq->altq_disc != NULL);
}
#else
static __inline int
ifq_is_enabled(struct ifaltq *_ifq)
{
	return(0);
}

static __inline int
ifq_is_attached(struct ifaltq *_ifq)
{
	return(0);
}
#endif

static __inline int
ifq_is_ready(struct ifaltq *_ifq)
{
	return(_ifq->altq_flags & ALTQF_READY);
}

static __inline void
ifq_set_ready(struct ifaltq *_ifq)
{
	_ifq->altq_flags |= ALTQF_READY;
}

static __inline int
ifq_enqueue(struct ifaltq *_ifq, struct mbuf *_m, struct altq_pktattr *_pa)
{
	if (ifq_is_enabled(_ifq)) {
		return((*_ifq->altq_enqueue)(_ifq, _m, _pa));
	} else {
		if (IF_QFULL(_ifq)) {
			m_freem(_m);
			return(ENOBUFS);
		} else {
			IF_ENQUEUE(_ifq, _m);
			return(0);
		}
	}
}

static __inline struct mbuf *
ifq_dequeue(struct ifaltq *_ifq)
{
#ifdef ALTQ
	if (_ifq->altq_tbr != NULL)
		return(tbr_dequeue(_ifq, ALTDQ_REMOVE));
#endif
	if (ifq_is_enabled(_ifq)) {
		return((*_ifq->altq_dequeue)(_ifq, ALTDQ_REMOVE));
	} else {
		struct mbuf *_m;

		IF_DEQUEUE(_ifq, _m);
		return(_m);
	}
}

static __inline struct mbuf *
ifq_poll(struct ifaltq *_ifq)
{
#ifdef ALTQ
	if (_ifq->altq_tbr != NULL)
		return(tbr_dequeue(_ifq, ALTDQ_POLL));
#endif
	if (ifq_is_enabled(_ifq)) {
		return((*_ifq->altq_dequeue)(_ifq, ALTDQ_POLL));
	} else {
		struct mbuf *_m;

		IF_POLL(_ifq, _m);
		return(_m);
	}
}

static __inline void
ifq_purge(struct ifaltq *_ifq)
{
	if (ifq_is_enabled(_ifq))
		(*_ifq->altq_request)(_ifq, ALTRQ_PURGE, NULL);
	else
		IF_DRAIN(_ifq);
}

static __inline void
ifq_classify(struct ifaltq *_ifq, struct mbuf *_m, uint8_t _af,
	     struct altq_pktattr *_pa)
{
	if (!ifq_is_enabled(_ifq))
		return;
	_pa->pattr_af = _af;
	_pa->pattr_hdr = mtod(_m, caddr_t);
	if (_ifq->altq_flags & ALTQF_CLASSIFY)
		(*_ifq->altq_classify)(_ifq, _m, _pa);
}

static __inline int
ifq_handoff(struct ifnet *_ifp, struct mbuf *_m, struct altq_pktattr *_pa)
{
	int _error, _s;

	_s = splimp();
	_error = ifq_enqueue(&_ifp->if_snd, _m, _pa);
	if (_error == 0) {
		_ifp->if_obytes += _m->m_pkthdr.len;
		if (_m->m_flags & M_MCAST)
			_ifp->if_omcasts++;
		if ((_ifp->if_flags & IFF_OACTIVE) == 0)
			(*_ifp->if_start)(_ifp);
	}
	splx(_s);
	return(_error);
}

static __inline int
ifq_is_empty(struct ifaltq *_ifq)
{
	return(_ifq->ifq_len == 0);
}

static __inline void
ifq_set_maxlen(struct ifaltq *_ifq, int _len)
{
	_ifq->ifq_maxlen = _len;
}

#endif
