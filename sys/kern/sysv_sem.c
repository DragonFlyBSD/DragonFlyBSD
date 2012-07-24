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
static int semundo_adjust (struct proc *p, struct sem_undo **supptr, 
		int semid, int semnum, int adjval);
static void semundo_clear (int semid, int semnum);

/* XXX casting to (sy_call_t *) is bogus, as usual. */
static sy_call_t *semcalls[] = {
	(sy_call_t *)sys___semctl, (sy_call_t *)sys_semget,
	(sy_call_t *)sys_semop
};

static struct lwkt_token semu_token = LWKT_TOKEN_INITIALIZER(semu_token);
static int	semtot = 0;
static struct semid_ds *sema;	/* semaphore id pool */
static struct sem *sem;		/* semaphore pool */
static struct sem_undo *semu_list; /* list of active undo structures */
static int	*semu;		/* undo structure pool */

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
	struct	sem_undo *un_next;	/* ptr to next active undo structure */
	struct	proc *un_proc;		/* owner of this structure */
	short	un_cnt;			/* # of active entries */
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
#define SEMMNI	22		/* # of semaphore identifiers */
#endif
#ifndef SEMMNS
#define SEMMNS	341		/* # of semaphores in system */
#endif
#ifndef SEMUME
#define SEMUME	10		/* max # of undo entries per process */
#endif
#ifndef SEMMNU
#define SEMMNU	30		/* # of undo structures in system */
#endif

/* shouldn't need tuning */
#ifndef SEMMAP
#define SEMMAP	30		/* # of entries in semaphore map */
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
#define SEMUSZ	SEM_ALIGN(offsetof(struct sem_undo, un_ent[SEMUME]))

/*
 * Macro to find a particular sem_undo vector
 */
#define SEMU(ix)	((struct sem_undo *)(((intptr_t)semu) + (ix) * \
					     seminfo.semusz))

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
                SEMUSZ,         /* size in bytes of undo structure */
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

	sem = kmalloc(sizeof(struct sem) * seminfo.semmns, M_SEM, M_WAITOK);
	sema = kmalloc(sizeof(struct semid_ds) * seminfo.semmni, M_SEM, M_WAITOK);
	semu = kmalloc(seminfo.semmnu * seminfo.semusz, M_SEM, M_WAITOK);

	for (i = 0; i < seminfo.semmni; i++) {
		sema[i].sem_base = 0;
		sema[i].sem_perm.mode = 0;
	}
	for (i = 0; i < seminfo.semmnu; i++) {
		struct sem_undo *suptr = SEMU(i);
		suptr->un_proc = NULL;
	}
	semu_list = NULL;
}
SYSINIT(sysv_sem, SI_SUB_SYSV_SEM, SI_ORDER_FIRST, seminit, NULL)

/*
 * Entry point for all SEM calls
 *
 * semsys_args(int which, a2, a3, ...) (VARARGS)
 *
 * MPALMOSTSAFE
 */
int
sys_semsys(struct semsys_args *uap)
{
	struct thread *td = curthread;
	unsigned int which = (unsigned int)uap->which;
	int error;

	if (!jail_sysvipc_allowed && td->td_ucred->cr_prison != NULL)
		return (ENOSYS);

	if (which >= NELEM(semcalls))
		return (EINVAL);
	bcopy(&uap->a2, &uap->which,
	      sizeof(struct semsys_args) - offsetof(struct semsys_args, a2));
	error = (*semcalls[which])(uap);
	return (error);
}

/*
 * Allocate a new sem_undo structure for a process
 * (returns ptr to structure or NULL if no more room)
 *
 * semu_token is held by the caller.
 */
