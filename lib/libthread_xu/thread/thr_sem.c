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
#include <sys/queue.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "un-namespace.h"
#include "thr_private.h"

#define container_of(ptr, type, member)				\
({								\
	__typeof(((type *)0)->member) *_p = (ptr);		\
	(type *)((char *)_p - offsetof(type, member));		\
})

/*
 * Semaphore definitions.
 */
struct sem {
	u_int32_t		magic;
	volatile umtx_t		count;
	int			semid;
	int			unused; /* pad */
};

#define	SEM_MAGIC	((u_int32_t) 0x09fa4012)

static char const *sem_prefix = "/var/run/sem";


/*
 * POSIX requires that two successive calls to sem_open return
 * the same address if no call to unlink nor close have been
 * done in the middle. For that, we keep a list of open semaphore
 * and search for an existing one before remapping a semaphore.
 * We have to keep the fd open to check for races.
 *
 * Example :
 * sem_open("/test", O_CREAT | O_EXCL...) -> fork() ->
 * parent :
 *   sem_unlink("/test") -> sem_open("/test", O_CREAT | O_EXCl ...)
 * child :
 *   sem_open("/test", 0).
 * We need to check that the cached mapping is the one of the most up
 * to date file linked at this name, or child process will reopen the
 * *old* version of the semaphore, which is wrong.
 *
 * fstat and nlink check is used to test for this race.
 */

struct sem_info {
	int open_count;
	ino_t inode;
	dev_t dev;
	int fd;
	sem_t sem;
	LIST_ENTRY(sem_info) next;
};



static pthread_once_t once = PTHREAD_ONCE_INIT;
static pthread_mutex_t sem_lock;
static LIST_HEAD(,sem_info) sem_list = LIST_HEAD_INITIALIZER(sem_list);


#define SEMID_LWP	0
#define SEMID_FORK	1
#define SEMID_NAMED	2

static void
sem_prefork(void)
{
	_pthread_mutex_lock(&sem_lock);
}

static void
sem_postfork(void)
{
	_pthread_mutex_unlock(&sem_lock);
}

static void
sem_child_postfork(void)
{
	_pthread_mutex_unlock(&sem_lock);
}

static void
sem_module_init(void)
{
	pthread_mutexattr_t ma;

	_pthread_mutexattr_init(&ma);
	_pthread_mutexattr_settype(&ma,  PTHREAD_MUTEX_RECURSIVE);
	_pthread_mutex_init(&sem_lock, &ma);
	_pthread_mutexattr_destroy(&ma);
	_pthread_atfork(sem_prefork, sem_postfork, sem_child_postfork);
}

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
	if (sem == NULL) {
		errno = EINVAL;
		return (-1);
	}

	*sem = sem_alloc(value, pshared);
	if (*sem == NULL)
		return (-1);
	return (0);
}

