/* $FreeBSD: src/sys/kern/sysv_msg.c,v 1.23.2.5 2002/12/31 08:54:53 maxim Exp $ */

/*
 * Implementation of SVID messages
 *
 * Author:  Daniel Boulet
 *
 * Copyright 1993 Daniel Boulet and RTMX Inc.
 * Copyright (c) 2013 Larisa Grigore <larisagrigore@gmail.com>
 *
 * This system call was implemented by Daniel Boulet under contract from RTMX.
 *
 * Redistribution and use in source forms, with and without modification,
 * are permitted provided that this entire comment appears intact.
 *
 * Redistribution in binary form may occur without any restrictions.
 * Obviously, it would be nice if you gave credit where credit is due
 * but requiring it would be too onerous.
 *
 * This software is provided ``AS IS'' without any warranties of any kind.
 */

#include "namespace.h"
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <err.h>
#include <pthread.h>
#include <string.h>
#include <stdarg.h>
#include <sys/param.h>
#include <sys/queue.h>
#include <sys/mman.h>
#include "un-namespace.h"

#include "sysvipc_lock.h"
#include "sysvipc_ipc.h"
#include "sysvipc_hash.h"
#include "sysvipc_msg.h"
#include "sysvipc_shm.h"

#define SYSV_MUTEX_LOCK(x)		if (__isthreaded) _pthread_mutex_lock(x)
#define SYSV_MUTEX_UNLOCK(x)	if (__isthreaded) _pthread_mutex_unlock(x)
#define SYSV_MUTEX_DESTROY(x)	if (__isthreaded) _pthread_mutex_destroy(x)

extern struct hashtable *shmaddrs;
extern struct hashtable *shmres;
extern pthread_mutex_t lock_resources;

struct msginfo msginfo = {
                MSGMAX,         /* max chars in a message */
                MSGMNI,         /* # of message queue identifiers */
                MSGMNB,         /* max chars in a queue */
                MSGTQL,         /* max messages in system */
                MSGSSZ,         /* size of a message segment (must be small power of 2 greater than 4) */
                MSGSEG          /* number of message segments */
};

static int
put_shmdata(int id) {
	struct shm_data *data;
	int ret = -1;

	SYSV_MUTEX_LOCK(&lock_resources);
	data = _hash_lookup(shmres, id);
	if (!data) {
		sysv_print_err("something wrong put_shmdata\n");
		goto done; /* It should not reach here. */
	}

	data->used--;
	if (data->used == 0 && data->removed) {
		sysv_print("really remove the sem\n");
		SYSV_MUTEX_UNLOCK(&lock_resources);
		/* OBS: Even if the shmctl fails (the thread doesn't
		 * have IPC_M permissions), all structures associated
		 * with it will be removed in the current process.*/
		shmdt(data->internal);
		if (data->removed == SEG_ALREADY_REMOVED)
			return 1; /* The queue was removed
			by another process so there is nothing else
			we must do. */
		/* Else inform the daemon that the segment is removed. */
		return (sysvipc_shmctl(id, IPC_RMID, NULL));
	}

	ret = 0;
done:
	SYSV_MUTEX_UNLOCK(&lock_resources);
	return (ret);
}

static struct msqid_pool*
get_msqpptr(int msqid, int to_remove, int shm_access) {
	struct msqid_pool *msqpptr;

	struct shm_data *shmdata =
		get_shmdata(msqid, to_remove, shm_access);
	if (!shmdata) {
		/* Error is set in get_shmdata. */
		return NULL;
	}

	msqpptr = (struct msqid_pool *)shmdata->internal;
	if (!msqpptr) {
		put_shmdata(msqid);
		errno = EINVAL;
		return NULL;
	}

	return msqpptr;
}

static int
msqp_exist(int msqid, struct msqid_pool *msqpptr) {
	/* Was it removed? */
	if (msqpptr->gen == -1 ||
			msqpptr->ds.msg_perm.seq != IPCID_TO_SEQ(msqid))
		return 0;

	return 1;
}

