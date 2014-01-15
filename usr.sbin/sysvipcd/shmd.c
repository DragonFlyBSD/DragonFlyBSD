/*
 * Copyright (c) 1994 Adam Glass and Charles Hannum.  All rights reserved.
 * Copyright (c) 2013 Larisa Grigore <larisagrigore@gmail.com>
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

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/param.h>
#include <sys/vmmeter.h>
#include <sys/wait.h>
#include <sys/queue.h>
#include <sys/mman.h>

#include <err.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>

#include "limits.h"
#include "perm.h"
#include "utilsd.h"

#include "shmd.h"
#include "sysvipc_hash.h"
#include "sysvipc_sockets.h"

static struct	shminfo shminfo = {
//	0,
	SHMMIN,
	SHMMNI,
	SHMSEG
//	0
};

/* Shared memory.*/
static int shm_last_free, shm_committed, shmalloced;
int shm_nused;
static struct shmid_ds	*shmsegs;

/* Message queues.*/
extern struct msginfo msginfo;

extern struct hashtable *clientshash;

static int
create_sysv_file(struct shmget_msg *msg, size_t size,
		struct shmid_ds *shmseg) {
	char filename[FILENAME_MAX];
	int fd;
	void *addr;
	int nsems;
	struct semid_pool *sems;
	struct msqid_pool *msgq;
	key_t key = msg->key;
	int i;

	errno = 0;

	switch(msg->type) {
		case SHMGET:
			sprintf(filename, "%s/%s_%ld", DIRPATH, SHM_NAME, key);
			break;
		case SEMGET:
			sprintf(filename, "%s/%s_%ld", DIRPATH, SEM_NAME, key);
			break;
		case MSGGET:
			sprintf(filename, "%s/%s_%ld", DIRPATH, MSG_NAME, key);
			break;
		case UNDOGET:
			sprintf(filename, "%s/%s_%ld", DIRPATH, UNDO_NAME, key);
			break;
		default:
			return (-EINVAL);
	}

	fd = open(filename, O_RDWR | O_CREAT, 0666);
	if (fd < 0) {
		sysvd_print_err("create sysv file: open\n");
		goto out;
	}

	ftruncate(fd, size);

	switch(msg->type) {
		case SEMGET:
			/* Map the semaphore to initialize it. */
			addr = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
			//TODO modify 0 for more sems on a page
			if (!addr) {
				sysvd_print_err("create sysv file: mmap");
				goto error;
			}

			/* There is no need for any lock because all clients
			 * that try to access this segment are blocked until
			 * it becames ~SHMSEG_REMOVED. */
			sems = (struct semid_pool*)addr;
			nsems = (msg->size - sizeof(struct semid_pool)) /
				sizeof(struct sem);
			sysvd_print("alocate %d sems\n", nsems);

			/* Init lock. */
#ifdef SYSV_RWLOCK
			sysv_rwlock_init(&sems->rwlock);
#else
			sysv_mutex_init(&sems->mutex);
#endif
			/* Credentials are kept in shmid_ds structure. */
			sems->ds.sem_perm.seq = shmseg->shm_perm.seq;
			sems->ds.sem_nsems = nsems;
			sems->ds.sem_otime = 0;
			//sems->ds.sem_ctime = time(NULL);
			//semtot += nsems;
			sems->gen = 0;

			/* Initialize each sem. */
			memset(sems->ds.sem_base, 0, nsems + sizeof(struct sem));

#ifdef SYSV_SEMS
			int l;
			for (l=0; l < nsems; l++)
				sysv_mutex_init(&sems->ds.sem_base[l].sem_mutex);
#endif

			munmap(addr, size);

			break;
		case MSGGET:
			/* Map the message queue to initialize it. */
			addr = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
			if (!addr) {
				sysvd_print_err("create sysv file: mmap");
				goto error;
			}

			/* There is no need for any lock because all clients
			 * that try to access this segment are blocked until
			 * it becames ~SHMSEG_REMOVED. */
			msgq = (struct msqid_pool*)addr; //TODO
			/*sysvd_print("Attention!!! : %ld %ld %ld %ld\n",
					sizeof(struct msqid_pool),
					sizeof(msgq->msghdrs),
					sizeof(msgq->msgmaps),
					sizeof(msgq->msgpool));*/

			/* Init lock. */
#ifdef SYSV_RWLOCK
			sysv_rwlock_init(&msgq->rwlock);
#else
			sysv_mutex_init(&msgq->mutex);
#endif
			/* In kernel implementation, this was done globally. */
			for (i = 0; i < msginfo.msgseg; i++) {
				if (i > 0)
					msgq->msgmaps[i-1].next = i;
				msgq->msgmaps[i].next = -1;	/* implies entry is available */
			}
			msgq->free_msgmaps = 0;
			msgq->nfree_msgmaps = msginfo.msgseg;

			for (i = 0; i < msginfo.msgtql; i++) {
				msgq->msghdrs[i].msg_type = 0;
				if (i > 0)
					msgq->msghdrs[i-1].msg_next = i;
				msgq->msghdrs[i].msg_next = -1;
			}
			msgq->free_msghdrs = 0;

			/* Credentials are kept in shmid_ds structure. */
			msgq->ds.msg_perm.seq = shmseg->shm_perm.seq;
			msgq->ds.first.msg_first_index = -1;
			msgq->ds.last.msg_last_index = -1;
			msgq->ds.msg_cbytes = 0;
			msgq->ds.msg_qnum = 0;
			msgq->ds.msg_qbytes = msginfo.msgmnb;
			msgq->ds.msg_lspid = 0;
			msgq->ds.msg_lrpid = 0;
			msgq->ds.msg_stime = 0;
			msgq->ds.msg_rtime = 0;

			munmap(addr, size);

			break;
		default:
			break;
	}

	unlink(filename);
out:
	return (fd);
error:
	close(fd);
	return (-1);
}

