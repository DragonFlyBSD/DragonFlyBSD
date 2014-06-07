/*-
 * Copyright (c) 1994-1995 Søren Schmidt
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer 
 *    in this position and unchanged.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission
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
 *
 * $FreeBSD: src/sys/compat/linux/linux_ipc.c,v 1.17.2.3 2001/11/05 19:08:22 marcel Exp $
 * $DragonFly: src/sys/emulation/linux/linux_ipc.c,v 1.8 2006/06/05 07:26:09 dillon Exp $
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/sysproto.h>
#include <sys/proc.h>
#include <sys/msg.h>
#include <sys/sem.h>
#include <sys/shm.h>

#include <arch_linux/linux.h>
#include <machine/limits.h>
#include <arch_linux/linux_proto.h>
#include "linux_ipc.h"
#include "linux_util.h"


static void
bsd_to_linux_shminfo( struct shminfo *bpp, struct l_shminfo *lpp)
{
	lpp->shmmax = bpp->shmmax;
	lpp->shmmin = bpp->shmmin;
	lpp->shmmni = bpp->shmmni;
	lpp->shmseg = bpp->shmseg;
	lpp->shmall = bpp->shmall;
}

#if 0
static void
bsd_to_linux_shm_info( struct shm_info *bpp, struct l_shm_info *lpp)
{
	lpp->used_ids = bpp->used_ids ;
	lpp->shm_tot = bpp->shm_tot ;
	lpp->shm_rss = bpp->shm_rss ;
	lpp->shm_swp = bpp->shm_swp ;
	lpp->swap_attempts = bpp->swap_attempts ;
	lpp->swap_successes = bpp->swap_successes ;
}
#endif

/*
 * MPSAFE
 */
static void
linux_to_bsd_ipc_perm(struct l_ipc_perm *lpp, struct ipc_perm *bpp)
{
    bpp->key = lpp->key;
    bpp->uid = lpp->uid;
    bpp->gid = lpp->gid;
    bpp->cuid = lpp->cuid;
    bpp->cgid = lpp->cgid;
    bpp->mode = lpp->mode;
    bpp->seq = lpp->seq;
}

/*
 * MPSAFE
 */
static void
bsd_to_linux_ipc_perm(struct ipc_perm *bpp, struct l_ipc_perm *lpp)
{
    lpp->key = bpp->key;
    lpp->uid = bpp->uid;
    lpp->gid = bpp->gid;
    lpp->cuid = bpp->cuid;
    lpp->cgid = bpp->cgid;
    lpp->mode = bpp->mode;
    lpp->seq = bpp->seq;
}

/*
 * MPSAFE
 */
static void
linux_to_bsd_semid_ds(struct l_semid_ds *lsp, struct semid_ds *bsp)
{
    linux_to_bsd_ipc_perm(&lsp->sem_perm, &bsp->sem_perm);
    bsp->sem_otime = lsp->sem_otime;
    bsp->sem_ctime = lsp->sem_ctime;
    bsp->sem_nsems = lsp->sem_nsems;
    bsp->sem_base = lsp->sem_base;
}

/*
 * MPSAFE
 */
static void
bsd_to_linux_semid_ds(struct semid_ds *bsp, struct l_semid_ds *lsp)
{
	bsd_to_linux_ipc_perm(&bsp->sem_perm, &lsp->sem_perm);
	lsp->sem_otime = bsp->sem_otime;
	lsp->sem_ctime = bsp->sem_ctime;
	lsp->sem_nsems = bsp->sem_nsems;
	lsp->sem_base = bsp->sem_base;
}

/*
 * MPSAFE
 */
static void
linux_to_bsd_shmid_ds(struct l_shmid_ds *lsp, struct shmid_ds *bsp)
{
    linux_to_bsd_ipc_perm(&lsp->shm_perm, &bsp->shm_perm);
    bsp->shm_segsz = lsp->shm_segsz;
    bsp->shm_lpid = lsp->shm_lpid;
    bsp->shm_cpid = lsp->shm_cpid;
    bsp->shm_nattch = lsp->shm_nattch;
    bsp->shm_atime = lsp->shm_atime;
    bsp->shm_dtime = lsp->shm_dtime;
    bsp->shm_ctime = lsp->shm_ctime;
}

