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
 * $DragonFly: src/sys/kern/kern_event.c,v 1.33 2007/02/03 17:05:57 corecode Exp $
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
#include <sys/select.h>
#include <sys/queue.h>
#include <sys/event.h>
#include <sys/eventvar.h>
#include <sys/poll.h>
#include <sys/protosw.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <sys/sysproto.h>
#include <sys/uio.h>
#include <sys/signalvar.h>
#include <sys/filio.h>

#include <sys/thread2.h>
#include <sys/file2.h>
#include <sys/mplock2.h>

#include <vm/vm_zone.h>

MALLOC_DEFINE(M_KQUEUE, "kqueue", "memory for kqueue system");

static int	kqueue_scan(struct kqueue *kq, struct kevent *kevp, int count,
		    struct timespec *tsp, int *errorp);
static int 	kqueue_read(struct file *fp, struct uio *uio,
		    struct ucred *cred, int flags);
static int	kqueue_write(struct file *fp, struct uio *uio,
		    struct ucred *cred, int flags);
static int	kqueue_ioctl(struct file *fp, u_long com, caddr_t data,
		    struct ucred *cred, struct sysmsg *msg);
static int 	kqueue_poll(struct file *fp, int events, struct ucred *cred);
static int 	kqueue_kqfilter(struct file *fp, struct knote *kn);
static int 	kqueue_stat(struct file *fp, struct stat *st,
		    struct ucred *cred);
static int 	kqueue_close(struct file *fp);
static void 	kqueue_wakeup(struct kqueue *kq);

/*
 * MPSAFE
 */
static struct fileops kqueueops = {
	.fo_read = kqueue_read,
	.fo_write = kqueue_write,
	.fo_ioctl = kqueue_ioctl,
	.fo_poll = kqueue_poll,
	.fo_kqfilter = kqueue_kqfilter,
	.fo_stat = kqueue_stat,
	.fo_close = kqueue_close,
	.fo_shutdown = nofo_shutdown
};

static void 	knote_attach(struct knote *kn);
static void 	knote_drop(struct knote *kn);
static void 	knote_enqueue(struct knote *kn);
static void 	knote_dequeue(struct knote *kn);
static void 	knote_init(void);
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
	{ 1, filt_fileattach, NULL, NULL };
static struct filterops kqread_filtops =
	{ 1, NULL, filt_kqdetach, filt_kqueue };
static struct filterops proc_filtops =
	{ 0, filt_procattach, filt_procdetach, filt_proc };
static struct filterops timer_filtops =
	{ 0, filt_timerattach, filt_timerdetach, filt_timer };

static vm_zone_t	knote_zone;
static int 		kq_ncallouts = 0;
static int 		kq_calloutmax = (4 * 1024);
SYSCTL_INT(_kern, OID_AUTO, kq_calloutmax, CTLFLAG_RW,
    &kq_calloutmax, 0, "Maximum number of callouts allocated for kqueue");

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
};

static int
filt_fileattach(struct knote *kn)
{
	return (fo_kqfilter(kn->kn_fp, kn));
}

/*
 * MPALMOSTSAFE - acquires mplock
 */
static int
kqueue_kqfilter(struct file *fp, struct knote *kn)
{
	struct kqueue *kq = (struct kqueue *)kn->kn_fp->f_data;

	get_mplock();
	if (kn->kn_filter != EVFILT_READ) {
		rel_mplock();
		return (1);
	}

	kn->kn_fop = &kqread_filtops;
	SLIST_INSERT_HEAD(&kq->kq_sel.si_note, kn, kn_selnext);
	rel_mplock();
	return (0);
}

