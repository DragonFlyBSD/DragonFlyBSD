/*-
 * Copyright (c) 2005-2008 Daniel Braniss <danny@cs.huji.ac.il>
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
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD: src/sys/dev/iscsi/initiator/iscsivar.h,v 1.2 2008/11/25 07:17:11 scottl Exp $
 */
/*
 | $Id: iscsivar.h,v 1.30 2007/04/22 10:12:11 danny Exp danny $
 */
#ifndef ISCSI_INITIATOR_DEBUG
#define ISCSI_INITIATOR_DEBUG 1
#endif

#ifdef ISCSI_INITIATOR_DEBUG
extern int iscsi_debug;
#define debug(level, fmt, args...)	do {if(level <= iscsi_debug)\
	kprintf("%s: " fmt "\n", __func__ , ##args);} while(0)
#define sdebug(level, fmt, args...)	do {if(level <= iscsi_debug)\
	kprintf("%d] %s: " fmt "\n", sp->sid, __func__ , ##args);} while(0)
#define debug_called(level)		do {if(level <= iscsi_debug)\
	kprintf("%s: called\n",  __func__);} while(0)
#else
#define debug(level, fmt, args...)
#define debug_called(level)
#define sdebug(level, fmt, args...)
#endif /* ISCSI_INITIATOR_DEBUG */

#define xdebug(fmt, args...)	kprintf(">>> %s: " fmt "\n", __func__ , ##args)

#define PROC_LOCK(p)
#define PROC_UNLOCK(p)

#define MAX_SESSIONS		256

typedef uint32_t digest_t(const void *, int len, uint32_t ocrc);

typedef struct objcache	*objcache_t;

MALLOC_DECLARE(M_ISCSI);

#ifndef BIT
#define BIT(n)	(1 <<(n))
#endif

#define ISC_SM_RUN	BIT(0)
#define ISC_SM_RUNNING	BIT(1)

#define ISC_LINK_UP	BIT(2)
#define ISC_CON_RUN	BIT(3)
#define ISC_CON_RUNNING	BIT(4)
#define ISC_KILL	BIT(5)
#define ISC_OQNOTEMPTY	BIT(6)
#define ISC_OWAITING	BIT(7)
#define ISC_FFPHASE	BIT(8)
#define ISC_FFPWAIT	BIT(9)

#define ISC_MEMWAIT	BIT(10)
#define ISC_SIGNALED	BIT(11)
#define ISC_FROZEN	BIT(12)
#define ISC_STALLED	BIT(13)

#define ISC_HOLD	BIT(14)
#define ISC_HOLDED	BIT(15)

#define ISC_SHUTDOWN	BIT(31)

/*
 | some stats
 */
struct i_stats {
     int	npdu;	// number of pdus malloc'ed.
     int	nrecv;	// unprocessed received pdus
     int	nsent;	// sent pdus

     int	nrsp, max_rsp;
     int	nrsv, max_rsv;
     int	ncsnd, max_csnd;
     int	nisnd, max_isnd;
     int	nwsnd, max_wsnd;
     int	nhld, max_hld;

     struct timeval t_sent;
     struct timeval t_recv;
};

/*
 | one per 'session'
 */

typedef TAILQ_HEAD(, pduq) queue_t;

typedef struct isc_session {
     TAILQ_ENTRY(isc_session)	sp_link;
     int		flags;
     struct cdev	*dev;
     struct socket	*soc;
     struct file	*fp;
     struct thread	*td;

     struct proc 	*proc; // the userland process
     int		signal;

     struct thread 	*soc_thr;

     struct thread	*stp;	// the sm thread

     struct isc_softc	*isc;

     digest_t   	*hdrDigest;     // the digest alg. if any
     digest_t   	*dataDigest;    // the digest alg. if any

     int		sid;		// Session ID
     int		targetid;
//     int		cid;		// Connection ID
//     int		tsih;		// target session identifier handle
     sn_t       	sn;             // sequence number stuff;
     int		cws;		// current window size

     int		target_nluns; // this and target_lun are
				      // hopefully temporal till I
				      // figure out a better way.
     lun_id_t		target_lun[ISCSI_MAX_LUNS];

     struct mtx		rsp_mtx;
     struct mtx		rsv_mtx;
     struct mtx		snd_mtx;
     struct mtx		hld_mtx;
     struct mtx		io_mtx;
     queue_t		rsp;
     queue_t		rsv;
     queue_t		csnd;
     queue_t		isnd;
     queue_t		wsnd;
     queue_t		hld;

     /*
      | negotiable values
      */
     isc_opt_t		opt;

     struct i_stats	stats;
     struct cam_path	*cam_path;
     bhs_t		bhs;
     struct uio		uio;
     struct iovec	iov;
     /*
      | sysctl stuff
      */
     struct sysctl_ctx_list	clist;
     struct sysctl_oid	*oid;
     int	douio;	//XXX: turn on/off uio on read
} isc_session_t;

typedef struct pduq {
     TAILQ_ENTRY(pduq)	pq_link;

     caddr_t		buf;
     u_int		len;	// the total length of the pdu
     pdu_t		pdu;
     union ccb		*ccb;

     struct uio		uio;
     struct iovec	iov[5];	// XXX: careful ...
     struct mbuf	*mp;
     struct timeval	ts;
     int 		refcnt;
     queue_t		*pduq;
} pduq_t;

struct isc_softc {
     //int		state;
     struct cdev	*dev;
     eventhandler_tag	eh;
     char		isid[6];	// Initiator Session ID (48 bits)
     struct lock	lock;

     int			nsess;
     TAILQ_HEAD(,isc_session)	isc_sess;
     isc_session_t		*sessions[MAX_SESSIONS];

     struct lock		pdu_lock;
#ifdef  ISCSI_INITIATOR_DEBUG
     int			 npdu_alloc, npdu_max; // for instrumentation
#endif
#define MAX_PDUS	(MAX_SESSIONS*256) // XXX: at the moment this is arbitrary
     objcache_t			pdu_zone; // pool of free pdu's
     TAILQ_HEAD(,pduq)		freepdu;
     /*
      | cam stuff
      */
     struct cam_sim		*cam_sim;
     struct cam_path		*cam_path;
     struct lock		cam_lock;
     /*
      | sysctl stuff
      */
     struct sysctl_ctx_list	clist;
     struct sysctl_oid		*oid;
};

#ifdef  ISCSI_INITIATOR_DEBUG
extern struct lock iscsi_dbg_lock;
#endif

void	isc_start_receiver(isc_session_t *sp);
void	isc_stop_receiver(isc_session_t *sp);

int	isc_sendPDU(isc_session_t *sp, pduq_t *pq);
int	isc_qout(isc_session_t *sp, pduq_t *pq);
int	i_prepPDU(isc_session_t *sp, pduq_t *pq);

int	ism_fullfeature(struct cdev *dev, int flag);

int	i_pdu_flush(isc_session_t *sc);
int	i_setopt(isc_session_t *sp, isc_opt_t *opt);
void	i_freeopt(isc_opt_t *opt);

int	ic_init(struct isc_softc *sc);
void	ic_destroy(struct isc_softc *sc);
int	ic_fullfeature(struct cdev *dev);
void	ic_lost_target(isc_session_t *sp, int target);
int	ic_getCamVals(isc_session_t *sp, iscsi_cam_t *cp);

void	ism_recv(isc_session_t *sp, pduq_t *pq);
int	ism_start(isc_session_t *sp);
void	ism_stop(isc_session_t *sp);

int	scsi_encap(struct cam_sim *sim, union ccb *ccb);
int	scsi_decap(isc_session_t *sp, pduq_t *opq, pduq_t *pq);
void	iscsi_r2t(isc_session_t *sp, pduq_t *opq, pduq_t *pq);
void	iscsi_done(isc_session_t *sp, pduq_t *opq, pduq_t *pq);
void	iscsi_reject(isc_session_t *sp, pduq_t *opq, pduq_t *pq);
void	iscsi_async(isc_session_t *sp,  pduq_t *pq);
void	iscsi_cleanup(isc_session_t *sp);
int	iscsi_requeue(isc_session_t *sp);

void	ic_freeze(isc_session_t *sp);
void	ic_release(isc_session_t *sp);

// Serial Number Arithmetic
#define _MAXINCR	0x7FFFFFFF	// 2 ^ 31 - 1
#define SNA_GT(i1, i2)	((i1 != i2) && (\
	(i1 < i2 && i2 - i1 > _MAXINCR) ||\
	(i1 > i2 && i1 - i2 < _MAXINCR))?1: 0)

