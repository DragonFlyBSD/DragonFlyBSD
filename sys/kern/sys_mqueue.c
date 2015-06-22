/*	$NetBSD: sys_mqueue.c,v 1.16 2009/04/11 23:05:26 christos Exp $	*/

/*
 * Copyright (c) 2007, 2008 Mindaugas Rasiukevicius <rmind at NetBSD org>
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
 */

/*
 * Implementation of POSIX message queues.
 * Defined in the Base Definitions volume of IEEE Std 1003.1-2001.
 *
 * Locking
 *
 * Global list of message queues (mqueue_head) and proc_t::p_mqueue_cnt
 * counter are protected by mqlist_mtx lock.  The very message queue and
 * its members are protected by mqueue::mq_mtx.
 *
 * Lock order:
 *	mqlist_mtx
 *	  -> mqueue::mq_mtx
 */

#include <stdbool.h>
#include <sys/param.h>
#include <sys/types.h>
#include <sys/errno.h>
#include <sys/fcntl.h>
#include <sys/file.h>
#include <sys/filedesc.h>
#include <sys/ucred.h>
#include <sys/priv.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/mqueue.h>
#include <sys/proc.h>
#include <sys/queue.h>
#include <sys/event.h>
#include <sys/serialize.h>
#include <sys/signal.h>
#include <sys/signalvar.h>
#include <sys/spinlock.h>
#include <sys/spinlock2.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <sys/sysproto.h>
#include <sys/systm.h>
#include <sys/lock.h>
#include <sys/unistd.h>
#include <sys/vnode.h>

/* System-wide limits. */
static u_int			mq_open_max = MQ_OPEN_MAX;
static u_int			mq_prio_max = MQ_PRIO_MAX;
static u_int			mq_max_msgsize = 16 * MQ_DEF_MSGSIZE;
static u_int			mq_def_maxmsg = 32;
static u_int			mq_max_maxmsg = 16 * 32;

struct lock			mqlist_mtx;
static LIST_HEAD(, mqueue)	mqueue_head =
	LIST_HEAD_INITIALIZER(mqueue_head);

typedef struct	file file_t;	/* XXX: Should we put this in sys/types.h ? */

/* Function prototypes */
static int	mq_stat_fop(file_t *, struct stat *, struct ucred *cred);
static int	mq_close_fop(file_t *);
static int	mq_kqfilter_fop(struct file *fp, struct knote *kn);
static void	mqfilter_read_detach(struct knote *kn);
static void	mqfilter_write_detach(struct knote *kn);
static int	mqfilter_read(struct knote *kn, long hint);
static int	mqfilter_write(struct knote *kn, long hint);

/* Some time-related utility functions */
static int	tstohz(const struct timespec *ts);

/* File operations vector */
static struct fileops mqops = {
	.fo_read = badfo_readwrite,
	.fo_write = badfo_readwrite,
	.fo_ioctl = badfo_ioctl,
	.fo_stat = mq_stat_fop,
	.fo_close = mq_close_fop,
	.fo_kqfilter = mq_kqfilter_fop,
	.fo_shutdown = badfo_shutdown
};

/* Define a new malloc type for message queues */
MALLOC_DECLARE(M_MQBUF);
MALLOC_DEFINE(M_MQBUF, "mqueues", "Buffers to message queues");

/*
 * Initialize POSIX message queue subsystem.
 */
void
mqueue_sysinit(void)
{
	lockinit(&mqlist_mtx, "mqlist_mtx", 0, LK_CANRECURSE);
}

/*
 * Free the message.
 */
static void
mqueue_freemsg(struct mq_msg *msg, const size_t size)
{
	kfree(msg, M_MQBUF);
}

/*
 * Destroy the message queue.
 */
static void
mqueue_destroy(struct mqueue *mq)
{
	struct mq_msg *msg;
	size_t msz;
	u_int i;

	/* Note MQ_PQSIZE + 1. */
	for (i = 0; i < MQ_PQSIZE + 1; i++) {
		while ((msg = TAILQ_FIRST(&mq->mq_head[i])) != NULL) {
			TAILQ_REMOVE(&mq->mq_head[i], msg, msg_queue);
			msz = sizeof(struct mq_msg) + msg->msg_len;
			mqueue_freemsg(msg, msz);
		}
	}
	lockuninit(&mq->mq_mtx);
	kfree(mq, M_MQBUF);
}

/*
 * Lookup for file name in general list of message queues.
 *  => locks the message queue
 */
static void *
mqueue_lookup(char *name)
{
	struct mqueue *mq;

	KKASSERT(lockstatus(&mqlist_mtx, curthread));

	LIST_FOREACH(mq, &mqueue_head, mq_list) {
		if (strncmp(mq->mq_name, name, MQ_NAMELEN) == 0) {
			lockmgr(&mq->mq_mtx, LK_EXCLUSIVE);
			return mq;
		}
	}

	return NULL;
}

