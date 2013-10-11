/*-
 * Copyright (c) 1982, 1986, 1991, 1993
 *	The Regents of the University of California.  All rights reserved.
 * (c) UNIX System Laboratories, Inc.
 * All or some portions of this file are derived from material licensed
 * to the University of California by American Telephone and Telegraph
 * Co. or Unix System Laboratories, Inc. and are reproduced herein with
 * the permission of UNIX System Laboratories, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)kern_resource.c	8.5 (Berkeley) 1/21/94
 * $FreeBSD: src/sys/kern/kern_resource.c,v 1.55.2.5 2001/11/03 01:41:08 ps Exp $
 */

#include "opt_compat.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/sysproto.h>
#include <sys/file.h>
#include <sys/kern_syscall.h>
#include <sys/kernel.h>
#include <sys/resourcevar.h>
#include <sys/malloc.h>
#include <sys/proc.h>
#include <sys/priv.h>
#include <sys/time.h>
#include <sys/lockf.h>

#include <vm/vm.h>
#include <vm/vm_param.h>
#include <sys/lock.h>
#include <vm/pmap.h>
#include <vm/vm_map.h>

#include <sys/thread2.h>
#include <sys/spinlock2.h>

static int donice (struct proc *chgp, int n);
static int doionice (struct proc *chgp, int n);

static MALLOC_DEFINE(M_UIDINFO, "uidinfo", "uidinfo structures");
#define	UIHASH(uid)	(&uihashtbl[(uid) & uihash])
static struct spinlock uihash_lock;
static LIST_HEAD(uihashhead, uidinfo) *uihashtbl;
static u_long uihash;		/* size of hash table - 1 */

static struct uidinfo	*uicreate (uid_t uid);
static struct uidinfo	*uilookup (uid_t uid);

/*
 * Resource controls and accounting.
 */

struct getpriority_info {
	int low;
	int who;
};

static int getpriority_callback(struct proc *p, void *data);

/*
 * MPALMOSTSAFE
 */
int
sys_getpriority(struct getpriority_args *uap)
{
	struct getpriority_info info;
	thread_t curtd = curthread;
	struct proc *curp = curproc;
	struct proc *p;
	struct pgrp *pg;
	int low = PRIO_MAX + 1;
	int error;

	switch (uap->which) {
	case PRIO_PROCESS:
		if (uap->who == 0) {
			low = curp->p_nice;
		} else {
			p = pfind(uap->who);
			if (p) {
				lwkt_gettoken_shared(&p->p_token);
				if (PRISON_CHECK(curtd->td_ucred, p->p_ucred))
					low = p->p_nice;
				lwkt_reltoken(&p->p_token);
				PRELE(p);
			}
		}
		break;
	case PRIO_PGRP: 
		if (uap->who == 0) {
			lwkt_gettoken_shared(&curp->p_token);
			pg = curp->p_pgrp;
			pgref(pg);
			lwkt_reltoken(&curp->p_token);
		} else if ((pg = pgfind(uap->who)) == NULL) {
			break;
		} /* else ref held from pgfind */

		lwkt_gettoken_shared(&pg->pg_token);
		LIST_FOREACH(p, &pg->pg_members, p_pglist) {
			if (PRISON_CHECK(curtd->td_ucred, p->p_ucred) &&
			    p->p_nice < low) {
				low = p->p_nice;
			}
		}
		lwkt_reltoken(&pg->pg_token);
		pgrel(pg);
		break;
	case PRIO_USER:
		if (uap->who == 0)
			uap->who = curtd->td_ucred->cr_uid;
		info.low = low;
		info.who = uap->who;
		allproc_scan(getpriority_callback, &info);
		low = info.low;
		break;

	default:
		error = EINVAL;
		goto done;
	}
	if (low == PRIO_MAX + 1) {
		error = ESRCH;
		goto done;
	}
	uap->sysmsg_result = low;
	error = 0;
done:
	return (error);
}