/*
 * inlines
 *
 * DragonFly note: CAM locks itself, peripherals do not lock CAM.
 */
#ifdef _CAM_CAM_XPT_SIM_H

#define CAM_LOCK(arg)	/*lockmgr(&arg->cam_lock, LK_EXCLUSIVE)*/
#define CAM_UNLOCK(arg)	/*lockmgr(&arg->cam_lock, LK_RELEASE)*/

static __inline void
XPT_DONE(struct isc_softc *isp, union ccb *ccb)
{
     CAM_LOCK(isp);
     xpt_done(ccb);
     CAM_UNLOCK(isp);
}

#endif /* _CAM_CAM_XPT_SIM_H */

#define iscsi_lock_ex(mtx)	mtx_lock_ex_quick(mtx)
#define iscsi_unlock_ex(mtx)	mtx_unlock(mtx)
#define issleep(id, mtx, flags, wmesg, to)	\
				mtxsleep(id, mtx, flags, wmesg, to)

static __inline pduq_t *
pdu_alloc(struct isc_softc *isc, int wait)
{
     pduq_t	*pq;

     lockmgr(&isc->pdu_lock, LK_EXCLUSIVE);
     if((pq = TAILQ_FIRST(&isc->freepdu)) == NULL) {
	  lockmgr(&isc->pdu_lock, LK_RELEASE);
	  pq = objcache_get(isc->pdu_zone, wait /* M_WAITOK or M_NOWAIT*/);
     }
     else {
	  TAILQ_REMOVE(&isc->freepdu, pq, pq_link);
	  lockmgr(&isc->pdu_lock, LK_RELEASE);
     }

     if(pq == NULL) {
	  debug(7, "out of mem");
	  return NULL;
     }
#ifdef ISCSI_INITIATOR_DEBUG
     lockmgr(&isc->pdu_lock, LK_EXCLUSIVE);
     isc->npdu_alloc++;
     if(isc->npdu_alloc > isc->npdu_max)
	  isc->npdu_max = isc->npdu_alloc;
     lockmgr(&isc->pdu_lock, LK_RELEASE);
#endif
     memset(pq, 0, sizeof(pduq_t));

     return pq;
}

