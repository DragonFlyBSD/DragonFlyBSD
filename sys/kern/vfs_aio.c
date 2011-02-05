/*
 * Copyright (c) 1997 John S. Dyson.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. John S. Dyson's name may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * DISCLAIMER:  This code isn't warranted to do anything useful.  Anything
 * bad that happens because of using this software isn't the responsibility
 * of the author.  This software is distributed AS-IS.
 *
 * $FreeBSD: src/sys/kern/vfs_aio.c,v 1.70.2.28 2003/05/29 06:15:35 alc Exp $
 * $DragonFly: src/sys/kern/vfs_aio.c,v 1.42 2007/07/20 17:21:52 dillon Exp $
 */

/*
 * This file contains support for the POSIX 1003.1B AIO/LIO facility.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/buf.h>
#include <sys/sysproto.h>
#include <sys/filedesc.h>
#include <sys/kernel.h>
#include <sys/fcntl.h>
#include <sys/file.h>
#include <sys/lock.h>
#include <sys/unistd.h>
#include <sys/proc.h>
#include <sys/resourcevar.h>
#include <sys/signalvar.h>
#include <sys/protosw.h>
#include <sys/socketvar.h>
#include <sys/sysctl.h>
#include <sys/vnode.h>
#include <sys/conf.h>
#include <sys/event.h>
#include <sys/objcache.h>

#include <vm/vm.h>
#include <vm/vm_extern.h>
#include <vm/pmap.h>
#include <vm/vm_map.h>
#include <sys/aio.h>

#include <sys/file2.h>
#include <sys/buf2.h>
#include <sys/sysref2.h>
#include <sys/thread2.h>
#include <sys/mplock2.h>

#include <machine/limits.h>
#include "opt_vfs_aio.h"

#ifdef VFS_AIO

/*
 * Counter for allocating reference ids to new jobs.  Wrapped to 1 on
 * overflow.
 */
static	long jobrefid;

#define JOBST_NULL		0x0
#define JOBST_JOBQGLOBAL	0x2
#define JOBST_JOBRUNNING	0x3
#define JOBST_JOBFINISHED	0x4
#define	JOBST_JOBQBUF		0x5
#define	JOBST_JOBBFINISHED	0x6

#ifndef MAX_AIO_PER_PROC
#define MAX_AIO_PER_PROC	32
#endif

#ifndef MAX_AIO_QUEUE_PER_PROC
#define MAX_AIO_QUEUE_PER_PROC	256 /* Bigger than AIO_LISTIO_MAX */
#endif

#ifndef MAX_AIO_PROCS
#define MAX_AIO_PROCS		32
#endif

#ifndef MAX_AIO_QUEUE
#define	MAX_AIO_QUEUE		1024 /* Bigger than AIO_LISTIO_MAX */
#endif

#ifndef TARGET_AIO_PROCS
#define TARGET_AIO_PROCS	4
#endif

#ifndef MAX_BUF_AIO
#define MAX_BUF_AIO		16
#endif

#ifndef AIOD_TIMEOUT_DEFAULT
#define	AIOD_TIMEOUT_DEFAULT	(10 * hz)
#endif

#ifndef AIOD_LIFETIME_DEFAULT
#define AIOD_LIFETIME_DEFAULT	(30 * hz)
#endif

SYSCTL_NODE(_vfs, OID_AUTO, aio, CTLFLAG_RW, 0, "Async IO management");

static int max_aio_procs = MAX_AIO_PROCS;
SYSCTL_INT(_vfs_aio, OID_AUTO, max_aio_procs,
	CTLFLAG_RW, &max_aio_procs, 0,
	"Maximum number of kernel threads to use for handling async IO");

static int num_aio_procs = 0;
SYSCTL_INT(_vfs_aio, OID_AUTO, num_aio_procs,
	CTLFLAG_RD, &num_aio_procs, 0,
	"Number of presently active kernel threads for async IO");

/*
 * The code will adjust the actual number of AIO processes towards this
 * number when it gets a chance.
 */
static int target_aio_procs = TARGET_AIO_PROCS;
SYSCTL_INT(_vfs_aio, OID_AUTO, target_aio_procs, CTLFLAG_RW, &target_aio_procs,
	0, "Preferred number of ready kernel threads for async IO");

static int max_queue_count = MAX_AIO_QUEUE;
SYSCTL_INT(_vfs_aio, OID_AUTO, max_aio_queue, CTLFLAG_RW, &max_queue_count, 0,
    "Maximum number of aio requests to queue, globally");

static int num_queue_count = 0;
SYSCTL_INT(_vfs_aio, OID_AUTO, num_queue_count, CTLFLAG_RD, &num_queue_count, 0,
    "Number of queued aio requests");

static int num_buf_aio = 0;
SYSCTL_INT(_vfs_aio, OID_AUTO, num_buf_aio, CTLFLAG_RD, &num_buf_aio, 0,
    "Number of aio requests presently handled by the buf subsystem");

/* Number of async I/O thread in the process of being started */
/* XXX This should be local to _aio_aqueue() */
static int num_aio_resv_start = 0;

static int aiod_timeout;
SYSCTL_INT(_vfs_aio, OID_AUTO, aiod_timeout, CTLFLAG_RW, &aiod_timeout, 0,
    "Timeout value for synchronous aio operations");

static int aiod_lifetime;
SYSCTL_INT(_vfs_aio, OID_AUTO, aiod_lifetime, CTLFLAG_RW, &aiod_lifetime, 0,
    "Maximum lifetime for idle aiod");

static int max_aio_per_proc = MAX_AIO_PER_PROC;
SYSCTL_INT(_vfs_aio, OID_AUTO, max_aio_per_proc, CTLFLAG_RW, &max_aio_per_proc,
    0, "Maximum active aio requests per process (stored in the process)");

static int max_aio_queue_per_proc = MAX_AIO_QUEUE_PER_PROC;
SYSCTL_INT(_vfs_aio, OID_AUTO, max_aio_queue_per_proc, CTLFLAG_RW,
    &max_aio_queue_per_proc, 0,
    "Maximum queued aio requests per process (stored in the process)");

static int max_buf_aio = MAX_BUF_AIO;
SYSCTL_INT(_vfs_aio, OID_AUTO, max_buf_aio, CTLFLAG_RW, &max_buf_aio, 0,
    "Maximum buf aio requests per process (stored in the process)");

/*
 * AIO process info
 */
#define AIOP_FREE	0x1			/* proc on free queue */
#define AIOP_SCHED	0x2			/* proc explicitly scheduled */

struct aioproclist {
	int aioprocflags;			/* AIO proc flags */
	TAILQ_ENTRY(aioproclist) list;		/* List of processes */
	struct proc *aioproc;			/* The AIO thread */
};

/*
 * data-structure for lio signal management
 */
struct aio_liojob {
	int	lioj_flags;
	int	lioj_buffer_count;
	int	lioj_buffer_finished_count;
	int	lioj_queue_count;
	int	lioj_queue_finished_count;
	struct	sigevent lioj_signal;	/* signal on all I/O done */
	TAILQ_ENTRY(aio_liojob) lioj_list;
	struct	kaioinfo *lioj_ki;
};
#define	LIOJ_SIGNAL		0x1	/* signal on all done (lio) */
#define	LIOJ_SIGNAL_POSTED	0x2	/* signal has been posted */

/*
 * per process aio data structure
 */
struct kaioinfo {
	int	kaio_flags;		/* per process kaio flags */
	int	kaio_maxactive_count;	/* maximum number of AIOs */
	int	kaio_active_count;	/* number of currently used AIOs */
	int	kaio_qallowed_count;	/* maxiumu size of AIO queue */
	int	kaio_queue_count;	/* size of AIO queue */
	int	kaio_ballowed_count;	/* maximum number of buffers */
	int	kaio_queue_finished_count; /* number of daemon jobs finished */
	int	kaio_buffer_count;	/* number of physio buffers */
	int	kaio_buffer_finished_count; /* count of I/O done */
	struct 	proc *kaio_p;		/* process that uses this kaio block */
	TAILQ_HEAD(,aio_liojob) kaio_liojoblist; /* list of lio jobs */
	TAILQ_HEAD(,aiocblist) kaio_jobqueue;	/* job queue for process */
	TAILQ_HEAD(,aiocblist) kaio_jobdone;	/* done queue for process */
	TAILQ_HEAD(,aiocblist) kaio_bufqueue;	/* buffer job queue for process */
	TAILQ_HEAD(,aiocblist) kaio_bufdone;	/* buffer done queue for process */
	TAILQ_HEAD(,aiocblist) kaio_sockqueue;	/* queue for aios waiting on sockets */
};

#define KAIO_RUNDOWN	0x1	/* process is being run down */
#define KAIO_WAKEUP	0x2	/* wakeup process when there is a significant event */

static TAILQ_HEAD(,aioproclist) aio_freeproc, aio_activeproc;
static TAILQ_HEAD(,aiocblist) aio_jobs;			/* Async job list */
static TAILQ_HEAD(,aiocblist) aio_bufjobs;		/* Phys I/O job list */
static TAILQ_HEAD(,aiocblist) aio_freejobs;		/* Pool of free jobs */

static void	aio_init_aioinfo(struct proc *p);
static void	aio_onceonly(void *);
static int	aio_free_entry(struct aiocblist *aiocbe);
static void	aio_process(struct aiocblist *aiocbe);
static int	aio_newproc(void);
static int	aio_aqueue(struct aiocb *job, int type);
static void	aio_physwakeup(struct bio *bio);
static int	aio_fphysio(struct aiocblist *aiocbe);
static int	aio_qphysio(struct proc *p, struct aiocblist *iocb);
static void	aio_daemon(void *uproc, struct trapframe *frame);
static void	process_signal(void *aioj);

