/*-
 * Copyright (c) 2005 John Baldwin <jhb@FreeBSD.org>
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
 * 3. Neither the name of the author nor the names of any co-contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
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
 * $FreeBSD: src/sys/sys/refcount.h,v 1.1 2005/09/27 18:01:33 jhb Exp $
 */

#ifndef _SYS_REFCOUNT_H_
#define _SYS_REFCOUNT_H_

#include <machine/atomic.h>

#define REFCNTF_WAITING	0x40000000

void _refcount_wait(volatile u_int *countp, const char *wstr);
int _refcount_release_wakeup_n(volatile u_int *countp, u_int i);

static __inline void
refcount_init(volatile u_int *countp, u_int value)
{
	*countp = value;
}

static __inline void
refcount_acquire(volatile u_int *countp)
{
	atomic_add_acq_int(countp, 1);
}

static __inline void
refcount_acquire_n(volatile u_int *countp, u_int i)
{
	atomic_add_acq_int(countp, i);
}

static __inline int
refcount_release(volatile u_int *countp)
{
	return (atomic_fetchadd_int(countp, -1) == 1);
}

static __inline int
refcount_release_n(volatile u_int *countp, u_int i)
{
	return (atomic_fetchadd_int(countp, -i) == i);
}

/*
 * Release a refcount and also handle waiters who have (atomically)
 * set the REFCNTF_WAITING flag.  If the flag was set the atomic op
 * will fail (because the 'old' value we pass it is with the flag
 * cleared).  The atomic op can also fail on a race so the helper
 * function deals with all cases.
 *
 * This function returns TRUE(1) on the last release and FALSE(0) otherwise.
 *
 * NOTE: (i) must be non-zero.
 */
static __inline int
refcount_release_wakeup(volatile u_int *countp)
{
	u_int n = *countp & ~REFCNTF_WAITING;
	if (!atomic_cmpset_int(countp, n, n - 1))
		return(_refcount_release_wakeup_n(countp, 1));
	return(n == 1);
}

static __inline int
refcount_release_wakeup_n(volatile u_int *countp, u_int i)
{
	u_int n = *countp & ~REFCNTF_WAITING;
	if (!atomic_cmpset_int(countp, n, n - i))
		return(_refcount_release_wakeup_n(countp, i));
	return(n == i);
}

/*
 * Wait for all refs on *countp to go away.
 *
 * WARNING!  If this function is used then all releases on countp MUST
 *	     use refcount_release_wakeup() instead of refcount_release().
 */
static __inline void
refcount_wait(volatile u_int *countp, const char *wstr)
{
	if (*countp)
		_refcount_wait(countp, wstr);
}

#endif	/* ! _SYS_REFCOUNT_H_ */
