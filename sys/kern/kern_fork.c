/*
 * Copyright (c) 1982, 1986, 1989, 1991, 1993
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
 *	@(#)kern_fork.c	8.6 (Berkeley) 4/8/94
 * $FreeBSD: src/sys/kern/kern_fork.c,v 1.72.2.14 2003/06/26 04:15:10 silby Exp $
 */

#include "opt_ktrace.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/sysproto.h>
#include <sys/filedesc.h>
#include <sys/kernel.h>
#include <sys/sysctl.h>
#include <sys/malloc.h>
#include <sys/proc.h>
#include <sys/resourcevar.h>
#include <sys/vnode.h>
#include <sys/acct.h>
#include <sys/ktrace.h>
#include <sys/unistd.h>
#include <sys/jail.h>

#include <vm/vm.h>
#include <sys/lock.h>
#include <vm/pmap.h>
#include <vm/vm_map.h>
#include <vm/vm_extern.h>

#include <sys/vmmeter.h>
#include <sys/refcount.h>
#include <sys/thread2.h>
#include <sys/signal2.h>
#include <sys/spinlock2.h>

#include <sys/dsched.h>

static MALLOC_DEFINE(M_ATFORK, "atfork", "atfork callback");

/*
 * These are the stuctures used to create a callout list for things to do
 * when forking a process
 */
struct forklist {
	forklist_fn function;
	TAILQ_ENTRY(forklist) next;
};

TAILQ_HEAD(forklist_head, forklist);
static struct forklist_head fork_list = TAILQ_HEAD_INITIALIZER(fork_list);

static struct lwp *lwp_fork(struct lwp *, struct proc *, int flags);

int forksleep; /* Place for fork1() to sleep on. */

/*
 * Red-Black tree support for LWPs
 */

static int
rb_lwp_compare(struct lwp *lp1, struct lwp *lp2)
{
	if (lp1->lwp_tid < lp2->lwp_tid)
		return(-1);
	if (lp1->lwp_tid > lp2->lwp_tid)
		return(1);
	return(0);
}

RB_GENERATE2(lwp_rb_tree, lwp, u.lwp_rbnode, rb_lwp_compare, lwpid_t, lwp_tid);

/*
 * fork() system call
 */
int
sys_fork(struct fork_args *uap)
{
	struct lwp *lp = curthread->td_lwp;
	struct proc *p2;
	int error;

	error = fork1(lp, RFFDG | RFPROC | RFPGLOCK, &p2);
	if (error == 0) {
		PHOLD(p2);
		start_forked_proc(lp, p2);
		uap->sysmsg_fds[0] = p2->p_pid;
		uap->sysmsg_fds[1] = 0;
		PRELE(p2);
	}
	return error;
}

/*
 * vfork() system call
 */
int
sys_vfork(struct vfork_args *uap)
{
	struct lwp *lp = curthread->td_lwp;
	struct proc *p2;
	int error;

	error = fork1(lp, RFFDG | RFPROC | RFPPWAIT | RFMEM | RFPGLOCK, &p2);
	if (error == 0) {
		PHOLD(p2);
		start_forked_proc(lp, p2);
		uap->sysmsg_fds[0] = p2->p_pid;
		uap->sysmsg_fds[1] = 0;
		PRELE(p2);
	}
	return error;
}

/*
 * Handle rforks.  An rfork may (1) operate on the current process without
 * creating a new, (2) create a new process that shared the current process's
 * vmspace, signals, and/or descriptors, or (3) create a new process that does
 * not share these things (normal fork).
 *
 * Note that we only call start_forked_proc() if a new process is actually
 * created.
 *
 * rfork { int flags }
 */
int
sys_rfork(struct rfork_args *uap)
{
	struct lwp *lp = curthread->td_lwp;
	struct proc *p2;
	int error;

	if ((uap->flags & RFKERNELONLY) != 0)
		return (EINVAL);

	error = fork1(lp, uap->flags | RFPGLOCK, &p2);
	if (error == 0) {
		if (p2) {
			PHOLD(p2);
			start_forked_proc(lp, p2);
			uap->sysmsg_fds[0] = p2->p_pid;
			uap->sysmsg_fds[1] = 0;
			PRELE(p2);
		} else {
			uap->sysmsg_fds[0] = 0;
			uap->sysmsg_fds[1] = 0;
		}
	}
	return error;
}