static int
try_rwlock_rdlock(int msqid, struct msqid_pool *msqpptr) {
	sysv_print("try get rd lock\n");
#ifdef SYSV_RWLOCK
	sysv_rwlock_rdlock(&msqpptr->rwlock);
#else
	sysv_mutex_lock(&msqpptr->mutex);
#endif
	sysv_print("get rd lock\n");
	if (!msqp_exist(msqid, msqpptr)) {
		errno = EINVAL;
		sysv_print("error rd lock\n");
#ifdef SYSV_RWLOCK
		sysv_rwlock_unlock(&msqpptr->rwlock);
#else
		sysv_mutex_unlock(&msqpptr->mutex);
#endif
		return -1;
	}
	sysv_print("end rd lock\n");
	return 0;
}

static int
try_rwlock_wrlock(int msqid, struct msqid_pool *msqpptr) {
	sysv_print("try get wr lock\n");
#ifdef SYSV_RWLOCK
	sysv_rwlock_wrlock(&msqpptr->rwlock);
#else
	sysv_mutex_lock(&msqpptr->mutex);
#endif
	sysv_print("get wr lock\n");
	if (!msqp_exist(msqid, msqpptr)) {
		sysv_print("error rw lock\n");
		errno = EINVAL;
#ifdef SYSV_RWLOCK
		sysv_rwlock_unlock(&msqpptr->rwlock);
#else
		sysv_mutex_unlock(&msqpptr->mutex);
#endif
		return -1;
	}
	sysv_print("end rw lock\n");
	return 0;
}

static int
rwlock_unlock(int msqid, struct msqid_pool *msqpptr) {
	if (!msqp_exist(msqid, msqpptr)) {
		errno = EINVAL;
		return -1;
	}
#ifdef SYSV_RWLOCK
	sysv_rwlock_unlock(&msqpptr->rwlock);
#else
	sysv_mutex_unlock(&msqpptr->mutex);
#endif
	sysv_print("unlock rw lock\n");
	return 0;
}

static void
msg_freehdr(struct msqid_pool *msqpptr, struct msg *msghdr)
{
	while (msghdr->msg_ts > 0) {
		short next;
		if (msghdr->msg_spot < 0 || msghdr->msg_spot >= msginfo.msgseg) {
			sysv_print_err("msghdr->msg_spot out of range");
			exit(-1);
		}
		next = msqpptr->msgmaps[msghdr->msg_spot].next;
		msqpptr->msgmaps[msghdr->msg_spot].next =
			msqpptr->free_msgmaps;
		msqpptr->free_msgmaps = msghdr->msg_spot;
		msqpptr->nfree_msgmaps++;
		msghdr->msg_spot = next;
		if (msghdr->msg_ts >= msginfo.msgssz)
			msghdr->msg_ts -= msginfo.msgssz;
		else
			msghdr->msg_ts = 0;
	}
	if (msghdr->msg_spot != -1) {
		sysv_print_err("msghdr->msg_spot != -1");
		exit(-1);
	}
	msghdr->msg_next = msqpptr->free_msghdrs;
	msqpptr->free_msghdrs = (msghdr - &msqpptr->msghdrs[0]) /
		sizeof(struct msg);
}

int
sysvipc_msgget(key_t key, int msgflg) {
	int msqid;
	void *shmaddr;
	size_t size = sizeof(struct msqid_pool);

	msqid = _shmget(key, size, msgflg, MSGGET);
	if (msqid == -1)
		goto done;

	/* If the msg is in process of being removed there are two cases:
	 * - the daemon knows that and it will handle this situation.
	 * - one of the threads from this address space remove it and the daemon
	 *   wasn't announced yet; in this scenario, the msg is marked
	 *   using "removed" field of shm_data and future calls will return
	 *   EIDRM error.
	 */

#if 0
	/* Set access type. */
	shm_access = semflg & (IPC_W | IPC_R);
	if(set_shmdata_access(semid, shm_access) != 0) {
		/* errno already set. */
		goto done;
	}
#endif

	shmaddr = sysvipc_shmat(msqid, NULL, 0);
	if (!shmaddr) {
		msqid = -1;
		sysvipc_shmctl(msqid, IPC_RMID, NULL);
		goto done;
	}
	sysv_print("shmaddr = %lx\n", (unsigned long)shmaddr);

done:
	return msqid;
}

