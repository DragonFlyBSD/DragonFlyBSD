/* $FreeBSD: src/sys/kern/sysv_sem.c,v 1.69 2004/03/17 09:37:13 cperciva Exp $ */

/*
 * Implementation of SVID semaphores
 *
 * Author:  Daniel Boulet
 * Copyright (c) 2013 Larisa Grigore <larisagrigore@gmail.com>
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
#include <sys/sem.h>
#include "un-namespace.h"

#include "sysvipc_lock.h"
#include "sysvipc_ipc.h"
#include "sysvipc_shm.h"
#include "sysvipc_sem.h"
#include "sysvipc_hash.h"


#define SYSV_MUTEX_LOCK(x)		if (__isthreaded) _pthread_mutex_lock(x)
#define SYSV_MUTEX_UNLOCK(x)	if (__isthreaded) _pthread_mutex_unlock(x)
#define SYSV_MUTEX_DESTROY(x)	if (__isthreaded) _pthread_mutex_destroy(x)

extern struct hashtable *shmaddrs;
extern struct hashtable *shmres;
extern pthread_mutex_t lock_resources;

struct sem_undo *undos = NULL;
pthread_mutex_t lock_undo = PTHREAD_MUTEX_INITIALIZER;

static int semundo_clear(int, int);

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
		sysvipc_shmdt(data->internal);
		semundo_clear(id, -1);
		if (data->removed == SEG_ALREADY_REMOVED)
			return 1; /* The semaphore was removed
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

static struct semid_pool*
get_semaptr(int semid, int to_remove, int shm_access) {
	struct semid_pool *semaptr;

	struct shm_data *shmdata = get_shmdata(semid, to_remove, shm_access);
	if (!shmdata) {
		/* Error is set in get_shmdata. */
		return (NULL);
	}

	semaptr = (struct semid_pool *)shmdata->internal;
	if (!semaptr) {
		put_shmdata(semid);
		errno = EINVAL;
		return (NULL);
	}

	return (semaptr);
}

static int
sema_exist(int semid, struct semid_pool *semaptr) {
	/* Was it removed? */
	if (semaptr->gen == -1 ||
			semaptr->ds.sem_perm.seq != IPCID_TO_SEQ(semid))
		return (0);

	return (1);
}

/* This is the function called when a the semaphore
 * is descovered as removed. It marks the process
 * internal data and munmap the */
static void
mark_for_removal(int shmid) {
	sysv_print("Mark that the segment was removed\n");
	get_shmdata(shmid, SEG_ALREADY_REMOVED, 0);
	 /* Setting SEG_ALREADY_REMOVED parameter, when put_shmdata
	  * is called, the internal resources will be freed.
	  */
	/* Decrement the "usage" field. */
	put_shmdata(shmid);
}

static int
try_rwlock_rdlock(int semid, struct semid_pool *semaptr) {
	sysv_print(" before rd lock id = %d %x\n", semid, semaptr);
#ifdef SYSV_RWLOCK
	sysv_rwlock_rdlock(&semaptr->rwlock);
	sysv_print("rd lock id = %d\n", semid);
#else
	sysv_mutex_lock(&semaptr->mutex);
	sysv_print("lock id = %d\n", semid);
#endif
	if (!sema_exist(semid, semaptr)) {
		errno = EINVAL;
		sysv_print("error sema %d doesn't exist\n", semid);
#ifdef SYSV_RWLOCK
		sysv_rwlock_unlock(&semaptr->rwlock);
#else
		sysv_mutex_unlock(&semaptr->mutex);
#endif
		/* Internal resources must be freed. */
		mark_for_removal(semid);
		return (-1);
	}
	return (0);
}

static int
try_rwlock_wrlock(int semid, struct semid_pool *semaptr) {
#ifdef SYSV_RWLOCK
	sysv_print("before wrlock id = %d %x\n", semid, semaptr);
	sysv_rwlock_wrlock(&semaptr->rwlock);
#else
	sysv_print("before lock id = %d %x\n", semid, semaptr);
	sysv_mutex_lock(&semaptr->mutex);
#endif
	sysv_print("lock id = %d\n", semid);
	if (!sema_exist(semid, semaptr)) {
		errno = EINVAL;
		sysv_print("error sema %d doesn't exist\n", semid);
#ifdef SYSV_RWLOCK
		sysv_rwlock_unlock(&semaptr->rwlock);
#else
		sysv_mutex_unlock(&semaptr->mutex);
#endif
		/* Internal resources must be freed. */
		mark_for_removal(semid);
		return (-1);
	}
	return (0);
}