/*
 * Low level thread create used by pthreads.
 */
int
sys_lwp_create(struct lwp_create_args *uap)
{
	struct proc *p = curproc;
	struct lwp *lp;
	struct lwp_params params;
	int error;

	error = copyin(uap->params, &params, sizeof(params));
	if (error)
		goto fail2;

	lwkt_gettoken(&p->p_token);
	plimit_lwp_fork(p);	/* force exclusive access */
	lp = lwp_fork(curthread->td_lwp, p, RFPROC);
	error = cpu_prepare_lwp(lp, &params);
	if (error)
		goto fail;
	if (params.tid1 != NULL &&
	    (error = copyout(&lp->lwp_tid, params.tid1, sizeof(lp->lwp_tid))))
		goto fail;
	if (params.tid2 != NULL &&
	    (error = copyout(&lp->lwp_tid, params.tid2, sizeof(lp->lwp_tid))))
		goto fail;

	/*
	 * Now schedule the new lwp. 
	 */
	p->p_usched->resetpriority(lp);
	crit_enter();
	lp->lwp_stat = LSRUN;
	p->p_usched->setrunqueue(lp);
	crit_exit();
	lwkt_reltoken(&p->p_token);

	return (0);

fail:
	lwp_rb_tree_RB_REMOVE(&p->p_lwp_tree, lp);
	--p->p_nthreads;
	/* lwp_dispose expects an exited lwp, and a held proc */
	atomic_set_int(&lp->lwp_mpflags, LWP_MP_WEXIT);
	lp->lwp_thread->td_flags |= TDF_EXITING;
	lwkt_remove_tdallq(lp->lwp_thread);
	PHOLD(p);
	biosched_done(lp->lwp_thread);
	dsched_exit_thread(lp->lwp_thread);
	lwp_dispose(lp);
	lwkt_reltoken(&p->p_token);
fail2:
	return (error);
}

int	nprocs = 1;		/* process 0 */