/*
 * Figure out the current lowest nice priority for processes owned
 * by the specified user.
 */
static
int
getpriority_callback(struct proc *p, void *data)
{
	struct getpriority_info *info = data;

	lwkt_gettoken_shared(&p->p_token);
	if (PRISON_CHECK(curthread->td_ucred, p->p_ucred) &&
	    p->p_ucred->cr_uid == info->who &&
	    p->p_nice < info->low) {
		info->low = p->p_nice;
	}
	lwkt_reltoken(&p->p_token);
	return(0);
}

struct setpriority_info {
	int prio;
	int who;
	int error;
	int found;
};

static int setpriority_callback(struct proc *p, void *data);

/*
 * MPALMOSTSAFE
 */
int
sys_setpriority(struct setpriority_args *uap)
{
	struct setpriority_info info;
	thread_t curtd = curthread;
	struct proc *curp = curproc;
	struct proc *p;
	struct pgrp *pg;
	int found = 0, error = 0;

	switch (uap->which) {
	case PRIO_PROCESS:
		if (uap->who == 0) {
			lwkt_gettoken(&curp->p_token);
			error = donice(curp, uap->prio);
			found++;
			lwkt_reltoken(&curp->p_token);
		} else {
			p = pfind(uap->who);
			if (p) {
				lwkt_gettoken(&p->p_token);
				if (PRISON_CHECK(curtd->td_ucred, p->p_ucred)) {
					error = donice(p, uap->prio);
					found++;
				}
				lwkt_reltoken(&p->p_token);
				PRELE(p);
			}
		}
		break;
	case PRIO_PGRP: 
		if (uap->who == 0) {
			lwkt_gettoken_shared(&curp->p_token);
			pg = curp->p_pgrp;
			pgref(pg);
			lwkt_reltoken(&curp->p_token);
		} else if ((pg = pgfind(uap->who)) == NULL) {
			break;
		} /* else ref held from pgfind */

		lwkt_gettoken(&pg->pg_token);
restart:
		LIST_FOREACH(p, &pg->pg_members, p_pglist) {
			PHOLD(p);
			lwkt_gettoken(&p->p_token);
			if (p->p_pgrp == pg &&
			    PRISON_CHECK(curtd->td_ucred, p->p_ucred)) {
				error = donice(p, uap->prio);
				found++;
			}
			lwkt_reltoken(&p->p_token);
			if (p->p_pgrp != pg) {
				PRELE(p);
				goto restart;
			}
			PRELE(p);
		}
		lwkt_reltoken(&pg->pg_token);
		pgrel(pg);
		break;
	case PRIO_USER:
		if (uap->who == 0)
			uap->who = curtd->td_ucred->cr_uid;
		info.prio = uap->prio;
		info.who = uap->who;
		info.error = 0;
		info.found = 0;
		allproc_scan(setpriority_callback, &info);
		error = info.error;
		found = info.found;
		break;
	default:
		error = EINVAL;
		found = 1;
		break;
	}

	if (found == 0)
		error = ESRCH;
	return (error);
}

static
int
setpriority_callback(struct proc *p, void *data)
{
	struct setpriority_info *info = data;
	int error;

	lwkt_gettoken(&p->p_token);
	if (p->p_ucred->cr_uid == info->who &&
	    PRISON_CHECK(curthread->td_ucred, p->p_ucred)) {
		error = donice(p, info->prio);
		if (error)
			info->error = error;
		++info->found;
	}
	lwkt_reltoken(&p->p_token);
	return(0);
}

/*
 * Caller must hold chgp->p_token
 */
