/* $FreeBSD: src/sys/kern/sysv_sem.c,v 1.69 2004/03/17 09:37:13 cperciva Exp $ */

/*
 * Implementation of SVID semaphores
 *
 * Author:  Daniel Boulet
 *
 * This software is provided ``AS IS'' without any warranties of any kind.
 */

#include "opt_sysvipc.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/sysproto.h>
#include <sys/kernel.h>
#include <sys/proc.h>
#include <sys/sem.h>
#include <sys/sysent.h>
#include <sys/sysctl.h>
#include <sys/malloc.h>
#include <sys/jail.h>
#include <sys/thread.h>

#include <sys/thread2.h>

static MALLOC_DEFINE(M_SEM, "sem", "SVID compatible semaphores");

static void seminit (void *);

static struct sem_undo *semu_alloc (struct proc *p);
static int semundo_adjust (struct proc *p, int semid, int semnum, int adjval);
static void semundo_clear (int semid, int semnum);

static struct lwkt_token semu_token = LWKT_TOKEN_INITIALIZER(semu_token);
static int	semtot = 0;
static struct semid_pool *sema;	/* semaphore id pool */
static TAILQ_HEAD(, sem_undo) semu_list = TAILQ_HEAD_INITIALIZER(semu_list);
static struct lock sema_lk;

struct sem {
	u_short	semval;		/* semaphore value */
	pid_t	sempid;		/* pid of last operation */
	u_short	semncnt;	/* # awaiting semval > cval */
	u_short	semzcnt;	/* # awaiting semval = 0 */
};

/*
 * Undo structure (one per process)
 */
struct sem_undo {
	TAILQ_ENTRY(sem_undo) un_entry;	/* linked list for semundo_clear() */
	struct	proc *un_proc;		/* owner of this structure */
	int	un_refs;		/* prevent unlink/kfree */
	short	un_cnt;			/* # of active entries */
	short	un_unused;
	struct undo {
		short	un_adjval;	/* adjust on exit values */
		short	un_num;		/* semaphore # */
		int	un_id;		/* semid */
	} un_ent[1];			/* undo entries */
};

/*
 * Configuration parameters
 */
#ifndef SEMMNI
#define SEMMNI	1024		/* # of semaphore identifiers */
#endif
#ifndef SEMMNS
#define SEMMNS	32767		/* # of semaphores in system */
#endif
#ifndef SEMUME
#define SEMUME	25		/* max # of undo entries per process */
#endif
#ifndef SEMMNU
#define SEMMNU	1024		/* # of undo structures in system */
				/* NO LONGER USED */
#endif

/* shouldn't need tuning */
#ifndef SEMMAP
#define SEMMAP	128		/* # of entries in semaphore map */
#endif
#ifndef SEMMSL
#define SEMMSL	SEMMNS		/* max # of semaphores per id */
#endif
#ifndef SEMOPM
#define SEMOPM	100		/* max # of operations per semop call */
#endif

#define SEMVMX	32767		/* semaphore maximum value */
#define SEMAEM	16384		/* adjust on exit max value */

/*
 * Due to the way semaphore memory is allocated, we have to ensure that
 * SEMUSZ is properly aligned.
 */

#define SEM_ALIGN(bytes) (((bytes) + (sizeof(long) - 1)) & ~(sizeof(long) - 1))

/* actual size of an undo structure */
#define SEMUSZ(nent)	SEM_ALIGN(offsetof(struct sem_undo, un_ent[nent]))

/*
 * semaphore info struct
 */
struct seminfo seminfo = {
                SEMMAP,         /* # of entries in semaphore map */
                SEMMNI,         /* # of semaphore identifiers */
                SEMMNS,         /* # of semaphores in system */
                SEMMNU,         /* # of undo structures in system */
                SEMMSL,         /* max # of semaphores per id */
                SEMOPM,         /* max # of operations per semop call */
                SEMUME,         /* max # of undo entries per process */
                SEMUSZ(SEMUME), /* size in bytes of undo structure */
                SEMVMX,         /* semaphore maximum value */
                SEMAEM          /* adjust on exit max value */
};

TUNABLE_INT("kern.ipc.semmap", &seminfo.semmap);
TUNABLE_INT("kern.ipc.semmni", &seminfo.semmni);
TUNABLE_INT("kern.ipc.semmns", &seminfo.semmns);
TUNABLE_INT("kern.ipc.semmnu", &seminfo.semmnu);
TUNABLE_INT("kern.ipc.semmsl", &seminfo.semmsl);
TUNABLE_INT("kern.ipc.semopm", &seminfo.semopm);
TUNABLE_INT("kern.ipc.semume", &seminfo.semume);
TUNABLE_INT("kern.ipc.semusz", &seminfo.semusz);
TUNABLE_INT("kern.ipc.semvmx", &seminfo.semvmx);
TUNABLE_INT("kern.ipc.semaem", &seminfo.semaem);