/*
 * mqueue_get: get the mqueue from the descriptor.
 *  => locks the message queue, if found.
 *  => holds a reference on the file descriptor.
 */
static int
mqueue_get(struct lwp *l, mqd_t mqd, file_t **fpr)
{
	struct mqueue *mq;
	file_t *fp;

	fp = holdfp(curproc->p_fd, (int)mqd, -1);	/* XXX: Why -1 ? */
	if (__predict_false(fp == NULL))
		return EBADF;

	if (__predict_false(fp->f_type != DTYPE_MQUEUE)) {
		fdrop(fp);
		return EBADF;
	}
	mq = fp->f_data;
	lockmgr(&mq->mq_mtx, LK_EXCLUSIVE);

	*fpr = fp;
	return 0;
}

/*
 * mqueue_linear_insert: perform linear insert according to the message
 * priority into the reserved queue (MQ_PQRESQ).  Reserved queue is a
 * sorted list used only when mq_prio_max is increased via sysctl.
 */
static inline void
mqueue_linear_insert(struct mqueue *mq, struct mq_msg *msg)
{
	struct mq_msg *mit;

	TAILQ_FOREACH(mit, &mq->mq_head[MQ_PQRESQ], msg_queue) {
		if (msg->msg_prio > mit->msg_prio)
			break;
	}
	if (mit == NULL) {
		TAILQ_INSERT_TAIL(&mq->mq_head[MQ_PQRESQ], msg, msg_queue);
	} else {
		TAILQ_INSERT_BEFORE(mit, msg, msg_queue);
	}
}

/*
 * Compute number of ticks in the specified amount of time.
 */
static int
tstohz(const struct timespec *ts)
{
	struct timeval tv;

	/*
	 * usec has great enough resolution for hz, so convert to a
	 * timeval and use tvtohz() above.
	 */
	TIMESPEC_TO_TIMEVAL(&tv, ts);
	return tvtohz_high(&tv);	/* XXX Why _high() and not _low() ? */
}

/*
 * Converter from struct timespec to the ticks.
 * Used by mq_timedreceive(), mq_timedsend().
 */
int
abstimeout2timo(struct timespec *ts, int *timo)
{
	struct timespec tsd;
	int error;

	error = itimespecfix(ts);
	if (error) {
		return error;
	}
	getnanotime(&tsd);
	timespecsub(ts, &tsd);
	if (ts->tv_sec < 0 || (ts->tv_sec == 0 && ts->tv_nsec <= 0)) {
		return ETIMEDOUT;
	}
	*timo = tstohz(ts);
	KKASSERT(*timo != 0);

	return 0;
}

static int
mq_stat_fop(file_t *fp, struct stat *st, struct ucred *cred)
{
	struct mqueue *mq = fp->f_data;

	(void)memset(st, 0, sizeof(*st));

	lockmgr(&mq->mq_mtx, LK_EXCLUSIVE);
	st->st_mode = mq->mq_mode;
	st->st_uid = mq->mq_euid;
	st->st_gid = mq->mq_egid;
	st->st_atimespec = mq->mq_atime;
	st->st_mtimespec = mq->mq_mtime;
	/*st->st_ctimespec = st->st_birthtimespec = mq->mq_btime;*/
	st->st_uid = fp->f_cred->cr_uid;
	st->st_gid = fp->f_cred->cr_svgid;
	lockmgr(&mq->mq_mtx, LK_RELEASE);

	return 0;
}

static struct filterops mqfiltops_read =
{ FILTEROP_ISFD|FILTEROP_MPSAFE, NULL, mqfilter_read_detach, mqfilter_read };
static struct filterops mqfiltops_write =
{ FILTEROP_ISFD|FILTEROP_MPSAFE, NULL, mqfilter_write_detach, mqfilter_write };

static int
mq_kqfilter_fop(struct file *fp, struct knote *kn)
{
	struct mqueue *mq = fp->f_data;
	struct klist *klist;

	lockmgr(&mq->mq_mtx, LK_EXCLUSIVE);

	switch (kn->kn_filter) {
	case EVFILT_READ:
		kn->kn_fop = &mqfiltops_read;
		kn->kn_hook = (caddr_t)mq;
		klist = &mq->mq_rkq.ki_note;
		break;
	case EVFILT_WRITE:
		kn->kn_fop = &mqfiltops_write;
		kn->kn_hook = (caddr_t)mq;
		klist = &mq->mq_wkq.ki_note;
		break;
	default:
		lockmgr(&mq->mq_mtx, LK_RELEASE);
		return (EOPNOTSUPP);
	}

	knote_insert(klist, kn);
	lockmgr(&mq->mq_mtx, LK_RELEASE);

	return (0);
}

