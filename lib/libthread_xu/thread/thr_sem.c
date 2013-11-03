/*
 * Copyright (C) 2005 David Xu <davidxu@freebsd.org>.
 * Copyright (C) 2000 Jason Evans <jasone@freebsd.org>.
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice(s), this list of conditions and the following disclaimer as
 *    the first lines of this file unmodified other than the possible
 *    addition of one or more copyright notices.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice(s), this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDER(S) ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT HOLDER(S) BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "namespace.h"
#include <machine/tls.h>
#include <sys/semaphore.h>
#include <sys/mman.h>

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdlib.h>
#include <time.h>
#include "un-namespace.h"
#include "thr_private.h"

/*
 * Semaphore definitions.
 */
struct sem {
	u_int32_t		magic;
	volatile umtx_t		count;
	int			semid;
	int			unused;	/* pad */
};

#define	SEM_MAGIC	((u_int32_t) 0x09fa4012)

#define SEMID_LWP	0
#define SEMID_FORK	1

static inline int
sem_check_validity(sem_t *sem)
{

	if ((sem != NULL) && (*sem != NULL) && ((*sem)->magic == SEM_MAGIC)) {
		return (0);
	} else {
		errno = EINVAL;
		return (-1);
	}
}

static sem_t
sem_alloc(unsigned int value, int pshared)
{
	sem_t sem;
	int semid;

	if (value > SEM_VALUE_MAX) {
		errno = EINVAL;
		return (NULL);
	}
	if (pshared) {
		static __thread sem_t sem_base;
		static __thread int sem_count;

		if (sem_base == NULL) {
			sem_base = mmap(NULL, getpagesize(),
					PROT_READ | PROT_WRITE,
					MAP_ANON | MAP_SHARED,
					-1, 0);
			sem_count = getpagesize() / sizeof(*sem);
		}
		sem = sem_base++;
		if (--sem_count == 0)
			sem_base = NULL;
		semid = SEMID_FORK;
	} else {
		sem = malloc(sizeof(struct sem));
		semid = SEMID_LWP;
	}
	if (sem == NULL) {
		errno = ENOSPC;
		return (NULL);
	}
	sem->magic = SEM_MAGIC;
	sem->count = (u_int32_t)value;
	sem->semid = semid;
	return (sem);
}

int
_sem_init(sem_t *sem, int pshared, unsigned int value)
{
	(*sem) = sem_alloc(value, pshared);
	if ((*sem) == NULL)
		return (-1);
	return (0);
}

int
_sem_destroy(sem_t *sem)
{
	if (sem_check_validity(sem) != 0)
		return (-1);

	(*sem)->magic = 0;

	switch ((*sem)->semid) {
	case SEMID_LWP:
		free(*sem);
		break;
	case SEMID_FORK:
		/* memory is left intact */
		break;
	}
	return (0);
}

int
_sem_getvalue(sem_t * __restrict sem, int * __restrict sval)
{
	if (sem_check_validity(sem) != 0)
		return (-1);

	*sval = (*sem)->count;
	return (0);
}

int
_sem_trywait(sem_t *sem)
{
	int val;

	if (sem_check_validity(sem) != 0)
		return (-1);

	while ((val = (*sem)->count) > 0) {
		if (atomic_cmpset_int(&(*sem)->count, val, val - 1))
			return (0);
	}
	errno = EAGAIN;
	return (-1);
}

int
_sem_wait(sem_t *sem)
{
	struct pthread *curthread;
	int val, oldcancel, retval;

	if (sem_check_validity(sem) != 0)
		return (-1);

	curthread = tls_get_curthread();
	_pthread_testcancel();
	do {
		while ((val = (*sem)->count) > 0) {
			if (atomic_cmpset_acq_int(&(*sem)->count, val, val - 1))
				return (0);
		}
		oldcancel = _thr_cancel_enter(curthread);
		retval = _thr_umtx_wait(&(*sem)->count, 0, NULL, 0);
		_thr_cancel_leave(curthread, oldcancel);
	} while (retval == 0);
	errno = retval;
	return (-1);
}

int
_sem_timedwait(sem_t * __restrict sem, const struct timespec * __restrict abstime)
{
	struct timespec ts, ts2;
	struct pthread *curthread;
	int val, oldcancel, retval;

	if (sem_check_validity(sem) != 0)
		return (-1);

	curthread = tls_get_curthread();

	/*
	 * The timeout argument is only supposed to
	 * be checked if the thread would have blocked.
	 */
	_pthread_testcancel();
	do {
		while ((val = (*sem)->count) > 0) {
			if (atomic_cmpset_acq_int(&(*sem)->count, val, val - 1))
				return (0);
		}
		if (abstime == NULL ||
		    abstime->tv_nsec >= 1000000000 || abstime->tv_nsec < 0) {
			errno = EINVAL;
			return (-1);
		}
		clock_gettime(CLOCK_REALTIME, &ts);
		TIMESPEC_SUB(&ts2, abstime, &ts);
		oldcancel = _thr_cancel_enter(curthread);
		retval = _thr_umtx_wait(&(*sem)->count, 0, &ts2,
					CLOCK_REALTIME);
		_thr_cancel_leave(curthread, oldcancel);
	} while (retval == 0);
	errno = retval;
	return (-1);
}

int
_sem_post(sem_t *sem)
{
	int val;
	
	if (sem_check_validity(sem) != 0)
		return (-1);

	/*
	 * sem_post() is required to be safe to call from within
	 * signal handlers, these code should work as that.
	 */
	do {
		val = (*sem)->count;
	} while (!atomic_cmpset_acq_int(&(*sem)->count, val, val + 1));
	_thr_umtx_wake(&(*sem)->count, val + 1);
	return (0);
}

sem_t *
_sem_open(__unused const char *name, __unused int oflag, ...)
{
	errno = ENOSYS;
	return (SEM_FAILED);
}

int
_sem_close(__unused sem_t *sem)
{
	errno = ENOSYS;
	return (-1);
}

int
_sem_unlink(__unused const char *name)
{
	errno = ENOSYS;
	return (-1);
}

__strong_reference(_sem_destroy, sem_destroy);
__strong_reference(_sem_getvalue, sem_getvalue);
__strong_reference(_sem_init, sem_init);
__strong_reference(_sem_trywait, sem_trywait);
__strong_reference(_sem_wait, sem_wait);
__strong_reference(_sem_timedwait, sem_timedwait);
__strong_reference(_sem_post, sem_post);
__strong_reference(_sem_open, sem_open);
__strong_reference(_sem_close, sem_close);
__strong_reference(_sem_unlink, sem_unlink);