SYSINIT(aio, SI_SUB_VFS, SI_ORDER_ANY, aio_onceonly, NULL);

/*
 * Zones for:
 * 	kaio	Per process async io info
 *	aiop	async io thread data
 *	aiocb	async io jobs
 *	aiol	list io job pointer - internal to aio_suspend XXX
 *	aiolio	list io jobs
 */
static struct objcache *kaio_oc, *aiop_oc, *aiocb_oc, *aiol_oc, *aiolio_oc;

static MALLOC_DEFINE(M_AIO, "AIO", "AIO");
static MALLOC_DEFINE(M_AIOP, "AIO proc", "AIO process");
static MALLOC_DEFINE(M_AIOCB, "AIO cb", "AIO cb");
static MALLOC_DEFINE(M_AIOL, "AIO list io", "AIO list io");
static MALLOC_DEFINE(M_AIOLIO, "AIO list io job", "AIO list io job");

/*
 * Startup initialization
 */
static void
aio_onceonly(void *na)
{
	TAILQ_INIT(&aio_freeproc);
	TAILQ_INIT(&aio_activeproc);
	TAILQ_INIT(&aio_jobs);
	TAILQ_INIT(&aio_bufjobs);
	TAILQ_INIT(&aio_freejobs);
	kaio_oc = objcache_create_simple(M_AIO, sizeof(struct kaioinfo));
	aiop_oc = objcache_create_simple(M_AIOP, sizeof(struct aioproclist));
	aiocb_oc = objcache_create_simple(M_AIOCB, sizeof(struct aiocblist));
	aiol_oc = objcache_create_simple(M_AIOL, AIO_LISTIO_MAX*sizeof(intptr_t));
	aiolio_oc = objcache_create_simple(M_AIOLIO, sizeof(struct aio_liojob));
	aiod_timeout = AIOD_TIMEOUT_DEFAULT;
	aiod_lifetime = AIOD_LIFETIME_DEFAULT;
	jobrefid = 1;
}

/*
 * Init the per-process aioinfo structure.  The aioinfo limits are set
 * per-process for user limit (resource) management.
 */
static void
aio_init_aioinfo(struct proc *p)
{
	struct kaioinfo *ki;
	if (p->p_aioinfo == NULL) {
		ki = objcache_get(kaio_oc, M_WAITOK);
		p->p_aioinfo = ki;
		ki->kaio_flags = 0;
		ki->kaio_maxactive_count = max_aio_per_proc;
		ki->kaio_active_count = 0;
		ki->kaio_qallowed_count = max_aio_queue_per_proc;
		ki->kaio_queue_count = 0;
		ki->kaio_ballowed_count = max_buf_aio;
		ki->kaio_buffer_count = 0;
		ki->kaio_buffer_finished_count = 0;
		ki->kaio_p = p;
		TAILQ_INIT(&ki->kaio_jobdone);
		TAILQ_INIT(&ki->kaio_jobqueue);
		TAILQ_INIT(&ki->kaio_bufdone);
		TAILQ_INIT(&ki->kaio_bufqueue);
		TAILQ_INIT(&ki->kaio_liojoblist);
		TAILQ_INIT(&ki->kaio_sockqueue);
	}
	
	while (num_aio_procs < target_aio_procs)
		aio_newproc();
}

/*
 * Free a job entry.  Wait for completion if it is currently active, but don't
 * delay forever.  If we delay, we return a flag that says that we have to
 * restart the queue scan.
 */
static int
aio_free_entry(struct aiocblist *aiocbe)
{
	struct kaioinfo *ki;
	struct aio_liojob *lj;
	struct proc *p;
	int error;

	if (aiocbe->jobstate == JOBST_NULL)
		panic("aio_free_entry: freeing already free job");

	p = aiocbe->userproc;
	ki = p->p_aioinfo;
	lj = aiocbe->lio;
	if (ki == NULL)
		panic("aio_free_entry: missing p->p_aioinfo");

	while (aiocbe->jobstate == JOBST_JOBRUNNING) {
		aiocbe->jobflags |= AIOCBLIST_RUNDOWN;
		tsleep(aiocbe, 0, "jobwai", 0);
	}
	if (aiocbe->bp == NULL) {
		if (ki->kaio_queue_count <= 0)
			panic("aio_free_entry: process queue size <= 0");
		if (num_queue_count <= 0)
			panic("aio_free_entry: system wide queue size <= 0");
	
		if (lj) {
			lj->lioj_queue_count--;
			if (aiocbe->jobflags & AIOCBLIST_DONE)
				lj->lioj_queue_finished_count--;
		}
		ki->kaio_queue_count--;
		if (aiocbe->jobflags & AIOCBLIST_DONE)
			ki->kaio_queue_finished_count--;
		num_queue_count--;
	} else {
		if (lj) {
			lj->lioj_buffer_count--;
			if (aiocbe->jobflags & AIOCBLIST_DONE)
				lj->lioj_buffer_finished_count--;
		}
		if (aiocbe->jobflags & AIOCBLIST_DONE)
			ki->kaio_buffer_finished_count--;
		ki->kaio_buffer_count--;
		num_buf_aio--;
	}

	/* aiocbe is going away, we need to destroy any knotes */
	/* XXX lwp knote wants a thread, but only cares about the process */
	knote_empty(&aiocbe->klist);

	if ((ki->kaio_flags & KAIO_WAKEUP) || ((ki->kaio_flags & KAIO_RUNDOWN)
	    && ((ki->kaio_buffer_count == 0) && (ki->kaio_queue_count == 0)))) {
		ki->kaio_flags &= ~KAIO_WAKEUP;
		wakeup(p);
	}

	if (aiocbe->jobstate == JOBST_JOBQBUF) {
		if ((error = aio_fphysio(aiocbe)) != 0)
			return error;
		if (aiocbe->jobstate != JOBST_JOBBFINISHED)
			panic("aio_free_entry: invalid physio finish-up state");
		crit_enter();
		TAILQ_REMOVE(&ki->kaio_bufdone, aiocbe, plist);
		crit_exit();
	} else if (aiocbe->jobstate == JOBST_JOBQGLOBAL) {
		crit_enter();
		TAILQ_REMOVE(&aio_jobs, aiocbe, list);
		TAILQ_REMOVE(&ki->kaio_jobqueue, aiocbe, plist);
		crit_exit();
	} else if (aiocbe->jobstate == JOBST_JOBFINISHED)
		TAILQ_REMOVE(&ki->kaio_jobdone, aiocbe, plist);
	else if (aiocbe->jobstate == JOBST_JOBBFINISHED) {
		crit_enter();
		TAILQ_REMOVE(&ki->kaio_bufdone, aiocbe, plist);
		crit_exit();
		if (aiocbe->bp) {
			vunmapbuf(aiocbe->bp);
			relpbuf(aiocbe->bp, NULL);
			aiocbe->bp = NULL;
		}
	}
	if (lj && (lj->lioj_buffer_count == 0) && (lj->lioj_queue_count == 0)) {
		TAILQ_REMOVE(&ki->kaio_liojoblist, lj, lioj_list);
		objcache_put(aiolio_oc, lj);
	}
	aiocbe->jobstate = JOBST_NULL;
	callout_stop(&aiocbe->timeout);
	fdrop(aiocbe->fd_file);
	TAILQ_INSERT_HEAD(&aio_freejobs, aiocbe, list);
	return 0;
}
#endif /* VFS_AIO */

/*
 * Rundown the jobs for a given process.  
 */
