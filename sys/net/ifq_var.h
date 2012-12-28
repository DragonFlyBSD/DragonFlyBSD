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
 */
/*
 * NOTE ON MPSAFE access.  Routines which manipulate the packet queue must
 * be called within a critical section to interlock subsystems based on
 * the MP lock, and must be holding the interface serializer to interlock
 * MPSAFE subsystems.  Once all subsystems are made MPSAFE, the critical
 * section will no longer be required.
 */

#ifndef _NET_IFQ_VAR_H_
#define _NET_IFQ_VAR_H_

#ifndef _KERNEL

#error "This file should not be included by userland programs."

#else

#ifndef _SYS_SYSTM_H_
#include <sys/systm.h>
#endif
#ifndef _SYS_THREAD2_H_
#include <sys/thread2.h>
#endif
#ifndef _SYS_SERIALIZE_H_
#include <sys/serialize.h>
#endif
#ifndef _SYS_MBUF_H_
#include <sys/mbuf.h>
#endif
#ifndef _NET_IF_VAR_H_
#include <net/if_var.h>
#endif
#ifndef _NET_ALTQ_IF_ALTQ_H_
#include <net/altq/if_altq.h>
#endif

struct ifaltq;

/*
 * Support for non-ALTQ interfaces.
 */
int		ifq_classic_enqueue(struct ifaltq *, struct mbuf *,
				    struct altq_pktattr *);
struct mbuf	*ifq_classic_dequeue(struct ifaltq *, struct mbuf *, int);
int		ifq_classic_request(struct ifaltq *, int, void *);
void		ifq_set_classic(struct ifaltq *);

void		ifq_set_maxlen(struct ifaltq *, int);
int		ifq_dispatch(struct ifnet *, struct mbuf *,
			     struct altq_pktattr *);

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

/*
 * WARNING: Should only be called in an MPSAFE manner.
 */
static __inline int
ifq_is_ready(struct ifaltq *_ifq)
{
	return(_ifq->altq_flags & ALTQF_READY);
}

/*
 * WARNING: Should only be called in an MPSAFE manner.
 */
static __inline void
ifq_set_ready(struct ifaltq *_ifq)
{
	_ifq->altq_flags |= ALTQF_READY;
}

/*
 * WARNING: Should only be called in an MPSAFE manner.
 */
static __inline int
ifq_enqueue_locked(struct ifaltq *_ifq, struct mbuf *_m,
		   struct altq_pktattr *_pa)
{
#ifdef ALTQ
	if (!ifq_is_enabled(_ifq))
		return ifq_classic_enqueue(_ifq, _m, _pa);
	else
#endif
	return _ifq->altq_enqueue(_ifq, _m, _pa);
}

static __inline int
ifq_enqueue(struct ifaltq *_ifq, struct mbuf *_m, struct altq_pktattr *_pa)
{
	int _error;

	ALTQ_LOCK(_ifq);
	_error = ifq_enqueue_locked(_ifq, _m, _pa);
	ALTQ_UNLOCK(_ifq);
	return _error;
}

/*
 * WARNING: Should only be called in an MPSAFE manner.
 */
static __inline struct mbuf *
ifq_dequeue(struct ifaltq *_ifq, struct mbuf *_mpolled)
{
	struct mbuf *_m;

	ALTQ_LOCK(_ifq);
	if (_ifq->altq_prepended != NULL) {
		_m = _ifq->altq_prepended;
		_ifq->altq_prepended = NULL;
		KKASSERT(_ifq->ifq_len > 0);
		_ifq->ifq_len--;
		ALTQ_UNLOCK(_ifq);
		return _m;
	}

#ifdef ALTQ
	if (_ifq->altq_tbr != NULL)
		_m = tbr_dequeue(_ifq, _mpolled, ALTDQ_REMOVE);
	else if (!ifq_is_enabled(_ifq))
		_m = ifq_classic_dequeue(_ifq, _mpolled, ALTDQ_REMOVE);
	else
#endif
	_m = _ifq->altq_dequeue(_ifq, _mpolled, ALTDQ_REMOVE);
	ALTQ_UNLOCK(_ifq);
	return _m;
}

/*
 * WARNING: Should only be called in an MPSAFE manner.
 */
