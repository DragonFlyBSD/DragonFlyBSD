/*-
 * Copyright (c) 1999,2000,2001 Jonathan Lemon <jlemon@FreeBSD.org>
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
 * $FreeBSD: src/sys/kern/kern_event.c,v 1.2.2.10 2004/04/04 07:03:14 cperciva Exp $
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/proc.h>
#include <sys/malloc.h> 
#include <sys/unistd.h>
#include <sys/file.h>
#include <sys/lock.h>
#include <sys/fcntl.h>
#include <sys/queue.h>
#include <sys/event.h>
#include <sys/eventvar.h>
#include <sys/protosw.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <sys/sysproto.h>
#include <sys/thread.h>
#include <sys/uio.h>
#include <sys/signalvar.h>
#include <sys/filio.h>
#include <sys/ktr.h>

#include <sys/thread2.h>
#include <sys/file2.h>
#include <sys/mplock2.h>

/*
 * Global token for kqueue subsystem
 */
#if 0
struct lwkt_token kq_token = LWKT_TOKEN_INITIALIZER(kq_token);
SYSCTL_LONG(_lwkt, OID_AUTO, kq_collisions,
    CTLFLAG_RW, &kq_token.t_collisions, 0,
    "Collision counter of kq_token");
#endif

MALLOC_DEFINE(M_KQUEUE, "kqueue", "memory for kqueue system");

struct kevent_copyin_args {
	struct kevent_args	*ka;
	int			pchanges;
};

static int	kqueue_sleep(struct kqueue *kq, struct timespec *tsp);
static int	kqueue_scan(struct kqueue *kq, struct kevent *kevp, int count,
		    struct knote *marker);
static int 	kqueue_read(struct file *fp, struct uio *uio,
		    struct ucred *cred, int flags);
static int	kqueue_write(struct file *fp, struct uio *uio,
		    struct ucred *cred, int flags);
static int	kqueue_ioctl(struct file *fp, u_long com, caddr_t data,
		    struct ucred *cred, struct sysmsg *msg);
static int 	kqueue_kqfilter(struct file *fp, struct knote *kn);
static int 	kqueue_stat(struct file *fp, struct stat *st,
		    struct ucred *cred);
static int 	kqueue_close(struct file *fp);
static void	kqueue_wakeup(struct kqueue *kq);
static int	filter_attach(struct knote *kn);
static int	filter_event(struct knote *kn, long hint);

/*
 * MPSAFE
 */
static struct fileops kqueueops = {
	.fo_read = kqueue_read,
	.fo_write = kqueue_write,
	.fo_ioctl = kqueue_ioctl,
	.fo_kqfilter = kqueue_kqfilter,
	.fo_stat = kqueue_stat,
	.fo_close = kqueue_close,
	.fo_shutdown = nofo_shutdown
};

static void 	knote_attach(struct knote *kn);
static void 	knote_drop(struct knote *kn);
static void	knote_detach_and_drop(struct knote *kn);
static void 	knote_enqueue(struct knote *kn);
static void 	knote_dequeue(struct knote *kn);
static struct 	knote *knote_alloc(void);
static void 	knote_free(struct knote *kn);

static void	filt_kqdetach(struct knote *kn);
static int	filt_kqueue(struct knote *kn, long hint);
static int	filt_procattach(struct knote *kn);
static void	filt_procdetach(struct knote *kn);
static int	filt_proc(struct knote *kn, long hint);
static int	filt_fileattach(struct knote *kn);
static void	filt_timerexpire(void *knx);
static int	filt_timerattach(struct knote *kn);
static void	filt_timerdetach(struct knote *kn);
static int	filt_timer(struct knote *kn, long hint);

static struct filterops file_filtops =
	{ FILTEROP_ISFD, filt_fileattach, NULL, NULL };
static struct filterops kqread_filtops =
	{ FILTEROP_ISFD, NULL, filt_kqdetach, filt_kqueue };
static struct filterops proc_filtops =
	{ 0, filt_procattach, filt_procdetach, filt_proc };
static struct filterops timer_filtops =
	{ 0, filt_timerattach, filt_timerdetach, filt_timer };

static int 		kq_ncallouts = 0;
static int 		kq_calloutmax = (4 * 1024);
SYSCTL_INT(_kern, OID_AUTO, kq_calloutmax, CTLFLAG_RW,
    &kq_calloutmax, 0, "Maximum number of callouts allocated for kqueue");
static int		kq_checkloop = 1000000;
SYSCTL_INT(_kern, OID_AUTO, kq_checkloop, CTLFLAG_RW,
    &kq_checkloop, 0, "Maximum number of callouts allocated for kqueue");

#define KNOTE_ACTIVATE(kn) do { 					\
	kn->kn_status |= KN_ACTIVE;					\
	if ((kn->kn_status & (KN_QUEUED | KN_DISABLED)) == 0)		\
		knote_enqueue(kn);					\
} while(0)

#define	KN_HASHSIZE		64		/* XXX should be tunable */
#define KN_HASH(val, mask)	(((val) ^ (val >> 8)) & (mask))

extern struct filterops aio_filtops;
extern struct filterops sig_filtops;

/*
 * Table for for all system-defined filters.
 */
static struct filterops *sysfilt_ops[] = {
	&file_filtops,			/* EVFILT_READ */
	&file_filtops,			/* EVFILT_WRITE */
	&aio_filtops,			/* EVFILT_AIO */
	&file_filtops,			/* EVFILT_VNODE */
	&proc_filtops,			/* EVFILT_PROC */
	&sig_filtops,			/* EVFILT_SIGNAL */
	&timer_filtops,			/* EVFILT_TIMER */
	&file_filtops,			/* EVFILT_EXCEPT */
};

static int
filt_fileattach(struct knote *kn)
{
	return (fo_kqfilter(kn->kn_fp, kn));
}

/*
 * MPSAFE
 */
static int
kqueue_kqfilter(struct file *fp, struct knote *kn)
{
	struct kqueue *kq = (struct kqueue *)kn->kn_fp->f_data;

	if (kn->kn_filter != EVFILT_READ)
		return (EOPNOTSUPP);

	kn->kn_fop = &kqread_filtops;
	knote_insert(&kq->kq_kqinfo.ki_note, kn);
	return (0);
}

