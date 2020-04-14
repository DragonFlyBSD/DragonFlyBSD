/*
 * Copyright (c) 2011 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Matthew Dillon <dillon@backplane.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name of The DragonFly Project nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific, prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
/*
 * Implement helper routines for the refcount inlines in sys/refcount.h.
 *
 * These helpers implement the refcount_release_wakeup() and refcount_wait()
 * APIs for the non-trivial or race case.  The trivial non-race case is
 * handled by the inline in sys/refcount.h
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/sysctl.h>
#include <sys/thread.h>
#include <sys/refcount.h>

#include <machine/cpufunc.h>

/*
 * Interlocked wait against a decrement-to-0 (sans the REFCNTF_WAITING flag).
 *
 * Users of this waiting API must use refcount_release_wakeup() to release
 * refs instead of refcount_release().  refcount_release() will not wake
 * up waiters.
 */
void
_refcount_wait(volatile u_int *countp, const char *wstr)
{
	u_int n;
	int base_ticks = ticks;

	n = *countp;
	for (;;) {
		cpu_ccfence();
		if ((n & ~REFCNTF_WAITING) == 0)
			break;
		if ((int)(ticks - base_ticks) >= hz*60 - 1) {
			kprintf("warning: refcount_wait %s: long wait\n", wstr);
			base_ticks = ticks;
		}
		tsleep_interlock(countp, 0);
		if (atomic_fcmpset_int(countp, &n, n | REFCNTF_WAITING))
			tsleep(countp, PINTERLOCKED, wstr, hz*10);
	}
}