static int
donice(struct proc *chgp, int n)
{
	struct ucred *cr = curthread->td_ucred;
	struct lwp *lp;

	if (cr->cr_uid && cr->cr_ruid &&
	    cr->cr_uid != chgp->p_ucred->cr_uid &&
	    cr->cr_ruid != chgp->p_ucred->cr_uid)
		return (EPERM);
	if (n > PRIO_MAX)
		n = PRIO_MAX;
	if (n < PRIO_MIN)
		n = PRIO_MIN;
	if (n < chgp->p_nice && priv_check_cred(cr, PRIV_SCHED_SETPRIORITY, 0))
		return (EACCES);
	chgp->p_nice = n;
	FOREACH_LWP_IN_PROC(lp, chgp) {
		LWPHOLD(lp);
		chgp->p_usched->resetpriority(lp);
		LWPRELE(lp);
	}
	return (0);
}


struct ioprio_get_info {
	int high;
	int who;
};

static int ioprio_get_callback(struct proc *p, void *data);

/*
 * MPALMOSTSAFE
 */
int
sys_ioprio_get(struct ioprio_get_args *uap)
{
	struct ioprio_get_info info;
	thread_t curtd = curthread;
	struct proc *curp = curproc;
	struct proc *p;
	struct pgrp *pg;
	int high = IOPRIO_MIN-2;
	int error;

	switch (uap->which) {
	case PRIO_PROCESS:
		if (uap->who == 0) {
			high = curp->p_ionice;
		} else {
			p = pfind(uap->who);
			if (p) {
				lwkt_gettoken_shared(&p->p_token);
				if (PRISON_CHECK(curtd->td_ucred, p->p_ucred))
					high = p->p_ionice;
				lwkt_reltoken(&p->p_token);
				PRELE(p);
			}
		}
		break;
	case PRIO_PGRP:
		if (uap->who == 0) {
			lwkt_gettoken_shared(&curp->p_token);
			pg = curp->p_pgrp;
			pgref(pg);
			lwkt_reltoken(&curp->p_token);
		} else if ((pg = pgfind(uap->who)) == NULL) {
			break;
		} /* else ref held from pgfind */

		lwkt_gettoken_shared(&pg->pg_token);
		LIST_FOREACH(p, &pg->pg_members, p_pglist) {
			if (PRISON_CHECK(curtd->td_ucred, p->p_ucred) &&
			    p->p_nice > high)
				high = p->p_ionice;
		}
		lwkt_reltoken(&pg->pg_token);
		pgrel(pg);
		break;
	case PRIO_USER:
		if (uap->who == 0)
			uap->who = curtd->td_ucred->cr_uid;
		info.high = high;
		info.who = uap->who;
		allproc_scan(ioprio_get_callback, &info);
		high = info.high;
		break;
	default:
		error = EINVAL;
		goto done;
	}
	if (high == IOPRIO_MIN-2) {
		error = ESRCH;
		goto done;
	}
	uap->sysmsg_result = high;
	error = 0;
done:
	return (error);
}

/*
 * Figure out the current lowest nice priority for processes owned
 * by the specified user.
 */
static
int
ioprio_get_callback(struct proc *p, void *data)
{
	struct ioprio_get_info *info = data;

	lwkt_gettoken_shared(&p->p_token);
	if (PRISON_CHECK(curthread->td_ucred, p->p_ucred) &&
	    p->p_ucred->cr_uid == info->who &&
	    p->p_ionice > info->high) {
		info->high = p->p_ionice;
	}
	lwkt_reltoken(&p->p_token);
	return(0);
}


struct ioprio_set_info {
	int prio;
	int who;
	int error;
	int found;
};

static int ioprio_set_callback(struct proc *p, void *data);

/*
 * MPALMOSTSAFE
 */
