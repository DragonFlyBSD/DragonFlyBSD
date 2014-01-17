/* $FreeBSD: src/sys/kern/sysv_sem.c,v 1.69 2004/03/17 09:37:13 cperciva Exp $ */

/*
 * Implementation of SVID semaphores
 *
 * Author:  Daniel Boulet
 * Copyright (c) 2013 Larisa Grigore <larisagrigore@gmail.com>
 *
 * This software is provided ``AS IS'' without any warranties of any kind.
 */

#ifndef _SYSV_SEM_H_
#define _SYSV_SEM_H_

#include <sys/sem.h>

#include "sysvipc_lock.h"
#include "sysvipc_lock_generic.h"

//#define SYSV_SEMS
		/* Used to define if each semaphore in the
		 * set is protected by a mutex, the entire
		 * group being protected by a read lock.
		 * If SYSV_SEMS is not defined, then the entire
		 * group is protected only by a write lock.
		 */
struct sem {
	u_short	semval;		/* semaphore value */
	pid_t	sempid;		/* pid of last operation */
	u_short	semncnt;	/* # awaiting semval > cval */
	u_short	semzcnt;	/* # awaiting semval = 0 */
#ifdef SYSV_SEMS
	struct sysv_mutex sem_mutex;
#endif
};

/* Used internally. The struct semid_ds is used only
 * by caller, as argument to semctl.
 */
struct semid_ds_internal {
	struct	ipc_perm sem_perm;	/* operation permission struct */
	u_short	sem_nsems;	/* number of sems in set */
	time_t	sem_otime;	/* last operation time */
	time_t	sem_ctime;	/* last change time */
    				/* Times measured in secs since */
    				/* 00:00:00 GMT, Jan. 1, 1970 */
	struct	sem sem_base[0];	/* pointer to first semaphore in set */
};

struct semid_pool {
#ifdef SYSV_RWLOCK
	struct sysv_rwlock rwlock;
#else
	struct sysv_mutex mutex;
#endif
	struct semid_ds_internal ds;
	char gen;
};

/*
 * Undo structure (one per process)
 */
struct sem_undo {
//	pthread_rwlock_t un_lock;
	int	un_pages;
	short	un_cnt;			/* # of active entries */
	short	un_unused;
	struct undo {
		short	un_adjval;	/* adjust on exit values */
		short	un_num;		/* semaphore # */
		int	un_id;		/* semid */
	} un_ent[0];			/* undo entries */
};

int sysvipc___semctl (int, int, int, union semun *);
int sysvipc_semget (key_t, int, int);
int sysvipc_semop (int, struct sembuf *, unsigned);

#endif /* !_SEM_H_ */