/*
 * MPSAFE
 */
static void
bsd_to_linux_shmid_ds(struct shmid_ds *bsp, struct l_shmid_ds *lsp)
{
    bsd_to_linux_ipc_perm(&bsp->shm_perm, &lsp->shm_perm);
    if (bsp->shm_segsz > INT_MAX)
	    lsp->shm_segsz = INT_MAX;
    else
	    lsp->shm_segsz = bsp->shm_segsz;
    lsp->shm_lpid = bsp->shm_lpid;
    lsp->shm_cpid = bsp->shm_cpid;
    lsp->shm_nattch = bsp->shm_nattch;
    lsp->shm_atime = bsp->shm_atime;
    lsp->shm_dtime = bsp->shm_dtime;
    lsp->shm_ctime = bsp->shm_ctime;
    lsp->private3 = 0;
}

static void
linux_to_bsd_msqid_ds(struct l_msqid_ds *lsp, struct msqid_ds *bsp)
{
    linux_to_bsd_ipc_perm(&lsp->msg_perm, &bsp->msg_perm);
    bsp->msg_cbytes = lsp->msg_cbytes;
    bsp->msg_qnum = lsp->msg_qnum;
    bsp->msg_qbytes = lsp->msg_qbytes;
    bsp->msg_lspid = lsp->msg_lspid;
    bsp->msg_lrpid = lsp->msg_lrpid;
    bsp->msg_stime = lsp->msg_stime;
    bsp->msg_rtime = lsp->msg_rtime;
    bsp->msg_ctime = lsp->msg_ctime;
}

static void
bsd_to_linux_msqid_ds(struct msqid_ds *bsp, struct l_msqid_ds *lsp)
{
    bsd_to_linux_ipc_perm(&bsp->msg_perm, &lsp->msg_perm);
    lsp->msg_cbytes = bsp->msg_cbytes;
    lsp->msg_qnum = bsp->msg_qnum;
    lsp->msg_qbytes = bsp->msg_qbytes;
    lsp->msg_lspid = bsp->msg_lspid;
    lsp->msg_lrpid = bsp->msg_lrpid;
    lsp->msg_stime = bsp->msg_stime;
    lsp->msg_rtime = bsp->msg_rtime;
    lsp->msg_ctime = bsp->msg_ctime;
}

static void
linux_ipc_perm_to_ipc64_perm(struct l_ipc_perm *in, struct l_ipc64_perm *out)
{

	/* XXX: do we really need to do something here? */
	out->key = in->key;
	out->uid = in->uid;
	out->gid = in->gid;
	out->cuid = in->cuid;
	out->cgid = in->cgid;
	out->mode = in->mode;
	out->seq = in->seq;
}

static int
linux_msqid_pullup(l_int ver, struct l_msqid_ds *linux_msqid, caddr_t uaddr)
{
	struct l_msqid64_ds linux_msqid64;
	int error;

	if (ver == LINUX_IPC_64) {
		error = copyin(uaddr, &linux_msqid64, sizeof(linux_msqid64));
		if (error != 0)
			return (error);

		bzero(linux_msqid, sizeof(*linux_msqid));

		linux_msqid->msg_perm.uid = linux_msqid64.msg_perm.uid;
		linux_msqid->msg_perm.gid = linux_msqid64.msg_perm.gid;
		linux_msqid->msg_perm.mode = linux_msqid64.msg_perm.mode;

		if (linux_msqid64.msg_qbytes > USHRT_MAX)
			linux_msqid->msg_lqbytes = linux_msqid64.msg_qbytes;
		else
			linux_msqid->msg_qbytes = linux_msqid64.msg_qbytes;
	} else {
		error = copyin(uaddr, linux_msqid, sizeof(*linux_msqid));
	}
	return (error);
}