int
sys_ioprio_set(struct ioprio_set_args *uap)
{
	struct ioprio_set_info info;
	thread_t curtd = curthread;
	struct proc *curp = curproc;
	struct proc *p;
	struct pgrp *pg;
	int found = 0, error = 0;

	switch (uap->which) {
	case PRIO_PROCESS:
		if (uap->who == 0) {
			lwkt_gettoken(&curp->p_token);
			error = doionice(curp, uap->prio);
			lwkt_reltoken(&curp->p_token);
			found++;
		} else {
			p = pfind(uap->who);
			if (p) {
				lwkt_gettoken(&p->p_token);
				if (PRISON_CHECK(curtd->td_ucred, p->p_ucred)) {
					error = doionice(p, uap->prio);
					found++;
				}
				lwkt_reltoken(&p->p_token);
				PRELE(p);
			}
		}
		break;
	case PRIO_PGRP:
		if (uap->who == 0) {
			lwkt_gettoken_shared(&curp->p_token);
			pg = curp->p_pgrp;
			pgref(pg);
			lwkt_reltoken(&curp->p_token);
		} else if ((pg = pgfind(uap->who)) == NULL) {
			break;
		} /* else ref held from pgfind */

		lwkt_gettoken(&pg->pg_token);
restart:
		LIST_FOREACH(p, &pg->pg_members, p_pglist) {
			PHOLD(p);
			lwkt_gettoken(&p->p_token);
			if (p->p_pgrp == pg &&
			    PRISON_CHECK(curtd->td_ucred, p->p_ucred)) {
				error = doionice(p, uap->prio);
				found++;
			}
			lwkt_reltoken(&p->p_token);
			if (p->p_pgrp != pg) {
				PRELE(p);
				goto restart;
			}
			PRELE(p);
		}
		lwkt_reltoken(&pg->pg_token);
		pgrel(pg);
		break;
	case PRIO_USER:
		if (uap->who == 0)
			uap->who = curtd->td_ucred->cr_uid;
		info.prio = uap->prio;
		info.who = uap->who;
		info.error = 0;
		info.found = 0;
		allproc_scan(ioprio_set_callback, &info);
		error = info.error;
		found = info.found;
		break;
	default:
		error = EINVAL;
		found = 1;
		break;
	}

	if (found == 0)
		error = ESRCH;
	return (error);
}

static
int
ioprio_set_callback(struct proc *p, void *data)
{
	struct ioprio_set_info *info = data;
	int error;

	lwkt_gettoken(&p->p_token);
	if (p->p_ucred->cr_uid == info->who &&
	    PRISON_CHECK(curthread->td_ucred, p->p_ucred)) {
		error = doionice(p, info->prio);
		if (error)
			info->error = error;
		++info->found;
	}
	lwkt_reltoken(&p->p_token);
	return(0);
}

int
doionice(struct proc *chgp, int n)
{
	struct ucred *cr = curthread->td_ucred;

	if (cr->cr_uid && cr->cr_ruid &&
	    cr->cr_uid != chgp->p_ucred->cr_uid &&
	    cr->cr_ruid != chgp->p_ucred->cr_uid)
		return (EPERM);
	if (n > IOPRIO_MAX)
		n = IOPRIO_MAX;
	if (n < IOPRIO_MIN)
		n = IOPRIO_MIN;
	if (n < chgp->p_ionice &&
	    priv_check_cred(cr, PRIV_SCHED_SETPRIORITY, 0))
		return (EACCES);
	chgp->p_ionice = n;

	return (0);

}

/*
 * MPALMOSTSAFE
 */