SYSCTL_INT(_kern_ipc, OID_AUTO, semmap, CTLFLAG_RW, &seminfo.semmap, 0,
    "Number of entries in semaphore map");
SYSCTL_INT(_kern_ipc, OID_AUTO, semmni, CTLFLAG_RD, &seminfo.semmni, 0,
    "Number of semaphore identifiers");
SYSCTL_INT(_kern_ipc, OID_AUTO, semmns, CTLFLAG_RD, &seminfo.semmns, 0,
    "Total number of semaphores");
SYSCTL_INT(_kern_ipc, OID_AUTO, semmnu, CTLFLAG_RD, &seminfo.semmnu, 0,
    "Total number of undo structures");
SYSCTL_INT(_kern_ipc, OID_AUTO, semmsl, CTLFLAG_RW, &seminfo.semmsl, 0,
    "Max number of semaphores per id");
SYSCTL_INT(_kern_ipc, OID_AUTO, semopm, CTLFLAG_RD, &seminfo.semopm, 0,
    "Max number of operations per semop call");
SYSCTL_INT(_kern_ipc, OID_AUTO, semume, CTLFLAG_RD, &seminfo.semume, 0,
    "Max number of undo entries per process");
SYSCTL_INT(_kern_ipc, OID_AUTO, semusz, CTLFLAG_RD, &seminfo.semusz, 0,
    "Size in bytes of undo structure");
SYSCTL_INT(_kern_ipc, OID_AUTO, semvmx, CTLFLAG_RW, &seminfo.semvmx, 0,
    "Semaphore maximum value");
SYSCTL_INT(_kern_ipc, OID_AUTO, semaem, CTLFLAG_RW, &seminfo.semaem, 0,
    "Adjust on exit max value");

#if 0
RO seminfo.semmap	/* SEMMAP unused */
RO seminfo.semmni
RO seminfo.semmns
RO seminfo.semmnu	/* undo entries per system */
RW seminfo.semmsl
RO seminfo.semopm	/* SEMOPM unused */
RO seminfo.semume
RO seminfo.semusz	/* param - derived from SEMUME for per-proc sizeof */
RO seminfo.semvmx	/* SEMVMX unused - user param */
RO seminfo.semaem	/* SEMAEM unused - user param */
#endif

static void
seminit(void *dummy)
{
	int i;

	sema = kmalloc(sizeof(struct semid_pool) * seminfo.semmni,
		      M_SEM, M_WAITOK | M_ZERO);

	lockinit(&sema_lk, "semglb", 0, 0);
	for (i = 0; i < seminfo.semmni; i++) {
		struct semid_pool *semaptr = &sema[i];

		lockinit(&semaptr->lk, "semary", 0, 0);
		semaptr->ds.sem_base = NULL;
		semaptr->ds.sem_perm.mode = 0;
	}
}
SYSINIT(sysv_sem, SI_SUB_SYSV_SEM, SI_ORDER_FIRST, seminit, NULL);

/*
 * Allocate a new sem_undo structure for a process
 * (returns ptr to structure or NULL if no more room)
 */
static struct sem_undo *
semu_alloc(struct proc *p)
{
	struct sem_undo *semu;

	/*
	 * Allocate the semu structure and associate it with the process,
	 * as necessary.
	 */
	while ((semu = p->p_sem_undo) == NULL) {
		semu = kmalloc(SEMUSZ(seminfo.semume), M_SEM,
			       M_WAITOK | M_ZERO);
		lwkt_gettoken(&semu_token);
		lwkt_gettoken(&p->p_token);
		if (p->p_sem_undo == NULL) {
			p->p_sem_undo = semu;
			p->p_flags |= P_SYSVSEM;
			semu->un_proc = p;
			TAILQ_INSERT_TAIL(&semu_list, semu, un_entry);
		} else {
			kfree(semu, M_SEM);
		}
		lwkt_reltoken(&p->p_token);
		lwkt_reltoken(&semu_token);
	}
	return(semu);
}

/*
 * Adjust a particular entry for a particular proc
 */
