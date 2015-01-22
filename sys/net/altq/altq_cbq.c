/*	$KAME: altq_cbq.c,v 1.20 2004/04/17 10:54:48 kjc Exp $	*/
/*	$DragonFly: src/sys/net/altq/altq_cbq.c,v 1.7 2008/05/14 11:59:23 sephe Exp $ */

/*
 * Copyright (c) Sun Microsystems, Inc. 1993-1998 All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *      This product includes software developed by the SMCC Technology
 *      Development Group at Sun Microsystems, Inc.
 *
 * 4. The name of the Sun Microsystems, Inc nor may not be used to endorse or
 *      promote products derived from this software without specific prior
 *      written permission.
 *
 * SUN MICROSYSTEMS DOES NOT CLAIM MERCHANTABILITY OF THIS SOFTWARE OR THE
 * SUITABILITY OF THIS SOFTWARE FOR ANY PARTICULAR PURPOSE.  The software is
 * provided "as is" without express or implied warranty of any kind.
 *
 * These notices must be retained in any copies of any part of this software.
 */

#include "opt_altq.h"
#include "opt_inet.h"
#include "opt_inet6.h"

#ifdef ALTQ_CBQ	/* cbq is enabled by ALTQ_CBQ option in opt_altq.h */

#include <sys/param.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/socket.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/callout.h>
#include <sys/errno.h>
#include <sys/time.h>
#include <sys/thread.h>

#include <net/if.h>
#include <net/ifq_var.h>
#include <netinet/in.h>

#include <net/pf/pfvar.h>
#include <net/altq/altq.h>
#include <net/altq/altq_cbq.h>

#include <sys/thread2.h>

#define CBQ_SUBQ_INDEX		ALTQ_SUBQ_INDEX_DEFAULT
#define CBQ_LOCK(ifq) \
    ALTQ_SQ_LOCK(&(ifq)->altq_subq[CBQ_SUBQ_INDEX])
#define CBQ_UNLOCK(ifq) \
    ALTQ_SQ_UNLOCK(&(ifq)->altq_subq[CBQ_SUBQ_INDEX])
#define CBQ_ASSERT_LOCKED(ifq) \
    ALTQ_SQ_ASSERT_LOCKED(&(ifq)->altq_subq[CBQ_SUBQ_INDEX])

/*
 * Forward Declarations.
 */
static int		 cbq_class_destroy(cbq_state_t *, struct rm_class *);
static struct rm_class  *clh_to_clp(cbq_state_t *, uint32_t);
static int		 cbq_clear_interface(cbq_state_t *);
static int		 cbq_request(struct ifaltq_subque *, int, void *);
static int		 cbq_enqueue(struct ifaltq_subque *, struct mbuf *,
			     struct altq_pktattr *);
static struct mbuf	*cbq_dequeue(struct ifaltq_subque *, int);
static void		 cbqrestart(struct ifaltq *);
static void		 get_class_stats(class_stats_t *, struct rm_class *);
static void		 cbq_purge(cbq_state_t *);

/*
 * int
 * cbq_class_destroy(cbq_mod_state_t *, struct rm_class *) - This
 *	function destroys a given traffic class.  Before destroying
 *	the class, all traffic for that class is released.
 */
static int
cbq_class_destroy(cbq_state_t *cbqp, struct rm_class *cl)
{
	int	i;

	/* delete the class */
	rmc_delete_class(&cbqp->ifnp, cl);

	/*
	 * free the class handle
	 */
	for (i = 0; i < CBQ_MAX_CLASSES; i++)
		if (cbqp->cbq_class_tbl[i] == cl)
			cbqp->cbq_class_tbl[i] = NULL;

	if (cl == cbqp->ifnp.root_)
		cbqp->ifnp.root_ = NULL;
	if (cl == cbqp->ifnp.default_)
		cbqp->ifnp.default_ = NULL;
	return (0);
}

