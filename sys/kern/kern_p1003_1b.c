/*
 * Copyright (c) 1996, 1997, 1998
 *	HD Associates, Inc.  All rights reserved.
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
 *	This product includes software developed by HD Associates, Inc
 * 4. Neither the name of the author nor the names of any co-contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY HD ASSOCIATES AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL HD ASSOCIATES OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD: src/sys/posix4/p1003_1b.c,v 1.5.2.2 2003/03/25 06:13:35 rwatson Exp $
 */

/* p1003_1b: Real Time common code.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/sysent.h>
#include <sys/posix4.h>
#include <sys/proc.h>
#include <sys/syslog.h>
#include <sys/module.h>
#include <sys/sysproto.h>
#include <sys/sysctl.h>
#include <sys/unistd.h>

MALLOC_DEFINE(M_P31B, "p1003.1b", "Posix 1003.1B");

/*
 * p31b_proc: Return a proc struct corresponding to a pid to operate on.
 *
 * Enforce permission policy.
 *
 * The policy is the same as for sending signals except there
 * is no notion of process groups.
 *
 * pid == 0 means my process.
 *
 * This is disabled until I've got a permission gate in again:
 * only root can do this.
 */

#if 0
/*
 * This is stolen from CANSIGNAL in kern_sig:
 *
 * Can process p, with pcred pc, do "write flavor" operations to process q?
 */
#define CAN_AFFECT(p, cr, q) \
	((cr)->cr_uid == 0 || \
	    (cr)->cr_ruid == (q)->p_ucred->cr_ruid || \
	    (cr)->cr_uid == (q)->p_ucred->cr_ruid || \
	    (cr)->cr_ruid == (q)->p_ucred->cr_uid || \
	    (cr)->cr_uid == (q)->p_ucred->cr_uid)
#else
#define CAN_AFFECT(p, cr, q) ((cr)->cr_uid == 0)
#endif

#if !defined(_KPOSIX_PRIORITY_SCHEDULING)

int syscall_not_present(const char *s);

/* The system calls return ENOSYS if an entry is called that is
 * not run-time supported.  I am also logging since some programs
 * start to use this when they shouldn't.  That will be removed if annoying.
 */
int syscall_not_present(const char *s)
{
	struct proc *p = curproc;
	log(LOG_ERR, "cmd %s pid %d tried to use non-present %s\n",
			p->p_comm, p->p_pid, s);

	/* a " return nosys(p, uap); " here causes a core dump.
	 */

	return ENOSYS;
}

/* Not configured but loadable via a module:
 */

static int sched_attach(void)
{
	return 0;
}

