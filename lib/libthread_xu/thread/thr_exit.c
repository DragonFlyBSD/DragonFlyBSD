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
 *
 * $FreeBSD: src/lib/libpthread/thread/thr_exit.c,v 1.39 2004/10/23 23:37:54 davidxu Exp $
 */

#include "namespace.h"
#include <machine/tls.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include "un-namespace.h"

#include "thr_private.h"

static void	exit_thread(void) __dead2;

void
_thread_exitf(const char *fname, int lineno, const char *fmt, ...)
{
	va_list ap;

	/* Write an error message to the standard error file descriptor: */
	_thread_printf(STDERR_FILENO, "Fatal error '");

	va_start(ap, fmt);
	_thread_vprintf(STDERR_FILENO, fmt, ap);
	va_end(ap);

	_thread_printf(STDERR_FILENO, "' at line %d in file %s (errno = %d)\n",
	    lineno, fname, errno);

	abort();
}

void
_thread_exit(const char *fname, int lineno, const char *msg)
{
	_thread_exitf(fname, lineno, "%s", msg);
}

/*
 * Only called when a thread is cancelled.  It may be more useful
 * to call it from pthread_exit() if other ways of asynchronous or
 * abnormal thread termination can be found.
 */
void
_thr_exit_cleanup(void)
{
	struct pthread	*curthread = tls_get_curthread();

	/*
	 * POSIX states that cancellation/termination of a thread should
	 * not release any visible resources (such as mutexes) and that
	 * it is the applications responsibility.  Resources that are
	 * internal to the threads library, including file and fd locks,
	 * are not visible to the application and need to be released.
	 */
	/* Unlock all private mutexes: */
	_mutex_unlock_private(curthread);

	/*
	 * This still isn't quite correct because we don't account
	 * for held spinlocks (see libc/stdlib/malloc.c).
	 */
}

void
_pthread_exit(void *status)
{
	struct pthread *curthread = tls_get_curthread();

	/* Check if this thread is already in the process of exiting: */
	if ((curthread->cancelflags & THR_CANCEL_EXITING) != 0) {
		PANIC("Thread %p has called "
		    "pthread_exit() from a destructor. POSIX 1003.1 "
		    "1996 s16.2.5.2 does not allow this!", curthread);
	}

	/* Flag this thread as exiting. */
	atomic_set_int(&curthread->cancelflags, THR_CANCEL_EXITING);

	_thr_exit_cleanup();

	/* Save the return value: */
	curthread->ret = status;
	while (curthread->cleanup != NULL) {
		_pthread_cleanup_pop(1);
	}

	exit_thread();
}

__strong_reference(_pthread_exit, pthread_exit);

static void
exit_thread(void)
{
	struct pthread *curthread = tls_get_curthread();

	/* Check if there is thread specific data: */
	if (curthread->specific != NULL) {
		/* Run the thread-specific data destructors: */
		_thread_cleanupspecific();
	}

	if (!_thr_isthreaded())
		exit(0);

	THREAD_LIST_LOCK(curthread);
	_thread_active_threads--;
	if (_thread_active_threads == 0) {
		THREAD_LIST_UNLOCK(curthread);
		exit(0);
		/* Never reach! */
	}
	THR_LOCK(curthread);
	curthread->state = PS_DEAD;
	THR_UNLOCK(curthread);
	curthread->refcount--;
	if (curthread->tlflags & TLFLAGS_DETACHED)
		THR_GCLIST_ADD(curthread);
	THREAD_LIST_UNLOCK(curthread);
	if (curthread->joiner)
		_thr_umtx_wake(&curthread->state, 0);
	if (SHOULD_REPORT_EVENT(curthread, TD_DEATH))
		_thr_report_death(curthread);
	/* Exit and set terminated to once we're really dead. */
	extexit(EXTEXIT_SETINT|EXTEXIT_LWP, 1, &curthread->terminated);
	PANIC("thr_exit() returned");
	/* Never reach! */
}