static int
linux_msqid_pushdown(l_int ver, struct l_msqid_ds *linux_msqid, caddr_t uaddr)
{
	struct l_msqid64_ds linux_msqid64;

	if (ver == LINUX_IPC_64) {
		bzero(&linux_msqid64, sizeof(linux_msqid64));

		linux_ipc_perm_to_ipc64_perm(&linux_msqid->msg_perm,
		    &linux_msqid64.msg_perm);

		linux_msqid64.msg_stime = linux_msqid->msg_stime;
		linux_msqid64.msg_rtime = linux_msqid->msg_rtime;
		linux_msqid64.msg_ctime = linux_msqid->msg_ctime;

		if (linux_msqid->msg_cbytes == 0)
			linux_msqid64.msg_cbytes = linux_msqid->msg_lcbytes;
		else
			linux_msqid64.msg_cbytes = linux_msqid->msg_cbytes;

		linux_msqid64.msg_qnum = linux_msqid->msg_qnum;

		if (linux_msqid->msg_qbytes == 0)
			linux_msqid64.msg_qbytes = linux_msqid->msg_lqbytes;
		else
			linux_msqid64.msg_qbytes = linux_msqid->msg_qbytes;

		linux_msqid64.msg_lspid = linux_msqid->msg_lspid;
		linux_msqid64.msg_lrpid = linux_msqid->msg_lrpid;

		return (copyout(&linux_msqid64, uaddr, sizeof(linux_msqid64)));
	} else {
		return (copyout(linux_msqid, uaddr, sizeof(*linux_msqid)));
	}
}

static int
linux_semid_pullup(l_int ver, struct l_semid_ds *linux_semid, caddr_t uaddr)
{
	struct l_semid64_ds linux_semid64;
	int error;

	if (ver == LINUX_IPC_64) {
		error = copyin(uaddr, &linux_semid64, sizeof(linux_semid64));
		if (error != 0)
			return (error);

		bzero(linux_semid, sizeof(*linux_semid));

		linux_semid->sem_perm.uid = linux_semid64.sem_perm.uid;
		linux_semid->sem_perm.gid = linux_semid64.sem_perm.gid;
		linux_semid->sem_perm.mode = linux_semid64.sem_perm.mode;
	} else {
		error = copyin(uaddr, linux_semid, sizeof(*linux_semid));
	}
	return (error);
}

static int
linux_semid_pushdown(l_int ver, struct l_semid_ds *linux_semid, caddr_t uaddr)
{
	struct l_semid64_ds linux_semid64;

	if (ver == LINUX_IPC_64) {
		bzero(&linux_semid64, sizeof(linux_semid64));

		linux_ipc_perm_to_ipc64_perm(&linux_semid->sem_perm,
		    &linux_semid64.sem_perm);

		linux_semid64.sem_otime = linux_semid->sem_otime;
		linux_semid64.sem_ctime = linux_semid->sem_ctime;
		linux_semid64.sem_nsems = linux_semid->sem_nsems;

		return (copyout(&linux_semid64, uaddr, sizeof(linux_semid64)));
	} else {
		return (copyout(linux_semid, uaddr, sizeof(*linux_semid)));
	}
}

static int
linux_shmid_pullup(l_int ver, struct l_shmid_ds *linux_shmid, caddr_t uaddr)
{
	struct l_shmid64_ds linux_shmid64;
	int error;

	if (ver == LINUX_IPC_64) {
		error = copyin(uaddr, &linux_shmid64, sizeof(linux_shmid64));
		if (error != 0)
			return (error);

		bzero(linux_shmid, sizeof(*linux_shmid));

		linux_shmid->shm_perm.uid = linux_shmid64.shm_perm.uid;
		linux_shmid->shm_perm.gid = linux_shmid64.shm_perm.gid;
		linux_shmid->shm_perm.mode = linux_shmid64.shm_perm.mode;
	} else {
		error = copyin(uaddr, linux_shmid, sizeof(*linux_shmid));
	}
	return (error);
}