/* convert class handle to class pointer */
static struct rm_class *
clh_to_clp(cbq_state_t *cbqp, uint32_t chandle)
{
	int i;
	struct rm_class *cl;

	if (chandle == 0)
		return (NULL);
	/*
	 * first, try optimistically the slot matching the lower bits of
	 * the handle.  if it fails, do the linear table search.
	 */
	i = chandle % CBQ_MAX_CLASSES;
	if ((cl = cbqp->cbq_class_tbl[i]) != NULL &&
	    cl->stats_.handle == chandle)
		return (cl);
	for (i = 0; i < CBQ_MAX_CLASSES; i++)
		if ((cl = cbqp->cbq_class_tbl[i]) != NULL &&
		    cl->stats_.handle == chandle)
			return (cl);
	return (NULL);
}

static int
cbq_clear_interface(cbq_state_t *cbqp)
{
	int		 again, i;
	struct rm_class	*cl;

	/* clear out the classes now */
	do {
		again = 0;
		for (i = 0; i < CBQ_MAX_CLASSES; i++) {
			if ((cl = cbqp->cbq_class_tbl[i]) != NULL) {
				if (is_a_parent_class(cl))
					again++;
				else {
					cbq_class_destroy(cbqp, cl);
					cbqp->cbq_class_tbl[i] = NULL;
					if (cl == cbqp->ifnp.root_)
						cbqp->ifnp.root_ = NULL;
					if (cl == cbqp->ifnp.default_)
						cbqp->ifnp.default_ = NULL;
				}
			}
		}
	} while (again);

	return (0);
}