int
sysvipc_msgctl(int msqid, int cmd, struct msqid_ds *buf) {
	int error;
	struct msqid_pool *msqpptr = NULL;
	struct shmid_ds shmds;
	int shm_access = 0;

	error = 0;

	switch (cmd) {
		case IPC_SET: /* Originally was IPC_M but this is checked
				 by daemon. */
			shm_access = IPC_W;
			break;
		case IPC_STAT:
			shm_access = IPC_R;
			break;
		default:
			break;
	}

	msqpptr = get_msqpptr(msqid, cmd==IPC_RMID, shm_access);
	if (!msqpptr) {
		errno = EINVAL;
		return -1;
	}

	switch (cmd) {
	case IPC_RMID:
		/* Mark that the segment is removed. This is done in
		 * get_msqpptr call in order to announce other processes.
		 * It will be actually removed after put_shmdata call and
		 * not other thread from this address space use shm_data
		 * structure.
		 */
		break;
	case IPC_SET:
		error = try_rwlock_rdlock(msqid, msqpptr);
		if (error)
			break;
		if (buf->msg_qbytes == 0) {
			sysv_print_err("can't reduce msg_qbytes to 0\n");
			errno = EINVAL;		/* non-standard errno! */
			rwlock_unlock(msqid, msqpptr);
			break;
		}
		rwlock_unlock(msqid, msqpptr);

		memset(&shmds, 0, sizeof(shmds)/sizeof(unsigned char));
		memcpy(&shmds.shm_perm, &buf->msg_perm,
				sizeof(struct ipc_perm));
		error = sysvipc_shmctl(msqid, cmd, &shmds);
		if (error)
			break;

		/* There is no need to check if we have right to modify the
		 * size because we have right to change other fileds. */
			
		if (round_page(buf->msg_qbytes) !=
				round_page(msqpptr->ds.msg_qbytes)) {
				sysv_print("change msg size\n");
				/* TODO same as in semundo_adjust only
				 * that there is no way to inform other
				 * processes about the change. */
		}

		error = try_rwlock_wrlock(msqid, msqpptr);
		if (error)
			break;
		msqpptr->ds.msg_qbytes = buf->msg_qbytes;
		rwlock_unlock(msqid, msqpptr);
		/* OBS: didn't update ctime and mode as in kernel implementation
		 * it is done. Those fields are already updated for shmid_ds
		 * struct when calling shmctl
		 */
		break;

	case IPC_STAT:
		error = sysvipc_shmctl(msqid, cmd, &shmds);
		if (error)
			break;

		memcpy(&buf->msg_perm, &shmds.shm_perm,
				sizeof(struct ipc_perm));
		buf->msg_ctime = shmds.shm_ctime;

		/* Read fields that are not kept in shmds. */
		error = try_rwlock_rdlock(msqid, msqpptr);
		if (error)
			break;
		buf->msg_first = (struct msg *)(u_long)
			msqpptr->ds.first.msg_first_index;
		buf->msg_last = (struct msg *)(u_long)
			msqpptr->ds.last.msg_last_index;
		buf->msg_cbytes = msqpptr->ds.msg_cbytes;
		buf->msg_qnum = msqpptr->ds.msg_qnum;
		buf->msg_qbytes = msqpptr->ds.msg_qbytes;
		buf->msg_lspid = msqpptr->ds.msg_lspid;
		buf->msg_lrpid = msqpptr->ds.msg_lrpid;
		buf->msg_stime = msqpptr->ds.msg_stime;
		buf->msg_rtime = msqpptr->ds.msg_rtime;
		rwlock_unlock(msqid, msqpptr);
		break;
	default:
		sysv_print_err("invalid command %d\n", cmd);
		errno = EINVAL;
		break;
	}

	put_shmdata(msqid);

	return(error);
}