static void
filt_kqdetach(struct knote *kn)
{
	struct kqueue *kq = (struct kqueue *)kn->kn_fp->f_data;

	knote_remove(&kq->kq_kqinfo.ki_note, kn);
}

/*ARGSUSED*/
static int
filt_kqueue(struct knote *kn, long hint)
{
	struct kqueue *kq = (struct kqueue *)kn->kn_fp->f_data;

	kn->kn_data = kq->kq_count;
	return (kn->kn_data > 0);
}

static int
filt_procattach(struct knote *kn)
{
	struct proc *p;
	int immediate;

	immediate = 0;
	p = pfind(kn->kn_id);
	if (p == NULL && (kn->kn_sfflags & NOTE_EXIT)) {
		p = zpfind(kn->kn_id);
		immediate = 1;
	}
	if (p == NULL) {
		return (ESRCH);
	}
	if (!PRISON_CHECK(curthread->td_ucred, p->p_ucred)) {
		if (p)
			PRELE(p);
		return (EACCES);
	}

	lwkt_gettoken(&p->p_token);
	kn->kn_ptr.p_proc = p;
	kn->kn_flags |= EV_CLEAR;		/* automatically set */

	/*
	 * internal flag indicating registration done by kernel
	 */
	if (kn->kn_flags & EV_FLAG1) {
		kn->kn_data = kn->kn_sdata;		/* ppid */
		kn->kn_fflags = NOTE_CHILD;
		kn->kn_flags &= ~EV_FLAG1;
	}

	knote_insert(&p->p_klist, kn);

	/*
	 * Immediately activate any exit notes if the target process is a
	 * zombie.  This is necessary to handle the case where the target
	 * process, e.g. a child, dies before the kevent is negistered.
	 */
	if (immediate && filt_proc(kn, NOTE_EXIT))
		KNOTE_ACTIVATE(kn);
	lwkt_reltoken(&p->p_token);
	PRELE(p);

	return (0);
}

/*
 * The knote may be attached to a different process, which may exit,
 * leaving nothing for the knote to be attached to.  So when the process
 * exits, the knote is marked as DETACHED and also flagged as ONESHOT so
 * it will be deleted when read out.  However, as part of the knote deletion,
 * this routine is called, so a check is needed to avoid actually performing
 * a detach, because the original process does not exist any more.
 */
static void
filt_procdetach(struct knote *kn)
{
	struct proc *p;

	if (kn->kn_status & KN_DETACHED)
		return;
	/* XXX locking? take proc_token here? */
	p = kn->kn_ptr.p_proc;
	knote_remove(&p->p_klist, kn);
}

static int
filt_proc(struct knote *kn, long hint)
{
	u_int event;

	/*
	 * mask off extra data
	 */
	event = (u_int)hint & NOTE_PCTRLMASK;

	/*
	 * if the user is interested in this event, record it.
	 */
	if (kn->kn_sfflags & event)
		kn->kn_fflags |= event;

	/*
	 * Process is gone, so flag the event as finished.  Detach the
	 * knote from the process now because the process will be poof,
	 * gone later on.
	 */
	if (event == NOTE_EXIT) {
		struct proc *p = kn->kn_ptr.p_proc;
		if ((kn->kn_status & KN_DETACHED) == 0) {
			PHOLD(p);
			knote_remove(&p->p_klist, kn);
			kn->kn_status |= KN_DETACHED;
			kn->kn_data = p->p_xstat;
			kn->kn_ptr.p_proc = NULL;
			PRELE(p);
		}
		kn->kn_flags |= (EV_EOF | EV_NODATA | EV_ONESHOT); 
		return (1);
	}

	/*
	 * process forked, and user wants to track the new process,
	 * so attach a new knote to it, and immediately report an
	 * event with the parent's pid.
	 */
	if ((event == NOTE_FORK) && (kn->kn_sfflags & NOTE_TRACK)) {
		struct kevent kev;
		int error;

		/*
		 * register knote with new process.
		 */
		kev.ident = hint & NOTE_PDATAMASK;	/* pid */
		kev.filter = kn->kn_filter;
		kev.flags = kn->kn_flags | EV_ADD | EV_ENABLE | EV_FLAG1;
		kev.fflags = kn->kn_sfflags;
		kev.data = kn->kn_id;			/* parent */
		kev.udata = kn->kn_kevent.udata;	/* preserve udata */
		error = kqueue_register(kn->kn_kq, &kev);
		if (error)
			kn->kn_fflags |= NOTE_TRACKERR;
	}

	return (kn->kn_fflags != 0);
}

/*
 * The callout interlocks with callout_terminate() but can still
 * race a deletion so if KN_DELETING is set we just don't touch
 * the knote.
 */
static void
filt_timerexpire(void *knx)
{
	struct lwkt_token *tok;
	struct knote *kn = knx;
	struct callout *calloutp;
	struct timeval tv;
	int tticks;

	tok = lwkt_token_pool_lookup(kn->kn_kq);
	lwkt_gettoken(tok);
	if ((kn->kn_status & KN_DELETING) == 0) {
		kn->kn_data++;
		KNOTE_ACTIVATE(kn);

		if ((kn->kn_flags & EV_ONESHOT) == 0) {
			tv.tv_sec = kn->kn_sdata / 1000;
			tv.tv_usec = (kn->kn_sdata % 1000) * 1000;
			tticks = tvtohz_high(&tv);
			calloutp = (struct callout *)kn->kn_hook;
			callout_reset(calloutp, tticks, filt_timerexpire, kn);
		}
	}
	lwkt_reltoken(tok);
}

/*
 * data contains amount of time to sleep, in milliseconds
 */ 
