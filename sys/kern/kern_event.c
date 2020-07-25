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
#include <sys/sysmsg.h>
#include <sys/thread.h>
#include <sys/uio.h>
#include <sys/signalvar.h>
#include <sys/filio.h>
#include <sys/ktr.h>
#include <sys/spinlock.h>

#include <sys/thread2.h>
#include <sys/file2.h>
#include <sys/mplock2.h>
#include <sys/spinlock2.h>

#define EVENT_REGISTER	1
#define EVENT_PROCESS	2

static MALLOC_DEFINE(M_KQUEUE, "kqueue", "memory for kqueue system");

struct kevent_copyin_args {
	const struct kevent_args *ka;
	struct kevent		*eventlist;
	const struct kevent	*changelist;
	int			pchanges;
};

#define KNOTE_CACHE_MAX		64

struct knote_cache_list {
	struct klist		knote_cache;
	int			knote_cache_cnt;
} __cachealign;

static int	kqueue_scan(struct kqueue *kq, struct kevent *kevp, int count,
		    struct knote *marker, int closedcounter, int scan_flags);
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

static void	precise_sleep_intr(systimer_t info, int in_ipi,
				   struct intrframe *frame);
static int	precise_sleep(void *ident, int flags, const char *wmesg,
			      int us);

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
static int	filt_userattach(struct knote *kn);
static void	filt_userdetach(struct knote *kn);
static int	filt_user(struct knote *kn, long hint);
static void	filt_usertouch(struct knote *kn, struct kevent *kev,
				u_long type);
static int	filt_fsattach(struct knote *kn);
static void	filt_fsdetach(struct knote *kn);
static int	filt_fs(struct knote *kn, long hint);

static struct filterops file_filtops =
	{ FILTEROP_ISFD | FILTEROP_MPSAFE, filt_fileattach, NULL, NULL };
static struct filterops kqread_filtops =
	{ FILTEROP_ISFD | FILTEROP_MPSAFE, NULL, filt_kqdetach, filt_kqueue };
static struct filterops proc_filtops =
	{ FILTEROP_MPSAFE, filt_procattach, filt_procdetach, filt_proc };
static struct filterops timer_filtops =
	{ FILTEROP_MPSAFE, filt_timerattach, filt_timerdetach, filt_timer };
static struct filterops user_filtops =
	{ FILTEROP_MPSAFE, filt_userattach, filt_userdetach, filt_user };
static struct filterops fs_filtops =
	{ FILTEROP_MPSAFE, filt_fsattach, filt_fsdetach, filt_fs };

static int 		kq_ncallouts = 0;
static int 		kq_calloutmax = 65536;
SYSCTL_INT(_kern, OID_AUTO, kq_calloutmax, CTLFLAG_RW,
    &kq_calloutmax, 0, "Maximum number of callouts allocated for kqueue");
static int		kq_checkloop = 1000000;
SYSCTL_INT(_kern, OID_AUTO, kq_checkloop, CTLFLAG_RW,
    &kq_checkloop, 0, "Maximum number of loops for kqueue scan");
static int		kq_sleep_threshold = 20000;
SYSCTL_INT(_kern, OID_AUTO, kq_sleep_threshold, CTLFLAG_RW,
    &kq_sleep_threshold, 0, "Minimum sleep duration without busy-looping");

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
	&user_filtops,			/* EVFILT_USER */
	&fs_filtops,			/* EVFILT_FS */
};

static struct knote_cache_list	knote_cache_lists[MAXCPU];

/*
 * Acquire a knote, return non-zero on success, 0 on failure.
 *
 * If we cannot acquire the knote we sleep and return 0.  The knote
 * may be stale on return in this case and the caller must restart
 * whatever loop they are in.
 *
 * Related kq token must be held.
 */