int
sysvipc_msgsnd(int msqid, const void *msgp, size_t msgsz, int msgflg)
{
	int segs_needed, error;
	struct msg *msghdr;
	struct msqid_pool *msqpptr, *auxmsqpptr;
	struct msqid_ds_internal *msqptr;
	short next;
	int val_to_sleep;
	char *auxmsgp = (char *)msgp;
	int _index;

	sysv_print("call to msgsnd(%d, %ld, %d)\n", msqid, msgsz, msgflg);

	/*if (!jail_sysvipc_allowed && td->td_ucred->cr_prison != NULL)
		return (ENOSYS);
*/
	if (!msgp) {
		errno = EINVAL;
		return -1;
	}

	msqpptr = get_msqpptr(msqid, 0, IPC_W);
	if (!msqpptr) {
		errno = EINVAL;
		return -1;
	}
	error = -1;

	if (try_rwlock_wrlock(msqid, msqpptr) == -1) {
		errno = EIDRM;
		goto done;
	}

	msqptr = &msqpptr->ds;

	segs_needed = (msgsz + msginfo.msgssz - 1) / msginfo.msgssz;
	sysv_print("msgsz=%ld, msgssz=%d, segs_needed=%d\n", msgsz, msginfo.msgssz,
	    segs_needed);
	for (;;) {
		int need_more_resources = 0;

		if (msgsz + msqptr->msg_cbytes > msqptr->msg_qbytes) {
			sysv_print("msgsz + msg_cbytes > msg_qbytes\n");
			need_more_resources = 1;
		}

		if (segs_needed > msqpptr->nfree_msgmaps) {
			sysv_print("segs_needed > nfree_msgmaps (= %d)\n",
					msqpptr->nfree_msgmaps);
			need_more_resources = 1;
		}

		if (msqpptr->free_msghdrs == -1) {
			sysv_print("no more msghdrs\n");
			need_more_resources = 1;
		}

		if (need_more_resources) {
			if ((msgflg & IPC_NOWAIT) != 0) {
				sysv_print_err("need more resources but caller doesn't want to wait\n");
				errno = EAGAIN;
				goto done;
			}

			sysv_print("goodnight\n");
			val_to_sleep = msqpptr->gen;
			rwlock_unlock(msqid, msqpptr);
			put_shmdata(msqid);

			if (umtx_sleep((int *)&msqpptr->gen, val_to_sleep, SYSV_TIMEOUT) != 0) {
				sysv_print_err("msgsnd:  interrupted system call\n");
				errno = EINTR;
				goto done;
			}

			/* Check if another thread didn't remove the msg queue. */
			auxmsqpptr = get_msqpptr(msqid, 0, IPC_W);
			if (!auxmsqpptr) {
				errno = EIDRM;
				return -1;
			}

			if (auxmsqpptr != msqpptr) {
				errno = EIDRM;
				goto done;
			}

			/* Check if another process didn't remove the queue. */
			if (try_rwlock_wrlock(msqid, msqpptr) == -1) {
				errno = EIDRM;
				goto done;
			}

			if (msqptr != &msqpptr->ds) {
				sysv_print("msqptr != &msqpptr->ds");
				exit(-1);
			}

		} else {
			sysv_print("got all the resources that we need\n");
			break;
		}
	}

	/*
	 * We have the resources that we need.
	 * Make sure!
	 */
#if 0
	if (segs_needed > nfree_msgmaps) {
		sysv_print_err("segs_needed > nfree_msgmaps");
		exit(-1);
	}
#endif
	if (msgsz + msqptr->msg_cbytes > msqptr->msg_qbytes) {
		sysv_print_err("msgsz + msg_cbytes > msg_qbytes");
		exit(-1);
	}

	/*
	 * Allocate a message header
	 */
	msghdr = &msqpptr->msghdrs[msqpptr->free_msghdrs];
	msqpptr->free_msghdrs = msghdr->msg_next;
	msghdr->msg_spot = -1;
	msghdr->msg_ts = msgsz;

	/*
	 * Allocate space for the message
	 */
	while (segs_needed > 0) {
		next = msqpptr->free_msgmaps;
		if (next < 0 || next > msginfo.msgseg) {
			sysv_print_err("out of range free_msgmaps %d #1\n", next);
			exit(-1);
		}

		msqpptr->free_msgmaps = msqpptr->msgmaps[next].next;
		msqpptr->nfree_msgmaps--;
		msqpptr->msgmaps[next].next = msghdr->msg_spot;
		msghdr->msg_spot = next;
		segs_needed--;
	}

	/*
	 * Copy in the message type
	 */
	memcpy(&msghdr->msg_type, auxmsgp, sizeof(msghdr->msg_type));
	auxmsgp = (char *)auxmsgp + sizeof(msghdr->msg_type);

	/*
	 * Validate the message type
	 */
	sysv_print("msg_type = %ld\n", msghdr->msg_type);

	if (msghdr->msg_type < 1) {
		msg_freehdr(msqpptr, msghdr);
		umtx_wakeup((int *)&msqpptr->gen, 0);
		sysv_print_err("mtype (%ld) < 1\n", msghdr->msg_type);
		errno = EINVAL;
		goto done;
	}

	/*
	 * Copy in the message body
	 */
	next = msghdr->msg_spot;
	while (msgsz > 0) {
		size_t tlen;
		if (msgsz > (size_t)msginfo.msgssz)
			tlen = msginfo.msgssz;
		else
			tlen = msgsz;
		if (next < 0 || next > msginfo.msgseg) {
			sysv_print_err("out of range free_msgmaps %d #2\n", next);
			exit(-1);
		}

		memcpy(&msqpptr->msgpool[next * msginfo.msgssz], auxmsgp, tlen);
		msgsz -= tlen;
		auxmsgp = (char *)auxmsgp + tlen;
		next = msqpptr->msgmaps[next].next;
	}

	/*
	 * Put the message into the queue
	 */
	_index = (msghdr - &msqpptr->msghdrs[0]) /
		sizeof(struct msg);
	sysv_print("index_msghdr = %d\n", _index);
	if (msqptr->first.msg_first_index == -1) {
		msqptr->first.msg_first_index = _index;
		msqptr->last.msg_last_index = _index;
	} else {
		msqpptr->msghdrs[msqptr->last.msg_last_index].msg_next = _index;
		msqptr->last.msg_last_index = _index;
	}
	msqpptr->msghdrs[msqptr->last.msg_last_index].msg_next = -1;

	msqptr->msg_cbytes += msghdr->msg_ts;
	msqptr->msg_qnum++;
	msqptr->msg_lspid = getpid();
	msqptr->msg_stime = time(NULL);

	umtx_wakeup((int *)&msqpptr->gen, 0);
	error = 0;

done:
	rwlock_unlock(msqid, msqpptr);
	put_shmdata(msqid);
	return(error);
}