static int
filt_timerattach(struct knote *kn)
{
	struct callout *calloutp;
	struct timeval tv;
	int tticks;

	if (kq_ncallouts >= kq_calloutmax) {
		kn->kn_hook = NULL;
		return (ENOMEM);
	}
	kq_ncallouts++;

	tv.tv_sec = kn->kn_sdata / 1000;
	tv.tv_usec = (kn->kn_sdata % 1000) * 1000;
	tticks = tvtohz_high(&tv);

	kn->kn_flags |= EV_CLEAR;		/* automatically set */
	calloutp = kmalloc(sizeof(*calloutp), M_KQUEUE, M_WAITOK);
	callout_init(calloutp);
	kn->kn_hook = (caddr_t)calloutp;
	callout_reset(calloutp, tticks, filt_timerexpire, kn);

	return (0);
}

/*
 * This function is called with the knote flagged locked but it is
 * still possible to race a callout event due to the callback blocking.
 * We must call callout_terminate() instead of callout_stop() to deal
 * with the race.
 */
static void
filt_timerdetach(struct knote *kn)
{
	struct callout *calloutp;

	calloutp = (struct callout *)kn->kn_hook;
	callout_terminate(calloutp);
	kfree(calloutp, M_KQUEUE);
	kq_ncallouts--;
}

static int
filt_timer(struct knote *kn, long hint)
{

	return (kn->kn_data != 0);
}

/*
 * Acquire a knote, return non-zero on success, 0 on failure.
 *
 * If we cannot acquire the knote we sleep and return 0.  The knote
 * may be stale on return in this case and the caller must restart
 * whatever loop they are in.
 *
 * Related kq token must be held.
 */
static __inline
int
knote_acquire(struct knote *kn)
{
	if (kn->kn_status & KN_PROCESSING) {
		kn->kn_status |= KN_WAITING | KN_REPROCESS;
		tsleep(kn, 0, "kqepts", hz);
		/* knote may be stale now */
		return(0);
	}
	kn->kn_status |= KN_PROCESSING;
	return(1);
}

/*
 * Release an acquired knote, clearing KN_PROCESSING and handling any
 * KN_REPROCESS events.
 *
 * Caller must be holding the related kq token
 *
 * Non-zero is returned if the knote is destroyed or detached.
 */
static __inline
int
knote_release(struct knote *kn)
{
	while (kn->kn_status & KN_REPROCESS) {
		kn->kn_status &= ~KN_REPROCESS;
		if (kn->kn_status & KN_WAITING) {
			kn->kn_status &= ~KN_WAITING;
			wakeup(kn);
		}
		if (kn->kn_status & KN_DELETING) {
			knote_detach_and_drop(kn);
			return(1);
			/* NOT REACHED */
		}
		if (filter_event(kn, 0))
			KNOTE_ACTIVATE(kn);
	}
	if (kn->kn_status & KN_DETACHED) {
		kn->kn_status &= ~KN_PROCESSING;
		return(1);
	} else {
		kn->kn_status &= ~KN_PROCESSING;
		return(0);
	}
}

/*
 * Initialize a kqueue.
 *
 * NOTE: The lwp/proc code initializes a kqueue for select/poll ops.
 *
 * MPSAFE
 */
void
kqueue_init(struct kqueue *kq, struct filedesc *fdp)
{
	TAILQ_INIT(&kq->kq_knpend);
	TAILQ_INIT(&kq->kq_knlist);
	kq->kq_count = 0;
	kq->kq_fdp = fdp;
	SLIST_INIT(&kq->kq_kqinfo.ki_note);
}

/*
 * Terminate a kqueue.  Freeing the actual kq itself is left up to the
 * caller (it might be embedded in a lwp so we don't do it here).
 *
 * The kq's knlist must be completely eradicated so block on any
 * processing races.
 */
void
kqueue_terminate(struct kqueue *kq)
{
	struct lwkt_token *tok;
	struct knote *kn;

	tok = lwkt_token_pool_lookup(kq);
	lwkt_gettoken(tok);
	while ((kn = TAILQ_FIRST(&kq->kq_knlist)) != NULL) {
		if (knote_acquire(kn))
			knote_detach_and_drop(kn);
	}
	if (kq->kq_knhash) {
		hashdestroy(kq->kq_knhash, M_KQUEUE, kq->kq_knhashmask);
		kq->kq_knhash = NULL;
		kq->kq_knhashmask = 0;
	}
	lwkt_reltoken(tok);
}

/*
 * MPSAFE
 */
int
sys_kqueue(struct kqueue_args *uap)
{
	struct thread *td = curthread;
	struct kqueue *kq;
	struct file *fp;
	int fd, error;

	error = falloc(td->td_lwp, &fp, &fd);
	if (error)
		return (error);
	fp->f_flag = FREAD | FWRITE;
	fp->f_type = DTYPE_KQUEUE;
	fp->f_ops = &kqueueops;

	kq = kmalloc(sizeof(struct kqueue), M_KQUEUE, M_WAITOK | M_ZERO);
	kqueue_init(kq, td->td_proc->p_fd);
	fp->f_data = kq;

	fsetfd(kq->kq_fdp, fp, fd);
	uap->sysmsg_result = fd;
	fdrop(fp);
	return (error);
}

/*
 * Copy 'count' items into the destination list pointed to by uap->eventlist.
 */
static int
kevent_copyout(void *arg, struct kevent *kevp, int count, int *res)
{
	struct kevent_copyin_args *kap;
	int error;

	kap = (struct kevent_copyin_args *)arg;

	error = copyout(kevp, kap->ka->eventlist, count * sizeof(*kevp));
	if (error == 0) {
		kap->ka->eventlist += count;
		*res += count;
	} else {
		*res = -1;
	}

	return (error);
}

/*
 * Copy at most 'max' items from the list pointed to by kap->changelist,
 * return number of items in 'events'.
 */
static int
kevent_copyin(void *arg, struct kevent *kevp, int max, int *events)
{
	struct kevent_copyin_args *kap;
	int error, count;

	kap = (struct kevent_copyin_args *)arg;

	count = min(kap->ka->nchanges - kap->pchanges, max);
	error = copyin(kap->ka->changelist, kevp, count * sizeof *kevp);
	if (error == 0) {
		kap->ka->changelist += count;
		kap->pchanges += count;
		*events = count;
	}

	return (error);
}

/*
 * MPSAFE
 */