static int
linux_shmid_pushdown(l_int ver, struct l_shmid_ds *linux_shmid, caddr_t uaddr)
{
	struct l_shmid64_ds linux_shmid64;

	/*
	 * XXX: This is backwards and loses information in shm_nattch
	 * and shm_segsz.  We should probably either expose the BSD
	 * shmid structure directly and convert it to either the
	 * non-64 or 64 variant directly or the code should always
	 * convert to the 64 variant and then truncate values into the
	 * non-64 variant if needed since the 64 variant has more
	 * precision.
	 */
	if (ver == LINUX_IPC_64) {
		bzero(&linux_shmid64, sizeof(linux_shmid64));

		linux_ipc_perm_to_ipc64_perm(&linux_shmid->shm_perm,
		    &linux_shmid64.shm_perm);

		linux_shmid64.shm_segsz = linux_shmid->shm_segsz;
		linux_shmid64.shm_atime = linux_shmid->shm_atime;
		linux_shmid64.shm_dtime = linux_shmid->shm_dtime;
		linux_shmid64.shm_ctime = linux_shmid->shm_ctime;
		linux_shmid64.shm_cpid = linux_shmid->shm_cpid;
		linux_shmid64.shm_lpid = linux_shmid->shm_lpid;
		linux_shmid64.shm_nattch = linux_shmid->shm_nattch;

		return (copyout(&linux_shmid64, uaddr, sizeof(linux_shmid64)));
	} else {
		return (copyout(linux_shmid, uaddr, sizeof(*linux_shmid)));
	}
}

static int
linux_shminfo_pushdown(l_int ver, struct l_shminfo *linux_shminfo,
    caddr_t uaddr)
{
	struct l_shminfo64 linux_shminfo64;

	if (ver == LINUX_IPC_64) {
		bzero(&linux_shminfo64, sizeof(linux_shminfo64));

		linux_shminfo64.shmmax = linux_shminfo->shmmax;
		linux_shminfo64.shmmin = linux_shminfo->shmmin;
		linux_shminfo64.shmmni = linux_shminfo->shmmni;
		linux_shminfo64.shmseg = linux_shminfo->shmseg;
		linux_shminfo64.shmall = linux_shminfo->shmall;

		return (copyout(&linux_shminfo64, uaddr,
		    sizeof(linux_shminfo64)));
	} else {
		return (copyout(linux_shminfo, uaddr, sizeof(*linux_shminfo)));
	}
}

/*
 * MPSAFE
 */
int
linux_semop(struct linux_semop_args *args)
{
	struct semop_args bsd_args;
	int error;

	bsd_args.sysmsg_result = 0;
	bsd_args.semid = args->semid;
	bsd_args.sops = (struct sembuf *)args->tsops;
	bsd_args.nsops = args->nsops;
	error = sys_semop(&bsd_args);
	args->sysmsg_result = bsd_args.sysmsg_result;
	return(error);
}

/*
 * MPSAFE
 */
int
linux_semget(struct linux_semget_args *args)
{
	struct semget_args bsd_args;
	int error;

	bsd_args.sysmsg_result = 0;
	bsd_args.key = args->key;
	bsd_args.nsems = args->nsems;
	bsd_args.semflg = args->semflg;
	error = sys_semget(&bsd_args);
	args->sysmsg_result = bsd_args.sysmsg_result;
	return(error);
}

/*
 * MPSAFE
 */