static struct sem_undo *
semu_alloc(struct proc *p)
{
	int i;
	struct sem_undo *suptr;
	struct sem_undo **supptr;
	int attempt;

	/*
	 * Try twice to allocate something.
	 * (we'll purge any empty structures after the first pass so
	 * two passes are always enough)
	 */
	for (attempt = 0; attempt < 2; attempt++) {
		/*
		 * Look for a free structure.
		 * Fill it in and return it if we find one.
		 */
		for (i = 0; i < seminfo.semmnu; i++) {
			suptr = SEMU(i);
			if (suptr->un_proc == NULL) {
				suptr->un_next = semu_list;
				semu_list = suptr;
				suptr->un_cnt = 0;
				suptr->un_proc = p;
				goto done;
			}
		}

		/*
		 * We didn't find a free one, if this is the first attempt
		 * then try to free some structures.
		 */

		if (attempt == 0) {
			/* All the structures are in use - try to free some */
			int did_something = 0;

			supptr = &semu_list;
			while ((suptr = *supptr) != NULL) {
				if (suptr->un_cnt == 0)  {
					suptr->un_proc = NULL;
					*supptr = suptr->un_next;
					did_something = 1;
				} else {
					supptr = &(suptr->un_next);
				}
			}

			/* If we didn't free anything then just give-up */
			if (!did_something) {
				suptr = NULL;
				goto done;
			}
		} else {
			/*
			 * The second pass failed even though we freed
			 * something after the first pass!
			 * This is IMPOSSIBLE!
			 */
			panic("semu_alloc - second attempt failed");
		}
	}
	suptr = NULL;
done:
	return (suptr);
}

/*
 * Adjust a particular entry for a particular proc
 */

static int
semundo_adjust(struct proc *p, struct sem_undo **supptr, int semid, int semnum,
	       int adjval)
{
	struct sem_undo *suptr;
	struct undo *sunptr;
	int i;
	int error = 0;

	/*
	 * Look for and remember the sem_undo if the caller doesn't
	 * provide it.
	 */
	lwkt_gettoken(&semu_token);
	lwkt_gettoken(&p->p_token);
	suptr = *supptr;
	if (suptr == NULL) {
		for (suptr = semu_list; suptr != NULL;
		    suptr = suptr->un_next) {
			if (suptr->un_proc == p) {
				*supptr = suptr;
				break;
			}
		}
		if (suptr == NULL) {
			if (adjval == 0)
				goto done;
			p->p_flags |= P_SYSVSEM;
			suptr = semu_alloc(p);
			if (suptr == NULL) {
				error = ENOSPC;
				goto done;
			}
			*supptr = suptr;
		}
	}

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
				suptr->un_ent[i] =
				    suptr->un_ent[suptr->un_cnt];
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
		sunptr->un_id = semid; sunptr->un_num = semnum;
	} else {
		error = EINVAL;
	}
done:
	lwkt_reltoken(&p->p_token);
	lwkt_reltoken(&semu_token);
	return (error);
}