int
kern_kevent(struct kqueue *kq, int nevents, int *res, void *uap,
	    k_copyin_fn kevent_copyinfn, k_copyout_fn kevent_copyoutfn,
	    struct timespec *tsp_in)
{
	struct kevent *kevp;
	struct timespec *tsp;
	int i, n, total, error, nerrors = 0;
	int lres;
	int limit = kq_checkloop;
	struct kevent kev[KQ_NEVENTS];
	struct knote marker;
	struct lwkt_token *tok;

	if (tsp_in == NULL || tsp_in->tv_sec || tsp_in->tv_nsec)
		atomic_set_int(&curthread->td_mpflags, TDF_MP_BATCH_DEMARC);


	tsp = tsp_in;
	*res = 0;

	tok = lwkt_token_pool_lookup(kq);
	lwkt_gettoken(tok);
	for ( ;; ) {
		n = 0;
		error = kevent_copyinfn(uap, kev, KQ_NEVENTS, &n);
		if (error)
			goto done;
		if (n == 0)
			break;
		for (i = 0; i < n; i++) {
			kevp = &kev[i];
			kevp->flags &= ~EV_SYSFLAGS;
			error = kqueue_register(kq, kevp);

			/*
			 * If a registration returns an error we
			 * immediately post the error.  The kevent()
			 * call itself will fail with the error if
			 * no space is available for posting.
			 *
			 * Such errors normally bypass the timeout/blocking
			 * code.  However, if the copyoutfn function refuses
			 * to post the error (see sys_poll()), then we
			 * ignore it too.
			 */
			if (error) {
				kevp->flags = EV_ERROR;
				kevp->data = error;
				lres = *res;
				kevent_copyoutfn(uap, kevp, 1, res);
				if (*res < 0) {
					goto done;
				} else if (lres != *res) {
					nevents--;
					nerrors++;
				}
			}
		}
	}
	if (nerrors) {
		error = 0;
		goto done;
	}

	/*
	 * Acquire/wait for events - setup timeout
	 */
	if (tsp != NULL) {
		struct timespec ats;

		if (tsp->tv_sec || tsp->tv_nsec) {
			getnanouptime(&ats);
			timespecadd(tsp, &ats);		/* tsp = target time */
		}
	}

	/*
	 * Loop as required.
	 *
	 * Collect as many events as we can. Sleeping on successive
	 * loops is disabled if copyoutfn has incremented (*res).
	 *
	 * The loop stops if an error occurs, all events have been
	 * scanned (the marker has been reached), or fewer than the
	 * maximum number of events is found.
	 *
	 * The copyoutfn function does not have to increment (*res) in
	 * order for the loop to continue.
	 *
	 * NOTE: doselect() usually passes 0x7FFFFFFF for nevents.
	 */
	total = 0;
	error = 0;
	marker.kn_filter = EVFILT_MARKER;
	marker.kn_status = KN_PROCESSING;
	TAILQ_INSERT_TAIL(&kq->kq_knpend, &marker, kn_tqe);
	while ((n = nevents - total) > 0) {
		if (n > KQ_NEVENTS)
			n = KQ_NEVENTS;

		/*
		 * If no events are pending sleep until timeout (if any)
		 * or an event occurs.
		 *
		 * After the sleep completes the marker is moved to the
		 * end of the list, making any received events available
		 * to our scan.
		 */
		if (kq->kq_count == 0 && *res == 0) {
			error = kqueue_sleep(kq, tsp);
			if (error)
				break;

			TAILQ_REMOVE(&kq->kq_knpend, &marker, kn_tqe);
			TAILQ_INSERT_TAIL(&kq->kq_knpend, &marker, kn_tqe);
		}

		/*
		 * Process all received events
		 * Account for all non-spurious events in our total
		 */
		i = kqueue_scan(kq, kev, n, &marker);
		if (i) {
			lres = *res;
			error = kevent_copyoutfn(uap, kev, i, res);
			total += *res - lres;
			if (error)
				break;
		}
		if (limit && --limit == 0)
			panic("kqueue: checkloop failed i=%d", i);

		/*
		 * Normally when fewer events are returned than requested
		 * we can stop.  However, if only spurious events were
		 * collected the copyout will not bump (*res) and we have
		 * to continue.
		 */
		if (i < n && *res)
			break;

		/*
		 * Deal with an edge case where spurious events can cause
		 * a loop to occur without moving the marker.  This can
		 * prevent kqueue_scan() from picking up new events which
		 * race us.  We must be sure to move the marker for this
		 * case.
		 *
		 * NOTE: We do not want to move the marker if events
		 *	 were scanned because normal kqueue operations
		 *	 may reactivate events.  Moving the marker in
		 *	 that case could result in duplicates for the
		 *	 same event.
		 */
		if (i == 0) {
			TAILQ_REMOVE(&kq->kq_knpend, &marker, kn_tqe);
			TAILQ_INSERT_TAIL(&kq->kq_knpend, &marker, kn_tqe);
		}
	}
	TAILQ_REMOVE(&kq->kq_knpend, &marker, kn_tqe);

	/* Timeouts do not return EWOULDBLOCK. */
	if (error == EWOULDBLOCK)
		error = 0;

done:
	lwkt_reltoken(tok);
	return (error);
}

/*
 * MPALMOSTSAFE
 */
int
sys_kevent(struct kevent_args *uap)
{
	struct thread *td = curthread;
	struct proc *p = td->td_proc;
	struct timespec ts, *tsp;
	struct kqueue *kq;
	struct file *fp = NULL;
	struct kevent_copyin_args *kap, ka;
	int error;

	if (uap->timeout) {
		error = copyin(uap->timeout, &ts, sizeof(ts));
		if (error)
			return (error);
		tsp = &ts;
	} else {
		tsp = NULL;
	}
	fp = holdfp(p->p_fd, uap->fd, -1);
	if (fp == NULL)
		return (EBADF);
	if (fp->f_type != DTYPE_KQUEUE) {
		fdrop(fp);
		return (EBADF);
	}

	kq = (struct kqueue *)fp->f_data;

	kap = &ka;
	kap->ka = uap;
	kap->pchanges = 0;

	error = kern_kevent(kq, uap->nevents, &uap->sysmsg_result, kap,
			    kevent_copyin, kevent_copyout, tsp);

	fdrop(fp);

	return (error);
}