int
linux_semctl(struct linux_semctl_args *args)
{
	struct l_semid_ds linux_semid;
	struct __semctl_args bsd_args;
	struct l_seminfo linux_seminfo;
	int error;
	union semun *unptr;
	caddr_t sg;

	sg = stackgap_init();

	/* Make sure the arg parameter can be copied in. */
	unptr = stackgap_alloc(&sg, sizeof(union semun));
	bcopy(&args->arg, unptr, sizeof(union semun));

	bsd_args.sysmsg_result = 0;
	bsd_args.semid = args->semid;
	bsd_args.semnum = args->semnum;
	bsd_args.arg = unptr;

	switch (args->cmd & ~LINUX_IPC_64) {
	case LINUX_IPC_RMID:
		bsd_args.cmd = IPC_RMID;
		break;
	case LINUX_GETNCNT:
		bsd_args.cmd = GETNCNT;
		break;
	case LINUX_GETPID:
		bsd_args.cmd = GETPID;
		break;
	case LINUX_GETVAL:
		bsd_args.cmd = GETVAL;
		break;
	case LINUX_GETZCNT:
		bsd_args.cmd = GETZCNT;
		break;
	case LINUX_SETVAL:
		bsd_args.cmd = SETVAL;
		break;
	case LINUX_IPC_SET:
		bsd_args.cmd = IPC_SET;
		error = linux_semid_pullup(args->cmd & LINUX_IPC_64,
		    &linux_semid, (caddr_t)args->arg.buf);
		if (error)
			return (error);
		unptr->buf = stackgap_alloc(&sg, sizeof(struct semid_ds));
		linux_to_bsd_semid_ds(&linux_semid, unptr->buf);
		break;
	case LINUX_IPC_STAT:
	case LINUX_SEM_STAT:
		if ((args->cmd & ~LINUX_IPC_64) == LINUX_IPC_STAT)
			bsd_args.cmd = IPC_STAT;
		else
			bsd_args.cmd = SEM_STAT;
		unptr->buf = stackgap_alloc(&sg, sizeof(struct semid_ds));
		error = sys___semctl(&bsd_args);
		if (error)
			return error;
		args->sysmsg_result = IXSEQ_TO_IPCID(bsd_args.semid, 
							unptr->buf->sem_perm);
		bsd_to_linux_semid_ds(unptr->buf, &linux_semid);
		error = linux_semid_pushdown(args->cmd & LINUX_IPC_64,
		    &linux_semid, (caddr_t)(args->arg.buf));
		if (error == 0)
			args->sysmsg_iresult = ((args->cmd & ~LINUX_IPC_64) == SEM_STAT)
			    ? bsd_args.sysmsg_result : 0;
		return (error);
	case LINUX_IPC_INFO:
	case LINUX_SEM_INFO:
		error = copyin((caddr_t)args->arg.buf, &linux_seminfo, 
						sizeof(linux_seminfo) );
		if (error)
			return error;
		bcopy(&seminfo, &linux_seminfo, sizeof(linux_seminfo) );
/* XXX BSD equivalent?
#define used_semids 10
#define used_sems 10
		linux_seminfo.semusz = used_semids;
		linux_seminfo.semaem = used_sems;
*/
		error = copyout((caddr_t)&linux_seminfo, (caddr_t)args->arg.buf,
						sizeof(linux_seminfo) );
		if (error)
			return error;
		args->sysmsg_result = seminfo.semmni;
		return 0;			/* No need for __semctl call */
	case LINUX_GETALL:
		bsd_args.cmd = GETALL;
		break;
	case LINUX_SETALL:
		bsd_args.cmd = SETALL;
		break;
	default:
		uprintf("linux: 'ipc' type=%d not implemented\n", args->cmd & ~LINUX_IPC_64);
		return EINVAL;
	}
	error = sys___semctl(&bsd_args);
	args->sysmsg_result = bsd_args.sysmsg_result;
	return(error);
}

/*
 * MPSAFE
 */
int
linux_msgsnd(struct linux_msgsnd_args *args)
{
    struct msgsnd_args bsd_args;
    int error;

    if ((l_long)args->msgsz < 0 || args->msgsz > (l_long)msginfo.msgmax)
        return (EINVAL);
    bsd_args.sysmsg_result = 0;
    bsd_args.msqid = args->msqid;
    bsd_args.msgp = args->msgp;
    bsd_args.msgsz = args->msgsz;
    bsd_args.msgflg = args->msgflg;
    error = sys_msgsnd(&bsd_args);
    args->sysmsg_result = bsd_args.sysmsg_result;
    return(error);
}

/*
 * MPSAFE
 */
int
linux_msgrcv(struct linux_msgrcv_args *args)
{
    struct msgrcv_args bsd_args; 
    int error;
    if ((l_long)args->msgsz < 0 || args->msgsz > (l_long)msginfo.msgmax)
	    return (EINVAL);
    bsd_args.sysmsg_result = 0;
    bsd_args.msqid = args->msqid;
    bsd_args.msgp = args->msgp;
    bsd_args.msgsz = args->msgsz;
    bsd_args.msgtyp = 0; /* XXX - args->msgtyp; */
    bsd_args.msgflg = args->msgflg;
    error = sys_msgrcv(&bsd_args);
    args->sysmsg_result = bsd_args.sysmsg_result;
    return(error);
}

/*
 * MPSAFE
 */
int
linux_msgget(struct linux_msgget_args *args)
{
    struct msgget_args bsd_args;
    int error;

    bsd_args.sysmsg_result = 0;
    bsd_args.key = args->key;
    bsd_args.msgflg = args->msgflg;
    error = sys_msgget(&bsd_args);
    args->sysmsg_result = bsd_args.sysmsg_result;
    return(error);
}

