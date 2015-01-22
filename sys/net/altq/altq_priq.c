/*	$KAME: altq_priq.c,v 1.12 2004/04/17 10:54:48 kjc Exp $	*/
/*	$DragonFly: src/sys/net/altq/altq_priq.c,v 1.9 2008/05/14 11:59:23 sephe Exp $ */

/*
 * Copyright (C) 2000-2003
 *	Sony Computer Science Laboratories Inc.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY SONY CSL AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL SONY CSL OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
/*
 * priority queue
 */

#include "opt_altq.h"
#include "opt_inet.h"
#include "opt_inet6.h"

#ifdef ALTQ_PRIQ  /* priq is enabled by ALTQ_PRIQ option in opt_altq.h */

#include <sys/param.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/errno.h>
#include <sys/kernel.h>
#include <sys/queue.h>
#include <sys/thread.h>

#include <net/if.h>
#include <net/ifq_var.h>
#include <netinet/in.h>

#include <net/pf/pfvar.h>
#include <net/altq/altq.h>
#include <net/altq/altq_priq.h>

#include <sys/thread2.h>

#define PRIQ_SUBQ_INDEX		ALTQ_SUBQ_INDEX_DEFAULT
#define PRIQ_LOCK(ifq) \
    ALTQ_SQ_LOCK(&(ifq)->altq_subq[PRIQ_SUBQ_INDEX])
#define PRIQ_UNLOCK(ifq) \
    ALTQ_SQ_UNLOCK(&(ifq)->altq_subq[PRIQ_SUBQ_INDEX])

/*
 * function prototypes
 */
static int	priq_clear_interface(struct priq_if *);
static int	priq_request(struct ifaltq_subque *, int, void *);
static void	priq_purge(struct priq_if *);
static struct priq_class *priq_class_create(struct priq_if *, int, int, int, int);
static int	priq_class_destroy(struct priq_class *);
static int	priq_enqueue(struct ifaltq_subque *, struct mbuf *,
		    struct altq_pktattr *);
static struct mbuf *priq_dequeue(struct ifaltq_subque *, int);

static int	priq_addq(struct priq_class *, struct mbuf *);
static struct mbuf *priq_getq(struct priq_class *);
static struct mbuf *priq_pollq(struct priq_class *);
static void	priq_purgeq(struct priq_class *);

static void	get_class_stats(struct priq_classstats *, struct priq_class *);
static struct priq_class *clh_to_clp(struct priq_if *, uint32_t);

int
priq_pfattach(struct pf_altq *a, struct ifaltq *ifq)
{
	return altq_attach(ifq, ALTQT_PRIQ, a->altq_disc, ifq_mapsubq_default,
	    priq_enqueue, priq_dequeue, priq_request, NULL, NULL);
}

int
priq_add_altq(struct pf_altq *a)
{
	struct priq_if *pif;
	struct ifnet *ifp;

	ifnet_lock();

	if ((ifp = ifunit(a->ifname)) == NULL) {
		ifnet_unlock();
		return (EINVAL);
	}
	if (!ifq_is_ready(&ifp->if_snd)) {
		ifnet_unlock();
		return (ENODEV);
	}

	pif = kmalloc(sizeof(*pif), M_ALTQ, M_WAITOK | M_ZERO);
	pif->pif_bandwidth = a->ifbandwidth;
	pif->pif_maxpri = -1;
	pif->pif_ifq = &ifp->if_snd;
	ifq_purge_all(&ifp->if_snd);

	ifnet_unlock();

	/* keep the state in pf_altq */
	a->altq_disc = pif;

	return (0);
}

int
priq_remove_altq(struct pf_altq *a)
{
	struct priq_if *pif;

	if ((pif = a->altq_disc) == NULL)
		return (EINVAL);
	a->altq_disc = NULL;

	priq_clear_interface(pif);

	kfree(pif, M_ALTQ);
	return (0);
}