/*
 * Caller must be holding the kq token
 */
int
kqueue_register(struct kqueue *kq, struct kevent *kev)
{
	struct lwkt_token *tok;
	struct filedesc *fdp = kq->kq_fdp;
	struct filterops *fops;
	struct file *fp = NULL;
	struct knote *kn = NULL;
	int error = 0;

	if (kev->filter < 0) {
		if (kev->filter + EVFILT_SYSCOUNT < 0)
			return (EINVAL);
		fops = sysfilt_ops[~kev->filter];	/* to 0-base index */
	} else {
		/*
		 * XXX
		 * filter attach routine is responsible for insuring that
		 * the identifier can be attached to it.
		 */
		kprintf("unknown filter: %d\n", kev->filter);
		return (EINVAL);
	}

	tok = lwkt_token_pool_lookup(kq);
	lwkt_gettoken(tok);
	if (fops->f_flags & FILTEROP_ISFD) {
		/* validate descriptor */
		fp = holdfp(fdp, kev->ident, -1);
		if (fp == NULL) {
			lwkt_reltoken(tok);
			return (EBADF);
		}
		lwkt_getpooltoken(&fp->f_klist);
again1:
		SLIST_FOREACH(kn, &fp->f_klist, kn_link) {
			if (kn->kn_kq == kq &&
			    kn->kn_filter == kev->filter &&
			    kn->kn_id == kev->ident) {
				if (knote_acquire(kn) == 0)
					goto again1;
				break;
			}
		}
		lwkt_relpooltoken(&fp->f_klist);
	} else {
		if (kq->kq_knhashmask) {
			struct klist *list;
			
			list = &kq->kq_knhash[
			    KN_HASH((u_long)kev->ident, kq->kq_knhashmask)];
			lwkt_getpooltoken(list);
again2:
			SLIST_FOREACH(kn, list, kn_link) {
				if (kn->kn_id == kev->ident &&
				    kn->kn_filter == kev->filter) {
					if (knote_acquire(kn) == 0)
						goto again2;
					break;
				}
			}
			lwkt_relpooltoken(list);
		}
	}

	/*
	 * NOTE: At this point if kn is non-NULL we will have acquired
	 *	 it and set KN_PROCESSING.
	 */
	if (kn == NULL && ((kev->flags & EV_ADD) == 0)) {
		error = ENOENT;
		goto done;
	}

	/*
	 * kn now contains the matching knote, or NULL if no match
	 */
	if (kev->flags & EV_ADD) {
		if (kn == NULL) {
			kn = knote_alloc();
			if (kn == NULL) {
				error = ENOMEM;
				goto done;
			}
			kn->kn_fp = fp;
			kn->kn_kq = kq;
			kn->kn_fop = fops;

			/*
			 * apply reference count to knote structure, and
			 * do not release it at the end of this routine.
			 */
			fp = NULL;

			kn->kn_sfflags = kev->fflags;
			kn->kn_sdata = kev->data;
			kev->fflags = 0;
			kev->data = 0;
			kn->kn_kevent = *kev;

			/*
			 * KN_PROCESSING prevents the knote from getting
			 * ripped out from under us while we are trying
			 * to attach it, in case the attach blocks.
			 */
			kn->kn_status = KN_PROCESSING;
			knote_attach(kn);
			if ((error = filter_attach(kn)) != 0) {
				kn->kn_status |= KN_DELETING | KN_REPROCESS;
				knote_drop(kn);
				goto done;
			}

			/*
			 * Interlock against close races which either tried
			 * to remove our knote while we were blocked or missed
			 * it entirely prior to our attachment.  We do not
			 * want to end up with a knote on a closed descriptor.
			 */
			if ((fops->f_flags & FILTEROP_ISFD) &&
			    checkfdclosed(fdp, kev->ident, kn->kn_fp)) {
				kn->kn_status |= KN_DELETING | KN_REPROCESS;
			}
		} else {
			/*
			 * The user may change some filter values after the
			 * initial EV_ADD, but doing so will not reset any 
			 * filter which have already been triggered.
			 */
			KKASSERT(kn->kn_status & KN_PROCESSING);
			kn->kn_sfflags = kev->fflags;
			kn->kn_sdata = kev->data;
			kn->kn_kevent.udata = kev->udata;
		}

		/*
		 * Execute the filter event to immediately activate the
		 * knote if necessary.  If reprocessing events are pending
		 * due to blocking above we do not run the filter here
		 * but instead let knote_release() do it.  Otherwise we
		 * might run the filter on a deleted event.
		 */
		if ((kn->kn_status & KN_REPROCESS) == 0) {
			if (filter_event(kn, 0))
				KNOTE_ACTIVATE(kn);
		}
	} else if (kev->flags & EV_DELETE) {
		/*
		 * Delete the existing knote
		 */
		knote_detach_and_drop(kn);
		goto done;
	}

	/*
	 * Disablement does not deactivate a knote here.
	 */
	if ((kev->flags & EV_DISABLE) &&
	    ((kn->kn_status & KN_DISABLED) == 0)) {
		kn->kn_status |= KN_DISABLED;
	}

	/*
	 * Re-enablement may have to immediately enqueue an active knote.
	 */
	if ((kev->flags & EV_ENABLE) && (kn->kn_status & KN_DISABLED)) {
		kn->kn_status &= ~KN_DISABLED;
		if ((kn->kn_status & KN_ACTIVE) &&
		    ((kn->kn_status & KN_QUEUED) == 0)) {
			knote_enqueue(kn);
		}
	}

	/*
	 * Handle any required reprocessing
	 */
	knote_release(kn);
	/* kn may be invalid now */

done:
	lwkt_reltoken(tok);
	if (fp != NULL)
		fdrop(fp);
	return (error);
}

/*
 * Block as necessary until the target time is reached.
 * If tsp is NULL we block indefinitely.  If tsp->ts_secs/nsecs are both
 * 0 we do not block at all.
 *
 * Caller must be holding the kq token.
 */