static __inline void
pdu_free(struct isc_softc *isc, pduq_t *pq)
{
     if(pq->mp)
	  m_freem(pq->mp);
#ifdef NO_USE_MBUF
     if(pq->buf != NULL)
	  kfree(pq->buf, M_ISCSI);
#endif
     lockmgr(&isc->pdu_lock, LK_EXCLUSIVE);
     TAILQ_INSERT_TAIL(&isc->freepdu, pq, pq_link);
#ifdef ISCSI_INITIATOR_DEBUG
     isc->npdu_alloc--;
#endif
     lockmgr(&isc->pdu_lock, LK_RELEASE);
}

static __inline void
i_nqueue_rsp(isc_session_t *sp, pduq_t *pq)
{
     iscsi_lock_ex(&sp->rsp_mtx);
     if(++sp->stats.nrsp > sp->stats.max_rsp)
	  sp->stats.max_rsp = sp->stats.nrsp;
     TAILQ_INSERT_TAIL(&sp->rsp, pq, pq_link);
     iscsi_unlock_ex(&sp->rsp_mtx);
}

static __inline pduq_t *
i_dqueue_rsp(isc_session_t *sp)
{
     pduq_t *pq;

     iscsi_lock_ex(&sp->rsp_mtx);
     if((pq = TAILQ_FIRST(&sp->rsp)) != NULL) {
	  sp->stats.nrsp--;
	  TAILQ_REMOVE(&sp->rsp, pq, pq_link);
     }
     iscsi_unlock_ex(&sp->rsp_mtx);

     return pq;
}