int
fork1(struct lwp *lp1, int flags, struct proc **procp)
{
	struct proc *p1 = lp1->lwp_proc;
	struct proc *p2;
	struct proc *pptr;
	struct pgrp *p1grp;
	struct pgrp *plkgrp;
	uid_t uid;
	int ok, error;
	static int curfail = 0;
	static struct timeval lastfail;
	struct forklist *ep;
	struct filedesc_to_leader *fdtol;

	if ((flags & (RFFDG|RFCFDG)) == (RFFDG|RFCFDG))
		return (EINVAL);

	lwkt_gettoken(&p1->p_token);
	plkgrp = NULL;
	p2 = NULL;

	/*
	 * Here we don't create a new process, but we divorce
	 * certain parts of a process from itself.
	 */
	if ((flags & RFPROC) == 0) {
		/*
		 * This kind of stunt does not work anymore if
		 * there are native threads (lwps) running
		 */
		if (p1->p_nthreads != 1) {
			error = EINVAL;
			goto done;
		}

		vm_fork(p1, 0, flags);

		/*
		 * Close all file descriptors.
		 */
		if (flags & RFCFDG) {
			struct filedesc *fdtmp;
			fdtmp = fdinit(p1);
			fdfree(p1, fdtmp);
		}

		/*
		 * Unshare file descriptors (from parent.)
		 */
		if (flags & RFFDG) {
			if (p1->p_fd->fd_refcnt > 1) {
				struct filedesc *newfd;
				error = fdcopy(p1, &newfd);
				if (error != 0) {
					error = ENOMEM;
					goto done;
				}
				fdfree(p1, newfd);
			}
		}
		*procp = NULL;
		error = 0;
		goto done;
	}

	/*
	 * Interlock against process group signal delivery.  If signals
	 * are pending after the interlock is obtained we have to restart
	 * the system call to process the signals.  If we don't the child
	 * can miss a pgsignal (such as ^C) sent during the fork.
	 *
	 * We can't use CURSIG() here because it will process any STOPs
	 * and cause the process group lock to be held indefinitely.  If
	 * a STOP occurs, the fork will be restarted after the CONT.
	 */
	p1grp = p1->p_pgrp;
	if ((flags & RFPGLOCK) && (plkgrp = p1->p_pgrp) != NULL) {
		pgref(plkgrp);
		lockmgr(&plkgrp->pg_lock, LK_SHARED);
		if (CURSIG_NOBLOCK(lp1)) {
			error = ERESTART;
			goto done;
		}
	}

	/*
	 * Although process entries are dynamically created, we still keep
	 * a global limit on the maximum number we will create.  Don't allow
	 * a nonprivileged user to use the last ten processes; don't let root
	 * exceed the limit. The variable nprocs is the current number of
	 * processes, maxproc is the limit.
	 */
	uid = lp1->lwp_thread->td_ucred->cr_ruid;
	if ((nprocs >= maxproc - 10 && uid != 0) || nprocs >= maxproc) {
		if (ppsratecheck(&lastfail, &curfail, 1))
			kprintf("maxproc limit exceeded by uid %d, please "
			       "see tuning(7) and login.conf(5).\n", uid);
		tsleep(&forksleep, 0, "fork", hz / 2);
		error = EAGAIN;
		goto done;
	}

	/*
	 * Increment the nprocs resource before blocking can occur.  There
	 * are hard-limits as to the number of processes that can run.
	 */
	atomic_add_int(&nprocs, 1);

	/*
	 * Increment the count of procs running with this uid. Don't allow
	 * a nonprivileged user to exceed their current limit.
	 */
	ok = chgproccnt(lp1->lwp_thread->td_ucred->cr_ruidinfo, 1,
		(uid != 0) ? p1->p_rlimit[RLIMIT_NPROC].rlim_cur : 0);
	if (!ok) {
		/*
		 * Back out the process count
		 */
		atomic_add_int(&nprocs, -1);
		if (ppsratecheck(&lastfail, &curfail, 1))
			kprintf("maxproc limit exceeded by uid %d, please "
			       "see tuning(7) and login.conf(5).\n", uid);
		tsleep(&forksleep, 0, "fork", hz / 2);
		error = EAGAIN;
		goto done;
	}

	/*
	 * Allocate a new process, don't get fancy: zero the structure.
	 */
	p2 = kmalloc(sizeof(struct proc), M_PROC, M_WAITOK|M_ZERO);

	/*
	 * Core initialization.  SIDL is a safety state that protects the
	 * partially initialized process once it starts getting hooked
	 * into system structures and becomes addressable.
	 *
	 * We must be sure to acquire p2->p_token as well, we must hold it
	 * once the process is on the allproc list to avoid things such
	 * as competing modifications to p_flags.
	 */
	mycpu->gd_forkid += ncpus;
	p2->p_forkid = mycpu->gd_forkid + mycpu->gd_cpuid;
	p2->p_lasttid = -1;	/* first tid will be 0 */
	p2->p_stat = SIDL;

	RB_INIT(&p2->p_lwp_tree);
	spin_init(&p2->p_spin, "procfork1");
	lwkt_token_init(&p2->p_token, "proc");
	lwkt_gettoken(&p2->p_token);

	/*
	 * Setup linkage for kernel based threading XXX lwp.  Also add the
	 * process to the allproclist.
	 *
	 * The process structure is addressable after this point.
	 */
	if (flags & RFTHREAD) {
		p2->p_peers = p1->p_peers;
		p1->p_peers = p2;
		p2->p_leader = p1->p_leader;
	} else {
		p2->p_leader = p2;
	}
	proc_add_allproc(p2);

	/*
	 * Initialize the section which is copied verbatim from the parent.
	 */
	bcopy(&p1->p_startcopy, &p2->p_startcopy,
	      ((caddr_t)&p2->p_endcopy - (caddr_t)&p2->p_startcopy));

	/*
	 * Duplicate sub-structures as needed.  Increase reference counts
	 * on shared objects.
	 *
	 * NOTE: because we are now on the allproc list it is possible for
	 *	 other consumers to gain temporary references to p2
	 *	 (p2->p_lock can change).
	 */
	if (p1->p_flags & P_PROFIL)
		startprofclock(p2);
	p2->p_ucred = crhold(lp1->lwp_thread->td_ucred);

	if (jailed(p2->p_ucred))
		p2->p_flags |= P_JAILED;

	if (p2->p_args)
		refcount_acquire(&p2->p_args->ar_ref);

	p2->p_usched = p1->p_usched;
	/* XXX: verify copy of the secondary iosched stuff */
	dsched_new_proc(p2);

	if (flags & RFSIGSHARE) {
		p2->p_sigacts = p1->p_sigacts;
		refcount_acquire(&p2->p_sigacts->ps_refcnt);
	} else {
		p2->p_sigacts = kmalloc(sizeof(*p2->p_sigacts),
					M_SUBPROC, M_WAITOK);
		bcopy(p1->p_sigacts, p2->p_sigacts, sizeof(*p2->p_sigacts));
		refcount_init(&p2->p_sigacts->ps_refcnt, 1);
	}
	if (flags & RFLINUXTHPN) 
	        p2->p_sigparent = SIGUSR1;
	else
	        p2->p_sigparent = SIGCHLD;

	/* bump references to the text vnode (for procfs) */
	p2->p_textvp = p1->p_textvp;
	if (p2->p_textvp)
		vref(p2->p_textvp);

	/* copy namecache handle to the text file */
	if (p1->p_textnch.mount)
		cache_copy(&p1->p_textnch, &p2->p_textnch);

	/*
	 * Handle file descriptors
	 */
	if (flags & RFCFDG) {
		p2->p_fd = fdinit(p1);
		fdtol = NULL;
	} else if (flags & RFFDG) {
		error = fdcopy(p1, &p2->p_fd);
		if (error != 0) {
			error = ENOMEM;
			goto done;
		}
		fdtol = NULL;
	} else {
		p2->p_fd = fdshare(p1);
		if (p1->p_fdtol == NULL) {
			p1->p_fdtol = filedesc_to_leader_alloc(NULL,
							       p1->p_leader);
		}
		if ((flags & RFTHREAD) != 0) {
			/*
			 * Shared file descriptor table and
			 * shared process leaders.
			 */
			fdtol = p1->p_fdtol;
			fdtol->fdl_refcount++;
		} else {
			/* 
			 * Shared file descriptor table, and
			 * different process leaders 
			 */
			fdtol = filedesc_to_leader_alloc(p1->p_fdtol, p2);
		}
	}
	p2->p_fdtol = fdtol;
	p2->p_limit = plimit_fork(p1);

	/*
	 * Preserve some more flags in subprocess.  P_PROFIL has already
	 * been preserved.
	 */
	p2->p_flags |= p1->p_flags & P_SUGID;
	if (p1->p_session->s_ttyvp != NULL && (p1->p_flags & P_CONTROLT))
		p2->p_flags |= P_CONTROLT;
	if (flags & RFPPWAIT) {
		p2->p_flags |= P_PPWAIT;
		if (p1->p_upmap)
			p1->p_upmap->invfork = 1;
	}


	/*
	 * Inherit the virtual kernel structure (allows a virtual kernel
	 * to fork to simulate multiple cpus).
	 */
	if (p1->p_vkernel)
		vkernel_inherit(p1, p2);

	/*
	 * Once we are on a pglist we may receive signals.  XXX we might
	 * race a ^C being sent to the process group by not receiving it
	 * at all prior to this line.
	 */
	pgref(p1grp);
	lwkt_gettoken(&p1grp->pg_token);
	LIST_INSERT_AFTER(p1, p2, p_pglist);
	lwkt_reltoken(&p1grp->pg_token);

	/*
	 * Attach the new process to its parent.
	 *
	 * If RFNOWAIT is set, the newly created process becomes a child
	 * of init.  This effectively disassociates the child from the
	 * parent.
	 */
	if (flags & RFNOWAIT)
		pptr = initproc;
	else
		pptr = p1;
	p2->p_pptr = pptr;
	LIST_INIT(&p2->p_children);

	lwkt_gettoken(&pptr->p_token);
	LIST_INSERT_HEAD(&pptr->p_children, p2, p_sibling);
	lwkt_reltoken(&pptr->p_token);

	varsymset_init(&p2->p_varsymset, &p1->p_varsymset);
	callout_init_mp(&p2->p_ithandle);

#ifdef KTRACE
	/*
	 * Copy traceflag and tracefile if enabled.  If not inherited,
	 * these were zeroed above but we still could have a trace race
	 * so make sure p2's p_tracenode is NULL.
	 */
	if ((p1->p_traceflag & KTRFAC_INHERIT) && p2->p_tracenode == NULL) {
		p2->p_traceflag = p1->p_traceflag;
		p2->p_tracenode = ktrinherit(p1->p_tracenode);
	}
#endif

	/*
	 * This begins the section where we must prevent the parent
	 * from being swapped.
	 *
	 * Gets PRELE'd in the caller in start_forked_proc().
	 */
	PHOLD(p1);

	vm_fork(p1, p2, flags);

	/*
	 * Create the first lwp associated with the new proc.
	 * It will return via a different execution path later, directly
	 * into userland, after it was put on the runq by
	 * start_forked_proc().
	 */
	lwp_fork(lp1, p2, flags);

	if (flags == (RFFDG | RFPROC | RFPGLOCK)) {
		mycpu->gd_cnt.v_forks++;
		mycpu->gd_cnt.v_forkpages += p2->p_vmspace->vm_dsize +
					     p2->p_vmspace->vm_ssize;
	} else if (flags == (RFFDG | RFPROC | RFPPWAIT | RFMEM | RFPGLOCK)) {
		mycpu->gd_cnt.v_vforks++;
		mycpu->gd_cnt.v_vforkpages += p2->p_vmspace->vm_dsize +
					      p2->p_vmspace->vm_ssize;
	} else if (p1 == &proc0) {
		mycpu->gd_cnt.v_kthreads++;
		mycpu->gd_cnt.v_kthreadpages += p2->p_vmspace->vm_dsize +
						p2->p_vmspace->vm_ssize;
	} else {
		mycpu->gd_cnt.v_rforks++;
		mycpu->gd_cnt.v_rforkpages += p2->p_vmspace->vm_dsize +
					      p2->p_vmspace->vm_ssize;
	}

	/*
	 * Both processes are set up, now check if any loadable modules want
	 * to adjust anything.
	 *   What if they have an error? XXX
	 */
	TAILQ_FOREACH(ep, &fork_list, next) {
		(*ep->function)(p1, p2, flags);
	}

	/*
	 * Set the start time.  Note that the process is not runnable.  The
	 * caller is responsible for making it runnable.
	 */
	microtime(&p2->p_start);
	p2->p_acflag = AFORK;

	/*
	 * tell any interested parties about the new process
	 */
	KNOTE(&p1->p_klist, NOTE_FORK | p2->p_pid);

	/*
	 * Return child proc pointer to parent.
	 */
	*procp = p2;
	error = 0;
done:
	if (p2)
		lwkt_reltoken(&p2->p_token);
	lwkt_reltoken(&p1->p_token);
	if (plkgrp) {
		lockmgr(&plkgrp->pg_lock, LK_RELEASE);
		pgrel(plkgrp);
	}
	return (error);
}