static int
priq_add_queue_locked(struct pf_altq *a, struct priq_if *pif)
{
	struct priq_class *cl;

	KKASSERT(a->priority < PRIQ_MAXPRI);
	KKASSERT(a->qid != 0);

	if (pif->pif_classes[a->priority] != NULL)
		return (EBUSY);
	if (clh_to_clp(pif, a->qid) != NULL)
		return (EBUSY);

	cl = priq_class_create(pif, a->priority, a->qlimit,
			       a->pq_u.priq_opts.flags, a->qid);
	if (cl == NULL)
		return (ENOMEM);

	return (0);
}

int
priq_add_queue(struct pf_altq *a)
{
	struct priq_if *pif;
	struct ifaltq *ifq;
	int error;

	/* check parameters */
	if (a->priority >= PRIQ_MAXPRI)
		return (EINVAL);
	if (a->qid == 0)
		return (EINVAL);

	/* XXX not MP safe */
	if ((pif = a->altq_disc) == NULL)
		return (EINVAL);
	ifq = pif->pif_ifq;

	PRIQ_LOCK(ifq);
	error = priq_add_queue_locked(a, pif);
	PRIQ_UNLOCK(ifq);

	return error;
}

static int
priq_remove_queue_locked(struct pf_altq *a, struct priq_if *pif)
{
	struct priq_class *cl;

	if ((cl = clh_to_clp(pif, a->qid)) == NULL)
		return (EINVAL);

	return (priq_class_destroy(cl));
}

int
priq_remove_queue(struct pf_altq *a)
{
	struct priq_if *pif;
	struct ifaltq *ifq;
	int error;

	/* XXX not MF safe */
	if ((pif = a->altq_disc) == NULL)
		return (EINVAL);
	ifq = pif->pif_ifq;

	PRIQ_LOCK(ifq);
	error = priq_remove_queue_locked(a, pif);
	PRIQ_UNLOCK(ifq);

	return error;
}

int
priq_getqstats(struct pf_altq *a, void *ubuf, int *nbytes)
{
	struct priq_if *pif;
	struct priq_class *cl;
	struct priq_classstats stats;
	struct ifaltq *ifq;
	int error = 0;

	if (*nbytes < sizeof(stats))
		return (EINVAL);

	ifnet_lock();

	/* XXX not MP safe */
	if ((pif = altq_lookup(a->ifname, ALTQT_PRIQ)) == NULL) {
		ifnet_unlock();
		return (EBADF);
	}
	ifq = pif->pif_ifq;

	PRIQ_LOCK(ifq);

	if ((cl = clh_to_clp(pif, a->qid)) == NULL) {
		PRIQ_UNLOCK(ifq);
		ifnet_unlock();
		return (EINVAL);
	}

	get_class_stats(&stats, cl);

	PRIQ_UNLOCK(ifq);

	ifnet_unlock();

	if ((error = copyout((caddr_t)&stats, ubuf, sizeof(stats))) != 0)
		return (error);
	*nbytes = sizeof(stats);
	return (0);
}

/*
 * bring the interface back to the initial state by discarding
 * all the filters and classes.
 */
static int
priq_clear_interface(struct priq_if *pif)
{
	struct priq_class *cl;
	int pri;

	/* clear out the classes */
	for (pri = 0; pri <= pif->pif_maxpri; pri++) {
		if ((cl = pif->pif_classes[pri]) != NULL)
			priq_class_destroy(cl);
	}

	return (0);
}

static int
priq_request(struct ifaltq_subque *ifsq, int req, void *arg)
{
	struct ifaltq *ifq = ifsq->ifsq_altq;
	struct priq_if *pif = (struct priq_if *)ifq->altq_disc;

	crit_enter();
	switch (req) {
	case ALTRQ_PURGE:
		if (ifsq_get_index(ifsq) == PRIQ_SUBQ_INDEX) {
			priq_purge(pif);
		} else {
			/*
			 * Race happened, the unrelated subqueue was
			 * picked during the packet scheduler transition.
			 */
			ifsq_classic_request(ifsq, ALTRQ_PURGE, NULL);
		}
		break;
	}
	crit_exit();
	return (0);
}

