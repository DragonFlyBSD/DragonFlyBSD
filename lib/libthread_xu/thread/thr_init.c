/*
 * Copyright (c) 2003 Daniel M. Eischen <deischen@freebsd.org>
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
 */

#include "namespace.h"
#include <sys/param.h>
#include <sys/signalvar.h>

#include <sys/ioctl.h>
#include <sys/sysctl.h>
#include <sys/time.h>
#include <sys/ttycom.h>
#include <sys/mman.h>

#include <fcntl.h>
#include <paths.h>
#include <pthread.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "un-namespace.h"

#include "libc_private.h"
#include "thr_private.h"

/* thr_attr.c */
STATIC_LIB_REQUIRE(_pthread_attr_init);
/* thr_barrier.c */
STATIC_LIB_REQUIRE(_pthread_barrier_init);
/* thr_barrierattr.c */
STATIC_LIB_REQUIRE(_pthread_barrierattr_init);
/* thr_cancel.c */
STATIC_LIB_REQUIRE(_pthread_cancel);
/* thr_clean.c */
STATIC_LIB_REQUIRE(_pthread_cleanup_push);
/* thr_concurrency.c */
STATIC_LIB_REQUIRE(_pthread_getconcurrency);
STATIC_LIB_REQUIRE(_pthread_setconcurrency);
/* thr_cond.c */
STATIC_LIB_REQUIRE(_pthread_cond_init);
/* thr_condattr.c */
STATIC_LIB_REQUIRE(_pthread_condattr_init);
/* thr_create.c */
STATIC_LIB_REQUIRE(_pthread_create);
/* thr_detach.c */
STATIC_LIB_REQUIRE(_pthread_detach);
/* thr_equal.c */
STATIC_LIB_REQUIRE(_pthread_equal);
/* thr_exit.c */
STATIC_LIB_REQUIRE(_pthread_exit);
/* thr_fork.c */
STATIC_LIB_REQUIRE(_pthread_atfork);
STATIC_LIB_REQUIRE(_fork);
/* thr_getprio.c */
STATIC_LIB_REQUIRE(_pthread_getprio);
/* thr_getschedparam.c */
STATIC_LIB_REQUIRE(_pthread_getschedparam);
/* thr_info.c */
STATIC_LIB_REQUIRE(_pthread_set_name_np);
/* thr_init.c */
STATIC_LIB_REQUIRE(_pthread_init_early);
/* thr_join.c */
STATIC_LIB_REQUIRE(_pthread_join);
/* thr_kill.c */
STATIC_LIB_REQUIRE(_pthread_kill);
/* thr_main_np.c */
STATIC_LIB_REQUIRE(_pthread_main_np);
/* thr_multi_np.c */
STATIC_LIB_REQUIRE(_pthread_multi_np);
/* thr_mutex.c */
STATIC_LIB_REQUIRE(_pthread_mutex_init);
/* thr_mutex_prioceiling.c */
STATIC_LIB_REQUIRE(_pthread_mutexattr_getprioceiling);
/* thr_mutex_protocol.c */
STATIC_LIB_REQUIRE(_pthread_mutexattr_getprotocol);
/* thr_mutexattr.c */
STATIC_LIB_REQUIRE(_pthread_mutexattr_init);
/* thr_once.c */
STATIC_LIB_REQUIRE(_pthread_once);
/* thr_pspinlock.c */
STATIC_LIB_REQUIRE(_pthread_spin_init);
/* thr_resume_np.c */
STATIC_LIB_REQUIRE(_pthread_resume_np);
/* thr_rwlock.c */
STATIC_LIB_REQUIRE(_pthread_rwlock_init);
/* thr_rwlockattr.c */
STATIC_LIB_REQUIRE(_pthread_rwlockattr_init);
/* thr_self.c */
STATIC_LIB_REQUIRE(_pthread_self);
/* thr_sem.c */
STATIC_LIB_REQUIRE(_sem_init);
/* thr_setprio.c */
STATIC_LIB_REQUIRE(_pthread_setprio);
/* thr_setschedparam.c */
STATIC_LIB_REQUIRE(_pthread_setschedparam);
/* thr_sig.c */
STATIC_LIB_REQUIRE(_sigwait);
/* thr_single_np.c */
STATIC_LIB_REQUIRE(_pthread_single_np);
/* thr_spec.c */
STATIC_LIB_REQUIRE(_pthread_key_create);
/* thr_spinlock.c */
STATIC_LIB_REQUIRE(_spinlock);
/* thr_suspend_np.c */
STATIC_LIB_REQUIRE(_pthread_suspend_np);
/* thr_switch_np.c */
STATIC_LIB_REQUIRE(_pthread_switch_add_np);
/* thr_symbols.c */
STATIC_LIB_REQUIRE(_thread_state_running);
/* thr_syscalls.c */
STATIC_LIB_REQUIRE(__wait4);
/* thr_yield.c */
STATIC_LIB_REQUIRE(_pthread_yield);