static int
kqueue_sleep(struct kqueue *kq, struct timespec *tsp)
{
	int error = 0;

	if (tsp == NULL) {
		kq->kq_state |= KQ_SLEEP;
		error = tsleep(kq, PCATCH, "kqread", 0);
	} else if (tsp->tv_sec == 0 && tsp->tv_nsec == 0) {
		error = EWOULDBLOCK;
	} else {
		struct timespec ats;
		struct timespec atx = *tsp;
		int timeout;

		getnanouptime(&ats);
		timespecsub(&atx, &ats);
		if (ats.tv_sec < 0) {
			error = EWOULDBLOCK;
		} else {
			timeout = atx.tv_sec > 24 * 60 * 60 ?
				24 * 60 * 60 * hz : tstohz_high(&atx);
			kq->kq_state |= KQ_SLEEP;
			error = tsleep(kq, PCATCH, "kqread", timeout);
		}
	}

	/* don't restart after signals... */
	if (error == ERESTART)
		return (EINTR);

	return (error);
}

/*
 * Scan the kqueue, return the number of active events placed in kevp up
 * to count.
 *
 * Continuous mode events may get recycled, do not continue scanning past
 * marker unless no events have been collected.
 *
 * Caller must be holding the kq token
 */
static int
kqueue_scan(struct kqueue *kq, struct kevent *kevp, int count,
            struct knote *marker)
{
        struct knote *kn, local_marker;
        int total;

        total = 0;
	local_marker.kn_filter = EVFILT_MARKER;
	local_marker.kn_status = KN_PROCESSING;

	/*
	 * Collect events.
	 */
	TAILQ_INSERT_HEAD(&kq->kq_knpend, &local_marker, kn_tqe);
	while (count) {
		kn = TAILQ_NEXT(&local_marker, kn_tqe);
		if (kn->kn_filter == EVFILT_MARKER) {
			/* Marker reached, we are done */
			if (kn == marker)
				break;

			/* Move local marker past some other threads marker */
			kn = TAILQ_NEXT(kn, kn_tqe);
			TAILQ_REMOVE(&kq->kq_knpend, &local_marker, kn_tqe);
			TAILQ_INSERT_BEFORE(kn, &local_marker, kn_tqe);
			continue;
		}

		/*
		 * We can't skip a knote undergoing processing, otherwise
		 * we risk not returning it when the user process expects
		 * it should be returned.  Sleep and retry.
		 */
		if (knote_acquire(kn) == 0)
			continue;

		/*
		 * Remove the event for processing.
		 *
		 * WARNING!  We must leave KN_QUEUED set to prevent the
		 *	     event from being KNOTE_ACTIVATE()d while
		 *	     the queue state is in limbo, in case we
		 *	     block.
		 *
		 * WARNING!  We must set KN_PROCESSING to avoid races
		 *	     against deletion or another thread's
		 *	     processing.
		 */
		TAILQ_REMOVE(&kq->kq_knpend, kn, kn_tqe);
		kq->kq_count--;

		/*
		 * We have to deal with an extremely important race against
		 * file descriptor close()s here.  The file descriptor can
		 * disappear MPSAFE, and there is a small window of
		 * opportunity between that and the call to knote_fdclose().
		 *
		 * If we hit that window here while doselect or dopoll is
		 * trying to delete a spurious event they will not be able
		 * to match up the event against a knote and will go haywire.
		 */
		if ((kn->kn_fop->f_flags & FILTEROP_ISFD) &&
		    checkfdclosed(kq->kq_fdp, kn->kn_kevent.ident, kn->kn_fp)) {
			kn->kn_status |= KN_DELETING | KN_REPROCESS;
		}

		if (kn->kn_status & KN_DISABLED) {
			/*
			 * If disabled we ensure the event is not queued
			 * but leave its active bit set.  On re-enablement
			 * the event may be immediately triggered.
			 */
			kn->kn_status &= ~KN_QUEUED;
		} else if ((kn->kn_flags & EV_ONESHOT) == 0 &&
			   (kn->kn_status & KN_DELETING) == 0 &&
			   filter_event(kn, 0) == 0) {
			/*
			 * If not running in one-shot mode and the event
			 * is no longer present we ensure it is removed
			 * from the queue and ignore it.
			 */
			kn->kn_status &= ~(KN_QUEUED | KN_ACTIVE);
		} else {
			/*
			 * Post the event
			 */
			*kevp++ = kn->kn_kevent;
			++total;
			--count;

			if (kn->kn_flags & EV_ONESHOT) {
				kn->kn_status &= ~KN_QUEUED;
				kn->kn_status |= KN_DELETING | KN_REPROCESS;
			} else if (kn->kn_flags & EV_CLEAR) {
				kn->kn_data = 0;
				kn->kn_fflags = 0;
				kn->kn_status &= ~(KN_QUEUED | KN_ACTIVE);
			} else {
				TAILQ_INSERT_TAIL(&kq->kq_knpend, kn, kn_tqe);
				kq->kq_count++;
			}
		}

		/*
		 * Handle any post-processing states
		 */
		knote_release(kn);
	}
	TAILQ_REMOVE(&kq->kq_knpend, &local_marker, kn_tqe);

	return (total);
}

/*
 * XXX
 * This could be expanded to call kqueue_scan, if desired.
 *
 * MPSAFE
 */
static int
kqueue_read(struct file *fp, struct uio *uio, struct ucred *cred, int flags)
{
	return (ENXIO);
}

/*
 * MPSAFE
 */
static int
kqueue_write(struct file *fp, struct uio *uio, struct ucred *cred, int flags)
{
	return (ENXIO);
}

/*
 * MPALMOSTSAFE
 */
static int
kqueue_ioctl(struct file *fp, u_long com, caddr_t data,
	     struct ucred *cred, struct sysmsg *msg)
{
	struct lwkt_token *tok;
	struct kqueue *kq;
	int error;

	kq = (struct kqueue *)fp->f_data;
	tok = lwkt_token_pool_lookup(kq);
	lwkt_gettoken(tok);

	switch(com) {
	case FIOASYNC:
		if (*(int *)data)
			kq->kq_state |= KQ_ASYNC;
		else
			kq->kq_state &= ~KQ_ASYNC;
		error = 0;
		break;
	case FIOSETOWN:
		error = fsetown(*(int *)data, &kq->kq_sigio);
		break;
	default:
		error = ENOTTY;
		break;
	}
	lwkt_reltoken(tok);
	return (error);
}