void
aio_proc_rundown(struct proc *p)
{
#ifndef VFS_AIO
	return;
#else
	struct kaioinfo *ki;
	struct aio_liojob *lj, *ljn;
	struct aiocblist *aiocbe, *aiocbn;
	struct file *fp;
	struct socket *so;

	ki = p->p_aioinfo;
	if (ki == NULL)
		return;

	ki->kaio_flags |= LIOJ_SIGNAL_POSTED;
	while ((ki->kaio_active_count > 0) || (ki->kaio_buffer_count >
	    ki->kaio_buffer_finished_count)) {
		ki->kaio_flags |= KAIO_RUNDOWN;
		if (tsleep(p, 0, "kaiowt", aiod_timeout))
			break;
	}

	/*
	 * Move any aio ops that are waiting on socket I/O to the normal job
	 * queues so they are cleaned up with any others.
	 */
	crit_enter();
	for (aiocbe = TAILQ_FIRST(&ki->kaio_sockqueue); aiocbe; aiocbe =
	    aiocbn) {
		aiocbn = TAILQ_NEXT(aiocbe, plist);
		fp = aiocbe->fd_file;
		if (fp != NULL) {
			so = (struct socket *)fp->f_data;
			TAILQ_REMOVE(&so->so_aiojobq, aiocbe, list);
			if (TAILQ_EMPTY(&so->so_aiojobq)) {
				atomic_clear_int(&so->so_snd.ssb_flags,
						 SSB_AIO);
				atomic_clear_int(&so->so_rcv.ssb_flags,
						 SSB_AIO);
			}
		}
		TAILQ_REMOVE(&ki->kaio_sockqueue, aiocbe, plist);
		TAILQ_INSERT_HEAD(&aio_jobs, aiocbe, list);
		TAILQ_INSERT_HEAD(&ki->kaio_jobqueue, aiocbe, plist);
	}
	crit_exit();

restart1:
	for (aiocbe = TAILQ_FIRST(&ki->kaio_jobdone); aiocbe; aiocbe = aiocbn) {
		aiocbn = TAILQ_NEXT(aiocbe, plist);
		if (aio_free_entry(aiocbe))
			goto restart1;
	}

restart2:
	for (aiocbe = TAILQ_FIRST(&ki->kaio_jobqueue); aiocbe; aiocbe =
	    aiocbn) {
		aiocbn = TAILQ_NEXT(aiocbe, plist);
		if (aio_free_entry(aiocbe))
			goto restart2;
	}

restart3:
	crit_enter();
	while (TAILQ_FIRST(&ki->kaio_bufqueue)) {
		ki->kaio_flags |= KAIO_WAKEUP;
		tsleep(p, 0, "aioprn", 0);
		crit_exit();
		goto restart3;
	}
	crit_exit();

restart4:
	crit_enter();
	for (aiocbe = TAILQ_FIRST(&ki->kaio_bufdone); aiocbe; aiocbe = aiocbn) {
		aiocbn = TAILQ_NEXT(aiocbe, plist);
		if (aio_free_entry(aiocbe)) {
			crit_exit();
			goto restart4;
		}
	}
	crit_exit();

        /*
         * If we've slept, jobs might have moved from one queue to another.
         * Retry rundown if we didn't manage to empty the queues.
         */
        if (TAILQ_FIRST(&ki->kaio_jobdone) != NULL ||
	    TAILQ_FIRST(&ki->kaio_jobqueue) != NULL ||
	    TAILQ_FIRST(&ki->kaio_bufqueue) != NULL ||
	    TAILQ_FIRST(&ki->kaio_bufdone) != NULL)
		goto restart1;

	for (lj = TAILQ_FIRST(&ki->kaio_liojoblist); lj; lj = ljn) {
		ljn = TAILQ_NEXT(lj, lioj_list);
		if ((lj->lioj_buffer_count == 0) && (lj->lioj_queue_count ==
		    0)) {
			TAILQ_REMOVE(&ki->kaio_liojoblist, lj, lioj_list);
			objcache_put(aiolio_oc, lj);
		} else {
#ifdef DIAGNOSTIC
			kprintf("LIO job not cleaned up: B:%d, BF:%d, Q:%d, "
			    "QF:%d\n", lj->lioj_buffer_count,
			    lj->lioj_buffer_finished_count,
			    lj->lioj_queue_count,
			    lj->lioj_queue_finished_count);
#endif
		}
	}

	objcache_put(kaio_oc, ki);
	p->p_aioinfo = NULL;
#endif /* VFS_AIO */
}

#ifdef VFS_AIO
/*
 * Select a job to run (called by an AIO daemon).
 */
static struct aiocblist *
aio_selectjob(struct aioproclist *aiop)
{
	struct aiocblist *aiocbe;
	struct kaioinfo *ki;
	struct proc *userp;

	crit_enter();
	for (aiocbe = TAILQ_FIRST(&aio_jobs); aiocbe; aiocbe =
	    TAILQ_NEXT(aiocbe, list)) {
		userp = aiocbe->userproc;
		ki = userp->p_aioinfo;

		if (ki->kaio_active_count < ki->kaio_maxactive_count) {
			TAILQ_REMOVE(&aio_jobs, aiocbe, list);
			crit_exit();
			return aiocbe;
		}
	}
	crit_exit();

	return NULL;
}

/*
 * The AIO processing activity.  This is the code that does the I/O request for
 * the non-physio version of the operations.  The normal vn operations are used,
 * and this code should work in all instances for every type of file, including
 * pipes, sockets, fifos, and regular files.
 */
static void
aio_process(struct aiocblist *aiocbe)
{
	struct thread *mytd;
	struct aiocb *cb;
	struct file *fp;
	struct uio auio;
	struct iovec aiov;
	int cnt;
	int error;
	int oublock_st, oublock_end;
	int inblock_st, inblock_end;

	mytd = curthread;
	cb = &aiocbe->uaiocb;
	fp = aiocbe->fd_file;

	aiov.iov_base = (void *)(uintptr_t)cb->aio_buf;
	aiov.iov_len = cb->aio_nbytes;

	auio.uio_iov = &aiov;
	auio.uio_iovcnt = 1;
	auio.uio_offset = cb->aio_offset;
	auio.uio_resid = cb->aio_nbytes;
	cnt = cb->aio_nbytes;
	auio.uio_segflg = UIO_USERSPACE;
	auio.uio_td = mytd;

	inblock_st = mytd->td_lwp->lwp_ru.ru_inblock;
	oublock_st = mytd->td_lwp->lwp_ru.ru_oublock;
	/*
	 * _aio_aqueue() acquires a reference to the file that is
	 * released in aio_free_entry().
	 */
	if (cb->aio_lio_opcode == LIO_READ) {
		auio.uio_rw = UIO_READ;
		error = fo_read(fp, &auio, fp->f_cred, O_FOFFSET);
	} else {
		auio.uio_rw = UIO_WRITE;
		error = fo_write(fp, &auio, fp->f_cred, O_FOFFSET);
	}
	inblock_end = mytd->td_lwp->lwp_ru.ru_inblock;
	oublock_end = mytd->td_lwp->lwp_ru.ru_oublock;

	aiocbe->inputcharge = inblock_end - inblock_st;
	aiocbe->outputcharge = oublock_end - oublock_st;

	if ((error) && (auio.uio_resid != cnt)) {
		if (error == ERESTART || error == EINTR || error == EWOULDBLOCK)
			error = 0;
		if ((error == EPIPE) && (cb->aio_lio_opcode == LIO_WRITE))
			ksignal(aiocbe->userproc, SIGPIPE);
	}

	cnt -= auio.uio_resid;
	cb->_aiocb_private.error = error;
	cb->_aiocb_private.status = cnt;
}

/*
 * The AIO daemon, most of the actual work is done in aio_process,
 * but the setup (and address space mgmt) is done in this routine.
 */
static void
aio_daemon(void *uproc, struct trapframe *frame)
{
	struct aio_liojob *lj;
	struct aiocb *cb;
	struct aiocblist *aiocbe;
	struct aioproclist *aiop;
	struct kaioinfo *ki;
	struct proc *mycp, *userp;
	struct vmspace *curvm;
	struct lwp *mylwp;
	struct ucred *cr;

	/*
	 * mplock not held on entry but we aren't mpsafe yet.
	 */
	get_mplock();

	mylwp = curthread->td_lwp;
	mycp = mylwp->lwp_proc;

	if (mycp->p_textvp) {
		vrele(mycp->p_textvp);
		mycp->p_textvp = NULL;
	}

	/*
	 * Allocate and ready the aio control info.  There is one aiop structure
	 * per daemon.
	 */
	aiop = objcache_get(aiop_oc, M_WAITOK);
	aiop->aioproc = mycp;
	aiop->aioprocflags |= AIOP_FREE;

	crit_enter();

	/*
	 * Place thread (lightweight process) onto the AIO free thread list.
	 */
	if (TAILQ_EMPTY(&aio_freeproc))
		wakeup(&aio_freeproc);
	TAILQ_INSERT_HEAD(&aio_freeproc, aiop, list);

	crit_exit();

	/* Make up a name for the daemon. */
	strcpy(mycp->p_comm, "aiod");

	/*
	 * Get rid of our current filedescriptors.  AIOD's don't need any
	 * filedescriptors, except as temporarily inherited from the client.
	 * Credentials are also cloned, and made equivalent to "root".
	 */
	fdfree(mycp, NULL);
	cr = cratom(&mycp->p_ucred);
	cr->cr_uid = 0;
	uireplace(&cr->cr_uidinfo, uifind(0));
	cr->cr_ngroups = 1;
	cr->cr_groups[0] = 1;

	/* The daemon resides in its own pgrp. */
	enterpgrp(mycp, mycp->p_pid, 1);

	/* Mark special process type. */
	mycp->p_flag |= P_SYSTEM | P_KTHREADP;

	/*
	 * Wakeup parent process.  (Parent sleeps to keep from blasting away
	 * and creating too many daemons.)
	 */
	wakeup(mycp);
	curvm = NULL;

	for (;;) {
		/*
		 * Take daemon off of free queue
		 */
		if (aiop->aioprocflags & AIOP_FREE) {
			crit_enter();
			TAILQ_REMOVE(&aio_freeproc, aiop, list);
			TAILQ_INSERT_TAIL(&aio_activeproc, aiop, list);
			aiop->aioprocflags &= ~AIOP_FREE;
			crit_exit();
		}
		aiop->aioprocflags &= ~AIOP_SCHED;

		/*
		 * Check for jobs.
		 */
		while ((aiocbe = aio_selectjob(aiop)) != NULL) {
			cb = &aiocbe->uaiocb;
			userp = aiocbe->userproc;

			aiocbe->jobstate = JOBST_JOBRUNNING;

			/*
			 * Connect to process address space for user program.
			 */
			if (curvm != userp->p_vmspace) {
				pmap_setlwpvm(mylwp, userp->p_vmspace);
				if (curvm)
					sysref_put(&curvm->vm_sysref);
				curvm = userp->p_vmspace;
				sysref_get(&curvm->vm_sysref);
			}

			ki = userp->p_aioinfo;
			lj = aiocbe->lio;

			/* Account for currently active jobs. */
			ki->kaio_active_count++;

			/* Do the I/O function. */
			aio_process(aiocbe);

			/* Decrement the active job count. */
			ki->kaio_active_count--;

			/*
			 * Increment the completion count for wakeup/signal
			 * comparisons.
			 */
			aiocbe->jobflags |= AIOCBLIST_DONE;
			ki->kaio_queue_finished_count++;
			if (lj)
				lj->lioj_queue_finished_count++;
			if ((ki->kaio_flags & KAIO_WAKEUP) || ((ki->kaio_flags
			    & KAIO_RUNDOWN) && (ki->kaio_active_count == 0))) {
				ki->kaio_flags &= ~KAIO_WAKEUP;
				wakeup(userp);
			}

			crit_enter();
			if (lj && (lj->lioj_flags &
			    (LIOJ_SIGNAL|LIOJ_SIGNAL_POSTED)) == LIOJ_SIGNAL) {
				if ((lj->lioj_queue_finished_count ==
				    lj->lioj_queue_count) &&
				    (lj->lioj_buffer_finished_count ==
				    lj->lioj_buffer_count)) {
						ksignal(userp,
						    lj->lioj_signal.sigev_signo);
						lj->lioj_flags |=
						    LIOJ_SIGNAL_POSTED;
				}
			}
			crit_exit();

			aiocbe->jobstate = JOBST_JOBFINISHED;

			crit_enter();
			TAILQ_REMOVE(&ki->kaio_jobqueue, aiocbe, plist);
			TAILQ_INSERT_TAIL(&ki->kaio_jobdone, aiocbe, plist);
			crit_exit();
			KNOTE(&aiocbe->klist, 0);

			if (aiocbe->jobflags & AIOCBLIST_RUNDOWN) {
				wakeup(aiocbe);
				aiocbe->jobflags &= ~AIOCBLIST_RUNDOWN;
			}

			if (cb->aio_sigevent.sigev_notify == SIGEV_SIGNAL) {
				ksignal(userp, cb->aio_sigevent.sigev_signo);
			}
		}

		/*
		 * Disconnect from user address space.
		 */
		if (curvm) {
			/* swap our original address space back in */
			pmap_setlwpvm(mylwp, mycp->p_vmspace);
			sysref_put(&curvm->vm_sysref);
			curvm = NULL;
		}

		/*
		 * If we are the first to be put onto the free queue, wakeup
		 * anyone waiting for a daemon.
		 */
		crit_enter();
		TAILQ_REMOVE(&aio_activeproc, aiop, list);
		if (TAILQ_EMPTY(&aio_freeproc))
			wakeup(&aio_freeproc);
		TAILQ_INSERT_HEAD(&aio_freeproc, aiop, list);
		aiop->aioprocflags |= AIOP_FREE;
		crit_exit();

		/*
		 * If daemon is inactive for a long time, allow it to exit,
		 * thereby freeing resources.
		 */
		if (((aiop->aioprocflags & AIOP_SCHED) == 0) && tsleep(mycp,
		    0, "aiordy", aiod_lifetime)) {
			crit_enter();
			if (TAILQ_EMPTY(&aio_jobs)) {
				if ((aiop->aioprocflags & AIOP_FREE) &&
				    (num_aio_procs > target_aio_procs)) {
					TAILQ_REMOVE(&aio_freeproc, aiop, list);
					crit_exit();
					objcache_put(aiop_oc, aiop);
					num_aio_procs--;
#ifdef DIAGNOSTIC
					if (mycp->p_vmspace->vm_sysref.refcnt <= 1) {
						kprintf("AIOD: bad vm refcnt for"
						    " exiting daemon: %d\n",
						    mycp->p_vmspace->vm_sysref.refcnt);
					}
#endif
					exit1(0);
				}
			}
			crit_exit();
		}
	}
}