int
sys_lwp_rtprio(struct lwp_rtprio_args *uap)
{
	struct ucred *cr = curthread->td_ucred;
	struct proc *p;
	struct lwp *lp;
	struct rtprio rtp;
	int error;

	error = copyin(uap->rtp, &rtp, sizeof(struct rtprio));
	if (error)
		return error;
	if (uap->pid < 0)
		return EINVAL;

	if (uap->pid == 0) {
		p = curproc;
		PHOLD(p);
	} else {
		p = pfind(uap->pid);
	}
	if (p == NULL) {
		error = ESRCH;
		goto done;
	}
	lwkt_gettoken(&p->p_token);

	if (uap->tid < -1) {
		error = EINVAL;
		goto done;
	}
	if (uap->tid == -1) {
		/*
		 * sadly, tid can be 0 so we can't use 0 here
		 * like sys_rtprio()
		 */
		lp = curthread->td_lwp;
	} else {
		lp = lwp_rb_tree_RB_LOOKUP(&p->p_lwp_tree, uap->tid);
		if (lp == NULL) {
			error = ESRCH;
			goto done;
		}
	}

	switch (uap->function) {
	case RTP_LOOKUP:
		error = copyout(&lp->lwp_rtprio, uap->rtp,
				sizeof(struct rtprio));
		break;
	case RTP_SET:
		if (cr->cr_uid && cr->cr_ruid &&
		    cr->cr_uid != p->p_ucred->cr_uid &&
		    cr->cr_ruid != p->p_ucred->cr_uid) {
			error = EPERM;
			break;
		}
		/* disallow setting rtprio in most cases if not superuser */
		if (priv_check_cred(cr, PRIV_SCHED_RTPRIO, 0)) {
			/* can't set someone else's */
			if (uap->pid) { /* XXX */
				error = EPERM;
				break;
			}
			/* can't set realtime priority */
/*
 * Realtime priority has to be restricted for reasons which should be
 * obvious. However, for idle priority, there is a potential for
 * system deadlock if an idleprio process gains a lock on a resource
 * that other processes need (and the idleprio process can't run
 * due to a CPU-bound normal process). Fix me! XXX
 */
 			if (RTP_PRIO_IS_REALTIME(rtp.type)) {
				error = EPERM;
				break;
			}
		}
		switch (rtp.type) {
#ifdef RTP_PRIO_FIFO
		case RTP_PRIO_FIFO:
#endif
		case RTP_PRIO_REALTIME:
		case RTP_PRIO_NORMAL:
		case RTP_PRIO_IDLE:
			if (rtp.prio > RTP_PRIO_MAX) {
				error = EINVAL;
			} else {
				lp->lwp_rtprio = rtp;
				error = 0;
			}
			break;
		default:
			error = EINVAL;
			break;
		}
		break;
	default:
		error = EINVAL;
		break;
	}

done:
	if (p) {
		lwkt_reltoken(&p->p_token);
		PRELE(p);
	}
	return (error);
}

/*
 * Set realtime priority
 *
 * MPALMOSTSAFE
 */
int
sys_rtprio(struct rtprio_args *uap)
{
	struct ucred *cr = curthread->td_ucred;
	struct proc *p;
	struct lwp *lp;
	struct rtprio rtp;
	int error;

	error = copyin(uap->rtp, &rtp, sizeof(struct rtprio));
	if (error)
		return (error);

	if (uap->pid == 0) {
		p = curproc;
		PHOLD(p);
	} else {
		p = pfind(uap->pid);
	}

	if (p == NULL) {
		error = ESRCH;
		goto done;
	}
	lwkt_gettoken(&p->p_token);

	/* XXX lwp */
	lp = FIRST_LWP_IN_PROC(p);
	switch (uap->function) {
	case RTP_LOOKUP:
		error = copyout(&lp->lwp_rtprio, uap->rtp,
				sizeof(struct rtprio));
		break;
	case RTP_SET:
		if (cr->cr_uid && cr->cr_ruid &&
		    cr->cr_uid != p->p_ucred->cr_uid &&
		    cr->cr_ruid != p->p_ucred->cr_uid) {
			error = EPERM;
			break;
		}
		/* disallow setting rtprio in most cases if not superuser */
		if (priv_check_cred(cr, PRIV_SCHED_RTPRIO, 0)) {
			/* can't set someone else's */
			if (uap->pid) {
				error = EPERM;
				break;
			}
			/* can't set realtime priority */
/*
 * Realtime priority has to be restricted for reasons which should be
 * obvious. However, for idle priority, there is a potential for
 * system deadlock if an idleprio process gains a lock on a resource
 * that other processes need (and the idleprio process can't run
 * due to a CPU-bound normal process). Fix me! XXX
 */
			if (RTP_PRIO_IS_REALTIME(rtp.type)) {
				error = EPERM;
				break;
			}
		}
		switch (rtp.type) {
#ifdef RTP_PRIO_FIFO
		case RTP_PRIO_FIFO:
#endif
		case RTP_PRIO_REALTIME:
		case RTP_PRIO_NORMAL:
		case RTP_PRIO_IDLE:
			if (rtp.prio > RTP_PRIO_MAX) {
				error = EINVAL;
				break;
			}
			lp->lwp_rtprio = rtp;
			error = 0;
			break;
		default:
			error = EINVAL;
			break;
		}
		break;
	default:
		error = EINVAL;
		break;
	}
done:
	if (p) {
		lwkt_reltoken(&p->p_token);
		PRELE(p);
	}

	return (error);
}

