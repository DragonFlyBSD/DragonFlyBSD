/*
 * Copyright (c) 2013 Larisa Grigore <larisagrigore@gmail.com>.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by Adam Glass and Charles
 *	Hannum.
 * 4. The names of the authors may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "namespace.h"
#include <sys/param.h>
#include <sys/queue.h>
#include <sys/mman.h>
#include <sys/shm.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <err.h>
#include <pthread.h>
#include <unistd.h>
#include "un-namespace.h"

#include "sysvipc_lock.h"
#include "sysvipc_ipc.h"
#include "sysvipc_sockets.h"
#include "sysvipc_shm.h"
#include "sysvipc_hash.h"

#define SYSV_MUTEX_LOCK(x)		if (__isthreaded) _pthread_mutex_lock(x)
#define SYSV_MUTEX_UNLOCK(x)	if (__isthreaded) _pthread_mutex_unlock(x)
#define SYSV_MUTEX_DESTROY(x)	if (__isthreaded) _pthread_mutex_destroy(x)

struct hashtable *shmres = NULL;
struct hashtable *shmaddrs = NULL;
pthread_mutex_t lock_resources = PTHREAD_MUTEX_INITIALIZER;

/* File descriptor used to communicate with the daemon. */
extern int daemon_fd;
/* Structure used to save semaphore operation with SEMUNDO flag. */
extern struct sem_undo *undos;

static int
shminit(void) {
	if (shmres) {
		errno = EPERM;
		return (-1);
	}

	shmres = _hash_init(MAXSIZE);
	if (!shmres)
		goto out_resources;

	shmaddrs = _hash_init(MAXSIZE);
	if (!shmaddrs)
		goto out_addrs;

	return 0;

out_addrs:
	_hash_destroy(shmres);
out_resources:
	return -1;
}

/*static int
shmexit(void) {
	if (!shmres)
		return -EPERM;

	_hash_destroy(shmres);
	_hash_destroy(shmaddrs);
	SYSV_MUTEX_DESTROY(lock_resources);

	return 0;
}*/

/* Init sysv ipc resources and those used for shared memory. */
static int
shmcheck(void) {
	int ret;

	/* Init sysv resources. */
	if ((ret = sysvinit()) != 0)
		return (ret);
	/* Init resorces used for shared memory. */
	if ((ret = shminit()) < 0)
		return (ret);
	return (0);
}

/* Check if sysv ipc resources are initialized. */
static int
is_shm_started(void) {
	if (!is_sysvinit())
		return (0);
	if (!shmres)
		return (0);
	return (1);
}

/* OBS: It is used only a rwlock for both hashtables and
 * socket. I've made that choice because is I considered to
 * be much expensive to acquire/release more than one especially
 * as the daemon is not multithreading.
 */

/* This function has another parameter apart from shmget.
 * The parameter has information about the type of sysv
 * ipc resource (shm, sem, msg, undo).
 * The undo segment is used for sem ops with UNDO flag set.
 */
int
_shmget(key_t key, size_t size, int shmflg, int type) {
	struct shmget_msg msg;
	struct shm_data *data;
	int shmid, fd;
	int flags;

	SYSV_MUTEX_LOCK(&lock_resources);
	if (shmcheck() < 0) {
		sysv_print_err("init sysv ipc\n");
		goto done;
	}

	msg.key = key;
	msg.size = size;
	msg.shmflg = shmflg;
	msg.type = type;

	send_message(daemon_fd, type, (char *)&msg, sizeof(msg));

	/* Accept a file installed by the daemon.
	 * The file is used as shared memory. */
	fd = receive_fd(daemon_fd);
	if (fd < 0) {
		shmid = -1;
		goto done;
	}

	flags = _fcntl(fd, F_GETFD, 0);
	if (_fcntl(fd, F_SETFD, flags & FD_CLOEXEC) == -1) {
		sysv_print_err("fcntl error\n");
		shmid = -1;
		goto done;
	}

	/* Receive the resource id or error. */
	receive_message(daemon_fd, (char *)&shmid, sizeof(shmid));

	if (shmid < 0) {
		errno = -shmid;
		shmid = -1;
		goto done;
	}

	/* Do we already have an entry for this resource? */
	data = _hash_lookup(shmres, shmid);
	if (data)
		goto done;

	/* If not, add necessary data about it. */
	data = malloc(sizeof(struct shm_data));
	data->fd = fd;
	data->size = size;
	data->shmid = shmid;
	data->type = type;
	data->used = 0;
	data->removed = 0;
	data->access = 0; /* Used for sems. */

	/* Insert data in hashtable using the shmid. */
	_hash_insert(shmres, shmid, data);
done:
	SYSV_MUTEX_UNLOCK(&lock_resources);
	return (shmid);
}