/* discard all the queued packets on the interface */
static void
priq_purge(struct priq_if *pif)
{
	struct priq_class *cl;
	int pri;

	for (pri = 0; pri <= pif->pif_maxpri; pri++) {
		if ((cl = pif->pif_classes[pri]) != NULL && !qempty(cl->cl_q))
			priq_purgeq(cl);
	}
	if (ifq_is_enabled(pif->pif_ifq))
		ALTQ_SQ_CNTR_RESET(&pif->pif_ifq->altq_subq[PRIQ_SUBQ_INDEX]);
}

static struct priq_class *
priq_class_create(struct priq_if *pif, int pri, int qlimit, int flags, int qid)
{
	struct priq_class *cl;

#ifndef ALTQ_RED
	if (flags & PRCF_RED) {
#ifdef ALTQ_DEBUG
		kprintf("priq_class_create: RED not configured for PRIQ!\n");
#endif
		return (NULL);
	}
#endif

	if ((cl = pif->pif_classes[pri]) != NULL) {
		/* modify the class instead of creating a new one */
		crit_enter();
		if (!qempty(cl->cl_q))
			priq_purgeq(cl);
		crit_exit();
#ifdef ALTQ_RIO
		if (q_is_rio(cl->cl_q))
			rio_destroy((rio_t *)cl->cl_red);
#endif
#ifdef ALTQ_RED
		if (q_is_red(cl->cl_q))
			red_destroy(cl->cl_red);
#endif
	} else {
		cl = kmalloc(sizeof(*cl), M_ALTQ, M_WAITOK | M_ZERO);
		cl->cl_q = kmalloc(sizeof(*cl->cl_q), M_ALTQ, M_WAITOK | M_ZERO);
	}

	pif->pif_classes[pri] = cl;
	if (flags & PRCF_DEFAULTCLASS)
		pif->pif_default = cl;
	if (qlimit == 0)
		qlimit = 50;  /* use default */
	qlimit(cl->cl_q) = qlimit;
	qtype(cl->cl_q) = Q_DROPTAIL;
	qlen(cl->cl_q) = 0;
	cl->cl_flags = flags;
	cl->cl_pri = pri;
	if (pri > pif->pif_maxpri)
		pif->pif_maxpri = pri;
	cl->cl_pif = pif;
	cl->cl_handle = qid;

#ifdef ALTQ_RED
	if (flags & (PRCF_RED|PRCF_RIO)) {
		int red_flags, red_pkttime;

		red_flags = 0;
		if (flags & PRCF_ECN)
			red_flags |= REDF_ECN;
#ifdef ALTQ_RIO
		if (flags & PRCF_CLEARDSCP)
			red_flags |= RIOF_CLEARDSCP;
#endif
		if (pif->pif_bandwidth < 8)
			red_pkttime = 1000 * 1000 * 1000; /* 1 sec */
		else
			red_pkttime = (int64_t)pif->pif_ifq->altq_ifp->if_mtu
			  * 1000 * 1000 * 1000 / (pif->pif_bandwidth / 8);
#ifdef ALTQ_RIO
		if (flags & PRCF_RIO) {
			cl->cl_red = (red_t *)rio_alloc(0, NULL,
						red_flags, red_pkttime);
			if (cl->cl_red != NULL)
				qtype(cl->cl_q) = Q_RIO;
		} else
#endif
		if (flags & PRCF_RED) {
			cl->cl_red = red_alloc(0, 0,
			    qlimit(cl->cl_q) * 10/100,
			    qlimit(cl->cl_q) * 30/100,
			    red_flags, red_pkttime);
			if (cl->cl_red != NULL)
				qtype(cl->cl_q) = Q_RED;
		}
	}
#endif /* ALTQ_RED */

	return (cl);
}

