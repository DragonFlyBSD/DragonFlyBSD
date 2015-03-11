/*
 * Copyright (c) 2005 David Xu <davidxu@freebsd.org>
 * Copyright (c) 2003 Daniel M. Eischen <deischen@gdeb.com>
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
 * $FreeBSD: src/lib/libpthread/thread/thr_create.c,v 1.58 2004/10/23 23:28:36 davidxu Exp $
 */

#include "namespace.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <stddef.h>
#include <sys/time.h>
#include <machine/reg.h>
#include <machine/tls.h>

#include <pthread.h>
#include <sys/signalvar.h>
#include "un-namespace.h"

#include "thr_private.h"
#include "libc_private.h"

static int  create_stack(struct pthread_attr *pattr);
static void thread_start(void *);

int
_pthread_create(pthread_t * thread, const pthread_attr_t * attr,
       void *(*start_routine) (void *), void *arg)
{
	struct lwp_params create_params;
	void *stack;
	sigset_t sigmask, oldsigmask;
	struct pthread *curthread, *new_thread;
	int ret = 0, locked;

	_thr_check_init();

	/*
	 * Tell libc and others now they need lock to protect their data.
	 */
	if (_thr_isthreaded() == 0 && _thr_setthreaded(1))
		return (EAGAIN);

	curthread = tls_get_curthread();
	if ((new_thread = _thr_alloc(curthread)) == NULL)
		return (EAGAIN);

	if (attr == NULL || *attr == NULL)
		/* Use the default thread attributes: */
		new_thread->attr = _pthread_attr_default;
	else
		new_thread->attr = *(*attr);
	if (new_thread->attr.sched_inherit == PTHREAD_INHERIT_SCHED) {
		/* inherit scheduling contention scope */
		if (curthread->attr.flags & PTHREAD_SCOPE_SYSTEM)
			new_thread->attr.flags |= PTHREAD_SCOPE_SYSTEM;
		else
			new_thread->attr.flags &= ~PTHREAD_SCOPE_SYSTEM;
		/*
		 * scheduling policy and scheduling parameters will be
		 * inherited in following code.
		 */
	}

	if (_thread_scope_system > 0)
		new_thread->attr.flags |= PTHREAD_SCOPE_SYSTEM;
	else if (_thread_scope_system < 0)
		new_thread->attr.flags &= ~PTHREAD_SCOPE_SYSTEM;

	if (create_stack(&new_thread->attr) != 0) {
		/* Insufficient memory to create a stack: */
		new_thread->terminated = 1;
		_thr_free(curthread, new_thread);
		return (EAGAIN);
	}
	/*
	 * Write a magic value to the thread structure
	 * to help identify valid ones:
	 */
	new_thread->magic = THR_MAGIC;
	new_thread->start_routine = start_routine;
	new_thread->arg = arg;
	new_thread->cancelflags = PTHREAD_CANCEL_ENABLE |
	    PTHREAD_CANCEL_DEFERRED;
	/*
	 * Check if this thread is to inherit the scheduling
	 * attributes from its parent:
	 */
	if (new_thread->attr.sched_inherit == PTHREAD_INHERIT_SCHED) {
		/*
		 * Copy the scheduling attributes. Lock the scheduling
		 * lock to get consistent scheduling parameters.
		 */
		THR_LOCK(curthread);
		new_thread->base_priority = curthread->base_priority;
		new_thread->attr.prio = curthread->attr.prio;
		new_thread->attr.sched_policy = curthread->attr.sched_policy;
		THR_UNLOCK(curthread);
	} else {
		/*
		 * Use just the thread priority, leaving the
		 * other scheduling attributes as their
		 * default values:
		 */
		new_thread->base_priority = new_thread->attr.prio;
	}
	new_thread->active_priority = new_thread->base_priority;

	/* Initialize the mutex queue: */
	TAILQ_INIT(&new_thread->mutexq);

	/* Initialise hooks in the thread structure: */
	if (new_thread->attr.suspend == THR_CREATE_SUSPENDED)
		new_thread->flags = THR_FLAGS_NEED_SUSPEND;

	new_thread->state = PS_RUNNING;

	if (new_thread->attr.flags & PTHREAD_CREATE_DETACHED)
		new_thread->tlflags |= TLFLAGS_DETACHED;

	/* Add the new thread. */
	new_thread->refcount = 1;
	_thr_link(curthread, new_thread);
	/* Return thread pointer eariler so that new thread can use it. */
	(*thread) = new_thread;
	if (SHOULD_REPORT_EVENT(curthread, TD_CREATE)) {
		THR_THREAD_LOCK(curthread, new_thread);
		locked = 1;
	} else
		locked = 0;
	/* Schedule the new thread. */
	stack = (char *)new_thread->attr.stackaddr_attr +
		        new_thread->attr.stacksize_attr;
	bzero(&create_params, sizeof(create_params));
	create_params.lwp_func = thread_start;
	create_params.lwp_arg = new_thread;
	create_params.lwp_stack = stack;
	create_params.lwp_tid1 = &new_thread->tid;
	/*
	 * Thread created by thr_create() inherits currrent thread
	 * sigmask, however, before new thread setup itself correctly,
	 * it can not handle signal, so we should mask all signals here.
	 * We do this at the very last moment, so that we don't run
	 * into problems while we have all signals disabled.
	 */
	SIGFILLSET(sigmask);
	__sys_sigprocmask(SIG_SETMASK, &sigmask, &oldsigmask);
	new_thread->sigmask = oldsigmask;
	ret = lwp_create(&create_params);
	__sys_sigprocmask(SIG_SETMASK, &oldsigmask, NULL);
	if (ret != 0) {
		if (!locked)
			THR_THREAD_LOCK(curthread, new_thread);
		new_thread->state = PS_DEAD;
		new_thread->terminated = 1;
		if (new_thread->flags & THR_FLAGS_NEED_SUSPEND) {
			new_thread->cycle++;
			_thr_umtx_wake(&new_thread->cycle, INT_MAX);
		}
		THR_THREAD_UNLOCK(curthread, new_thread);
		THREAD_LIST_LOCK(curthread);
		_thread_active_threads--;
		new_thread->tlflags |= TLFLAGS_DETACHED;
		_thr_ref_delete_unlocked(curthread, new_thread);
		THREAD_LIST_UNLOCK(curthread);
		(*thread) = NULL;
		ret = EAGAIN;
	} else if (locked) {
		_thr_report_creation(curthread, new_thread);
		THR_THREAD_UNLOCK(curthread, new_thread);
	}
	return (ret);
}

static int
create_stack(struct pthread_attr *pattr)
{
	int ret;

	/* Check if a stack was specified in the thread attributes: */
	if ((pattr->stackaddr_attr) != NULL) {
		pattr->guardsize_attr = 0;
		pattr->flags |= THR_STACK_USER;
		ret = 0;
	}
	else
		ret = _thr_stack_alloc(pattr);
	return (ret);
}

static void
thread_start(void *arg)
{
	struct pthread *curthread = (struct pthread *)arg;

	tls_set_tcb(curthread->tcb);

	/* Thread was created with all signals blocked, unblock them. */
	__sys_sigprocmask(SIG_SETMASK, &curthread->sigmask, NULL);

	THR_LOCK(curthread);
	THR_UNLOCK(curthread);

	if (curthread->flags & THR_FLAGS_NEED_SUSPEND)
		_thr_suspend_check(curthread);
	_nmalloc_thr_init();

	/* Run the current thread's start routine with argument: */
	_pthread_exit(curthread->start_routine(curthread->arg));

	/* This point should never be reached. */
	PANIC("Thread has resumed after exit");
}

__strong_reference(_pthread_create, pthread_create);
