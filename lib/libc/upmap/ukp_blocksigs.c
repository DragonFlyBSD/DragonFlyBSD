/*
 * Copyright (c) 2019 The DragonFly Project.  All rights reserved.
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

#include "namespace.h"
#include <sys/cdefs.h>
#include <sys/types.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <machine/cpufunc.h>
#include <machine/atomic.h>
#include <errno.h>
#include <time.h>
#include <stdio.h>
#include <unistd.h>
#include "un-namespace.h"
#include "libc_private.h"
#include "upmap.h"

int _sigblockall(void);
int _sigunblockall(void);
int __getpid(void);
pid_t __sys_getpid(void);

/*
 * Block all maskable signals to this thread.  Does not prevent signal
 * delivery to the thread's pending signal mask, but does prevent signal
 * dispatch.
 *
 * Reentrant, returns the reentrancy count.  First call will thus return
 * 1.  -1 is returned if the feature is not supported.
 *
 * The pointer is set-up on program startup and will remain valid on fork()
 * (the kernel will re-fault the proper page).  On vfork(), however, the
 * pointer points to the parent thread's mapping.
 */
int
_sigblockall(void)
{
	if (__lpmap_blockallsigs)
		return atomic_fetchadd_int(__lpmap_blockallsigs, 1) + 1;
	return -1;
}

/*
 * Reverse the effects of sigblockall().  Returns the reentrancy count
 * after decrement.  -1 is returned if not supported.
 *
 * Bit 31 is set on return if signals were received while blocked.  Upon
 * last unblock this bit also indicates that pending service routines ran.
 * The bit is cumulative until the last unblock occurs.
 */
int
_sigunblockall(void)
{
	uint32_t bas;

	if (__lpmap_blockallsigs) {
		bas = atomic_fetchadd_int(__lpmap_blockallsigs, -1) - 1;
		if (bas == 0x80000000U) {
			(void)__sys_getpid();	/* force signal processing */
			atomic_clear_int(__lpmap_blockallsigs, 0x80000000U);
		}
		return (int)bas;
	}
	return -1;
}

__weak_reference(_sigblockall, sigblockall);
__weak_reference(_sigunblockall, sigunblockall);
