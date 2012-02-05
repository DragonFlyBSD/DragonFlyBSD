/*	$NetBSD: puffs_msgif.c,v 1.87 2011/07/03 08:57:43 mrg Exp $	*/

/*
 * Copyright (c) 2005, 2006, 2007  Antti Kantee.  All Rights Reserved.
 *
 * Development of this software was supported by the
 * Google Summer of Code program and the Ulla Tuominen Foundation.
 * The Google SoC project was mentored by Bill Studenmund.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/param.h>
#include <sys/kthread.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/objcache.h>
#include <sys/mount.h>
#include <sys/namei.h>
#include <sys/proc.h>
#include <sys/signal2.h>
#include <sys/vnode.h>
#include <machine/inttypes.h>

#include <dev/misc/putter/putter_sys.h>
#include <vfs/puffs/puffs_msgif.h>
#include <vfs/puffs/puffs_sys.h>

/*
 * waitq data structures
 */

/*
 * While a request is going to userspace, park the caller within the
 * kernel.  This is the kernel counterpart of "struct puffs_req".
 */
struct puffs_msgpark {
	struct puffs_req	*park_preq;	/* req followed by buf	*/

	size_t			park_copylen;	/* userspace copylength	*/
	size_t			park_maxlen;	/* max size in comeback */

	struct puffs_req	*park_creq;	/* non-compat preq	*/
	size_t			park_creqlen;	/* non-compat preq len	*/

	parkdone_fn		park_done;	/* "biodone" a'la puffs	*/
	void			*park_donearg;

	int			park_flags;
	int			park_refcount;

	struct cv		park_cv;
	struct lock		park_mtx;

	TAILQ_ENTRY(puffs_msgpark) park_entries;
};
#define PARKFLAG_WAITERGONE	0x01
#define PARKFLAG_DONE		0x02
#define PARKFLAG_ONQUEUE1	0x04
#define PARKFLAG_ONQUEUE2	0x08
#define PARKFLAG_CALL		0x10
#define PARKFLAG_WANTREPLY	0x20
#define	PARKFLAG_HASERROR	0x40

static struct objcache *parkpc;
#ifdef PUFFSDEBUG
static int totalpark;
#endif

static boolean_t
makepark(void *obj, void *privdata, int flags)
{
	struct puffs_msgpark *park = obj;

	lockinit(&park->park_mtx, "puffs park_mtx", 0, 0);
	cv_init(&park->park_cv, "puffsrpl");

	return TRUE;
}

static void
nukepark(void *obj, void *privdata)
{
	struct puffs_msgpark *park = obj;

	cv_destroy(&park->park_cv);
	lockuninit(&park->park_mtx);
}

void
puffs_msgif_init(void)
{

	parkpc = objcache_create_mbacked(M_PUFFS, sizeof(struct puffs_msgpark),
	    NULL, 0, makepark, nukepark, NULL);
}

void
puffs_msgif_destroy(void)
{

	objcache_destroy(parkpc);
}

static struct puffs_msgpark *
puffs_msgpark_alloc(int waitok)
{
	struct puffs_msgpark *park;

	park = objcache_get(parkpc, waitok ? M_WAITOK : M_NOWAIT);
	if (park == NULL)
		return park;

	park->park_refcount = 1;
	park->park_preq = park->park_creq = NULL;
	park->park_flags = PARKFLAG_WANTREPLY;

#ifdef PUFFSDEBUG
	totalpark++;
#endif

	return park;
}

static void
puffs_msgpark_reference(struct puffs_msgpark *park)
{

	KKASSERT(lockstatus(&park->park_mtx, curthread) == LK_EXCLUSIVE);
	park->park_refcount++;
}

/*
 * Release reference to park structure.
 */
static void
puffs_msgpark_release1(struct puffs_msgpark *park, int howmany)
{
	struct puffs_req *preq = park->park_preq;
	struct puffs_req *creq = park->park_creq;
	int refcnt;

	KKASSERT(lockstatus(&park->park_mtx, curthread) == LK_EXCLUSIVE);
	refcnt = park->park_refcount -= howmany;
	lockmgr(&park->park_mtx, LK_RELEASE);

	KKASSERT(refcnt >= 0);

	if (refcnt == 0) {
		if (preq)
			kfree(preq, M_PUFFS);
#if 1
		if (creq)
			kfree(creq, M_PUFFS);
#endif
		objcache_put(parkpc, park);

#ifdef PUFFSDEBUG
		totalpark--;
#endif
	}
}
#define puffs_msgpark_release(a) puffs_msgpark_release1(a, 1)

#ifdef PUFFSDEBUG
static void
parkdump(struct puffs_msgpark *park)
{

	DPRINTF_VERBOSE(("park %p, preq %p, id %" PRIu64 "\n"
	    "\tcopy %zu, max %zu - done: %p/%p\n"
	    "\tflags 0x%08x, refcount %d, cv/mtx: %p/%p\n",
	    park, park->park_preq, park->park_preq->preq_id,
	    park->park_copylen, park->park_maxlen,
	    park->park_done, park->park_donearg,
	    park->park_flags, park->park_refcount,
	    &park->park_cv, &park->park_mtx));
}