static void
mqfilter_read_detach(struct knote *kn)
{
	struct mqueue *mq = (struct mqueue *)kn->kn_hook;

	lockmgr(&mq->mq_mtx, LK_EXCLUSIVE);
	struct klist *klist = &mq->mq_rkq.ki_note;
	knote_remove(klist, kn);
	lockmgr(&mq->mq_mtx, LK_RELEASE);
}

static void
mqfilter_write_detach(struct knote *kn)
{
	struct mqueue *mq = (struct mqueue *)kn->kn_hook;

	lockmgr(&mq->mq_mtx, LK_EXCLUSIVE);
	struct klist *klist = &mq->mq_wkq.ki_note;
	knote_remove(klist, kn);
	lockmgr(&mq->mq_mtx, LK_RELEASE);
}

static int
mqfilter_read(struct knote *kn, long hint)
{
	struct mqueue *mq = (struct mqueue *)kn->kn_hook;
	struct mq_attr *mqattr;
	int ready = 0;

	lockmgr(&mq->mq_mtx, LK_EXCLUSIVE);
	mqattr = &mq->mq_attrib;
	/* Ready for receiving, if there are messages in the queue */
	if (mqattr->mq_curmsgs)
		ready = 1;
	lockmgr(&mq->mq_mtx, LK_RELEASE);

	return (ready);
}

static int
mqfilter_write(struct knote *kn, long hint)
{
	struct mqueue *mq = (struct mqueue *)kn->kn_hook;
	struct mq_attr *mqattr;
	int ready = 0;

	lockmgr(&mq->mq_mtx, LK_EXCLUSIVE);
	mqattr = &mq->mq_attrib;
	/* Ready for sending, if the message queue is not full */
	if (mqattr->mq_curmsgs < mqattr->mq_maxmsg)
		ready = 1;
	lockmgr(&mq->mq_mtx, LK_RELEASE);

	return (ready);
}

static int
mq_close_fop(file_t *fp)
{
	struct proc *p = curproc;
	struct mqueue *mq = fp->f_data;
	bool destroy;

	lockmgr(&mqlist_mtx, LK_EXCLUSIVE);
	lockmgr(&mq->mq_mtx, LK_EXCLUSIVE);

	/* Decrease the counters */
	p->p_mqueue_cnt--;
	mq->mq_refcnt--;

	/* Remove notification if registered for this process */
	if (mq->mq_notify_proc == p)
		mq->mq_notify_proc = NULL;

	/*
	 * If this is the last reference and mqueue is marked for unlink,
	 * remove and later destroy the message queue.
	 */
	if (mq->mq_refcnt == 0 && (mq->mq_attrib.mq_flags & MQ_UNLINK)) {
		LIST_REMOVE(mq, mq_list);
		destroy = true;
	} else
		destroy = false;

	lockmgr(&mq->mq_mtx, LK_RELEASE);
	lockmgr(&mqlist_mtx, LK_RELEASE);

	if (destroy)
		mqueue_destroy(mq);

	return 0;
}

/*
 * General mqueue system calls.
 */