static __inline int
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
static __inline int
knote_release(struct knote *kn)
{
	int ret;

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
	if (kn->kn_status & KN_DETACHED)
		ret = 1;
	else
		ret = 0;
	kn->kn_status &= ~KN_PROCESSING;
	/* kn should not be accessed anymore */
	return ret;
}

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
		int n;

		/*
		 * register knote with new process.
		 */
		kev.ident = hint & NOTE_PDATAMASK;	/* pid */
		kev.filter = kn->kn_filter;
		kev.flags = kn->kn_flags | EV_ADD | EV_ENABLE | EV_FLAG1;
		kev.fflags = kn->kn_sfflags;
		kev.data = kn->kn_id;			/* parent */
		kev.udata = kn->kn_kevent.udata;	/* preserve udata */
		n = 1;
		error = kqueue_register(kn->kn_kq, &kev, &n);
		if (error)
			kn->kn_fflags |= NOTE_TRACKERR;
	}

	return (kn->kn_fflags != 0);
}

static void
filt_timerreset(struct knote *kn)
{
	struct callout *calloutp;
	struct timeval tv;
	int tticks;

	tv.tv_sec = kn->kn_sdata / 1000;
	tv.tv_usec = (kn->kn_sdata % 1000) * 1000;
	tticks = tvtohz_high(&tv);
	calloutp = (struct callout *)kn->kn_hook;
	callout_reset(calloutp, tticks, filt_timerexpire, kn);
}

/*
 * The callout interlocks with callout_stop() but can still
 * race a deletion so if KN_DELETING is set we just don't touch
 * the knote.
 */
static void
filt_timerexpire(void *knx)
{
	struct knote *kn = knx;
	struct kqueue *kq = kn->kn_kq;

	lwkt_getpooltoken(kq);

	/*
	 * Open knote_acquire(), since we can't sleep in callout,
	 * however, we do need to record this expiration.
	 */
	kn->kn_data++;
	if (kn->kn_status & KN_PROCESSING) {
		kn->kn_status |= KN_REPROCESS;
		if ((kn->kn_status & KN_DELETING) == 0 &&
		    (kn->kn_flags & EV_ONESHOT) == 0)
			filt_timerreset(kn);
		lwkt_relpooltoken(kq);
		return;
	}
	KASSERT((kn->kn_status & KN_DELETING) == 0,
	    ("acquire a deleting knote %#x", kn->kn_status));
	kn->kn_status |= KN_PROCESSING;

	KNOTE_ACTIVATE(kn);
	if ((kn->kn_flags & EV_ONESHOT) == 0)
		filt_timerreset(kn);

	knote_release(kn);

	lwkt_relpooltoken(kq);
}

/*
 * data contains amount of time to sleep, in milliseconds
 */
static int
filt_timerattach(struct knote *kn)
{
	struct callout *calloutp;
	int prev_ncallouts;

	prev_ncallouts = atomic_fetchadd_int(&kq_ncallouts, 1);
	if (prev_ncallouts >= kq_calloutmax) {
		atomic_subtract_int(&kq_ncallouts, 1);
		kn->kn_hook = NULL;
		return (ENOMEM);
	}

	kn->kn_flags |= EV_CLEAR;		/* automatically set */
	calloutp = kmalloc(sizeof(*calloutp), M_KQUEUE, M_WAITOK);
	callout_init_mp(calloutp);
	kn->kn_hook = (caddr_t)calloutp;

	filt_timerreset(kn);
	return (0);
}

/*
 * This function is called with the knote flagged locked but it is
 * still possible to race a callout event due to the callback blocking.
 */
static void
filt_timerdetach(struct knote *kn)
{
	struct callout *calloutp;

	calloutp = (struct callout *)kn->kn_hook;
	callout_terminate(calloutp);
	kn->kn_hook = NULL;
	kfree(calloutp, M_KQUEUE);
	atomic_subtract_int(&kq_ncallouts, 1);
}

static int
filt_timer(struct knote *kn, long hint)
{
	return (kn->kn_data != 0);
}

/*
 * EVFILT_USER
 */
