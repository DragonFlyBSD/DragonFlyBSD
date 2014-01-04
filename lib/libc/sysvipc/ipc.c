/**
 * Copyright (c) 2013 Larisa Grigore<larisagrigore@gmail.com>.
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
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "namespace.h"
#include <sys/param.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include "un-namespace.h"

#include <stdio.h>

#include "sysvipc_ipc.h"
#include "sysvipc_sockets.h"
#include "sysvipc_sem.h"
#include "sysvipc_shm.h"
#include "sysvipc_hash.h"
#include "sysvipc_lock.h"
#include "sysvipc_lock_generic.h"

#define SYSV_MUTEX_LOCK(x)		if (__isthreaded) _pthread_mutex_lock(x)
#define SYSV_MUTEX_UNLOCK(x)	if (__isthreaded) _pthread_mutex_unlock(x)
#define SYSV_MUTEX_DESTROY(x)	if (__isthreaded) _pthread_mutex_destroy(x)

int daemon_fd = -1;

extern pthread_mutex_t lock_resources;
//extern pthread_rwlock_t rwlock_addrs;
extern pthread_mutex_t lock_undo;
extern struct hashtable *shmaddrs;

/* Send the type of the message followed by data. */
int
send_message(int fd, int type, char *data, int size) {
	_write(fd, &type, sizeof(type));
	return (send_msg_with_cred(fd, data, size));
}

/* Receive the type of the message that will follow. */
int
receive_type_message(int fd) {
	int type;
	int r = _read(fd, &type, sizeof(type));
	return (r == 0 ? 0 : type);
}

/* Receive data. */
int
receive_message(int fd, char *data, int size) {
	_read(fd, data, size);
	return (0);
}

int
is_sysvinit(void) {
	return (daemon_fd == -1 ? 0:1);
}

static int
register_to_daemon(void) {
	int flags;
	char test = 't';

	daemon_fd = connect_to_daemon(LISTEN_SOCKET_FILE);

	flags = _fcntl(daemon_fd, F_GETFD, 0);
	if (_fcntl(daemon_fd, F_SETFD, flags & FD_CLOEXEC) == -1) {
		sysv_print_err("fcntl error\n");
		return (-1);
	}

	/* Send a message such that daemon can obtain process credentials.*/
	send_msg_with_cred(daemon_fd, &test, sizeof(test));

	sysv_print("register to daemon: sock fd = %d\n", daemon_fd);

	return (0);
}

/* Used in fork case, to avoid deadlocks.
 * The fork caller acquires all locks before fork and release them
 * after because the child will have only a thread. If one lock is
 * taken by another thread than, in the child process, nobody will
 * release it.
 */
static void
acquire_locks(void) {
	struct entries_list *list;
	struct hashentry *tmp;
	struct shm_data *data;
	struct semid_pool *semaptr;
	int i;

	SYSV_MUTEX_LOCK(&lock_undo);
	SYSV_MUTEX_LOCK(&lock_resources);
	//pthread_rwlock_wrlock(&rwlock_addrs);

	for (i=0; i<get_hash_size(MAXSIZE); i++) {
		list = &shmaddrs->entries[i];
		if (LIST_EMPTY(list))
			continue;
		LIST_FOREACH(tmp, list, entry_link) {
			data = (struct shm_data*)tmp->value;
			if (data->type == SEMGET) {
				semaptr = (struct semid_pool *)data->internal;
#ifdef SYSV_RWLOCK
#ifdef SYSV_SEMS
				/* There is no need to acquire the mutexes from
				 * each semaphore in the group. It is enough
				 * to acquire the group lock in write mode.
				 */
#endif
				sysv_rwlock_wrlock(&semaptr->rwlock);
#else
				sysv_mutex_lock(&semaptr->mutex);
#endif
			}
		}
	}
}

/* Function called by parent after fork to release locks
 * acquired before fork.
 */
static void
parent_release_locks(void) {
	struct entries_list *list;
	struct hashentry *tmp;
	struct shm_data *data;
	struct semid_pool *semaptr;
	int i;

	SYSV_MUTEX_UNLOCK(&lock_undo);
	SYSV_MUTEX_UNLOCK(&lock_resources);
	//pthread_rwlock_unlock(&rwlock_addrs);

	for (i=0; i<get_hash_size(MAXSIZE); i++) {
		list = &shmaddrs->entries[i];
		if (LIST_EMPTY(list))
			continue;
		LIST_FOREACH(tmp, list, entry_link) {
			data = (struct shm_data*)tmp->value;
			if (data->type == SEMGET) {
				semaptr = (struct semid_pool *)data->internal;
#ifdef SYSV_RWLOCK
				sysv_rwlock_unlock(&semaptr->rwlock);
#else
				sysv_mutex_unlock(&semaptr->mutex);
#endif
			}
		}
	}
}

/* Function called by child after fork to release locks
 * acquired before fork by the parent.
 * Only locks specific to the address space are released.
 * Those created in the shared memory are released by the
 * parent.
 */
static void
child_release_locks(void) {
	SYSV_MUTEX_UNLOCK(&lock_undo);
	SYSV_MUTEX_UNLOCK(&lock_resources);
	//pthread_rwlock_unlock(&rwlock_addrs);
}

static void
prepare_parent_atfork(void) {
	/* Function called only if the process has
	 * sysv ipc structures initialized.
	 */
	if (!is_sysvinit())
		return;

	/* Acquire all locks to be sure that neither one is
	 * held by another thread.
	 */
	acquire_locks();
}

static void
parent_atfork(void) {
	if (!is_sysvinit())
		return;

	/* Release locks acquired before fork. */
	parent_release_locks();
}

static void
child_atfork(void) {
	if (!is_sysvinit())
		return;

	/* Release locks acquired before fork. */
	child_release_locks();
	/* Close the file descriptor used by parent. */
	_close(daemon_fd);

	/* Register it to daemon too. */
	if (register_to_daemon() < 0) {
		sysv_print_err("register to daemon error\n");
		exit(-1);
	}

	/* Inform the daemon about each shared memory segment used. */
	shmchild();
}

/* The function is called only once, when the process uses for
 * the first time sysv ipc resources.
 */
int
sysvinit(void) {

	if (is_sysvinit()) {
		return (IPC_INITIALIZED);
	}

	if (register_to_daemon() < 0)
		return (-1);

	/* Add handlers for parent and child when fork is called. */
	if (_pthread_atfork(prepare_parent_atfork, parent_atfork,
				child_atfork) < 0) {
		sysv_print_err("pthread_atfork error\n");
		return (-1);
	}
	return 0;
}

int
sysvexit(void) {
	if (!is_sysvinit())
		return (-1);

	return (0);
}