int
sys_mq_open(struct mq_open_args *uap)
{
	/* {
		syscallarg(const char *) name;
		syscallarg(int) oflag;
		syscallarg(mode_t) mode;
		syscallarg(struct mq_attr) attr;
	} */
	struct thread *td = curthread;
	struct proc *p = td->td_proc;
	struct filedesc *fdp = p->p_fd;
	struct mqueue *mq, *mq_new = NULL;
	file_t *fp;
	char *name;
	int mqd, error, oflag;

	/* Check access mode flags */
	oflag = SCARG(uap, oflag);
	if ((oflag & O_ACCMODE) == (O_WRONLY | O_RDWR)) {
		return EINVAL;
	}

	/* Get the name from the user-space */
	name = kmalloc(MQ_NAMELEN, M_MQBUF, M_WAITOK | M_ZERO | M_NULLOK);
	if (name == NULL)
		return (ENOMEM);
	error = copyinstr(SCARG(uap, name), name, MQ_NAMELEN - 1, NULL);
	if (error) {
		kfree(name, M_MQBUF);
		return error;
	}

	if (oflag & O_CREAT) {
		struct mq_attr attr;
		u_int i;

		/* Check the limit */
		if (p->p_mqueue_cnt == mq_open_max) {
			kfree(name, M_MQBUF);
			return EMFILE;
		}

		/* Empty name is invalid */
		if (name[0] == '\0') {
			kfree(name, M_MQBUF);
			return EINVAL;
		}

		/* Check for mqueue attributes */
		if (SCARG(uap, attr)) {
			error = copyin(SCARG(uap, attr), &attr,
				sizeof(struct mq_attr));
			if (error) {
				kfree(name, M_MQBUF);
				return error;
			}
			if (attr.mq_maxmsg <= 0 ||
			    attr.mq_maxmsg > mq_max_maxmsg ||
			    attr.mq_msgsize <= 0 ||
			    attr.mq_msgsize > mq_max_msgsize) {
				kfree(name, M_MQBUF);
				return EINVAL;
			}
			attr.mq_curmsgs = 0;
		} else {
			memset(&attr, 0, sizeof(struct mq_attr));
			attr.mq_maxmsg = mq_def_maxmsg;
			attr.mq_msgsize =
			    MQ_DEF_MSGSIZE - sizeof(struct mq_msg);
		}

		/*
		 * Allocate new mqueue, initialize data structures,
		 * copy the name, attributes and set the flag.
		 */
		mq_new = kmalloc(sizeof(struct mqueue), M_MQBUF, 
					M_WAITOK | M_ZERO | M_NULLOK);
		if (mq_new == NULL) {
			kfree(name, M_MQBUF);
			return (ENOMEM);
		}

		lockinit(&mq_new->mq_mtx, "mq_new->mq_mtx", 0, LK_CANRECURSE);
		for (i = 0; i < (MQ_PQSIZE + 1); i++) {
			TAILQ_INIT(&mq_new->mq_head[i]);
		}

		strlcpy(mq_new->mq_name, name, MQ_NAMELEN);
		memcpy(&mq_new->mq_attrib, &attr, sizeof(struct mq_attr));

		/*CTASSERT((O_MASK & (MQ_UNLINK | MQ_RECEIVE)) == 0);*/
		/* mq_new->mq_attrib.mq_flags = (O_MASK & oflag); */
		mq_new->mq_attrib.mq_flags = oflag;

		/* Store mode and effective UID with GID */
		mq_new->mq_mode = ((SCARG(uap, mode) &
		    ~p->p_fd->fd_cmask) & ALLPERMS) & ~S_ISTXT;
		mq_new->mq_euid = td->td_ucred->cr_uid;
		mq_new->mq_egid = td->td_ucred->cr_svgid;
	}

	/* Allocate file structure and descriptor */
	error = falloc(td->td_lwp, &fp, &mqd);
	if (error) {
		if (mq_new)
			mqueue_destroy(mq_new);
		kfree(name, M_MQBUF);
		return error;
	}
	fp->f_type = DTYPE_MQUEUE;
	fp->f_flag = FFLAGS(oflag) & (FREAD | FWRITE);
	fp->f_ops = &mqops;

	/* Look up for mqueue with such name */
	lockmgr(&mqlist_mtx, LK_EXCLUSIVE);
	mq = mqueue_lookup(name);
	if (mq) {
		int acc_mode;

		KKASSERT(lockstatus(&mq->mq_mtx, curthread));

		/* Check if mqueue is not marked as unlinking */
		if (mq->mq_attrib.mq_flags & MQ_UNLINK) {
			error = EACCES;
			goto exit;
		}
		/* Fail if O_EXCL is set, and mqueue already exists */
		if ((oflag & O_CREAT) && (oflag & O_EXCL)) {
			error = EEXIST;
			goto exit;
		}

		/*
		 * Check the permissions. Note the difference between
		 * VREAD/VWRITE and FREAD/FWRITE.
		 */
		acc_mode = 0;
		if (fp->f_flag & FREAD) {
			acc_mode |= VREAD;
		}
		if (fp->f_flag & FWRITE) {
			acc_mode |= VWRITE;
		}
		if (vaccess(VNON, mq->mq_mode, mq->mq_euid, mq->mq_egid,
			acc_mode, td->td_ucred)) {

			error = EACCES;
			goto exit;
		}
	} else {
		/* Fail if mqueue neither exists, nor we create it */
		if ((oflag & O_CREAT) == 0) {
			lockmgr(&mqlist_mtx, LK_RELEASE);
			KKASSERT(mq_new == NULL);
			fsetfd(fdp, NULL, mqd);
			fp->f_ops = &badfileops;
			fdrop(fp);
			kfree(name, M_MQBUF);
			return ENOENT;
		}

		/* Check the limit */
		if (p->p_mqueue_cnt == mq_open_max) {
			error = EMFILE;
			goto exit;
		}

		/* Insert the queue to the list */
		mq = mq_new;
		lockmgr(&mq->mq_mtx, LK_EXCLUSIVE);
		LIST_INSERT_HEAD(&mqueue_head, mq, mq_list);
		mq_new = NULL;
		getnanotime(&mq->mq_btime);
		mq->mq_atime = mq->mq_mtime = mq->mq_btime;
	}

	/* Increase the counters, and make descriptor ready */
	p->p_mqueue_cnt++;
	mq->mq_refcnt++;
	fp->f_data = mq;
exit:
	lockmgr(&mq->mq_mtx, LK_RELEASE);
	lockmgr(&mqlist_mtx, LK_RELEASE);

	if (mq_new)
		mqueue_destroy(mq_new);
	if (error) {
		fsetfd(fdp, NULL, mqd);
		fp->f_ops = &badfileops;
	} else {
		fsetfd(fdp, fp, mqd);
		uap->sysmsg_result = mqd;
	}
	fdrop(fp);
	kfree(name, M_MQBUF);

	return error;
}