int
sysvipc_shmget(key_t key, size_t size, int shmflg) {
	return (_shmget(key, size, shmflg, SHMGET));
}

void *
sysvipc_shmat(int shmid, const void *shmaddr, int shmflg) {
	struct shmat_msg msg;
	void *addr = NULL;
	int error;
	int flags, prot;
	size_t size;
	struct shm_data *data;

	SYSV_MUTEX_LOCK(&lock_resources);
	if (!is_shm_started()) {
		errno = EINVAL;
		goto done;
	}

	/* Get data using shmid. */
	data = _hash_lookup(shmres, shmid);
	if (data == NULL) {
		errno = EINVAL;
		goto done;
	}

	size = round_page(data->size);

#ifdef VM_PROT_READ_IS_EXEC
	prot = PROT_READ | PROT_EXECUTE;
#else
	prot = PROT_READ;
#endif
	if ((shmflg & SHM_RDONLY) == 0)
		prot |= PROT_WRITE;

	flags = MAP_SHARED;
	if (shmaddr) {
		if (shmflg & SHM_RND) {
			addr = (void *)((vm_offset_t)shmaddr & ~(SHMLBA-1));
		} else if (((vm_offset_t)shmaddr & (SHMLBA-1)) == 0) {
			addr = __DECONST(void *, shmaddr);
		} else {
			errno = EINVAL;
			goto done;
		}
	}

	msg.shmid = shmid;
	msg.shmaddr = shmaddr;
	msg.shmflg = shmflg;
	msg.size = data->size; /* For undo segment. */

	send_message(daemon_fd, SHMAT, (char *)&msg, sizeof(msg));
	receive_message(daemon_fd, (char *)&error, sizeof(error));
	if (error) {
		errno = error;
		goto done;
	}

	addr = mmap(addr, size, prot, flags, data->fd, 0);
	if (!addr) {
		sysv_print_err("mmap\n");
		/* Detach ourselves from the segment. */
		send_message(daemon_fd, SHMDT, (char *)&shmid, sizeof(shmid));
		goto done;
	}

	/* Necessary for SEMGET, MSGGET, UNDOGET. */
	data->internal = addr;

	/* Save the mapped address for munmap call. */
	_hash_insert(shmaddrs, (u_long)addr, data);
done:
	SYSV_MUTEX_UNLOCK(&lock_resources);
	return (addr);
}

/* Remove a sysv ipc resource. */
static
void shmremove(int shmid) {
	struct shm_data *data;
	data = _hash_remove(shmres, shmid);

	//TODO nu trebuie demapat?
	_close(data->fd);
	free(data);
	data = NULL;
}

int
sysvipc_shmctl(int shmid, int cmd, struct shmid_ds *buf) {
	int size, ret;
	struct shmctl_msg *msg;

/*	if (cmd == IPC_SET)
		size = sizeof(struct shmctl_msg) + sizeof(struct shmid_ds);
	else
		size = sizeof(struct shmctl_msg);
*/
	SYSV_MUTEX_LOCK(&lock_resources);

	ret = -1;

	if (!is_shm_started()) {
		errno = EINVAL;
		goto done;
	}

	size = sizeof(struct shmctl_msg);
	msg = malloc(size);
	msg->shmid = shmid;
	msg->cmd = cmd;

	if (cmd == IPC_SET)
		msg->buf = *buf;

	send_message(daemon_fd, SHMCTL, (char *)msg, sizeof(*msg));

	receive_message(daemon_fd, (char *)&ret, sizeof(ret));

	/* Get data in IPC_STAT case. */
	if (ret == 0 && cmd == IPC_STAT)
		receive_message(daemon_fd, (char *)buf, sizeof(*buf));

	/* Free all resources specific to a shmid in IPC_RMID case. */
	if (ret == 0 && cmd == IPC_RMID)
		shmremove(shmid);

	errno = ret;
done:
	SYSV_MUTEX_UNLOCK(&lock_resources);
	return (ret == 0 ? 0 : -1);
}

/* Functionality of shmdt with the possibility to inform or not
 * the daemon.
 * Inform the daemon when shmdt is called and not when an error
 * occurs and the daemon doesn't know that the process is attaced.
 */