char		*_usrstack;
struct pthread	*_thr_initial;
int		_thread_scope_system;
static void	*_thr_main_redzone;

pid_t		_thr_pid;
size_t		_thr_guard_default;
size_t		_thr_stack_default =	THR_STACK_DEFAULT;
size_t		_thr_stack_initial =	THR_STACK_INITIAL;
int		_thr_page_size;

static void	init_private(void);
static void	_libpthread_uninit(void);
static void	init_main_thread(struct pthread *thread);
static int	init_once = 0;

void	_thread_init(void) __attribute__ ((constructor));
void
_thread_init(void)
{
	_libpthread_init(NULL);
}

void	_thread_uninit(void) __attribute__ ((destructor));
void
_thread_uninit(void)
{
	_libpthread_uninit();
}

/*
 * This function is used by libc to initialise libpthread
 * early during dynamic linking.
 */
void _pthread_init_early(void);
void
_pthread_init_early(void)
{
	_libpthread_init(NULL);
}

/*
 * Threaded process initialization.
 *
 * This is only called under two conditions:
 *
 *   1) Some thread routines have detected that the library hasn't yet
 *      been initialized (_thr_initial == NULL && curthread == NULL), or
 *
 *   2) An explicit call to reinitialize after a fork (indicated
 *      by curthread != NULL)
 */
void
_libpthread_init(struct pthread *curthread)
{
	int fd, first = 0;
	sigset_t sigset, oldset;

	/* Check if this function has already been called: */
	if ((_thr_initial != NULL) && (curthread == NULL))
		/* Only initialize the threaded application once. */
		return;

	/*
	 * Check for the special case of this process running as
	 * or in place of init as pid = 1:
	 */
	if ((_thr_pid = getpid()) == 1) {
		/*
		 * Setup a new session for this process which is
		 * assumed to be running as root.
		 */
		if (setsid() == -1)
			PANIC("Can't set session ID");
		if (revoke(_PATH_CONSOLE) != 0)
			PANIC("Can't revoke console");
		if ((fd = __sys_open(_PATH_CONSOLE, O_RDWR)) < 0)
			PANIC("Can't open console");
		if (setlogin("root") == -1)
			PANIC("Can't set login to root");
		if (__sys_ioctl(fd, TIOCSCTTY, NULL) == -1)
			PANIC("Can't set controlling terminal");
	}

	/* Initialize pthread private data. */
	init_private();

	/* Set the initial thread. */
	if (curthread == NULL) {
		first = 1;
		/* Create and initialize the initial thread. */
		curthread = _thr_alloc(NULL);
		if (curthread == NULL)
			PANIC("Can't allocate initial thread");
		init_main_thread(curthread);
	}
	/*
	 * Add the thread to the thread list queue.
	 */
	THR_LIST_ADD(curthread);
	_thread_active_threads = 1;

	/* Setup the thread specific data */
	tls_set_tcb(curthread->tcb);

	if (first) {
		SIGFILLSET(sigset);
		__sys_sigprocmask(SIG_SETMASK, &sigset, &oldset);
		_thr_signal_init();
		_thr_initial = curthread;
		SIGDELSET(oldset, SIGCANCEL);
		__sys_sigprocmask(SIG_SETMASK, &oldset, NULL);
		if (td_eventismember(&_thread_event_mask, TD_CREATE))
			_thr_report_creation(curthread, curthread);
	}
}