int
sys_mq_close(struct mq_close_args *uap)
{
	return sys_close((void *)uap);
}

/*
 * Primary mq_receive1() function.
 */
int
mq_receive1(struct lwp *l, mqd_t mqdes, void *msg_ptr, size_t msg_len,
    unsigned *msg_prio, struct timespec *ts, ssize_t *mlen)
{
	file_t *fp = NULL;
	struct mqueue *mq;
	struct mq_msg *msg = NULL;
	struct mq_attr *mqattr;
	u_int idx;
	int error;

	/* Get the message queue */
	error = mqueue_get(l, mqdes, &fp);
	if (error) {
		return error;
	}
	mq = fp->f_data;
	if ((fp->f_flag & FREAD) == 0) {
		error = EBADF;
		goto error;
	}
	getnanotime(&mq->mq_atime);
	mqattr = &mq->mq_attrib;

	/* Check the message size limits */
	if (msg_len < mqattr->mq_msgsize) {
		error = EMSGSIZE;
		goto error;
	}

	/* Check if queue is empty */
	while (mqattr->mq_curmsgs == 0) {
		int t;

		if (mqattr->mq_flags & O_NONBLOCK) {
			error = EAGAIN;
			goto error;
		}
		if (ts) {
			error = abstimeout2timo(ts, &t);
			if (error)
				goto error;
		} else
			t = 0;
		/*
		 * Block until someone sends the message.
		 * While doing this, notification should not be sent.
		 */
		mqattr->mq_flags |= MQ_RECEIVE;
		error = lksleep(&mq->mq_send_cv, &mq->mq_mtx, PCATCH, "mqsend", t);
		mqattr->mq_flags &= ~MQ_RECEIVE;
		if (error || (mqattr->mq_flags & MQ_UNLINK)) {
			error = (error == EWOULDBLOCK) ? ETIMEDOUT : EINTR;
			goto error;
		}
	}


	/*
	 * Find the highest priority message, and remove it from the queue.
	 * At first, reserved queue is checked, bitmap is next.
	 */
	msg = TAILQ_FIRST(&mq->mq_head[MQ_PQRESQ]);
	if (__predict_true(msg == NULL)) {
		idx = ffs(mq->mq_bitmap);
		msg = TAILQ_FIRST(&mq->mq_head[idx]);
		KKASSERT(msg != NULL);
	} else {
		idx = MQ_PQRESQ;
	}
	TAILQ_REMOVE(&mq->mq_head[idx], msg, msg_queue);

	/* Unmark the bit, if last message. */
	if (__predict_true(idx) && TAILQ_EMPTY(&mq->mq_head[idx])) {
		KKASSERT((MQ_PQSIZE - idx) == msg->msg_prio);
		mq->mq_bitmap &= ~(1 << --idx);
	}

	/* Decrement the counter and signal waiter, if any */
	mqattr->mq_curmsgs--;
	wakeup_one(&mq->mq_recv_cv);

	/* Ready for sending now */
	KNOTE(&mq->mq_wkq.ki_note, 0);
error:
	lockmgr(&mq->mq_mtx, LK_RELEASE);
	fdrop(fp);
	if (error)
		return error;

	/*
	 * Copy the data to the user-space.
	 * Note: According to POSIX, no message should be removed from the
	 * queue in case of fail - this would be violated.
	 */
	*mlen = msg->msg_len;
	error = copyout(msg->msg_ptr, msg_ptr, msg->msg_len);
	if (error == 0 && msg_prio)
		error = copyout(&msg->msg_prio, msg_prio, sizeof(unsigned));
	mqueue_freemsg(msg, sizeof(struct mq_msg) + msg->msg_len);

	return error;
}

int
sys_mq_receive(struct mq_receive_args *uap)
{
	/* {
		syscallarg(mqd_t) mqdes;
		syscallarg(char *) msg_ptr;
		syscallarg(size_t) msg_len;
		syscallarg(unsigned *) msg_prio;
	} */
	ssize_t mlen;
	int error;

	error = mq_receive1(curthread->td_lwp, SCARG(uap, mqdes), SCARG(uap, msg_ptr),
	    SCARG(uap, msg_len), SCARG(uap, msg_prio), 0, &mlen);
	if (error == 0)
		uap->sysmsg_result = mlen;

	return error;
}