/*
 * MPSAFE
 */
int
sys_setrlimit(struct __setrlimit_args *uap)
{
	struct rlimit alim;
	int error;

	error = copyin(uap->rlp, &alim, sizeof(alim));
	if (error)
		return (error);

	error = kern_setrlimit(uap->which, &alim);

	return (error);
}

/*
 * MPSAFE
 */
int
sys_getrlimit(struct __getrlimit_args *uap)
{
	struct rlimit lim;
	int error;

	error = kern_getrlimit(uap->which, &lim);

	if (error == 0)
		error = copyout(&lim, uap->rlp, sizeof(*uap->rlp));
	return error;
}

/*
 * Transform the running time and tick information in lwp lp's thread into user,
 * system, and interrupt time usage.
 *
 * Since we are limited to statclock tick granularity this is a statisical
 * calculation which will be correct over the long haul, but should not be
 * expected to measure fine grained deltas.
 *
 * It is possible to catch a lwp in the midst of being created, so
 * check whether lwp_thread is NULL or not.
 */
void
calcru(struct lwp *lp, struct timeval *up, struct timeval *sp)
{
	struct thread *td;

	/*
	 * Calculate at the statclock level.  YYY if the thread is owned by
	 * another cpu we need to forward the request to the other cpu, or
	 * have a token to interlock the information in order to avoid racing
	 * thread destruction.
	 */
	if ((td = lp->lwp_thread) != NULL) {
		crit_enter();
		up->tv_sec = td->td_uticks / 1000000;
		up->tv_usec = td->td_uticks % 1000000;
		sp->tv_sec = td->td_sticks / 1000000;
		sp->tv_usec = td->td_sticks % 1000000;
		crit_exit();
	}
}

/*
 * Aggregate resource statistics of all lwps of a process.
 *
 * proc.p_ru keeps track of all statistics directly related to a proc.  This
 * consists of RSS usage and nswap information and aggregate numbers for all
 * former lwps of this proc.
 *
 * proc.p_cru is the sum of all stats of reaped children.
 *
 * lwp.lwp_ru contains the stats directly related to one specific lwp, meaning
 * packet, scheduler switch or page fault counts, etc.  This information gets
 * added to lwp.lwp_proc.p_ru when the lwp exits.
 */
void
calcru_proc(struct proc *p, struct rusage *ru)
{
	struct timeval upt, spt;
	long *rip1, *rip2;
	struct lwp *lp;

	*ru = p->p_ru;

	FOREACH_LWP_IN_PROC(lp, p) {
		calcru(lp, &upt, &spt);
		timevaladd(&ru->ru_utime, &upt);
		timevaladd(&ru->ru_stime, &spt);
		for (rip1 = &ru->ru_first, rip2 = &lp->lwp_ru.ru_first;
		     rip1 <= &ru->ru_last;
		     rip1++, rip2++)
			*rip1 += *rip2;
	}
}


/*
 * MPALMOSTSAFE
 */