static void
filt_kqdetach(struct knote *kn)
{
	struct kqueue *kq = (struct kqueue *)kn->kn_fp->f_data;

	SLIST_REMOVE(&kq->kq_sel.si_note, kn, knote, kn_selnext);
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
	lwkt_gettoken(&proc_token);
	p = pfind(kn->kn_id);
	if (p == NULL && (kn->kn_sfflags & NOTE_EXIT)) {
		p = zpfind(kn->kn_id);
		immediate = 1;
	}
	if (p == NULL) {
		lwkt_reltoken(&proc_token);
		return (ESRCH);
	}
	if (!PRISON_CHECK(curthread->td_ucred, p->p_ucred)) {
		lwkt_reltoken(&proc_token);
		return (EACCES);
	}

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

	/* XXX lock the proc here while adding to the list? */
	SLIST_INSERT_HEAD(&p->p_klist, kn, kn_selnext);

	/*
	 * Immediately activate any exit notes if the target process is a
	 * zombie.  This is necessary to handle the case where the target
	 * process, e.g. a child, dies before the kevent is registered.
	 */
	if (immediate && filt_proc(kn, NOTE_EXIT))
		KNOTE_ACTIVATE(kn);
	lwkt_reltoken(&proc_token);

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
	/* XXX locking?  this might modify another process. */
	p = kn->kn_ptr.p_proc;
	SLIST_REMOVE(&p->p_klist, kn, knote, kn_selnext);
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
			SLIST_REMOVE(&p->p_klist, kn, knote, kn_selnext);
			kn->kn_status |= KN_DETACHED;
			kn->kn_data = p->p_xstat;
			kn->kn_ptr.p_proc = NULL;
		}
		kn->kn_flags |= (EV_EOF | EV_ONESHOT); 
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