static void
semundo_clear(int semid, int semnum)
{
	struct sem_undo *suptr;

	lwkt_gettoken(&semu_token);
	for (suptr = semu_list; suptr != NULL; suptr = suptr->un_next) {
		struct undo *sunptr = &suptr->un_ent[0];
		int i = 0;

		while (i < suptr->un_cnt) {
			if (sunptr->un_id == semid) {
				if (semnum == -1 || sunptr->un_num == semnum) {
					suptr->un_cnt--;
					if (i < suptr->un_cnt) {
						suptr->un_ent[i] =
						  suptr->un_ent[suptr->un_cnt];
						continue;
					}
				}
				if (semnum != -1)
					break;
			}
			i++, sunptr++;
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
	struct semid_ds *semaptr;
	struct semid_ds *semakptr;

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
		lwkt_getpooltoken(semakptr);
		if ((semakptr->sem_perm.mode & SEM_ALLOC) == 0) {
			eval = EINVAL;
			lwkt_relpooltoken(semakptr);
			break;
		}
		if ((eval = ipcperm(td->td_proc, &semakptr->sem_perm, IPC_R))) {
			lwkt_relpooltoken(semakptr);
			break;
		}
		bcopy(&semakptr, arg->buf, sizeof(struct semid_ds));
		rval = IXSEQ_TO_IPCID(semid, semakptr->sem_perm);
		lwkt_relpooltoken(semakptr);
		break;
	}
	
	semid = IPCID_TO_IX(semid);
	if (semid < 0 || semid >= seminfo.semmni) {
		return(EINVAL);
	}
	semaptr = &sema[semid];
	lwkt_getpooltoken(semaptr);

	if ((semaptr->sem_perm.mode & SEM_ALLOC) == 0 ||
	    semaptr->sem_perm.seq != IPCID_TO_SEQ(uap->semid)) {
		lwkt_relpooltoken(semaptr);
		return(EINVAL);
	}

	eval = 0;
	rval = 0;

	switch (cmd) {
	case IPC_RMID:
		if ((eval = ipcperm(td->td_proc, &semaptr->sem_perm, IPC_M)) != 0)
			break;
		semaptr->sem_perm.cuid = cred->cr_uid;
		semaptr->sem_perm.uid = cred->cr_uid;
		semtot -= semaptr->sem_nsems;
		for (i = semaptr->sem_base - sem; i < semtot; i++)
			sem[i] = sem[i + semaptr->sem_nsems];
		for (i = 0; i < seminfo.semmni; i++) {
			if ((sema[i].sem_perm.mode & SEM_ALLOC) &&
			    sema[i].sem_base > semaptr->sem_base)
				sema[i].sem_base -= semaptr->sem_nsems;
		}
		semaptr->sem_perm.mode = 0;
		semundo_clear(semid, -1);
		wakeup((caddr_t)semaptr);
		break;

	case IPC_SET:
		eval = ipcperm(td->td_proc, &semaptr->sem_perm, IPC_M);
		if (eval)
			break;
		if ((eval = copyin(arg, &real_arg, sizeof(real_arg))) != 0)
			break;
		if ((eval = copyin(real_arg.buf, (caddr_t)&sbuf,
				   sizeof(sbuf))) != 0) {
			break;
		}
		semaptr->sem_perm.uid = sbuf.sem_perm.uid;
		semaptr->sem_perm.gid = sbuf.sem_perm.gid;
		semaptr->sem_perm.mode = (semaptr->sem_perm.mode & ~0777) |
					 (sbuf.sem_perm.mode & 0777);
		semaptr->sem_ctime = time_second;
		break;

	case IPC_STAT:
		if ((eval = ipcperm(td->td_proc, &semaptr->sem_perm, IPC_R)))
			break;
		if ((eval = copyin(arg, &real_arg, sizeof(real_arg))) != 0)
			break;
		eval = copyout(semaptr, real_arg.buf, sizeof(struct semid_ds));
		break;

	case GETNCNT:
		eval = ipcperm(td->td_proc, &semaptr->sem_perm, IPC_R);
		if (eval)
			break;
		if (semnum < 0 || semnum >= semaptr->sem_nsems) {
			eval = EINVAL;
			break;
		}
		rval = semaptr->sem_base[semnum].semncnt;
		break;

	case GETPID:
		eval = ipcperm(td->td_proc, &semaptr->sem_perm, IPC_R);
		if (eval)
			break;
		if (semnum < 0 || semnum >= semaptr->sem_nsems) {
			eval = EINVAL;
			break;
		}
		rval = semaptr->sem_base[semnum].sempid;
		break;

	case GETVAL:
		eval = ipcperm(td->td_proc, &semaptr->sem_perm, IPC_R);
		if (eval)
			break;
		if (semnum < 0 || semnum >= semaptr->sem_nsems) {
			eval = EINVAL;
			break;
		}
		rval = semaptr->sem_base[semnum].semval;
		break;

	case GETALL:
		eval = ipcperm(td->td_proc, &semaptr->sem_perm, IPC_R);
		if (eval)
			break;
		if ((eval = copyin(arg, &real_arg, sizeof(real_arg))) != 0)
			break;
		for (i = 0; i < semaptr->sem_nsems; i++) {
			eval = copyout(&semaptr->sem_base[i].semval,
				       &real_arg.array[i],
				       sizeof(real_arg.array[0]));
			if (eval)
				break;
		}
		break;

	case GETZCNT:
		eval = ipcperm(td->td_proc, &semaptr->sem_perm, IPC_R);
		if (eval)
			break;
		if (semnum < 0 || semnum >= semaptr->sem_nsems) {
			eval = EINVAL;
			break;
		}
		rval = semaptr->sem_base[semnum].semzcnt;
		break;

	case SETVAL:
		eval = ipcperm(td->td_proc, &semaptr->sem_perm, IPC_W);
		if (eval)
			break;
		if (semnum < 0 || semnum >= semaptr->sem_nsems) {
			eval = EINVAL;
			break;
		}
		if ((eval = copyin(arg, &real_arg, sizeof(real_arg))) != 0)
			break;
		semaptr->sem_base[semnum].semval = real_arg.val;
		semundo_clear(semid, semnum);
		wakeup((caddr_t)semaptr);
		break;

	case SETALL:
		eval = ipcperm(td->td_proc, &semaptr->sem_perm, IPC_W);
		if (eval)
			break;
		if ((eval = copyin(arg, &real_arg, sizeof(real_arg))) != 0)
			break;
		for (i = 0; i < semaptr->sem_nsems; i++) {
			eval = copyin(&real_arg.array[i],
				      (caddr_t)&semaptr->sem_base[i].semval,
				      sizeof(real_arg.array[0]));
			if (eval != 0)
				break;
		}
		semundo_clear(semid, -1);
		wakeup((caddr_t)semaptr);
		break;

	default:
		eval = EINVAL;
		break;
	}
	lwkt_relpooltoken(semaptr);

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
			if ((sema[semid].sem_perm.mode & SEM_ALLOC) == 0 ||
			    sema[semid].sem_perm.key != key) {
				continue;
			}
			lwkt_getpooltoken(&sema[semid]);
			if ((sema[semid].sem_perm.mode & SEM_ALLOC) == 0 ||
			    sema[semid].sem_perm.key != key) {
				lwkt_relpooltoken(&sema[semid]);
				continue;
			}
			break;
		}
		if (semid < seminfo.semmni) {
#ifdef SEM_DEBUG
			kprintf("found public key\n");
#endif
			if ((eval = ipcperm(td->td_proc,
					    &sema[semid].sem_perm,
					    semflg & 0700))) {
				lwkt_relpooltoken(&sema[semid]);
				goto done;
			}
			if (nsems > 0 && sema[semid].sem_nsems < nsems) {
#ifdef SEM_DEBUG
				kprintf("too small\n");
#endif
				eval = EINVAL;
				lwkt_relpooltoken(&sema[semid]);
				goto done;
			}
			if ((semflg & IPC_CREAT) && (semflg & IPC_EXCL)) {
#ifdef SEM_DEBUG
				kprintf("not exclusive\n");
#endif
				eval = EEXIST;
				lwkt_relpooltoken(&sema[semid]);
				goto done;
			}
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
		if (nsems > seminfo.semmns - semtot) {
#ifdef SEM_DEBUG
			kprintf("not enough semaphores left "
				"(need %d, got %d)\n",
				nsems, seminfo.semmns - semtot);
#endif
			eval = ENOSPC;
			goto done;
		}
		for (semid = 0; semid < seminfo.semmni; semid++) {
			if (sema[semid].sem_perm.mode & SEM_ALLOC)
				continue;
			lwkt_getpooltoken(&sema[semid]);
			if (sema[semid].sem_perm.mode & SEM_ALLOC) {
				lwkt_relpooltoken(&sema[semid]);
				continue;
			}
			break;
		}
		if (semid == seminfo.semmni) {
#ifdef SEM_DEBUG
			kprintf("no more semid_ds's available\n");
#endif
			eval = ENOSPC;
			goto done;
		}
#ifdef SEM_DEBUG
		kprintf("semid %d is available\n", semid);
#endif
		sema[semid].sem_perm.key = key;
		sema[semid].sem_perm.cuid = cred->cr_uid;
		sema[semid].sem_perm.uid = cred->cr_uid;
		sema[semid].sem_perm.cgid = cred->cr_gid;
		sema[semid].sem_perm.gid = cred->cr_gid;
		sema[semid].sem_perm.mode = (semflg & 0777) | SEM_ALLOC;
		sema[semid].sem_perm.seq =
		    (sema[semid].sem_perm.seq + 1) & 0x7fff;
		sema[semid].sem_nsems = nsems;
		sema[semid].sem_otime = 0;
		sema[semid].sem_ctime = time_second;
		sema[semid].sem_base = &sem[semtot];
		semtot += nsems;
		bzero(sema[semid].sem_base,
		    sizeof(sema[semid].sem_base[0])*nsems);
#ifdef SEM_DEBUG
		kprintf("sembase = 0x%x, next = 0x%x\n",
			sema[semid].sem_base, &sem[semtot]);
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
			IXSEQ_TO_IPCID(semid, sema[semid].sem_perm);
		lwkt_relpooltoken(&sema[semid]);
	}
	return(eval);
}