static int
semundo_adjust(struct proc *p, int semid, int semnum, int adjval)
{
	struct sem_undo *suptr;
	struct undo *sunptr;
	int i;
	int error = 0;

	/*
	 * Look for and remember the sem_undo if the caller doesn't
	 * provide it.
	 */
	suptr = semu_alloc(p);
	lwkt_gettoken(&p->p_token);

	/*
	 * Look for the requested entry and adjust it (delete if adjval becomes
	 * 0).
	 */
	sunptr = &suptr->un_ent[0];
	for (i = 0; i < suptr->un_cnt; i++, sunptr++) {
		if (sunptr->un_id != semid || sunptr->un_num != semnum)
			continue;
		if (adjval == 0)
			sunptr->un_adjval = 0;
		else
			sunptr->un_adjval += adjval;
		if (sunptr->un_adjval == 0) {
			suptr->un_cnt--;
			if (i < suptr->un_cnt)
				suptr->un_ent[i] = suptr->un_ent[suptr->un_cnt];
		}
		goto done;
	}

	/* Didn't find the right entry - create it */
	if (adjval == 0)
		goto done;
	if (suptr->un_cnt != seminfo.semume) {
		sunptr = &suptr->un_ent[suptr->un_cnt];
		suptr->un_cnt++;
		sunptr->un_adjval = adjval;
		sunptr->un_id = semid;
		sunptr->un_num = semnum;
	} else {
		error = EINVAL;
	}
done:
	lwkt_reltoken(&p->p_token);

	return (error);
}

/*
 * This is rather expensive
 */
static void
semundo_clear(int semid, int semnum)
{
	struct proc *p;
	struct sem_undo *suptr;
	struct sem_undo *sunext;
	struct undo *sunptr;
	int i;

	lwkt_gettoken(&semu_token);
	sunext = TAILQ_FIRST(&semu_list);
	while ((suptr = sunext) != NULL) {
		if ((p = suptr->un_proc) == NULL) {
			suptr = TAILQ_NEXT(suptr, un_entry);
			continue;
		}
		++suptr->un_refs;
		PHOLD(p);
		lwkt_gettoken(&p->p_token);

		sunptr = &suptr->un_ent[0];
		i = 0;

		while (i < suptr->un_cnt) {
			if (sunptr->un_id == semid) {
				if (semnum == -1 || sunptr->un_num == semnum) {
					suptr->un_cnt--;
					if (i < suptr->un_cnt) {
						suptr->un_ent[i] =
						  suptr->un_ent[suptr->un_cnt];
						/*
						 * do not increment i
						 * or sunptr after copydown.
						 */
						continue;
					}
				}
				if (semnum != -1)
					break;
			}
			++i;
			++sunptr;
		}

		lwkt_reltoken(&p->p_token);
		PRELE(p);

		/*
		 * Handle deletion races
		 */
		sunext = TAILQ_NEXT(suptr, un_entry);
		if (--suptr->un_refs == 0 && suptr->un_proc == NULL) {
			KKASSERT(suptr->un_cnt == 0);
			TAILQ_REMOVE(&semu_list, suptr, un_entry);
			kfree(suptr, M_SEM);
		}
	}
	lwkt_reltoken(&semu_token);
}

/*
 * Note that the user-mode half of this passes a union, not a pointer
 *
 * MPALMOSTSAFE
 */