int
sys_getrusage(struct getrusage_args *uap)
{
	struct proc *p = curproc;
	struct rusage ru;
	struct rusage *rup;
	int error;

	lwkt_gettoken(&p->p_token);

	switch (uap->who) {
	case RUSAGE_SELF:
		rup = &ru;
		calcru_proc(p, rup);
		error = 0;
		break;
	case RUSAGE_CHILDREN:
		rup = &p->p_cru;
		error = 0;
		break;
	default:
		error = EINVAL;
		break;
	}
	lwkt_reltoken(&p->p_token);

	if (error == 0)
		error = copyout(rup, uap->rusage, sizeof(struct rusage));
	return (error);
}

void
ruadd(struct rusage *ru, struct rusage *ru2)
{
	long *ip, *ip2;
	int i;

	timevaladd(&ru->ru_utime, &ru2->ru_utime);
	timevaladd(&ru->ru_stime, &ru2->ru_stime);
	if (ru->ru_maxrss < ru2->ru_maxrss)
		ru->ru_maxrss = ru2->ru_maxrss;
	ip = &ru->ru_first; ip2 = &ru2->ru_first;
	for (i = &ru->ru_last - &ru->ru_first; i >= 0; i--)
		*ip++ += *ip2++;
}

/*
 * Find the uidinfo structure for a uid.  This structure is used to
 * track the total resource consumption (process count, socket buffer
 * size, etc.) for the uid and impose limits.
 */
void
uihashinit(void)
{
	spin_init(&uihash_lock);
	uihashtbl = hashinit(maxproc / 16, M_UIDINFO, &uihash);
}

/*
 * NOTE: Must be called with uihash_lock held
 *
 * MPSAFE
 */
static struct uidinfo *
uilookup(uid_t uid)
{
	struct	uihashhead *uipp;
	struct	uidinfo *uip;

	uipp = UIHASH(uid);
	LIST_FOREACH(uip, uipp, ui_hash) {
		if (uip->ui_uid == uid)
			break;
	}
	return (uip);
}

/*
 * Helper function to creat ea uid that could not be found.
 * This function will properly deal with races.
 *
 * MPSAFE
 */
static struct uidinfo *
uicreate(uid_t uid)
{
	struct	uidinfo *uip, *tmp;

	/*
	 * Allocate space and check for a race
	 */
	uip = kmalloc(sizeof(*uip), M_UIDINFO, M_WAITOK|M_ZERO);

	/*
	 * Initialize structure and enter it into the hash table
	 */
	spin_init(&uip->ui_lock);
	uip->ui_uid = uid;
	uip->ui_ref = 1;	/* we're returning a ref */
	varsymset_init(&uip->ui_varsymset, NULL);

	/*
	 * Somebody may have already created the uidinfo for this
	 * uid. If so, return that instead.
	 */
	spin_lock(&uihash_lock);
	tmp = uilookup(uid);
	if (tmp != NULL) {
		uihold(tmp);
		spin_unlock(&uihash_lock);

		spin_uninit(&uip->ui_lock);
		varsymset_clean(&uip->ui_varsymset);
		kfree(uip, M_UIDINFO);
		uip = tmp;
	} else {
		LIST_INSERT_HEAD(UIHASH(uid), uip, ui_hash);
		spin_unlock(&uihash_lock);
	}
	return (uip);
}

/*
 *
 *
 * MPSAFE
 */
struct uidinfo *
uifind(uid_t uid)
{
	struct	uidinfo *uip;

	spin_lock(&uihash_lock);
	uip = uilookup(uid);
	if (uip == NULL) {
		spin_unlock(&uihash_lock);
		uip = uicreate(uid);
	} else {
		uihold(uip);
		spin_unlock(&uihash_lock);
	}
	return (uip);
}

/*
 * Helper funtion to remove a uidinfo whos reference count is
 * transitioning from 1->0.  The reference count is 1 on call.
 *
 * Zero is returned on success, otherwise non-zero and the
 * uiphas not been removed.
 *
 * MPSAFE
 */