/*
 * MPALMOSTSAFE
 */
int
sys_semop(struct semop_args *uap)
{
	struct thread *td = curthread;
	int semid = uap->semid;
	u_int nsops = uap->nsops;
	struct sembuf sops[MAX_SOPS];
	struct semid_ds *semaptr;
	struct sembuf *sopptr;
	struct sem *semptr;
	struct sem_undo *suptr = NULL;
	int i, j, eval;
	int do_wakeup, do_undos;

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
	semaptr = &sema[semid];
	lwkt_getpooltoken(semaptr);
	if ((semaptr->sem_perm.mode & SEM_ALLOC) == 0) {
		eval = EINVAL;
		goto done;
	}
	if (semaptr->sem_perm.seq != IPCID_TO_SEQ(uap->semid)) {
		eval = EINVAL;
		goto done;
	}

	if ((eval = ipcperm(td->td_proc, &semaptr->sem_perm, IPC_W))) {
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
		do_wakeup = 0;

		for (i = 0; i < nsops; i++) {
			sopptr = &sops[i];

			if (sopptr->sem_num >= semaptr->sem_nsems) {
				eval = EFBIG;
				goto done;
			}

			semptr = &semaptr->sem_base[sopptr->sem_num];

#ifdef SEM_DEBUG
			kprintf("semop:  semaptr=%x, sem_base=%x, semptr=%x, sem[%d]=%d : op=%d, flag=%s\n",
			    semaptr, semaptr->sem_base, semptr,
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
					    semptr->semzcnt > 0)
						do_wakeup = 1;
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
				if (semptr->semncnt > 0)
					do_wakeup = 1;
				semptr->semval += sopptr->sem_op;
				if (sopptr->sem_flg & SEM_UNDO)
					do_undos = 1;
			}
		}

		/*
		 * Did we get through the entire vector?
		 */
		if (i >= nsops)
			goto donex;

		/*
		 * No ... rollback anything that we've already done
		 */
#ifdef SEM_DEBUG
		kprintf("semop:  rollback 0 through %d\n", i-1);
#endif
		for (j = 0; j < i; j++)
			semaptr->sem_base[sops[j].sem_num].semval -=
			    sops[j].sem_op;

		/*
		 * If the request that we couldn't satisfy has the
		 * NOWAIT flag set then return with EAGAIN.
		 */
		if (sopptr->sem_flg & IPC_NOWAIT) {
			eval = EAGAIN;
			goto done;
		}

		if (sopptr->sem_op == 0)
			semptr->semzcnt++;
		else
			semptr->semncnt++;

#ifdef SEM_DEBUG
		kprintf("semop:  good night!\n");
#endif
		eval = tsleep((caddr_t)semaptr, PCATCH, "semwait", 0);
#ifdef SEM_DEBUG
		kprintf("semop:  good morning (eval=%d)!\n", eval);
#endif

		suptr = NULL;	/* sem_undo may have been reallocated */

		/* return code is checked below, after sem[nz]cnt-- */

		/*
		 * Make sure that the semaphore still exists
		 */
		if ((semaptr->sem_perm.mode & SEM_ALLOC) == 0 ||
		    semaptr->sem_perm.seq != IPCID_TO_SEQ(uap->semid)) {
			eval = EIDRM;
			goto done;
		}

		/*
		 * The semaphore is still alive.  Readjust the count of
		 * waiting processes.
		 */
		if (sopptr->sem_op == 0)
			semptr->semzcnt--;
		else
			semptr->semncnt--;

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
			eval = semundo_adjust(td->td_proc, &suptr, semid,
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
				if (semundo_adjust(td->td_proc, &suptr, semid,
					       sops[j].sem_num, adjval) != 0)
					panic("semop - can't undo undos");
			}

			for (j = 0; j < nsops; j++)
				semaptr->sem_base[sops[j].sem_num].semval -=
				    sops[j].sem_op;

#ifdef SEM_DEBUG
			kprintf("eval = %d from semundo_adjust\n", eval);
#endif
			goto done;
		} /* loop through the sops */
	} /* if (do_undos) */

	/* We're definitely done - set the sempid's */
	for (i = 0; i < nsops; i++) {
		sopptr = &sops[i];
		semptr = &semaptr->sem_base[sopptr->sem_num];
		semptr->sempid = td->td_proc->p_pid;
	}

	/* Do a wakeup if any semaphore was up'd. */
	if (do_wakeup) {
#ifdef SEM_DEBUG
		kprintf("semop:  doing wakeup\n");
#endif
		wakeup((caddr_t)semaptr);
#ifdef SEM_DEBUG
		kprintf("semop:  back from wakeup\n");
#endif
	}