static int
filt_userattach(struct knote *kn)
{
	u_int ffctrl;

	kn->kn_hook = NULL;
	if (kn->kn_sfflags & NOTE_TRIGGER)
		kn->kn_ptr.hookid = 1;
	else
		kn->kn_ptr.hookid = 0;

	ffctrl = kn->kn_sfflags & NOTE_FFCTRLMASK;
	kn->kn_sfflags &= NOTE_FFLAGSMASK;
	switch (ffctrl) {
	case NOTE_FFNOP:
		break;

	case NOTE_FFAND:
		kn->kn_fflags &= kn->kn_sfflags;
		break;

	case NOTE_FFOR:
		kn->kn_fflags |= kn->kn_sfflags;
		break;

	case NOTE_FFCOPY:
		kn->kn_fflags = kn->kn_sfflags;
		break;

	default:
		/* XXX Return error? */
		break;
	}
	/* We just happen to copy this value as well. Undocumented. */
	kn->kn_data = kn->kn_sdata;

	return 0;
}

static void
filt_userdetach(struct knote *kn)
{
	/* nothing to do */
}

static int
filt_user(struct knote *kn, long hint)
{
	return (kn->kn_ptr.hookid);
}

static void
filt_usertouch(struct knote *kn, struct kevent *kev, u_long type)
{
	u_int ffctrl;

	switch (type) {
	case EVENT_REGISTER:
		if (kev->fflags & NOTE_TRIGGER)
			kn->kn_ptr.hookid = 1;

		ffctrl = kev->fflags & NOTE_FFCTRLMASK;
		kev->fflags &= NOTE_FFLAGSMASK;
		switch (ffctrl) {
		case NOTE_FFNOP:
			break;

		case NOTE_FFAND:
			kn->kn_fflags &= kev->fflags;
			break;

		case NOTE_FFOR:
			kn->kn_fflags |= kev->fflags;
			break;

		case NOTE_FFCOPY:
			kn->kn_fflags = kev->fflags;
			break;

		default:
			/* XXX Return error? */
			break;
		}
		/* We just happen to copy this value as well. Undocumented. */
		kn->kn_data = kev->data;

		/*
		 * This is not the correct use of EV_CLEAR in an event
		 * modification, it should have been passed as a NOTE instead.
		 * But we need to maintain compatibility with Apple & FreeBSD.
		 *
		 * Note however that EV_CLEAR can still be used when doing
		 * the initial registration of the event and works as expected
		 * (clears the event on reception).
		 */
		if (kev->flags & EV_CLEAR) {
			kn->kn_ptr.hookid = 0;
			/*
			 * Clearing kn->kn_data is fine, since it gets set
			 * every time anyway. We just shouldn't clear
			 * kn->kn_fflags here, since that would limit the
			 * possible uses of this API. NOTE_FFAND or
			 * NOTE_FFCOPY should be used for explicitly clearing
			 * kn->kn_fflags.
			 */
			kn->kn_data = 0;
		}
		break;

        case EVENT_PROCESS:
		*kev = kn->kn_kevent;
		kev->fflags = kn->kn_fflags;
		kev->data = kn->kn_data;
		if (kn->kn_flags & EV_CLEAR) {
			kn->kn_ptr.hookid = 0;
			/* kn_data, kn_fflags handled by parent */
		}
		break;

	default:
		panic("filt_usertouch() - invalid type (%ld)", type);
		break;
	}
}

/*
 * EVFILT_FS
 */
struct klist fs_klist = SLIST_HEAD_INITIALIZER(&fs_klist);

static int
filt_fsattach(struct knote *kn)
{
	kn->kn_flags |= EV_CLEAR;
	knote_insert(&fs_klist, kn);

	return (0);
}

static void
filt_fsdetach(struct knote *kn)
{
	knote_remove(&fs_klist, kn);
}

static int
filt_fs(struct knote *kn, long hint)
{
	kn->kn_fflags |= hint;
	return (kn->kn_fflags != 0);
}

/*
 * Initialize a kqueue.
 *
 * NOTE: The lwp/proc code initializes a kqueue for select/poll ops.
 */
