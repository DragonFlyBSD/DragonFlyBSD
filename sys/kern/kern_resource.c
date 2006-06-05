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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
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
 * $DragonFly: src/sys/kern/kern_resource.c,v 1.28 2006/06/05 07:26:10 dillon Exp $
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
#include <sys/time.h>
#include <sys/lockf.h>

#include <vm/vm.h>
#include <vm/vm_param.h>
#include <sys/lock.h>
#include <vm/pmap.h>
#include <vm/vm_map.h>

#include <sys/thread2.h>

static int donice (struct proc *chgp, int n);

static MALLOC_DEFINE(M_UIDINFO, "uidinfo", "uidinfo structures");
#define	UIHASH(uid)	(&uihashtbl[(uid) & uihash])
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

int
sys_getpriority(struct getpriority_args *uap)
{
	struct getpriority_info info;
	struct proc *curp = curproc;
	struct proc *p;
	int low = PRIO_MAX + 1;

	switch (uap->which) {
	case PRIO_PROCESS:
		if (uap->who == 0)
			p = curp;
		else
			p = pfind(uap->who);
		if (p == 0)
			break;
		if (!PRISON_CHECK(curp->p_ucred, p->p_ucred))
			break;
		low = p->p_nice;
		break;

	case PRIO_PGRP: 
	{
		struct pgrp *pg;

		if (uap->who == 0)
			pg = curp->p_pgrp;
		else if ((pg = pgfind(uap->who)) == NULL)
			break;
		LIST_FOREACH(p, &pg->pg_members, p_pglist) {
			if ((PRISON_CHECK(curp->p_ucred, p->p_ucred) && p->p_nice < low))
				low = p->p_nice;
		}
		break;
	}
	case PRIO_USER:
		if (uap->who == 0)
			uap->who = curp->p_ucred->cr_uid;
		info.low = low;
		info.who = uap->who;
		allproc_scan(getpriority_callback, &info);
		low = info.low;
		break;

	default:
		return (EINVAL);
	}
	if (low == PRIO_MAX + 1)
		return (ESRCH);
	uap->sysmsg_result = low;
	return (0);
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

	if (PRISON_CHECK(curproc->p_ucred, p->p_ucred) &&
	    p->p_ucred->cr_uid == info->who &&
	    p->p_nice < info->low) {
		info->low = p->p_nice;
	}
	return(0);
}

struct setpriority_info {
	int prio;
	int who;
	int error;
	int found;
};

static int setpriority_callback(struct proc *p, void *data);

int
sys_setpriority(struct setpriority_args *uap)
{
	struct setpriority_info info;
	struct proc *curp = curproc;
	struct proc *p;
	int found = 0, error = 0;

	switch (uap->which) {
	case PRIO_PROCESS:
		if (uap->who == 0)
			p = curp;
		else
			p = pfind(uap->who);
		if (p == 0)
			break;
		if (!PRISON_CHECK(curp->p_ucred, p->p_ucred))
			break;
		error = donice(p, uap->prio);
		found++;
		break;

	case PRIO_PGRP: 
	{
		struct pgrp *pg;

		if (uap->who == 0)
			pg = curp->p_pgrp;
		else if ((pg = pgfind(uap->who)) == NULL)
			break;
		LIST_FOREACH(p, &pg->pg_members, p_pglist) {
			if (PRISON_CHECK(curp->p_ucred, p->p_ucred)) {
				error = donice(p, uap->prio);
				found++;
			}
		}
		break;
	}
	case PRIO_USER:
		if (uap->who == 0)
			uap->who = curp->p_ucred->cr_uid;
		info.prio = uap->prio;
		info.who = uap->who;
		info.error = 0;
		info.found = 0;
		allproc_scan(setpriority_callback, &info);
		error = info.error;
		found = info.found;
		break;

	default:
		return (EINVAL);
	}
	if (found == 0)
		return (ESRCH);
	return (error);
}

