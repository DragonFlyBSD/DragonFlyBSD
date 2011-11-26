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

#include <sys/thread2.h>

/*
 * Helper function to wait for a reference count to become zero.
 * We set REFCNTF_WAITING and sleep if the reference count is not zero.
 *
 * In the case where REFCNTF_WAITING is already set the atomic op validates
 * that it is still set after the tsleep_interlock() call.
 *
 * Users of this waiting API must use refcount_release_wakeup() to release
 * refs instead of refcount_release().  refcount_release() will not wake
 * up waiters.
 */
void
_refcount_wait(volatile u_int *countp, const char *wstr)
{
	u_int n;
	int loops = 0;
	int threshold = 5;

	for (;;) {
		n = *countp;
		cpu_ccfence();
		if (n == 0)
			break;
		if (loops > threshold) {
			kprintf("refcount_wait %s long wait\n", wstr);
			loops = 0;
		}
		KKASSERT(n != REFCNTF_WAITING);	/* impossible state */
		tsleep_interlock(countp, 0);
		if (atomic_cmpset_int(countp, n, n | REFCNTF_WAITING))
			tsleep(countp, PINTERLOCKED, wstr, hz*10);
		loops++;
	}
}

/*
 * This helper function implements the release-with-wakeup API.  It is
 * executed for the non-trivial case or if the atomic op races.
 *
 * On the i->0 transition is REFCNTF_WAITING is set it will be cleared
 * and a wakeup() will be issued.
 *
 * On any other transition we simply subtract (i) and leave the
 * REFCNTF_WAITING flag intact.
 *
 * This function returns TRUE(1) on the last release, whether a wakeup
 * occured or not, and FALSE(0) otherwise.
 *
 * NOTE!  (i) cannot be 0
 */
int
_refcount_release_wakeup_n(volatile u_int *countp, u_int i)
{
	u_int n;

	for (;;) {
		n = *countp;
		cpu_ccfence();
		if (n == (REFCNTF_WAITING | i)) {
			if (atomic_cmpset_int(countp, n, 0)) {
				wakeup(countp);
				n = i;
				break;
			}
		} else {
			KKASSERT(n != REFCNTF_WAITING); /* illegal state */
			if (atomic_cmpset_int(countp, n, n - i))
				break;
		}
	}
	return (n == i);
}