static struct lwp *
lwp_fork(struct lwp *origlp, struct proc *destproc, int flags)
{
	globaldata_t gd = mycpu;
	struct lwp *lp;
	struct thread *td;

	lp = kmalloc(sizeof(struct lwp), M_LWP, M_WAITOK|M_ZERO);

	lp->lwp_proc = destproc;
	lp->lwp_vmspace = destproc->p_vmspace;
	lp->lwp_stat = LSRUN;
	bcopy(&origlp->lwp_startcopy, &lp->lwp_startcopy,
	    (unsigned) ((caddr_t)&lp->lwp_endcopy -
			(caddr_t)&lp->lwp_startcopy));
	lp->lwp_flags |= origlp->lwp_flags & LWP_ALTSTACK;
	/*
	 * Set cpbase to the last timeout that occured (not the upcoming
	 * timeout).
	 *
	 * A critical section is required since a timer IPI can update
	 * scheduler specific data.
	 */
	crit_enter();
	lp->lwp_cpbase = gd->gd_schedclock.time - gd->gd_schedclock.periodic;
	destproc->p_usched->heuristic_forking(origlp, lp);
	crit_exit();
	CPUMASK_ANDMASK(lp->lwp_cpumask, usched_mastermask);
	lwkt_token_init(&lp->lwp_token, "lwp_token");
	spin_init(&lp->lwp_spin, "lwptoken");

	/*
	 * Assign the thread to the current cpu to begin with so we
	 * can manipulate it.
	 */
	td = lwkt_alloc_thread(NULL, LWKT_THREAD_STACK, gd->gd_cpuid, 0);
	lp->lwp_thread = td;
	td->td_ucred = crhold(destproc->p_ucred);
	td->td_proc = destproc;
	td->td_lwp = lp;
	td->td_switch = cpu_heavy_switch;
#ifdef NO_LWKT_SPLIT_USERPRI
	lwkt_setpri(td, TDPRI_USER_NORM);
#else
	lwkt_setpri(td, TDPRI_KERN_USER);
#endif
	lwkt_set_comm(td, "%s", destproc->p_comm);

	/*
	 * cpu_fork will copy and update the pcb, set up the kernel stack,
	 * and make the child ready to run.
	 */
	cpu_fork(origlp, lp, flags);
	kqueue_init(&lp->lwp_kqueue, destproc->p_fd);

	/*
	 * Assign a TID to the lp.  Loop until the insert succeeds (returns
	 * NULL).
	 */
	lp->lwp_tid = destproc->p_lasttid;
	do {
		if (++lp->lwp_tid < 0)
			lp->lwp_tid = 1;
	} while (lwp_rb_tree_RB_INSERT(&destproc->p_lwp_tree, lp) != NULL);
	destproc->p_lasttid = lp->lwp_tid;
	destproc->p_nthreads++;

	/*
	 * This flag is set and never cleared.  It means that the process
	 * was threaded at some point.  Used to improve exit performance.
	 */
	destproc->p_flags |= P_MAYBETHREADED;

	return (lp);
}

