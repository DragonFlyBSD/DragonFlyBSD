/*
 * Copyright (c) 2004 The DragonFly Project.  All rights reserved.
 * 
 * This code is derived from software contributed to The DragonFly Project
 * by Simon Schubert <corecode@fs.ei.tum.de>
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
 * 
 * $DragonFly: src/lib/libc/gen/pthread_fake.c,v 1.1 2004/12/16 22:55:29 joerg Exp $
 */

#ifndef _THREAD_SAFE
/*
 * Provide the functions for non-threaded programs using
 * thread-aware dynamic modules.
 *
 * This list explicitly lakes pthread_create, since an application
 * using this function clearly should be linked against -lc_r.
 * Leaving this function out allows the linker to catch this.
 * Same reason applies for thread-aware modules.
 */

#include <sys/cdefs.h>

#include <errno.h>
#include <stddef.h>

__weak_reference(_pthread_fake_inval, pthread_attr_destroy);
__weak_reference(_pthread_fake_inval, pthread_attr_get_np);
__weak_reference(_pthread_fake_inval, pthread_attr_getdetachstate);
__weak_reference(_pthread_fake_inval, pthread_attr_getinheritsched);
__weak_reference(_pthread_fake_inval, pthread_attr_getschedparam);
__weak_reference(_pthread_fake_inval, pthread_attr_getschedpolicy);
__weak_reference(_pthread_fake_inval, pthread_attr_getscope);
__weak_reference(_pthread_fake_inval, pthread_attr_getstack);
__weak_reference(_pthread_fake_inval, pthread_attr_getstackaddr);
__weak_reference(_pthread_fake_inval, pthread_attr_getstacksize);
__weak_reference(_pthread_fake_inval, pthread_attr_init);
__weak_reference(_pthread_fake_inval, pthread_attr_setcreatesuspend_np);
__weak_reference(_pthread_fake_inval, pthread_attr_setdetachstate);
__weak_reference(_pthread_fake_inval, pthread_attr_setinheritsched);
__weak_reference(_pthread_fake_inval, pthread_attr_setschedparam);
__weak_reference(_pthread_fake_inval, pthread_attr_setschedpolicy);
__weak_reference(_pthread_fake_inval, pthread_attr_setscope);
__weak_reference(_pthread_fake_inval, pthread_attr_setstack);
__weak_reference(_pthread_fake_inval, pthread_attr_setstackaddr);
__weak_reference(_pthread_fake_inval, pthread_attr_setstacksize);
__weak_reference(_pthread_fake_inval, pthread_cancel);
__weak_reference(_pthread_fake_inval, pthread_setcancelstate);
__weak_reference(_pthread_fake_inval, pthread_setcanceltype);
__weak_reference(_pthread_fake_inval, pthread_testcancel);/*XXX void */
__weak_reference(_pthread_fake_inval, pthread_cleanup_push);/*XXX void */
__weak_reference(_pthread_fake_inval, pthread_cleanup_pop);/*XXX void */
__weak_reference(_pthread_fake_inval, pthread_getconcurrency);
__weak_reference(_pthread_fake_inval, pthread_setconcurrency);
__weak_reference(_pthread_fake_inval, pthread_cond_init);
__weak_reference(_pthread_fake_inval, pthread_cond_destroy);
__weak_reference(_pthread_fake_inval, pthread_cond_wait);
__weak_reference(_pthread_fake_inval, pthread_cond_timedwait);
__weak_reference(_pthread_fake_inval, pthread_cond_signal);
__weak_reference(_pthread_fake_inval, pthread_cond_broadcast);
__weak_reference(_pthread_fake_inval, pthread_condattr_destroy);
__weak_reference(_pthread_fake_inval, pthread_condattr_init);
__weak_reference(_pthread_fake_inval, pthread_detach);
__weak_reference(_pthread_fake_inval, pthread_equal);/*XXX*/
__weak_reference(_pthread_fake_inval, pthread_exit);/*XXX void*/
__weak_reference(_pthread_fake_inval, pthread_getprio);
__weak_reference(_pthread_fake_inval, pthread_getschedparam);
__weak_reference(_pthread_fake_inval, pthread_set_name_np);/*XXX void*/
__weak_reference(_pthread_fake_inval, pthread_join);
__weak_reference(_pthread_fake_inval, pthread_kill);
__weak_reference(_pthread_fake_inval, pthread_main_np);
__weak_reference(_pthread_fake_inval, pthread_mutexattr_init);
__weak_reference(_pthread_fake_inval, pthread_mutexattr_setkind_np);
__weak_reference(_pthread_fake_inval, pthread_mutexattr_getkind_np);
__weak_reference(_pthread_fake_inval, pthread_mutexattr_gettype);
__weak_reference(_pthread_fake_inval, pthread_mutexattr_settype);
__weak_reference(_pthread_fake_inval, pthread_multi_np);
__weak_reference(_pthread_fake_inval, pthread_mutex_init);
__weak_reference(_pthread_fake_inval, pthread_mutex_destroy);
__weak_reference(_pthread_fake_inval, pthread_mutex_trylock);
__weak_reference(_pthread_fake_inval, pthread_mutex_lock);
__weak_reference(_pthread_fake_inval, pthread_mutex_unlock);
__weak_reference(_pthread_fake_inval, pthread_mutexattr_getprioceiling);
__weak_reference(_pthread_fake_inval, pthread_mutexattr_setprioceiling);
__weak_reference(_pthread_fake_inval, pthread_mutex_getprioceiling);
__weak_reference(_pthread_fake_inval, pthread_mutex_setprioceiling);
__weak_reference(_pthread_fake_inval, pthread_mutexattr_getprotocol);
__weak_reference(_pthread_fake_inval, pthread_mutexattr_setprotocol);
__weak_reference(_pthread_fake_inval, pthread_mutexattr_destroy);
__weak_reference(_pthread_fake_inval, pthread_once);
__weak_reference(_pthread_fake_inval, pthread_resume_np);
__weak_reference(_pthread_fake_inval, pthread_resume_all_np);/*XXX void*/
__weak_reference(_pthread_fake_inval, pthread_rwlock_destroy);
__weak_reference(_pthread_fake_inval, pthread_rwlock_init);
__weak_reference(_pthread_fake_inval, pthread_rwlock_rdlock);
__weak_reference(_pthread_fake_inval, pthread_rwlock_tryrdlock);
__weak_reference(_pthread_fake_inval, pthread_rwlock_trywrlock);
__weak_reference(_pthread_fake_inval, pthread_rwlock_unlock);
__weak_reference(_pthread_fake_inval, pthread_rwlock_wrlock);
__weak_reference(_pthread_fake_inval, pthread_rwlockattr_destroy);
__weak_reference(_pthread_fake_inval, pthread_rwlockattr_getpshared);
__weak_reference(_pthread_fake_inval, pthread_rwlockattr_init);
__weak_reference(_pthread_fake_inval, pthread_rwlockattr_setpshared);
__weak_reference(_pthread_fake_null, pthread_self);/*XXX pthread_t */
__weak_reference(_pthread_fake_inval, sem_init);
__weak_reference(_pthread_fake_inval, sem_destroy);
__weak_reference(_pthread_fake_inval, sem_open);
__weak_reference(_pthread_fake_inval, sem_close);
__weak_reference(_pthread_fake_inval, sem_unlink);
__weak_reference(_pthread_fake_inval, sem_wait);
__weak_reference(_pthread_fake_inval, sem_trywait);
__weak_reference(_pthread_fake_inval, sem_post);
__weak_reference(_pthread_fake_inval, sem_getvalue);
__weak_reference(_pthread_fake_inval, pthread_setprio);
__weak_reference(_pthread_fake_inval, pthread_setschedparam);
__weak_reference(_pthread_fake_inval, pthread_sigmask);
__weak_reference(_pthread_fake_inval, pthread_single_np);
__weak_reference(_pthread_fake_inval, pthread_key_create);
__weak_reference(_pthread_fake_inval, pthread_key_delete);
__weak_reference(_pthread_fake_inval, pthread_getspecific);
__weak_reference(_pthread_fake_inval, pthread_setspecific);
__weak_reference(_pthread_fake_inval, pthread_suspend_np);
__weak_reference(_pthread_fake_inval, pthread_suspend_all_np);/*XXX void*/
__weak_reference(_pthread_fake_inval, pthread_switch_add_np);
__weak_reference(_pthread_fake_inval, pthread_switch_delete_np);
__weak_reference(_pthread_fake_inval, sched_yield);
__weak_reference(_pthread_fake_inval, pthread_yield);/*XXX void*/

int
_pthread_fake_inval(void)
{
	return EINVAL;
}

void *
_pthread_fake_null(void)
{
	return NULL;
}

#endif /* _THREAD_SAFE */