/*
 * Create a new AIO daemon.  This is mostly a kernel-thread fork routine.  The
 * AIO daemon modifies its environment itself.
 */
static int
aio_newproc(void)
{
	int error;
	struct lwp *lp, *nlp;
	struct proc *np;

	lp = &lwp0;
	error = fork1(lp, RFPROC|RFMEM|RFNOWAIT, &np);
	if (error)
		return error;
	nlp = ONLY_LWP_IN_PROC(np);
	cpu_set_fork_handler(nlp, aio_daemon, curproc);
	start_forked_proc(lp, np);

	/*
	 * Wait until daemon is started, but continue on just in case to
	 * handle error conditions.
	 */
	error = tsleep(np, 0, "aiosta", aiod_timeout);
	num_aio_procs++;

	return error;
}

/*
 * Try the high-performance, low-overhead physio method for eligible
 * VCHR devices.  This method doesn't use an aio helper thread, and
 * thus has very low overhead. 
 *
 * Assumes that the caller, _aio_aqueue(), has incremented the file
 * structure's reference count, preventing its deallocation for the
 * duration of this call. 
 */
static int
aio_qphysio(struct proc *p, struct aiocblist *aiocbe)
{
	int error;
	struct aiocb *cb;
	struct file *fp;
	struct buf *bp;
	struct vnode *vp;
	struct kaioinfo *ki;
	struct aio_liojob *lj;
	int notify;

	cb = &aiocbe->uaiocb;
	fp = aiocbe->fd_file;

	if (fp->f_type != DTYPE_VNODE) 
		return (-1);

	vp = (struct vnode *)fp->f_data;

	/*
	 * If its not a disk, we don't want to return a positive error.
	 * It causes the aio code to not fall through to try the thread
	 * way when you're talking to a regular file.
	 */
	if (!vn_isdisk(vp, &error)) {
		if (error == ENOTBLK)
			return (-1);
		else
			return (error);
	}

 	if (cb->aio_nbytes % vp->v_rdev->si_bsize_phys)
		return (-1);

	if (cb->aio_nbytes >
	    MAXPHYS - (((vm_offset_t) cb->aio_buf) & PAGE_MASK))
		return (-1);

	ki = p->p_aioinfo;
	if (ki->kaio_buffer_count >= ki->kaio_ballowed_count) 
		return (-1);

	ki->kaio_buffer_count++;

	lj = aiocbe->lio;
	if (lj)
		lj->lioj_buffer_count++;

	/* Create and build a buffer header for a transfer. */
	bp = getpbuf_kva(NULL);
	BUF_KERNPROC(bp);

	/*
	 * Get a copy of the kva from the physical buffer.
	 */
	bp->b_bio1.bio_caller_info1.ptr = p;
	error = 0;

	bp->b_cmd = (cb->aio_lio_opcode == LIO_WRITE) ?
		    BUF_CMD_WRITE : BUF_CMD_READ;
	bp->b_bio1.bio_done = aio_physwakeup;
	bp->b_bio1.bio_flags |= BIO_SYNC;
	bp->b_bio1.bio_offset = cb->aio_offset;

	/* Bring buffer into kernel space. */
	if (vmapbuf(bp, __DEVOLATILE(char *, cb->aio_buf), cb->aio_nbytes) < 0) {
		error = EFAULT;
		goto doerror;
	}

	crit_enter();

	aiocbe->bp = bp;
	bp->b_bio1.bio_caller_info2.ptr = aiocbe;
	TAILQ_INSERT_TAIL(&aio_bufjobs, aiocbe, list);
	TAILQ_INSERT_TAIL(&ki->kaio_bufqueue, aiocbe, plist);
	aiocbe->jobstate = JOBST_JOBQBUF;
	cb->_aiocb_private.status = cb->aio_nbytes;
	num_buf_aio++;
	bp->b_error = 0;

	crit_exit();
	
	/*
	 * Perform the transfer.  vn_strategy must be used even though we
	 * know we have a device in order to deal with requests which exceed
	 * device DMA limitations.
	 */
	vn_strategy(vp, &bp->b_bio1);

	notify = 0;
	crit_enter();
	
#if 0
	/*
	 * If we had an error invoking the request, or an error in processing
	 * the request before we have returned, we process it as an error in
	 * transfer.  Note that such an I/O error is not indicated immediately,
	 * but is returned using the aio_error mechanism.  In this case,
	 * aio_suspend will return immediately.
	 */
	if (bp->b_error || (bp->b_flags & B_ERROR)) {
		struct aiocb *job = aiocbe->uuaiocb;

		aiocbe->uaiocb._aiocb_private.status = 0;
		suword(&job->_aiocb_private.status, 0);
		aiocbe->uaiocb._aiocb_private.error = bp->b_error;
		suword(&job->_aiocb_private.error, bp->b_error);

		ki->kaio_buffer_finished_count++;

		if (aiocbe->jobstate != JOBST_JOBBFINISHED) {
			aiocbe->jobstate = JOBST_JOBBFINISHED;
			aiocbe->jobflags |= AIOCBLIST_DONE;
			TAILQ_REMOVE(&aio_bufjobs, aiocbe, list);
			TAILQ_REMOVE(&ki->kaio_bufqueue, aiocbe, plist);
			TAILQ_INSERT_TAIL(&ki->kaio_bufdone, aiocbe, plist);
			notify = 1;
		}
	}
#endif
	crit_exit();
	if (notify)
		KNOTE(&aiocbe->klist, 0);
	return 0;

doerror:
	ki->kaio_buffer_count--;
	if (lj)
		lj->lioj_buffer_count--;
	aiocbe->bp = NULL;
	relpbuf(bp, NULL);
	return error;
}

/*
 * This waits/tests physio completion.
 */
static int
aio_fphysio(struct aiocblist *iocb)
{
	struct buf *bp;
	int error;

	bp = iocb->bp;

	error = biowait_timeout(&bp->b_bio1, "physstr", aiod_timeout);
	if (error == EWOULDBLOCK)
		return EINPROGRESS;

	/* Release mapping into kernel space. */
	vunmapbuf(bp);
	iocb->bp = 0;

	error = 0;
	
	/* Check for an error. */
	if (bp->b_flags & B_ERROR)
		error = bp->b_error;

	relpbuf(bp, NULL);
	return (error);
}
#endif /* VFS_AIO */

/*
 * Wake up aio requests that may be serviceable now.
 */