int
sys_mq_timedreceive(struct mq_timedreceive_args *uap)
{
	/* {
		syscallarg(mqd_t) mqdes;
		syscallarg(char *) msg_ptr;
		syscallarg(size_t) msg_len;
		syscallarg(unsigned *) msg_prio;
		syscallarg(const struct timespec *) abs_timeout;
	} */
	int error;
	ssize_t mlen;
	struct timespec ts, *tsp;

	/* Get and convert time value */
	if (SCARG(uap, abs_timeout)) {
		error = copyin(SCARG(uap, abs_timeout), &ts, sizeof(ts));
		if (error)
			return error;
		tsp = &ts;
	} else {
		tsp = NULL;
	}

	error = mq_receive1(curthread->td_lwp, SCARG(uap, mqdes), SCARG(uap, msg_ptr),
	    SCARG(uap, msg_len), SCARG(uap, msg_prio), tsp, &mlen);
	if (error == 0)
		uap->sysmsg_result = mlen;

	return error;
}

/*
 * Primary mq_send1() function.
 */
int
mq_send1(struct lwp *l, mqd_t mqdes, const char *msg_ptr, size_t msg_len,
    unsigned msg_prio, struct timespec *ts)
{
	file_t *fp = NULL;
	struct mqueue *mq;
	struct mq_msg *msg;
	struct mq_attr *mqattr;
	struct proc *notify = NULL;
	/*ksiginfo_t ksi;*/
	size_t size;
	int error;

	/* Check the priority range */
	if (msg_prio >= mq_prio_max)
		return EINVAL;

	/* Allocate a new message */
	size = sizeof(struct mq_msg) + msg_len;
	if (size > mq_max_msgsize)
		return EMSGSIZE;

	msg = kmalloc(size, M_MQBUF, M_WAITOK | M_NULLOK);
	if (msg == NULL)
		return (ENOMEM);


	/* Get the data from user-space */
	error = copyin(msg_ptr, msg->msg_ptr, msg_len);
	if (error) {
		mqueue_freemsg(msg, size);
		return error;
	}
	msg->msg_len = msg_len;
	msg->msg_prio = msg_prio;

	/* Get the mqueue */
	error = mqueue_get(l, mqdes, &fp);
	if (error) {
		mqueue_freemsg(msg, size);
		return error;
	}
	mq = fp->f_data;
	if ((fp->f_flag & FWRITE) == 0) {
		error = EBADF;
		goto error;
	}
	getnanotime(&mq->mq_mtime);
	mqattr = &mq->mq_attrib;

	/* Check the message size limit */
	if (msg_len <= 0 || msg_len > mqattr->mq_msgsize) {
		error = EMSGSIZE;
		goto error;
	}

	/* Check if queue is full */
	while (mqattr->mq_curmsgs >= mqattr->mq_maxmsg) {
		int t;

		if (mqattr->mq_flags & O_NONBLOCK) {
			error = EAGAIN;
			goto error;
		}
		if (ts) {
			error = abstimeout2timo(ts, &t);
			if (error)
				goto error;
		} else
			t = 0;
		/* Block until queue becomes available */
		error = lksleep(&mq->mq_recv_cv, &mq->mq_mtx, PCATCH, "mqrecv", t);
		if (error || (mqattr->mq_flags & MQ_UNLINK)) {
			error = (error == EWOULDBLOCK) ? ETIMEDOUT : error;
			goto error;
		}
	}
	KKASSERT(mq->mq_attrib.mq_curmsgs < mq->mq_attrib.mq_maxmsg);

	/*
	 * Insert message into the queue, according to the priority.
	 * Note the difference between index and priority.
	 */
	if (__predict_true(msg_prio < MQ_PQSIZE)) {
		u_int idx = MQ_PQSIZE - msg_prio;

		KKASSERT(idx != MQ_PQRESQ);
		TAILQ_INSERT_TAIL(&mq->mq_head[idx], msg, msg_queue);
		mq->mq_bitmap |= (1 << --idx);
	} else {
		mqueue_linear_insert(mq, msg);
	}

	/* Check for the notify */
	if (mqattr->mq_curmsgs == 0 && mq->mq_notify_proc &&
	    (mqattr->mq_flags & MQ_RECEIVE) == 0 &&
	    mq->mq_sig_notify.sigev_notify == SIGEV_SIGNAL) {
		/* Initialize the signal */
		/*KSI_INIT(&ksi);*/
		/*ksi.ksi_signo = mq->mq_sig_notify.sigev_signo;*/
		/*ksi.ksi_code = SI_MESGQ;*/
		/*ksi.ksi_value = mq->mq_sig_notify.sigev_value;*/
		/* Unregister the process */
		notify = mq->mq_notify_proc;
		mq->mq_notify_proc = NULL;
	}

	/* Increment the counter and signal waiter, if any */
	mqattr->mq_curmsgs++;
	wakeup_one(&mq->mq_send_cv);

	/* Ready for receiving now */
	KNOTE(&mq->mq_rkq.ki_note, 0);
error:
	if (error) {
		lockmgr(&mq->mq_mtx, LK_RELEASE);
		fdrop(fp);
		mqueue_freemsg(msg, size);
	} else if (notify) {
		PHOLD(notify);
		lockmgr(&mq->mq_mtx, LK_RELEASE);
		fdrop(fp);
		/* Send the notify, if needed */
		/*kpsignal(notify, &ksi, NULL);*/
		ksignal(notify, mq->mq_sig_notify.sigev_signo);
		PRELE(notify);
	} else {
		lockmgr(&mq->mq_mtx, LK_RELEASE);
		fdrop(fp);
	}
	return error;
}

