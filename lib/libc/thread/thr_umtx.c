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
 */

#include <assert.h>
#include <errno.h>
#include <unistd.h>
#include <sys/time.h>

#include "thr_private.h"

#define cpu_ccfence()	__asm __volatile("" : : : "memory")

/*
 * This function is used to acquire a contested lock.
 *
 * There is a performance trade-off between spinning and sleeping.  In
 * a heavily-multi-threaded program, heavily contested locks that are
 * sleeping and waking up create a large IPI load on the system.  For
 * example, qemu with a lot of CPUs configured.  It winds up being much
 * faster to spin instead.
 *
 * So the first optimization here is to hard loop in-scale with the number
 * of therads.
 *
 * The second optimization is to wake-up just one waiter at a time.  This
 * is frought with issues because waiters can abort and races can result in
 * nobody being woken up to acquire the released lock, so to smooth things
 * over sleeps are limited to 1mS before we retry.
 */
int
__thr_umtx_lock(volatile umtx_t *mtx, int id, int timo)
{
	int v;
	int errval;
	int ret = 0;
	int retry = _thread_active_threads * 200 + 10;

	v = *mtx;
	cpu_ccfence();
	id &= 0x3FFFFFFF;

	for (;;) {
		cpu_pause();
		if (v == 0) {
			if (atomic_fcmpset_int(mtx, &v, id))
				break;
			continue;
		}
		if (--retry) {
			v = *mtx;
			continue;
		}

		/*
		 * Set the waiting bit.  If the fcmpset fails v is loaded
		 * with the current content of the mutex, and if the waiting
		 * bit is already set, we can also sleep.
		 */
		if (atomic_fcmpset_int(mtx, &v, v|0x40000000) ||
		    (v & 0x40000000)) {
			if (timo == 0) {
				_umtx_sleep_err(mtx, v|0x40000000, 1000);
			} else if (timo > 1500) {
				/*
				 * Short sleep and retry.  Because umtx
				 * ops can timeout and abort, wakeup1()
				 * races can cause a wakeup to be missed.
				 */
				_umtx_sleep_err(mtx, v|0x40000000, 1000);
				timo -= 1000;
			} else {
				/*
				 * Final sleep, do one last attempt to get
				 * the lock before giving up.
				 */
				errval = _umtx_sleep_err(mtx, v|0x40000000,
							 timo);
				if (__predict_false(errval == EAGAIN)) {
					if (atomic_cmpset_acq_int(mtx, 0, id))
						ret = 0;
					else
						ret = ETIMEDOUT;
					break;
				}
			}
		}
		retry = _thread_active_threads * 200 + 10;
	}
	return (ret);
}

/*
 * Inline followup when releasing a mutex.  The mutex has been released
 * but 'v' either doesn't match id or needs a wakeup.
 */
void
__thr_umtx_unlock(volatile umtx_t *mtx, int v, int id)
{
	if (v & 0x40000000) {
		_umtx_wakeup_err(mtx, 1);
		v &= 0x3FFFFFFF;
	}
	THR_ASSERT(v == id, "thr_umtx_unlock: wrong owner");
}

/*
 * Low level timed umtx lock.  This function must never return
 * EINTR.
 */
int
__thr_umtx_timedlock(volatile umtx_t *mtx, int id,
		     const struct timespec *timeout)
{
	struct timespec ts, ts2, ts3;
	int timo, ret;

	if ((timeout->tv_sec < 0) ||
	    (timeout->tv_sec == 0 && timeout->tv_nsec <= 0)) {
		return (ETIMEDOUT);
	}

	/* XXX there should have MONO timer! */
	clock_gettime(CLOCK_REALTIME, &ts);
	timespecadd(&ts, timeout, &ts);
	ts2 = *timeout;

	id &= 0x3FFFFFFF;

	for (;;) {
		if (ts2.tv_nsec) {
			timo = (int)(ts2.tv_nsec / 1000);
			if (timo == 0)
				timo = 1;
		} else {
			timo = 1000000;
		}
		ret = __thr_umtx_lock(mtx, id, timo);
		if (ret != EINTR && ret != ETIMEDOUT)
			break;
		clock_gettime(CLOCK_REALTIME, &ts3);
		timespecsub(&ts, &ts3, &ts2);
		if (ts2.tv_sec < 0 ||
		    (ts2.tv_sec == 0 && ts2.tv_nsec <= 0)) {
			ret = ETIMEDOUT;
			break;
		}
	}
	return (ret);
}

/*
 * Regular umtx wait that cannot return EINTR
 */
int
_thr_umtx_wait(volatile umtx_t *mtx, int exp, const struct timespec *timeout,
	       int clockid)
{
	struct timespec ts, ts2, ts3;
	int timo, errval, ret = 0;

	cpu_ccfence();
	if (*mtx != exp)
		return (0);

	if (timeout == NULL) {
		/*
		 * NOTE: If no timeout, EINTR cannot be returned.  Ignore
		 *	 EINTR.
		 */
		while ((errval = _umtx_sleep_err(mtx, exp, 10000000)) > 0) {
			if (errval == EBUSY)
				break;
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

	/*
	 * Timed waits can return EINTR
	 */
	if ((timeout->tv_sec < 0) ||
	    (timeout->tv_sec == 0 && timeout->tv_nsec <= 0))
	return (ETIMEDOUT);

	clock_gettime(clockid, &ts);
	timespecadd(&ts, timeout, &ts);
	ts2 = *timeout;

	for (;;) {
		if (ts2.tv_nsec) {
			timo = (int)(ts2.tv_nsec / 1000);
			if (timo == 0)
				timo = 1;
		} else {
			timo = 1000000;
		}

		if ((errval = _umtx_sleep_err(mtx, exp, timo)) > 0) {
			if (errval == EBUSY) {
				ret = 0;
				break;
			}
			if (errval == EINTR) {
				ret = EINTR;
				break;
			}
		}

		clock_gettime(clockid, &ts3);
		timespecsub(&ts, &ts3, &ts2);
		if (ts2.tv_sec < 0 || (ts2.tv_sec == 0 && ts2.tv_nsec <= 0)) {
			ret = ETIMEDOUT;
			break;
		}
	}
	return (ret);
}

/*
 * Simple version without a timeout which can also return EINTR
 */
int
_thr_umtx_wait_intr(volatile umtx_t *mtx, int exp)
{
	int ret = 0;
	int errval;

	cpu_ccfence();
	for (;;) {
		if (*mtx != exp)
			return (0);
		errval = _umtx_sleep_err(mtx, exp, 10000000);
		if (errval == 0)
			break;
		if (errval == EBUSY)
			break;
		if (errval == EINTR) {
			ret = errval;
			break;
		}
		cpu_ccfence();
	}
	return (ret);
}

void
_thr_umtx_wake(volatile umtx_t *mtx, int count)
{
	_umtx_wakeup_err(mtx, count);
}