int
sys___semctl(struct __semctl_args *uap)
{
	struct thread *td = curthread;
	int semid = uap->semid;
	int semnum = uap->semnum;
	int cmd = uap->cmd;
	union semun *arg = uap->arg;
	union semun real_arg;
	struct ucred *cred = td->td_ucred;
	int i, rval, eval;
	struct semid_ds sbuf;
	struct semid_pool *semaptr;
	struct semid_pool *semakptr;
	struct sem *semptr;

#ifdef SEM_DEBUG
	kprintf("call to semctl(%d, %d, %d, 0x%x)\n", semid, semnum, cmd, arg);
#endif

	if (!jail_sysvipc_allowed && cred->cr_prison != NULL)
		return (ENOSYS);

	switch (cmd) {
	case SEM_STAT:
		/*
		 * For this command we assume semid is an array index
		 * rather than an IPC id.
		 */
		if (semid < 0 || semid >= seminfo.semmni) {
			eval = EINVAL;
			break;
		}
		semakptr = &sema[semid];
		lockmgr(&semakptr->lk, LK_EXCLUSIVE);
		if ((semakptr->ds.sem_perm.mode & SEM_ALLOC) == 0) {
			eval = EINVAL;
			lockmgr(&semakptr->lk, LK_RELEASE);
			break;
		}
		if ((eval = ipcperm(td->td_proc, &semakptr->ds.sem_perm, IPC_R))) {
			lockmgr(&semakptr->lk, LK_RELEASE);
			break;
		}
		bcopy(&semakptr->ds, arg->buf, sizeof(struct semid_ds));
		rval = IXSEQ_TO_IPCID(semid, semakptr->ds.sem_perm);
		lockmgr(&semakptr->lk, LK_RELEASE);
		break;
	}
	
	semid = IPCID_TO_IX(semid);
	if (semid < 0 || semid >= seminfo.semmni) {
		return(EINVAL);
	}
	semaptr = &sema[semid];
	lockmgr(&semaptr->lk, LK_EXCLUSIVE);

	if ((semaptr->ds.sem_perm.mode & SEM_ALLOC) == 0 ||
	    semaptr->ds.sem_perm.seq != IPCID_TO_SEQ(uap->semid)) {
		lockmgr(&semaptr->lk, LK_RELEASE);
		return(EINVAL);
	}

	eval = 0;
	rval = 0;

	switch (cmd) {
	case IPC_RMID:
		eval = ipcperm(td->td_proc, &semaptr->ds.sem_perm, IPC_M);
		if (eval != 0)
			break;
		semaptr->ds.sem_perm.cuid = cred->cr_uid;
		semaptr->ds.sem_perm.uid = cred->cr_uid;

		/*
		 * NOTE: Nobody will be waiting on the semaphores since
		 *	 we have an exclusive lock on semaptr->lk).
		 */
		lockmgr(&sema_lk, LK_EXCLUSIVE);
		semtot -= semaptr->ds.sem_nsems;
		kfree(semaptr->ds.sem_base, M_SEM);
		semaptr->ds.sem_base = NULL;
		semaptr->ds.sem_perm.mode = 0;	/* clears SEM_ALLOC */
		lockmgr(&sema_lk, LK_RELEASE);

		semundo_clear(semid, -1);
		break;

	case IPC_SET:
		eval = ipcperm(td->td_proc, &semaptr->ds.sem_perm, IPC_M);
		if (eval)
			break;
		if ((eval = copyin(arg, &real_arg, sizeof(real_arg))) != 0)
			break;
		if ((eval = copyin(real_arg.buf, (caddr_t)&sbuf,
				   sizeof(sbuf))) != 0) {
			break;
		}
		semaptr->ds.sem_perm.uid = sbuf.sem_perm.uid;
		semaptr->ds.sem_perm.gid = sbuf.sem_perm.gid;
		semaptr->ds.sem_perm.mode =
			(semaptr->ds.sem_perm.mode & ~0777) |
			(sbuf.sem_perm.mode & 0777);
		semaptr->ds.sem_ctime = time_second;
		break;

	case IPC_STAT:
		eval = ipcperm(td->td_proc, &semaptr->ds.sem_perm, IPC_R);
		if (eval)
			break;
		if ((eval = copyin(arg, &real_arg, sizeof(real_arg))) != 0)
			break;
		eval = copyout(&semaptr->ds, real_arg.buf,
			       sizeof(struct semid_ds));
		break;

	case GETNCNT:
		eval = ipcperm(td->td_proc, &semaptr->ds.sem_perm, IPC_R);
		if (eval)
			break;
		if (semnum < 0 || semnum >= semaptr->ds.sem_nsems) {
			eval = EINVAL;
			break;
		}
		rval = semaptr->ds.sem_base[semnum].semncnt;
		break;

	case GETPID:
		eval = ipcperm(td->td_proc, &semaptr->ds.sem_perm, IPC_R);
		if (eval)
			break;
		if (semnum < 0 || semnum >= semaptr->ds.sem_nsems) {
			eval = EINVAL;
			break;
		}
		rval = semaptr->ds.sem_base[semnum].sempid;
		break;

	case GETVAL:
		eval = ipcperm(td->td_proc, &semaptr->ds.sem_perm, IPC_R);
		if (eval)
			break;
		if (semnum < 0 || semnum >= semaptr->ds.sem_nsems) {
			eval = EINVAL;
			break;
		}
		rval = semaptr->ds.sem_base[semnum].semval;
		break;

	case GETALL:
		eval = ipcperm(td->td_proc, &semaptr->ds.sem_perm, IPC_R);
		if (eval)
			break;
		if ((eval = copyin(arg, &real_arg, sizeof(real_arg))) != 0)
			break;
		for (i = 0; i < semaptr->ds.sem_nsems; i++) {
			eval = copyout(&semaptr->ds.sem_base[i].semval,
				       &real_arg.array[i],
				       sizeof(real_arg.array[0]));
			if (eval)
				break;
		}
		break;

	case GETZCNT:
		eval = ipcperm(td->td_proc, &semaptr->ds.sem_perm, IPC_R);
		if (eval)
			break;
		if (semnum < 0 || semnum >= semaptr->ds.sem_nsems) {
			eval = EINVAL;
			break;
		}
		rval = semaptr->ds.sem_base[semnum].semzcnt;
		break;

	case SETVAL:
		eval = ipcperm(td->td_proc, &semaptr->ds.sem_perm, IPC_W);
		if (eval)
			break;
		if (semnum < 0 || semnum >= semaptr->ds.sem_nsems) {
			eval = EINVAL;
			break;
		}
		if ((eval = copyin(arg, &real_arg, sizeof(real_arg))) != 0)
			break;

		/*
		 * Because we hold semaptr->lk exclusively we can safely
		 * modify any semptr content without acquiring its token.
		 */
		semptr = &semaptr->ds.sem_base[semnum];
		semptr->semval = real_arg.val;
		semundo_clear(semid, semnum);
		if (semptr->semzcnt || semptr->semncnt)
			wakeup(semptr);
		break;

	case SETALL:
		eval = ipcperm(td->td_proc, &semaptr->ds.sem_perm, IPC_W);
		if (eval)
			break;
		if ((eval = copyin(arg, &real_arg, sizeof(real_arg))) != 0)
			break;
		/*
		 * Because we hold semaptr->lk exclusively we can safely
		 * modify any semptr content without acquiring its token.
		 */
		for (i = 0; i < semaptr->ds.sem_nsems; i++) {
			semptr = &semaptr->ds.sem_base[i];
			eval = copyin(&real_arg.array[i],
				      (caddr_t)&semptr->semval,
				      sizeof(real_arg.array[0]));
			if (semptr->semzcnt || semptr->semncnt)
				wakeup(semptr);
			if (eval != 0)
				break;
		}
		semundo_clear(semid, -1);
		break;

	default:
		eval = EINVAL;
		break;
	}
	lockmgr(&semaptr->lk, LK_RELEASE);

	if (eval == 0)
		uap->sysmsg_result = rval;
	return(eval);
}