int
_sem_destroy(sem_t *sem)
{
	if (sem_check_validity(sem) != 0) {
		errno = EINVAL;
		return (-1);
	}

	(*sem)->magic = 0;

	switch ((*sem)->semid) {
		case SEMID_LWP:
			free(*sem);
			break;
		case SEMID_FORK:
			/* memory is left intact */
			break;
		default:
			errno = EINVAL;
			return (-1);
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

static int
get_path(const char *name, char *path, size_t len, char const **prefix)
{
	size_t path_len;

	*prefix = NULL;
	
	if (name[0] == '/') {
		*prefix = getenv("LIBTHREAD_SEM_PREFIX");

		if (*prefix == NULL)
			*prefix = sem_prefix;

		path_len = strlcpy(path, *prefix, len);

		if (path_len > len) {
			return (ENAMETOOLONG);
		}
	}

	path_len = strlcat(path, name, len);

	if (path_len > len)
		return (ENAMETOOLONG);

	return (0);
}


static sem_t *
sem_get_mapping(ino_t inode, dev_t dev)
{
	struct sem_info *ni;
	struct stat sbuf;

	LIST_FOREACH(ni, &sem_list, next) {
		if (ni->inode == inode && ni->dev == dev) {
			/* Check for races */
			if(_fstat(ni->fd, &sbuf) == 0) {
				if (sbuf.st_nlink > 0) {
					ni->open_count++;
					return (&ni->sem);
				} else {
					ni->inode = 0;
					LIST_REMOVE(ni, next);
				}
			}
			return (SEM_FAILED);

		}
	}

	return (SEM_FAILED);
}


static sem_t *
sem_add_mapping(ino_t inode, dev_t dev, sem_t sem, int fd)
{
	struct sem_info *ni;

	ni = malloc(sizeof(struct sem_info));
	if (ni == NULL) {
		errno = ENOSPC;
		return (SEM_FAILED);
	}

	bzero(ni, sizeof(*ni));
	ni->open_count = 1;
	ni->sem = sem;
	ni->fd = fd;
	ni->inode = inode;
	ni->dev = dev;

	LIST_INSERT_HEAD(&sem_list, ni, next);

	return (&ni->sem);
}

static int
sem_close_mapping(sem_t *sem)
{
	struct sem_info *ni;

	if ((*sem)->semid != SEMID_NAMED)
		return (EINVAL);

	ni = container_of(sem, struct sem_info, sem);

	if ( --ni->open_count > 0) {
		return (0);
	} else {
		if (ni->inode != 0) {
			LIST_REMOVE(ni, next);
		}	
		munmap(ni->sem, getpagesize());
		__sys_close(ni->fd);
		free(ni);
		return (0);
	}
}

sem_t *
_sem_open(const char *name, int oflag, ...)
{
	char path[PATH_MAX];
	char tmppath[PATH_MAX];
	char const *prefix = NULL;
	char *tmpname = NULL;
	size_t path_len;
	int error, fd, create;
	sem_t *sem;
	sem_t semtmp;
	va_list ap;
	mode_t mode;
	struct stat sbuf;
	unsigned int value = 0;
	unsigned int retry_count;
	
	create = 0;
	error = 0;
	fd = -1;
	sem = SEM_FAILED;

	/*
	 * Bail out if invalid flags specified.
	 */
	if (oflag & ~(O_CREAT|O_EXCL)) {
		errno = EINVAL;
		return (SEM_FAILED);
	}

	oflag |= O_RDWR;
	oflag |= O_CLOEXEC;

	if (name == NULL) {
		errno = EINVAL;
		return (SEM_FAILED);
	}

	_pthread_once(&once, sem_module_init);

	_pthread_mutex_lock(&sem_lock);

	error = get_path(name, path, PATH_MAX, &prefix);
	if (error) {
		errno = error;
		goto error;
	}

retry:
	tmpname = NULL;
	retry_count = 10;

	fd = __sys_open(path, O_RDWR | O_CLOEXEC);

	if (fd > 0) {
		
		if ((oflag & O_EXCL) == O_EXCL) {
			__sys_close(fd);
			errno = EEXIST;
			goto error;
		}

		if (_fstat(fd, &sbuf) != 0) {
			/* Bad things happened, like another thread closing our descriptor */
			__sys_close(fd);
			errno = EINVAL;
			goto error;
		}

		sem = sem_get_mapping(sbuf.st_ino, sbuf.st_dev);

		if (sem != SEM_FAILED) {
			__sys_close(fd);
			goto done;
		}

		if ((sbuf.st_mode & S_IFREG) == 0) {
			/* We only want regular files here */
			__sys_close(fd);
			errno = EINVAL;
			goto error;
		}
	} else if ((oflag & O_CREAT) && errno == ENOENT) {

		va_start(ap, oflag);

		mode = (mode_t) va_arg(ap, int);
		value = (unsigned int) va_arg(ap, int);

		va_end(ap);

		if (value > SEM_VALUE_MAX) {
			errno = EINVAL;
			goto error;
		}

		strlcpy(tmppath, prefix, sizeof(tmppath));
		path_len = strlcat(tmppath, "/sem.XXXXXX", sizeof(tmppath));

		if (path_len > sizeof(tmppath)) {
			errno = ENAMETOOLONG;
			goto error;	
		}

		while (retry_count-- > 0) {
			tmpname = mktemp(tmppath);

			if ( tmpname == NULL) {
				errno = EINVAL;
				goto error;
			}

			fd = __sys_open(tmpname, O_RDWR | O_CLOEXEC | O_CREAT | O_EXCL, mode);

			if (fd > 0 || errno != EEXIST) {
				break;
			}

		}

		if (retry_count == 0) {
			__sys_close(fd);
			errno = ENOSPC; /* XXX POSIX does not allow for EAGAIN */
			goto error;
		}

		create = 1;
	}

	if (fd == -1) {
		switch (errno) {
			case ENOTDIR:
			case EISDIR:
			case EMLINK:
			case ELOOP:
				errno = EINVAL;
				break;
			case EDQUOT:
			case EIO:
				errno = ENOSPC;
				break;
			case EROFS:
				errno = EACCES;
		}
		goto error;
	}

	semtmp = (sem_t) mmap(NULL, getpagesize(), PROT_READ | PROT_WRITE,
			MAP_NOSYNC | MAP_SHARED, fd, 0);

	if (semtmp == MAP_FAILED) {
		if (errno != EACCES && errno != EMFILE)
			errno = ENOMEM;

		if (create)
			unlink(tmpname);

		__sys_close(fd);	
		goto error;
	}

	if (create) {
		ftruncate(fd, sizeof(struct sem));
		semtmp->magic = SEM_MAGIC;
		semtmp->count = (u_int32_t)value;
		semtmp->semid = SEMID_NAMED;

		if (link(tmpname, path) != 0) {
			munmap(semtmp, getpagesize());
			__sys_close(fd);
			unlink(tmpname);

			if (errno == EEXIST && (oflag & O_EXCL) == 0) {
				goto retry;
			}

			goto error;
		}
		unlink(tmpname);

		if (_fstat(fd, &sbuf) != 0) {
			/* Bad things happened, like another thread closing our descriptor */
			munmap(semtmp, getpagesize());
			__sys_close(fd);
			errno = EINVAL;
			goto error;
		}

	}
	sem = sem_add_mapping(sbuf.st_ino, sbuf.st_dev, semtmp, fd);

done:
	_pthread_mutex_unlock(&sem_lock);
	return (sem);

error:
	_pthread_mutex_unlock(&sem_lock);
	return (SEM_FAILED);

}

int
_sem_close(sem_t *sem)
{
	_pthread_once(&once, sem_module_init);

	_pthread_mutex_lock(&sem_lock);

	if (sem_check_validity(sem)) {
		_pthread_mutex_unlock(&sem_lock);
		errno = EINVAL;
		return (-1);
	}

	if (sem_close_mapping(sem)) {
		_pthread_mutex_unlock(&sem_lock);
		errno = EINVAL;
		return (-1);
	}
	_pthread_mutex_unlock(&sem_lock);

	return (0);
}

int
_sem_unlink(const char *name)
{
	char path[PATH_MAX];
	const char *prefix;
	int error;

	error = get_path(name, path, PATH_MAX, &prefix);
	if (error) {
		errno = error;
		return (-1);
	}

	error = unlink(path);

	if(error) {
		if (errno != ENAMETOOLONG && errno != ENOENT)
			errno = EACCES;

		return (-1);
	}

	return (0);
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