static void
filt_timerexpire(void *knx)
{
	struct knote *kn = knx;
	struct callout *calloutp;
	struct timeval tv;
	int tticks;

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

/*
 * data contains amount of time to sleep, in milliseconds
 */ 
static int
filt_timerattach(struct knote *kn)
{
	struct callout *calloutp;
	struct timeval tv;
	int tticks;

	if (kq_ncallouts >= kq_calloutmax)
		return (ENOMEM);
	kq_ncallouts++;

	tv.tv_sec = kn->kn_sdata / 1000;
	tv.tv_usec = (kn->kn_sdata % 1000) * 1000;
	tticks = tvtohz_high(&tv);

	kn->kn_flags |= EV_CLEAR;		/* automatically set */
	MALLOC(calloutp, struct callout *, sizeof(*calloutp),
	    M_KQUEUE, M_WAITOK);
	callout_init(calloutp);
	kn->kn_hook = (caddr_t)calloutp;
	callout_reset(calloutp, tticks, filt_timerexpire, kn);

	return (0);
}

static void
filt_timerdetach(struct knote *kn)
{
	struct callout *calloutp;

	calloutp = (struct callout *)kn->kn_hook;
	callout_stop(calloutp);
	FREE(calloutp, M_KQUEUE);
	kq_ncallouts--;
}

static int
filt_timer(struct knote *kn, long hint)
{

	return (kn->kn_data != 0);
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
	kq->kq_fdp = fdp;
}

/*
 * Terminate a kqueue.  Freeing the actual kq itself is left up to the
 * caller (it might be embedded in a lwp so we don't do it here).
 */
void
kqueue_terminate(struct kqueue *kq)
{
	struct knote *kn;
	struct klist *list;
	int hv;

	while ((kn = TAILQ_FIRST(&kq->kq_knlist)) != NULL) {
		kn->kn_fop->f_detach(kn);
		if (kn->kn_fop->f_isfd) {
			list = &kn->kn_fp->f_klist;
			SLIST_REMOVE(list, kn, knote, kn_link);
			fdrop(kn->kn_fp);
			kn->kn_fp = NULL;
		} else {
			hv = KN_HASH(kn->kn_id, kq->kq_knhashmask);
			list = &kq->kq_knhash[hv];
			SLIST_REMOVE(list, kn, knote, kn_link);
		}
		TAILQ_REMOVE(&kq->kq_knlist, kn, kn_kqlink);
		if (kn->kn_status & KN_QUEUED)
			knote_dequeue(kn);
		knote_free(kn);
	}

	if (kq->kq_knhash) {
		kfree(kq->kq_knhash, M_KQUEUE);
		kq->kq_knhash = NULL;
		kq->kq_knhashmask = 0;
	}
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
kevent_copyout(void *arg, struct kevent *kevp, int count)
{
	struct kevent_args *uap;
	int error;

	uap = (struct kevent_args *)arg;

	error = copyout(kevp, uap->eventlist, count * sizeof *kevp);
	if (error == 0)
		uap->eventlist += count;
	return (error);
}

/*
 * Copy 'count' items from the list pointed to by uap->changelist.
 */
static int
kevent_copyin(void *arg, struct kevent *kevp, int count)
{
	struct kevent_args *uap;
	int error;

	uap = (struct kevent_args *)arg;

	error = copyin(uap->changelist, kevp, count * sizeof *kevp);
	if (error == 0)
		uap->changelist += count;
	return (error);
}

/*
 * MPALMOSTSAFE
 */
int
kern_kevent(int fd, int nchanges, int nevents, struct kevent_args *uap,
    k_copyin_fn kevent_copyinfn, k_copyout_fn kevent_copyoutfn,
    struct timespec *tsp_in)
{
	struct thread *td = curthread;
	struct proc *p = td->td_proc;
	struct kevent *kevp;
	struct kqueue *kq;
	struct file *fp = NULL;
	struct timespec ts;
	struct timespec *tsp;
	int i, n, total, nerrors, error;
	struct kevent kev[KQ_NEVENTS];

	tsp = tsp_in;

	fp = holdfp(p->p_fd, fd, -1);
	if (fp == NULL)
		return (EBADF);
	if (fp->f_type != DTYPE_KQUEUE) {
		fdrop(fp);
		return (EBADF);
	}

	kq = (struct kqueue *)fp->f_data;
	nerrors = 0;

	get_mplock();
	while (nchanges > 0) {
		n = nchanges > KQ_NEVENTS ? KQ_NEVENTS : nchanges;
		error = kevent_copyinfn(uap, kev, n);
		if (error)
			goto done;
		for (i = 0; i < n; i++) {
			kevp = &kev[i];
			kevp->flags &= ~EV_SYSFLAGS;
			error = kqueue_register(kq, kevp);
			if (error) {
				if (nevents != 0) {
					kevp->flags = EV_ERROR;
					kevp->data = error;
					kevent_copyoutfn(uap, kevp, 1);
					nevents--;
					nerrors++;
				} else {
					goto done;
				}
			}
		}
		nchanges -= n;
	}
	if (nerrors) {
        	uap->sysmsg_result = nerrors;
		error = 0;
		goto done;
	}

	/*
	 * Acquire/wait for events - setup timeout
	 */
	if (tsp != NULL) {
		struct timespec ats;

		if (tsp->tv_sec || tsp->tv_nsec) {
			nanouptime(&ats);
			timespecadd(tsp, &ats);		/* tsp = target time */
		}
	}

	/*
	 * Loop as required.
	 *
	 * Collect as many events as we can.  The timeout on successive
	 * loops is disabled (kqueue_scan() becomes non-blocking).
	 */
	total = 0;
	error = 0;
	while ((n = nevents - total) > 0) {
		if (n > KQ_NEVENTS)
			n = KQ_NEVENTS;
		i = kqueue_scan(kq, kev, n, tsp, &error);
		if (i == 0)
			break;
		error = kevent_copyoutfn(uap, kev, i);
		total += i;
		if (error || i != n)
			break;
		tsp = &ts;		/* successive loops non-blocking */
		tsp->tv_sec = 0;
		tsp->tv_nsec = 0;
	}
	uap->sysmsg_result = total;
done:
	rel_mplock();
	if (fp != NULL)
		fdrop(fp);
	return (error);
}

/*
 * MPALMOSTSAFE
 */
int
sys_kevent(struct kevent_args *uap)
{
	struct timespec ts, *tsp;
	int error;

	if (uap->timeout) {
		error = copyin(uap->timeout, &ts, sizeof(ts));
		if (error)
			return (error);
		tsp = &ts;
	} else {
		tsp = NULL;
	}

	error = kern_kevent(uap->fd, uap->nchanges, uap->nevents,
	    uap, kevent_copyin, kevent_copyout, tsp);

	return (error);
}

int
kqueue_register(struct kqueue *kq, struct kevent *kev)
{
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

	if (fops->f_isfd) {
		/* validate descriptor */
		fp = holdfp(fdp, kev->ident, -1);
		if (fp == NULL)
			return (EBADF);

		SLIST_FOREACH(kn, &fp->f_klist, kn_link) {
			if (kn->kn_kq == kq &&
			    kn->kn_filter == kev->filter &&
			    kn->kn_id == kev->ident) {
				break;
			}
		}
	} else {
		if (kq->kq_knhashmask) {
			struct klist *list;
			
			list = &kq->kq_knhash[
			    KN_HASH((u_long)kev->ident, kq->kq_knhashmask)];
			SLIST_FOREACH(kn, list, kn_link) {
				if (kn->kn_id == kev->ident &&
				    kn->kn_filter == kev->filter)
					break;
			}
		}
	}

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

			knote_attach(kn);
			if ((error = fops->f_attach(kn)) != 0) {
				knote_drop(kn);
				goto done;
			}
		} else {
			/*
			 * The user may change some filter values after the
			 * initial EV_ADD, but doing so will not reset any 
			 * filter which have already been triggered.
			 */
			kn->kn_sfflags = kev->fflags;
			kn->kn_sdata = kev->data;
			kn->kn_kevent.udata = kev->udata;
		}

		crit_enter();
		if (kn->kn_fop->f_event(kn, 0))
			KNOTE_ACTIVATE(kn);
		crit_exit();
	} else if (kev->flags & EV_DELETE) {
		kn->kn_fop->f_detach(kn);
		knote_drop(kn);
		goto done;
	}

	if ((kev->flags & EV_DISABLE) &&
	    ((kn->kn_status & KN_DISABLED) == 0)) {
		crit_enter();
		kn->kn_status |= KN_DISABLED;
		crit_exit();
	}

	if ((kev->flags & EV_ENABLE) && (kn->kn_status & KN_DISABLED)) {
		crit_enter();
		kn->kn_status &= ~KN_DISABLED;
		if ((kn->kn_status & KN_ACTIVE) &&
		    ((kn->kn_status & KN_QUEUED) == 0))
			knote_enqueue(kn);
		crit_exit();
	}

done:
	if (fp != NULL)
		fdrop(fp);
	return (error);
}