/* Install for the client the file corresponding to fd. */
static int
install_fd_client(pid_t pid, int fd) {
	int ret;
	struct client *cl = _hash_lookup(clientshash, pid);
	if (!cl) {
		sysvd_print_err("no client entry for pid = %d\n", pid);
		return (-1);
	}

	ret = send_fd(cl->sock, fd);
	if (ret < 0) {
		sysvd_print_err("can not send fd to client %d\n", pid);
		return (-1);
	}

	return (0);
}

static int
shm_find_segment_by_key(key_t key)
{
	int i;

	for (i = 0; i < shmalloced; i++) {
		if ((shmsegs[i].shm_perm.mode & SHMSEG_ALLOCATED) &&
				shmsegs[i].shm_perm.key == key)
			return (i);
	}
	return (-1);
}

static struct shmid_ds *
shm_find_segment_by_shmid(int shmid)
{
	int segnum;
	struct shmid_ds *shmseg;

	segnum = IPCID_TO_IX(shmid);
	if (segnum < 0 || segnum >= shmalloced) {
		sysvd_print_err("segnum out of range\n");
		return (NULL);
	}

	shmseg = &shmsegs[segnum];
	if ((shmseg->shm_perm.mode & (SHMSEG_ALLOCATED | SHMSEG_REMOVED))
			!= SHMSEG_ALLOCATED ||
			shmseg->shm_perm.seq != IPCID_TO_SEQ(shmid)) {
		sysvd_print("segment most probably removed\n");
		return (NULL);
	}
	return (shmseg);
}