static __inline int
uifree(struct uidinfo *uip)
{
	/*
	 * If we are still the only holder after acquiring the uihash_lock
	 * we can safely unlink the uip and destroy it.  Otherwise we lost
	 * a race and must fail.
	 */
	spin_lock(&uihash_lock);
	if (uip->ui_ref != 1) {
		spin_unlock(&uihash_lock);
		return(-1);
	}
	LIST_REMOVE(uip, ui_hash);
	spin_unlock(&uihash_lock);

	/*
	 * The uip is now orphaned and we can destroy it at our
	 * leisure.
	 */
	if (uip->ui_sbsize != 0)
		kprintf("freeing uidinfo: uid = %d, sbsize = %jd\n",
		    uip->ui_uid, (intmax_t)uip->ui_sbsize);
	if (uip->ui_proccnt != 0)
		kprintf("freeing uidinfo: uid = %d, proccnt = %ld\n",
		    uip->ui_uid, uip->ui_proccnt);
	
	varsymset_clean(&uip->ui_varsymset);
	lockuninit(&uip->ui_varsymset.vx_lock);
	spin_uninit(&uip->ui_lock);
	kfree(uip, M_UIDINFO);
	return(0);
}

/*
 * MPSAFE
 */
void
uihold(struct uidinfo *uip)
{
	atomic_add_int(&uip->ui_ref, 1);
	KKASSERT(uip->ui_ref >= 0);
}

/*
 * NOTE: It is important for us to not drop the ref count to 0
 *	 because this can cause a 2->0/2->0 race with another
 *	 concurrent dropper.  Losing the race in that situation
 *	 can cause uip to become stale for one of the other
 *	 threads.
 *
 * MPSAFE
 */
void
uidrop(struct uidinfo *uip)
{
	int ref;

	KKASSERT(uip->ui_ref > 0);

	for (;;) {
		ref = uip->ui_ref;
		cpu_ccfence();
		if (ref == 1) {
			if (uifree(uip) == 0)
				break;
		} else if (atomic_cmpset_int(&uip->ui_ref, ref, ref - 1)) {
			break;
		}
		/* else retry */
	}
}

void
uireplace(struct uidinfo **puip, struct uidinfo *nuip)
{
	uidrop(*puip);
	*puip = nuip;
}

/*
 * Change the count associated with number of processes
 * a given user is using.  When 'max' is 0, don't enforce a limit
 */
int
chgproccnt(struct uidinfo *uip, int diff, int max)
{
	int ret;
	spin_lock(&uip->ui_lock);
	/* don't allow them to exceed max, but allow subtraction */
	if (diff > 0 && uip->ui_proccnt + diff > max && max != 0) {
		ret = 0;
	} else {
		uip->ui_proccnt += diff;
		if (uip->ui_proccnt < 0)
			kprintf("negative proccnt for uid = %d\n", uip->ui_uid);
		ret = 1;
	}
	spin_unlock(&uip->ui_lock);
	return ret;
}

/*
 * Change the total socket buffer size a user has used.
 */
int
chgsbsize(struct uidinfo *uip, u_long *hiwat, u_long to, rlim_t max)
{
	rlim_t new;

	spin_lock(&uip->ui_lock);
	new = uip->ui_sbsize + to - *hiwat;
	KKASSERT(new >= 0);

	/*
	 * If we are trying to increase the socket buffer size
	 * Scale down the hi water mark when we exceed the user's
	 * allowed socket buffer space.
	 *
	 * We can't scale down too much or we will blow up atomic packet
	 * operations.
	 */
	if (to > *hiwat && to > MCLBYTES && new > max) {
		to = to * max / new;
		if (to < MCLBYTES)
			to = MCLBYTES;
	}
	uip->ui_sbsize = new;
	*hiwat = to;
	spin_unlock(&uip->ui_lock);
	return (1);
}

