/*-
 * Copyright (c) 2005 David Xu <davidxu@freebsd.org>
 * Copyright (c) 2005 Matthew Dillon <dillon@backplane.com>
 *
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
 * $DragonFly: src/lib/libthread_xu/thread/thr_umtx.c,v 1.1 2005/02/01 12:38:27 davidxu Exp $
 */

/*
 * Part of these code is derived from /usr/src/test/debug/umtx.c.
 */

#include <assert.h>
#include <errno.h>
#include <unistd.h>
#include <sys/time.h>

#include "thr_private.h"

static int get_contested(volatile umtx_t *mtx, int timo);

int
__thr_umtx_lock(volatile umtx_t *mtx, int timo)
{
    int v;
    int ret;

    for (;;) {
	v = *mtx;
	if ((v & UMTX_LOCKED) == 0) {
	    /* not locked, attempt to lock. */
	    if (atomic_cmpset_acq_int(mtx, v, v | UMTX_LOCKED)) {
		ret = 0;
		break;
	    }
	} else {
	    /*
	     * Locked, bump the contested count and obtain
	     * the contested mutex.
	     */
	    if (atomic_cmpset_acq_int(mtx, v, v + 1)) {
		ret = get_contested(mtx, timo);
		break;
	    }
	}
    }

    return (ret);
}

static int
get_contested(volatile umtx_t *mtx, int timo)
{
    int ret = 0;
    int v;

    for (;;) {
	v = *mtx;
	assert(v & ~UMTX_LOCKED); /* our contesting count still there */
	if ((v & UMTX_LOCKED) == 0) {
	    /*
	     * Not locked, attempt to remove our contested
	     * count and lock at the same time.
	     */
	    if (atomic_cmpset_acq_int(mtx, v, (v - 1) | UMTX_LOCKED)) {
		ret = 0;
		break;
	    }
	} else {
	    /*
	     * Retried after resuming from umtx_sleep, try to leave if there
	     * was error, e.g, timeout.
	     */
	    if (ret) {
		if (atomic_cmpset_acq_int(mtx, v, v - 1))
			break;
		else
			continue;
	    }

	    /*
	     * Still locked, sleep and try again.
	     */
	    if (timo == 0) {
		umtx_sleep(mtx, v, 0);
	    } else {
		if (umtx_sleep(mtx, v, timo) < 0) {
		    if (errno == EAGAIN)
			ret = ETIMEDOUT;
		}
	    }
	}
    }

    return (ret);
}

void
__thr_umtx_unlock(volatile umtx_t *mtx)
{
    int v;

    for (;;) {
	v = *mtx;
	assert(v & UMTX_LOCKED);	/* we still have it locked */
	if (v == UMTX_LOCKED) {
	    /*
	     * We hold an uncontested lock, try to set to an unlocked
	     * state.
	     */
	    if (atomic_cmpset_acq_int(mtx, UMTX_LOCKED, 0))
		return;
	} else {
	    /*
	     * We hold a contested lock, unlock and wakeup exactly
	     * one sleeper. It is possible for this to race a new
	     * thread obtaining a lock, in which case any contested
	     * sleeper we wake up will simply go back to sleep.
	     */
	    if (atomic_cmpset_acq_int(mtx, v, v & ~UMTX_LOCKED)) {
		umtx_wakeup(mtx, 1);
		return;
	    }
	}
    }
}

int
__thr_umtx_timedlock(volatile umtx_t *mtx, const struct timespec *timeout)
{
    struct timespec ts, ts2, ts3;
    int timo, ret;

    if ((timeout->tv_sec < 0) ||
        (timeout->tv_sec == 0 && timeout->tv_nsec <= 0))
	return (ETIMEDOUT);

    /* XXX there should have MONO timer! */
    clock_gettime(CLOCK_REALTIME, &ts);
    TIMESPEC_ADD(&ts, &ts, timeout);
    ts2 = *timeout;

    for (;;) {
    	if (ts2.tv_nsec) {
	    timo = (int)(ts2.tv_nsec / 1000);
	    if (timo == 0)
		timo = 1;
	} else {
	    timo = 1000000;
	}
	ret = __thr_umtx_lock(mtx, timo);
	if (ret != ETIMEDOUT)
	    break;
	clock_gettime(CLOCK_REALTIME, &ts3);
	TIMESPEC_SUB(&ts2, &ts, &ts3);
	if (ts2.tv_sec < 0 || (ts2.tv_sec == 0 && ts2.tv_nsec <= 0)) {
	    ret = ETIMEDOUT;
	    break;
	}
    }
    return (ret);
}

int
_thr_umtx_wait(volatile umtx_t *mtx, int exp, const struct timespec *timeout)
{
    struct timespec ts, ts2, ts3;
    int timo, ret = 0;

    if (*mtx != exp)
	return (0);

    if (timeout == NULL) {
	if (umtx_sleep(mtx, exp, 0) < 0) {
	    if (errno == EINTR)
		ret = EINTR;
	}
	return (ret);
    }

    if ((timeout->tv_sec < 0) ||
        (timeout->tv_sec == 0 && timeout->tv_nsec <= 0))
	return (ETIMEDOUT);

    /* XXX there should have MONO timer! */
    clock_gettime(CLOCK_REALTIME, &ts);
    TIMESPEC_ADD(&ts, &ts, timeout);
    ts2 = *timeout;

    for (;;) {
    	if (ts2.tv_nsec) {
	    timo = (int)(ts2.tv_nsec / 1000);
	    if (timo == 0)
		timo = 1;
	} else {
	    timo = 1000000;
	}
	if (umtx_sleep(mtx, exp, timo) < 0) {
	    if (errno == EBUSY) {
		ret = 0;
		break;
	    } else if (errno == EINTR) {
		ret = EINTR;
		break;
	    }
	}
	clock_gettime(CLOCK_REALTIME, &ts3);
	TIMESPEC_SUB(&ts2, &ts, &ts3);
	if (ts2.tv_sec < 0 || (ts2.tv_sec == 0 && ts2.tv_nsec <= 0)) {
	    ret = ETIMEDOUT;
	    break;
	}
    }
    return (ret);
}

void _thr_umtx_wake(volatile umtx_t *mtx, int count)
{
    umtx_wakeup(mtx, count);
}