/*
 * Scan the kqueue, blocking if necessary until the target time is reached.
 * If tsp is NULL we block indefinitely.  If tsp->ts_secs/nsecs are both
 * 0 we do not block at all.
 */
static int
kqueue_scan(struct kqueue *kq, struct kevent *kevp, int count,
	    struct timespec *tsp, int *errorp)
{
	struct knote *kn, marker;
	int total;

	total = 0;
again:
	crit_enter();
	if (kq->kq_count == 0) {
		if (tsp == NULL) {
			kq->kq_state |= KQ_SLEEP;
			*errorp = tsleep(kq, PCATCH, "kqread", 0);
		} else if (tsp->tv_sec == 0 && tsp->tv_nsec == 0) {
			*errorp = EWOULDBLOCK;
		} else {
			struct timespec ats;
			struct timespec atx = *tsp;
			int timeout;

			nanouptime(&ats);
			timespecsub(&atx, &ats);
			if (ats.tv_sec < 0) {
				*errorp = EWOULDBLOCK;
			} else {
				timeout = atx.tv_sec > 24 * 60 * 60 ?
					24 * 60 * 60 * hz : tstohz_high(&atx);
				kq->kq_state |= KQ_SLEEP;
				*errorp = tsleep(kq, PCATCH, "kqread", timeout);
			}
		}
		crit_exit();
		if (*errorp == 0)
			goto again;
		/* don't restart after signals... */
		if (*errorp == ERESTART)
			*errorp = EINTR;
		else if (*errorp == EWOULDBLOCK)
			*errorp = 0;
		goto done;
	}