static int
rwlock_unlock(int semid, struct semid_pool *semaptr) {
	sysv_print("unlock id = %d %x\n", semid, semaptr);
	if (!sema_exist(semid, semaptr)) {
		/* Internal resources must be freed. */
		mark_for_removal(semid);
		errno = EINVAL;
		return (-1);
	}
#ifdef SYSV_RWLOCK
	sysv_rwlock_unlock(&semaptr->rwlock);
#else
	sysv_mutex_unlock(&semaptr->mutex);
#endif
	return (0);
}

int
sysvipc_semget(key_t key, int nsems, int semflg) {
	int semid;
	void *shmaddr;
	//int shm_access;
	int size = sizeof(struct semid_pool) + nsems * sizeof(struct sem);

	//TODO resources limits
	sysv_print("handle semget\n");

	semid = _shmget(key, size, semflg, SEMGET);
	if (semid == -1) {
		/* errno already set. */
		goto done;
	}

	/* If the semaphore is in process of being removed there are two cases:
	 * - the daemon knows that and it will handle this situation.
	 * - one of the threads from this address space remove it and the daemon
	 *   wasn't announced yet; in this scenario, the semaphore is marked
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
	shmaddr = sysvipc_shmat(semid, NULL, 0);
	if (!shmaddr) {
		semid = -1;
		sysvipc_shmctl(semid, IPC_RMID, NULL);
		goto done;
	}

	//TODO more semaphores in a single file

done:
	sysv_print("end handle semget %d\n", semid);
	return (semid);
}

static int
semundo_clear(int semid, int semnum)
{
	struct undo *sunptr;
	int i;

	sysv_print("semundo clear\n");

	SYSV_MUTEX_LOCK(&lock_undo);
	if (!undos)
		goto done;

	sunptr = &undos->un_ent[0];
	i = 0;

	while (i < undos->un_cnt) {
		if (sunptr->un_id == semid) {
			if (semnum == -1 || sunptr->un_num == semnum) {
				undos->un_cnt--;
				if (i < undos->un_cnt) {
					undos->un_ent[i] =
					  undos->un_ent[undos->un_cnt];
					continue;
				}
			}
			if (semnum != -1)
				break;
		}
		++i;
		++sunptr;
	}

	//TODO Shrink memory if case; not sure if necessary
done:
	SYSV_MUTEX_UNLOCK(&lock_undo);
	sysv_print("end semundo clear\n");
	return (0);
}

int
sysvipc___semctl(int semid, int semnum , int cmd, union semun *arg)
{
	int i, error;
	struct semid_pool *semaptr = NULL;
	struct sem *semptr = NULL;
	struct shmid_ds shmds;
	int shm_access = 0;

	/*if (!jail_sysvipc_allowed && cred->cr_prison != NULL)
		return (ENOSYS);
*/

	sysv_print("semctl cmd = %d\n", cmd);

	error = 0;

	switch (cmd) {
		case IPC_SET: /* Originally was IPC_M but this is checked
				 by daemon. */
		case SETVAL:
		case SETALL:
			shm_access = IPC_W;
			break;
		case IPC_STAT:
		case GETNCNT:
		case GETPID:
		case GETVAL:
		case GETALL:
		case GETZCNT:
			shm_access = IPC_R;
			break;
		default:
			break;
	}

	semaptr = get_semaptr(semid, cmd==IPC_RMID, shm_access);
	if (!semaptr) {
		/* errno already set. */
		return (-1);
	}

	switch (cmd) {
	case IPC_RMID:
		/* Mark that the segment is removed. This is done in
		 * get_semaptr call in order to announce other processes.
		 * It will be actually removed after put_shmdata call and
		 * not other thread from this address space use shm_data
		 * structure.
		 */
		break;

	case IPC_SET:
		if (!arg->buf) {
			error = EFAULT;
			break;
		}

		memset(&shmds, 0, sizeof(shmds)/sizeof(unsigned char));
		memcpy(&shmds.shm_perm, &arg->buf->sem_perm,
				sizeof(struct ipc_perm));
		error = sysvipc_shmctl(semid, cmd, &shmds);
		/* OBS: didn't update ctime and mode as in kernel implementation
		 * it is done. Those fields are already updated for shmid_ds
		 * struct when calling shmctl
		 */
		break;

	case IPC_STAT:
		if (!arg->buf) {
			error = EFAULT;
			break;
		}

		error = sysvipc_shmctl(semid, cmd, &shmds);
		if (error)
			break;

		memcpy(&arg->buf->sem_perm, &shmds.shm_perm,
				sizeof(struct ipc_perm));
		arg->buf->sem_nsems = (shmds.shm_segsz - sizeof(struct semid_pool)) /
			sizeof(struct sem);
		arg->buf->sem_ctime = shmds.shm_ctime;

		/* otime is semaphore specific so read it from
		 * semaptr
		 */
		error = try_rwlock_rdlock(semid, semaptr);
		if (error)
			break;
		arg->buf->sem_otime = semaptr->ds.sem_otime;
		rwlock_unlock(semid, semaptr);
		break;

	case GETNCNT:
		if (semnum < 0 || semnum >= semaptr->ds.sem_nsems) {
			errno = EINVAL;
			break;
		}

		error = try_rwlock_rdlock(semid, semaptr);
		if (error)
			break;
		error = semaptr->ds.sem_base[semnum].semncnt;
		rwlock_unlock(semid, semaptr);
		break;

	case GETPID:
		if (semnum < 0 || semnum >= semaptr->ds.sem_nsems) {
			errno = EINVAL;
			break;
		}

		error = try_rwlock_rdlock(semid, semaptr);
		if (error)
			break;
		error = semaptr->ds.sem_base[semnum].sempid;
		rwlock_unlock(semid, semaptr);
		break;

	case GETVAL:
		if (semnum < 0 || semnum >= semaptr->ds.sem_nsems) {
			errno = EINVAL;
			break;
		}

		error = try_rwlock_rdlock(semid, semaptr);
		if (error)
			break;
		error = semaptr->ds.sem_base[semnum].semval;
		rwlock_unlock(semid, semaptr);
		break;

	case GETALL:
		if (!arg->array) {
			error = EFAULT;
			break;
		}

		error = try_rwlock_rdlock(semid, semaptr);
		if (error)
			break;
		for (i = 0; i < semaptr->ds.sem_nsems; i++) {
			arg->array[i] = semaptr->ds.sem_base[i].semval;
		}
		rwlock_unlock(semid, semaptr);
		break;

	case GETZCNT:
		if (semnum < 0 || semnum >= semaptr->ds.sem_nsems) {
			errno = EINVAL;
			break;
		}

		error = try_rwlock_rdlock(semid, semaptr);
		if (error)
			break;
		error = semaptr->ds.sem_base[semnum].semzcnt;
		rwlock_unlock(semid, semaptr);
		break;

	case SETVAL:
		if (semnum < 0 || semnum >= semaptr->ds.sem_nsems) {
			errno = EINVAL;
			break;
		}

		error = try_rwlock_wrlock(semid, semaptr);
		if (error)
			break;
		semptr = &semaptr->ds.sem_base[semnum];
		semptr->semval = arg->val;
		semundo_clear(semid, semnum);
		if (semptr->semzcnt || semptr->semncnt)
			umtx_wakeup((int *)&semptr->semval, 0);
		rwlock_unlock(semid, semaptr);
		break;

	case SETALL:
		if (!arg->array) {
			error = EFAULT;
			break;
		}

		error = try_rwlock_wrlock(semid, semaptr);
		if (error)
			break;
		for (i = 0; i < semaptr->ds.sem_nsems; i++) {
			semptr = &semaptr->ds.sem_base[i];
			semptr->semval = arg->array[i];
			if (semptr->semzcnt || semptr->semncnt)
				umtx_wakeup((int *)&semptr->semval, 0);
		}
		semundo_clear(semid, -1);
		rwlock_unlock(semid, semaptr);
		break;

	default:
		errno = EINVAL;
		break;
	}

	put_shmdata(semid);

	sysv_print("end semctl\n");
	return (error);
}

