/*
 * Copyright (c) 2005 David Xu <davidxu@freebsd.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $DragonFly: src/lib/libthread_xu/thread/thr_cond.c,v 1.12 2008/04/15 01:45:22 dillon Exp $
 */

#include "namespace.h"
#include <machine/tls.h>

#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <limits.h>
#include "un-namespace.h"

#include "thr_private.h"

umtx_t		_cond_static_lock;

int	__pthread_cond_wait(pthread_cond_t *cond, pthread_mutex_t *mutex);
int	__pthread_cond_timedwait(pthread_cond_t *cond, pthread_mutex_t *mutex,
		       const struct timespec *abstime);
/*
 * Prototypes
 */
static int cond_init(pthread_cond_t *cond, const pthread_condattr_t *attr);
static int cond_wait_common(pthread_cond_t *cond, pthread_mutex_t *mutex,
		    const struct timespec *abstime, int cancel);
static int cond_signal_common(pthread_cond_t *cond, int broadcast);

static int
cond_init(pthread_cond_t *cond, const pthread_condattr_t *cond_attr)
{
	pthread_cond_t	pcond;
	int             rval = 0;

	if ((pcond = (pthread_cond_t)
	    malloc(sizeof(struct pthread_cond))) == NULL) {
		rval = ENOMEM;
	} else {
		/*
		 * Initialise the condition variable structure:
		 */
		_thr_umtx_init(&pcond->c_lock);
		pcond->c_seqno = 0;
		pcond->c_waiters = 0;
		pcond->c_broadcast = 0;
		if (cond_attr == NULL || *cond_attr == NULL) {
			pcond->c_pshared = 0;
			pcond->c_clockid = CLOCK_REALTIME;
		} else {
			pcond->c_pshared = (*cond_attr)->c_pshared;
			pcond->c_clockid = (*cond_attr)->c_clockid;
		}
		*cond = pcond;
	}
	/* Return the completion status: */
	return (rval);
}

static int
init_static(struct pthread *thread, pthread_cond_t *cond)
{
	int ret;

	THR_LOCK_ACQUIRE(thread, &_cond_static_lock);

	if (*cond == NULL)
		ret = cond_init(cond, NULL);
	else
		ret = 0;

	THR_LOCK_RELEASE(thread, &_cond_static_lock);

	return (ret);
}

int
_pthread_cond_init(pthread_cond_t *cond, const pthread_condattr_t *cond_attr)
{
	*cond = NULL;
	return cond_init(cond, cond_attr);
}

int
_pthread_cond_destroy(pthread_cond_t *cond)
{
	struct pthread_cond	*cv;
	struct pthread		*curthread = tls_get_curthread();
	int			rval = 0;

	if (cond == NULL)
		rval = EINVAL;
	else if (*cond == NULL)
		rval = 0;
	else {
		/* Lock the condition variable structure: */
		THR_LOCK_ACQUIRE(curthread, &(*cond)->c_lock);
		if ((*cond)->c_waiters != 0) {
			THR_LOCK_RELEASE(curthread, &(*cond)->c_lock);
			return (EBUSY);
		}

		/*
		 * NULL the caller's pointer now that the condition
		 * variable has been destroyed:
		 */
		cv = *cond;
		*cond = NULL;

		/* Unlock the condition variable structure: */
		THR_LOCK_RELEASE(curthread, &cv->c_lock);

		/* Free the cond lock structure: */

		/*
		 * Free the memory allocated for the condition
		 * variable structure:
		 */
		free(cv);

	}
	/* Return the completion status: */
	return (rval);
}

struct cond_cancel_info
{
	pthread_mutex_t	*mutex;
	pthread_cond_t	*cond;
	int		seqno;
	int		count;	
};

static void
cond_cancel_handler(void *arg)
{
	struct pthread *curthread = tls_get_curthread();
	struct cond_cancel_info *info = (struct cond_cancel_info *)arg;
	pthread_cond_t cv;

	cv = *(info->cond);
	THR_LOCK_ACQUIRE(curthread, &cv->c_lock);
	if (--cv->c_waiters == 0)
		cv->c_broadcast = 0;
	if (cv->c_seqno != info->seqno) {
		_thr_umtx_wake(&cv->c_seqno, 1);
		/* cv->c_seqno++; XXX why was this here? */
		_thr_umtx_wake(&cv->c_seqno, 1);
	}
	THR_LOCK_RELEASE(curthread, &cv->c_lock);

	_mutex_cv_lock(info->mutex, info->count);
}