#define SYSCALL_NOT_PRESENT_GEN(SC) \
int sys_##SC (struct SC##_args *uap) \
{ \
	return syscall_not_present(#SC); \
}

SYSCALL_NOT_PRESENT_GEN(sched_setparam)
SYSCALL_NOT_PRESENT_GEN(sched_getparam)
SYSCALL_NOT_PRESENT_GEN(sched_setscheduler)
SYSCALL_NOT_PRESENT_GEN(sched_getscheduler)
SYSCALL_NOT_PRESENT_GEN(sched_yield)
SYSCALL_NOT_PRESENT_GEN(sched_get_priority_max)
SYSCALL_NOT_PRESENT_GEN(sched_get_priority_min)
SYSCALL_NOT_PRESENT_GEN(sched_rr_get_interval)

#else

/*
 * p31b_proc: Look up a proc from a PID.  If proc is 0 it is
 * my own proc.
 *
 * Returns a held process in *pp.
 */
static
int
p31b_proc(pid_t pid, struct proc **pp)
{
	int ret = 0;
	struct proc *p = curproc;
	struct proc *other_proc;

	if (pid == 0) {
		other_proc = p;
		if (other_proc)
			PHOLD(other_proc);
	} else {
		other_proc = pfind(pid);
		/* ref from pfind() */
	}

	if (other_proc) {
		/* Enforce permission policy.
		 */
		if (CAN_AFFECT(p, p->p_ucred, other_proc)) {
			*pp = other_proc;
			lwkt_gettoken(&other_proc->p_token);
		} else {
			*pp = NULL;
			ret = EPERM;
			PRELE(other_proc);
		}
	} else {
		*pp = NULL;
		ret = ESRCH;
	}
	return ret;
}

static
void
p31b_proc_done(struct proc *other_proc)
{
	if (other_proc) {
		lwkt_reltoken(&other_proc->p_token);
		PRELE(other_proc);
	}
}

/* Configured in kernel version:
 */
static struct ksched *ksched;

static int sched_attach(void)
{
	int ret = ksched_attach(&ksched);

	if (ret == 0)
		p31b_setcfg(CTL_P1003_1B_PRIORITY_SCHEDULING, 1);

	return ret;
}

int
sys_sched_setparam(struct sched_setparam_args *uap)
{
	struct proc *p;
	struct lwp *lp;
	struct sched_param sched_param;
	int e;

	copyin(uap->param, &sched_param, sizeof(sched_param));

	if ((e = p31b_proc(uap->pid, &p)) == 0) {
		lp = FIRST_LWP_IN_PROC(p); /* XXX lwp */
		if (lp) {
			LWPHOLD(lp);
			lwkt_gettoken(&lp->lwp_token);
			e = ksched_setparam(&uap->sysmsg_reg, ksched, lp,
				    (const struct sched_param *)&sched_param);
			lwkt_reltoken(&lp->lwp_token);
			LWPRELE(lp);
		} else {
			e = ESRCH;
		}
		p31b_proc_done(p);
	}
	return e;
}

int
sys_sched_getparam(struct sched_getparam_args *uap)
{
	struct proc *targetp;
	struct lwp *lp;
	struct sched_param sched_param;
	int e;
 
	if ((e = p31b_proc(uap->pid, &targetp)) == 0) {
		lp = FIRST_LWP_IN_PROC(targetp); /* XXX lwp */
		if (lp) {
			LWPHOLD(lp);
			lwkt_gettoken(&lp->lwp_token);
			e = ksched_getparam(&uap->sysmsg_reg, ksched,
					    lp, &sched_param);
			lwkt_reltoken(&lp->lwp_token);
			LWPRELE(lp);
		} else {
			e = ESRCH;
		}
		p31b_proc_done(targetp);
	}
	if (e == 0)
		copyout(&sched_param, uap->param, sizeof(sched_param));
	return e;
}

int
sys_sched_setscheduler(struct sched_setscheduler_args *uap)
{
	struct proc *p;
	struct lwp *lp;
	int e;
	struct sched_param sched_param;

	copyin(uap->param, &sched_param, sizeof(sched_param));

	if ((e = p31b_proc(uap->pid, &p)) == 0) {
		lp = FIRST_LWP_IN_PROC(p); /* XXX lwp */
		if (lp) {
			LWPHOLD(lp);
			lwkt_gettoken(&lp->lwp_token);
			e = ksched_setscheduler(&uap->sysmsg_reg, ksched,
						lp, uap->policy,
				    (const struct sched_param *)&sched_param);
			lwkt_reltoken(&lp->lwp_token);
			LWPRELE(lp);
		} else {
			e = ESRCH;
		}
		p31b_proc_done(p);
	}
	return e;
}

int
sys_sched_getscheduler(struct sched_getscheduler_args *uap)
{
	struct proc *targetp;
	struct lwp *lp;
	int e;
 
	if ((e = p31b_proc(uap->pid, &targetp)) == 0) {
		lp = FIRST_LWP_IN_PROC(targetp); /* XXX lwp */
		if (lp) {
			LWPHOLD(lp);
			lwkt_gettoken(&lp->lwp_token);
			e = ksched_getscheduler(&uap->sysmsg_reg, ksched, lp);
			lwkt_reltoken(&lp->lwp_token);
			LWPRELE(lp);
		} else {
			e = ESRCH;
		}
		p31b_proc_done(targetp);
	}
	return e;
}

/*
 * MPSAFE
 */
int
sys_sched_yield(struct sched_yield_args *uap)
{
	return ksched_yield(&uap->sysmsg_reg, ksched);
}

/*
 * MPSAFE
 */
int
sys_sched_get_priority_max(struct sched_get_priority_max_args *uap)
{
	return ksched_get_priority_max(&uap->sysmsg_reg, ksched, uap->policy);
}

/*
 * MPSAFE
 */
int
sys_sched_get_priority_min(struct sched_get_priority_min_args *uap)
{
	return ksched_get_priority_min(&uap->sysmsg_reg, ksched, uap->policy);
}

int
sys_sched_rr_get_interval(struct sched_rr_get_interval_args *uap)
{
	int e;
	struct proc *p;
	struct lwp *lp;
	struct timespec ts;

	if ((e = p31b_proc(uap->pid, &p)) == 0) {
		lp = FIRST_LWP_IN_PROC(p); /* XXX lwp */
		if (lp) {
			LWPHOLD(lp);
			lwkt_gettoken(&lp->lwp_token);
			e = ksched_rr_get_interval(&uap->sysmsg_reg, ksched,
						   lp, &ts);
			if (e == 0)
				e = copyout(&ts, uap->interval, sizeof(ts));
			lwkt_reltoken(&lp->lwp_token);
			LWPRELE(lp);
		} else {
			e = ESRCH;
		}
		p31b_proc_done(p);
	}
	return e;
}

#endif

static void
p31binit(void *notused)
{
	(void) sched_attach();
	p31b_setcfg(CTL_P1003_1B_PAGESIZE, PAGE_SIZE);
	p31b_setcfg(CTL_P1003_1B_ASYNCHRONOUS_IO, -1);
	p31b_setcfg(CTL_P1003_1B_MESSAGE_PASSING, _POSIX_MESSAGE_PASSING);
}

SYSINIT(p31b, SI_SUB_P1003_1B, SI_ORDER_FIRST, p31binit, NULL);