static int
cbq_request(struct ifaltq_subque *ifsq, int req, void *arg)
{
	struct ifaltq *ifq = ifsq->ifsq_altq;
	cbq_state_t	*cbqp = (cbq_state_t *)ifq->altq_disc;

	crit_enter();
	switch (req) {
	case ALTRQ_PURGE:
		if (ifsq_get_index(ifsq) == CBQ_SUBQ_INDEX) {
			cbq_purge(cbqp);
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

/* copy the stats info in rm_class to class_states_t */
static void
get_class_stats(class_stats_t *statsp, struct rm_class *cl)
{
	statsp->xmit_cnt	= cl->stats_.xmit_cnt;
	statsp->drop_cnt	= cl->stats_.drop_cnt;
	statsp->over		= cl->stats_.over;
	statsp->borrows		= cl->stats_.borrows;
	statsp->overactions	= cl->stats_.overactions;
	statsp->delays		= cl->stats_.delays;

	statsp->depth		= cl->depth_;
	statsp->priority	= cl->pri_;
	statsp->maxidle		= cl->maxidle_;
	statsp->minidle		= cl->minidle_;
	statsp->offtime		= cl->offtime_;
	statsp->qmax		= qlimit(cl->q_);
	statsp->ns_per_byte	= cl->ns_per_byte_;
	statsp->wrr_allot	= cl->w_allotment_;
	statsp->qcnt		= qlen(cl->q_);
	statsp->avgidle		= cl->avgidle_;

	statsp->qtype		= qtype(cl->q_);
#ifdef ALTQ_RED
	if (q_is_red(cl->q_))
		red_getstats(cl->red_, &statsp->red[0]);
#endif
#ifdef ALTQ_RIO
	if (q_is_rio(cl->q_))
		rio_getstats((rio_t *)cl->red_, &statsp->red[0]);
#endif
}

int
cbq_pfattach(struct pf_altq *a, struct ifaltq *ifq)
{
	return altq_attach(ifq, ALTQT_CBQ, a->altq_disc, ifq_mapsubq_default,
	    cbq_enqueue, cbq_dequeue, cbq_request, NULL, NULL);
}

int
cbq_add_altq(struct pf_altq *a)
{
	cbq_state_t	*cbqp;
	struct ifnet	*ifp;

	ifnet_lock();

	if ((ifp = ifunit(a->ifname)) == NULL) {
		ifnet_unlock();
		return (EINVAL);
	}
	if (!ifq_is_ready(&ifp->if_snd)) {
		ifnet_unlock();
		return (ENODEV);
	}

	/* allocate and initialize cbq_state_t */
	cbqp = kmalloc(sizeof(*cbqp), M_ALTQ, M_WAITOK | M_ZERO);
	callout_init(&cbqp->cbq_callout);
	cbqp->cbq_qlen = 0;
	cbqp->ifnp.ifq_ = &ifp->if_snd;	    /* keep the ifq */
	ifq_purge_all(&ifp->if_snd);

	ifnet_unlock();

	/* keep the state in pf_altq */
	a->altq_disc = cbqp;

	return (0);
}

int
cbq_remove_altq(struct pf_altq *a)
{
	cbq_state_t	*cbqp;

	if ((cbqp = a->altq_disc) == NULL)
		return (EINVAL);
	a->altq_disc = NULL;

	cbq_clear_interface(cbqp);

	if (cbqp->ifnp.default_)
		cbq_class_destroy(cbqp, cbqp->ifnp.default_);
	if (cbqp->ifnp.root_)
		cbq_class_destroy(cbqp, cbqp->ifnp.root_);

	/* deallocate cbq_state_t */
	kfree(cbqp, M_ALTQ);

	return (0);
}

static int
cbq_add_queue_locked(struct pf_altq *a, cbq_state_t *cbqp)
{
	struct rm_class	*borrow, *parent;
	struct rm_class	*cl;
	struct cbq_opts	*opts;
	int		i;

	KKASSERT(a->qid != 0);

	/*
	 * find a free slot in the class table.  if the slot matching
	 * the lower bits of qid is free, use this slot.  otherwise,
	 * use the first free slot.
	 */
	i = a->qid % CBQ_MAX_CLASSES;
	if (cbqp->cbq_class_tbl[i] != NULL) {
		for (i = 0; i < CBQ_MAX_CLASSES; i++)
			if (cbqp->cbq_class_tbl[i] == NULL)
				break;
		if (i == CBQ_MAX_CLASSES)
			return (EINVAL);
	}

	opts = &a->pq_u.cbq_opts;
	/* check parameters */
	if (a->priority >= CBQ_MAXPRI)
		return (EINVAL);

	/* Get pointers to parent and borrow classes.  */
	parent = clh_to_clp(cbqp, a->parent_qid);
	if (opts->flags & CBQCLF_BORROW)
		borrow = parent;
	else
		borrow = NULL;

	/*
	 * A class must borrow from it's parent or it can not
	 * borrow at all.  Hence, borrow can be null.
	 */
	if (parent == NULL && (opts->flags & CBQCLF_ROOTCLASS) == 0) {
		kprintf("cbq_add_queue: no parent class!\n");
		return (EINVAL);
	}

	if ((borrow != parent)  && (borrow != NULL)) {
		kprintf("cbq_add_class: borrow class != parent\n");
		return (EINVAL);
	}

	/*
	 * check parameters
	 */
	switch (opts->flags & CBQCLF_CLASSMASK) {
	case CBQCLF_ROOTCLASS:
		if (parent != NULL)
			return (EINVAL);
		if (cbqp->ifnp.root_)
			return (EINVAL);
		break;
	case CBQCLF_DEFCLASS:
		if (cbqp->ifnp.default_)
			return (EINVAL);
		break;
	case 0:
		if (a->qid == 0)
			return (EINVAL);
		break;
	default:
		/* more than two flags bits set */
		return (EINVAL);
	}

	/*
	 * create a class.  if this is a root class, initialize the
	 * interface.
	 */
	if ((opts->flags & CBQCLF_CLASSMASK) == CBQCLF_ROOTCLASS) {
		rmc_init(cbqp->ifnp.ifq_, &cbqp->ifnp, opts->ns_per_byte,
		    cbqrestart, a->qlimit, RM_MAXQUEUED,
		    opts->maxidle, opts->minidle, opts->offtime,
		    opts->flags);
		cl = cbqp->ifnp.root_;
	} else {
		cl = rmc_newclass(a->priority,
				  &cbqp->ifnp, opts->ns_per_byte,
				  rmc_delay_action, a->qlimit, parent, borrow,
				  opts->maxidle, opts->minidle, opts->offtime,
				  opts->pktsize, opts->flags);
	}
	if (cl == NULL)
		return (ENOMEM);

	/* return handle to user space. */
	cl->stats_.handle = a->qid;
	cl->stats_.depth = cl->depth_;

	/* save the allocated class */
	cbqp->cbq_class_tbl[i] = cl;

	if ((opts->flags & CBQCLF_CLASSMASK) == CBQCLF_DEFCLASS)
		cbqp->ifnp.default_ = cl;

	return (0);
}

int
cbq_add_queue(struct pf_altq *a)
{
	cbq_state_t *cbqp;
	struct ifaltq *ifq;
	int error;

	if (a->qid == 0)
		return (EINVAL);

	/* XXX not MP safe */
	if ((cbqp = a->altq_disc) == NULL)
		return (EINVAL);
	ifq = cbqp->ifnp.ifq_;

	CBQ_LOCK(ifq);
	error = cbq_add_queue_locked(a, cbqp);
	CBQ_UNLOCK(ifq);

	return error;
}

static int
cbq_remove_queue_locked(struct pf_altq *a, cbq_state_t *cbqp)
{
	struct rm_class	*cl;
	int		i;

	if ((cl = clh_to_clp(cbqp, a->qid)) == NULL)
		return (EINVAL);

	/* if we are a parent class, then return an error. */
	if (is_a_parent_class(cl))
		return (EINVAL);

	/* delete the class */
	rmc_delete_class(&cbqp->ifnp, cl);

	/*
	 * free the class handle
	 */
	for (i = 0; i < CBQ_MAX_CLASSES; i++)
		if (cbqp->cbq_class_tbl[i] == cl) {
			cbqp->cbq_class_tbl[i] = NULL;
			if (cl == cbqp->ifnp.root_)
				cbqp->ifnp.root_ = NULL;
			if (cl == cbqp->ifnp.default_)
				cbqp->ifnp.default_ = NULL;
			break;
		}

	return (0);
}

int
cbq_remove_queue(struct pf_altq *a)
{
	cbq_state_t *cbqp;
	struct ifaltq *ifq;
	int error;

	/* XXX not MP safe */
	if ((cbqp = a->altq_disc) == NULL)
		return (EINVAL);
	ifq = cbqp->ifnp.ifq_;

	CBQ_LOCK(ifq);
	error = cbq_remove_queue_locked(a, cbqp);
	CBQ_UNLOCK(ifq);

	return error;
}

int
cbq_getqstats(struct pf_altq *a, void *ubuf, int *nbytes)
{
	cbq_state_t	*cbqp;
	struct rm_class	*cl;
	class_stats_t	 stats;
	int		 error = 0;
	struct ifaltq 	*ifq;

	if (*nbytes < sizeof(stats))
		return (EINVAL);

	ifnet_lock();

	/* XXX not MP safe */
	if ((cbqp = altq_lookup(a->ifname, ALTQT_CBQ)) == NULL) {
		ifnet_unlock();
		return (EBADF);
	}
	ifq = cbqp->ifnp.ifq_;

	CBQ_LOCK(ifq);

	if ((cl = clh_to_clp(cbqp, a->qid)) == NULL) {
		CBQ_UNLOCK(ifq);
		ifnet_unlock();
		return (EINVAL);
	}

	get_class_stats(&stats, cl);

	CBQ_UNLOCK(ifq);

	ifnet_unlock();

	if ((error = copyout((caddr_t)&stats, ubuf, sizeof(stats))) != 0)
		return (error);
	*nbytes = sizeof(stats);
	return (0);
}

/*
 * int
 * cbq_enqueue(struct ifaltq_subqueue *ifq, struct mbuf *m,
 *     struct altq_pktattr *pattr)
 *		- Queue data packets.
 *
 *	cbq_enqueue is set to ifp->if_altqenqueue and called by an upper
 *	layer (e.g. ether_output).  cbq_enqueue queues the given packet
 *	to the cbq, then invokes the driver's start routine.
 *
 *	Returns:	0 if the queueing is successful.
 *			ENOBUFS if a packet dropping occurred as a result of
 *			the queueing.
 */

static int
cbq_enqueue(struct ifaltq_subque *ifsq, struct mbuf *m,
    struct altq_pktattr *pktattr __unused)
{
	struct ifaltq *ifq = ifsq->ifsq_altq;
	cbq_state_t	*cbqp = (cbq_state_t *)ifq->altq_disc;
	struct rm_class	*cl;
	int len;

	if (ifsq_get_index(ifsq) != CBQ_SUBQ_INDEX) {
		/*
		 * Race happened, the unrelated subqueue was
		 * picked during the packet scheduler transition.
		 */
		ifsq_classic_request(ifsq, ALTRQ_PURGE, NULL);
		m_freem(m);
		return (ENOBUFS);
	}

	/* grab class set by classifier */
	M_ASSERTPKTHDR(m);
	if (m->m_pkthdr.fw_flags & PF_MBUF_STRUCTURE)
		cl = clh_to_clp(cbqp, m->m_pkthdr.pf.qid);
	else
		cl = NULL;
	if (cl == NULL) {
		cl = cbqp->ifnp.default_;
		if (cl == NULL) {
			m_freem(m);
			return (ENOBUFS);
		}
	}
	crit_enter();
	cl->pktattr_ = NULL;
	len = m_pktlen(m);
	if (rmc_queue_packet(cl, m) != 0) {
		/* drop occurred.  some mbuf was freed in rmc_queue_packet. */
		PKTCNTR_ADD(&cl->stats_.drop_cnt, len);
		crit_exit();
		return (ENOBUFS);
	}

	/* successfully queued. */
	++cbqp->cbq_qlen;
	ALTQ_SQ_PKTCNT_INC(ifsq);
	crit_exit();
	return (0);
}

static struct mbuf *
cbq_dequeue(struct ifaltq_subque *ifsq, int op)
{
	struct ifaltq *ifq = ifsq->ifsq_altq;
	cbq_state_t	*cbqp = (cbq_state_t *)ifq->altq_disc;
	struct mbuf	*m;

	if (ifsq_get_index(ifsq) != CBQ_SUBQ_INDEX) {
		/*
		 * Race happened, the unrelated subqueue was
		 * picked during the packet scheduler transition.
		 */
		ifsq_classic_request(ifsq, ALTRQ_PURGE, NULL);
		return NULL;
	}

	crit_enter();
	m = rmc_dequeue_next(&cbqp->ifnp, op);

	if (m && op == ALTDQ_REMOVE) {
		--cbqp->cbq_qlen;  /* decrement # of packets in cbq */
		ALTQ_SQ_PKTCNT_DEC(ifsq);

		/* Update the class. */
		rmc_update_class_util(&cbqp->ifnp);
	}
	crit_exit();
	return (m);
}

/*
 * void
 * cbqrestart(queue_t *) - Restart sending of data.
 * called from rmc_restart in a critical section via timeout after waking up
 * a suspended class.
 *	Returns:	NONE
 */

static void
cbqrestart(struct ifaltq *ifq)
{
	cbq_state_t	*cbqp;

	CBQ_ASSERT_LOCKED(ifq);

	if (!ifq_is_enabled(ifq))
		/* cbq must have been detached */
		return;

	if ((cbqp = (cbq_state_t *)ifq->altq_disc) == NULL)
		/* should not happen */
		return;

	if (cbqp->cbq_qlen > 0) {
		struct ifnet *ifp = ifq->altq_ifp;
		struct ifaltq_subque *ifsq = &ifq->altq_subq[CBQ_SUBQ_INDEX];

		/* Release the altq lock to avoid deadlock */
		CBQ_UNLOCK(ifq);

		ifsq_serialize_hw(ifsq);
		if (ifp->if_start && !ifsq_is_oactive(ifsq))
			(*ifp->if_start)(ifp, ifsq);
		ifsq_deserialize_hw(ifsq);

		CBQ_LOCK(ifq);
	}
}

static void
cbq_purge(cbq_state_t *cbqp)
{
	struct rm_class	*cl;
	int i;
	for (i = 0; i < CBQ_MAX_CLASSES; i++) {
		if ((cl = cbqp->cbq_class_tbl[i]) != NULL)
			rmc_dropall(cl);
	}
	if (ifq_is_enabled(cbqp->ifnp.ifq_))
		ALTQ_SQ_CNTR_RESET(&cbqp->ifnp.ifq_->altq_subq[CBQ_SUBQ_INDEX]);
}

#endif /* ALTQ_CBQ */