/*
 * MPSAFE
 */
int
linux_msgctl(struct linux_msgctl_args *args)
{
    struct msgctl_args bsd_args;
    struct l_msqid_ds linux_msqid;
    int error, bsd_cmd;
    struct msqid_ds *unptr;
    caddr_t sg;
   
   sg = stackgap_init();
	/* Make sure the arg parameter can be copied in. */
	unptr = stackgap_alloc(&sg, sizeof(struct msqid_ds));
	bcopy(&args->buf, unptr, sizeof(struct msqid_ds));

    bsd_cmd = args->cmd & ~LINUX_IPC_64;
    bsd_args.sysmsg_result = 0;
    bsd_args.msqid = args->msqid;
    bsd_args.cmd = bsd_cmd;
    bsd_args.buf = unptr;
    switch(bsd_cmd) {
    case LINUX_IPC_INFO:
    case LINUX_MSG_INFO: {
	struct l_msginfo linux_msginfo;

	/*
	 * XXX MSG_INFO uses the same data structure but returns different
	 * dynamic counters in msgpool, msgmap, and msgtql fields.
	 */
	linux_msginfo.msgpool = (long)msginfo.msgmni *
	    (long)msginfo.msgmnb / 1024L;	/* XXX MSG_INFO. */
	linux_msginfo.msgmap = msginfo.msgmnb;	/* XXX MSG_INFO. */
	linux_msginfo.msgmax = msginfo.msgmax;
	linux_msginfo.msgmnb = msginfo.msgmnb;
	linux_msginfo.msgmni = msginfo.msgmni;
	linux_msginfo.msgssz = msginfo.msgssz;
	linux_msginfo.msgtql = msginfo.msgtql;	/* XXX MSG_INFO. */
	linux_msginfo.msgseg = msginfo.msgseg;
	error = copyout(&linux_msginfo, PTRIN(args->buf),
	    sizeof(linux_msginfo));
	if (error == 0)
	    args->sysmsg_iresult = msginfo.msgmni;	/* XXX */

	return (error);
    }

/*
 * TODO: implement this
 * case LINUX_MSG_STAT:
 */
    case LINUX_IPC_STAT:
	/* NOTHING */
	break;

    case LINUX_IPC_SET:
	error = linux_msqid_pullup(args->cmd & LINUX_IPC_64,
	    &linux_msqid, (caddr_t)(args->buf));
	if (error)
	    return (error);
	linux_to_bsd_msqid_ds(&linux_msqid, unptr);
	break;

    case LINUX_IPC_RMID:
	/* NOTHING */
	break;

    default:
	return (EINVAL);
	break;
    }

    error = sys_msgctl(&bsd_args);
    if (error != 0)
	if (bsd_cmd != LINUX_IPC_RMID || error != EINVAL)
	    return (error);
    if (bsd_cmd == LINUX_IPC_STAT) {
	bsd_to_linux_msqid_ds(bsd_args.buf, &linux_msqid);
	return (linux_msqid_pushdown(args->cmd & LINUX_IPC_64,
	  &linux_msqid, PTRIN(args->buf)));
    }
    args->sysmsg_result = bsd_args.sysmsg_result;
    return ((args->cmd == LINUX_IPC_RMID && error == EINVAL) ? 0 : error);




}

/*
 * MPSAFE
 */
int
linux_shmat(struct linux_shmat_args *args)
{
    struct shmat_args bsd_args;
    int error;

    bsd_args.sysmsg_result = 0;
    bsd_args.shmid = args->shmid;
    bsd_args.shmaddr = args->shmaddr;
    bsd_args.shmflg = args->shmflg;
    if ((error = sys_shmat(&bsd_args)))
	return error;
#ifdef __i386__
    if ((error = copyout(&bsd_args.sysmsg_lresult, (caddr_t)args->raddr, sizeof(l_ulong))))
	return error;
    args->sysmsg_result = 0;
#else
    args->sysmsg_result = bsd_args.sysmsg_result;
#endif
    return 0;
}

/*
 * MPSAFE
 */
