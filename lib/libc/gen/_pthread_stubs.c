/*
 * Copyright (c) 2001 Daniel Eischen <deischen@FreeBSD.org>.
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
 *
 * THIS SOFTWARE IS PROVIDED BY DANIEL EISCHEN AND CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
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
 * $FreeBSD: src/lib/libc/gen/_pthread_stubs.c,v 1.5 2001/06/11 23:18:22 iedowse Exp $
 */

#include <stdlib.h>
#include <pthread.h>

/*
 * Weak symbols: All libc internal usage of these functions should
 * use the weak symbol versions (_pthread_XXX).  If libpthread is
 * linked, it will override these functions with (non-weak) routines.
 * The _pthread_XXX functions are provided solely for internal libc
 * usage to avoid unwanted cancellation points and to differentiate
 * between application locks and libc locks (threads holding the
 * latter can't be allowed to exit/terminate).
 */

#define WR(f, n)			\
    __weak_reference(f, _ ## n);	\
    __weak_reference(f, n)

WR(__atfork, pthread_atfork);
WR(stub_zero, pthread_attr_destroy);
WR(stub_zero, pthread_attr_get_np);
WR(stub_zero, pthread_attr_getdetachstate);
WR(stub_zero, pthread_attr_getguardsize);
WR(stub_zero, pthread_attr_getinheritsched);
WR(stub_zero, pthread_attr_getschedparam);
WR(stub_zero, pthread_attr_getschedpolicy);
WR(stub_zero, pthread_attr_getscope);
WR(stub_zero, pthread_attr_getstack);
WR(stub_zero, pthread_attr_getstackaddr);
WR(stub_zero, pthread_attr_getstacksize);
WR(stub_zero, pthread_attr_init);
WR(stub_zero, pthread_attr_setcreatesuspend_np);
WR(stub_zero, pthread_attr_setdetachstate);
WR(stub_zero, pthread_attr_setguardsize);
WR(stub_zero, pthread_attr_setinheritsched);
WR(stub_zero, pthread_attr_setschedparam);
WR(stub_zero, pthread_attr_setschedpolicy);
WR(stub_zero, pthread_attr_setscope);
WR(stub_zero, pthread_attr_setstack);
WR(stub_zero, pthread_attr_setstackaddr);
WR(stub_zero, pthread_attr_setstacksize);
WR(stub_zero, pthread_barrier_destroy);
WR(stub_zero, pthread_barrier_init);
WR(stub_zero, pthread_barrier_wait);
WR(stub_zero, pthread_barrierattr_destroy);
WR(stub_zero, pthread_barrierattr_getpshared);
WR(stub_zero, pthread_barrierattr_init);
WR(stub_zero, pthread_barrierattr_setpshared);
WR(stub_zero, pthread_cancel);
WR(stub_zero, pthread_cleanup_pop);
WR(stub_zero, pthread_cleanup_push);
WR(stub_zero, pthread_cond_broadcast);
WR(stub_zero, pthread_cond_destroy);
WR(stub_zero, pthread_cond_init);
WR(stub_zero, pthread_cond_signal);
WR(stub_zero, pthread_cond_timedwait);
WR(stub_zero, pthread_cond_wait);
WR(stub_zero, pthread_condattr_destroy);
WR(stub_zero, pthread_condattr_getclock);
WR(stub_zero, pthread_condattr_getpshared);
WR(stub_zero, pthread_condattr_init);
WR(stub_zero, pthread_condattr_setclock);
WR(stub_zero, pthread_condattr_setpshared);
WR(stub_zero, pthread_detach);
WR(stub_true, pthread_equal);
WR(stub_exit, pthread_exit);
WR(stub_zero, pthread_getconcurrency);
WR(stub_zero, pthread_getprio);
WR(stub_zero, pthread_getschedparam);
WR(stub_null, pthread_getspecific);
WR(stub_empty, pthread_init_early);
WR(stub_zero, pthread_join);
WR(stub_zero, pthread_key_create);
WR(stub_zero, pthread_key_delete);
WR(stub_zero, pthread_kill);
WR(stub_main, pthread_main_np);
WR(stub_zero, pthread_multi_np);
WR(stub_zero, pthread_mutex_destroy);
WR(stub_zero, pthread_mutex_getprioceiling);
WR(stub_zero, pthread_mutex_init);
WR(stub_zero, pthread_mutex_lock);
WR(stub_zero, pthread_mutex_setprioceiling);
WR(stub_zero, pthread_mutex_timedlock);
WR(stub_zero, pthread_mutex_trylock);
WR(stub_zero, pthread_mutex_unlock);
WR(stub_zero, pthread_mutexattr_destroy);
WR(stub_zero, pthread_mutexattr_getkind_np);
WR(stub_zero, pthread_mutexattr_getprioceiling);
WR(stub_zero, pthread_mutexattr_getprotocol);
WR(stub_zero, pthread_mutexattr_getpshared);
WR(stub_zero, pthread_mutexattr_gettype);
WR(stub_zero, pthread_mutexattr_init);
WR(stub_zero, pthread_mutexattr_setkind_np);
WR(stub_zero, pthread_mutexattr_setprioceiling);
WR(stub_zero, pthread_mutexattr_setprotocol);
WR(stub_zero, pthread_mutexattr_setpshared);
WR(stub_zero, pthread_mutexattr_settype);
WR(stub_once, pthread_once);
WR(stub_zero, pthread_resume_all_np);
WR(stub_zero, pthread_resume_np);
WR(stub_zero, pthread_rwlock_destroy);
WR(stub_zero, pthread_rwlock_init);
WR(stub_zero, pthread_rwlock_rdlock);
WR(stub_zero, pthread_rwlock_timedrdlock);
WR(stub_zero, pthread_rwlock_timedwrlock);
WR(stub_zero, pthread_rwlock_tryrdlock);
WR(stub_zero, pthread_rwlock_trywrlock);
WR(stub_zero, pthread_rwlock_unlock);
WR(stub_zero, pthread_rwlock_wrlock);
WR(stub_zero, pthread_rwlockattr_destroy);
WR(stub_zero, pthread_rwlockattr_getpshared);
WR(stub_zero, pthread_rwlockattr_init);
WR(stub_zero, pthread_rwlockattr_setpshared);
WR(stub_self, pthread_self);
WR(stub_zero, pthread_set_name_np);
WR(stub_zero, pthread_setcancelstate);
WR(stub_zero, pthread_setcanceltype);
WR(stub_zero, pthread_setconcurrency);
WR(stub_zero, pthread_setprio);
WR(stub_zero, pthread_setschedparam);
WR(stub_zero, pthread_setspecific);
WR(stub_zero, pthread_sigmask);
WR(stub_zero, pthread_single_np);
WR(stub_zero, pthread_spin_destroy);
WR(stub_zero, pthread_spin_init);
WR(stub_zero, pthread_spin_lock);
WR(stub_zero, pthread_spin_trylock);
WR(stub_zero, pthread_spin_unlock);
WR(stub_zero, pthread_suspend_all_np);
WR(stub_zero, pthread_suspend_np);
WR(stub_zero, pthread_switch_add_np);
WR(stub_zero, pthread_switch_delete_np);
WR(stub_zero, pthread_testcancel);
WR(stub_zero, pthread_timedjoin_np);
WR(stub_zero, pthread_yield);
WR(stub_zero, sched_yield);
WR(stub_zero, sem_close);
WR(stub_zero, sem_destroy);
WR(stub_zero, sem_getvalue);
WR(stub_zero, sem_init);
WR(stub_zero, sem_open);
WR(stub_zero, sem_post);
WR(stub_zero, sem_trywait);
WR(stub_zero, sem_timedwait);
WR(stub_zero, sem_unlink);
WR(stub_zero, sem_wait);


static int __used
stub_zero(void)
{
	return (0);
}

static int __used
stub_once(pthread_once_t *o, void (*r)(void))
{
	if (o->state != PTHREAD_DONE_INIT) {
		(*r)();
		o->state = PTHREAD_DONE_INIT;
	}

	return (0);
}

static void * __used
stub_null(void)
{
	return (NULL);
}

static void * __used
stub_self(void)
{
	static struct {} main_thread;

	return (&main_thread);
}

static int __used
stub_main(void)
{
	return (-1);
}

static int __used
stub_true(void)
{
	return (1);
}

static void __used
stub_empty(void)
{
}

static void __used
stub_exit(void)
{
	exit(0);
}

/*
 * If libpthread is loaded, make sure it is initialised before
 * other libraries call pthread functions
 */
void _pthread_init(void) __constructor(101);
void _pthread_init_early(void);
void
_pthread_init(void)
{
	_pthread_init_early();
}

extern void (*cb_prepare)(void);
extern void (*cb_parent)(void);
extern void (*cb_child)(void);
extern int __isthreaded;

int
__atfork(void (*prepare)(void), void (*parent)(void),
    void (*child)(void))
{
	if (__isthreaded)
		return (-1);
	cb_prepare = prepare;
	cb_parent = parent;
	cb_child = child;
	return (0);
}
