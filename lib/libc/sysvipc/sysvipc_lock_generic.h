/*-
 * Copyright (c) 2003 David Xu <davidxu@freebsd.org>
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
 * $DragonFly: src/lib/libthread_xu/thread/thr_umtx.h,v 1.5 2008/04/14 20:12:41 dillon Exp $
 * $DragonFly: src/lib/libsysvipc/lock_generic.h,v 1 2013/09/20 Larisa Grigore<lariisagrigore@gmail.com>
 */

#ifndef _SYSV_DFLY_UMTX_H_
#define _SYSV_DFLY_UMTX_H_

#include <unistd.h>
#include <machine/atomic.h>

#define SYSV_TIMEOUT		4000

typedef int umtx_t;

int	__sysv_umtx_lock(volatile umtx_t *mtx, int timo);
void	__sysv_umtx_unlock(volatile umtx_t *mtx);

static inline void
_sysv_umtx_init(volatile umtx_t *mtx)
{
    *mtx = 0;
}

static inline int
_sysv_umtx_lock(volatile umtx_t *mtx)
{
    if (atomic_cmpset_acq_int(mtx, 0, 1))
	return (0);
    return (__sysv_umtx_lock(mtx, 0));
}

static inline void
_sysv_umtx_unlock(volatile umtx_t *mtx)
{
    if (atomic_cmpset_acq_int(mtx, 1, 0))
	return;
    __sysv_umtx_unlock(mtx);
}

#endif
