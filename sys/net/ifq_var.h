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

#ifndef _NET_IFQ_VAR_H_
#define _NET_IFQ_VAR_H_

#ifndef _KERNEL
#error "This file should not be included by userland programs."
#endif

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

#define ASSERT_ALTQ_SQ_DEFAULT(ifp, ifsq) \
	KASSERT(ifsq_get_ifp((ifsq)) == (ifp) && \
	    ifsq_get_index((ifsq)) == ALTQ_SUBQ_INDEX_DEFAULT, \
	    ("not ifp's default subqueue"));

struct ifaltq;
struct ifaltq_subque;

/*
 * Subqueue watchdog
 */
typedef void	(*ifsq_watchdog_t)(struct ifaltq_subque *);

struct ifsubq_watchdog {
	struct callout	wd_callout;
	int		wd_timer;
	struct ifaltq_subque *wd_subq;
	ifsq_watchdog_t	wd_watchdog;
};

/*
 * Support for "classic" ALTQ interfaces.
 */
int		ifsq_classic_enqueue(struct ifaltq_subque *, struct mbuf *,
		    struct altq_pktattr *);
struct mbuf	*ifsq_classic_dequeue(struct ifaltq_subque *, int);
int		ifsq_classic_request(struct ifaltq_subque *, int, void *);
void		ifq_set_classic(struct ifaltq *);

void		ifq_set_maxlen(struct ifaltq *, int);
void		ifq_set_methods(struct ifaltq *, altq_mapsubq_t,
		    ifsq_enqueue_t, ifsq_dequeue_t, ifsq_request_t);
int		ifq_mapsubq_default(struct ifaltq *, int);
int		ifq_mapsubq_mask(struct ifaltq *, int);

void		ifsq_devstart(struct ifaltq_subque *ifsq);
void		ifsq_devstart_sched(struct ifaltq_subque *ifsq);

void		ifsq_watchdog_init(struct ifsubq_watchdog *,
		    struct ifaltq_subque *, ifsq_watchdog_t);
void		ifsq_watchdog_start(struct ifsubq_watchdog *);
void		ifsq_watchdog_stop(struct ifsubq_watchdog *);

/*
 * Dispatch a packet to an interface.
 */
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

#else	/* !ALTQ */

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

#endif	/* ALTQ */

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

/*
 * Subqueue lock must be held
 */
static __inline int
ifsq_enqueue_locked(struct ifaltq_subque *_ifsq, struct mbuf *_m,
    struct altq_pktattr *_pa)
{
#ifdef ALTQ
	if (!ifq_is_enabled(_ifsq->ifsq_altq))
		return ifsq_classic_enqueue(_ifsq, _m, _pa);
	else
#endif
	return _ifsq->ifsq_enqueue(_ifsq, _m, _pa);
}

static __inline int
ifsq_enqueue(struct ifaltq_subque *_ifsq, struct mbuf *_m,
    struct altq_pktattr *_pa)
{
	int _error;

	ALTQ_SQ_LOCK(_ifsq);
	_error = ifsq_enqueue_locked(_ifsq, _m, _pa);
	ALTQ_SQ_UNLOCK(_ifsq);
	return _error;
}

static __inline struct mbuf *
ifsq_dequeue(struct ifaltq_subque *_ifsq)
{
	struct mbuf *_m;

	ALTQ_SQ_LOCK(_ifsq);
	if (_ifsq->ifsq_prepended != NULL) {
		_m = _ifsq->ifsq_prepended;
		_ifsq->ifsq_prepended = NULL;
		ALTQ_SQ_CNTR_DEC(_ifsq, _m->m_pkthdr.len);
		ALTQ_SQ_UNLOCK(_ifsq);
		return _m;
	}

#ifdef ALTQ
	if (_ifsq->ifsq_altq->altq_tbr != NULL)
		_m = tbr_dequeue(_ifsq, ALTDQ_REMOVE);
	else if (!ifq_is_enabled(_ifsq->ifsq_altq))
		_m = ifsq_classic_dequeue(_ifsq, ALTDQ_REMOVE);
	else
#endif
	_m = _ifsq->ifsq_dequeue(_ifsq, ALTDQ_REMOVE);
	ALTQ_SQ_UNLOCK(_ifsq);
	return _m;
}

/*
 * Subqueue lock must be held
 */