void
aio_swake(struct socket *so, struct signalsockbuf *ssb)
{
#ifndef VFS_AIO
	return;
#else
	struct aiocblist *cb,*cbn;
	struct proc *p;
	struct kaioinfo *ki = NULL;
	int opcode, wakecount = 0;
	struct aioproclist *aiop;

	if (ssb == &so->so_snd) {
		opcode = LIO_WRITE;
		atomic_clear_int(&so->so_snd.ssb_flags, SSB_AIO);
	} else {
		opcode = LIO_READ;
		atomic_clear_int(&so->so_rcv.ssb_flags, SSB_AIO);
	}

	for (cb = TAILQ_FIRST(&so->so_aiojobq); cb; cb = cbn) {
		cbn = TAILQ_NEXT(cb, list);
		if (opcode == cb->uaiocb.aio_lio_opcode) {
			p = cb->userproc;
			ki = p->p_aioinfo;
			TAILQ_REMOVE(&so->so_aiojobq, cb, list);
			TAILQ_REMOVE(&ki->kaio_sockqueue, cb, plist);
			TAILQ_INSERT_TAIL(&aio_jobs, cb, list);
			TAILQ_INSERT_TAIL(&ki->kaio_jobqueue, cb, plist);
			wakecount++;
			if (cb->jobstate != JOBST_JOBQGLOBAL)
				panic("invalid queue value");
		}
	}

	while (wakecount--) {
		if ((aiop = TAILQ_FIRST(&aio_freeproc)) != 0) {
			TAILQ_REMOVE(&aio_freeproc, aiop, list);
			TAILQ_INSERT_TAIL(&aio_activeproc, aiop, list);
			aiop->aioprocflags &= ~AIOP_FREE;
			wakeup(aiop->aioproc);
		}
	}
#endif /* VFS_AIO */
}

#ifdef VFS_AIO
/*
 * Queue a new AIO request.  Choosing either the threaded or direct physio VCHR
 * technique is done in this code.
 */
static int
_aio_aqueue(struct aiocb *job, struct aio_liojob *lj, int type)
{
	struct proc *p = curproc;
	struct file *fp;
	unsigned int fd;
	struct socket *so;
	int error;
	int opcode, user_opcode;
	struct aiocblist *aiocbe;
	struct aioproclist *aiop;
	struct kaioinfo *ki;
	struct kevent kev;
	struct kqueue *kq;
	struct file *kq_fp;
	int fflags;

	if ((aiocbe = TAILQ_FIRST(&aio_freejobs)) != NULL)
		TAILQ_REMOVE(&aio_freejobs, aiocbe, list);
	else
		aiocbe = objcache_get(aiocb_oc, M_WAITOK);

	aiocbe->inputcharge = 0;
	aiocbe->outputcharge = 0;
	callout_init(&aiocbe->timeout);
	SLIST_INIT(&aiocbe->klist);

	suword(&job->_aiocb_private.status, -1);
	suword(&job->_aiocb_private.error, 0);
	suword(&job->_aiocb_private.kernelinfo, -1);

	error = copyin(job, &aiocbe->uaiocb, sizeof(aiocbe->uaiocb));
	if (error) {
		suword(&job->_aiocb_private.error, error);
		TAILQ_INSERT_HEAD(&aio_freejobs, aiocbe, list);
		return error;
	}
	if (aiocbe->uaiocb.aio_sigevent.sigev_notify == SIGEV_SIGNAL &&
	    !_SIG_VALID(aiocbe->uaiocb.aio_sigevent.sigev_signo)) {
		TAILQ_INSERT_HEAD(&aio_freejobs, aiocbe, list);
		return EINVAL;
	}

	/* Save userspace address of the job info. */
	aiocbe->uuaiocb = job;

	/* Get the opcode. */
	user_opcode = aiocbe->uaiocb.aio_lio_opcode;
	if (type != LIO_NOP)
		aiocbe->uaiocb.aio_lio_opcode = type;
	opcode = aiocbe->uaiocb.aio_lio_opcode;

	/*
	 * Range check file descriptor.
	 */
	fflags = (opcode == LIO_WRITE) ? FWRITE : FREAD;
	fd = aiocbe->uaiocb.aio_fildes;
	fp = holdfp(p->p_fd, fd, fflags);
	if (fp == NULL) {
		TAILQ_INSERT_HEAD(&aio_freejobs, aiocbe, list);
		if (type == 0)
			suword(&job->_aiocb_private.error, EBADF);
		return EBADF;
	}

	aiocbe->fd_file = fp;

	if (aiocbe->uaiocb.aio_offset == -1LL) {
		error = EINVAL;
		goto aqueue_fail;
	}
	error = suword(&job->_aiocb_private.kernelinfo, jobrefid);
	if (error) {
		error = EINVAL;
		goto aqueue_fail;
	}
	aiocbe->uaiocb._aiocb_private.kernelinfo = (void *)(intptr_t)jobrefid;
	if (jobrefid == LONG_MAX)
		jobrefid = 1;
	else
		jobrefid++;
	
	if (opcode == LIO_NOP) {
		fdrop(fp);
		TAILQ_INSERT_HEAD(&aio_freejobs, aiocbe, list);
		if (type == 0) {
			suword(&job->_aiocb_private.error, 0);
			suword(&job->_aiocb_private.status, 0);
			suword(&job->_aiocb_private.kernelinfo, 0);
		}
		return 0;
	}
	if ((opcode != LIO_READ) && (opcode != LIO_WRITE)) {
		if (type == 0)
			suword(&job->_aiocb_private.status, 0);
		error = EINVAL;
		goto aqueue_fail;
	}

	if (aiocbe->uaiocb.aio_sigevent.sigev_notify == SIGEV_KEVENT) {
		kev.ident = aiocbe->uaiocb.aio_sigevent.sigev_notify_kqueue;
		kev.udata = aiocbe->uaiocb.aio_sigevent.sigev_value.sival_ptr;
	}
	else {
		/*
		 * This method for requesting kevent-based notification won't
		 * work on the alpha, since we're passing in a pointer
		 * via aio_lio_opcode, which is an int.  Use the SIGEV_KEVENT-
		 * based method instead.
		 */
		if (user_opcode == LIO_NOP || user_opcode == LIO_READ ||
		    user_opcode == LIO_WRITE)
			goto no_kqueue;

		error = copyin((struct kevent *)(uintptr_t)user_opcode,
		    &kev, sizeof(kev));
		if (error)
			goto aqueue_fail;
	}
	kq_fp = holdfp(p->p_fd, (int)kev.ident, -1);
	if (kq_fp == NULL || kq_fp->f_type != DTYPE_KQUEUE) {
		if (kq_fp) {
			fdrop(kq_fp);
			kq_fp = NULL;
		}
		error = EBADF;
		goto aqueue_fail;
	}
	kq = (struct kqueue *)kq_fp->f_data;
	kev.ident = (uintptr_t)aiocbe->uuaiocb;
	kev.filter = EVFILT_AIO;
	kev.flags = EV_ADD | EV_ENABLE | EV_FLAG1;
	kev.data = (intptr_t)aiocbe;
	error = kqueue_register(kq, &kev);
	fdrop(kq_fp);
aqueue_fail:
	if (error) {
		fdrop(fp);
		TAILQ_INSERT_HEAD(&aio_freejobs, aiocbe, list);
		if (type == 0)
			suword(&job->_aiocb_private.error, error);
		goto done;
	}
no_kqueue:

	suword(&job->_aiocb_private.error, EINPROGRESS);
	aiocbe->uaiocb._aiocb_private.error = EINPROGRESS;
	aiocbe->userproc = p;
	aiocbe->jobflags = 0;
	aiocbe->lio = lj;
	ki = p->p_aioinfo;

	if (fp->f_type == DTYPE_SOCKET) {
		/*
		 * Alternate queueing for socket ops: Reach down into the
		 * descriptor to get the socket data.  Then check to see if the
		 * socket is ready to be read or written (based on the requested
		 * operation).
		 *
		 * If it is not ready for io, then queue the aiocbe on the
		 * socket, and set the flags so we get a call when ssb_notify()
		 * happens.
		 */
		so = (struct socket *)fp->f_data;
		crit_enter();
		if (((opcode == LIO_READ) && (!soreadable(so))) || ((opcode ==
		    LIO_WRITE) && (!sowriteable(so)))) {
			TAILQ_INSERT_TAIL(&so->so_aiojobq, aiocbe, list);
			TAILQ_INSERT_TAIL(&ki->kaio_sockqueue, aiocbe, plist);
			if (opcode == LIO_READ)
				atomic_set_int(&so->so_rcv.ssb_flags, SSB_AIO);
			else
				atomic_set_int(&so->so_snd.ssb_flags, SSB_AIO);
			aiocbe->jobstate = JOBST_JOBQGLOBAL; /* XXX */
			ki->kaio_queue_count++;
			num_queue_count++;
			crit_exit();
			error = 0;
			goto done;
		}
		crit_exit();
	}

	if ((error = aio_qphysio(p, aiocbe)) == 0)
		goto done;
	if (error > 0) {
		suword(&job->_aiocb_private.status, 0);
		aiocbe->uaiocb._aiocb_private.error = error;
		suword(&job->_aiocb_private.error, error);
		goto done;
	}

	/* No buffer for daemon I/O. */
	aiocbe->bp = NULL;

	ki->kaio_queue_count++;
	if (lj)
		lj->lioj_queue_count++;
	crit_enter();
	TAILQ_INSERT_TAIL(&ki->kaio_jobqueue, aiocbe, plist);
	TAILQ_INSERT_TAIL(&aio_jobs, aiocbe, list);
	crit_exit();
	aiocbe->jobstate = JOBST_JOBQGLOBAL;

	num_queue_count++;
	error = 0;

	/*
	 * If we don't have a free AIO process, and we are below our quota, then
	 * start one.  Otherwise, depend on the subsequent I/O completions to
	 * pick-up this job.  If we don't successfully create the new process
	 * (thread) due to resource issues, we return an error for now (EAGAIN),
	 * which is likely not the correct thing to do.
	 */
	crit_enter();
retryproc:
	if ((aiop = TAILQ_FIRST(&aio_freeproc)) != NULL) {
		TAILQ_REMOVE(&aio_freeproc, aiop, list);
		TAILQ_INSERT_TAIL(&aio_activeproc, aiop, list);
		aiop->aioprocflags &= ~AIOP_FREE;
		wakeup(aiop->aioproc);
	} else if (((num_aio_resv_start + num_aio_procs) < max_aio_procs) &&
	    ((ki->kaio_active_count + num_aio_resv_start) <
	    ki->kaio_maxactive_count)) {
		num_aio_resv_start++;
		if ((error = aio_newproc()) == 0) {
			num_aio_resv_start--;
			goto retryproc;
		}
		num_aio_resv_start--;
	}
	crit_exit();
done:
	return error;
}