static
int
setpriority_callback(struct proc *p, void *data)
{
	struct setpriority_info *info = data;
	int error;

	if (p->p_ucred->cr_uid == info->who &&
	    PRISON_CHECK(curproc->p_ucred, p->p_ucred)) {
		error = donice(p, info->prio);
		if (error)
			info->error = error;
		++info->found;
	}
	return(0);
}

static int
donice(struct proc *chgp, int n)
{
	struct proc *curp = curproc;
	struct ucred *cr = curp->p_ucred;

	if (cr->cr_uid && cr->cr_ruid &&
	    cr->cr_uid != chgp->p_ucred->cr_uid &&
	    cr->cr_ruid != chgp->p_ucred->cr_uid)
		return (EPERM);
	if (n > PRIO_MAX)
		n = PRIO_MAX;
	if (n < PRIO_MIN)
		n = PRIO_MIN;
	if (n < chgp->p_nice && suser_cred(cr, 0))
		return (EACCES);
	chgp->p_nice = n;
	chgp->p_usched->resetpriority(&chgp->p_lwp);
	return (0);
}

/*
 * Set realtime priority
 */
/* ARGSUSED */
int
sys_rtprio(struct rtprio_args *uap)
{
	struct proc *curp = curproc;
	struct proc *p;
	struct ucred *cr = curp->p_ucred;
	struct rtprio rtp;
	int error;

	error = copyin(uap->rtp, &rtp, sizeof(struct rtprio));
	if (error)
		return (error);

	if (uap->pid == 0)
		p = curp;
	else
		p = pfind(uap->pid);

	if (p == 0)
		return (ESRCH);

	switch (uap->function) {
	case RTP_LOOKUP:
		return (copyout(&p->p_lwp.lwp_rtprio, uap->rtp, sizeof(struct rtprio)));
	case RTP_SET:
		if (cr->cr_uid && cr->cr_ruid &&
		    cr->cr_uid != p->p_ucred->cr_uid &&
		    cr->cr_ruid != p->p_ucred->cr_uid)
		        return (EPERM);
		/* disallow setting rtprio in most cases if not superuser */
		if (suser_cred(cr, 0)) {
			/* can't set someone else's */
			if (uap->pid)
				return (EPERM);
			/* can't set realtime priority */
/*
 * Realtime priority has to be restricted for reasons which should be
 * obvious. However, for idle priority, there is a potential for
 * system deadlock if an idleprio process gains a lock on a resource
 * that other processes need (and the idleprio process can't run
 * due to a CPU-bound normal process). Fix me! XXX
 */
 			if (RTP_PRIO_IS_REALTIME(rtp.type))
				return (EPERM);
		}
		switch (rtp.type) {
#ifdef RTP_PRIO_FIFO
		case RTP_PRIO_FIFO:
#endif
		case RTP_PRIO_REALTIME:
		case RTP_PRIO_NORMAL:
		case RTP_PRIO_IDLE:
			if (rtp.prio > RTP_PRIO_MAX)
				return (EINVAL);
			p->p_lwp.lwp_rtprio = rtp;
			return (0);
		default:
			return (EINVAL);
		}

	default:
		return (EINVAL);
	}
}

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
 * Transform the running time and tick information in proc p into user,
 * system, and interrupt time usage.
 *
 * Since we are limited to statclock tick granularity this is a statisical
 * calculation which will be correct over the long haul, but should not be
 * expected to measure fine grained deltas.
 */
void
calcru(struct proc *p, struct timeval *up, struct timeval *sp,
	struct timeval *ip)
{
	struct thread *td = p->p_thread;

	/*
	 * Calculate at the statclock level.  YYY if the thread is owned by
	 * another cpu we need to forward the request to the other cpu, or
	 * have a token to interlock the information.
	 */
	crit_enter();
	up->tv_sec = td->td_uticks / 1000000;
	up->tv_usec = td->td_uticks % 1000000;
	sp->tv_sec = td->td_sticks / 1000000;
	sp->tv_usec = td->td_sticks % 1000000;
	if (ip != NULL) {
		ip->tv_sec = td->td_iticks / 1000000;
		ip->tv_usec = td->td_iticks % 1000000;
	}
	crit_exit();
}