static __inline struct mbuf *
ifsq_poll_locked(struct ifaltq_subque *_ifsq)
{
	if (_ifsq->ifsq_prepended != NULL)
		return _ifsq->ifsq_prepended;

#ifdef ALTQ
	if (_ifsq->ifsq_altq->altq_tbr != NULL)
		return tbr_dequeue(_ifsq, ALTDQ_POLL);
	else if (!ifq_is_enabled(_ifsq->ifsq_altq))
		return ifsq_classic_dequeue(_ifsq, ALTDQ_POLL);
	else
#endif
	return _ifsq->ifsq_dequeue(_ifsq, ALTDQ_POLL);
}

static __inline struct mbuf *
ifsq_poll(struct ifaltq_subque *_ifsq)
{
	struct mbuf *_m;

	ALTQ_SQ_LOCK(_ifsq);
	_m = ifsq_poll_locked(_ifsq);
	ALTQ_SQ_UNLOCK(_ifsq);
	return _m;
}

static __inline int
ifsq_poll_pktlen(struct ifaltq_subque *_ifsq)
{
	struct mbuf *_m;
	int _len = 0;

	ALTQ_SQ_LOCK(_ifsq);

	_m = ifsq_poll_locked(_ifsq);
	if (_m != NULL) {
		M_ASSERTPKTHDR(_m);
		_len = _m->m_pkthdr.len;
	}

	ALTQ_SQ_UNLOCK(_ifsq);

	return _len;
}

/*
 * Subqueue lock must be held
 */
static __inline void
ifsq_purge_locked(struct ifaltq_subque *_ifsq)
{
	if (_ifsq->ifsq_prepended != NULL) {
		ALTQ_SQ_CNTR_DEC(_ifsq, _ifsq->ifsq_prepended->m_pkthdr.len);
		m_freem(_ifsq->ifsq_prepended);
		_ifsq->ifsq_prepended = NULL;
	}

#ifdef ALTQ
	if (!ifq_is_enabled(_ifsq->ifsq_altq))
		ifsq_classic_request(_ifsq, ALTRQ_PURGE, NULL);
	else
#endif
	_ifsq->ifsq_request(_ifsq, ALTRQ_PURGE, NULL);
}

static __inline void
ifsq_purge(struct ifaltq_subque *_ifsq)
{
	ALTQ_SQ_LOCK(_ifsq);
	ifsq_purge_locked(_ifsq);
	ALTQ_SQ_UNLOCK(_ifsq);
}

static __inline void
ifq_lock_all(struct ifaltq *_ifq)
{
	int _q;

	for (_q = 0; _q < _ifq->altq_subq_cnt; ++_q)
		ALTQ_SQ_LOCK(&_ifq->altq_subq[_q]);
}

static __inline void
ifq_unlock_all(struct ifaltq *_ifq)
{
	int _q;

	for (_q = _ifq->altq_subq_cnt - 1; _q >= 0; --_q)
		ALTQ_SQ_UNLOCK(&_ifq->altq_subq[_q]);
}

/*
 * All of the subqueue locks must be held
 */
static __inline void
ifq_purge_all_locked(struct ifaltq *_ifq)
{
	int _q;

	for (_q = 0; _q < _ifq->altq_subq_cnt; ++_q)
		ifsq_purge_locked(&_ifq->altq_subq[_q]);
}

static __inline void
ifq_purge_all(struct ifaltq *_ifq)
{
	ifq_lock_all(_ifq);
	ifq_purge_all_locked(_ifq);
	ifq_unlock_all(_ifq);
}

static __inline void
ifq_classify(struct ifaltq *_ifq, struct mbuf *_m, uint8_t _af,
    struct altq_pktattr *_pa)
{
#ifdef ALTQ
	if (ifq_is_enabled(_ifq)) {
		_pa->pattr_af = _af;
		_pa->pattr_hdr = mtod(_m, caddr_t);
		if (ifq_is_enabled(_ifq) &&
		    (_ifq->altq_flags & ALTQF_CLASSIFY)) {
			/* XXX default subqueue */
			struct ifaltq_subque *_ifsq =
			    &_ifq->altq_subq[ALTQ_SUBQ_INDEX_DEFAULT];

			ALTQ_SQ_LOCK(_ifsq);
			if (ifq_is_enabled(_ifq) &&
			    (_ifq->altq_flags & ALTQF_CLASSIFY))
				_ifq->altq_classify(_ifq, _m, _pa);
			ALTQ_SQ_UNLOCK(_ifsq);
		}
	}
#endif
}