	/*
	 * Collect events.  Continuous mode events may get recycled
	 * past the marker so we stop when we hit it unless no events
	 * have been collected.
	 */
	TAILQ_INSERT_TAIL(&kq->kq_knpend, &marker, kn_tqe);
	while (count) {
		kn = TAILQ_FIRST(&kq->kq_knpend);
		if (kn == &marker)
			break;
		TAILQ_REMOVE(&kq->kq_knpend, kn, kn_tqe);
		if (kn->kn_status & KN_DISABLED) {
			kn->kn_status &= ~KN_QUEUED;
			kq->kq_count--;
			continue;
		}
		if ((kn->kn_flags & EV_ONESHOT) == 0 &&
		    kn->kn_fop->f_event(kn, 0) == 0) {
			kn->kn_status &= ~(KN_QUEUED | KN_ACTIVE);
			kq->kq_count--;
			continue;
		}
		*kevp++ = kn->kn_kevent;
		++total;
		--count;

		/*
		 * Post-event action on the note
		 */
		if (kn->kn_flags & EV_ONESHOT) {
			kn->kn_status &= ~KN_QUEUED;
			kq->kq_count--;
			crit_exit();
			kn->kn_fop->f_detach(kn);
			knote_drop(kn);
			crit_enter();
		} else if (kn->kn_flags & EV_CLEAR) {
			kn->kn_data = 0;
			kn->kn_fflags = 0;
			kn->kn_status &= ~(KN_QUEUED | KN_ACTIVE);
			kq->kq_count--;
		} else {
			TAILQ_INSERT_TAIL(&kq->kq_knpend, kn, kn_tqe);
		}
	}
	TAILQ_REMOVE(&kq->kq_knpend, &marker, kn_tqe);
	crit_exit();
	if (total == 0)
		goto again;
done:
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
	struct kqueue *kq;
	int error;

	get_mplock();
	kq = (struct kqueue *)fp->f_data;

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
	rel_mplock();
	return (error);
}

/*
 * MPALMOSTSAFE - acquires mplock
 */