static void
_libpthread_uninit(void)
{
	if (_thr_initial == NULL)
		return;
	if (_thr_main_redzone && _thr_main_redzone != MAP_FAILED) {
		munmap(_thr_main_redzone, _thr_guard_default);
		_thr_main_redzone = NULL;
	}
	_thr_stack_cleanup();
}

/*
 * This function and pthread_create() do a lot of the same things.
 * It'd be nice to consolidate the common stuff in one place.
 */
static void
init_main_thread(struct pthread *thread)
{
	/* Setup the thread attributes. */
	thread->tid = _thr_get_tid();
	thread->attr = _pthread_attr_default;
	/*
	 * Set up the thread stack.
	 *
	 * Create a red zone below the main stack.  All other stacks
	 * are constrained to a maximum size by the parameters
	 * passed to mmap(), but this stack is only limited by
	 * resource limits, so this stack needs an explicitly mapped
	 * red zone to protect the thread stack that is just beyond.
	 */
	_thr_main_redzone = mmap(_usrstack - _thr_stack_initial -
				 _thr_guard_default, _thr_guard_default,
				 0, MAP_ANON | MAP_TRYFIXED, -1, 0);
	if (_thr_main_redzone == MAP_FAILED) {
		PANIC("Cannot allocate red zone for initial thread");
	}

	/*
	 * Mark the stack as an application supplied stack so that it
	 * isn't deallocated.
	 *
	 * XXX - I'm not sure it would hurt anything to deallocate
	 *       the main thread stack because deallocation doesn't
	 *       actually free() it; it just puts it in the free
	 *       stack queue for later reuse.
	 */
	thread->attr.stackaddr_attr = _usrstack - _thr_stack_initial;
	thread->attr.stacksize_attr = _thr_stack_initial;
	thread->attr.guardsize_attr = _thr_guard_default;
	thread->attr.flags |= THR_STACK_USER;

	/*
	 * Write a magic value to the thread structure
	 * to help identify valid ones:
	 */
	thread->magic = THR_MAGIC;

	thread->cancelflags = PTHREAD_CANCEL_ENABLE | PTHREAD_CANCEL_DEFERRED;
	thread->name = strdup("initial thread");

	/* Default the priority of the initial thread: */
	thread->base_priority = THR_DEFAULT_PRIORITY;
	thread->active_priority = THR_DEFAULT_PRIORITY;
	thread->inherited_priority = 0;

	/* Initialize the mutex queue: */
	TAILQ_INIT(&thread->mutexq);

	thread->state = PS_RUNNING;
	thread->uniqueid = 0;

	/* Others cleared to zero by thr_alloc() */
}

static void
init_private(void)
{
	size_t len;
	int mib[2];

	_thr_umtx_init(&_mutex_static_lock);
	_thr_umtx_init(&_cond_static_lock);
	_thr_umtx_init(&_rwlock_static_lock);
	_thr_umtx_init(&_keytable_lock);
	_thr_umtx_init(&_thr_atfork_lock);
	_thr_umtx_init(&_thr_event_lock);
	_thr_spinlock_init();
	_thr_list_init();

	/*
	 * Avoid reinitializing some things if they don't need to be,
	 * e.g. after a fork().
	 */
	if (init_once == 0) {
		/* Find the stack top */
		mib[0] = CTL_KERN;
		mib[1] = KERN_USRSTACK;
		len = sizeof (_usrstack);
		if (sysctl(mib, 2, &_usrstack, &len, NULL, 0) == -1)
			PANIC("Cannot get kern.usrstack from sysctl");
		_thr_page_size = getpagesize();
		_thr_guard_default = _thr_page_size;
		_pthread_attr_default.guardsize_attr = _thr_guard_default;
		
		TAILQ_INIT(&_thr_atfork_list);
#ifdef SYSTEM_SCOPE_ONLY
		_thread_scope_system = 1;
#else
		if (getenv("LIBPTHREAD_SYSTEM_SCOPE") != NULL)
			_thread_scope_system = 1;
		else if (getenv("LIBPTHREAD_PROCESS_SCOPE") != NULL)
			_thread_scope_system = -1;
#endif
	}
	init_once = 1;
}

__strong_reference(_pthread_init_early, pthread_init_early);