/*
 * MPALMOSTSAFE
 */
int
sys_semget(struct semget_args *uap)
{
	struct thread *td = curthread;
	int semid, eval;
	int key = uap->key;
	int nsems = uap->nsems;
	int semflg = uap->semflg;
	struct ucred *cred = td->td_ucred;

#ifdef SEM_DEBUG
	kprintf("semget(0x%x, %d, 0%o)\n", key, nsems, semflg);
#endif

	if (!jail_sysvipc_allowed && cred->cr_prison != NULL)
		return (ENOSYS);

	eval = 0;

	if (key != IPC_PRIVATE) {
		for (semid = 0; semid < seminfo.semmni; semid++) {
			if ((sema[semid].ds.sem_perm.mode & SEM_ALLOC) == 0 ||
			    sema[semid].ds.sem_perm.key != key) {
				continue;
			}
			lockmgr(&sema[semid].lk, LK_EXCLUSIVE);
			if ((sema[semid].ds.sem_perm.mode & SEM_ALLOC) == 0 ||
			    sema[semid].ds.sem_perm.key != key) {
				lockmgr(&sema[semid].lk, LK_RELEASE);
				continue;
			}
			break;
		}
		if (semid < seminfo.semmni) {
			/* sema[semid].lk still locked from above */
#ifdef SEM_DEBUG
			kprintf("found public key\n");
#endif
			if ((eval = ipcperm(td->td_proc,
					    &sema[semid].ds.sem_perm,
					    semflg & 0700))) {
				lockmgr(&sema[semid].lk, LK_RELEASE);
				goto done;
			}
			if (nsems > 0 && sema[semid].ds.sem_nsems < nsems) {
#ifdef SEM_DEBUG
				kprintf("too small\n");
#endif
				eval = EINVAL;
				lockmgr(&sema[semid].lk, LK_RELEASE);
				goto done;
			}
			if ((semflg & IPC_CREAT) && (semflg & IPC_EXCL)) {
#ifdef SEM_DEBUG
				kprintf("not exclusive\n");
#endif
				eval = EEXIST;
				lockmgr(&sema[semid].lk, LK_RELEASE);
				goto done;
			}

			/*
			 * Return this one.
			 */
			lockmgr(&sema[semid].lk, LK_RELEASE);
			goto done;
		}
	}

#ifdef SEM_DEBUG
	kprintf("need to allocate the semid_ds\n");
#endif
	if (key == IPC_PRIVATE || (semflg & IPC_CREAT)) {
		if (nsems <= 0 || nsems > seminfo.semmsl) {
#ifdef SEM_DEBUG
			kprintf("nsems out of range (0<%d<=%d)\n",
				nsems, seminfo.semmsl);
#endif
			eval = EINVAL;
			goto done;
		}

		/*
		 * SEM_ALLOC flag cannot be set unless sema_lk is locked.
		 * semtot field also protected by sema_lk.
		 */
		lockmgr(&sema_lk, LK_EXCLUSIVE);
		if (nsems > seminfo.semmns - semtot) {
#ifdef SEM_DEBUG
			kprintf("not enough semaphores left "
				"(need %d, got %d)\n",
				nsems, seminfo.semmns - semtot);
#endif
			eval = ENOSPC;
			lockmgr(&sema_lk, LK_RELEASE);
			goto done;
		}
		for (semid = 0; semid < seminfo.semmni; semid++) {
			if ((sema[semid].ds.sem_perm.mode & SEM_ALLOC) == 0)
				break;
		}
		if (semid == seminfo.semmni) {
#ifdef SEM_DEBUG
			kprintf("no more semid_ds's available\n");
#endif
			eval = ENOSPC;
			lockmgr(&sema_lk, LK_RELEASE);
			goto done;
		}
#ifdef SEM_DEBUG
		kprintf("semid %d is available\n", semid);
#endif
		lockmgr(&sema[semid].lk, LK_EXCLUSIVE);
		sema[semid].ds.sem_perm.key = key;
		sema[semid].ds.sem_perm.cuid = cred->cr_uid;
		sema[semid].ds.sem_perm.uid = cred->cr_uid;
		sema[semid].ds.sem_perm.cgid = cred->cr_gid;
		sema[semid].ds.sem_perm.gid = cred->cr_gid;
		sema[semid].ds.sem_perm.mode = (semflg & 0777) | SEM_ALLOC;
		sema[semid].ds.sem_perm.seq =
		    (sema[semid].ds.sem_perm.seq + 1) & 0x7fff;
		sema[semid].ds.sem_nsems = nsems;
		sema[semid].ds.sem_otime = 0;
		sema[semid].ds.sem_ctime = time_second;
		sema[semid].ds.sem_base = kmalloc(sizeof(struct sem) * nsems,
					       M_SEM, M_WAITOK|M_ZERO);
		semtot += nsems;
		++sema[semid].gen;
		lockmgr(&sema[semid].lk, LK_RELEASE);
		lockmgr(&sema_lk, LK_RELEASE);
#ifdef SEM_DEBUG
		kprintf("sembase = 0x%x, next = 0x%x\n",
			sema[semid].ds.sem_base, &sem[semtot]);
#endif
		/* eval == 0 */
	} else {
#ifdef SEM_DEBUG
		kprintf("didn't find it and wasn't asked to create it\n");
#endif
		eval = ENOENT;
	}

done:
	if (eval == 0) {
		uap->sysmsg_result =
			IXSEQ_TO_IPCID(semid, sema[semid].ds.sem_perm);
	}
	return(eval);
}