/* Remove a shared memory segment. */
static void
shm_deallocate_segment(int segnum)
{
	size_t size;
	struct shmid_ds *shmseg = &shmsegs[segnum];
	struct shm_handle *internal =
		(struct shm_handle *)shmseg->shm_internal;
//	int nsems;

	sysvd_print("deallocate segment %d\n", segnum);

	size = round_page(shmseg->shm_segsz);

#if 0
	if (internal->type == SEMGET) {
			nsems = (shmseg->shm_segsz - sizeof(struct semid_pool)) /
				sizeof(struct sem);
			semtot -= nsems;
			sysvd_print("freed %d sems\n", nsems);
	}
#endif

	/* Close the corresponding file. */
	close(internal->fd);

	/* Free other resources. */
	free(shmseg->shm_internal);
	shmseg->shm_internal = NULL;
	shm_committed -= btoc(size);
	shm_nused--;

	shmseg->shm_perm.mode = SHMSEG_FREE;
}

static void *map_seg(int);
static int munmap_seg(int, void *);

/* In sem and msg case notify the other processes that use it. */
static void
mark_segment_removed(int shmid, int type) {
	struct semid_pool *semaptr;
	struct msqid_pool *msgq;

	switch (type) {
		case SEMGET:
			semaptr = (struct semid_pool *)map_seg(shmid);
#ifdef SYSV_RWLOCK
			sysv_rwlock_wrlock(&semaptr->rwlock);
#else
			sysv_mutex_lock(&semaptr->mutex);
#endif
			semaptr->gen = -1;

			/* It is not necessary to wake waiting threads because
			 * if the group of semaphores is acquired by a thread,
			 * the smaptr lock is held, so it is impossible to
			 * reach this point.
			 */
#ifdef SYSV_RWLOCK
			sysv_rwlock_unlock(&semaptr->rwlock);
#else
			sysv_mutex_unlock(&semaptr->mutex);
#endif
			munmap_seg(shmid, semaptr);
			break;
		case MSGGET :
			msgq = (struct msqid_pool*)map_seg(shmid);
#ifdef SYSV_RWLOCK
			sysv_rwlock_wrlock(&msgq->rwlock);
#else
			sysv_mutex_lock(&msgq->mutex);
#endif
			msgq->gen = -1;

#ifdef SYSV_RWLOCK
			sysv_rwlock_unlock(&msgq->rwlock);
#else
			sysv_mutex_unlock(&msgq->mutex);
#endif
			munmap_seg(shmid, msgq);
			break;
		default:
			break;
	}
}

/* Get the id of an existing shared memory segment. */
static int
shmget_existing(struct shmget_msg *shmget_msg, int mode,
		int segnum, struct cmsgcred *cred)
{
	struct shmid_ds *shmseg;
	int error;

	shmseg = &shmsegs[segnum];
	if (shmseg->shm_perm.mode & SHMSEG_REMOVED) {
		/*
		 * This segment is in the process of being allocated.  Wait
		 * until it's done, and look the key up again (in case the
		 * allocation failed or it was freed).
		 */
		//TODO Maybe it will be necessary if the daemon is multithreading
		/*shmseg->shm_perm.mode |= SHMSEG_WANTED;
		  error = tsleep((caddr_t)shmseg, PCATCH, "shmget", 0);
		  if (error)
		  return error;
		  return EAGAIN;*/
	}
	if ((shmget_msg->shmflg & (IPC_CREAT | IPC_EXCL)) == (IPC_CREAT | IPC_EXCL))
		return (-EEXIST);
	error = ipcperm(cred, &shmseg->shm_perm, mode);
	if (error)
		return (-error);
	if (shmget_msg->size && (shmget_msg->size > shmseg->shm_segsz))
		return (-EINVAL);
	return (IXSEQ_TO_IPCID(segnum, shmseg->shm_perm));
}