/* ARGSUSED */
int
sys_getrusage(struct getrusage_args *uap)
{
	struct proc *p = curproc;
	struct rusage *rup;

	switch (uap->who) {

	case RUSAGE_SELF:
		rup = &p->p_stats->p_ru;
		calcru(p, &rup->ru_utime, &rup->ru_stime, NULL);
		break;

	case RUSAGE_CHILDREN:
		rup = &p->p_stats->p_cru;
		break;

	default:
		return (EINVAL);
	}
	return (copyout((caddr_t)rup, (caddr_t)uap->rusage,
	    sizeof (struct rusage)));
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
	uihashtbl = hashinit(maxproc / 16, M_UIDINFO, &uihash);
}

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

static struct uidinfo *
uicreate(uid_t uid)
{
	struct	uidinfo *uip, *norace;

	/*
	 * Allocate space and check for a race
	 */
	MALLOC(uip, struct uidinfo *, sizeof(*uip), M_UIDINFO, M_WAITOK);
	norace = uilookup(uid);
	if (norace != NULL) {
		FREE(uip, M_UIDINFO);
		return (norace);
	}

	/*
	 * Initialize structure and enter it into the hash table
	 */
	LIST_INSERT_HEAD(UIHASH(uid), uip, ui_hash);
	uip->ui_uid = uid;
	uip->ui_proccnt = 0;
	uip->ui_sbsize = 0;
	uip->ui_ref = 0;
	uip->ui_posixlocks = 0;
	varsymset_init(&uip->ui_varsymset, NULL);
	return (uip);
}

struct uidinfo *
uifind(uid_t uid)
{
	struct	uidinfo *uip;

	uip = uilookup(uid);
	if (uip == NULL)
		uip = uicreate(uid);
	uip->ui_ref++;
	return (uip);
}

static __inline void
uifree(struct uidinfo *uip)
{
	if (uip->ui_sbsize != 0)
		/* XXX no %qd in kernel.  Truncate. */
		printf("freeing uidinfo: uid = %d, sbsize = %ld\n",
		    uip->ui_uid, (long)uip->ui_sbsize);
	if (uip->ui_proccnt != 0)
		printf("freeing uidinfo: uid = %d, proccnt = %ld\n",
		    uip->ui_uid, uip->ui_proccnt);
	LIST_REMOVE(uip, ui_hash);
	varsymset_clean(&uip->ui_varsymset);
	FREE(uip, M_UIDINFO);
}

void
uihold(struct uidinfo *uip)
{
	++uip->ui_ref;
	KKASSERT(uip->ui_ref > 0);
}

void
uidrop(struct uidinfo *uip)
{
	KKASSERT(uip->ui_ref > 0);
	if (--uip->ui_ref == 0)
		uifree(uip);
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
	/* don't allow them to exceed max, but allow subtraction */
	if (diff > 0 && uip->ui_proccnt + diff > max && max != 0)
		return (0);
	uip->ui_proccnt += diff;
	if (uip->ui_proccnt < 0)
		printf("negative proccnt for uid = %d\n", uip->ui_uid);
	return (1);
}

/*
 * Change the total socket buffer size a user has used.
 */
int
chgsbsize(struct uidinfo *uip, u_long *hiwat, u_long to, rlim_t max)
{
	rlim_t new;

	crit_enter();
	new = uip->ui_sbsize + to - *hiwat;
	/* don't allow them to exceed max, but allow subtraction */
	if (to > *hiwat && new > max) {
		crit_exit();
		return (0);
	}
	uip->ui_sbsize = new;
	*hiwat = to;
	if (uip->ui_sbsize < 0)
		printf("negative sbsize for uid = %d\n", uip->ui_uid);
	crit_exit();
	return (1);
}