#ifdef SEM_DEBUG
	kprintf("semop:  done\n");
#endif
	uap->sysmsg_result = 0;
	eval = 0;
done:
	lwkt_relpooltoken(semaptr);
done2:
	return(eval);
}

/*
 * Go through the undo structures for this process and apply the adjustments to
 * semaphores.
 */
void
semexit(struct proc *p)
{
	struct sem_undo *suptr;
	struct sem_undo **supptr;
	int did_something;

	did_something = 0;

	/*
	 * We're getting a global token, don't do it if we couldn't
	 * possibly have any semaphores.
	 */
	if ((p->p_flags & P_SYSVSEM) == 0)
		return;

	/*
	 * Go through the chain of undo vectors looking for one
	 * associated with this process.  De-link it from the
	 * list right now, while we have the token, but do not
	 * clear un_proc until we finish cleaning up the relationship.
	 */
	lwkt_gettoken(&semu_token);
	for (supptr = &semu_list; (suptr = *supptr) != NULL;
	     supptr = &suptr->un_next) {
		if (suptr->un_proc == p) {
			*supptr = suptr->un_next;
			break;
		}
	}
	p->p_flags &= ~P_SYSVSEM;
	lwkt_reltoken(&semu_token);

	if (suptr == NULL)
		return;

#ifdef SEM_DEBUG
	kprintf("proc @%08x has undo structure with %d entries\n", p,
		suptr->un_cnt);
#endif

	/*
	 * If there are any active undo elements then process them.
	 */
	if (suptr->un_cnt > 0) {
		int ix;

		for (ix = 0; ix < suptr->un_cnt; ix++) {
			int semid = suptr->un_ent[ix].un_id;
			int semnum = suptr->un_ent[ix].un_num;
			int adjval = suptr->un_ent[ix].un_adjval;
			struct semid_ds *semaptr;

			semaptr = &sema[semid];
			if ((semaptr->sem_perm.mode & SEM_ALLOC) == 0)
				panic("semexit - semid not allocated");
			if (semnum >= semaptr->sem_nsems)
				panic("semexit - semnum out of range");

#ifdef SEM_DEBUG
			kprintf("semexit:  %08x id=%d num=%d(adj=%d) ; sem=%d\n",
			    suptr->un_proc, suptr->un_ent[ix].un_id,
			    suptr->un_ent[ix].un_num,
			    suptr->un_ent[ix].un_adjval,
			    semaptr->sem_base[semnum].semval);
#endif

			if (adjval < 0) {
				if (semaptr->sem_base[semnum].semval < -adjval)
					semaptr->sem_base[semnum].semval = 0;
				else
					semaptr->sem_base[semnum].semval +=
					    adjval;
			} else
				semaptr->sem_base[semnum].semval += adjval;

			wakeup((caddr_t)semaptr);
#ifdef SEM_DEBUG
			kprintf("semexit:  back from wakeup\n");
#endif
		}
	}

	/*
	 * Deallocate the undo vector.
	 */
	cpu_sfence();
	suptr->un_proc = NULL;
#ifdef SEM_DEBUG
	kprintf("removing vector\n");
#endif
}