/*
 * MPSAFE
 */
int
sys_semop(struct semop_args *uap)
{
	struct thread *td = curthread;
	int semid = uap->semid;
	u_int nsops = uap->nsops;
	struct sembuf sops[MAX_SOPS];
	struct semid_pool *semaptr;
	struct sembuf *sopptr;
	struct sem *semptr;
	struct sem *xsemptr;
	int i, j, eval;
	int do_undos;

#ifdef SEM_DEBUG
	kprintf("call to semop(%d, 0x%x, %u)\n", semid, sops, nsops);
#endif
	if (!jail_sysvipc_allowed && td->td_ucred->cr_prison != NULL)
		return (ENOSYS);

	semid = IPCID_TO_IX(semid);	/* Convert back to zero origin */

	if (semid < 0 || semid >= seminfo.semmni) {
		eval = EINVAL;
		goto done2;
	}

	wakeup_start_delayed();
	semaptr = &sema[semid];
	lockmgr(&semaptr->lk, LK_SHARED);

	if ((semaptr->ds.sem_perm.mode & SEM_ALLOC) == 0) {
		eval = EINVAL;
		goto done;
	}
	if (semaptr->ds.sem_perm.seq != IPCID_TO_SEQ(uap->semid)) {
		eval = EINVAL;
		goto done;
	}

	if ((eval = ipcperm(td->td_proc, &semaptr->ds.sem_perm, IPC_W))) {
#ifdef SEM_DEBUG
		kprintf("eval = %d from ipaccess\n", eval);
#endif
		goto done;
	}

	if (nsops > MAX_SOPS) {
#ifdef SEM_DEBUG
		kprintf("too many sops (max=%d, nsops=%u)\n", MAX_SOPS, nsops);
#endif
		eval = E2BIG;
		goto done;
	}

	if ((eval = copyin(uap->sops, &sops, nsops * sizeof(sops[0]))) != 0) {
#ifdef SEM_DEBUG
		kprintf("eval = %d from copyin(%08x, %08x, %u)\n", eval,
		    uap->sops, &sops, nsops * sizeof(sops[0]));
#endif
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
		long gen;

		semptr = NULL;

		for (i = 0; i < nsops; i++) {
			sopptr = &sops[i];

			if (sopptr->sem_num >= semaptr->ds.sem_nsems) {
				eval = EFBIG;
				goto done;
			}

			semptr = &semaptr->ds.sem_base[sopptr->sem_num];
			lwkt_getpooltoken(semptr);

#ifdef SEM_DEBUG
			kprintf("semop:  semaptr=%x, sem_base=%x, semptr=%x, "
				"sem[%d]=%d : op=%d, flag=%s\n",
			    semaptr, semaptr->ds.sem_base, semptr,
			    sopptr->sem_num, semptr->semval, sopptr->sem_op,
			    (sopptr->sem_flg & IPC_NOWAIT) ? "nowait" : "wait");
#endif

			if (sopptr->sem_op < 0) {
				if (semptr->semval + sopptr->sem_op < 0) {
#ifdef SEM_DEBUG
					kprintf("semop:  can't do it now\n");
#endif
					break;
				} else {
					semptr->semval += sopptr->sem_op;
					if (semptr->semval == 0 &&
					    semptr->semzcnt > 0) {
						wakeup(semptr);
					}
				}
				if (sopptr->sem_flg & SEM_UNDO)
					do_undos = 1;
			} else if (sopptr->sem_op == 0) {
				if (semptr->semval > 0) {
#ifdef SEM_DEBUG
					kprintf("semop:  not zero now\n");
#endif
					break;
				}
			} else {
				semptr->semval += sopptr->sem_op;
				if (sopptr->sem_flg & SEM_UNDO)
					do_undos = 1;
				if (semptr->semncnt > 0)
					wakeup(semptr);
			}
			lwkt_relpooltoken(semptr);
		}

		/*
		 * Did we get through the entire vector?
		 */
		if (i >= nsops)
			goto donex;

		/*
		 * No, protect the semaphore request which also flags that
		 * a wakeup is needed, then release semptr since we know
		 * another process is likely going to need to access it
		 * soon.
		 */
		if (sopptr->sem_op == 0)
			semptr->semzcnt++;
		else
			semptr->semncnt++;
		tsleep_interlock(semptr, PCATCH);
		lwkt_relpooltoken(semptr);

		/*
		 * Rollback the semaphores we had acquired.
		 */
#ifdef SEM_DEBUG
		kprintf("semop:  rollback 0 through %d\n", i-1);
#endif
		for (j = 0; j < i; j++) {
			xsemptr = &semaptr->ds.sem_base[sops[j].sem_num];
			lwkt_getpooltoken(xsemptr);
			xsemptr->semval -= sops[j].sem_op;
			if (xsemptr->semval == 0 && xsemptr->semzcnt > 0)
				wakeup(xsemptr);
			if (xsemptr->semval <= 0 && xsemptr->semncnt > 0)
				wakeup(xsemptr);
			lwkt_relpooltoken(xsemptr);
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
		 * Release semaptr->lk while sleeping, allowing other
		 * semops (like SETVAL, SETALL, etc), which require an
		 * exclusive lock and might wake us up.
		 *
		 * Reload and recheck the validity of semaptr on return.
		 * Note that semptr itself might have changed too, but
		 * we've already interlocked for semptr and that is what
		 * will be woken up if it wakes up the tsleep on a MP
		 * race.
		 *
		 * gen protects against destroy/re-create races where the
		 * creds match.
		 */
#ifdef SEM_DEBUG
		kprintf("semop:  good night!\n");
#endif
		gen = semaptr->gen;
		lockmgr(&semaptr->lk, LK_RELEASE);
		eval = tsleep(semptr, PCATCH | PINTERLOCKED, "semwait", hz);
		lockmgr(&semaptr->lk, LK_SHARED);
#ifdef SEM_DEBUG
		kprintf("semop:  good morning (eval=%d)!\n", eval);
#endif

		/* return code is checked below, after sem[nz]cnt-- */

		/*
		 * Make sure that the semaphore still exists
		 */
		if (semaptr->gen != gen ||
		    (semaptr->ds.sem_perm.mode & SEM_ALLOC) == 0 ||
		    semaptr->ds.sem_perm.seq != IPCID_TO_SEQ(uap->semid)) {
			eval = EIDRM;
			goto done;
		}

		/*
		 * The semaphore is still alive.  Readjust the count of
		 * waiting processes.
		 */
		semptr = &semaptr->ds.sem_base[sopptr->sem_num];
		lwkt_getpooltoken(semptr);
		if (sopptr->sem_op == 0)
			semptr->semzcnt--;
		else
			semptr->semncnt--;
		lwkt_relpooltoken(semptr);

		/*
		 * Is it really morning, or was our sleep interrupted?
		 * (Delayed check of tsleep() return code because we
		 * need to decrement sem[nz]cnt either way.)
		 */
		if (eval) {
			eval = EINTR;
			goto done;
		}
#ifdef SEM_DEBUG
		kprintf("semop:  good morning!\n");
#endif
		/* RETRY LOOP */
	}

donex:
	/*
	 * Process any SEM_UNDO requests.
	 */
	if (do_undos) {
		for (i = 0; i < nsops; i++) {
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
			eval = semundo_adjust(td->td_proc, semid,
					      sops[i].sem_num, -adjval);
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
				if (semundo_adjust(td->td_proc, semid,
					       sops[j].sem_num, adjval) != 0)
					panic("semop - can't undo undos");
			}

			for (j = 0; j < nsops; j++) {
				xsemptr = &semaptr->ds.sem_base[
							sops[j].sem_num];
				lwkt_getpooltoken(xsemptr);
				xsemptr->semval -= sops[j].sem_op;
				if (xsemptr->semval == 0 &&
				    xsemptr->semzcnt > 0)
					wakeup(xsemptr);
				if (xsemptr->semval <= 0 &&
				    xsemptr->semncnt > 0)
					wakeup(xsemptr);
				lwkt_relpooltoken(xsemptr);
			}

#ifdef SEM_DEBUG
			kprintf("eval = %d from semundo_adjust\n", eval);
#endif
			goto done;
		} /* loop through the sops */
	} /* if (do_undos) */

	/* We're definitely done - set the sempid's */
	for (i = 0; i < nsops; i++) {
		sopptr = &sops[i];
		semptr = &semaptr->ds.sem_base[sopptr->sem_num];
		lwkt_getpooltoken(semptr);
		semptr->sempid = td->td_proc->p_pid;
		lwkt_relpooltoken(semptr);
	}

	/* Do a wakeup if any semaphore was up'd. */
#ifdef SEM_DEBUG
	kprintf("semop:  done\n");
#endif
	uap->sysmsg_result = 0;
	eval = 0;
done:
	lockmgr(&semaptr->lk, LK_RELEASE);
	wakeup_end_delayed();
done2:
	return(eval);
}

