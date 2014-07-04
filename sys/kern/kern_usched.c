/*
 * Copyright (c) 2005 The DragonFly Project.  All rights reserved.
 * 
 * This code is derived from software contributed to The DragonFly Project
 * by Sergey Glushchenko <deen@smz.com.ua>
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name of The DragonFly Project nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific, prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 * 
 * $DragonFly: src/sys/kern/kern_usched.c,v 1.9 2007/07/02 17:06:55 dillon Exp $
 */

#include <sys/errno.h>
#include <sys/globaldata.h>		/* curthread */
#include <sys/proc.h>
#include <sys/priv.h>
#include <sys/sysproto.h>		/* struct usched_set_args */
#include <sys/systm.h>			/* strcmp() */
#include <sys/usched.h>	

#include <sys/mplock2.h>

#include <machine/smp.h>

static TAILQ_HEAD(, usched) usched_list = TAILQ_HEAD_INITIALIZER(usched_list);

cpumask_t usched_mastermask = CPUMASK_INITIALIZER_ALLONES;

/*
 * Called from very low level boot code, i386/i386/machdep.c/init386().
 * We cannot do anything fancy.  no malloc's, no nothing other then 
 * static initialization.
 */
struct usched *
usched_init(void)
{
	const char *defsched;

	defsched = kgetenv("kern.user_scheduler");

	/*
	 * Add various userland schedulers to the system.
	 */
	usched_ctl(&usched_bsd4, USCH_ADD);
	usched_ctl(&usched_dfly, USCH_ADD);
	usched_ctl(&usched_dummy, USCH_ADD);
	if (defsched == NULL )
		return(&usched_dfly);
	if (strcmp(defsched, "bsd4") == 0)
		return(&usched_bsd4);
	if (strcmp(defsched, "dfly") == 0)
		return(&usched_dfly);
	kprintf("WARNING: Running dummy userland scheduler\n");
	return(&usched_dummy);
}

/*
 * USCHED_CTL
 *
 * SYNOPSIS:
 * 	Add/remove usched to/from list.
 * 	
 * ARGUMENTS:
 * 	usched - pointer to target scheduler
 * 	action - addition or removal ?
 *
 * RETURN VALUES:
 * 	0 - success
 * 	EINVAL - error
 */
int
usched_ctl(struct usched *usched, int action)
{
	struct usched *item;	/* temporaly for TAILQ processing */
	int error = 0;

	switch(action) {
	case USCH_ADD:
		/*
		 * Make sure it isn't already on the list
		 */
#ifdef INVARIANTS
		TAILQ_FOREACH(item, &usched_list, entry) {
			KKASSERT(item != usched);
		}
#endif
		/*
		 * Optional callback to the scheduler before we officially
		 * add it to the list.
		 */
		if (usched->usched_register)
			usched->usched_register();
		TAILQ_INSERT_TAIL(&usched_list, usched, entry);
		break;
	case USCH_REM:
		/*
		 * Do not allow the default scheduler to be removed
		 */
		if (strcmp(usched->name, "bsd4") == 0) {
			error = EINVAL;
			break;
		}
		TAILQ_FOREACH(item, &usched_list, entry) {
			if (item == usched)
				break;
		}
		if (item) {
			if (item->usched_unregister)
				item->usched_unregister();
			TAILQ_REMOVE(&usched_list, item, entry);
		} else {
			error = EINVAL;
		}
		break;
	default:
		error = EINVAL;
		break;
	}
	return (error);
}

/*
 * Called from the scheduler clock on each cpu independently at the
 * common scheduling rate.  If th scheduler clock interrupted a running
 * lwp the lp will be non-NULL.
 */
void
usched_schedulerclock(struct lwp *lp, sysclock_t periodic, sysclock_t time)
{
	struct usched *item;

	TAILQ_FOREACH(item, &usched_list, entry) {
		if (lp && lp->lwp_proc->p_usched == item)
			item->schedulerclock(lp, periodic, time);
		else
			item->schedulerclock(NULL, periodic, time);
	}
}

/*
 * USCHED_SET(syscall)
 *
 * SYNOPSIS:
 * 	Setting up a proc's usched.
 *
 * ARGUMENTS:
 *	pid	-
 *	cmd	-
 * 	data	- 
 *	bytes	-
 * RETURN VALUES:
 * 	0 - success
 * 	EINVAL - error
 *
 * MPALMOSTSAFE
 */