static int
_shmdt(const void *shmaddr, int send_to_daemon) {
	int ret;
	size_t size;
	struct shm_data *data;

	ret = -1;

	SYSV_MUTEX_LOCK(&lock_resources);
	if (!is_shm_started()) {
		errno = EINVAL;
		goto done;
	}

	/* Verify if shmaddr was returned from a shmat call. */
	data = _hash_remove(shmaddrs, (u_long)shmaddr);
	if (data == NULL) {
		errno = EINVAL;
		goto done;
	}

	size = round_page(data->size);

	ret = munmap(__DECONST(void *, shmaddr), size);
	if (ret)
		goto done;

	if (send_to_daemon)
		send_message(daemon_fd, SHMDT, (char *)&data->shmid, sizeof(int));

	shmaddr = NULL;
	free(data);
	data = NULL;
done:
	SYSV_MUTEX_UNLOCK(&lock_resources);
	return (ret);
}

int
sysvipc_shmdt(const void *shmaddr) {
	return (_shmdt(shmaddr, 1));
}

void
shmchild(void) {
	int i;
	struct entries_list *list;
	struct hashentry *tmp, *ttmp;
	struct shmat_msg msg;
	struct shm_data *data;
	int error;

/* OBS: no locking is necessary because this function is called
 * after the child is created and at that moment only one thread
 * exists in the process.
 */
	for (i=0; i<get_hash_size(MAXSIZE); i++) {
		list = &shmaddrs->entries[i];
		if (LIST_EMPTY(list))
			continue;
		LIST_FOREACH_MUTABLE(tmp, list, entry_link, ttmp) {
			data = (struct shm_data*)tmp->value;
			/* Inform daemon that we are attached. */

			if (data->type == UNDOGET) {
				continue;
			}

			msg.shmid = data->shmid;
			msg.shmaddr = data->internal;
			msg.shmflg = 0; /* This is enough at this moment. */
			msg.size = data->size;
			/* Last field is not necessary because it is used only
			 * for undo segments.
			 */

			send_message(daemon_fd, SHMAT, (char *)&msg, sizeof(msg));
			receive_message(daemon_fd, (char *)&error, sizeof(error));

			/* If the daemon returned error munmap the region. */
			if (error) {
				errno = error;
				_shmdt(data->internal, 0);
				shmremove(data->shmid);
				sysv_print_err(" %d shmchild\n", error);
				sleep(20);
			}
			
		}
	}

	/* Remove semundo structures. Those are specific only for the parent.
	 * The child must create for itself a new one.
	 */
	data = _hash_remove(shmaddrs, (u_long)undos);
	if (undos) {
		munmap(undos, round_page(data->size));
		undos = NULL;
	}
}

/* Called each time a thread tries to access the sem/msg.
 * It is used in order to protect data against its removal
 * by another thread.
 */
struct shm_data*
get_shmdata(int id, int to_remove, int shm_access) {
	struct shm_data *data = NULL;

	SYSV_MUTEX_LOCK(&lock_resources);
	if (!is_shm_started()) {
		errno = EINVAL;
		goto done;
	}

	data = _hash_lookup(shmres, id);
	if (!data) {
		errno = EINVAL;
		goto done;
	}

	/* If segment was removed by another thread we can't use it. */
	if (data->removed) {
		sysv_print("segment already removed\n");
		errno = EINVAL;
		data = NULL;
		goto done;
	}

	/* Mark for removal. Inform the other threads from the
	 * same address space. */
	if (to_remove) {
		sysv_print("segment is removed\n");
		data->removed = to_remove; /* 1 if it is removed by
		the current process and 2 if it was removed by
		another one. */

		/* No need for any rights check because this is
		 * done by daemon if this is the process that removes
		 * the sem/msg.
		 * If not, there is no need for any right to clean
		 * internal resources.
		 */
		goto done2;
	}

	/* Avoid segmentation fault if the memory zone
	 * is accessed without necessary permissions
	 * (it was mapped according to them).
	 */
	if (!(data->access & shm_access)) {
#if 0
		sysv_print("no access rights has %o and wants %o\n",
				data->access, shm_access);
		errno = EACCES;
		data = NULL;
		goto done;
#endif
	}

done2:
	data->used++;
done:
	SYSV_MUTEX_UNLOCK(&lock_resources);
	return (data);
}

/* Set the shm_access type (IPC_R, IPC_W) for sem/msg. */
int
set_shmdata_access(int id, int shm_access) {
	struct shm_data *data;
	int ret = -1;

	SYSV_MUTEX_LOCK(&lock_resources);
	if (!is_shm_started()) {
		errno = EINVAL;
		goto done;
	}

	data = _hash_lookup(shmres, id);
	if (!data) {
		errno = EINVAL;
		goto done;
	}

	/* If segment was removed by another thread we can't use it. */
	if (data->removed) {
		errno = EINVAL;
		goto done;
	}

	data->access = shm_access;
	ret = 0;
done:
	SYSV_MUTEX_UNLOCK(&lock_resources);

	return (ret);
}