static __inline struct mbuf *
ifq_poll_locked(struct ifaltq *_ifq)
{
	if (_ifq->altq_prepended != NULL)
		return _ifq->altq_prepended;

#ifdef ALTQ
	if (_ifq->altq_tbr != NULL)
		return tbr_dequeue(_ifq, NULL, ALTDQ_POLL);
	else if (!ifq_is_enabled(_ifq))
		return ifq_classic_dequeue(_ifq, NULL, ALTDQ_POLL);
	else
#endif
	return _ifq->altq_dequeue(_ifq, NULL, ALTDQ_POLL);
}

static __inline struct mbuf *
ifq_poll(struct ifaltq *_ifq)
{
	struct mbuf *_m;

	ALTQ_LOCK(_ifq);
	_m = ifq_poll_locked(_ifq);
	ALTQ_UNLOCK(_ifq);
	return _m;
}

/*
 * WARNING: Should only be called in an MPSAFE manner.
 */
static __inline void
ifq_purge_locked(struct ifaltq *_ifq)
{
	if (_ifq->altq_prepended != NULL) {
		m_freem(_ifq->altq_prepended);
		_ifq->altq_prepended = NULL;
		KKASSERT(_ifq->ifq_len > 0);
		_ifq->ifq_len--;
	}

#ifdef ALTQ
	if (!ifq_is_enabled(_ifq))
		ifq_classic_request(_ifq, ALTRQ_PURGE, NULL);
	else
#endif
	_ifq->altq_request(_ifq, ALTRQ_PURGE, NULL);
}

static __inline void
ifq_purge(struct ifaltq *_ifq)
{
	ALTQ_LOCK(_ifq);
	ifq_purge_locked(_ifq);
	ALTQ_UNLOCK(_ifq);
}

/*
 * WARNING: Should only be called in an MPSAFE manner.
 */
static __inline void
ifq_purge_all_locked(struct ifaltq *_ifq)
{
	/* XXX temporary */
	ifq_purge_locked(_ifq);
}

static __inline void
ifq_purge_all(struct ifaltq *_ifq)
{
	ALTQ_LOCK(_ifq);
	ifq_purge_all_locked(_ifq);
	ALTQ_UNLOCK(_ifq);
}

/*
 * WARNING: Should only be called in an MPSAFE manner.
 */
static __inline void
ifq_classify(struct ifaltq *_ifq, struct mbuf *_m, uint8_t _af,
	     struct altq_pktattr *_pa)
{
#ifdef ALTQ
	ALTQ_LOCK(_ifq);
	if (ifq_is_enabled(_ifq)) {
		_pa->pattr_af = _af;
		_pa->pattr_hdr = mtod(_m, caddr_t);
		if (_ifq->altq_flags & ALTQF_CLASSIFY)
			_ifq->altq_classify(_ifq, _m, _pa);
	}
	ALTQ_UNLOCK(_ifq);
#endif
}

static __inline void
ifq_prepend(struct ifaltq *_ifq, struct mbuf *_m)
{
	ALTQ_LOCK(_ifq);
	KASSERT(_ifq->altq_prepended == NULL, ("pending prepended mbuf"));
	_ifq->altq_prepended = _m;
	_ifq->ifq_len++;
	ALTQ_UNLOCK(_ifq);
}

/*
 * Hand a packet to an interface. 
 *
 * For subsystems protected by the MP lock, access to the queue is protected
 * by a critical section.
 *
 * For MPSAFE subsystems and drivers, access to the queue is protected by
 * the ifnet serializer.
 */
static __inline int
ifq_handoff(struct ifnet *_ifp, struct mbuf *_m, struct altq_pktattr *_pa)
{
	int _error;

	ASSERT_IFNET_SERIALIZED_TX(_ifp);
	_error = ifq_enqueue(&_ifp->if_snd, _m, _pa);
	if (_error == 0) {
		_ifp->if_obytes += _m->m_pkthdr.len;
		if (_m->m_flags & M_MCAST)
			_ifp->if_omcasts++;
		if ((_ifp->if_flags & IFF_OACTIVE) == 0)
			(*_ifp->if_start)(_ifp);
	}
	return(_error);
}

static __inline int
ifq_is_empty(struct ifaltq *_ifq)
{
	return(_ifq->ifq_len == 0);
}

static __inline int
ifq_data_ready(struct ifaltq *_ifq)
{
#ifdef ALTQ
	if (_ifq->altq_tbr != NULL)
		return (ifq_poll_locked(_ifq) != NULL);
	else
#endif
	return !ifq_is_empty(_ifq);
}

#endif	/* _KERNEL */
#endif	/* _NET_IFQ_VAR_H_ */