/*
 * This routine queues an AIO request, checking for quotas.
 */
static int
aio_aqueue(struct aiocb *job, int type)
{
	struct proc *p = curproc;
	struct kaioinfo *ki;

	if (p->p_aioinfo == NULL)
		aio_init_aioinfo(p);

	if (num_queue_count >= max_queue_count)
		return EAGAIN;

	ki = p->p_aioinfo;
	if (ki->kaio_queue_count >= ki->kaio_qallowed_count)
		return EAGAIN;

	return _aio_aqueue(job, NULL, type);
}
#endif /* VFS_AIO */

/*
 * Support the aio_return system call, as a side-effect, kernel resources are
 * released.
 *
 * MPALMOSTSAFE
 */
int
sys_aio_return(struct aio_return_args *uap)
{
#ifndef VFS_AIO
	return (ENOSYS);
#else
	struct proc *p = curproc;
	struct lwp *lp = curthread->td_lwp;
	long jobref;
	struct aiocblist *cb, *ncb;
	struct aiocb *ujob;
	struct kaioinfo *ki;
	int error;

	ki = p->p_aioinfo;
	if (ki == NULL)
		return EINVAL;

	ujob = uap->aiocbp;

	jobref = fuword(&ujob->_aiocb_private.kernelinfo);
	if (jobref == -1 || jobref == 0)
		return EINVAL;

	get_mplock();
	TAILQ_FOREACH(cb, &ki->kaio_jobdone, plist) {
		if (((intptr_t) cb->uaiocb._aiocb_private.kernelinfo) ==
		    jobref) {
			if (ujob == cb->uuaiocb) {
				uap->sysmsg_result =
				    cb->uaiocb._aiocb_private.status;
			} else {
				uap->sysmsg_result = EFAULT;
			}
			if (cb->uaiocb.aio_lio_opcode == LIO_WRITE) {
				lp->lwp_ru.ru_oublock += cb->outputcharge;
				cb->outputcharge = 0;
			} else if (cb->uaiocb.aio_lio_opcode == LIO_READ) {
				lp->lwp_ru.ru_inblock += cb->inputcharge;
				cb->inputcharge = 0;
			}
			aio_free_entry(cb);
			error = 0;
			goto done;
		}
	}
	crit_enter();
	for (cb = TAILQ_FIRST(&ki->kaio_bufdone); cb; cb = ncb) {
		ncb = TAILQ_NEXT(cb, plist);
		if (((intptr_t) cb->uaiocb._aiocb_private.kernelinfo)
		    == jobref) {
			crit_exit();
			if (ujob == cb->uuaiocb) {
				uap->sysmsg_result =
				    cb->uaiocb._aiocb_private.status;
			} else {
				uap->sysmsg_result = EFAULT;
			}
			aio_free_entry(cb);
			error = 0;
			goto done;
		}
	}
	crit_exit();
	error = EINVAL;
done:
	rel_mplock();
	return (error);
#endif /* VFS_AIO */
}

/*
 * Allow a process to wakeup when any of the I/O requests are completed.
 *
 * MPALMOSTSAFE
 */
int
sys_aio_suspend(struct aio_suspend_args *uap)
{
#ifndef VFS_AIO
	return ENOSYS;
#else
	struct proc *p = curproc;
	struct timeval atv;
	struct timespec ts;
	struct aiocb *const *cbptr, *cbp;
	struct kaioinfo *ki;
	struct aiocblist *cb;
	int i;
	int njoblist;
	int error, timo;
	long *ijoblist;
	struct aiocb **ujoblist;
	
	if ((u_int)uap->nent > AIO_LISTIO_MAX)
		return EINVAL;

	timo = 0;
	if (uap->timeout) {
		/* Get timespec struct. */
		if ((error = copyin(uap->timeout, &ts, sizeof(ts))) != 0)
			return error;

		if (ts.tv_nsec < 0 || ts.tv_nsec >= 1000000000)
			return (EINVAL);

		TIMESPEC_TO_TIMEVAL(&atv, &ts);
		if (itimerfix(&atv))
			return (EINVAL);
		timo = tvtohz_high(&atv);
	}

	ki = p->p_aioinfo;
	if (ki == NULL)
		return EAGAIN;

	get_mplock();

	njoblist = 0;
	ijoblist = objcache_get(aiol_oc, M_WAITOK);
	ujoblist = objcache_get(aiol_oc, M_WAITOK);
	cbptr = uap->aiocbp;

	for (i = 0; i < uap->nent; i++) {
		cbp = (struct aiocb *)(intptr_t)fuword(&cbptr[i]);
		if (cbp == 0)
			continue;
		ujoblist[njoblist] = cbp;
		ijoblist[njoblist] = fuword(&cbp->_aiocb_private.kernelinfo);
		njoblist++;
	}

	if (njoblist == 0) {
		objcache_put(aiol_oc, ijoblist);
		objcache_put(aiol_oc, ujoblist);
		error = 0;
		goto done;
	}

	error = 0;
	for (;;) {
		TAILQ_FOREACH(cb, &ki->kaio_jobdone, plist) {
			for (i = 0; i < njoblist; i++) {
				if (((intptr_t)
				    cb->uaiocb._aiocb_private.kernelinfo) ==
				    ijoblist[i]) {
					if (ujoblist[i] != cb->uuaiocb)
						error = EINVAL;
					objcache_put(aiol_oc, ijoblist);
					objcache_put(aiol_oc, ujoblist);
					goto done;
				}
			}
		}

		crit_enter();
		for (cb = TAILQ_FIRST(&ki->kaio_bufdone); cb; cb =
		    TAILQ_NEXT(cb, plist)) {
			for (i = 0; i < njoblist; i++) {
				if (((intptr_t)
				    cb->uaiocb._aiocb_private.kernelinfo) ==
				    ijoblist[i]) {
					crit_exit();
					if (ujoblist[i] != cb->uuaiocb)
						error = EINVAL;
					objcache_put(aiol_oc, ijoblist);
					objcache_put(aiol_oc, ujoblist);
					goto done;
				}
			}
		}

		ki->kaio_flags |= KAIO_WAKEUP;
		error = tsleep(p, PCATCH, "aiospn", timo);
		crit_exit();

		if (error == ERESTART || error == EINTR) {
			objcache_put(aiol_oc, ijoblist);
			objcache_put(aiol_oc, ujoblist);
			error = EINTR;
			goto done;
		} else if (error == EWOULDBLOCK) {
			objcache_put(aiol_oc, ijoblist);
			objcache_put(aiol_oc, ujoblist);
			error = EAGAIN;
			goto done;
		}
	}

/* NOTREACHED */
	error = EINVAL;
done:
	rel_mplock();
	return (error);
#endif /* VFS_AIO */
}

/*
 * aio_cancel cancels any non-physio aio operations not currently in
 * progress.
 *
 * MPALMOSTSAFE
 */