static void
parkqdump(struct puffs_wq *q, int dumpall)
{
	struct puffs_msgpark *park;
	int total = 0;

	TAILQ_FOREACH(park, q, park_entries) {
		if (dumpall)
			parkdump(park);
		total++;
	}
	DPRINTF_VERBOSE(("puffs waitqueue at %p dumped, %d total\n", q, total));

}
#endif /* PUFFSDEBUG */

/*
 * A word about locking in the park structures: the lock protects the
 * fields of the *park* structure (not preq) and acts as an interlock
 * in cv operations.  The lock is always internal to this module and
 * callers do not need to worry about it.
 */

int
puffs_msgmem_alloc(size_t len, struct puffs_msgpark **ppark, void **mem,
	int cansleep)
{
	struct puffs_msgpark *park;
	void *m;

	m = kmalloc(len, M_PUFFS, M_ZERO | (cansleep ? M_WAITOK : M_NOWAIT));
	if (m == NULL) {
		KKASSERT(cansleep == 0);
		return ENOMEM;
	}

	park = puffs_msgpark_alloc(cansleep);
	if (park == NULL) {
		KKASSERT(cansleep == 0);
		kfree(m, M_PUFFS);
		return ENOMEM;
	}

	park->park_preq = m;
	park->park_maxlen = park->park_copylen = len;

	*ppark = park;
	*mem = m;

	return 0;
}

void
puffs_msgmem_release(struct puffs_msgpark *park)
{

	if (park == NULL)
		return;

	lockmgr(&park->park_mtx, LK_EXCLUSIVE);
	puffs_msgpark_release(park);
}

void
puffs_msg_setfaf(struct puffs_msgpark *park)
{

	KKASSERT((park->park_flags & PARKFLAG_CALL) == 0);
	park->park_flags &= ~PARKFLAG_WANTREPLY;
}

void
puffs_msg_setdelta(struct puffs_msgpark *park, size_t delta)
{

	KKASSERT(delta < park->park_maxlen); /* "<=" wouldn't make sense */
	park->park_copylen = park->park_maxlen - delta;
}

void
puffs_msg_setinfo(struct puffs_msgpark *park, int class, int type,
	puffs_cookie_t ck)
{

	park->park_preq->preq_opclass = PUFFSOP_OPCLASS(class);
	park->park_preq->preq_optype = type;
	park->park_preq->preq_cookie = ck;
}

void
puffs_msg_setcall(struct puffs_msgpark *park, parkdone_fn donefn, void *donearg)
{

	KKASSERT(park->park_flags & PARKFLAG_WANTREPLY);
	park->park_done = donefn;
	park->park_donearg = donearg;
	park->park_flags |= PARKFLAG_CALL;
}

/*
 * kernel-user-kernel waitqueues
 */

static uint64_t
puffs_getmsgid(struct puffs_mount *pmp)
{
	uint64_t rv;

	lockmgr(&pmp->pmp_lock, LK_EXCLUSIVE);
	rv = pmp->pmp_nextmsgid++;
	lockmgr(&pmp->pmp_lock, LK_RELEASE);

	return rv;
}

/*
 * A word about reference counting of parks.  A reference must be taken
 * when accessing a park and additionally when it is on a queue.  So
 * when taking it off a queue and releasing the access reference, the
 * reference count is generally decremented by 2.
 */