static int
priq_class_destroy(struct priq_class *cl)
{
	struct priq_if *pif;
	int pri;

	crit_enter();

	if (!qempty(cl->cl_q))
		priq_purgeq(cl);

	pif = cl->cl_pif;
	pif->pif_classes[cl->cl_pri] = NULL;
	if (pif->pif_maxpri == cl->cl_pri) {
		for (pri = cl->cl_pri; pri >= 0; pri--)
			if (pif->pif_classes[pri] != NULL) {
				pif->pif_maxpri = pri;
				break;
			}
		if (pri < 0)
			pif->pif_maxpri = -1;
	}
	crit_exit();

	if (cl->cl_red != NULL) {
#ifdef ALTQ_RIO
		if (q_is_rio(cl->cl_q))
			rio_destroy((rio_t *)cl->cl_red);
#endif
#ifdef ALTQ_RED
		if (q_is_red(cl->cl_q))
			red_destroy(cl->cl_red);
#endif
	}
	kfree(cl->cl_q, M_ALTQ);
	kfree(cl, M_ALTQ);
	return (0);
}

/*
 * priq_enqueue is an enqueue function to be registered to
 * (*ifsq_enqueue) in struct ifaltq_subque.
 */
static int
priq_enqueue(struct ifaltq_subque *ifsq, struct mbuf *m,
    struct altq_pktattr *pktattr)
{
	struct ifaltq *ifq = ifsq->ifsq_altq;
	struct priq_if *pif = (struct priq_if *)ifq->altq_disc;
	struct priq_class *cl;
	int error;
	int len;

	if (ifsq_get_index(ifsq) != PRIQ_SUBQ_INDEX) {
		/*
		 * Race happened, the unrelated subqueue was
		 * picked during the packet scheduler transition.
		 */
		ifsq_classic_request(ifsq, ALTRQ_PURGE, NULL);
		m_freem(m);
		return ENOBUFS;
	}

	crit_enter();

	/* grab class set by classifier */
	M_ASSERTPKTHDR(m);
	if (m->m_pkthdr.fw_flags & PF_MBUF_STRUCTURE)
		cl = clh_to_clp(pif, m->m_pkthdr.pf.qid);
	else
		cl = NULL;
	if (cl == NULL) {
		cl = pif->pif_default;
		if (cl == NULL) {
			m_freem(m);
			error = ENOBUFS;
			goto done;
		}
	}
	cl->cl_pktattr = NULL;
	len = m_pktlen(m);
	if (priq_addq(cl, m) != 0) {
		/* drop occurred.  mbuf was freed in priq_addq. */
		PKTCNTR_ADD(&cl->cl_dropcnt, len);
		error = ENOBUFS;
		goto done;
	}
	ALTQ_SQ_PKTCNT_INC(ifsq);
	error = 0;
done:
	crit_exit();
	return (error);
}

/*
 * priq_dequeue is a dequeue function to be registered to
 * (*ifsq_dequeue) in struct ifaltq_subque.
 *
 * note: ALTDQ_POLL returns the next packet without removing the packet
 *	from the queue.  ALTDQ_REMOVE is a normal dequeue operation.
 */