/*
 * The next two functionms are general routines to handle adding/deleting
 * items on the fork callout list.
 *
 * at_fork():
 * Take the arguments given and put them onto the fork callout list,
 * However first make sure that it's not already there.
 * Returns 0 on success or a standard error number.
 */
int
at_fork(forklist_fn function)
{
	struct forklist *ep;

#ifdef INVARIANTS
	/* let the programmer know if he's been stupid */
	if (rm_at_fork(function)) {
		kprintf("WARNING: fork callout entry (%p) already present\n",
		    function);
	}
#endif
	ep = kmalloc(sizeof(*ep), M_ATFORK, M_WAITOK|M_ZERO);
	ep->function = function;
	TAILQ_INSERT_TAIL(&fork_list, ep, next);
	return (0);
}

/*
 * Scan the exit callout list for the given item and remove it..
 * Returns the number of items removed (0 or 1)
 */
int
rm_at_fork(forklist_fn function)
{
	struct forklist *ep;

	TAILQ_FOREACH(ep, &fork_list, next) {
		if (ep->function == function) {
			TAILQ_REMOVE(&fork_list, ep, next);
			kfree(ep, M_ATFORK);
			return(1);
		}
	}	
	return (0);
}

/*
 * Add a forked process to the run queue after any remaining setup, such
 * as setting the fork handler, has been completed.
 *
 * p2 is held by the caller.
 */