/* Create a shared memory segment and return the id. */
static int
shmget_allocate_segment(pid_t pid, struct shmget_msg *shmget_msg,
		int mode, struct cmsgcred *cred)
{
	int i, segnum, shmid;
	size_t size;
	struct shmid_ds *shmseg;
	struct shm_handle *handle;
#if 0
	/* It is possible after a process calls exec().
	 * We don't create another segment but return the old one
	 * with all information.
	 * This segment is destroyed only when process dies.
	 * */
	if (shmget_msg->type == UNDOGET) {
		struct client *cl= _hash_lookup(clientshash, pid);
		if (cl->undoid != -1)
			return cl->undoid;
	}
#endif
	if ((long)shmget_msg->size < shminfo.shmmin)
			//|| (long)shmget_msg->size > shminfo.shmmax)
			/* There is no need to check the max limit,
			 * the operating system do this for us.
			 */
		return (-EINVAL);
	if (shm_nused >= shminfo.shmmni) /* any shmids left? */
		return (-ENOSPC);

	/* Compute the size of the segment. */
	size = round_page(shmget_msg->size);

	/* Find a free entry in the shmsegs vector. */
	if (shm_last_free < 0) {
		//	shmrealloc();	/* maybe expand the shmsegs[] array */
		for (i = 0; i < shmalloced; i++) {
			if (shmsegs[i].shm_perm.mode & SHMSEG_FREE)
				break;
		}
		if (i == shmalloced) {
			sysvd_print("i == shmalloced\n");
			return (-ENOSPC);
		}
		segnum = i;
	} else  {
		segnum = shm_last_free;
		shm_last_free = -1;
	}
	shmseg = &shmsegs[segnum];
	/*
	 * In case we sleep in malloc(), mark the segment present but deleted
	 * so that noone else tries to create the same key.
	 */
	shmseg->shm_perm.mode = SHMSEG_ALLOCATED | SHMSEG_REMOVED;
	shmseg->shm_perm.key = shmget_msg->key;
	shmseg->shm_perm.seq = (shmseg->shm_perm.seq + 1) & 0x7fff;

	/* Create the file for the shared memory segment. */
	handle = shmseg->shm_internal = malloc(sizeof(struct shm_handle));
	handle->type = shmget_msg->type;
	handle->fd = create_sysv_file(shmget_msg, size, shmseg);
	if (handle->fd == -1) {
		free(handle);
		handle = NULL;
		shmseg->shm_perm.mode = SHMSEG_FREE;
		shm_last_free = segnum;
		errno = -ENFILE;
		return (-1);
	}

	LIST_INIT(&handle->attached_list);

	if (handle->fd < 0) {
		free(shmseg->shm_internal);
		shmseg->shm_internal = NULL;
		shm_last_free = segnum;
		shmseg->shm_perm.mode = SHMSEG_FREE;
		return (-errno);
	}

	/* Get the id. */
	shmid = IXSEQ_TO_IPCID(segnum, shmseg->shm_perm);

	shmseg->shm_perm.cuid = shmseg->shm_perm.uid = cred->cmcred_euid;
	shmseg->shm_perm.cgid = shmseg->shm_perm.gid = cred->cmcred_gid;
	shmseg->shm_perm.mode = (shmseg->shm_perm.mode & SHMSEG_WANTED) |
		(mode & ACCESSPERMS) | SHMSEG_ALLOCATED;

	shmseg->shm_cpid = pid;
	shmseg->shm_lpid = shmseg->shm_nattch = 0;
	shmseg->shm_atime = shmseg->shm_dtime = 0;
	shmseg->shm_ctime = time(NULL);

	shmseg->shm_segsz = shmget_msg->size;
	shm_committed += btoc(size);
	shm_nused++;

	if (shmseg->shm_perm.mode & SHMSEG_WANTED) {
		/*
		 * Somebody else wanted this key while we were asleep.  Wake
		 * them up now.
		 */
		shmseg->shm_perm.mode &= ~SHMSEG_WANTED;
		//TODO multithreading
		//wakeup((caddr_t)shmseg);
	}
	shmseg->shm_perm.mode &= ~SHMSEG_REMOVED;

	if (shmget_msg->type == UNDOGET) {
		/* The file is used by daemon when clients terminates
		 * and sem_undo resources must be cleaned.
		 */
		struct client *cl= _hash_lookup(clientshash, pid);
		cl->undoid = shmid;
	}

	return (shmid);
}