/*
 * Adjust a particular entry for a particular proc
 */
static int
semundo_adjust(int semid, int semnum, int adjval)
{
	struct undo *sunptr;
	int i;
	int error = 0;
	size_t size;
	int undoid;
	void *addr;
	struct shm_data *data;

	sysv_print("semundo adjust\n");
	if (!adjval)
		goto done;

	SYSV_MUTEX_LOCK(&lock_undo);
	if (!undos) {
		sysv_print("get undo segment\n");
		undoid = _shmget(IPC_PRIVATE, PAGE_SIZE, IPC_CREAT | IPC_EXCL | 0600,
				UNDOGET);
		if (undoid == -1) {
			sysv_print_err("no undo segment\n");
			return (-1);
		}

		addr = sysvipc_shmat(undoid, NULL, 0);
		if (!addr) {
			sysv_print_err("can not map undo segment\n");
			sysvipc_shmctl(undoid, IPC_RMID, NULL);
			return (-1);
		}

		undos = (struct sem_undo *)addr;
		undos->un_pages = 1;
		undos->un_cnt = 0;
	}

	/*
	 * Look for the requested entry and adjust it (delete if adjval becomes
	 * 0).
	 */
	sunptr = &undos->un_ent[0];
	for (i = 0; i < undos->un_cnt; i++, sunptr++) {
		if (sunptr->un_id != semid && sunptr->un_num != semnum)
			continue;
		sunptr->un_adjval += adjval;
		if (sunptr->un_adjval == 0) {
			undos->un_cnt--;
			if (i < undos->un_cnt)
				undos->un_ent[i] = undos->un_ent[undos->un_cnt];
		}
		goto done;
	}

	/* Didn't find the right entry - create it */
	size = sizeof(struct sem_undo) + (undos->un_cnt + 1) *
		sizeof(struct sem_undo);
	if (size > (unsigned int)(undos->un_pages * PAGE_SIZE)) {
		sysv_print("need more undo space\n");
		sysvipc_shmdt(undos);
		undos->un_pages++;

		SYSV_MUTEX_LOCK(&lock_resources);
		data = _hash_lookup(shmaddrs, (u_long)undos);
		SYSV_MUTEX_UNLOCK(&lock_resources);

		/* It is not necessary any lock on "size" because it is used
		 * only by shmat and shmdt.
		 * shmat for undoid is called only from this function and it
		 * is protected by undo_lock.
		 * shmdt for undoid is not called anywhere because the segment
		 * is destroyed by the daemon when the client dies.
		 */
		data->size = undos->un_pages * PAGE_SIZE;
		undos = sysvipc_shmat(data->shmid, NULL, 0);
	}

	sunptr = &undos->un_ent[undos->un_cnt];
	undos->un_cnt++;
	sunptr->un_adjval = adjval;
	sunptr->un_id = semid;
	sunptr->un_num = semnum;
	//if (suptr->un_cnt == seminfo.semume) TODO move it in daemon
	/*} else {
	  error = EINVAL; //se face prin notificare
	  }*/
done:
	SYSV_MUTEX_UNLOCK(&lock_undo);

	sysv_print("semundo adjust end\n");
	return (error);
}

