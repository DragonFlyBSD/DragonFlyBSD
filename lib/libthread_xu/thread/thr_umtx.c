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
 * $DragonFly: src/lib/libthread_xu/thread/thr_umtx.c,v 1.4 2008/04/14 20:12:41 dillon Exp $
 */

#include <assert.h>
#include <errno.h>
#include <unistd.h>
#include <sys/time.h>

#include "thr_private.h"

/*
 * This function is used to acquire a contested lock.
 */
int
__thr_umtx_lock(volatile umtx_t *mtx, int timo)
{
	int v, errval, ret = 0;

	/* contested */
	do {
		v = *mtx;
		if (v == 2 || atomic_cmpset_acq_int(mtx, 1, 2)) {
			if (timo == 0)
				_umtx_sleep_err(mtx, 2, timo);
			else if ( (errval = _umtx_sleep_err(mtx, 2, timo)) > 0) {
				if (errval == EAGAIN) {
					if (atomic_cmpset_acq_int(mtx, 0, 2))
						ret = 0;
					else
						ret = ETIMEDOUT;
					break;
				}
			}
		}
	} while (!atomic_cmpset_acq_int(mtx, 0, 2));

	return (ret);
}

void
__thr_umtx_unlock(volatile umtx_t *mtx)
{
	int v;

	for (;;) {
		v = *mtx;
		if (atomic_cmpset_acq_int(mtx, v, v-1)) {
			if (v != 1) {
				*mtx = 0;
				_umtx_wakeup_err(mtx, 1);
			}
			break;
		}
	}
}

/*
 * Low level timed umtx lock.  This function must never return
 * EINTR.
 */
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
	if (ret != EINTR && ret != ETIMEDOUT)
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
_thr_umtx_wait(volatile umtx_t *mtx, int exp, const struct timespec *timeout,
	       int clockid)
{
    struct timespec ts, ts2, ts3;
    int timo, errval, ret = 0;

    if (*mtx != exp)
	return (0);

    if (timeout == NULL) {
	while ( (errval = _umtx_sleep_err(mtx, exp, 10000000)) > 0) {
	    if (errval == EBUSY)
		break;
	    if (errval == EINTR) {
		ret = EINTR;
		break;
	    }
#if 0
	    if (errval == ETIMEDOUT || errval == EWOULDBLOCK) {
		if (*mtx != exp) {
		    fprintf(stderr,
			    "thr_umtx_wait: FAULT VALUE CHANGE "
			    "%d -> %d oncond %p\n",
			    exp, *mtx, mtx);
		}
	    }
#endif
	    if (*mtx != exp)
		return(0);
	}
	return (ret);
    }

    if ((timeout->tv_sec < 0) ||
        (timeout->tv_sec == 0 && timeout->tv_nsec <= 0))
	return (ETIMEDOUT);

    clock_gettime(clockid, &ts);
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
	if ( (errval = _umtx_sleep_err(mtx, exp, timo)) > 0) {
	    if (errval == EBUSY) {
		ret = 0;
		break;
	    } else if (errval == EINTR) {
		ret = EINTR;
		break;
	    }
	}
	clock_gettime(clockid, &ts3);
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
    _umtx_wakeup_err(mtx, count);
}