static __inline void
ifsq_prepend(struct ifaltq_subque *_ifsq, struct mbuf *_m)
{
	ALTQ_SQ_LOCK(_ifsq);
	KASSERT(_ifsq->ifsq_prepended == NULL, ("pending prepended mbuf"));
	_ifsq->ifsq_prepended = _m;
	ALTQ_SQ_CNTR_INC(_ifsq, _m->m_pkthdr.len);
	ALTQ_SQ_UNLOCK(_ifsq);
}

/*
 * Subqueue hardware serializer must be held
 */
static __inline void
ifsq_set_oactive(struct ifaltq_subque *_ifsq)
{
	_ifsq->ifsq_hw_oactive = 1;
}

/*
 * Subqueue hardware serializer must be held
 */
static __inline void
ifsq_clr_oactive(struct ifaltq_subque *_ifsq)
{
	_ifsq->ifsq_hw_oactive = 0;
}

/*
 * Subqueue hardware serializer must be held
 */
static __inline int
ifsq_is_oactive(const struct ifaltq_subque *_ifsq)
{
	return _ifsq->ifsq_hw_oactive;
}

/*
 * Hand a packet to the interface's default subqueue.
 *
 * The default subqueue hardware serializer must be held.  If the
 * subqueue hardware serializer is not held yet, ifq_dispatch()
 * should be used to get better performance.
 */
static __inline int
ifq_handoff(struct ifnet *_ifp, struct mbuf *_m, struct altq_pktattr *_pa)
{
	struct ifaltq_subque *_ifsq;
	int _error;
	int _qid = ALTQ_SUBQ_INDEX_DEFAULT; /* XXX default subqueue */

	_ifsq = &_ifp->if_snd.altq_subq[_qid];

	ASSERT_ALTQ_SQ_SERIALIZED_HW(_ifsq);
	_error = ifsq_enqueue(_ifsq, _m, _pa);
	if (_error == 0) {
		IFNET_STAT_INC(_ifp, obytes, _m->m_pkthdr.len);
		if (_m->m_flags & M_MCAST)
			IFNET_STAT_INC(_ifp, omcasts, 1);
		if (!ifsq_is_oactive(_ifsq))
			(*_ifp->if_start)(_ifp, _ifsq);
	}
	return(_error);
}

static __inline int
ifsq_is_empty(const struct ifaltq_subque *_ifsq)
{
	return(_ifsq->ifsq_len == 0);
}

/*
 * Subqueue lock must be held
 */
static __inline int
ifsq_data_ready(struct ifaltq_subque *_ifsq)
{
#ifdef ALTQ
	if (_ifsq->ifsq_altq->altq_tbr != NULL)
		return (ifsq_poll_locked(_ifsq) != NULL);
	else
#endif
	return !ifsq_is_empty(_ifsq);
}

/*
 * Subqueue lock must be held
 */
static __inline int
ifsq_is_started(const struct ifaltq_subque *_ifsq)
{
	return _ifsq->ifsq_started;
}

/*
 * Subqueue lock must be held
 */
static __inline void
ifsq_set_started(struct ifaltq_subque *_ifsq)
{
	_ifsq->ifsq_started = 1;
}

/*
 * Subqueue lock must be held
 */
static __inline void
ifsq_clr_started(struct ifaltq_subque *_ifsq)
{
	_ifsq->ifsq_started = 0;
}

static __inline struct ifsubq_stage *
ifsq_get_stage(struct ifaltq_subque *_ifsq, int _cpuid)
{
	return &_ifsq->ifsq_stage[_cpuid];
}

static __inline int
ifsq_get_cpuid(const struct ifaltq_subque *_ifsq)
{
	return _ifsq->ifsq_cpuid;
}

static __inline void
ifsq_set_cpuid(struct ifaltq_subque *_ifsq, int _cpuid)
{
	KASSERT(_cpuid >= 0 && _cpuid < ncpus,
	    ("invalid ifsq_cpuid %d", _cpuid));
	_ifsq->ifsq_cpuid = _cpuid;
}

static __inline struct lwkt_msg *
ifsq_get_ifstart_lmsg(struct ifaltq_subque *_ifsq, int _cpuid)
{
	return &_ifsq->ifsq_ifstart_nmsg[_cpuid].lmsg;
}

static __inline int
ifsq_get_index(const struct ifaltq_subque *_ifsq)
{
	return _ifsq->ifsq_index;
}

static __inline void
ifsq_set_priv(struct ifaltq_subque *_ifsq, void *_priv)
{
	_ifsq->ifsq_hw_priv = _priv;
}

static __inline void *
ifsq_get_priv(const struct ifaltq_subque *_ifsq)
{
	return _ifsq->ifsq_hw_priv;
}