/*
 * MPSAFE
 */
static int
kqueue_stat(struct file *fp, struct stat *st, struct ucred *cred)
{
	struct kqueue *kq = (struct kqueue *)fp->f_data;

	bzero((void *)st, sizeof(*st));
	st->st_size = kq->kq_count;
	st->st_blksize = sizeof(struct kevent);
	st->st_mode = S_IFIFO;
	return (0);
}

/*
 * MPSAFE
 */
static int
kqueue_close(struct file *fp)
{
	struct kqueue *kq = (struct kqueue *)fp->f_data;

	kqueue_terminate(kq);

	fp->f_data = NULL;
	funsetown(&kq->kq_sigio);

	kfree(kq, M_KQUEUE);
	return (0);
}

static void
kqueue_wakeup(struct kqueue *kq)
{
	if (kq->kq_state & KQ_SLEEP) {
		kq->kq_state &= ~KQ_SLEEP;
		wakeup(kq);
	}
	KNOTE(&kq->kq_kqinfo.ki_note, 0);
}

/*
 * Calls filterops f_attach function, acquiring mplock if filter is not
 * marked as FILTEROP_MPSAFE.
 *
 * Caller must be holding the related kq token
 */
static int
filter_attach(struct knote *kn)
{
	int ret;

	if (kn->kn_fop->f_flags & FILTEROP_MPSAFE) {
		ret = kn->kn_fop->f_attach(kn);
	} else {
		get_mplock();
		ret = kn->kn_fop->f_attach(kn);
		rel_mplock();
	}
	return (ret);
}

/*
 * Detach the knote and drop it, destroying the knote.
 *
 * Calls filterops f_detach function, acquiring mplock if filter is not
 * marked as FILTEROP_MPSAFE.
 *
 * Caller must be holding the related kq token
 */
static void
knote_detach_and_drop(struct knote *kn)
{
	kn->kn_status |= KN_DELETING | KN_REPROCESS;
	if (kn->kn_fop->f_flags & FILTEROP_MPSAFE) {
		kn->kn_fop->f_detach(kn);
	} else {
		get_mplock();
		kn->kn_fop->f_detach(kn);
		rel_mplock();
	}
	knote_drop(kn);
}

/*
 * Calls filterops f_event function, acquiring mplock if filter is not
 * marked as FILTEROP_MPSAFE.
 *
 * If the knote is in the middle of being created or deleted we cannot
 * safely call the filter op.
 *
 * Caller must be holding the related kq token
 */
static int
filter_event(struct knote *kn, long hint)
{
	int ret;

	if (kn->kn_fop->f_flags & FILTEROP_MPSAFE) {
		ret = kn->kn_fop->f_event(kn, hint);
	} else {
		get_mplock();
		ret = kn->kn_fop->f_event(kn, hint);
		rel_mplock();
	}
	return (ret);
}

/*
 * Walk down a list of knotes, activating them if their event has triggered.
 *
 * If we encounter any knotes which are undergoing processing we just mark
 * them for reprocessing and do not try to [re]activate the knote.  However,
 * if a hint is being passed we have to wait and that makes things a bit
 * sticky.
 */
void
knote(struct klist *list, long hint)
{
	struct kqueue *kq;
	struct knote *kn;
	struct knote *kntmp;

	lwkt_getpooltoken(list);
restart:
	SLIST_FOREACH(kn, list, kn_next) {
		kq = kn->kn_kq;
		lwkt_getpooltoken(kq);

		/* temporary verification hack */
		SLIST_FOREACH(kntmp, list, kn_next) {
			if (kn == kntmp)
				break;
		}
		if (kn != kntmp || kn->kn_kq != kq) {
			lwkt_relpooltoken(kq);
			goto restart;
		}

		if (kn->kn_status & KN_PROCESSING) {
			/*
			 * Someone else is processing the knote, ask the
			 * other thread to reprocess it and don't mess
			 * with it otherwise.
			 */
			if (hint == 0) {
				kn->kn_status |= KN_REPROCESS;
				lwkt_relpooltoken(kq);
				continue;
			}

			/*
			 * If the hint is non-zero we have to wait or risk
			 * losing the state the caller is trying to update.
			 *
			 * XXX This is a real problem, certain process
			 *     and signal filters will bump kn_data for
			 *     already-processed notes more than once if
			 *     we restart the list scan.  FIXME.
			 */
			kn->kn_status |= KN_WAITING | KN_REPROCESS;
			tsleep(kn, 0, "knotec", hz);
			lwkt_relpooltoken(kq);
			goto restart;
		}

		/*
		 * Become the reprocessing master ourselves.
		 *
		 * If hint is non-zer running the event is mandatory
		 * when not deleting so do it whether reprocessing is
		 * set or not.
		 */
		kn->kn_status |= KN_PROCESSING;
		if ((kn->kn_status & KN_DELETING) == 0) {
			if (filter_event(kn, hint))
				KNOTE_ACTIVATE(kn);
		}
		if (knote_release(kn)) {
			lwkt_relpooltoken(kq);
			goto restart;
		}
		lwkt_relpooltoken(kq);
	}
	lwkt_relpooltoken(list);
}

/*
 * Insert knote at head of klist.
 *
 * This function may only be called via a filter function and thus
 * kq_token should already be held and marked for processing.
 */
void
knote_insert(struct klist *klist, struct knote *kn)
{
	lwkt_getpooltoken(klist);
	KKASSERT(kn->kn_status & KN_PROCESSING);
	SLIST_INSERT_HEAD(klist, kn, kn_next);
	lwkt_relpooltoken(klist);
}

/*
 * Remove knote from a klist
 *
 * This function may only be called via a filter function and thus
 * kq_token should already be held and marked for processing.
 */
void
knote_remove(struct klist *klist, struct knote *kn)
{
	lwkt_getpooltoken(klist);
	KKASSERT(kn->kn_status & KN_PROCESSING);
	SLIST_REMOVE(klist, kn, knote, kn_next);
	lwkt_relpooltoken(klist);
}