static struct mbuf *
priq_dequeue(struct ifaltq_subque *ifsq, int op)
{
	struct ifaltq *ifq = ifsq->ifsq_altq;
	struct priq_if *pif = (struct priq_if *)ifq->altq_disc;
	struct priq_class *cl;
	struct mbuf *m;
	int pri;

	if (ifsq_get_index(ifsq) != PRIQ_SUBQ_INDEX) {
		/*
		 * Race happened, the unrelated subqueue was
		 * picked during the packet scheduler transition.
		 */
		ifsq_classic_request(ifsq, ALTRQ_PURGE, NULL);
		return NULL;
	}

	if (ifsq_is_empty(ifsq)) {
		/* no packet in the queue */
		return (NULL);
	}

	crit_enter();
	m = NULL;
	for (pri = pif->pif_maxpri;  pri >= 0; pri--) {
		if ((cl = pif->pif_classes[pri]) != NULL && !qempty(cl->cl_q)) {
			if (op == ALTDQ_POLL) {
				m = priq_pollq(cl);
				break;
			}

			m = priq_getq(cl);
			if (m != NULL) {
				ALTQ_SQ_PKTCNT_DEC(ifsq);
				if (qempty(cl->cl_q))
					cl->cl_period++;
				PKTCNTR_ADD(&cl->cl_xmitcnt, m_pktlen(m));
			}
			break;
		}
	}
	crit_exit();
	return (m);
}

static int
priq_addq(struct priq_class *cl, struct mbuf *m)
{
#ifdef ALTQ_RIO
	if (q_is_rio(cl->cl_q))
		return rio_addq((rio_t *)cl->cl_red, cl->cl_q, m,
				cl->cl_pktattr);
#endif
#ifdef ALTQ_RED
	if (q_is_red(cl->cl_q))
		return red_addq(cl->cl_red, cl->cl_q, m, cl->cl_pktattr);
#endif
	if (qlen(cl->cl_q) >= qlimit(cl->cl_q)) {
		m_freem(m);
		return (-1);
	}

	if (cl->cl_flags & PRCF_CLEARDSCP)
		write_dsfield(m, cl->cl_pktattr, 0);

	_addq(cl->cl_q, m);

	return (0);
}

static struct mbuf *
priq_getq(struct priq_class *cl)
{
#ifdef ALTQ_RIO
	if (q_is_rio(cl->cl_q))
		return rio_getq((rio_t *)cl->cl_red, cl->cl_q);
#endif
#ifdef ALTQ_RED
	if (q_is_red(cl->cl_q))
		return red_getq(cl->cl_red, cl->cl_q);
#endif
	return _getq(cl->cl_q);
}

static struct mbuf *
priq_pollq(struct priq_class *cl)
{
	return qhead(cl->cl_q);
}

static void
priq_purgeq(struct priq_class *cl)
{
	struct mbuf *m;

	if (qempty(cl->cl_q))
		return;

	while ((m = _getq(cl->cl_q)) != NULL) {
		PKTCNTR_ADD(&cl->cl_dropcnt, m_pktlen(m));
		m_freem(m);
	}
	KKASSERT(qlen(cl->cl_q) == 0);
}

static void
get_class_stats(struct priq_classstats *sp, struct priq_class *cl)
{
	sp->class_handle = cl->cl_handle;
	sp->qlength = qlen(cl->cl_q);
	sp->qlimit = qlimit(cl->cl_q);
	sp->period = cl->cl_period;
	sp->xmitcnt = cl->cl_xmitcnt;
	sp->dropcnt = cl->cl_dropcnt;

	sp->qtype = qtype(cl->cl_q);
#ifdef ALTQ_RED
	if (q_is_red(cl->cl_q))
		red_getstats(cl->cl_red, &sp->red[0]);
#endif
#ifdef ALTQ_RIO
	if (q_is_rio(cl->cl_q))
		rio_getstats((rio_t *)cl->cl_red, &sp->red[0]);
#endif
}

/* convert a class handle to the corresponding class pointer */
static struct priq_class *
clh_to_clp(struct priq_if *pif, uint32_t chandle)
{
	struct priq_class *cl;
	int idx;

	if (chandle == 0)
		return (NULL);

	for (idx = pif->pif_maxpri; idx >= 0; idx--)
		if ((cl = pif->pif_classes[idx]) != NULL &&
		    cl->cl_handle == chandle)
			return (cl);

	return (NULL);
}

#endif /* ALTQ_PRIQ */