static __inline struct ifnet *
ifsq_get_ifp(const struct ifaltq_subque *_ifsq)
{
	return _ifsq->ifsq_ifp;
}

static __inline void
ifsq_set_hw_serialize(struct ifaltq_subque *_ifsq,
    struct lwkt_serialize *_hwslz)
{
	KASSERT(_hwslz != NULL, ("NULL hw serialize"));
	KASSERT(_ifsq->ifsq_hw_serialize == NULL,
	    ("hw serialize has been setup"));
	_ifsq->ifsq_hw_serialize = _hwslz;
}

static __inline void
ifsq_serialize_hw(struct ifaltq_subque *_ifsq)
{
	lwkt_serialize_enter(_ifsq->ifsq_hw_serialize);
}

static __inline void
ifsq_deserialize_hw(struct ifaltq_subque *_ifsq)
{
	lwkt_serialize_exit(_ifsq->ifsq_hw_serialize);
}

static __inline int
ifsq_tryserialize_hw(struct ifaltq_subque *_ifsq)
{
	return lwkt_serialize_try(_ifsq->ifsq_hw_serialize);
}

static __inline struct ifaltq_subque *
ifq_get_subq_default(const struct ifaltq *_ifq)
{
	return &_ifq->altq_subq[ALTQ_SUBQ_INDEX_DEFAULT];
}

static __inline struct ifaltq_subque *
ifq_get_subq(const struct ifaltq *_ifq, int _idx)
{
	KASSERT(_idx >= 0 && _idx < _ifq->altq_subq_cnt,
	    ("invalid qid %d", _idx));
	return &_ifq->altq_subq[_idx];
}

static __inline struct ifaltq_subque *
ifq_map_subq(struct ifaltq *_ifq, int _cpuid)
{ 
	int _idx = _ifq->altq_mapsubq(_ifq, _cpuid);
	return ifq_get_subq(_ifq, _idx);
}

static __inline void
ifq_set_subq_cnt(struct ifaltq *_ifq, int _cnt)
{
	_ifq->altq_subq_cnt = _cnt;
}

static __inline void
ifq_set_subq_mask(struct ifaltq *_ifq, uint32_t _mask)
{
	KASSERT(((_mask + 1) & _mask) == 0, ("invalid mask %08x", _mask));
	_ifq->altq_subq_mask = _mask;
}

/* COMPAT */
static __inline int
ifq_is_oactive(const struct ifaltq *_ifq)
{
	return ifsq_is_oactive(ifq_get_subq_default(_ifq));
}

/* COMPAT */
static __inline void
ifq_set_oactive(struct ifaltq *_ifq)
{
	ifsq_set_oactive(ifq_get_subq_default(_ifq));
}

/* COMPAT */
static __inline void
ifq_clr_oactive(struct ifaltq *_ifq)
{
	ifsq_clr_oactive(ifq_get_subq_default(_ifq));
}

/* COMPAT */
static __inline int
ifq_is_empty(struct ifaltq *_ifq)
{
	return ifsq_is_empty(ifq_get_subq_default(_ifq));
}

/* COMPAT */
static __inline void
ifq_purge(struct ifaltq *_ifq)
{
	ifsq_purge(ifq_get_subq_default(_ifq));
}

/* COMPAT */
static __inline struct mbuf *
ifq_dequeue(struct ifaltq *_ifq)
{
	return ifsq_dequeue(ifq_get_subq_default(_ifq));
}

/* COMPAT */
static __inline void
ifq_prepend(struct ifaltq *_ifq, struct mbuf *_m)
{
	ifsq_prepend(ifq_get_subq_default(_ifq), _m);
}

/* COMPAT */
static __inline void
ifq_set_cpuid(struct ifaltq *_ifq, int _cpuid)
{
	KASSERT(_ifq->altq_subq_cnt == 1,
	    ("invalid subqueue count %d", _ifq->altq_subq_cnt));
	ifsq_set_cpuid(ifq_get_subq_default(_ifq), _cpuid);
}

/* COMPAT */
static __inline void
ifq_set_hw_serialize(struct ifaltq *_ifq, struct lwkt_serialize *_hwslz)
{
	KASSERT(_ifq->altq_subq_cnt == 1,
	    ("invalid subqueue count %d", _ifq->altq_subq_cnt));
	ifsq_set_hw_serialize(ifq_get_subq_default(_ifq), _hwslz);
}

#endif	/* _NET_IFQ_VAR_H_ */