#if 0
/*
 * Remove all knotes from a specified klist
 *
 * Only called from aio.
 */
void
knote_empty(struct klist *list)
{
	struct knote *kn;

	lwkt_gettoken(&kq_token);
	while ((kn = SLIST_FIRST(list)) != NULL) {
		if (knote_acquire(kn))
			knote_detach_and_drop(kn);
	}
	lwkt_reltoken(&kq_token);
}
#endif

void
knote_assume_knotes(struct kqinfo *src, struct kqinfo *dst,
		    struct filterops *ops, void *hook)
{
	struct kqueue *kq;
	struct knote *kn;

	lwkt_getpooltoken(&src->ki_note);
	lwkt_getpooltoken(&dst->ki_note);
	while ((kn = SLIST_FIRST(&src->ki_note)) != NULL) {
		kq = kn->kn_kq;
		lwkt_getpooltoken(kq);
		if (SLIST_FIRST(&src->ki_note) != kn || kn->kn_kq != kq) {
			lwkt_relpooltoken(kq);
			continue;
		}
		if (knote_acquire(kn)) {
			knote_remove(&src->ki_note, kn);
			kn->kn_fop = ops;
			kn->kn_hook = hook;
			knote_insert(&dst->ki_note, kn);
			knote_release(kn);
			/* kn may be invalid now */
		}
		lwkt_relpooltoken(kq);
	}
	lwkt_relpooltoken(&dst->ki_note);
	lwkt_relpooltoken(&src->ki_note);
}

/*
 * Remove all knotes referencing a specified fd
 */
void
knote_fdclose(struct file *fp, struct filedesc *fdp, int fd)
{
	struct kqueue *kq;
	struct knote *kn;
	struct knote *kntmp;

	lwkt_getpooltoken(&fp->f_klist);
restart:
	SLIST_FOREACH(kn, &fp->f_klist, kn_link) {
		if (kn->kn_kq->kq_fdp == fdp && kn->kn_id == fd) {
			kq = kn->kn_kq;
			lwkt_getpooltoken(kq);

			/* temporary verification hack */
			SLIST_FOREACH(kntmp, &fp->f_klist, kn_link) {
				if (kn == kntmp)
					break;
			}
			if (kn != kntmp || kn->kn_kq->kq_fdp != fdp ||
			    kn->kn_id != fd || kn->kn_kq != kq) {
				lwkt_relpooltoken(kq);
				goto restart;
			}
			if (knote_acquire(kn))
				knote_detach_and_drop(kn);
			lwkt_relpooltoken(kq);
			goto restart;
		}
	}
	lwkt_relpooltoken(&fp->f_klist);
}

/*
 * Low level attach function.
 *
 * The knote should already be marked for processing.
 * Caller must hold the related kq token.
 */
static void
knote_attach(struct knote *kn)
{
	struct klist *list;
	struct kqueue *kq = kn->kn_kq;

	if (kn->kn_fop->f_flags & FILTEROP_ISFD) {
		KKASSERT(kn->kn_fp);
		list = &kn->kn_fp->f_klist;
	} else {
		if (kq->kq_knhashmask == 0)
			kq->kq_knhash = hashinit(KN_HASHSIZE, M_KQUEUE,
						 &kq->kq_knhashmask);
		list = &kq->kq_knhash[KN_HASH(kn->kn_id, kq->kq_knhashmask)];
	}
	lwkt_getpooltoken(list);
	SLIST_INSERT_HEAD(list, kn, kn_link);
	TAILQ_INSERT_HEAD(&kq->kq_knlist, kn, kn_kqlink);
	lwkt_relpooltoken(list);
}

/*
 * Low level drop function.
 *
 * The knote should already be marked for processing.
 * Caller must hold the related kq token.
 */
static void
knote_drop(struct knote *kn)
{
	struct kqueue *kq;
	struct klist *list;

	kq = kn->kn_kq;

	if (kn->kn_fop->f_flags & FILTEROP_ISFD)
		list = &kn->kn_fp->f_klist;
	else
		list = &kq->kq_knhash[KN_HASH(kn->kn_id, kq->kq_knhashmask)];

	lwkt_getpooltoken(list);
	SLIST_REMOVE(list, kn, knote, kn_link);
	TAILQ_REMOVE(&kq->kq_knlist, kn, kn_kqlink);
	if (kn->kn_status & KN_QUEUED)
		knote_dequeue(kn);
	if (kn->kn_fop->f_flags & FILTEROP_ISFD) {
		fdrop(kn->kn_fp);
		kn->kn_fp = NULL;
	}
	knote_free(kn);
	lwkt_relpooltoken(list);
}

/*
 * Low level enqueue function.
 *
 * The knote should already be marked for processing.
 * Caller must be holding the kq token
 */
static void
knote_enqueue(struct knote *kn)
{
	struct kqueue *kq = kn->kn_kq;

	KASSERT((kn->kn_status & KN_QUEUED) == 0, ("knote already queued"));
	TAILQ_INSERT_TAIL(&kq->kq_knpend, kn, kn_tqe);
	kn->kn_status |= KN_QUEUED;
	++kq->kq_count;

	/*
	 * Send SIGIO on request (typically set up as a mailbox signal)
	 */
	if (kq->kq_sigio && (kq->kq_state & KQ_ASYNC) && kq->kq_count == 1)
		pgsigio(kq->kq_sigio, SIGIO, 0);

	kqueue_wakeup(kq);
}

/*
 * Low level dequeue function.
 *
 * The knote should already be marked for processing.
 * Caller must be holding the kq token
 */
static void
knote_dequeue(struct knote *kn)
{
	struct kqueue *kq = kn->kn_kq;

	KASSERT(kn->kn_status & KN_QUEUED, ("knote not queued"));
	TAILQ_REMOVE(&kq->kq_knpend, kn, kn_tqe);
	kn->kn_status &= ~KN_QUEUED;
	kq->kq_count--;
}

static struct knote *
knote_alloc(void)
{
	return kmalloc(sizeof(struct knote), M_KQUEUE, M_WAITOK);
}

static void
knote_free(struct knote *kn)
{
	kfree(kn, M_KQUEUE);
}