static __inline void
i_nqueue_rsv(isc_session_t *sp, pduq_t *pq)
{
     iscsi_lock_ex(&sp->rsv_mtx);
     if(++sp->stats.nrsv > sp->stats.max_rsv)
	  sp->stats.max_rsv = sp->stats.nrsv;
     TAILQ_INSERT_TAIL(&sp->rsv, pq, pq_link);
     iscsi_unlock_ex(&sp->rsv_mtx);
}

static __inline pduq_t *
i_dqueue_rsv(isc_session_t *sp)
{
     pduq_t *pq;

     iscsi_lock_ex(&sp->rsv_mtx);
     if((pq = TAILQ_FIRST(&sp->rsv)) != NULL) {
	  sp->stats.nrsv--;
	  TAILQ_REMOVE(&sp->rsv, pq, pq_link);
     }
     iscsi_unlock_ex(&sp->rsv_mtx);

     return pq;
}

static __inline void
i_nqueue_csnd(isc_session_t *sp, pduq_t *pq)
{
     iscsi_lock_ex(&sp->snd_mtx);
     if(++sp->stats.ncsnd > sp->stats.max_csnd)
	  sp->stats.max_csnd = sp->stats.ncsnd;
     TAILQ_INSERT_TAIL(&sp->csnd, pq, pq_link);
     iscsi_unlock_ex(&sp->snd_mtx);
}

static __inline pduq_t *
i_dqueue_csnd(isc_session_t *sp)
{
     pduq_t *pq;

     iscsi_lock_ex(&sp->snd_mtx);
     if((pq = TAILQ_FIRST(&sp->csnd)) != NULL) {
	  sp->stats.ncsnd--;
	  TAILQ_REMOVE(&sp->csnd, pq, pq_link);
     }
     iscsi_unlock_ex(&sp->snd_mtx);

     return pq;
}

static __inline void
i_nqueue_isnd(isc_session_t *sp, pduq_t *pq)
{
     iscsi_lock_ex(&sp->snd_mtx);
     if(++sp->stats.nisnd > sp->stats.max_isnd)
	  sp->stats.max_isnd = sp->stats.nisnd;
     TAILQ_INSERT_TAIL(&sp->isnd, pq, pq_link);
     iscsi_unlock_ex(&sp->snd_mtx);
}

static __inline pduq_t *
i_dqueue_isnd(isc_session_t *sp)
{
     pduq_t *pq;

     iscsi_lock_ex(&sp->snd_mtx);
     if((pq = TAILQ_FIRST(&sp->isnd)) != NULL) {
	  sp->stats.nisnd--;
	  TAILQ_REMOVE(&sp->isnd, pq, pq_link);
     }
     iscsi_unlock_ex(&sp->snd_mtx);

     return pq;
}

static __inline void
i_nqueue_wsnd(isc_session_t *sp, pduq_t *pq)
{
     iscsi_lock_ex(&sp->snd_mtx);
     if(++sp->stats.nwsnd > sp->stats.max_wsnd)
	  sp->stats.max_wsnd = sp->stats.nwsnd;
     TAILQ_INSERT_TAIL(&sp->wsnd, pq, pq_link);
     iscsi_unlock_ex(&sp->snd_mtx);
}

static __inline pduq_t *
i_dqueue_wsnd(isc_session_t *sp)
{
     pduq_t *pq;

     iscsi_lock_ex(&sp->snd_mtx);
     if((pq = TAILQ_FIRST(&sp->wsnd)) != NULL) {
	  sp->stats.nwsnd--;
	  TAILQ_REMOVE(&sp->wsnd, pq, pq_link);
     }
     iscsi_unlock_ex(&sp->snd_mtx);

     return pq;
}