/* Handle a shmget() request. */
int
handle_shmget(pid_t pid, struct shmget_msg *shmget_msg,
		struct cmsgcred *cred ) {
	int segnum, mode, error;
	struct shmid_ds *shmseg;
	struct shm_handle *handle;

	//if (!jail_sysvipc_allowed && td->td_cmsgcred->cr_prison != NULL)
	//	return (ENOSYS);
	mode = shmget_msg->shmflg & ACCESSPERMS;

	sysvd_print("ask for key = %ld\n", shmget_msg->key);
	shmget_msg->key = (shmget_msg->key & 0x3FFF) |
		(shmget_msg->type << 30);
	sysvd_print("ask for key = %ld\n", shmget_msg->key);

	if (shmget_msg->key != IPC_PRIVATE) {
		//again:
		segnum = shm_find_segment_by_key(shmget_msg->key);
		if (segnum >= 0) {
			error = shmget_existing(shmget_msg, mode, segnum, cred);
			//TODO if daemon is multithreading
			//if (error == EAGAIN)
			//	goto again;
			goto done;
		}
		if ((shmget_msg->shmflg & IPC_CREAT) == 0) {
			error = -ENOENT;
			goto done_err;
		}
	}
	error = shmget_allocate_segment(pid, shmget_msg, mode, cred);
	sysvd_print("allocate segment = %d\n", error);
done:
	/*
	 * Install to th client the file corresponding to the
	 * shared memory segment.
	 * client_fd is the file descriptor added in the client
	 * files table.
	 */
	shmseg = shm_find_segment_by_shmid(error);
	if (shmseg == NULL) {
		sysvd_print_err("can not find segment by shmid\n");
		return (-1);
	}

	handle = (struct shm_handle *)shmseg->shm_internal;
	if (install_fd_client(pid, handle->fd) != 0)
		error = errno;
done_err:
	return (error);

}

/* Handle a shmat() request. */
int
handle_shmat(pid_t pid, struct shmat_msg *shmat_msg,
		struct cmsgcred *cred ) {
	int error;
	int fd;
	struct shmid_ds *shmseg;
	struct pid_attached *pidatt;
	struct shm_handle *handle;
	size_t new_size = shmat_msg->size;
	struct client *cl;
	struct id_attached *idatt;

	/*if (!jail_sysvipc_allowed && td->td_cmsgcred->cr_prison != NULL)
	  return (ENOSYS);

again:*/
	shmseg = shm_find_segment_by_shmid(shmat_msg->shmid);
	if (shmseg == NULL) {
		sysvd_print_err("shmat error: segment was not found\n");
		error = EINVAL;
		goto done;
	}
	error = ipcperm(cred, &shmseg->shm_perm, 
			(shmat_msg->shmflg & SHM_RDONLY) ? IPC_R : IPC_R|IPC_W);
	if (error)
		goto done;

	handle = shmseg->shm_internal;

	if (shmat_msg->size > shmseg->shm_segsz) {
		if (handle->type != UNDOGET) {
			error = EINVAL;
			goto done;
		}

		fd = ((struct shm_handle*)shmseg->shm_internal)->fd;
		ftruncate(fd, round_page(new_size));
		shmseg->shm_segsz = new_size;
	}

	shmseg->shm_lpid = pid;
	shmseg->shm_atime = time(NULL);

	if (handle->type != UNDOGET)
		shmseg->shm_nattch++;
	else
		shmseg->shm_nattch = 1; /* Only a process calls shmat and
		only once. If it does it for more than once that is because
		it called exec() and reinitialized the undo segment. */

	/* Insert the pid in the segment list of attaced pids.
	 * The list is checked in handle_shmdt so that only
	 * attached pids can dettached from this segment.
	 */
	sysvd_print("nattch = %d pid = %d\n",
			shmseg->shm_nattch, pid);

	pidatt = malloc(sizeof(*pidatt));
	pidatt->pid = pid;
	LIST_INSERT_HEAD(&handle->attached_list, pidatt, link);

	/* Add the segment at the list of attached segments of the client.
	 * It is used when the process finishes its execution. The daemon
	 * walks through the list to dettach the segments.
	 */
	idatt = malloc(sizeof(*idatt));
	idatt->shmid = shmat_msg->shmid;
	cl = _hash_lookup(clientshash, pid);
	LIST_INSERT_HEAD(&cl->ids_attached, idatt, link);

	return (0);
done:
	return (error);
}

