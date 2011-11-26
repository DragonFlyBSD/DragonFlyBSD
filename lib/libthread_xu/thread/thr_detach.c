/*
 * Copyright (c) 1995 John Birrell <jb@cimlogic.com.au>.
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by John Birrell.
 * 4. Neither the name of the author nor the names of any co-contributors
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
 *
 * $DragonFly: src/lib/libthread_xu/thread/thr_detach.c,v 1.4 2006/04/06 13:03:09 davidxu Exp $
 */

#include "namespace.h"
#include <machine/tls.h>

#include <errno.h>
#include <pthread.h>
#include "un-namespace.h"

#include "thr_private.h"

int
_pthread_detach(pthread_t pthread)
{
	struct pthread *curthread = tls_get_curthread();
	int rval;

	if (pthread == NULL)
		return (EINVAL);

	THREAD_LIST_LOCK(curthread);
	if ((rval = _thr_find_thread(curthread, pthread,
			/*include dead*/1)) != 0) {
		THREAD_LIST_UNLOCK(curthread);
		return (rval);
	}

	/* Check if the thread is already detached or has a joiner. */
	if ((pthread->tlflags & TLFLAGS_DETACHED) != 0 ||
	    (pthread->joiner != NULL)) {
		THREAD_LIST_UNLOCK(curthread);
		return (EINVAL);
	}

	/* Flag the thread as detached. */
	pthread->tlflags |= TLFLAGS_DETACHED;
	if (pthread->state == PS_DEAD)
		THR_GCLIST_ADD(pthread);
	THREAD_LIST_UNLOCK(curthread);

	return (0);
}

__strong_reference(_pthread_detach, pthread_detach);
