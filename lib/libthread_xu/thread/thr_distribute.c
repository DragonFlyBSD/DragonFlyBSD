
#include "namespace.h"
#include <machine/tls.h>
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
#ifdef _PTHREADS_DEBUGGING
#include <stdio.h>
#endif
#include "un-namespace.h"

#include "thr_private.h"
void
_libthread_distribute_static_tls(size_t offset, void *src,
				 size_t len, size_t total_len);

void
_libthread_distribute_static_tls(size_t offset, void *src,
				 size_t len, size_t total_len)
{
	struct pthread *curthread = tls_get_curthread();
	struct pthread *td;
	char *tlsbase;

	THREAD_LIST_LOCK(curthread);
	TAILQ_FOREACH(td, &_thread_list, tle) {
		tlsbase = (char *)td->tcb - offset;
		memcpy(tlsbase, src, len);
		memset(tlsbase + len, 0, total_len - len);
	}
	THREAD_LIST_UNLOCK(curthread);
}

__strong_reference(_libthread_distribute_static_tls, _pthread_distribute_static_tls);