/* Handle a shmdt() request. */
int
handle_shmdt(pid_t pid, int shmid) {
	struct shmid_ds *shmseg;
	int segnum;
	struct shm_handle *handle;
	struct pid_attached *pidatt;
	struct id_attached *idatt;
	struct client *cl;

	sysvd_print("shmdt pid %d shmid %d\n", pid, shmid);
	/*if (!jail_sysvipc_allowed && td->td_cmsgcred->cr_prison != NULL)
	  return (ENOSYS);
	*/

	segnum = IPCID_TO_IX(shmid);
	shmseg = &shmsegs[segnum];
	handle = shmseg->shm_internal;

	/* Check if pid is attached. */
	LIST_FOREACH(pidatt, &handle->attached_list, link)
		if (pidatt->pid == pid)
			break;
	if (!pidatt) {
		sysvd_print_err("process %d is not attached to %d (1)\n",
				pid, shmid);
		return (EINVAL);
	}
	LIST_REMOVE(pidatt, link);

	/* Remove the segment from the list of attached segments of the pid.*/
	cl = _hash_lookup(clientshash, pid);
	LIST_FOREACH(idatt, &cl->ids_attached, link)
		if (idatt->shmid == shmid)
			break;
	if (!idatt) {
		sysvd_print_err("process %d is not attached to %d (2)\n",
				pid, shmid);
		return (EINVAL);
	}
	LIST_REMOVE(idatt, link);

	shmseg->shm_dtime = time(NULL);

	/* If no other process attaced remove the segment. */
	if ((--shmseg->shm_nattch <= 0) &&
			(shmseg->shm_perm.mode & SHMSEG_REMOVED)) {
		shm_deallocate_segment(segnum);
		shm_last_free = segnum;
	}

	return (0);
}

/* Handle a shmctl() request. */
int
handle_shmctl(struct shmctl_msg *shmctl_msg,
		struct cmsgcred *cred ) {
	int error = 0;
	struct shmid_ds *shmseg, *inbuf;

	/*	if (!jail_sysvipc_allowed && td->td_cmsgcred->cr_prison != NULL)
		return (ENOSYS);
		*/
	shmseg = shm_find_segment_by_shmid(shmctl_msg->shmid);

	if (shmseg == NULL) {
		error = EINVAL;
		goto done;
	}

	switch (shmctl_msg->cmd) {
		case IPC_STAT:
			sysvd_print("IPC STAT\n");
			error = ipcperm(cred, &shmseg->shm_perm, IPC_R);
			if (error) {
				sysvd_print("IPC_STAT not allowed\n");
				break;
			}
			shmctl_msg->buf = *shmseg;
			break;
		case IPC_SET:
			sysvd_print("IPC SET\n");
			error = ipcperm(cred, &shmseg->shm_perm, IPC_M);
			if (error) {
				sysvd_print("IPC_SET not allowed\n");
				break;
			}
			inbuf = &shmctl_msg->buf;

			shmseg->shm_perm.uid = inbuf->shm_perm.uid;
			shmseg->shm_perm.gid = inbuf->shm_perm.gid;
			shmseg->shm_perm.mode =
				(shmseg->shm_perm.mode & ~ACCESSPERMS) |
				(inbuf->shm_perm.mode & ACCESSPERMS);
			shmseg->shm_ctime = time(NULL);
			break;
		case IPC_RMID:
			sysvd_print("IPC RMID shmid = %d\n",
					shmctl_msg->shmid);
			error = ipcperm(cred, &shmseg->shm_perm, IPC_M);
			if (error) {
				sysvd_print("IPC_RMID not allowed\n");
				break;
			}
			shmseg->shm_perm.key = IPC_PRIVATE;
			shmseg->shm_perm.mode |= SHMSEG_REMOVED;
			if (shmseg->shm_nattch <= 0) {
				shm_deallocate_segment(IPCID_TO_IX(shmctl_msg->shmid));
				shm_last_free = IPCID_TO_IX(shmctl_msg->shmid);
			}
			else {
				/* In sem and msg cases, other process must be
				 * noticed about the removal. */
				struct shm_handle *internal =
					(struct shm_handle *)shmseg->shm_internal;
				mark_segment_removed(shmctl_msg->shmid,
						internal->type);
			}
			break;
#if 0
		case SHM_LOCK:
		case SHM_UNLOCK:
#endif
		default:
			error = EINVAL;
			break;
	}
done:
	return (error);

}

