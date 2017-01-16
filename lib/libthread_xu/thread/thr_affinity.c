/*
 * Copyright (c) 1995-1998 John Birrell <jb@cimlogic.com.au>
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
 */

#include "namespace.h"
#include <sys/lwp.h>
#include <errno.h>
#include <string.h>
#include <pthread.h>
#include <pthread_np.h>
#include "un-namespace.h"

#include "thr_private.h"

int
_pthread_getaffinity_np(pthread_t thread, size_t cpusetsize,
    cpu_set_t *mask)
{
	struct pthread *curthread = tls_get_curthread();
	cpu_set_t mask1;
	int ret;

	if (curthread == thread) {
		ret = 0;
		if (lwp_getaffinity(0, thread->tid, &mask1) < 0)
			ret = errno;
	} else {
		ret = ESRCH;
		if (_thr_ref_add(curthread, thread, 0) == 0) {
			THR_THREAD_LOCK(curthread, thread);
			if (thread->state != PS_DEAD) {
				ret = 0;
				if (lwp_getaffinity(0, thread->tid, &mask1) < 0)
					ret = errno;
			}
			THR_THREAD_UNLOCK(curthread, thread);
			_thr_ref_delete(curthread, thread);
		}
	}
	if (ret != 0)
		return (ret);

	if (cpusetsize > sizeof(mask1)) {
		memset(mask, 0, cpusetsize);
		memcpy(mask, &mask1, sizeof(mask1));
	} else {
		memcpy(mask, &mask1, cpusetsize);
	}
	return (0);
}
__strong_reference(_pthread_getaffinity_np, pthread_getaffinity_np);

int
_pthread_setaffinity_np(pthread_t thread, size_t cpusetsize,
    const cpu_set_t *mask)
{
	struct pthread *curthread = tls_get_curthread();
	size_t cplen = cpusetsize;
	cpu_set_t mask1;
	int ret;

	if (cplen > sizeof(mask1))
		cplen = sizeof(mask1);
	CPU_ZERO(&mask1);
	memcpy(&mask1, mask, cplen);

	if (curthread == thread) {
		ret = 0;
		if (lwp_setaffinity(0, thread->tid, &mask1) < 0)
			ret = errno;
	} else {
		ret = ESRCH;
		if (_thr_ref_add(curthread, thread, 0) == 0) {
			THR_THREAD_LOCK(curthread, thread);
			if (thread->state != PS_DEAD) {
				ret = 0;
				if (lwp_setaffinity(0, thread->tid, &mask1) < 0)
					ret = errno;
			}
			THR_THREAD_UNLOCK(curthread, thread);
			_thr_ref_delete(curthread, thread);
		}
	}
	return (ret);
}
__strong_reference(_pthread_setaffinity_np, pthread_setaffinity_np);