static int
kqueue_poll(struct file *fp, int events, struct ucred *cred)
{
	struct kqueue *kq = (struct kqueue *)fp->f_data;
	int revents = 0;

	get_mplock();
	crit_enter();
        if (events & (POLLIN | POLLRDNORM)) {
                if (kq->kq_count) {
                        revents |= events & (POLLIN | POLLRDNORM);
		} else {
                        selrecord(curthread, &kq->kq_sel);
			kq->kq_state |= KQ_SEL;
		}
	}
	crit_exit();
	rel_mplock();
	return (revents);
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
 * MPALMOSTSAFE - acquires mplock
 */
static int
kqueue_close(struct file *fp)
{
	struct kqueue *kq = (struct kqueue *)fp->f_data;

	get_mplock();

	kqueue_terminate(kq);

	fp->f_data = NULL;
	funsetown(kq->kq_sigio);
	rel_mplock();

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
	if (kq->kq_state & KQ_SEL) {
		kq->kq_state &= ~KQ_SEL;
		selwakeup(&kq->kq_sel);
	}
	KNOTE(&kq->kq_sel.si_note, 0);
}

/*
 * walk down a list of knotes, activating them if their event has triggered.
 */
void
knote(struct klist *list, long hint)
{
	struct knote *kn;

	SLIST_FOREACH(kn, list, kn_selnext)
		if (kn->kn_fop->f_event(kn, hint))
			KNOTE_ACTIVATE(kn);
}

/*
 * remove all knotes from a specified klist
 */
void
knote_remove(struct klist *list)
{
	struct knote *kn;

	while ((kn = SLIST_FIRST(list)) != NULL) {
		kn->kn_fop->f_detach(kn);
		knote_drop(kn);
	}
}

/*
 * remove all knotes referencing a specified fd
 */
void
knote_fdclose(struct file *fp, struct filedesc *fdp, int fd)
{
	struct knote *kn;

restart:
	SLIST_FOREACH(kn, &fp->f_klist, kn_link) {
		if (kn->kn_kq->kq_fdp == fdp && kn->kn_id == fd) {
			kn->kn_fop->f_detach(kn);
			knote_drop(kn);
			goto restart;
		}
	}
}

static void
knote_attach(struct knote *kn)
{
	struct klist *list;
	struct kqueue *kq = kn->kn_kq;

	if (kn->kn_fop->f_isfd) {
		KKASSERT(kn->kn_fp);
		list = &kn->kn_fp->f_klist;
	} else {
		if (kq->kq_knhashmask == 0)
			kq->kq_knhash = hashinit(KN_HASHSIZE, M_KQUEUE,
						 &kq->kq_knhashmask);
		list = &kq->kq_knhash[KN_HASH(kn->kn_id, kq->kq_knhashmask)];
	}
	SLIST_INSERT_HEAD(list, kn, kn_link);
	TAILQ_INSERT_HEAD(&kq->kq_knlist, kn, kn_kqlink);
	kn->kn_status = 0;
}

/*
 * should be called outside of a critical section, since we don't want to
 * hold a critical section while calling fdrop and free.
 */
static void
knote_drop(struct knote *kn)
{
	struct kqueue *kq;
	struct klist *list;

	kq = kn->kn_kq;

	if (kn->kn_fop->f_isfd)
		list = &kn->kn_fp->f_klist;
	else
		list = &kq->kq_knhash[KN_HASH(kn->kn_id, kq->kq_knhashmask)];

	SLIST_REMOVE(list, kn, knote, kn_link);
	TAILQ_REMOVE(&kq->kq_knlist, kn, kn_kqlink);
	if (kn->kn_status & KN_QUEUED)
		knote_dequeue(kn);
	if (kn->kn_fop->f_isfd)
		fdrop(kn->kn_fp);
	knote_free(kn);
}


static void
knote_enqueue(struct knote *kn)
{
	struct kqueue *kq = kn->kn_kq;

	crit_enter();
	KASSERT((kn->kn_status & KN_QUEUED) == 0, ("knote already queued"));

	TAILQ_INSERT_TAIL(&kq->kq_knpend, kn, kn_tqe);
	kn->kn_status |= KN_QUEUED;
	++kq->kq_count;

	/*
	 * Send SIGIO on request (typically set up as a mailbox signal)
	 */
	if (kq->kq_sigio && (kq->kq_state & KQ_ASYNC) && kq->kq_count == 1)
		pgsigio(kq->kq_sigio, SIGIO, 0);
	crit_exit();
	kqueue_wakeup(kq);
}

static void
knote_dequeue(struct knote *kn)
{
	struct kqueue *kq = kn->kn_kq;

	KASSERT(kn->kn_status & KN_QUEUED, ("knote not queued"));
	crit_enter();

	TAILQ_REMOVE(&kq->kq_knpend, kn, kn_tqe);
	kn->kn_status &= ~KN_QUEUED;
	kq->kq_count--;
	crit_exit();
}

static void
knote_init(void)
{
	knote_zone = zinit("KNOTE", sizeof(struct knote), 0, 0, 1);
}
SYSINIT(knote, SI_SUB_PSEUDO, SI_ORDER_ANY, knote_init, NULL)

static struct knote *
knote_alloc(void)
{
	return ((struct knote *)zalloc(knote_zone));
}

static void
knote_free(struct knote *kn)
{
	zfree(knote_zone, kn);
}