int
sys_mq_send(struct mq_send_args *uap)
{
	/* {
		syscallarg(mqd_t) mqdes;
		syscallarg(const char *) msg_ptr;
		syscallarg(size_t) msg_len;
		syscallarg(unsigned) msg_prio;
	} */

	return mq_send1(curthread->td_lwp, SCARG(uap, mqdes), SCARG(uap, msg_ptr),
	    SCARG(uap, msg_len), SCARG(uap, msg_prio), 0);
}

int
sys_mq_timedsend(struct mq_timedsend_args *uap)
{
	/* {
		syscallarg(mqd_t) mqdes;
		syscallarg(const char *) msg_ptr;
		syscallarg(size_t) msg_len;
		syscallarg(unsigned) msg_prio;
		syscallarg(const struct timespec *) abs_timeout;
	} */
	struct timespec ts, *tsp;
	int error;

	/* Get and convert time value */
	if (SCARG(uap, abs_timeout)) {
		error = copyin(SCARG(uap, abs_timeout), &ts, sizeof(ts));
		if (error)
			return error;
		tsp = &ts;
	} else {
		tsp = NULL;
	}

	return mq_send1(curthread->td_lwp, SCARG(uap, mqdes), SCARG(uap, msg_ptr),
	    SCARG(uap, msg_len), SCARG(uap, msg_prio), tsp);
}

int
sys_mq_notify(struct mq_notify_args *uap)
{
	/* {
		syscallarg(mqd_t) mqdes;
		syscallarg(const struct sigevent *) notification;
	} */
	file_t *fp = NULL;
	struct mqueue *mq;
	struct sigevent sig;
	int error;

	if (SCARG(uap, notification)) {
		/* Get the signal from user-space */
		error = copyin(SCARG(uap, notification), &sig,
		    sizeof(struct sigevent));
		if (error)
			return error;
		if (sig.sigev_notify == SIGEV_SIGNAL &&
		    (sig.sigev_signo <= 0 || sig.sigev_signo >= NSIG))
			return EINVAL;
	}

	error = mqueue_get(curthread->td_lwp, SCARG(uap, mqdes), &fp);
	if (error)
		return error;
	mq = fp->f_data;

	if (SCARG(uap, notification)) {
		/* Register notification: set the signal and target process */
		if (mq->mq_notify_proc == NULL) {
			memcpy(&mq->mq_sig_notify, &sig,
			    sizeof(struct sigevent));
			mq->mq_notify_proc = curproc;
		} else {
			/* Fail if someone else already registered */
			error = EBUSY;
		}
	} else {
		/* Unregister the notification */
		mq->mq_notify_proc = NULL;
	}
	lockmgr(&mq->mq_mtx, LK_RELEASE);
	fdrop(fp);

	return error;
}

int
sys_mq_getattr(struct mq_getattr_args *uap)
{
	/* {
		syscallarg(mqd_t) mqdes;
		syscallarg(struct mq_attr *) mqstat;
	} */
	file_t *fp = NULL;
	struct mqueue *mq;
	struct mq_attr attr;
	int error;

	/* Get the message queue */
	error = mqueue_get(curthread->td_lwp, SCARG(uap, mqdes), &fp);
	if (error)
		return error;
	mq = fp->f_data;
	memcpy(&attr, &mq->mq_attrib, sizeof(struct mq_attr));
	lockmgr(&mq->mq_mtx, LK_RELEASE);
	fdrop(fp);

	return copyout(&attr, SCARG(uap, mqstat), sizeof(struct mq_attr));
}

