#ifndef _LIBC_RTLD_PRIVATE_H_
#define	_LIBC_RTLD_PRIVATE_H_

#include "../libc/include/libc_private.h"

#define	__isthreaded	0
#define	_pthread_mutex_lock(mtx)	((void)0)
#define	_pthread_mutex_unlock(mtx)	((void)0)
#define	_pthread_mutex_destroy(mtx)	((void)0)
#define	_pthread_mutex_trylock(mtx)	(0)

#endif