void
puffs_msg_enqueue(struct puffs_mount *pmp, struct puffs_msgpark *park)
{
	struct thread *td = curthread;
	struct mount *mp;
	struct puffs_req *preq;
	sigset_t ss;

	/*
	 * Some clients reuse a park, so reset some flags.  We might
	 * want to provide a caller-side interface for this and add
	 * a few more invariant checks here, but this will do for now.
	 */
	KKASSERT(pmp != NULL && park != NULL);
	park->park_flags &= ~(PARKFLAG_DONE | PARKFLAG_HASERROR);
	KKASSERT((park->park_flags & PARKFLAG_WAITERGONE) == 0);

	mp = PMPTOMP(pmp);
	preq = park->park_preq;

	preq->preq_buflen = park->park_maxlen;
	KKASSERT(preq->preq_id == 0
	    || (preq->preq_opclass & PUFFSOPFLAG_ISRESPONSE));

	if ((park->park_flags & PARKFLAG_WANTREPLY) == 0)
		preq->preq_opclass |= PUFFSOPFLAG_FAF;
	else
		preq->preq_id = puffs_getmsgid(pmp);

	/* fill in caller information */
	if (td->td_proc == NULL || td->td_lwp == NULL) {
		DPRINTF_VERBOSE(("puffs_msg_enqueue: no process\n"));
		preq->preq_pid = 1;
		preq->preq_lid = 0;
		goto noproc;
	}
	preq->preq_pid = td->td_proc->p_pid;
	preq->preq_lid = td->td_lwp->lwp_tid;

	/*
	 * To support cv_sig, yet another movie: check if there are signals
	 * pending and we are issueing a non-FAF.  If so, return an error
	 * directly UNLESS we are issueing INACTIVE/RECLAIM.  In that case,
	 * convert it to a FAF, fire off to the file server and return
	 * an error.  Yes, this is bordering disgusting.  Barfbags are on me.
	 */
	ss = lwp_sigpend(td->td_lwp);
	SIGSETNAND(ss, td->td_lwp->lwp_sigmask);
	if (__predict_false((park->park_flags & PARKFLAG_WANTREPLY)
	   && (park->park_flags & PARKFLAG_CALL) == 0
	   && SIGNOTEMPTY(ss))) {

		/*
		 * see the comment about signals in puffs_msg_wait.
		 */
		if (SIGISMEMBER(ss, SIGINT) ||
		    SIGISMEMBER(ss, SIGTERM) ||
		    SIGISMEMBER(ss, SIGKILL) ||
		    SIGISMEMBER(ss, SIGHUP) ||
		    SIGISMEMBER(ss, SIGQUIT)) {
			park->park_flags |= PARKFLAG_HASERROR;
			preq->preq_rv = EINTR;
			if (PUFFSOP_OPCLASS(preq->preq_opclass) == PUFFSOP_VN
			    && (preq->preq_optype == PUFFS_VN_INACTIVE
			     || preq->preq_optype == PUFFS_VN_RECLAIM)) {
				park->park_preq->preq_opclass |=
				    PUFFSOPFLAG_FAF;
				park->park_flags &= ~PARKFLAG_WANTREPLY;
				DPRINTF_VERBOSE(("puffs_msg_enqueue: "
				    "converted to FAF %p\n", park));
			} else {
				return;
			}
		}
	}

 noproc:
	lockmgr(&pmp->pmp_lock, LK_EXCLUSIVE);
	if (pmp->pmp_status != PUFFSTAT_RUNNING) {
		lockmgr(&pmp->pmp_lock, LK_RELEASE);
		park->park_flags |= PARKFLAG_HASERROR;
		preq->preq_rv = ENXIO;
		return;
	}

#ifdef PUFFSDEBUG
	parkqdump(&pmp->pmp_msg_touser, puffsdebug > 1);
	parkqdump(&pmp->pmp_msg_replywait, puffsdebug > 1);
#endif

	/*
	 * Note: we don't need to lock park since we have the only
	 * reference to it at this point.
	 */
	TAILQ_INSERT_TAIL(&pmp->pmp_msg_touser, park, park_entries);
	park->park_flags |= PARKFLAG_ONQUEUE1;
	pmp->pmp_msg_touser_count++;
	park->park_refcount++;
	lockmgr(&pmp->pmp_lock, LK_RELEASE);

	cv_broadcast(&pmp->pmp_msg_waiter_cv);
	putter_notify(pmp->pmp_pi);

	DPRINTF_VERBOSE(("touser: req %" PRIu64 ", preq: %p, park: %p, "
	    "c/t: 0x%x/0x%x, f: 0x%x\n", preq->preq_id, preq, park,
	    preq->preq_opclass, preq->preq_optype, park->park_flags));
}

int
puffs_msg_wait(struct puffs_mount *pmp, struct puffs_msgpark *park)
{
	struct puffs_req *preq = park->park_preq; /* XXX: hmmm */
#ifdef XXXDF
	struct lwp *l = curthread->td_lwp;
	struct proc *p = curthread->td_proc;
	sigset_t ss;
	sigset_t oss;
#endif
	int error = 0;
	int rv;

	KKASSERT(pmp != NULL && park != NULL);

	/*
	 * block unimportant signals.
	 *
	 * The set of "important" signals here was chosen to be same as
	 * nfs interruptible mount.
	 */
#ifdef XXXDF
	SIGFILLSET(ss);
	SIGDELSET(ss, SIGINT);
	SIGDELSET(ss, SIGTERM);
	SIGDELSET(ss, SIGKILL);
	SIGDELSET(ss, SIGHUP);
	SIGDELSET(ss, SIGQUIT);
	lockmgr(p->p_lock, LK_EXCLUSIVE);
	sigprocmask1(l, SIG_BLOCK, &ss, &oss);
	lockmgr(p->p_lock, LK_RELEASE);
#endif

	lockmgr(&pmp->pmp_lock, LK_EXCLUSIVE);
	puffs_mp_reference(pmp);
	lockmgr(&pmp->pmp_lock, LK_RELEASE);

	lockmgr(&park->park_mtx, LK_EXCLUSIVE);
	/* did the response beat us to the wait? */
	if (__predict_false((park->park_flags & PARKFLAG_DONE)
	    || (park->park_flags & PARKFLAG_HASERROR))) {
		rv = park->park_preq->preq_rv;
		lockmgr(&park->park_mtx, LK_RELEASE);
		goto skipwait;
	}

	if ((park->park_flags & PARKFLAG_WANTREPLY) == 0
	    || (park->park_flags & PARKFLAG_CALL)) {
		lockmgr(&park->park_mtx, LK_RELEASE);
		rv = 0;
		goto skipwait;
	}

	error = cv_wait_sig(&park->park_cv, &park->park_mtx);
	DPRINTF_VERBOSE(("puffs_touser: waiter for %p woke up with %d\n",
	    park, error));
	if (error) {
		park->park_flags |= PARKFLAG_WAITERGONE;
		if (park->park_flags & PARKFLAG_DONE) {
			rv = preq->preq_rv;
			lockmgr(&park->park_mtx, LK_RELEASE);
		} else {
			/*
			 * ok, we marked it as going away, but
			 * still need to do queue ops.  take locks
			 * in correct order.
			 *
			 * We don't want to release our reference
			 * if it's on replywait queue to avoid error
			 * to file server.  putop() code will DTRT.
			 */
			lockmgr(&park->park_mtx, LK_RELEASE);
			lockmgr(&pmp->pmp_lock, LK_EXCLUSIVE);
			lockmgr(&park->park_mtx, LK_EXCLUSIVE);

			/*
			 * Still on queue1?  We can safely remove it
			 * without any consequences since the file
			 * server hasn't seen it.  "else" we need to
			 * wait for the response and just ignore it
			 * to avoid signalling an incorrect error to
			 * the file server.
			 */
			if (park->park_flags & PARKFLAG_ONQUEUE1) {
				TAILQ_REMOVE(&pmp->pmp_msg_touser,
				    park, park_entries);
				puffs_msgpark_release(park);
				pmp->pmp_msg_touser_count--;
				park->park_flags &= ~PARKFLAG_ONQUEUE1;
			} else {
				lockmgr(&park->park_mtx, LK_RELEASE);
			}
			lockmgr(&pmp->pmp_lock, LK_RELEASE);

			rv = EINTR;
		}
	} else {
		rv = preq->preq_rv;
		lockmgr(&park->park_mtx, LK_RELEASE);
	}

 skipwait:
	lockmgr(&pmp->pmp_lock, LK_EXCLUSIVE);
	puffs_mp_release(pmp);
	lockmgr(&pmp->pmp_lock, LK_RELEASE);

#ifdef XXXDF
	lockmgr(p->p_lock, LK_EXCLUSIVE);
	sigprocmask1(l, SIG_SETMASK, &oss, NULL);
	lockmgr(p->p_lock, LK_RELEASE);
#endif

	return rv;
}