int
sys_aio_cancel(struct aio_cancel_args *uap)
{
#ifndef VFS_AIO
	return ENOSYS;
#else
	struct proc *p = curproc;
	struct kaioinfo *ki;
	struct aiocblist *cbe, *cbn;
	struct file *fp;
	struct socket *so;
	struct proc *po;
	int error;
	int cancelled=0;
	int notcancelled=0;
	struct vnode *vp;

	fp = holdfp(p->p_fd, uap->fd, -1);
	if (fp == NULL)
		return (EBADF);

	get_mplock();

        if (fp->f_type == DTYPE_VNODE) {
		vp = (struct vnode *)fp->f_data;
		
		if (vn_isdisk(vp,&error)) {
			uap->sysmsg_result = AIO_NOTCANCELED;
			error = 0;
			goto done2;
		}
	} else if (fp->f_type == DTYPE_SOCKET) {
		so = (struct socket *)fp->f_data;

		crit_enter();

		for (cbe = TAILQ_FIRST(&so->so_aiojobq); cbe; cbe = cbn) {
			cbn = TAILQ_NEXT(cbe, list);
			if ((uap->aiocbp == NULL) ||
				(uap->aiocbp == cbe->uuaiocb) ) {
				po = cbe->userproc;
				ki = po->p_aioinfo;
				TAILQ_REMOVE(&so->so_aiojobq, cbe, list);
				TAILQ_REMOVE(&ki->kaio_sockqueue, cbe, plist);
				TAILQ_INSERT_TAIL(&ki->kaio_jobdone, cbe, plist);
				if (ki->kaio_flags & KAIO_WAKEUP) {
					wakeup(po);
				}
				cbe->jobstate = JOBST_JOBFINISHED;
				cbe->uaiocb._aiocb_private.status=-1;
				cbe->uaiocb._aiocb_private.error=ECANCELED;
				cancelled++;
/* XXX cancelled, knote? */
			        if (cbe->uaiocb.aio_sigevent.sigev_notify ==
				    SIGEV_SIGNAL)
					ksignal(cbe->userproc, cbe->uaiocb.aio_sigevent.sigev_signo);
				if (uap->aiocbp) 
					break;
			}
		}
		crit_exit();

		if ((cancelled) && (uap->aiocbp)) {
			uap->sysmsg_result = AIO_CANCELED;
			error = 0;
			goto done2;
		}
	}
	ki=p->p_aioinfo;
	if (ki == NULL)
		goto done;
	crit_enter();

	for (cbe = TAILQ_FIRST(&ki->kaio_jobqueue); cbe; cbe = cbn) {
		cbn = TAILQ_NEXT(cbe, plist);

		if ((uap->fd == cbe->uaiocb.aio_fildes) &&
		    ((uap->aiocbp == NULL ) || 
		     (uap->aiocbp == cbe->uuaiocb))) {
			
			if (cbe->jobstate == JOBST_JOBQGLOBAL) {
				TAILQ_REMOVE(&aio_jobs, cbe, list);
                                TAILQ_REMOVE(&ki->kaio_jobqueue, cbe, plist);
                                TAILQ_INSERT_TAIL(&ki->kaio_jobdone, cbe,
                                    plist);
				cancelled++;
				ki->kaio_queue_finished_count++;
				cbe->jobstate = JOBST_JOBFINISHED;
				cbe->uaiocb._aiocb_private.status = -1;
				cbe->uaiocb._aiocb_private.error = ECANCELED;
/* XXX cancelled, knote? */
			        if (cbe->uaiocb.aio_sigevent.sigev_notify ==
				    SIGEV_SIGNAL)
					ksignal(cbe->userproc, cbe->uaiocb.aio_sigevent.sigev_signo);
			} else {
				notcancelled++;
			}
		}
	}
	crit_exit();
done:
	if (notcancelled)
		uap->sysmsg_result = AIO_NOTCANCELED;
	else if (cancelled)
		uap->sysmsg_result = AIO_CANCELED;
	else
		uap->sysmsg_result = AIO_ALLDONE;
	error = 0;
done2:
	rel_mplock();
	fdrop(fp);
	return error;
#endif /* VFS_AIO */
}

/*
 * aio_error is implemented in the kernel level for compatibility purposes only.
 * For a user mode async implementation, it would be best to do it in a userland
 * subroutine.
 *
 * MPALMOSTSAFE
 */
int
sys_aio_error(struct aio_error_args *uap)
{
#ifndef VFS_AIO
	return ENOSYS;
#else
	struct proc *p = curproc;
	struct aiocblist *cb;
	struct kaioinfo *ki;
	long jobref;
	int error;

	ki = p->p_aioinfo;
	if (ki == NULL)
		return EINVAL;

	jobref = fuword(&uap->aiocbp->_aiocb_private.kernelinfo);
	if ((jobref == -1) || (jobref == 0))
		return EINVAL;

	get_mplock();
	error = 0;

	TAILQ_FOREACH(cb, &ki->kaio_jobdone, plist) {
		if (((intptr_t)cb->uaiocb._aiocb_private.kernelinfo) ==
		    jobref) {
			uap->sysmsg_result = cb->uaiocb._aiocb_private.error;
			goto done;
		}
	}

	crit_enter();

	for (cb = TAILQ_FIRST(&ki->kaio_jobqueue); cb; cb = TAILQ_NEXT(cb,
	    plist)) {
		if (((intptr_t)cb->uaiocb._aiocb_private.kernelinfo) ==
		    jobref) {
			uap->sysmsg_result = EINPROGRESS;
			crit_exit();
			goto done;
		}
	}

	for (cb = TAILQ_FIRST(&ki->kaio_sockqueue); cb; cb = TAILQ_NEXT(cb,
	    plist)) {
		if (((intptr_t)cb->uaiocb._aiocb_private.kernelinfo) ==
		    jobref) {
			uap->sysmsg_result = EINPROGRESS;
			crit_exit();
			goto done;
		}
	}
	crit_exit();

	crit_enter();
	for (cb = TAILQ_FIRST(&ki->kaio_bufdone); cb; cb = TAILQ_NEXT(cb,
	    plist)) {
		if (((intptr_t)cb->uaiocb._aiocb_private.kernelinfo) ==
		    jobref) {
			uap->sysmsg_result = cb->uaiocb._aiocb_private.error;
			crit_exit();
			goto done;
		}
	}

	for (cb = TAILQ_FIRST(&ki->kaio_bufqueue); cb; cb = TAILQ_NEXT(cb,
	    plist)) {
		if (((intptr_t)cb->uaiocb._aiocb_private.kernelinfo) ==
		    jobref) {
			uap->sysmsg_result = EINPROGRESS;
			crit_exit();
			goto done;
		}
	}
	crit_exit();
	error = EINVAL;
done:
	rel_mplock();
	return (error);
#endif /* VFS_AIO */
}

/*
 * syscall - asynchronous read from a file (REALTIME)
 *
 * MPALMOSTSAFE
 */
int
sys_aio_read(struct aio_read_args *uap)
{
#ifndef VFS_AIO
	return ENOSYS;
#else
	int error;

	get_mplock();
	error =  aio_aqueue(uap->aiocbp, LIO_READ);
	rel_mplock();
	return (error);
#endif /* VFS_AIO */
}

/*
 * syscall - asynchronous write to a file (REALTIME)
 *
 * MPALMOSTSAFE
 */
int
sys_aio_write(struct aio_write_args *uap)
{
#ifndef VFS_AIO
	return ENOSYS;
#else
	int error;

	get_mplock();
	error = aio_aqueue(uap->aiocbp, LIO_WRITE);
	rel_mplock();
	return (error);
#endif /* VFS_AIO */
}

/*
 * syscall - XXX undocumented
 *
 * MPALMOSTSAFE
 */
int
sys_lio_listio(struct lio_listio_args *uap)
{
#ifndef VFS_AIO
	return ENOSYS;
#else
	struct proc *p = curproc;
	struct lwp *lp = curthread->td_lwp;
	int nent, nentqueued;
	struct aiocb *iocb, * const *cbptr;
	struct aiocblist *cb;
	struct kaioinfo *ki;
	struct aio_liojob *lj;
	int error, runningcode;
	int nerror;
	int i;

	if ((uap->mode != LIO_NOWAIT) && (uap->mode != LIO_WAIT))
		return EINVAL;

	nent = uap->nent;
	if (nent > AIO_LISTIO_MAX)
		return EINVAL;

	get_mplock();

	if (p->p_aioinfo == NULL)
		aio_init_aioinfo(p);

	if ((nent + num_queue_count) > max_queue_count) {
		error = EAGAIN;
		goto done;
	}

	ki = p->p_aioinfo;
	if ((nent + ki->kaio_queue_count) > ki->kaio_qallowed_count) {
		error = EAGAIN;
		goto done;
	}

	lj = objcache_get(aiolio_oc, M_WAITOK);
	if (lj == NULL) {
		error = EAGAIN;
		goto done;
	}

	lj->lioj_flags = 0;
	lj->lioj_buffer_count = 0;
	lj->lioj_buffer_finished_count = 0;
	lj->lioj_queue_count = 0;
	lj->lioj_queue_finished_count = 0;
	lj->lioj_ki = ki;

	/*
	 * Setup signal.
	 */
	if (uap->sig && (uap->mode == LIO_NOWAIT)) {
		error = copyin(uap->sig, &lj->lioj_signal,
		    sizeof(lj->lioj_signal));
		if (error) {
			objcache_put(aiolio_oc, lj);
			goto done;
		}
		if (!_SIG_VALID(lj->lioj_signal.sigev_signo)) {
			objcache_put(aiolio_oc, lj);
			error = EINVAL;
			goto done;
		}
		lj->lioj_flags |= LIOJ_SIGNAL;
		lj->lioj_flags &= ~LIOJ_SIGNAL_POSTED;
	} else
		lj->lioj_flags &= ~LIOJ_SIGNAL;

	TAILQ_INSERT_TAIL(&ki->kaio_liojoblist, lj, lioj_list);
	/*
	 * Get pointers to the list of I/O requests.
	 */
	nerror = 0;
	nentqueued = 0;
	cbptr = uap->acb_list;
	for (i = 0; i < uap->nent; i++) {
		iocb = (struct aiocb *)(intptr_t)fuword(&cbptr[i]);
		if (((intptr_t)iocb != -1) && ((intptr_t)iocb != 0)) {
			error = _aio_aqueue(iocb, lj, 0);
			if (error == 0)
				nentqueued++;
			else
				nerror++;
		}
	}

	/*
	 * If we haven't queued any, then just return error.
	 */
	if (nentqueued == 0) {
		error = 0;
		goto done;
	}

	/*
	 * Calculate the appropriate error return.
	 */
	runningcode = 0;
	if (nerror)
		runningcode = EIO;

	if (uap->mode == LIO_WAIT) {
		int command, found, jobref;
		
		for (;;) {
			found = 0;
			for (i = 0; i < uap->nent; i++) {
				/*
				 * Fetch address of the control buf pointer in
				 * user space.
				 */
				iocb = (struct aiocb *)
				    (intptr_t)fuword(&cbptr[i]);
				if (((intptr_t)iocb == -1) || ((intptr_t)iocb
				    == 0))
					continue;

				/*
				 * Fetch the associated command from user space.
				 */
				command = fuword(&iocb->aio_lio_opcode);
				if (command == LIO_NOP) {
					found++;
					continue;
				}

				jobref = fuword(&iocb->_aiocb_private.kernelinfo);

				TAILQ_FOREACH(cb, &ki->kaio_jobdone, plist) {
					if (((intptr_t)cb->uaiocb._aiocb_private.kernelinfo)
					    == jobref) {
						if (cb->uaiocb.aio_lio_opcode
						    == LIO_WRITE) {
							lp->lwp_ru.ru_oublock +=
							    cb->outputcharge;
							cb->outputcharge = 0;
						} else if (cb->uaiocb.aio_lio_opcode
						    == LIO_READ) {
							lp->lwp_ru.ru_inblock +=
							    cb->inputcharge;
							cb->inputcharge = 0;
						}
						found++;
						break;
					}
				}

				crit_enter();
				TAILQ_FOREACH(cb, &ki->kaio_bufdone, plist) {
					if (((intptr_t)cb->uaiocb._aiocb_private.kernelinfo)
					    == jobref) {
						found++;
						break;
					}
				}
				crit_exit();
			}

			/*
			 * If all I/Os have been disposed of, then we can
			 * return.
			 */
			if (found == nentqueued) {
				error = runningcode;
				goto done;
			}
			
			ki->kaio_flags |= KAIO_WAKEUP;
			error = tsleep(p, PCATCH, "aiospn", 0);

			if (error == EINTR) {
				goto done;
			} else if (error == EWOULDBLOCK) {
				error = EAGAIN;
				goto done;
			}
		}
	}

	error = runningcode;
done:
	rel_mplock();
	return (error);
#endif /* VFS_AIO */
}