static int
cond_wait_common(pthread_cond_t *cond, pthread_mutex_t *mutex,
	const struct timespec *abstime, int cancel)
{
	struct pthread	*curthread = tls_get_curthread();
	struct timespec ts, ts2, *tsp;
	struct cond_cancel_info info;
	pthread_cond_t  cv;
	int		seq, oldseq;
	int		oldcancel;
	int		ret = 0;

	/*
	 * If the condition variable is statically initialized,
	 * perform the dynamic initialization:
	 */
	if (__predict_false(*cond == NULL &&
	    (ret = init_static(curthread, cond)) != 0))
		return (ret);

	cv = *cond;
/*	fprintf(stderr, "waiton1 %p\n", cv);*/
	THR_LOCK_ACQUIRE(curthread, &cv->c_lock);
	oldseq = cv->c_seqno;
/*	fprintf(stderr, "waiton2 %p %d\n", cv, oldseq);*/
	ret = _mutex_cv_unlock(mutex, &info.count);
	if (ret) {
		THR_LOCK_RELEASE(curthread, &cv->c_lock);
		return (ret);
	}
	seq = cv->c_seqno;
	info.mutex = mutex;
	info.cond  = cond;
	info.seqno = oldseq;

	++cv->c_waiters;

	/*
	 * loop if we have never been told to wake up
	 * or we lost a race.
	 */
	while (seq == oldseq /* || cv->c_wakeups == 0*/) {
		THR_LOCK_RELEASE(curthread, &cv->c_lock);

		if (abstime != NULL) {
			clock_gettime(cv->c_clockid, &ts);
			TIMESPEC_SUB(&ts2, abstime, &ts);
			tsp = &ts2;
		} else
			tsp = NULL;

		if (cancel) {
			THR_CLEANUP_PUSH(curthread, cond_cancel_handler, &info);
			oldcancel = _thr_cancel_enter(curthread);
			ret = _thr_umtx_wait(&cv->c_seqno, seq, tsp,
				cv->c_clockid);
			_thr_cancel_leave(curthread, oldcancel);
			THR_CLEANUP_POP(curthread, 0);
		} else {
			ret = _thr_umtx_wait(&cv->c_seqno, seq, tsp,
				cv->c_clockid);
		}

		THR_LOCK_ACQUIRE(curthread, &cv->c_lock);
		seq = cv->c_seqno;
		if (abstime != NULL && ret == ETIMEDOUT)
			break;
	}
	if (--cv->c_waiters == 0)
		cv->c_broadcast = 0;
	if (seq != oldseq)
		ret = 0;
	THR_LOCK_RELEASE(curthread, &cv->c_lock);
	_mutex_cv_lock(mutex, info.count);
	return (ret);
}

int
_pthread_cond_wait(pthread_cond_t *cond, pthread_mutex_t *mutex)
{

	return (cond_wait_common(cond, mutex, NULL, 0));
}

int
__pthread_cond_wait(pthread_cond_t *cond, pthread_mutex_t *mutex)
{

	return (cond_wait_common(cond, mutex, NULL, 1));
}

int
_pthread_cond_timedwait(pthread_cond_t * cond, pthread_mutex_t * mutex,
		       const struct timespec * abstime)
{
	if (abstime == NULL || abstime->tv_sec < 0 || abstime->tv_nsec < 0 ||
	    abstime->tv_nsec >= 1000000000)
		return (EINVAL);

	return (cond_wait_common(cond, mutex, abstime, 0));
}

int
__pthread_cond_timedwait(pthread_cond_t *cond, pthread_mutex_t *mutex,
		       const struct timespec *abstime)
{
	if (abstime == NULL || abstime->tv_sec < 0 || abstime->tv_nsec < 0 ||
	    abstime->tv_nsec >= 1000000000)
		return (EINVAL);

	return (cond_wait_common(cond, mutex, abstime, 1));
}

static int
cond_signal_common(pthread_cond_t *cond, int broadcast)
{
	struct pthread	*curthread = tls_get_curthread();
	pthread_cond_t	cv;
	int		ret = 0;

	/*
	 * If the condition variable is statically initialized, perform dynamic
	 * initialization.
	 */
	if (__predict_false(*cond == NULL &&
	    (ret = init_static(curthread, cond)) != 0))
		return (ret);

	cv = *cond;
/*	fprintf(stderr, "signal %p\n", cv);*/
	/* Lock the condition variable structure. */
	THR_LOCK_ACQUIRE(curthread, &cv->c_lock);
	cv->c_seqno++;
	if (cv->c_broadcast == 0)
		cv->c_broadcast = broadcast;

	if (cv->c_waiters) {
		if (cv->c_broadcast)
			_thr_umtx_wake(&cv->c_seqno, INT_MAX);
		else
			_thr_umtx_wake(&cv->c_seqno, 1);
	}
	THR_LOCK_RELEASE(curthread, &cv->c_lock);
	return (ret);
}

int
_pthread_cond_signal(pthread_cond_t * cond)
{

	return (cond_signal_common(cond, 0));
}

int
_pthread_cond_broadcast(pthread_cond_t * cond)
{

	return (cond_signal_common(cond, 1));
}

/*
 * Double underscore versions are cancellation points.  Single underscore
 * versions are not and are provided for libc internal usage (which
 * shouldn't introduce cancellation points).
 */
__strong_reference(__pthread_cond_wait, pthread_cond_wait);
__strong_reference(__pthread_cond_timedwait, pthread_cond_timedwait);

__strong_reference(_pthread_cond_init, pthread_cond_init);
__strong_reference(_pthread_cond_destroy, pthread_cond_destroy);
__strong_reference(_pthread_cond_signal, pthread_cond_signal);
__strong_reference(_pthread_cond_broadcast, pthread_cond_broadcast);

