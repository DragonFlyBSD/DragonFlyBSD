/*
 * Copyright (c) 2009 The DragonFly Project.  All rights reserved.
 * 
 * This code is derived from software contributed to The DragonFly Project
 * by Hasso Tepper <hasso@estpak.ee>
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

#include <sys/cdefs.h>

static int __used
_stub_return_error(void)
{
	return (-1);
}

__strong_reference(_stub_return_error, pthread_attr_getguardsize);
__strong_reference(_stub_return_error, pthread_attr_setguardsize);

__strong_reference(_stub_return_error, pthread_barrier_destroy);
__strong_reference(_stub_return_error, pthread_barrier_init);
__strong_reference(_stub_return_error, pthread_barrier_wait);
__strong_reference(_stub_return_error, pthread_barrierattr_destroy);
__strong_reference(_stub_return_error, pthread_barrierattr_getpshared);
__strong_reference(_stub_return_error, pthread_barrierattr_init);
__strong_reference(_stub_return_error, pthread_barrierattr_setpshared);

__strong_reference(_stub_return_error, pthread_condattr_getclock);
__strong_reference(_stub_return_error, pthread_condattr_getpshared);
__strong_reference(_stub_return_error, pthread_condattr_setclock);
__strong_reference(_stub_return_error, pthread_condattr_setpshared);

__strong_reference(_stub_return_error, pthread_mutex_timedlock);

__strong_reference(_stub_return_error, pthread_rwlock_timedrdlock);
__strong_reference(_stub_return_error, pthread_rwlock_timedwrlock);

__strong_reference(_stub_return_error, pthread_timedjoin_np);

__strong_reference(_stub_return_error, pthread_spin_init);
__strong_reference(_stub_return_error, pthread_spin_destroy);
__strong_reference(_stub_return_error, pthread_spin_trylock);
__strong_reference(_stub_return_error, pthread_spin_lock);
__strong_reference(_stub_return_error, pthread_spin_unlock);