void
start_forked_proc(struct lwp *lp1, struct proc *p2)
{
	struct lwp *lp2 = ONLY_LWP_IN_PROC(p2);
	int pflags;

	/*
	 * Move from SIDL to RUN queue, and activate the process's thread.
	 * Activation of the thread effectively makes the process "a"
	 * current process, so we do not setrunqueue().
	 *
	 * YYY setrunqueue works here but we should clean up the trampoline
	 * code so we just schedule the LWKT thread and let the trampoline
	 * deal with the userland scheduler on return to userland.
	 */
	KASSERT(p2->p_stat == SIDL,
	    ("cannot start forked process, bad status: %p", p2));
	p2->p_usched->resetpriority(lp2);
	crit_enter();
	p2->p_stat = SACTIVE;
	lp2->lwp_stat = LSRUN;
	p2->p_usched->setrunqueue(lp2);
	crit_exit();

	/*
	 * Now can be swapped.
	 */
	PRELE(lp1->lwp_proc);

	/*
	 * Preserve synchronization semantics of vfork.  P_PPWAIT is set in
	 * the child until it has retired the parent's resources.  The parent
	 * must wait for the flag to be cleared by the child.
	 *
	 * Interlock the flag/tsleep with atomic ops to avoid unnecessary
	 * p_token conflicts.
	 *
	 * XXX Is this use of an atomic op on a field that is not normally
	 *     manipulated with atomic ops ok?
	 */
	while ((pflags = p2->p_flags) & P_PPWAIT) {
		cpu_ccfence();
		tsleep_interlock(lp1->lwp_proc, 0);
		if (atomic_cmpset_int(&p2->p_flags, pflags, pflags))
			tsleep(lp1->lwp_proc, PINTERLOCKED, "ppwait", 0);
	}
}