int
sys_usched_set(struct usched_set_args *uap)
{
	struct proc *p = curthread->td_proc;
	struct usched *item;	/* temporaly for TAILQ processing */
	int error;
	char buffer[NAME_LENGTH];
	cpumask_t mask;
	struct lwp *lp;
	int cpuid;

	if (uap->pid != 0 && uap->pid != curthread->td_proc->p_pid)
		return (EINVAL);

	lp = curthread->td_lwp;
	get_mplock();

	switch (uap->cmd) {
	case USCHED_SET_SCHEDULER:
		if ((error = priv_check(curthread, PRIV_SCHED_SET)) != 0)
			break;
		error = copyinstr(uap->data, buffer, sizeof(buffer), NULL);
		if (error)
			break;
		TAILQ_FOREACH(item, &usched_list, entry) {
			if ((strcmp(item->name, buffer) == 0))
				break;
		}

		/*
		 * If the scheduler for a process is being changed, disassociate
		 * the old scheduler before switching to the new one.  
		 *
		 * XXX we might have to add an additional ABI call to do a 'full
		 * disassociation' and another ABI call to do a 'full
		 * reassociation'
		 */
		/* XXX lwp have to deal with multiple lwps here */
		if (p->p_nthreads != 1) {
			error = EINVAL;
			break;
		}
		if (item && item != p->p_usched) {
			/* XXX lwp */
			p->p_usched->release_curproc(ONLY_LWP_IN_PROC(p));
			p->p_usched->heuristic_exiting(ONLY_LWP_IN_PROC(p), p);
			p->p_usched = item;
		} else if (item == NULL) {
			error = EINVAL;
		}
		break;
	case USCHED_SET_CPU:
		if ((error = priv_check(curthread, PRIV_SCHED_CPUSET)) != 0)
			break;
		if (uap->bytes != sizeof(int)) {
			error = EINVAL;
			break;
		}
		error = copyin(uap->data, &cpuid, sizeof(int));
		if (error)
			break;
		if (cpuid < 0 || cpuid >= ncpus) {
			error = EFBIG;
			break;
		}
		if (CPUMASK_TESTBIT(smp_active_mask, cpuid) == 0) {
			error = EINVAL;
			break;
		}
		CPUMASK_ASSBIT(lp->lwp_cpumask, cpuid);
		if (cpuid != mycpu->gd_cpuid) {
			lwkt_migratecpu(cpuid);
			p->p_usched->changedcpu(lp);
		}
		break;
	case USCHED_GET_CPU:
		/* USCHED_GET_CPU doesn't require special privileges. */
		if (uap->bytes != sizeof(int)) {
			error = EINVAL;
			break;
		}
		error = copyout(&(mycpu->gd_cpuid), uap->data, sizeof(int));
		break;
	case USCHED_ADD_CPU:
		if ((error = priv_check(curthread, PRIV_SCHED_CPUSET)) != 0)
			break;
		if (uap->bytes != sizeof(int)) {
			error = EINVAL;
			break;
		}
		error = copyin(uap->data, &cpuid, sizeof(int));
		if (error)
			break;
		if (cpuid < 0 || cpuid >= ncpus) {
			error = EFBIG;
			break;
		}
		if (CPUMASK_TESTBIT(smp_active_mask, cpuid) == 0) {
			error = EINVAL;
			break;
		}
		CPUMASK_ORBIT(lp->lwp_cpumask, cpuid);
		break;
	case USCHED_DEL_CPU:
		/* USCHED_DEL_CPU doesn't require special privileges. */
		if (uap->bytes != sizeof(int)) {
			error = EINVAL;
			break;
		}
		error = copyin(uap->data, &cpuid, sizeof(int));
		if (error)
			break;
		if (cpuid < 0 || cpuid >= ncpus) {
			error = EFBIG;
			break;
		}
		lp = curthread->td_lwp;
		mask = lp->lwp_cpumask;
		CPUMASK_ANDMASK(mask, smp_active_mask);
		CPUMASK_NANDBIT(mask, cpuid);
		if (CPUMASK_TESTZERO(mask)) {
			error = EPERM;
		} else {
			CPUMASK_NANDBIT(lp->lwp_cpumask, cpuid);
			if (CPUMASK_TESTMASK(lp->lwp_cpumask,
					    mycpu->gd_cpumask) == 0) {
				mask = lp->lwp_cpumask;
				CPUMASK_ANDMASK(mask, smp_active_mask);
				cpuid = BSFCPUMASK(mask);
				lwkt_migratecpu(cpuid);
				p->p_usched->changedcpu(lp);
			}
		}
		break;
	default:
		error = EINVAL;
		break;
	}
	rel_mplock();
	return (error);
}