/* Function used by daemon to map a sysv resource. */
static void *
map_seg(int shmid) {
	struct shmid_ds *shmseg;
	struct shm_handle *internal;

	int fd;
	size_t size;
	void *addr;

	shmseg = shm_find_segment_by_shmid(shmid);
	if (!shmseg) {
		sysvd_print_err("map_seg error:"
				"semid %d not found\n", shmid);
		return (NULL);
	}

	internal = (struct shm_handle *)shmseg->shm_internal;
	if (!internal) {
		sysvd_print_err("map_seg error: internal for"
				"semid %d not found\n", shmid);
		return (NULL);
	}

	fd = internal->fd;

	size = round_page(shmseg->shm_segsz);

	addr = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (!addr) {
		sysvd_print_err("map_seg: error mmap semid = %d\n", shmid);
		return (NULL);
	}

	return (addr);
}

/* Function used by daemon to munmap a sysv resource. */
static int
munmap_seg(int shmid, void *addr) {
	struct shmid_ds *shmseg;
	struct shm_handle *internal;

	size_t size;

	shmseg = shm_find_segment_by_shmid(shmid);
	if (!shmseg) {
		sysvd_print_err("munmap_seg error:"
				"semid %d not found\n", shmid);
		return (-1);
	}

	internal = (struct shm_handle *)shmseg->shm_internal;
	if (!internal) {
		sysvd_print_err("munmap_seg error: internal for"
				"semid %d not found\n", shmid);
		return (-1);
	}

	size = round_page(shmseg->shm_segsz);
	munmap(addr, size);

	return (0);
}

void
shminit(void) {
	int i;

	shmalloced = shminfo.shmmni;
	shmsegs = malloc(shmalloced * sizeof(shmsegs[0]));
	for (i = 0; i < shmalloced; i++) {
		shmsegs[i].shm_perm.mode = SHMSEG_FREE;
		shmsegs[i].shm_perm.seq = 0;
	}
	shm_last_free = 0;
	shm_nused = 0;
	shm_committed = 0;

	/*
	 * msginfo.msgssz should be a power of two for efficiency reasons.
	 * It is also pretty silly if msginfo.msgssz is less than 8
	 * or greater than about 256 so ...
	 */
	i = 8;
	while (i < 1024 && i != msginfo.msgssz)
		i <<= 1;
	if (i != msginfo.msgssz) {
		sysvd_print_err("msginfo.msgssz=%d (0x%x)\n", msginfo.msgssz,
		    msginfo.msgssz);
		sysvd_print_err("msginfo.msgssz not a small power of 2");
		exit(-1);
	}
	msginfo.msgmax = msginfo.msgseg * msginfo.msgssz;
}

/*static void
shmfree(void) {
	free(shmsegs);
}*/