int
linux_shmdt(struct linux_shmdt_args *args)
{
    struct shmdt_args bsd_args;
    int error;

    bsd_args.sysmsg_result = 0;
    bsd_args.shmaddr = args->shmaddr;
    error = sys_shmdt(&bsd_args);
    args->sysmsg_result = bsd_args.sysmsg_result;
    return(error);
}

/*
 * MPSAFE
 */
int
linux_shmget(struct linux_shmget_args *args)
{
    struct shmget_args bsd_args;
    int error;

    bsd_args.sysmsg_result = 0;
    bsd_args.key = args->key;
    bsd_args.size = args->size;
    bsd_args.shmflg = args->shmflg;
    error = sys_shmget(&bsd_args);
    args->sysmsg_result = bsd_args.sysmsg_result;
    return(error);
}

/*
 * MPSAFE
 */
extern int shm_nused;
int
linux_shmctl(struct linux_shmctl_args *args)
{
    struct l_shmid_ds linux_shmid;
    struct l_shminfo linux_shminfo;
    struct l_shm_info linux_shm_info;
    struct shmctl_args bsd_args;
    int error;
    caddr_t sg = stackgap_init();

    bsd_args.sysmsg_result = 0;
    switch (args->cmd & ~LINUX_IPC_64) {
    case LINUX_IPC_STAT:
    case LINUX_SHM_STAT:
	bsd_args.shmid = args->shmid;
	bsd_args.cmd = IPC_STAT;
	bsd_args.buf = (struct shmid_ds*)stackgap_alloc(&sg, sizeof(struct shmid_ds));
	if ((error = sys_shmctl(&bsd_args)))
	    return error;
	bsd_to_linux_shmid_ds(bsd_args.buf, &linux_shmid);
	args->sysmsg_result = bsd_args.sysmsg_result;
	return (linux_shmid_pushdown(args->cmd & LINUX_IPC_64,
	     &linux_shmid, PTRIN(args->buf)));

    case LINUX_IPC_SET:
	if ((error = linux_shmid_pullup(args->cmd & LINUX_IPC_64,
	  &linux_shmid, PTRIN(args->buf))))
	    return error;
	bsd_args.buf = (struct shmid_ds*)stackgap_alloc(&sg, sizeof(struct shmid_ds));
	linux_to_bsd_shmid_ds(&linux_shmid, bsd_args.buf);
	bsd_args.shmid = args->shmid;
	bsd_args.cmd = IPC_SET;
	break;
    case LINUX_IPC_RMID:
	bsd_args.shmid = args->shmid;
	bsd_args.cmd = IPC_RMID;
	if (args->buf == NULL)
	    bsd_args.buf = NULL;
	else {
	    if ((error = linux_shmid_pullup(args->cmd & LINUX_IPC_64,
		    &linux_shmid, PTRIN(args->buf))))
		return error;
	    bsd_args.buf = (struct shmid_ds*)stackgap_alloc(&sg, sizeof(struct shmid_ds));
	    linux_to_bsd_shmid_ds(&linux_shmid, bsd_args.buf);
	}
	break;
    case LINUX_IPC_INFO:
	bsd_to_linux_shminfo(&shminfo, &linux_shminfo);
	return (linux_shminfo_pushdown(args->cmd & LINUX_IPC_64,
	    &linux_shminfo, PTRIN(args->buf)));
	break;
    case LINUX_SHM_INFO:
	linux_shm_info.used_ids = shm_nused;
	linux_shm_info.shm_tot = 0;
	linux_shm_info.shm_rss = 0;
	linux_shm_info.shm_swp = 0;
	linux_shm_info.swap_attempts = 0;
	linux_shm_info.swap_successes = 0;
	return copyout(&linux_shm_info, PTRIN(args->buf),
	    sizeof(struct l_shm_info));
    
    case LINUX_SHM_LOCK:
    case LINUX_SHM_UNLOCK:
    default:
	uprintf("linux: 'ipc' type=%d not implemented\n", args->cmd & ~LINUX_IPC_64);
	return EINVAL;
    }
    error = sys_shmctl(&bsd_args);
    args->sysmsg_result = bsd_args.sysmsg_result;
    return(error);
}