#ifdef VFS_AIO
/*
 * This is a weird hack so that we can post a signal.  It is safe to do so from
 * a timeout routine, but *not* from an interrupt routine.
 */
static void
process_signal(void *aioj)
{
	struct aiocblist *aiocbe = aioj;
	struct aio_liojob *lj = aiocbe->lio;
	struct aiocb *cb = &aiocbe->uaiocb;

	if ((lj) && (lj->lioj_signal.sigev_notify == SIGEV_SIGNAL) &&
	    (lj->lioj_queue_count == lj->lioj_queue_finished_count)) {
		ksignal(lj->lioj_ki->kaio_p, lj->lioj_signal.sigev_signo);
		lj->lioj_flags |= LIOJ_SIGNAL_POSTED;
	}

	if (cb->aio_sigevent.sigev_notify == SIGEV_SIGNAL)
		ksignal(aiocbe->userproc, cb->aio_sigevent.sigev_signo);
}

/*
 * Interrupt handler for physio, performs the necessary process wakeups, and
 * signals.
 */
static void
aio_physwakeup(struct bio *bio)
{
	struct buf *bp = bio->bio_buf;
	struct aiocblist *aiocbe;
	struct proc *p;
	struct kaioinfo *ki;
	struct aio_liojob *lj;

	aiocbe = bio->bio_caller_info2.ptr;
	get_mplock();

	if (aiocbe) {
		p = bio->bio_caller_info1.ptr;

		aiocbe->jobstate = JOBST_JOBBFINISHED;
		aiocbe->uaiocb._aiocb_private.status -= bp->b_resid;
		aiocbe->uaiocb._aiocb_private.error = 0;
		aiocbe->jobflags |= AIOCBLIST_DONE;

		if (bp->b_flags & B_ERROR)
			aiocbe->uaiocb._aiocb_private.error = bp->b_error;

		lj = aiocbe->lio;
		if (lj) {
			lj->lioj_buffer_finished_count++;
			
			/*
			 * wakeup/signal if all of the interrupt jobs are done.
			 */
			if (lj->lioj_buffer_finished_count ==
			    lj->lioj_buffer_count) {
				/*
				 * Post a signal if it is called for.
				 */
				if ((lj->lioj_flags &
				    (LIOJ_SIGNAL|LIOJ_SIGNAL_POSTED)) ==
				    LIOJ_SIGNAL) {
					lj->lioj_flags |= LIOJ_SIGNAL_POSTED;
					callout_reset(&aiocbe->timeout, 0,
							process_signal, aiocbe);
				}
			}
		}

		ki = p->p_aioinfo;
		if (ki) {
			ki->kaio_buffer_finished_count++;
			TAILQ_REMOVE(&aio_bufjobs, aiocbe, list);
			TAILQ_REMOVE(&ki->kaio_bufqueue, aiocbe, plist);
			TAILQ_INSERT_TAIL(&ki->kaio_bufdone, aiocbe, plist);

			KNOTE(&aiocbe->klist, 0);
			/* Do the wakeup. */
			if (ki->kaio_flags & (KAIO_RUNDOWN|KAIO_WAKEUP)) {
				ki->kaio_flags &= ~KAIO_WAKEUP;
				wakeup(p);
			}
		}

		if (aiocbe->uaiocb.aio_sigevent.sigev_notify == SIGEV_SIGNAL) {
			callout_reset(&aiocbe->timeout, 0, 
					process_signal, aiocbe);
		}
	}
	biodone_sync(bio);
	rel_mplock();
}
#endif /* VFS_AIO */

/*
 * syscall - wait for the next completion of an aio request
 *
 * MPALMOSTSAFE
 */
int
sys_aio_waitcomplete(struct aio_waitcomplete_args *uap)
{
#ifndef VFS_AIO
	return ENOSYS;
#else
	struct proc *p = curproc;
	struct lwp *lp = curthread->td_lwp;
	struct timeval atv;
	struct timespec ts;
	struct kaioinfo *ki;
	struct aiocblist *cb = NULL;
	int error, timo;
	
	suword(uap->aiocbp, (int)NULL);

	timo = 0;
	if (uap->timeout) {
		/* Get timespec struct. */
		error = copyin(uap->timeout, &ts, sizeof(ts));
		if (error)
			return error;

		if ((ts.tv_nsec < 0) || (ts.tv_nsec >= 1000000000))
			return (EINVAL);

		TIMESPEC_TO_TIMEVAL(&atv, &ts);
		if (itimerfix(&atv))
			return (EINVAL);
		timo = tvtohz_high(&atv);
	}

	ki = p->p_aioinfo;
	if (ki == NULL)
		return EAGAIN;

	get_mplock();

	for (;;) {
		if ((cb = TAILQ_FIRST(&ki->kaio_jobdone)) != 0) {
			suword(uap->aiocbp, (uintptr_t)cb->uuaiocb);
			uap->sysmsg_result = cb->uaiocb._aiocb_private.status;
			if (cb->uaiocb.aio_lio_opcode == LIO_WRITE) {
				lp->lwp_ru.ru_oublock +=
				    cb->outputcharge;
				cb->outputcharge = 0;
			} else if (cb->uaiocb.aio_lio_opcode == LIO_READ) {
				lp->lwp_ru.ru_inblock += cb->inputcharge;
				cb->inputcharge = 0;
			}
			aio_free_entry(cb);
			error = cb->uaiocb._aiocb_private.error;
			break;
		}

		crit_enter();
 		if ((cb = TAILQ_FIRST(&ki->kaio_bufdone)) != 0 ) {
			crit_exit();
			suword(uap->aiocbp, (uintptr_t)cb->uuaiocb);
			uap->sysmsg_result = cb->uaiocb._aiocb_private.status;
			aio_free_entry(cb);
			error = cb->uaiocb._aiocb_private.error;
			break;
		}

		ki->kaio_flags |= KAIO_WAKEUP;
		error = tsleep(p, PCATCH, "aiowc", timo);
		crit_exit();

		if (error == ERESTART) {
			error = EINTR;
			break;
		}
		if (error < 0)
			break;
		if (error == EINTR)
			break;
		if (error == EWOULDBLOCK) {
			error = EAGAIN;
			break;
		}
	}
	rel_mplock();
	return (error);
#endif /* VFS_AIO */
}

#ifndef VFS_AIO
static int
filt_aioattach(struct knote *kn)
{

	return (ENXIO);
}

struct filterops aio_filtops =
	{ 0, filt_aioattach, NULL, NULL };

#else
/* kqueue attach function */
static int
filt_aioattach(struct knote *kn)
{
	struct aiocblist *aiocbe = (struct aiocblist *)kn->kn_sdata;

	/*
	 * The aiocbe pointer must be validated before using it, so
	 * registration is restricted to the kernel; the user cannot
	 * set EV_FLAG1.
	 */
	if ((kn->kn_flags & EV_FLAG1) == 0)
		return (EPERM);
	kn->kn_flags &= ~EV_FLAG1;

	knote_insert(&aiocbe->klist, kn);

	return (0);
}

/* kqueue detach function */
static void
filt_aiodetach(struct knote *kn)
{
	struct aiocblist *aiocbe = (struct aiocblist *)kn->kn_sdata;

	knote_remove(&aiocbe->klist, kn);
}

/* kqueue filter function */
/*ARGSUSED*/
static int
filt_aio(struct knote *kn, long hint)
{
	struct aiocblist *aiocbe = (struct aiocblist *)kn->kn_sdata;

	kn->kn_data = aiocbe->uaiocb._aiocb_private.error;
	if (aiocbe->jobstate != JOBST_JOBFINISHED &&
	    aiocbe->jobstate != JOBST_JOBBFINISHED)
		return (0);
	kn->kn_flags |= EV_EOF; 
	return (1);
}

struct filterops aio_filtops =
	{ 0, filt_aioattach, filt_aiodetach, filt_aio };
#endif /* VFS_AIO */