int sysvipc_semop (int semid, struct sembuf *sops, unsigned nsops) {
	struct semid_pool *semaptr = NULL, *auxsemaptr = NULL;
	struct sembuf *sopptr;
	struct sem *semptr = NULL;
	struct sem *xsemptr = NULL;
	int eval = 0;
	int i, j;
	int do_undos;
	int val_to_sleep;

	sysv_print("[client %d] call to semop(%d, %u)\n",
			getpid(), semid, nsops);
//TODO
	/*if (!jail_sysvipc_allowed && td->td_ucred->cr_prison != NULL)
	  return (ENOSYS);
	  */

	semaptr = get_semaptr(semid, 0, IPC_W);
	if (!semaptr) {
		errno = EINVAL;
		return (-1);
	}

#ifdef SYSV_SEMS
	if (try_rwlock_rdlock(semid, semaptr) == -1) {
#else
	if (try_rwlock_wrlock(semid, semaptr) == -1) {
#endif
		sysv_print("sema removed\n");
		errno = EIDRM;
		goto done2;
	}

	if (nsops > MAX_SOPS) {
		sysv_print("too many sops (max=%d, nsops=%u)\n",
				getpid(), MAX_SOPS, nsops);
		eval = E2BIG;
		goto done;
	}

	/*
	* Loop trying to satisfy the vector of requests.
	* If we reach a point where we must wait, any requests already
	* performed are rolled back and we go to sleep until some other
	* process wakes us up.  At this point, we start all over again.
	*
	* This ensures that from the perspective of other tasks, a set
	* of requests is atomic (never partially satisfied).
	*/
	do_undos = 0;

	for (;;) {

		semptr = NULL;

		for (i = 0; i < (int)nsops; i++) {
			sopptr = &sops[i];

			if (sopptr->sem_num >= semaptr->ds.sem_nsems) {
				eval = EFBIG;
				goto done;
			}

			semptr = &semaptr->ds.sem_base[sopptr->sem_num];
#ifdef SYSV_SEMS
			sysv_mutex_lock(&semptr->sem_mutex);
#endif
			sysv_print("semop: sem[%d]=%d : op=%d, flag=%s\n",
				sopptr->sem_num, semptr->semval, sopptr->sem_op,
				(sopptr->sem_flg & IPC_NOWAIT) ? "nowait" : "wait");

			if (sopptr->sem_op < 0) {
				if (semptr->semval + sopptr->sem_op < 0) {
					sysv_print("semop:  can't do it now\n");
					break;
				} else {
					semptr->semval += sopptr->sem_op;
					if (semptr->semval == 0 &&
						semptr->semzcnt > 0)
						umtx_wakeup((int *)&semptr->semval, 0);
				}
				if (sopptr->sem_flg & SEM_UNDO)
					do_undos = 1;
			} else if (sopptr->sem_op == 0) {
				if (semptr->semval > 0) {
					sysv_print("semop:  not zero now\n");
					break;
				}
			} else {
				semptr->semval += sopptr->sem_op;
				if (sopptr->sem_flg & SEM_UNDO)
					do_undos = 1;
				if (semptr->semncnt > 0)
					umtx_wakeup((int *)&semptr->semval, 0);
			}
#ifdef SYSV_SEMS
			sysv_mutex_unlock(&semptr->sem_mutex);
#endif
		}

		/*
		 * Did we get through the entire vector?
		 */
		if (i >= (int)nsops)
			goto donex;

		if (sopptr->sem_op == 0)
			semptr->semzcnt++;
		else
			semptr->semncnt++;
#ifdef SYSV_SEMS
		sysv_mutex_unlock(&semptr->sem_mutex);
#endif
		/*
		 * Rollback the semaphores we had acquired.
		 */
		sysv_print("semop:  rollback 0 through %d\n", i-1);
		for (j = 0; j < i; j++) {
			xsemptr = &semaptr->ds.sem_base[sops[j].sem_num];
#ifdef SYSV_SEMS
			sysv_mutex_lock(&semptr->sem_mutex);
#endif
			xsemptr->semval -= sops[j].sem_op;
			if (xsemptr->semval == 0 && xsemptr->semzcnt > 0)
				umtx_wakeup((int *)&xsemptr->semval, 0);
			if (xsemptr->semval <= 0 && xsemptr->semncnt > 0)
				umtx_wakeup((int *)&xsemptr->semval, 0); //?!
#ifdef SYSV_SEMS
			sysv_mutex_unlock(&semptr->sem_mutex);
#endif
		}

		/*
		 * If the request that we couldn't satisfy has the
		 * NOWAIT flag set then return with EAGAIN.
		 */
		if (sopptr->sem_flg & IPC_NOWAIT) {
			eval = EAGAIN;
			goto done;
		}

		/*
		 * Release semaptr->lock while sleeping, allowing other
		 * semops (like SETVAL, SETALL, etc), which require an
		 * exclusive lock and might wake us up.
		 *
		 * Reload and recheck the validity of semaptr on return.
		 * Note that semptr itself might have changed too, but
		 * we've already interlocked for semptr and that is what
		 * will be woken up if it wakes up the tsleep on a MP
		 * race.
		 *
		 */

		sysv_print("semop:  good night!\n");
		val_to_sleep = semptr->semval;
		rwlock_unlock(semid, semaptr);
		put_shmdata(semid);

		/* We don't sleep more than SYSV_TIMEOUT because we could
		 * go to sleep after another process calls wakeup and remain
		 * blocked.
		 */
		eval = umtx_sleep((int *)&semptr->semval, val_to_sleep, SYSV_TIMEOUT);
		/* return code is checked below, after sem[nz]cnt-- */

		/*
		 * Make sure that the semaphore still exists
		 */

		/* Check if another thread didn't remove the semaphore. */
		auxsemaptr = get_semaptr(semid, 0, IPC_W); /* Redundant access check. */
		if (!auxsemaptr) {
			errno = EIDRM;
			return (-1);
		}
			
		if (auxsemaptr != semaptr) {
			errno = EIDRM;
			goto done;
		}

		/* Check if another process didn't remove the semaphore. */
#ifdef SYSV_SEMS
		if (try_rwlock_rdlock(semid, semaptr) == -1) {
#else
		if (try_rwlock_wrlock(semid, semaptr) == -1) {
#endif
			errno = EIDRM;
			goto done;
		}
		sysv_print("semop:  good morning (eval=%d)!\n", eval);

		/* The semaphore is still alive.  Readjust the count of
		 * waiting processes.
		 */
		semptr = &semaptr->ds.sem_base[sopptr->sem_num];
#ifdef SYSV_SEMS
		sysv_mutex_lock(&semptr->sem_mutex);
#endif
		if (sopptr->sem_op == 0)
			semptr->semzcnt--;
		else
			semptr->semncnt--;
#ifdef SYSV_SEMS
		sysv_mutex_unlock(&semptr->sem_mutex);
#endif

		/*
		 * Is it really morning, or was our sleep interrupted?
		 * (Delayed check of tsleep() return code because we
		 * need to decrement sem[nz]cnt either way.)
		 */
		if (eval) {
			eval = EINTR;
			goto done;
		}

		sysv_print("semop:  good morning!\n");
		/* RETRY LOOP */
}

donex:
	/*
	* Process any SEM_UNDO requests.
	*/
	if (do_undos) {
		for (i = 0; i < (int)nsops; i++) {
			/*
			 * We only need to deal with SEM_UNDO's for non-zero
			 * op's.
			 */
			int adjval;

			if ((sops[i].sem_flg & SEM_UNDO) == 0)
				continue;
			adjval = sops[i].sem_op;
			if (adjval == 0)
				continue;
			eval = semundo_adjust(semid, sops[i].sem_num, -adjval);
			if (eval == 0)
				continue;

			/*
			 * Oh-Oh!  We ran out of either sem_undo's or undo's.
			 * Rollback the adjustments to this point and then
			 * rollback the semaphore ups and down so we can return
			 * with an error with all structures restored.  We
			 * rollback the undo's in the exact reverse order that
			 * we applied them.  This guarantees that we won't run
			 * out of space as we roll things back out.
			 */
			for (j = i - 1; j >= 0; j--) {
				if ((sops[j].sem_flg & SEM_UNDO) == 0)
					continue;
				adjval = sops[j].sem_op;
				if (adjval == 0)
					continue;
				if (semundo_adjust(semid, sops[j].sem_num,
							adjval) != 0)
					sysv_print("semop - can't undo undos");
			}

			for (j = 0; j < (int)nsops; j++) {
				xsemptr = &semaptr->ds.sem_base[
					sops[j].sem_num];
#ifdef SYSV_SEMS
				sysv_mutex_lock(&semptr->sem_mutex);
#endif
				xsemptr->semval -= sops[j].sem_op;
				if (xsemptr->semval == 0 &&
						xsemptr->semzcnt > 0)
					umtx_wakeup((int *)&xsemptr->semval, 0);
				if (xsemptr->semval <= 0 &&
						xsemptr->semncnt > 0)
					umtx_wakeup((int *)&xsemptr->semval, 0); //?!
#ifdef SYSV_SEMS
				sysv_mutex_unlock(&semptr->sem_mutex);
#endif
			}

			sysv_print("eval = %d from semundo_adjust\n", eval);
			goto done;
		}
	}

	/* Set sempid field for each semaphore. */
	for (i = 0; i < (int)nsops; i++) {
		sopptr = &sops[i];
		semptr = &semaptr->ds.sem_base[sopptr->sem_num];
#ifdef SYSV_SEMS
		sysv_mutex_lock(&semptr->sem_mutex);
#endif
		semptr->sempid = getpid();
#ifdef SYSV_SEMS
		sysv_mutex_unlock(&semptr->sem_mutex);
#endif
	}

	sysv_print("semop:  done\n");
	semaptr->ds.sem_otime = time(NULL);
done:
	rwlock_unlock(semid, semaptr);
done2:
	put_shmdata(semid);

	return (eval);
}