/*
 * Go through the undo structures for this process and apply the adjustments to
 * semaphores.
 *
 * (p->p_token is held by the caller)
 */
void
semexit(struct proc *p)
{
	struct sem_undo *suptr;
	struct sem *semptr;

	/*
	 * We're getting a global token, don't do it if we couldn't
	 * possibly have any semaphores.
	 */
	if ((p->p_flags & P_SYSVSEM) == 0)
		return;
	suptr = p->p_sem_undo;
	KKASSERT(suptr != NULL);

	/*
	 * Disconnect suptr from the process and increment un_refs to
	 * prevent anyone else from being able to destroy the structure.
	 * Do not remove it from the linked list until after we are through
	 * scanning it as other semaphore calls might still effect it.
	 */
	lwkt_gettoken(&semu_token);
	p->p_sem_undo = NULL;
	p->p_flags &= ~P_SYSVSEM;
	suptr->un_proc = NULL;
	++suptr->un_refs;
	lwkt_reltoken(&semu_token);

	while (suptr->un_cnt) {
		struct semid_pool *semaptr;
		int semid;
		int semnum;
		int adjval;
		int ix;

		/*
		 * These values are stable because we hold p->p_token.
		 * However, they can get ripped out from under us when
		 * we block or obtain other tokens so we have to re-check.
		 */
		ix = suptr->un_cnt - 1;
		semid = suptr->un_ent[ix].un_id;
		semnum = suptr->un_ent[ix].un_num;
		adjval = suptr->un_ent[ix].un_adjval;

		semaptr = &sema[semid];

		/*
		 * Recheck after locking, then execute the undo
		 * operation.  semptr remains valid due to the
		 * semaptr->lk.
		 */
		lockmgr(&semaptr->lk, LK_SHARED);
		semptr = &semaptr->ds.sem_base[semnum];
		lwkt_getpooltoken(semptr);

		if (ix == suptr->un_cnt - 1 &&
		    semid == suptr->un_ent[ix].un_id &&
		    semnum == suptr->un_ent[ix].un_num &&
		    adjval == suptr->un_ent[ix].un_adjval) {
			/*
			 * Only do assertions when we aren't in a SMP race.
			 */
			if ((semaptr->ds.sem_perm.mode & SEM_ALLOC) == 0)
				panic("semexit - semid not allocated");
			if (semnum >= semaptr->ds.sem_nsems)
				panic("semexit - semnum out of range");
			--suptr->un_cnt;

			if (adjval < 0) {
				if (semptr->semval < -adjval)
					semptr->semval = 0;
				else
					semptr->semval += adjval;
			} else {
				semptr->semval += adjval;
			}
			wakeup(semptr);
		}
		lwkt_relpooltoken(semptr);
		lockmgr(&semaptr->lk, LK_RELEASE);
	}

	/*
	 * Final cleanup, remove from the list and deallocate on the
	 * last ref only.
	 */
	lwkt_gettoken(&semu_token);
	if (--suptr->un_refs == 0) {
		TAILQ_REMOVE(&semu_list, suptr, un_entry);
		KKASSERT(suptr->un_cnt == 0);
		kfree(suptr, M_SEM);
	}
	lwkt_reltoken(&semu_token);
}