/*
 * XXX: this suuuucks.  Hopefully I'll get rid of this lossage once
 * the whole setback-nonsense gets fixed.
 */
int
puffs_msg_wait2(struct puffs_mount *pmp, struct puffs_msgpark *park,
	struct puffs_node *pn1, struct puffs_node *pn2)
{
	struct puffs_req *preq;
	int rv;

	rv = puffs_msg_wait(pmp, park);

	preq = park->park_preq;
	if (pn1 && preq->preq_setbacks & PUFFS_SETBACK_INACT_N1)
		pn1->pn_stat |= PNODE_DOINACT;
	if (pn2 && preq->preq_setbacks & PUFFS_SETBACK_INACT_N2)
		pn2->pn_stat |= PNODE_DOINACT;

	if (pn1 && preq->preq_setbacks & PUFFS_SETBACK_NOREF_N1)
		pn1->pn_stat |= PNODE_NOREFS;
	if (pn2 && preq->preq_setbacks & PUFFS_SETBACK_NOREF_N2)
		pn2->pn_stat |= PNODE_NOREFS;

	return rv;

}

/*
 * XXX: lazy bum.  please, for the love of foie gras, fix me.
 * This should *NOT* depend on setfaf.  Also "memcpy" could
 * be done more nicely.
 */
void
puffs_msg_sendresp(struct puffs_mount *pmp, struct puffs_req *origpreq, int rv)
{
	struct puffs_msgpark *park;
	struct puffs_req *preq;

	puffs_msgmem_alloc(sizeof(struct puffs_req), &park, (void *)&preq, 1);
	puffs_msg_setfaf(park); /* XXXXXX: avoids reqid override */

	memcpy(preq, origpreq, sizeof(struct puffs_req));
	preq->preq_rv = rv;
	preq->preq_opclass |= PUFFSOPFLAG_ISRESPONSE;

	puffs_msg_enqueue(pmp, park);
	puffs_msgmem_release(park);
}

/*
 * Get next request in the outgoing queue.  "maxsize" controls the
 * size the caller can accommodate and "nonblock" signals if this
 * should block while waiting for input.  Handles all locking internally.
 */