int
semexit(int undoid) {
	struct sem_undo *suptr;
	struct sem *semptr;
	struct shmid_ds *undoseg;

	if (undoid < 0) {
		return (-1);
	}

	undoseg = shm_find_segment_by_shmid(undoid);
	/* The UNDO segment must be mapped by only one segment. */
	if (undoseg->shm_nattch != 1) {
		sysvd_print_err("undo segment mapped by more"
				"than one process\n");
		exit(-1);
	}

	suptr = (struct sem_undo *)map_seg(undoid);
	if (suptr == NULL) {
		sysvd_print_err("no %d undo segment found\n", undoid);
		return (-1);
	}

	/* No locking mechanism is required because only the
	 * client and the daemon can access the UNDO segment.
	 * At this moment the client is disconnected so only
	 * the daemon can modify this segment.
	 */
	while (suptr->un_cnt) {
		struct semid_pool *semaptr;
		int semid;
		int semnum;
		int adjval;
		int ix;

		ix = suptr->un_cnt - 1;
		semid = suptr->un_ent[ix].un_id;
		semnum = suptr->un_ent[ix].un_num;
		adjval = suptr->un_ent[ix].un_adjval;

		semaptr = (struct semid_pool *)map_seg(semid);
		if (!semaptr) {
			return (-1);
		}

		/* Was it removed? */
		if (semaptr->gen == -1 ||
			semaptr->ds.sem_perm.seq != IPCID_TO_SEQ(semid) ||
			(semaptr->ds.sem_perm.mode & SHMSEG_ALLOCATED) == 0) {
			--suptr->un_cnt;
			sysvd_print_err("semexit - semid not allocated\n");
			continue;
		}
		if (semnum >= semaptr->ds.sem_nsems) {
			--suptr->un_cnt;
			sysvd_print_err("semexit - semnum out of range\n");
			continue;
		}

#ifdef SYSV_RWLOCK
#ifdef SYSV_SEMS
		sysv_rwlock_rdlock(&semaptr->rwlock);
#else
		sysv_rwlock_wrlock(&semaptr->rwlock);
#endif //SYSV_SEMS
#else
		sysv_mutex_lock(&semaptr->mutex);
		/* Nobody can remove the semaphore beteen the check and the
		 * lock acquisition because it must first send a IPC_RMID
		 * to me and I will process that after finishing this function.
		 */
#endif //SYSV_RWLOCK
		semptr = &semaptr->ds.sem_base[semnum];
#ifdef SYSV_SEMS
		sysv_mutex_lock(&semptr->sem_mutex);
#endif
		if (ix == suptr->un_cnt - 1 &&
		    semid == suptr->un_ent[ix].un_id &&
		    semnum == suptr->un_ent[ix].un_num &&
		    adjval == suptr->un_ent[ix].un_adjval) {
			--suptr->un_cnt;

			if (adjval < 0) {
				if (semptr->semval < -adjval)
					semptr->semval = 0;
				else
					semptr->semval += adjval;
			} else {
				semptr->semval += adjval;
			}
			/* TODO multithreaded daemon:
			 * Check again if the semaphore was removed and do
			 * not wake anyone if it was.*/
			umtx_wakeup((int *)&semptr->semval, 0);
		}
#ifdef SYSV_SEMS
		sysv_mutex_unlock(&semptr->sem_mutex);
#endif

#ifdef SYSV_RWLOCK
		sysv_rwlock_unlock(&semaptr->rwlock);
#else
		sysv_mutex_unlock(&semaptr->mutex);
#endif
		munmap_seg(semid, semaptr);
	}

	munmap_seg(undoid, suptr);
	return (0);
}

void
shmexit(struct client *cl) {
	struct id_attached *idatt;

	while (!LIST_EMPTY(&cl->ids_attached)) {
		idatt = LIST_FIRST(&cl->ids_attached);
		handle_shmdt(cl->pid, idatt->shmid);
	}
}