void
kqueue_init(struct kqueue *kq, struct filedesc *fdp)
{
	bzero(kq, sizeof(*kq));
	TAILQ_INIT(&kq->kq_knpend);
	TAILQ_INIT(&kq->kq_knlist);
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
	struct knote *kn;

	lwkt_getpooltoken(kq);
	while ((kn = TAILQ_FIRST(&kq->kq_knlist)) != NULL) {
		if (knote_acquire(kn))
			knote_detach_and_drop(kn);
	}
	lwkt_relpooltoken(kq);

	if (kq->kq_knhash) {
		hashdestroy(kq->kq_knhash, M_KQUEUE, kq->kq_knhashmask);
		kq->kq_knhash = NULL;
		kq->kq_knhashmask = 0;
	}
}

/*
 * MPSAFE
 */
int
sys_kqueue(struct sysmsg *sysmsg, const struct kqueue_args *uap)
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
	sysmsg->sysmsg_result = fd;
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

	error = copyout(kevp, kap->eventlist, count * sizeof(*kevp));
	if (error == 0) {
		kap->eventlist += count;
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
	error = copyin(kap->changelist, kevp, count * sizeof *kevp);
	if (error == 0) {
		kap->changelist += count;
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
	    struct timespec *tsp_in, int flags)
{
	struct kevent *kevp;
	struct timespec *tsp, ats;
	int i, n, total, error, nerrors = 0;
	int gobbled;
	int lres;
	int limit = kq_checkloop;
	int closedcounter;
	int scan_flags;
	struct kevent kev[KQ_NEVENTS];
	struct knote marker;
	struct lwkt_token *tok;

	if (tsp_in == NULL || tsp_in->tv_sec || tsp_in->tv_nsec)
		atomic_set_int(&curthread->td_mpflags, TDF_MP_BATCH_DEMARC);

	tsp = tsp_in;
	*res = 0;

	closedcounter = kq->kq_fdp->fd_closedcounter;

	for (;;) {
		n = 0;
		error = kevent_copyinfn(uap, kev, KQ_NEVENTS, &n);
		if (error)
			return error;
		if (n == 0)
			break;
		for (i = 0; i < n; ++i)
			kev[i].flags &= ~EV_SYSFLAGS;
		for (i = 0; i < n; ++i) {
			gobbled = n - i;
			error = kqueue_register(kq, &kev[i], &gobbled);
			i += gobbled - 1;
			kevp = &kev[i];

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
			if (error || (kevp->flags & EV_RECEIPT)) {
				kevp->flags = EV_ERROR;
				kevp->data = error;
				lres = *res;
				kevent_copyoutfn(uap, kevp, 1, res);
				if (*res < 0) {
					return error;
				} else if (lres != *res) {
					nevents--;
					nerrors++;
				}
			}
		}
	}
	if (nerrors)
		return 0;

	/*
	 * Acquire/wait for events - setup timeout
	 *
	 * If no timeout specified clean up the run path by clearing the
	 * PRECISE flag.
	 */
	if (tsp != NULL) {
		if (tsp->tv_sec || tsp->tv_nsec) {
			getnanouptime(&ats);
			timespecadd(tsp, &ats, tsp);	/* tsp = target time */
		}
	} else {
		flags &= ~KEVENT_TIMEOUT_PRECISE;
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

	tok = lwkt_token_pool_lookup(kq);
	scan_flags = KEVENT_SCAN_INSERT_MARKER;

	while ((n = nevents - total) > 0) {
		if (n > KQ_NEVENTS)
			n = KQ_NEVENTS;

		/*
		 * Process all received events
		 * Account for all non-spurious events in our total
		 */
		i = kqueue_scan(kq, kev, n, &marker, closedcounter, scan_flags);
		scan_flags = KEVENT_SCAN_KEEP_MARKER;
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
		 * If no events were recorded (no events happened or the events
		 * that did happen were all spurious), block until an event
		 * occurs or the timeout occurs and reload the marker.
		 *
		 * If we saturated n (i == n) loop up without sleeping to
		 * continue processing the list.
		 */
		if (i != n && kq->kq_count == 0 && *res == 0) {
			int timeout;
			int ustimeout;

			if (tsp == NULL) {
				timeout = 0;
				ustimeout = 0;
			} else if (tsp->tv_sec == 0 && tsp->tv_nsec == 0) {
				error = EWOULDBLOCK;
				break;
			} else {
				struct timespec atx = *tsp;

				getnanouptime(&ats);
				timespecsub(&atx, &ats, &atx);
				if (atx.tv_sec < 0 ||
				    (atx.tv_sec == 0 && atx.tv_nsec <= 0)) {
					error = EWOULDBLOCK;
					break;
				}
				if (flags & KEVENT_TIMEOUT_PRECISE) {
					if (atx.tv_sec == 0 &&
					    atx.tv_nsec < kq_sleep_threshold) {
						ustimeout = kq_sleep_threshold /
							    1000;
					} else if (atx.tv_sec < 60) {
						ustimeout =
							atx.tv_sec * 1000000 +
							atx.tv_nsec / 1000;
					} else {
						ustimeout = 60 * 1000000;
					}
					if (ustimeout == 0)
						ustimeout = 1;
					timeout = 0;
				} else if (atx.tv_sec > 60 * 60) {
					timeout = 60 * 60 * hz;
					ustimeout = 0;
				} else {
					timeout = tstohz_high(&atx);
					ustimeout = 0;
				}
			}

			lwkt_gettoken(tok);
			if (kq->kq_count == 0) {
				kq->kq_sleep_cnt++;
				if (__predict_false(kq->kq_sleep_cnt == 0)) {
					/*
					 * Guard against possible wrapping.  And
					 * set it to 2, so that kqueue_wakeup()
					 * can wake everyone up.
					 */
					kq->kq_sleep_cnt = 2;
				}
				if (flags & KEVENT_TIMEOUT_PRECISE) {
					error = precise_sleep(kq, PCATCH,
							"kqread", ustimeout);
				} else {
					error = tsleep(kq, PCATCH,
							"kqread", timeout);
				}

				/* don't restart after signals... */
				if (error == ERESTART)
					error = EINTR;
				if (error == EWOULDBLOCK)
					error = 0;
				if (error) {
					lwkt_reltoken(tok);
					break;
				}
				scan_flags = KEVENT_SCAN_RELOAD_MARKER;
			}
			lwkt_reltoken(tok);
		}

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
		if (i == 0)
			scan_flags = KEVENT_SCAN_RELOAD_MARKER;
	}

	/*
	 * Remove the marker
	 */
	if (scan_flags != KEVENT_SCAN_INSERT_MARKER) {
		lwkt_gettoken(tok);
		TAILQ_REMOVE(&kq->kq_knpend, &marker, kn_tqe);
		lwkt_reltoken(tok);
	}

	/* Timeouts do not return EWOULDBLOCK. */
	if (error == EWOULDBLOCK)
		error = 0;
	return error;
}

/*
 * MPALMOSTSAFE
 */
int
sys_kevent(struct sysmsg *sysmsg, const struct kevent_args *uap)
{
	struct thread *td = curthread;
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
	fp = holdfp(td, uap->fd, -1);
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
	kap->eventlist = uap->eventlist;
	kap->changelist = uap->changelist;

	error = kern_kevent(kq, uap->nevents, &sysmsg->sysmsg_result, kap,
			    kevent_copyin, kevent_copyout, tsp, 0);

	dropfp(td, uap->fd, fp);

	return (error);
}

/*
 * Efficiently load multiple file pointers.  This significantly reduces
 * threaded overhead.  When doing simple polling we can depend on the
 * per-thread (fd,fp) cache.  With more descriptors, we batch.
 */
static
void
floadkevfps(thread_t td, struct filedesc *fdp, struct kevent *kev,
	    struct file **fp, int climit)
{
	struct filterops *fops;
	int tdcache;

	if (climit <= 2 && td->td_proc && td->td_proc->p_fd == fdp) {
		tdcache = 1;
	} else {
		tdcache = 0;
		spin_lock_shared(&fdp->fd_spin);
	}

	while (climit) {
		*fp = NULL;
		if (kev->filter < 0 &&
		    kev->filter + EVFILT_SYSCOUNT >= 0) {
			fops = sysfilt_ops[~kev->filter];
			if (fops->f_flags & FILTEROP_ISFD) {
				if (tdcache) {
					*fp = holdfp(td, kev->ident, -1);
				} else {
					*fp = holdfp_fdp_locked(fdp,
								kev->ident, -1);
				}
			}
		}
		--climit;
		++fp;
		++kev;
	}
	if (tdcache == 0)
		spin_unlock_shared(&fdp->fd_spin);
}

/*
 * Register up to *countp kev's.  Always registers at least 1.
 *
 * The number registered is returned in *countp.
 *
 * If an error occurs or a kev is flagged EV_RECEIPT, it is
 * processed and included in *countp, and processing then
 * stops.
 */
int
kqueue_register(struct kqueue *kq, struct kevent *kev, int *countp)
{
	struct filedesc *fdp = kq->kq_fdp;
	struct klist *list = NULL;
	struct filterops *fops;
	struct file *fp[KQ_NEVENTS];
	struct knote *kn = NULL;
	struct thread *td;
	int error;
	int count;
	int climit;
	int closedcounter;
	struct knote_cache_list *cache_list;

	td = curthread;
	climit = *countp;
	if (climit > KQ_NEVENTS)
		climit = KQ_NEVENTS;
	closedcounter = fdp->fd_closedcounter;
	floadkevfps(td, fdp, kev, fp, climit);

	lwkt_getpooltoken(kq);
	count = 0;

	/*
	 * To avoid races, only one thread can register events on this
	 * kqueue at a time.
	 */
	while (__predict_false(kq->kq_regtd != NULL && kq->kq_regtd != td)) {
		kq->kq_state |= KQ_REGWAIT;
		tsleep(&kq->kq_regtd, 0, "kqreg", 0);
	}
	if (__predict_false(kq->kq_regtd != NULL)) {
		/* Recursive calling of kqueue_register() */
		td = NULL;
	} else {
		/* Owner of the kq_regtd, i.e. td != NULL */
		kq->kq_regtd = td;
	}

loop:
	if (kev->filter < 0) {
		if (kev->filter + EVFILT_SYSCOUNT < 0) {
			error = EINVAL;
			++count;
			goto done;
		}
		fops = sysfilt_ops[~kev->filter];	/* to 0-base index */
	} else {
		/*
		 * XXX
		 * filter attach routine is responsible for insuring that
		 * the identifier can be attached to it.
		 */
		error = EINVAL;
		++count;
		goto done;
	}

	if (fops->f_flags & FILTEROP_ISFD) {
		/* validate descriptor */
		if (fp[count] == NULL) {
			error = EBADF;
			++count;
			goto done;
		}
	}

	cache_list = &knote_cache_lists[mycpuid];
	if (SLIST_EMPTY(&cache_list->knote_cache)) {
		struct knote *new_kn;

		new_kn = knote_alloc();
		crit_enter();
		SLIST_INSERT_HEAD(&cache_list->knote_cache, new_kn, kn_link);
		cache_list->knote_cache_cnt++;
		crit_exit();
	}

	if (fp[count] != NULL) {
		list = &fp[count]->f_klist;
	} else if (kq->kq_knhashmask) {
		list = &kq->kq_knhash[
			    KN_HASH((u_long)kev->ident, kq->kq_knhashmask)];
	}
	if (list != NULL) {
		lwkt_getpooltoken(list);
again:
		SLIST_FOREACH(kn, list, kn_link) {
			if (kn->kn_kq == kq &&
			    kn->kn_filter == kev->filter &&
			    kn->kn_id == kev->ident) {
				if (knote_acquire(kn) == 0)
					goto again;
				break;
			}
		}
		lwkt_relpooltoken(list);
	}

	/*
	 * NOTE: At this point if kn is non-NULL we will have acquired
	 *	 it and set KN_PROCESSING.
	 */
	if (kn == NULL && ((kev->flags & EV_ADD) == 0)) {
		error = ENOENT;
		++count;
		goto done;
	}

	/*
	 * kn now contains the matching knote, or NULL if no match
	 */
	if (kev->flags & EV_ADD) {
		if (kn == NULL) {
			crit_enter();
			kn = SLIST_FIRST(&cache_list->knote_cache);
			if (kn == NULL) {
				crit_exit();
				kn = knote_alloc();
			} else {
				SLIST_REMOVE_HEAD(&cache_list->knote_cache,
				    kn_link);
				cache_list->knote_cache_cnt--;
				crit_exit();
			}
			kn->kn_fp = fp[count];
			kn->kn_kq = kq;
			kn->kn_fop = fops;

			/*
			 * apply reference count to knote structure, and
			 * do not release it at the end of this routine.
			 */
			fp[count] = NULL;	/* safety */

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
				++count;
				goto done;
			}

			/*
			 * Interlock against close races which either tried
			 * to remove our knote while we were blocked or missed
			 * it entirely prior to our attachment.  We do not
			 * want to end up with a knote on a closed descriptor.
			 */
			if ((fops->f_flags & FILTEROP_ISFD) &&
			    checkfdclosed(curthread, fdp, kev->ident, kn->kn_fp,
					  closedcounter)) {
				kn->kn_status |= KN_DELETING | KN_REPROCESS;
			}
		} else {
			/*
			 * The user may change some filter values after the
			 * initial EV_ADD, but doing so will not reset any
			 * filter which have already been triggered.
			 */
			KKASSERT(kn->kn_status & KN_PROCESSING);
			if (fops == &user_filtops) {
				filt_usertouch(kn, kev, EVENT_REGISTER);
			} else {
				kn->kn_sfflags = kev->fflags;
				kn->kn_sdata = kev->data;
				kn->kn_kevent.udata = kev->udata;
			}
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
		error = 0;
		++count;
		goto done;
	} else {
		/*
		 * Modify an existing event.
		 *
		 * The user may change some filter values after the
		 * initial EV_ADD, but doing so will not reset any
		 * filter which have already been triggered.
		 */
		KKASSERT(kn->kn_status & KN_PROCESSING);
		if (fops == &user_filtops) {
			filt_usertouch(kn, kev, EVENT_REGISTER);
		} else {
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

	/*
	 * Loop control.  We stop on errors (above), and also stop after
	 * processing EV_RECEIPT, so the caller can process it.
	 */
	++count;
	if (kev->flags & EV_RECEIPT) {
		error = 0;
		goto done;
	}
	++kev;
	if (count < climit) {
		if (fp[count-1])		/* drop unprocessed fp */
			fdrop(fp[count-1]);
		goto loop;
	}

	/*
	 * Cleanup
	 */
done:
	if (td != NULL) { /* Owner of the kq_regtd */
		kq->kq_regtd = NULL;
		if (__predict_false(kq->kq_state & KQ_REGWAIT)) {
			kq->kq_state &= ~KQ_REGWAIT;
			wakeup(&kq->kq_regtd);
		}
	}
	lwkt_relpooltoken(kq);

	/*
	 * Drop unprocessed file pointers
	 */
	*countp = count;
	if (count && fp[count-1])
		fdrop(fp[count-1]);
	while (count < climit) {
		if (fp[count])
			fdrop(fp[count]);
		++count;
	}
	return (error);
}

/*
 * Scan the kqueue, return the number of active events placed in kevp up
 * to count.
 *
 * Continuous mode events may get recycled, do not continue scanning past
 * marker unless no events have been collected.
 */
static int
kqueue_scan(struct kqueue *kq, struct kevent *kevp, int count,
            struct knote *marker, int closedcounter, int scan_flags)
{
	struct knote *kn, local_marker;
	thread_t td = curthread;
	int total;

	total = 0;
	local_marker.kn_filter = EVFILT_MARKER;
	local_marker.kn_status = KN_PROCESSING;

	lwkt_getpooltoken(kq);

	/*
	 * Adjust marker, insert initial marker, or leave the marker alone.
	 *
	 * Also setup our local_marker.
	 */
	switch(scan_flags) {
	case KEVENT_SCAN_RELOAD_MARKER:
		TAILQ_REMOVE(&kq->kq_knpend, marker, kn_tqe);
		/* fall through */
	case KEVENT_SCAN_INSERT_MARKER:
		TAILQ_INSERT_TAIL(&kq->kq_knpend, marker, kn_tqe);
		break;
	}
	TAILQ_INSERT_HEAD(&kq->kq_knpend, &local_marker, kn_tqe);

	/*
	 * Collect events.
	 */
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
		    checkfdclosed(td, kq->kq_fdp, kn->kn_kevent.ident,
				  kn->kn_fp, closedcounter)) {
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
			if (kn->kn_fop == &user_filtops)
				filt_usertouch(kn, kevp, EVENT_PROCESS);
			else
				*kevp = kn->kn_kevent;
			++kevp;
			++total;
			--count;

			if (kn->kn_flags & EV_ONESHOT) {
				kn->kn_status &= ~KN_QUEUED;
				kn->kn_status |= KN_DELETING | KN_REPROCESS;
			} else {
				if (kn->kn_flags & (EV_CLEAR | EV_DISPATCH)) {
					if (kn->kn_flags & EV_CLEAR) {
						kn->kn_data = 0;
						kn->kn_fflags = 0;
					}
					if (kn->kn_flags & EV_DISPATCH) {
						kn->kn_status |= KN_DISABLED;
					}
					kn->kn_status &= ~(KN_QUEUED |
							   KN_ACTIVE);
				} else {
					TAILQ_INSERT_TAIL(&kq->kq_knpend, kn, kn_tqe);
					kq->kq_count++;
				}
			}
		}

		/*
		 * Handle any post-processing states
		 */
		knote_release(kn);
	}
	TAILQ_REMOVE(&kq->kq_knpend, &local_marker, kn_tqe);

	lwkt_relpooltoken(kq);
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

	kq = (struct kqueue *)fp->f_data;
	lwkt_getpooltoken(kq);
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
	lwkt_relpooltoken(kq);
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
	if (kq->kq_sleep_cnt) {
		u_int sleep_cnt = kq->kq_sleep_cnt;

		kq->kq_sleep_cnt = 0;
		if (sleep_cnt == 1)
			wakeup_one(kq);
		else
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
		 * If hint is non-zero running the event is mandatory
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
	lwkt_relpooltoken(list);
	TAILQ_INSERT_HEAD(&kq->kq_knlist, kn, kn_kqlink);
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
	lwkt_relpooltoken(list);
	TAILQ_REMOVE(&kq->kq_knlist, kn, kn_kqlink);
	if (kn->kn_status & KN_QUEUED)
		knote_dequeue(kn);
	if (kn->kn_fop->f_flags & FILTEROP_ISFD) {
		fdrop(kn->kn_fp);
		kn->kn_fp = NULL;
	}
	knote_free(kn);
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
	struct knote_cache_list *cache_list;

	cache_list = &knote_cache_lists[mycpuid];
	if (cache_list->knote_cache_cnt < KNOTE_CACHE_MAX) {
		crit_enter();
		SLIST_INSERT_HEAD(&cache_list->knote_cache, kn, kn_link);
		cache_list->knote_cache_cnt++;
		crit_exit();
		return;
	}
	kfree(kn, M_KQUEUE);
}

struct sleepinfo {
	void *ident;
	int timedout;
};

static void
precise_sleep_intr(systimer_t info, int in_ipi, struct intrframe *frame)
{
	struct sleepinfo *si;

	si = info->data;
	si->timedout = 1;
	wakeup(si->ident);
}

static int
precise_sleep(void *ident, int flags, const char *wmesg, int us)
{
	struct systimer info;
	struct sleepinfo si = {
		.ident = ident,
		.timedout = 0,
	};
	int r;

	tsleep_interlock(ident, flags);
	systimer_init_oneshot(&info, precise_sleep_intr, &si, us);
	r = tsleep(ident, flags | PINTERLOCKED, wmesg, 0);
	systimer_del(&info);
	if (si.timedout)
		r = EWOULDBLOCK;

	return r;
}