int
puffs_msgif_getout(void *this, size_t maxsize, int nonblock,
	uint8_t **data, size_t *dlen, void **parkptr)
{
	struct puffs_mount *pmp = this;
	struct puffs_msgpark *park = NULL;
	struct puffs_req *preq = NULL;
	int error;

	error = 0;
	lockmgr(&pmp->pmp_lock, LK_EXCLUSIVE);
	puffs_mp_reference(pmp);
	for (;;) {
		/* RIP? */
		if (pmp->pmp_status != PUFFSTAT_RUNNING) {
			error = ENXIO;
			break;
		}

		/* need platinum yendorian express card? */
		if (TAILQ_EMPTY(&pmp->pmp_msg_touser)) {
			DPRINTF_VERBOSE(("puffs_getout: no outgoing op, "));
			if (nonblock) {
				DPRINTF_VERBOSE(("returning EWOULDBLOCK\n"));
				error = EWOULDBLOCK;
				break;
			}
			DPRINTF_VERBOSE(("waiting ...\n"));

			error = cv_wait_sig(&pmp->pmp_msg_waiter_cv,
			    &pmp->pmp_lock);
			if (error)
				break;
			else
				continue;
		}

		park = TAILQ_FIRST(&pmp->pmp_msg_touser);
		if (park == NULL)
			continue;

		lockmgr(&park->park_mtx, LK_EXCLUSIVE);
		puffs_msgpark_reference(park);

		DPRINTF_VERBOSE(("puffs_getout: found park at %p, ", park));

		/* If it's a goner, don't process any furher */
		if (park->park_flags & PARKFLAG_WAITERGONE) {
			DPRINTF_VERBOSE(("waitergone!\n"));
			puffs_msgpark_release(park);
			continue;
		}
		preq = park->park_preq;

#if 0
		/* check size */
		/*
		 * XXX: this check is not valid for now, we don't know
		 * the size of the caller's input buffer.  i.e. this
		 * will most likely go away
		 */
		if (maxsize < preq->preq_frhdr.pfr_len) {
			DPRINTF(("buffer too small\n"));
			puffs_msgpark_release(park);
			error = E2BIG;
			break;
		}
#endif

		DPRINTF_VERBOSE(("returning\n"));

		/*
		 * Ok, we found what we came for.  Release it from the
		 * outgoing queue but do not unlock.  We will unlock
		 * only after we "releaseout" it to avoid complications:
		 * otherwise it is (theoretically) possible for userland
		 * to race us into "put" before we have a change to put
		 * this baby on the receiving queue.
		 */
		TAILQ_REMOVE(&pmp->pmp_msg_touser, park, park_entries);
		KKASSERT(park->park_flags & PARKFLAG_ONQUEUE1);
		park->park_flags &= ~PARKFLAG_ONQUEUE1;
		lockmgr(&park->park_mtx, LK_RELEASE);

		pmp->pmp_msg_touser_count--;
		KKASSERT(pmp->pmp_msg_touser_count >= 0);

		break;
	}
	puffs_mp_release(pmp);
	lockmgr(&pmp->pmp_lock, LK_RELEASE);

	if (error == 0) {
		*data = (uint8_t *)preq;
		preq->preq_pth.pth_framelen = park->park_copylen;
		*dlen = preq->preq_pth.pth_framelen;
		*parkptr = park;
	}

	return error;
}

/*
 * Release outgoing structure.  Now, depending on the success of the
 * outgoing send, it is either going onto the result waiting queue
 * or the death chamber.
 */
void
puffs_msgif_releaseout(void *this, void *parkptr, int status)
{
	struct puffs_mount *pmp = this;
	struct puffs_msgpark *park = parkptr;

	DPRINTF_VERBOSE(("puffs_releaseout: returning park %p, errno %d: " ,
	    park, status));
	lockmgr(&pmp->pmp_lock, LK_EXCLUSIVE);
	lockmgr(&park->park_mtx, LK_EXCLUSIVE);
	if (park->park_flags & PARKFLAG_WANTREPLY) {
		if (status == 0) {
			DPRINTF_VERBOSE(("enqueue replywait\n"));
			TAILQ_INSERT_TAIL(&pmp->pmp_msg_replywait, park,
			    park_entries);
			park->park_flags |= PARKFLAG_ONQUEUE2;
		} else {
			DPRINTF_VERBOSE(("error path!\n"));
			park->park_preq->preq_rv = status;
			park->park_flags |= PARKFLAG_DONE;
			cv_signal(&park->park_cv);
		}
		puffs_msgpark_release(park);
	} else {
		DPRINTF_VERBOSE(("release\n"));
		puffs_msgpark_release1(park, 2);
	}
	lockmgr(&pmp->pmp_lock, LK_RELEASE);
}

size_t
puffs_msgif_waitcount(void *this)
{
	struct puffs_mount *pmp = this;
	size_t rv;

	lockmgr(&pmp->pmp_lock, LK_EXCLUSIVE);
	rv = pmp->pmp_msg_touser_count;
	lockmgr(&pmp->pmp_lock, LK_RELEASE);

	return rv;
}

/*
 * XXX: locking with this one?
 */
static void
puffsop_msg(void *this, struct puffs_req *preq)
{
	struct puffs_mount *pmp = this;
	struct putter_hdr *pth = &preq->preq_pth;
	struct puffs_msgpark *park;
	int wgone;

	lockmgr(&pmp->pmp_lock, LK_EXCLUSIVE);

	/* Locate waiter */
	TAILQ_FOREACH(park, &pmp->pmp_msg_replywait, park_entries) {
		if (park->park_preq->preq_id == preq->preq_id)
			break;
	}
	if (park == NULL) {
		DPRINTF_VERBOSE(("puffsop_msg: no request: %" PRIu64 "\n",
		    preq->preq_id));
		lockmgr(&pmp->pmp_lock, LK_RELEASE);
		return; /* XXX send error */
	}

	lockmgr(&park->park_mtx, LK_EXCLUSIVE);
	puffs_msgpark_reference(park);
	if (pth->pth_framelen > park->park_maxlen) {
		DPRINTF_VERBOSE(("puffsop_msg: invalid buffer length: "
		    "%" PRIu64 " (req %" PRIu64 ", \n", pth->pth_framelen,
		    preq->preq_id));
		park->park_preq->preq_rv = EPROTO;
		cv_signal(&park->park_cv);
		puffs_msgpark_release1(park, 2);
		lockmgr(&pmp->pmp_lock, LK_RELEASE);
		return; /* XXX: error */
	}
	wgone = park->park_flags & PARKFLAG_WAITERGONE;

	KKASSERT(park->park_flags & PARKFLAG_ONQUEUE2);
	TAILQ_REMOVE(&pmp->pmp_msg_replywait, park, park_entries);
	park->park_flags &= ~PARKFLAG_ONQUEUE2;
	lockmgr(&pmp->pmp_lock, LK_RELEASE);

	if (wgone) {
		DPRINTF_VERBOSE(("puffsop_msg: bad service - waiter gone for "
		    "park %p\n", park));
	} else {
		memcpy(park->park_preq, preq, pth->pth_framelen);

		if (park->park_flags & PARKFLAG_CALL) {
			DPRINTF_VERBOSE(("puffsop_msg: call for %p, arg %p\n",
			    park->park_preq, park->park_donearg));
			park->park_done(pmp, preq, park->park_donearg);
		}
	}

	if (!wgone) {
		DPRINTF_VERBOSE(("puffs_putop: flagging done for "
		    "park %p\n", park));
		cv_signal(&park->park_cv);
	}

	park->park_flags |= PARKFLAG_DONE;
	puffs_msgpark_release1(park, 2);
}

