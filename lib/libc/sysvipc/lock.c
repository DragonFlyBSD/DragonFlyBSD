/*
 * Copyright (c) 1995 John Birrell <jb@cimlogic.com.au>.
 * Copyright (c) 1998 Alex Nash
 * Copyright (c) 2006 David Xu <yfxu@corp.netease.com>.
 * Copyright (c) 2013 Larisa Grigore <larisagrigore@gmail.com>.
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by John Birrell.
 * 4. Neither the name of the author nor the names of any co-contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY JOHN BIRRELL AND CONTRIBUTORS ``AS IS'' AND
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
 * $DragonFly: src/lib/libthread_xu/thread/thr_mutex.c,v 1.15 2008/05/09 16:03:27 dillon Exp $
 * $FreeBSD: src/lib/libpthread/thread/thr_rwlock.c,v 1.14 2004/01/08 15:37:09 deischen Exp $
 * $DragonFly: src/lib/libthread_xu/thread/thr_rwlock.c,v 1.7 2006/04/06 13:03:09 davidxu Exp $
 */

#include <machine/atomic.h>
#include <machine/tls.h>
#include <errno.h>

#include "sysvipc_utils.h"
#include "sysvipc_lock.h"
#include "sysvipc_lock_generic.h"

#include <limits.h>
#include <stdio.h>
#include <unistd.h>

#define MAX_READ_LOCKS          (INT_MAX - 1)

static int rdlock_count;

int
sysv_mutex_init(struct sysv_mutex *mutex) {
	if(mutex == NULL)
		return (EINVAL);
	mutex->_mutex_static_lock = 0;
	mutex->pid_owner = -1;
	mutex->tid_owner = -1;
	return (0);
}

int
sysv_mutex_lock(struct sysv_mutex *mutex)
{
	if (mutex->pid_owner == getpid() &&
			mutex->tid_owner == lwp_gettid()) {
		sysv_print_err("deadlock: mutex aleady acquired by the thread\n");
		return (EDEADLK);
	}
	_sysv_umtx_lock(&mutex->_mutex_static_lock);
	mutex->pid_owner = getpid();
	mutex->tid_owner = lwp_gettid();
	return (0);
}

int
sysv_mutex_unlock(struct sysv_mutex *mutex)
{
	if (mutex->pid_owner != getpid() ||
			mutex->tid_owner != lwp_gettid()) {
		sysv_print_err("eperm try unlock a mutex that is not acquired\n");
		return (EPERM);
	}

	mutex->tid_owner = -1;
	mutex->pid_owner = -1;
	_sysv_umtx_unlock(&mutex->_mutex_static_lock);
	return (0);
}

static int
sysv_cond_wait(int *val, struct sysv_mutex *mutex) {
	sysv_mutex_unlock(mutex);

	/* I use SYSV_TIMEOUT to avoid lossing a wakeup
	 * sent before going to sleep and remain blocked.
	 */
	umtx_sleep(val, *val, SYSV_TIMEOUT);
	return (sysv_mutex_lock(mutex));
}

static int
sysv_cond_signal(int *val) {
	return (umtx_wakeup(val, 0));
}

int
sysv_rwlock_init(struct sysv_rwlock *rwlock)
{
	int ret;

	if (rwlock == NULL)
		return (EINVAL);

	/* Initialize the lock. */
	sysv_mutex_init(&rwlock->lock);
	rwlock->state = 0;
	rwlock->blocked_writers = 0;

	return (ret);
}

int
sysv_rwlock_unlock (struct sysv_rwlock *rwlock)
{
	int ret;

	if (rwlock == NULL)
		return (EINVAL);

	/* Grab the monitor lock. */
	if ((ret = sysv_mutex_lock(&rwlock->lock)) != 0)
		return (ret);

	if (rwlock->state > 0) {
		rdlock_count--;
		rwlock->state--;
		if (rwlock->state == 0 && rwlock->blocked_writers) {
			ret = sysv_cond_signal(&rwlock->write_signal);
		}
	} else if (rwlock->state < 0) {
		rwlock->state = 0;

		if (rwlock->blocked_writers) {
			ret = sysv_cond_signal(&rwlock->write_signal);
		}
		else {
			ret = sysv_cond_signal(&rwlock->read_signal);
		}
	} else
		ret = EINVAL;

	sysv_mutex_unlock(&rwlock->lock);

	return (ret);
}

int
sysv_rwlock_wrlock (struct sysv_rwlock *rwlock)
{
	int ret;

	if (rwlock == NULL)
		return (EINVAL);

	/* Grab the monitor lock. */
	if ((ret = sysv_mutex_lock(&rwlock->lock)) != 0)
		return (ret);

	while (rwlock->state != 0) {
		rwlock->blocked_writers++;

		ret = sysv_cond_wait(&rwlock->write_signal, &rwlock->lock);
		if (ret != 0) {
			rwlock->blocked_writers--;
			/* No unlock is required because only the lock
			 * operation can return error.
			 */
			//sysv_mutex_unlock(&rwlock->lock);
			return (ret);
		}

		rwlock->blocked_writers--;
	}

	/* Indicate that we are locked for writing. */
	rwlock->state = -1;

	sysv_mutex_unlock(&rwlock->lock);

	return (ret);
}

int
sysv_rwlock_rdlock(struct sysv_rwlock *rwlock)
{
	int ret;

//	sysv_print("try get rd lock\n");
	if (rwlock == NULL)
		return (EINVAL);

	/* Grab the monitor lock. */
	if ((ret = sysv_mutex_lock(&rwlock->lock)) != 0)
		return (ret);

	/* Check the lock count. */
	if (rwlock->state == MAX_READ_LOCKS) {
		sysv_mutex_unlock(&rwlock->lock);
		return (EAGAIN);
	}

	if ((rdlock_count > 0) && (rwlock->state > 0)) {
		/*
		 * Taken from the pthread implementation with only
		 * one change; rdlock_count is per process not per
		 * thread;
		 * Original comment:
		 * To avoid having to track all the rdlocks held by
		 * a thread or all of the threads that hold a rdlock,
		 * we keep a simple count of all the rdlocks held by
		 * a thread.  If a thread holds any rdlocks it is
		 * possible that it is attempting to take a recursive
		 * rdlock.  If there are blocked writers and precedence
		 * is given to them, then that would result in the thread
		 * deadlocking.  So allowing a thread to take the rdlock
		 * when it already has one or more rdlocks avoids the
		 * deadlock.  I hope the reader can follow that logic ;-)
		 */
		;	/* nothing needed */
	} else {
		/* Give writers priority over readers. */
		while (rwlock->blocked_writers || rwlock->state < 0) {
			ret = sysv_cond_wait(&rwlock->read_signal,
			   &rwlock->lock);
			if (ret != 0) {
				/* No unlock necessary because only lock
				 * operation can return error.
				 */
				//sysv_mutex_unlock(&rwlock->lock);
				return (ret);
			}
		}
	}

	rdlock_count++;
	rwlock->state++; /* Indicate we are locked for reading. */

	/*
	 * Something is really wrong if this call fails.  Returning
	 * error won't do because we've already obtained the read
	 * lock.  Decrementing 'state' is no good because we probably
	 * don't have the monitor lock.
	 */
	sysv_mutex_unlock(&rwlock->lock);

	return (ret);
}
