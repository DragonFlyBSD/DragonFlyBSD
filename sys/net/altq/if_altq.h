/*	$KAME: if_altq.h,v 1.11 2003/07/10 12:07:50 kjc Exp $	*/

/*
 * Copyright (C) 1997-2003
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
#ifndef _NET_ALTQ_IF_ALTQ_H_
#define	_NET_ALTQ_IF_ALTQ_H_

#ifndef _SYS_SERIALIZE_H_
#include <sys/serialize.h>
#endif

/* Default subqueue */
#define ALTQ_SUBQ_INDEX_DEFAULT	0

struct mbuf;
struct altq_pktattr;

struct ifaltq_subque;
struct ifaltq;

typedef int (*altq_mapsubq_t)(struct ifaltq *, int);

typedef int (*ifsq_enqueue_t)(struct ifaltq_subque *, struct mbuf *,
    struct altq_pktattr *);
typedef struct mbuf *(*ifsq_dequeue_t)(struct ifaltq_subque *, int);
typedef int (*ifsq_request_t)(struct ifaltq_subque *, int, void *);

struct ifsubq_stage {
	struct ifaltq_subque *stg_subq;
	int		stg_cnt;
	int		stg_len;
	uint32_t	stg_flags;
	TAILQ_ENTRY(ifsubq_stage) stg_link;
} __cachealign;

#define IFSQ_STAGE_FLAG_QUED	0x1
#define IFSQ_STAGE_FLAG_SCHED	0x2

struct ifaltq_subque {
	struct lwkt_serialize ifsq_lock;
	int		ifsq_index;

	struct ifaltq	*ifsq_altq;
	struct ifnet	*ifsq_ifp;
	void		*ifsq_hw_priv;	/* hw private data */

	struct mbuf	*ifsq_prio_head;
	struct mbuf	*ifsq_prio_tail;
	struct mbuf	*ifsq_norm_head;
	struct mbuf	*ifsq_norm_tail;
	int		ifsq_prio_len;
	int		ifsq_prio_bcnt;
	int		ifsq_len;	/* packet counter */
	int		ifsq_maxlen;
	int		ifsq_bcnt;	/* byte counter */
	int		ifsq_maxbcnt;

	ifsq_enqueue_t	ifsq_enqueue;
	ifsq_dequeue_t	ifsq_dequeue;
	ifsq_request_t	ifsq_request;

	struct lwkt_serialize *ifsq_hw_serialize;
					/* hw serializer */
	struct mbuf	*ifsq_prepended;/* mbuf dequeued, but not yet xmit */
	int		ifsq_started;	/* ifnet.if_start interlock */
	int		ifsq_hw_oactive;/* hw too busy, protected by driver */
	int		ifsq_cpuid;	/* owner cpu */
	struct ifsubq_stage *ifsq_stage;/* packet staging information */
	struct netmsg_base *ifsq_ifstart_nmsg;
					/* percpu msgs to sched if_start */
} __cachealign;

#ifdef _KERNEL

#define ALTQ_SQ_ASSERT_LOCKED(ifsq)	ASSERT_SERIALIZED(&(ifsq)->ifsq_lock)
#define ALTQ_SQ_LOCK_INIT(ifsq)		lwkt_serialize_init(&(ifsq)->ifsq_lock)
#define ALTQ_SQ_LOCK(ifsq) \
	lwkt_serialize_adaptive_enter(&(ifsq)->ifsq_lock)
#define ALTQ_SQ_UNLOCK(ifsq)		lwkt_serialize_exit(&(ifsq)->ifsq_lock)

#define ASSERT_ALTQ_SQ_SERIALIZED_HW(ifsq) \
	ASSERT_SERIALIZED((ifsq)->ifsq_hw_serialize)
#define ASSERT_ALTQ_SQ_NOT_SERIALIZED_HW(ifsq) \
	ASSERT_NOT_SERIALIZED((ifsq)->ifsq_hw_serialize)

#define ALTQ_SQ_PKTCNT_INC(ifsq) \
do { \
	(ifsq)->ifsq_len++; \
} while (0)

#define ALTQ_SQ_PKTCNT_DEC(ifsq) \
do { \
	KASSERT((ifsq)->ifsq_len > 0, ("invalid packet count")); \
	(ifsq)->ifsq_len--; \
} while (0)

#define ALTQ_SQ_CNTR_INC(ifsq, bcnt) \
do { \
	ALTQ_SQ_PKTCNT_INC((ifsq)); \
	(ifsq)->ifsq_bcnt += (bcnt); \
} while (0)

#define ALTQ_SQ_CNTR_DEC(ifsq, bcnt) \
do { \
	ALTQ_SQ_PKTCNT_DEC((ifsq)); \
	KASSERT((ifsq)->ifsq_bcnt >= (bcnt), ("invalid byte count")); \
	(ifsq)->ifsq_bcnt -= (bcnt); \
} while (0)

#define ALTQ_SQ_CNTR_RESET(ifsq) \
do { \
	(ifsq)->ifsq_len = 0; \
	(ifsq)->ifsq_bcnt = 0; \
} while (0)