static void
puffsop_flush(struct puffs_mount *pmp, struct puffs_flush *pf)
{
	struct vnode *vp;
#ifdef XXXDF
	voff_t offlo, offhi;
	int rv, flags = 0;
#endif
	int rv;

	KKASSERT(pf->pf_req.preq_pth.pth_framelen == sizeof(struct puffs_flush));

	/* XXX: slurry */
	if (pf->pf_op == PUFFS_INVAL_NAMECACHE_ALL) {
#ifdef XXXDF
		cache_purgevfs(PMPTOMP(pmp));
		rv = 0;
#endif
		rv = ENOTSUP;
		goto out;
	}

	/*
	 * Get vnode, don't lock it.  Namecache is protected by its own lock
	 * and we have a reference to protect against premature harvesting.
	 *
	 * The node we want here might be locked and the op is in
	 * userspace waiting for us to complete ==> deadlock.  Another
	 * reason we need to eventually bump locking to userspace, as we
	 * will need to lock the node if we wish to do flushes.
	 */
	rv = puffs_cookie2vnode(pmp, pf->pf_cookie, 0, &vp);
	if (rv) {
		if (rv == PUFFS_NOSUCHCOOKIE)
			rv = ENOENT;
		goto out;
	}

	switch (pf->pf_op) {
#if 0
	/* not quite ready, yet */
	case PUFFS_INVAL_NAMECACHE_NODE:
	struct componentname *pf_cn;
	char *name;
		/* get comfortab^Wcomponentname */
		pf_cn = kmem_alloc(componentname);
		memset(pf_cn, 0, sizeof(struct componentname));
		break;

#endif
	case PUFFS_INVAL_NAMECACHE_DIR:
		if (vp->v_type != VDIR) {
			rv = EINVAL;
			break;
		}
		cache_purge(vp);
		break;

#ifdef XXXDF
	case PUFFS_INVAL_PAGECACHE_NODE_RANGE:
		flags = PGO_FREE;
		/*FALLTHROUGH*/
	case PUFFS_FLUSH_PAGECACHE_NODE_RANGE:
		if (flags == 0)
			flags = PGO_CLEANIT;

		if (pf->pf_end > vp->v_size || vp->v_type != VREG) {
			rv = EINVAL;
			break;
		}

		offlo = trunc_page(pf->pf_start);
		offhi = round_page(pf->pf_end);
		if (offhi != 0 && offlo >= offhi) {
			rv = EINVAL;
			break;
		}

		lockmgr(&vp->v_uobj.vmobjlock, LK_EXCLUSIVE);
		rv = VOP_PUTPAGES(vp, offlo, offhi, flags);
		break;
#endif

	default:
		rv = EINVAL;
	}

	vput(vp);

 out:
	puffs_msg_sendresp(pmp, &pf->pf_req, rv);
}

