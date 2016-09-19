/*-
 * Copyright (c) 2003 David Xu <davidxu@freebsd.org>
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
 */

#include "namespace.h"
#include <machine/tls.h>
#include <errno.h>
#include <stdlib.h>
#include <pthread.h>
#include <pthread_np.h>
#include "un-namespace.h"

#include "thr_private.h"

#define SPIN_COUNT 100000

int
_pthread_spin_init(pthread_spinlock_t *lock, int pshared)
{
	struct pthread_spinlock	*lck;

	if (lock == NULL || pshared != PTHREAD_PROCESS_PRIVATE)
		return (EINVAL);
	if ((lck = malloc(sizeof(struct pthread_spinlock))) == NULL)
		return (ENOMEM);
	_thr_umtx_init(&lck->s_lock);
	*lock = lck;
	return (0);
}

int
_pthread_spin_destroy(pthread_spinlock_t *lock)
{
	int ret;

	if (lock == NULL || *lock == NULL) {
		ret = EINVAL;
	} else {
		free(*lock);
		*lock = NULL;
		ret = 0;
	}
	return (ret);
}

int
_pthread_spin_trylock(pthread_spinlock_t *lock)
{
	struct pthread *curthread = tls_get_curthread();
	struct pthread_spinlock	*lck;

	if (lock == NULL || (lck = *lock) == NULL)
		return (EINVAL);
	return (THR_UMTX_TRYLOCK(curthread, &lck->s_lock));
}

int
_pthread_spin_lock(pthread_spinlock_t *lock)
{
	struct pthread *curthread;
	struct pthread_spinlock	*lck;
	int count;

	if (lock == NULL || (lck = *lock) == NULL)
		return (EINVAL);

	curthread = tls_get_curthread();
	count = SPIN_COUNT;
	while (THR_UMTX_TRYLOCK(curthread, &lck->s_lock) != 0) {
		while (lck->s_lock) {
			CPU_SPINWAIT;	/* tell cpu we are spinning */
			if (--count <= 0) {
				count = SPIN_COUNT;
				_pthread_yield();
			}
		}
	}
	return (0);
}

int
_pthread_spin_unlock(pthread_spinlock_t *lock)
{
	struct pthread *curthread = tls_get_curthread();
	struct pthread_spinlock	*lck;

	if (lock == NULL || (lck = *lock) == NULL)
		return (EINVAL);
	/* XXX: shouldn't return status? */
	THR_UMTX_UNLOCK(curthread, &lck->s_lock);
	return (0);
}

__strong_reference(_pthread_spin_init, pthread_spin_init);
__strong_reference(_pthread_spin_destroy, pthread_spin_destroy);
__strong_reference(_pthread_spin_trylock, pthread_spin_trylock);
__strong_reference(_pthread_spin_lock, pthread_spin_lock);
__strong_reference(_pthread_spin_unlock, pthread_spin_unlock);