#define ALTQ_SQ_PRIO_CNTR_INC(ifsq, bcnt) \
do { \
	(ifsq)->ifsq_prio_len++; \
	(ifsq)->ifsq_prio_bcnt += (bcnt); \
} while (0)

#define ALTQ_SQ_PRIO_CNTR_DEC(ifsq, bcnt) \
do { \
	KASSERT((ifsq)->ifsq_prio_len > 0, \
	    ("invalid prio packet count")); \
	(ifsq)->ifsq_prio_len--; \
	KASSERT((ifsq)->ifsq_prio_bcnt >= (bcnt), \
	    ("invalid prio byte count")); \
	(ifsq)->ifsq_prio_bcnt -= (bcnt); \
} while (0)

#endif	/* _KERNEL */

/*
 * Structure defining a queue for a network interface.
 */
struct	ifaltq {
	/* alternate queueing related fields */
	int	altq_type;		/* discipline type */
	int	altq_flags;		/* flags (e.g. ready, in-use) */
	void	*altq_disc;		/* for discipline-specific use */
	struct	ifnet *altq_ifp;	/* back pointer to interface */

	/* classifier fields */
	void	*altq_clfier;		/* classifier-specific use */
	void	*(*altq_classify)(struct ifaltq *, struct mbuf *,
				  struct altq_pktattr *);

	/* token bucket regulator */
	struct	tb_regulator *altq_tbr;

	/* Sub-queues mapping */
	altq_mapsubq_t altq_mapsubq;
	uint32_t altq_subq_mask;

	/* Sub-queues */
	int	altq_subq_cnt;
	struct ifaltq_subque *altq_subq;

	int	altq_maxlen;
};

#ifdef _KERNEL
/* COMPAT */
#define ALTQ_LOCK(ifq) \
	ALTQ_SQ_LOCK(&(ifq)->altq_subq[ALTQ_SUBQ_INDEX_DEFAULT])
/* COMPAT */
#define ALTQ_UNLOCK(ifq) \
	ALTQ_SQ_UNLOCK(&(ifq)->altq_subq[ALTQ_SUBQ_INDEX_DEFAULT])
#endif

#ifdef _KERNEL

/*
 * packet attributes used by queueing disciplines.
 * pattr_class is a discipline-dependent scheduling class that is
 * set by a classifier.
 * pattr_hdr and pattr_af may be used by a discipline to access
 * the header within a mbuf.  (e.g. ECN needs to update the CE bit)
 * note that pattr_hdr could be stale after m_pullup, though link
 * layer output routines usually don't use m_pullup.  link-level
 * compression also invalidates these fields.  thus, pattr_hdr needs
 * to be verified when a discipline touches the header.
 */
struct altq_pktattr {
	void	*pattr_class;		/* sched class set by classifier */
	int	pattr_af;		/* address family */
	caddr_t	pattr_hdr;		/* saved header position in mbuf */
};

/*
 * a token-bucket regulator limits the rate that a network driver can
 * dequeue packets from the output queue.
 * modern cards are able to buffer a large amount of packets and dequeue
 * too many packets at a time.  this bursty dequeue behavior makes it
 * impossible to schedule packets by queueing disciplines.
 * a token-bucket is used to control the burst size in a device
 * independent manner.
 */
struct tb_regulator {
	int64_t		tbr_rate;	/* (scaled) token bucket rate */
	int64_t		tbr_depth;	/* (scaled) token bucket depth */

	int64_t		tbr_token;	/* (scaled) current token */
	int64_t		tbr_filluptime;	/* (scaled) time to fill up bucket */
	uint64_t	tbr_last;	/* last time token was updated */

	int		tbr_lastop;	/* last dequeue operation type
					   needed for poll-and-dequeue */
};

/* if_altqflags */
#define	ALTQF_READY	 0x01	/* driver supports alternate queueing */
#define	ALTQF_ENABLED	 0x02	/* altq is in use */
#define	ALTQF_CLASSIFY	 0x04	/* classify packets */
#define	ALTQF_DRIVER1	 0x40	/* driver specific */

/* if_altqflags set internally only: */
#define	ALTQF_CANTCHANGE 	(ALTQF_READY)

/* altq_dequeue 2nd arg */
#define	ALTDQ_REMOVE		1	/* dequeue mbuf from the queue */
#define	ALTDQ_POLL		2	/* don't dequeue mbuf from the queue */

/* altq request types (currently only purge is defined) */
#define	ALTRQ_PURGE		1	/* purge all packets */

int	altq_attach(struct ifaltq *, int, void *, altq_mapsubq_t,
	    ifsq_enqueue_t, ifsq_dequeue_t, ifsq_request_t, void *,
	    void *(*)(struct ifaltq *, struct mbuf *, struct altq_pktattr *));
int	altq_detach(struct ifaltq *);
int	altq_enable(struct ifaltq *);
int	altq_disable(struct ifaltq *);
struct mbuf *tbr_dequeue(struct ifaltq_subque *, int);
extern int	(*altq_input)(struct mbuf *, int);
#endif /* _KERNEL */

#endif /* _NET_ALTQ_IF_ALTQ_H_ */
