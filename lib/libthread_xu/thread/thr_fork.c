/*
 * Copyright (c) 2005 David Xu <davidxu@freebsd.org>
 * Copyright (c) 2003 Daniel Eischen <deischen@freebsd.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Neither the name of the author nor the names of any co-contributors
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
 * $FreeBSD: head/lib/libthr/thread/thr_fork.c 213096 2010-08-23 $
 */

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
 */

#include "namespace.h"
#include <sys/syscall.h>

#include <machine/tls.h>

#include <errno.h>
#include <link.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>
#include <spinlock.h>
#include "un-namespace.h"

#include "libc_private.h"
#include "thr_private.h"

struct atfork_head	_thr_atfork_list;
umtx_t	_thr_atfork_lock;

int
_pthread_atfork(void (*prepare)(void), void (*parent)(void),
    void (*child)(void))
{
	struct pthread *curthread;
	struct pthread_atfork *af;

	if ((af = malloc(sizeof(struct pthread_atfork))) == NULL)
		return (ENOMEM);

	curthread = tls_get_curthread();
	af->prepare = prepare;
	af->parent = parent;
	af->child = child;
	THR_UMTX_LOCK(curthread, &_thr_atfork_lock);
	TAILQ_INSERT_TAIL(&_thr_atfork_list, af, qe);
	THR_UMTX_UNLOCK(curthread, &_thr_atfork_lock);
	return (0);
}

void
__pthread_cxa_finalize(struct dl_phdr_info *phdr_info)
{
	struct pthread *curthread;
	struct pthread_atfork *af, *af1;

	curthread = tls_get_curthread();
	THR_UMTX_LOCK(curthread, &_thr_atfork_lock);
	TAILQ_FOREACH_MUTABLE(af, &_thr_atfork_list, qe, af1) {
		if (__elf_phdr_match_addr(phdr_info, af->prepare) ||
		    __elf_phdr_match_addr(phdr_info, af->parent) ||
		    __elf_phdr_match_addr(phdr_info, af->child)) {
			TAILQ_REMOVE(&_thr_atfork_list, af, qe);
			free(af);
		}
	}
	THR_UMTX_UNLOCK(curthread, &_thr_atfork_lock);
}

/*
 * For a while, allow libpthread to work with a libc that doesn't
 * export the malloc lock.
 */
#pragma weak __malloc_lock

pid_t _fork(void);

pid_t
_fork(void)
{
	static umtx_t inprogress;
	static int waiters;
	umtx_t tmp;

	struct pthread *curthread;
	struct pthread_atfork *af;
	pid_t ret;
	int errsave;
#ifndef __DragonFly__
	int unlock_malloc;
#endif

	if (!_thr_is_inited())
		return (__syscall(SYS_fork));

	curthread = tls_get_curthread();

	THR_UMTX_LOCK(curthread, &_thr_atfork_lock);
	tmp = inprogress;
	while (tmp) {
		waiters++;
		THR_UMTX_UNLOCK(curthread, &_thr_atfork_lock);
		_thr_umtx_wait(&inprogress, tmp, NULL, 0);
		THR_UMTX_LOCK(curthread, &_thr_atfork_lock);
		waiters--;
		tmp = inprogress;
	}
	inprogress = 1;
	THR_UMTX_UNLOCK(curthread, &_thr_atfork_lock);

	/* Run down atfork prepare handlers. */
	TAILQ_FOREACH_REVERSE(af, &_thr_atfork_list, atfork_head, qe) {
		if (af->prepare != NULL)
			af->prepare();
	}

#ifndef __DragonFly__
	/*
	 * Try our best to protect memory from being corrupted in
	 * child process because another thread in malloc code will
	 * simply be kill by fork().
	 */
	if ((_thr_isthreaded() != 0) && (__malloc_lock != NULL)) {
		unlock_malloc = 1;
		_spinlock(__malloc_lock);
	} else {
		unlock_malloc = 0;
	}
#endif

	/*
	 * Block all signals until we reach a safe point.
	 */
	_thr_signal_block(curthread);

	/* Fork a new process: */
	if ((ret = __syscall(SYS_fork)) == 0) {
		/* Child process */
		errsave = errno;
		inprogress = 0;
		curthread->cancelflags &= ~THR_CANCEL_NEEDED;
		/*
		 * Thread list will be reinitialized, and later we call
		 * _libpthread_init(), it will add us back to list.
		 */
		curthread->tlflags &= ~(TLFLAGS_IN_TDLIST | TLFLAGS_DETACHED);

		/* child is a new kernel thread. */
		curthread->tid = _thr_get_tid();

		/* clear other threads locked us. */
		_thr_umtx_init(&curthread->lock);
		_thr_umtx_init(&_thr_atfork_lock);
		_thr_setthreaded(0);

		/* reinitialize libc spinlocks, this includes __malloc_lock. */
		_thr_spinlock_init();
		_mutex_fork(curthread);

		/* reinitalize library. */
		_libpthread_init(curthread);

		/* Ready to continue, unblock signals. */ 
		_thr_signal_unblock(curthread);

		/* Run down atfork child handlers. */
		TAILQ_FOREACH(af, &_thr_atfork_list, qe) {
			if (af->child != NULL)
				af->child();
		}
	} else {
		/* Parent process */
		errsave = errno;

#ifndef __DragonFly__
		if (unlock_malloc)
			_spinunlock(__malloc_lock);
#endif

		/* Ready to continue, unblock signals. */ 
		_thr_signal_unblock(curthread);

		/* Run down atfork parent handlers. */
		TAILQ_FOREACH(af, &_thr_atfork_list, qe) {
			if (af->parent != NULL)
				af->parent();
		}

		THR_UMTX_LOCK(curthread, &_thr_atfork_lock);
		inprogress = 0;
		if (waiters)
			_thr_umtx_wake(&inprogress, waiters);
		THR_UMTX_UNLOCK(curthread, &_thr_atfork_lock);
	}
	errno = errsave;

	/* Return the process ID: */
	return (ret);
}

__strong_reference(_fork, fork);
__strong_reference(_pthread_atfork, pthread_atfork);