int
puffs_msgif_dispatch(void *this, struct putter_hdr *pth)
{
	struct puffs_mount *pmp = this;
	struct puffs_req *preq = (struct puffs_req *)pth;
	struct puffs_sopreq *psopr;

	if (pth->pth_framelen < sizeof(struct puffs_req)) {
		puffs_msg_sendresp(pmp, preq, EINVAL); /* E2SMALL */
		return 0;
	}

	switch (PUFFSOP_OPCLASS(preq->preq_opclass)) {
	case PUFFSOP_VN:
	case PUFFSOP_VFS:
		DPRINTF_VERBOSE(("dispatch: vn/vfs message 0x%x\n",
		    preq->preq_optype));
		puffsop_msg(pmp, preq);
		break;

	case PUFFSOP_FLUSH: /* process in sop thread */
	{
		struct puffs_flush *pf;

		DPRINTF(("dispatch: flush 0x%x\n", preq->preq_optype));

		if (preq->preq_pth.pth_framelen != sizeof(struct puffs_flush)) {
			puffs_msg_sendresp(pmp, preq, EINVAL); /* E2SMALL */
			break;
		}
		pf = (struct puffs_flush *)preq;

		psopr = kmalloc(sizeof(*psopr), M_PUFFS, M_WAITOK);
		memcpy(&psopr->psopr_pf, pf, sizeof(*pf));
		psopr->psopr_sopreq = PUFFS_SOPREQ_FLUSH;

		lockmgr(&pmp->pmp_sopmtx, LK_EXCLUSIVE);
		if (pmp->pmp_sopthrcount == 0) {
			lockmgr(&pmp->pmp_sopmtx, LK_RELEASE);
			kfree(psopr, M_PUFFS);
			puffs_msg_sendresp(pmp, preq, ENXIO);
		} else {
			TAILQ_INSERT_TAIL(&pmp->pmp_sopreqs,
			    psopr, psopr_entries);
			cv_signal(&pmp->pmp_sopcv);
			lockmgr(&pmp->pmp_sopmtx, LK_RELEASE);
		}
		break;
	}

	case PUFFSOP_UNMOUNT: /* process in sop thread */
	{

		DPRINTF(("dispatch: unmount 0x%x\n", preq->preq_optype));

		psopr = kmalloc(sizeof(*psopr), M_PUFFS, M_WAITOK);
		psopr->psopr_preq = *preq;
		psopr->psopr_sopreq = PUFFS_SOPREQ_UNMOUNT;

		lockmgr(&pmp->pmp_sopmtx, LK_EXCLUSIVE);
		if (pmp->pmp_sopthrcount == 0) {
			lockmgr(&pmp->pmp_sopmtx, LK_RELEASE);
			kfree(psopr, M_PUFFS);
			puffs_msg_sendresp(pmp, preq, ENXIO);
		} else {
			TAILQ_INSERT_TAIL(&pmp->pmp_sopreqs,
			    psopr, psopr_entries);
			cv_signal(&pmp->pmp_sopcv);
			lockmgr(&pmp->pmp_sopmtx, LK_RELEASE);
		}
		break;
	}

	default:
		DPRINTF(("dispatch: invalid class 0x%x\n", preq->preq_opclass));
		puffs_msg_sendresp(pmp, preq, EOPNOTSUPP);
		break;
	}

	return 0;
}

/*
 * Work loop for thread processing all ops from server which
 * cannot safely be handled in caller context.  This includes
 * everything which might need a lock currently "held" by the file
 * server, i.e. a long-term kernel lock which will be released only
 * once the file server acknowledges a request
 */
void
puffs_sop_thread(void *arg)
{
	struct puffs_mount *pmp = arg;
	struct mount *mp = PMPTOMP(pmp);
	struct puffs_sopreq *psopr;
	boolean_t keeprunning;
	boolean_t unmountme = FALSE;

	lockmgr(&pmp->pmp_sopmtx, LK_EXCLUSIVE);
	for (keeprunning = TRUE; keeprunning; ) {
		while ((psopr = TAILQ_FIRST(&pmp->pmp_sopreqs)) == NULL)
			cv_wait(&pmp->pmp_sopcv, &pmp->pmp_sopmtx);
		TAILQ_REMOVE(&pmp->pmp_sopreqs, psopr, psopr_entries);
		lockmgr(&pmp->pmp_sopmtx, LK_RELEASE);

		switch (psopr->psopr_sopreq) {
		case PUFFS_SOPREQSYS_EXIT:
			keeprunning = FALSE;
			break;
		case PUFFS_SOPREQ_FLUSH:
			puffsop_flush(pmp, &psopr->psopr_pf);
			break;
		case PUFFS_SOPREQ_UNMOUNT:
			puffs_msg_sendresp(pmp, &psopr->psopr_preq, 0);

			unmountme = TRUE;
			keeprunning = FALSE;

			break;
		}

		kfree(psopr, M_PUFFS);
		lockmgr(&pmp->pmp_sopmtx, LK_EXCLUSIVE);
	}

	/*
	 * Purge remaining ops.
	 */
	while ((psopr = TAILQ_FIRST(&pmp->pmp_sopreqs)) != NULL) {
		TAILQ_REMOVE(&pmp->pmp_sopreqs, psopr, psopr_entries);
		lockmgr(&pmp->pmp_sopmtx, LK_RELEASE);
		puffs_msg_sendresp(pmp, &psopr->psopr_preq, ENXIO);
		kfree(psopr, M_PUFFS);
		lockmgr(&pmp->pmp_sopmtx, LK_EXCLUSIVE);
	}

	pmp->pmp_sopthrcount--;
	cv_broadcast(&pmp->pmp_sopcv);
	lockmgr(&pmp->pmp_sopmtx, LK_RELEASE); /* not allowed to access fs after this */

	/*
	 * If unmount was requested, we can now safely do it here, since
	 * our context is dead from the point-of-view of puffs_unmount()
	 * and we are just another thread.  dounmount() makes internally
	 * sure that VFS_UNMOUNT() isn't called reentrantly and that it
	 * is eventually completed.
	 */
	if (unmountme) {
		(void)dounmount(mp, MNT_FORCE);
	}

	kthread_exit();
}