int
sysvipc_msgrcv(int msqid, void *msgp, size_t msgsz, long mtype, int msgflg)
{
	size_t len;
	struct msqid_pool *msqpptr, *auxmsqpptr;
	struct msqid_ds_internal *msqptr;
	struct msg *msghdr;
	short msghdr_index;
	int error;
	short next;
	int val_to_sleep;
	char *auxmsgp = (char *)msgp;

	sysv_print("call to msgrcv(%d, %ld, %ld, %d)\n", msqid, msgsz, mtype, msgflg);
/*
	if (!jail_sysvipc_allowed && td->td_ucred->cr_prison != NULL)
		return (ENOSYS);
*/

	if (!msgp) {
		errno = EINVAL;
		return -1;
	}

	msqpptr = get_msqpptr(msqid, 0, IPC_R);
	if (!msqpptr) {
		errno = EINVAL;
		return -1;
	}

	error = -1;

	if (try_rwlock_wrlock(msqid, msqpptr) == -1) {
		errno = EIDRM;
		goto done;
	}

	msqptr = &msqpptr->ds;

	msghdr_index = -1;
	while (msghdr_index == -1) {
		if (mtype == 0) {
			msghdr_index = msqptr->first.msg_first_index;
			msghdr = &msqpptr->msghdrs[msghdr_index];
			if (msghdr_index != -1) {
				if (msgsz < msghdr->msg_ts &&
				    (msgflg & MSG_NOERROR) == 0) {
						sysv_print_err("first message on the queue is too big"
							"(want %d, got %d)\n",
							msgsz, msghdr->msg_ts);
					errno = E2BIG;
					goto done;
				}
				if (msqptr->first.msg_first_index == msqptr->last.msg_last_index) {
					msqptr->first.msg_first_index = -1;
					msqptr->last.msg_last_index = -1;
				} else {
					msqptr->first.msg_first_index = msghdr->msg_next;
					if (msqptr->first.msg_first_index == -1) {
						sysv_print_err("first.msg_first_index/last screwed up #1");
						exit(-1);
					}
				}
			}
		} else {
			short previous;
			short prev;
			previous = -1;
			prev = msqptr->first.msg_first_index;
			while ((msghdr_index = prev) != -1) {
				msghdr = &msqpptr->msghdrs[msghdr_index];
				/*
				 * Is this message's type an exact match or is
				 * this message's type less than or equal to
				 * the absolute value of a negative mtype?
				 * Note that the second half of this test can
				 * NEVER be true if mtype is positive since
				 * msg_type is always positive!
				 */
				if (mtype == msghdr->msg_type ||
				    msghdr->msg_type <= -mtype) {
					sysv_print("found message type %d, requested %d\n",
					    msghdr->msg_type, mtype);
					if (msgsz < msghdr->msg_ts &&
					    (msgflg & MSG_NOERROR) == 0) {
						sysv_print_err("requested message on the queue"
							" is too big (want %d, got %d)\n",
						    msgsz, msghdr->msg_ts);
						errno = E2BIG;
						goto done;
					}
					prev = msghdr->msg_next;
					if (msghdr_index == msqptr->last.msg_last_index) {
						if (previous == -1) {
							msqptr->first.msg_first_index = -1;
							msqptr->last.msg_last_index = -1;
						} else {
							msqptr->last.msg_last_index = previous;
						}
					}
					break;
				}
				previous = msghdr_index;
				prev = msghdr->msg_next;
			}
		}

		/*
		 * We've either extracted the msghdr for the appropriate
		 * message or there isn't one.
		 * If there is one then bail out of this loop.
		 */
		if (msghdr_index != -1)
			break;

		/*
		 * No message found.  Does the user want to wait?
		 */
		if ((msgflg & IPC_NOWAIT) != 0) {
			sysv_print_err("no appropriate message found (mtype=%d)\n",
			    mtype);
			errno = ENOMSG;
			goto done;
		}

		/*
		 * Wait for something to happen
		 */
		sysv_print("goodnight\n");
		val_to_sleep = msqpptr->gen;
		rwlock_unlock(msqid, msqpptr);
		put_shmdata(msqid);

		/* We don't sleep more than SYSV_TIMEOUT because we could
		 * go to sleep after another process calls wakeup and remain
		 * blocked.
		 */
		if (umtx_sleep((int *)&msqpptr->gen, val_to_sleep, SYSV_TIMEOUT) != 0) {
			sysv_print_err("msgrcv:  interrupted system call\n");
			errno = EINTR;
			goto done;
		}
		sysv_print("msgrcv:  good morning (error=%d)\n", errno);

		/* Check if another thread didn't remove the msg queue. */
		auxmsqpptr = get_msqpptr(msqid, 0, IPC_R);
		if (!auxmsqpptr) {
			errno = EIDRM;
			return -1;
		}

		if (auxmsqpptr != msqpptr) {
			errno = EIDRM;
			goto done;
		}

		/* Check if another process didn't remove the msg queue. */
		if (try_rwlock_wrlock(msqid, msqpptr) == -1) {
			errno = EIDRM;
			goto done;
		}

		if (msqptr != &msqpptr->ds) {
			sysv_print_err("msqptr != &msqpptr->ds");
			exit(-1);
		}
	}

	/*
	 * Return the message to the user.
	 */
	msqptr->msg_cbytes -= msghdr->msg_ts;
	msqptr->msg_qnum--;
	msqptr->msg_lrpid = getpid();
	msqptr->msg_rtime = time(NULL);

	/*
	 * Make msgsz the actual amount that we'll be returning.
	 * Note that this effectively truncates the message if it is too long
	 * (since msgsz is never increased).
	 */
	sysv_print("found a message, msgsz=%d, msg_ts=%d\n", msgsz,
	    msghdr->msg_ts);
	if (msgsz > msghdr->msg_ts)
		msgsz = msghdr->msg_ts;

	/*
	 * Return the type to the user.
	 */
	memcpy(auxmsgp, (caddr_t)&(msghdr->msg_type), sizeof(msghdr->msg_type));
	auxmsgp = (char *)auxmsgp + sizeof(msghdr->msg_type);

	/*
	 * Return the segments to the user
	 */
	next = msghdr->msg_spot;
	for (len = 0; len < msgsz; len += msginfo.msgssz) {
		size_t tlen;

		if (msgsz - len > (size_t)msginfo.msgssz)
			tlen = msginfo.msgssz;
		else
			tlen = msgsz - len;
		if (next < 0 || next > msginfo.msgseg) {
			sysv_print_err("out of range free_msgmaps %d #3\n", next);
			exit(-1);
		}

		memcpy(auxmsgp, (caddr_t)&msqpptr->msgpool[next * msginfo.msgssz], tlen);
		auxmsgp = (char *)auxmsgp + tlen;
		next = msqpptr->msgmaps[next].next;
	}

	/*
	 * Done, return the actual number of bytes copied out.
	 */
	msg_freehdr(msqpptr, msghdr);
	umtx_wakeup((int *)&msqpptr->gen, 0);
	error = msgsz;
done:
	rwlock_unlock(msqid, msqpptr);
	put_shmdata(msqid);
	return(error);
}