static __inline pduq_t *
i_dqueue_snd(isc_session_t *sp, int which)
{
     pduq_t *pq;

     pq = NULL;
     iscsi_lock_ex(&sp->snd_mtx);
     if((which & BIT(0)) && (pq = TAILQ_FIRST(&sp->isnd)) != NULL) {
	  sp->stats.nisnd--;
	  TAILQ_REMOVE(&sp->isnd, pq, pq_link);
	  pq->pduq = &sp->isnd;	// remember where you came from
     } else
     if((which & BIT(1)) && (pq = TAILQ_FIRST(&sp->wsnd)) != NULL) {
	  sp->stats.nwsnd--;
	  TAILQ_REMOVE(&sp->wsnd, pq, pq_link);
	  pq->pduq = &sp->wsnd;	// remember where you came from
     } else
     if((which & BIT(2)) && (pq = TAILQ_FIRST(&sp->csnd)) != NULL) {
	  sp->stats.ncsnd--;
	  TAILQ_REMOVE(&sp->csnd, pq, pq_link);
	  pq->pduq = &sp->csnd;	// remember where you came from
     }
     iscsi_unlock_ex(&sp->snd_mtx);

     return pq;
}

static __inline void
i_rqueue_pdu(isc_session_t *sp, pduq_t *pq)
{
     iscsi_lock_ex(&sp->snd_mtx);
     KASSERT(pq->pduq != NULL, ("pq->pduq is NULL"));
     TAILQ_INSERT_TAIL(pq->pduq, pq, pq_link);
     iscsi_unlock_ex(&sp->snd_mtx);
}

/*
 | Waiting for ACK (or something :-)
 */
static __inline void
i_nqueue_hld(isc_session_t *sp, pduq_t *pq)
{
     getmicrouptime(&pq->ts);
     iscsi_lock_ex(&sp->hld_mtx);
     if(++sp->stats.nhld > sp->stats.max_hld)
	  sp->stats.max_hld = sp->stats.nhld;
     TAILQ_INSERT_TAIL(&sp->hld, pq, pq_link);
     iscsi_unlock_ex(&sp->hld_mtx);
     return;
}

static __inline void
i_remove_hld(isc_session_t *sp, pduq_t *pq)
{
     iscsi_lock_ex(&sp->hld_mtx);
     sp->stats.nhld--;
     TAILQ_REMOVE(&sp->hld, pq, pq_link);
     iscsi_unlock_ex(&sp->hld_mtx);
}

static __inline pduq_t *
i_dqueue_hld(isc_session_t *sp)
{
     pduq_t *pq;

     iscsi_lock_ex(&sp->hld_mtx);
     if((pq = TAILQ_FIRST(&sp->hld)) != NULL) {
	  sp->stats.nhld--;
	  TAILQ_REMOVE(&sp->hld, pq, pq_link);
     }
     iscsi_unlock_ex(&sp->hld_mtx);

     return pq;
}

static __inline pduq_t *
i_search_hld(isc_session_t *sp, int itt, int keep)
{
     pduq_t	*pq, *tmp;

     pq = NULL;

     iscsi_lock_ex(&sp->hld_mtx);
     TAILQ_FOREACH_MUTABLE(pq, &sp->hld, pq_link, tmp) {
	  if(pq->pdu.ipdu.bhs.itt == itt) {
	       if(!keep) {
		    sp->stats.nhld--;
		    TAILQ_REMOVE(&sp->hld, pq, pq_link);
	       }
	       break;
	  }
     }
     iscsi_unlock_ex(&sp->hld_mtx);

     return pq;
}

static __inline void
i_mbufcopy(struct mbuf *mp, caddr_t dp, int len)
{
     struct mbuf *m;
     caddr_t bp;

     for(m = mp; m != NULL; m = m->m_next) {
	  bp = mtod(m, caddr_t);
	  /*
	   | the pdu is word (4 octed) aligned
	   | so len <= packet
	   */
	  memcpy(dp, bp, MIN(len, m->m_len));
	  dp += m->m_len;
	  len -= m->m_len;
	  if(len <= 0)
	       break;
     }
}
