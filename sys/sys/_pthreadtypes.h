/*
 * Copyright (c) 2020 The DragonFly Project.  All rights reserved.
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

#ifndef _SYS__PTHREADTYPES_H_
#define	_SYS__PTHREADTYPES_H_

#include <sys/cdefs.h>

/*
 * Forward opaque structure definitions that do not pollute namespaces.
 * To be used in headers where visibility is an issue.
 */
struct __pthread_s;
struct __pthread_attr_s;
struct __pthread_barrier_s;
struct __pthread_barrierattr_s;
struct __pthread_cond_s;
struct __pthread_condattr_s;
struct __pthread_mutex_s;
struct __pthread_mutexattr_s;
struct __pthread_once_s;
struct __pthread_rwlock_s;
struct __pthread_rwlockattr_s;
struct __pthread_spinlock_s;

/*
 * Basic pthread types to be used in function prototypes.
 */
#ifndef _PTHREAD_T_DECLARED
typedef	struct __pthread_s		*pthread_t;
#define	_PTHREAD_T_DECLARED
#endif
#ifndef _PTHREAD_ATTR_T_DECLARED
typedef	struct __pthread_attr_s		*pthread_attr_t;
#define	_PTHREAD_ATTR_T_DECLARED
#endif
typedef	struct __pthread_barrier_s	*pthread_barrier_t;
typedef	struct __pthread_barrierattr_s	*pthread_barrierattr_t;
typedef	struct __pthread_cond_s		*pthread_cond_t;
typedef	struct __pthread_condattr_s	*pthread_condattr_t;
typedef	struct __pthread_mutex_s	*pthread_mutex_t;
typedef	struct __pthread_mutexattr_s	*pthread_mutexattr_t;
typedef	int				pthread_key_t;
typedef	struct __pthread_once_s		pthread_once_t;

typedef	struct __pthread_rwlock_s	*pthread_rwlock_t;
typedef	struct __pthread_rwlockattr_s	*pthread_rwlockattr_t;
typedef	struct __pthread_spinlock_s	*pthread_spinlock_t;

/*
 * Once-only structure (partly public).
 */
struct __pthread_once_s {
	int		__state;
	void		*__sparelibc_r;	/* unused, kept for ABI compat */
};

#endif	/* !_SYS__PTHREADTYPES_H_ */
