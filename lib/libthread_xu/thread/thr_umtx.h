/*-
 * Copyright (c) 2005 David Xu <davidxu@freebsd.org>
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

#ifndef _THR_DFLY_UMTX_H_
#define _THR_DFLY_UMTX_H_

#include <unistd.h>

#define	cpu_pause()	__asm __volatile("pause":::"memory")

typedef int umtx_t;

int	__thr_umtx_lock(volatile umtx_t *mtx, int id, int timo);
int	__thr_umtx_timedlock(volatile umtx_t *mtx, int id,
		 const struct timespec *timeout);
void	__thr_umtx_unlock(volatile umtx_t *mtx, int v, int id);

static inline void
_thr_umtx_init(volatile umtx_t *mtx)
{
	*mtx = 0;
}

static inline int
_thr_umtx_trylock(volatile umtx_t *mtx, int id, int temporary)
{
	if (temporary)
		sigblockall();
	if (atomic_cmpset_acq_int(mtx, 0, id))
		return (0);
	cpu_pause();
	if (atomic_cmpset_acq_int(mtx, 0, id))
		return (0);
	cpu_pause();
	if (atomic_cmpset_acq_int(mtx, 0, id))
		return (0);
	if (temporary)
		sigunblockall();
	return (EBUSY);
}

static inline int
_thr_umtx_lock(volatile umtx_t *mtx, int id, int temporary)
{
	int res;

	if (temporary)
		sigblockall();
	if (atomic_cmpset_acq_int(mtx, 0, id))
		return (0);
	res = __thr_umtx_lock(mtx, id, 0);
	if (res && temporary)
		sigunblockall();
	return res;
}

static inline int
_thr_umtx_timedlock(volatile umtx_t *mtx, int id,
    const struct timespec *timeout, int temporary)
{
	int res;

	if (temporary)
		sigblockall();
	if (atomic_cmpset_acq_int(mtx, 0, id)) {
		return (0);
	}
	res = __thr_umtx_timedlock(mtx, id, timeout);
	if (res && temporary)
		sigunblockall();
	return res;
}

static inline void
_thr_umtx_unlock(volatile umtx_t *mtx, int id, int temporary)
{
	int v;

	v = atomic_swap_int(mtx, 0);
	if (v != id)
		__thr_umtx_unlock(mtx, v, id);
	if (temporary)
		sigunblockall();
}

int _thr_umtx_wait(volatile umtx_t *mtx, umtx_t exp,
		   const struct timespec *timeout, int clockid);
int _thr_umtx_wait_intr(volatile umtx_t *mtx, umtx_t exp);
void _thr_umtx_wake(volatile umtx_t *mtx, int count);
#endif