int
sys_mq_setattr(struct mq_setattr_args *uap)
{
	/* {
		syscallarg(mqd_t) mqdes;
		syscallarg(const struct mq_attr *) mqstat;
		syscallarg(struct mq_attr *) omqstat;
	} */
	file_t *fp = NULL;
	struct mqueue *mq;
	struct mq_attr attr;
	int error, nonblock;

	error = copyin(SCARG(uap, mqstat), &attr, sizeof(struct mq_attr));
	if (error)
		return error;
	nonblock = (attr.mq_flags & O_NONBLOCK);

	/* Get the message queue */
	error = mqueue_get(curthread->td_lwp, SCARG(uap, mqdes), &fp);
	if (error)
		return error;
	mq = fp->f_data;

	/* Copy the old attributes, if needed */
	if (SCARG(uap, omqstat)) {
		memcpy(&attr, &mq->mq_attrib, sizeof(struct mq_attr));
	}

	/* Ignore everything, except O_NONBLOCK */
	if (nonblock)
		mq->mq_attrib.mq_flags |= O_NONBLOCK;
	else
		mq->mq_attrib.mq_flags &= ~O_NONBLOCK;

	lockmgr(&mq->mq_mtx, LK_RELEASE);
	fdrop(fp);

	/*
	 * Copy the data to the user-space.
	 * Note: According to POSIX, the new attributes should not be set in
	 * case of fail - this would be violated.
	 */
	if (SCARG(uap, omqstat))
		error = copyout(&attr, SCARG(uap, omqstat),
		    sizeof(struct mq_attr));

	return error;
}

int
sys_mq_unlink(struct mq_unlink_args *uap)
{
	/* {
		syscallarg(const char *) name;
	} */
	struct thread *td = curthread;
	struct mqueue *mq;
	char *name;
	int error, refcnt = 0;

	/* Get the name from the user-space */
	name = kmalloc(MQ_NAMELEN, M_MQBUF, M_WAITOK | M_ZERO | M_NULLOK);
	if (name == NULL)
		return (ENOMEM);
	error = copyinstr(SCARG(uap, name), name, MQ_NAMELEN - 1, NULL);
	if (error) {
		kfree(name, M_MQBUF);
		return error;
	}

	/* Lookup for this file */
	lockmgr(&mqlist_mtx, LK_EXCLUSIVE);
	mq = mqueue_lookup(name);
	if (mq == NULL) {
		error = ENOENT;
		goto error;
	}

	/* Check the permissions */
	if (td->td_ucred->cr_uid != mq->mq_euid &&
	    priv_check(td, PRIV_ROOT) != 0) {
		lockmgr(&mq->mq_mtx, LK_RELEASE);
		error = EACCES;
		goto error;
	}

	/* Mark message queue as unlinking, before leaving the window */
	mq->mq_attrib.mq_flags |= MQ_UNLINK;

	/* Wake up all waiters, if there are such */
	wakeup(&mq->mq_send_cv);
	wakeup(&mq->mq_recv_cv);

	KNOTE(&mq->mq_rkq.ki_note, 0);
	KNOTE(&mq->mq_wkq.ki_note, 0);

	refcnt = mq->mq_refcnt;
	if (refcnt == 0)
		LIST_REMOVE(mq, mq_list);

	lockmgr(&mq->mq_mtx, LK_RELEASE);
error:
	lockmgr(&mqlist_mtx, LK_RELEASE);

	/*
	 * If there are no references - destroy the message
	 * queue, otherwise, the last mq_close() will do that.
	 */
	if (error == 0 && refcnt == 0)
		mqueue_destroy(mq);

	kfree(name, M_MQBUF);
	return error;
}

/*
 * SysCtl.
 */
SYSCTL_NODE(_kern, OID_AUTO, mqueue,
    CTLFLAG_RW, 0, "Message queue options");

SYSCTL_INT(_kern_mqueue, OID_AUTO, mq_open_max,
    CTLFLAG_RW, &mq_open_max, 0,
    "Maximal number of message queue descriptors per process");

SYSCTL_INT(_kern_mqueue, OID_AUTO, mq_prio_max,
    CTLFLAG_RW, &mq_prio_max, 0,
    "Maximal priority of the message");

SYSCTL_INT(_kern_mqueue, OID_AUTO, mq_max_msgsize,
    CTLFLAG_RW, &mq_max_msgsize, 0,
    "Maximal allowed size of the message");

SYSCTL_INT(_kern_mqueue, OID_AUTO, mq_def_maxmsg,
    CTLFLAG_RW, &mq_def_maxmsg, 0,
    "Default maximal message count");

SYSCTL_INT(_kern_mqueue, OID_AUTO, mq_max_maxmsg,
    CTLFLAG_RW, &mq_max_maxmsg, 0,
    "Maximal allowed message count");

SYSINIT(sys_mqueue_init, SI_SUB_PRE_DRIVERS, SI_ORDER_ANY, mqueue_sysinit, NULL);