int
puffs_msgif_close(void *this)
{
	struct puffs_mount *pmp = this;
	struct mount *mp = PMPTOMP(pmp);

	lockmgr(&pmp->pmp_lock, LK_EXCLUSIVE);
	puffs_mp_reference(pmp);

	/*
	 * Free the waiting callers before proceeding any further.
	 * The syncer might be jogging around in this file system
	 * currently.  If we allow it to go to the userspace of no
	 * return while trying to get the syncer lock, well ...
	 */
	puffs_userdead(pmp);

	/*
	 * Make sure someone from puffs_unmount() isn't currently in
	 * userspace.  If we don't take this precautionary step,
	 * they might notice that the mountpoint has disappeared
	 * from under them once they return.  Especially note that we
	 * cannot simply test for an unmounter before calling
	 * dounmount(), since it might be possible that that particular
	 * invocation of unmount was called without MNT_FORCE.  Here we
	 * *must* make sure unmount succeeds.  Also, restart is necessary
	 * since pmp isn't locked.  We might end up with PUTTER_DEAD after
	 * restart and exit from there.
	 */
	if (pmp->pmp_unmounting) {
		cv_wait(&pmp->pmp_unmounting_cv, &pmp->pmp_lock);
		puffs_mp_release(pmp);
		lockmgr(&pmp->pmp_lock, LK_RELEASE);
		DPRINTF(("puffs_fop_close: unmount was in progress for pmp %p, "
		    "restart\n", pmp));
		return ERESTART;
	}

	/* Won't access pmp from here anymore */
	puffs_mp_release(pmp);
	lockmgr(&pmp->pmp_lock, LK_RELEASE);

	/* Detach from VFS. */
	(void)dounmount(mp, MNT_FORCE);

	return 0;
}

/*
 * We're dead, kaput, RIP, slightly more than merely pining for the
 * fjords, belly-up, fallen, lifeless, finished, expired, gone to meet
 * our maker, ceased to be, etcetc.  YASD.  It's a dead FS!
 *
 * Caller must hold puffs mutex.
 */
void
puffs_userdead(struct puffs_mount *pmp)
{
	struct puffs_msgpark *park, *park_next;

	/*
	 * Mark filesystem status as dying so that operations don't
	 * attempt to march to userspace any longer.
	 */
	pmp->pmp_status = PUFFSTAT_DYING;

	/* signal waiters on REQUEST TO file server queue */
	for (park = TAILQ_FIRST(&pmp->pmp_msg_touser); park; park = park_next) {
		uint8_t opclass;

		lockmgr(&park->park_mtx, LK_EXCLUSIVE);
		puffs_msgpark_reference(park);
		park_next = TAILQ_NEXT(park, park_entries);

		KKASSERT(park->park_flags & PARKFLAG_ONQUEUE1);
		TAILQ_REMOVE(&pmp->pmp_msg_touser, park, park_entries);
		park->park_flags &= ~PARKFLAG_ONQUEUE1;
		pmp->pmp_msg_touser_count--;

		/*
		 * Even though waiters on QUEUE1 are removed in touser()
		 * in case of WAITERGONE, it is still possible for us to
		 * get raced here due to having to retake locks in said
		 * touser().  In the race case simply "ignore" the item
		 * on the queue and move on to the next one.
		 */
		if (park->park_flags & PARKFLAG_WAITERGONE) {
			KKASSERT((park->park_flags & PARKFLAG_CALL) == 0);
			KKASSERT(park->park_flags & PARKFLAG_WANTREPLY);
			puffs_msgpark_release(park);

		} else {
			opclass = park->park_preq->preq_opclass;
			park->park_preq->preq_rv = ENXIO;

			if (park->park_flags & PARKFLAG_CALL) {
				park->park_done(pmp, park->park_preq,
				    park->park_donearg);
				puffs_msgpark_release1(park, 2);
			} else if ((park->park_flags & PARKFLAG_WANTREPLY)==0) {
				puffs_msgpark_release1(park, 2);
			} else {
				park->park_preq->preq_rv = ENXIO;
				cv_signal(&park->park_cv);
				puffs_msgpark_release(park);
			}
		}
	}

	/* signal waiters on RESPONSE FROM file server queue */
	for (park=TAILQ_FIRST(&pmp->pmp_msg_replywait); park; park=park_next) {
		lockmgr(&park->park_mtx, LK_EXCLUSIVE);
		puffs_msgpark_reference(park);
		park_next = TAILQ_NEXT(park, park_entries);

		KKASSERT(park->park_flags & PARKFLAG_ONQUEUE2);
		KKASSERT(park->park_flags & PARKFLAG_WANTREPLY);

		TAILQ_REMOVE(&pmp->pmp_msg_replywait, park, park_entries);
		park->park_flags &= ~PARKFLAG_ONQUEUE2;

		if (park->park_flags & PARKFLAG_WAITERGONE) {
			KKASSERT((park->park_flags & PARKFLAG_CALL) == 0);
			puffs_msgpark_release(park);
		} else {
			park->park_preq->preq_rv = ENXIO;
			if (park->park_flags & PARKFLAG_CALL) {
				park->park_done(pmp, park->park_preq,
				    park->park_donearg);
				puffs_msgpark_release1(park, 2);
			} else {
				cv_signal(&park->park_cv);
				puffs_msgpark_release(park);
			}
		}
	}

	cv_broadcast(&pmp->pmp_msg_waiter_cv);
}
