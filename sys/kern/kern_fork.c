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
#include <sys/sysmsg.h>
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
#include <sys/lwp.h>

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
static MALLOC_DEFINE(M_REAPER, "reaper", "process reapers");

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

static struct lwp	*lwp_fork1(struct lwp *, struct proc *, int flags,
			    const cpumask_t *mask);
static void		lwp_fork2(struct lwp *lp1, struct proc *destproc,
			    struct lwp *lp2, int flags);
static int		lwp_create1(struct lwp_params *params,
			    const cpumask_t *mask);
static struct lock reaper_lock = LOCK_INITIALIZER("reapgl", 0, 0);

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
 * When forking, memory underpinning umtx-supported mutexes may be set
 * COW causing the physical address to change.  We must wakeup any threads
 * blocked on the physical address to allow them to re-resolve their VM.
 *
 * (caller is holding p->p_token)
 */
static void
wake_umtx_threads(struct proc *p1)
{
	struct lwp *lp;
	struct thread *td;

	RB_FOREACH(lp, lwp_rb_tree, &p1->p_lwp_tree) {
		td = lp->lwp_thread;
		if (td && (td->td_flags & TDF_TSLEEPQ) &&
		    (td->td_wdomain & PDOMAIN_MASK) == PDOMAIN_UMTX) {
			wakeup_domain(td->td_wchan, PDOMAIN_UMTX);
		}
	}
}

/*
 * fork() system call
 */
int
sys_fork(struct sysmsg *sysmsg, const struct fork_args *uap)
{
	struct lwp *lp = curthread->td_lwp;
	struct proc *p2;
	int error;

	error = fork1(lp, RFFDG | RFPROC | RFPGLOCK, &p2);
	if (error == 0) {
		PHOLD(p2);
		start_forked_proc(lp, p2);
		sysmsg->sysmsg_fds[0] = p2->p_pid;
		sysmsg->sysmsg_fds[1] = 0;
		PRELE(p2);
	}
	return error;
}

/*
 * vfork() system call
 */
int
sys_vfork(struct sysmsg *sysmsg, const struct vfork_args *uap)
{
	struct lwp *lp = curthread->td_lwp;
	struct proc *p2;
	int error;

	error = fork1(lp, RFFDG | RFPROC | RFPPWAIT | RFMEM | RFPGLOCK, &p2);
	if (error == 0) {
		PHOLD(p2);
		start_forked_proc(lp, p2);
		sysmsg->sysmsg_fds[0] = p2->p_pid;
		sysmsg->sysmsg_fds[1] = 0;
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
sys_rfork(struct sysmsg *sysmsg, const struct rfork_args *uap)
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
			sysmsg->sysmsg_fds[0] = p2->p_pid;
			sysmsg->sysmsg_fds[1] = 0;
			PRELE(p2);
		} else {
			sysmsg->sysmsg_fds[0] = 0;
			sysmsg->sysmsg_fds[1] = 0;
		}
	}
	return error;
}

static int
lwp_create1(struct lwp_params *uprm, const cpumask_t *umask)
{
	struct proc *p = curproc;
	struct lwp *lp;
	struct lwp_params params;
	cpumask_t *mask = NULL, mask0;
	int error;

	error = copyin(uprm, &params, sizeof(params));
	if (error)
		goto fail2;

	if (umask != NULL) {
		error = copyin(umask, &mask0, sizeof(mask0));
		if (error)
			goto fail2;
		CPUMASK_ANDMASK(mask0, smp_active_mask);
		if (CPUMASK_TESTNZERO(mask0))
			mask = &mask0;
	}

	lwkt_gettoken(&p->p_token);
	plimit_lwp_fork(p);	/* force exclusive access */
	lp = lwp_fork1(curthread->td_lwp, p, RFPROC | RFMEM, mask);
	lwp_fork2(curthread->td_lwp, p, lp, RFPROC | RFMEM);
	error = cpu_prepare_lwp(lp, &params);
	if (error)
		goto fail;
	if (params.lwp_tid1 != NULL &&
	    (error = copyout(&lp->lwp_tid, params.lwp_tid1, sizeof(lp->lwp_tid))))
		goto fail;
	if (params.lwp_tid2 != NULL &&
	    (error = copyout(&lp->lwp_tid, params.lwp_tid2, sizeof(lp->lwp_tid))))
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
	/*
	 * Make sure no one is using this lwp, before it is removed from
	 * the tree.  If we didn't wait it here, lwp tree iteration with
	 * blocking operation would be broken.
	 */
	while (lp->lwp_lock > 0)
		tsleep(lp, 0, "lwpfail", 1);
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

/*
 * Low level thread create used by pthreads.
 */
int
sys_lwp_create(struct sysmsg *sysmsg, const struct lwp_create_args *uap)
{

	return (lwp_create1(uap->params, NULL));
}

int
sys_lwp_create2(struct sysmsg *sysmsg, const struct lwp_create2_args *uap)
{

	return (lwp_create1(uap->params, uap->mask));
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
	struct lwp  *lp2;
	struct sysreaper *reap;
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

		vm_fork(p1, NULL, NULL, flags);
		if ((flags & RFMEM) == 0)
			wake_umtx_threads(p1);

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
	 * Increment the count of procs running with this uid.  This also
	 * applies to root.
	 */
	ok = chgproccnt(lp1->lwp_thread->td_ucred->cr_ruidinfo, 1,
			plimit_getadjvalue(RLIMIT_NPROC));
	if (!ok) {
		/*
		 * Back out the process count
		 */
		atomic_add_int(&nprocs, -1);
		if (ppsratecheck(&lastfail, &curfail, 1)) {
			kprintf("maxproc limit of %jd "
				"exceeded by \"%s\" uid %d, "
				"please see tuning(7) and login.conf(5).\n",
				plimit_getadjvalue(RLIMIT_NPROC),
				p1->p_comm,
				uid);
		}
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
	p2->p_lasttid = 0;	/* first tid will be 1 */
	p2->p_stat = SIDL;

	/*
	 * NOTE: Process 0 will not have a reaper, but process 1 (init) and
	 *	 all other processes always will.
	 */
	if ((reap = p1->p_reaper) != NULL) {
		reaper_hold(reap);
		p2->p_reaper = reap;
	} else {
		p2->p_reaper = NULL;
	}

	RB_INIT(&p2->p_lwp_tree);
	spin_init(&p2->p_spin, "procfork1");
	lwkt_token_init(&p2->p_token, "proc");
	lwkt_gettoken(&p2->p_token);
	p2->p_uidpcpu = kmalloc(sizeof(*p2->p_uidpcpu) * ncpus,
				M_SUBPROC, M_WAITOK | M_ZERO);

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
	dsched_enter_proc(p2);

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
	 * Adjust depth for resource downscaling
	 */
	if ((p2->p_depth & 31) != 31)
		++p2->p_depth;

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
			atomic_add_int(&p1->p_upmap->invfork, 1);
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
	 * of the reaper (typically init).  This effectively disassociates
	 * the child from the parent.
	 *
	 * Temporarily hold pptr for the RFNOWAIT case to avoid ripouts.
	 */
	if (flags & RFNOWAIT) {
		pptr = reaper_get(reap);
		if (pptr == NULL) {
			pptr = initproc;
			PHOLD(pptr);
		}
	} else {
		pptr = p1;
	}
	p2->p_pptr = pptr;
	p2->p_ppid = pptr->p_pid;
	LIST_INIT(&p2->p_children);

	lwkt_gettoken(&pptr->p_token);
	LIST_INSERT_HEAD(&pptr->p_children, p2, p_sibling);
	lwkt_reltoken(&pptr->p_token);

	if (flags & RFNOWAIT)
		PRELE(pptr);

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
	 * from being messed with too heavily while we run through the
	 * fork operation.
	 *
	 * Gets PRELE'd in the caller in start_forked_proc().
	 *
	 * Create the first lwp associated with the new proc.  It will
	 * return via a different execution path later, directly into
	 * userland, after it was put on the runq by start_forked_proc().
	 */
	PHOLD(p1);

	lp2 = lwp_fork1(lp1, p2, flags, NULL);
	vm_fork(p1, p2, lp2, flags);
	if ((flags & RFMEM) == 0)
		wake_umtx_threads(p1);
	lwp_fork2(lp1, p2, lp2, flags);

	if (flags == (RFFDG | RFPROC | RFPGLOCK)) {
		mycpu->gd_cnt.v_forks++;
		mycpu->gd_cnt.v_forkpages += btoc(p2->p_vmspace->vm_dsize) +
					     btoc(p2->p_vmspace->vm_ssize);
	} else if (flags == (RFFDG | RFPROC | RFPPWAIT | RFMEM | RFPGLOCK)) {
		mycpu->gd_cnt.v_vforks++;
		mycpu->gd_cnt.v_vforkpages += btoc(p2->p_vmspace->vm_dsize) +
					      btoc(p2->p_vmspace->vm_ssize);
	} else if (p1 == &proc0) {
		mycpu->gd_cnt.v_kthreads++;
		mycpu->gd_cnt.v_kthreadpages += btoc(p2->p_vmspace->vm_dsize) +
						btoc(p2->p_vmspace->vm_ssize);
	} else {
		mycpu->gd_cnt.v_rforks++;
		mycpu->gd_cnt.v_rforkpages += btoc(p2->p_vmspace->vm_dsize) +
					      btoc(p2->p_vmspace->vm_ssize);
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

/*
 * The first part of lwp_fork*() allocates enough of the new lwp that
 * vm_fork() can use it to deal with /dev/lpmap mappings.
 */
static struct lwp *
lwp_fork1(struct lwp *lp1, struct proc *destproc, int flags,
	 const cpumask_t *mask)
{
	struct lwp *lp2;

	lp2 = kmalloc(sizeof(struct lwp), M_LWP, M_WAITOK|M_ZERO);
	lp2->lwp_proc = destproc;
	lp2->lwp_stat = LSRUN;
	bcopy(&lp1->lwp_startcopy, &lp2->lwp_startcopy,
	    (unsigned) ((caddr_t)&lp2->lwp_endcopy -
			(caddr_t)&lp2->lwp_startcopy));
	if (mask != NULL)
		lp2->lwp_cpumask = *mask;

	lwkt_token_init(&lp2->lwp_token, "lwp_token");
	TAILQ_INIT(&lp2->lwp_lpmap_backing_list);
	spin_init(&lp2->lwp_spin, "lwptoken");

	/*
	 * Use the same TID for the first thread in the new process after
	 * a fork or vfork.  This is needed to keep pthreads and /dev/lpmap
	 * sane.  In particular a consequence of implementing the per-thread
	 * /dev/lpmap map code makes this mandatory.
	 *
	 * NOTE: exec*() will reset the TID to 1 to keep things sane in that
	 *	 department too.
	 *
	 * NOTE: In the case of lwp_create(), this TID represents a conflict
	 *	 which will be resolved in lwp_fork2(), but in the case of
	 *	 a fork(), the TID has to be correct or vm_fork() will not
	 *	 keep the correct lpmap.
	 */
	lp2->lwp_tid = lp1->lwp_tid;

	return lp2;
}

/*
 * The second part of lwp_fork*()
 */
static void
lwp_fork2(struct lwp *lp1, struct proc *destproc, struct lwp *lp2, int flags)
{
	globaldata_t gd = mycpu;
	struct thread *td2;

	lp2->lwp_vmspace = destproc->p_vmspace;

	/*
	 * Reset the sigaltstack if memory is shared, otherwise inherit
	 * it.
	 */
	if (flags & RFMEM) {
		lp2->lwp_sigstk.ss_flags = SS_DISABLE;
		lp2->lwp_sigstk.ss_size = 0;
		lp2->lwp_sigstk.ss_sp = NULL;
		lp2->lwp_flags &= ~LWP_ALTSTACK;
	} else {
		lp2->lwp_flags |= lp1->lwp_flags & LWP_ALTSTACK;
	}

	/*
	 * Set cpbase to the last timeout that occured (not the upcoming
	 * timeout).
	 *
	 * A critical section is required since a timer IPI can update
	 * scheduler specific data.
	 */
	crit_enter();
	lp2->lwp_cpbase = gd->gd_schedclock.time - gd->gd_schedclock.periodic;
	destproc->p_usched->heuristic_forking(lp1, lp2);
	crit_exit();
	CPUMASK_ANDMASK(lp2->lwp_cpumask, usched_mastermask);

	/*
	 * Assign the thread to the current cpu to begin with so we
	 * can manipulate it.
	 */
	td2 = lwkt_alloc_thread(NULL, LWKT_THREAD_STACK, gd->gd_cpuid, 0);
	lp2->lwp_thread = td2;
	td2->td_wakefromcpu = gd->gd_cpuid;
	td2->td_ucred = crhold(destproc->p_ucred);
	td2->td_proc = destproc;
	td2->td_lwp = lp2;
	td2->td_switch = cpu_heavy_switch;
#ifdef NO_LWKT_SPLIT_USERPRI
	lwkt_setpri(td2, TDPRI_USER_NORM);
#else
	lwkt_setpri(td2, TDPRI_KERN_USER);
#endif
	lwkt_set_comm(td2, "%s", destproc->p_comm);

	/*
	 * cpu_fork will copy and update the pcb, set up the kernel stack,
	 * and make the child ready to run.
	 */
	cpu_fork(lp1, lp2, flags);
	kqueue_init(&lp2->lwp_kqueue, destproc->p_fd);

	/*
	 * Associate the new thread with destproc, after we've set most of
	 * it up and gotten its related td2 installed.  Otherwise we can
	 * race other random kernel code that iterates LWPs and expects the
	 * thread to be assigned.
	 *
	 * Leave 2 bits open so the pthreads library can optimize locks
	 * by combining the TID with a few Lock-related flags.
	 */
	while (lwp_rb_tree_RB_INSERT(&destproc->p_lwp_tree, lp2) != NULL) {
		++lp2->lwp_tid;
		if (lp2->lwp_tid == 0 || lp2->lwp_tid == 0x3FFFFFFF)
			lp2->lwp_tid = 1;
	}

	destproc->p_lasttid = lp2->lwp_tid;
	destproc->p_nthreads++;

	/*
	 * This flag is set and never cleared.  It means that the process
	 * was threaded at some point.  Used to improve exit performance.
	 */
	pmap_maybethreaded(&destproc->p_vmspace->vm_pmap);
	destproc->p_flags |= P_MAYBETHREADED;

	/*
	 * If the original lp had a lpmap and a non-zero blockallsigs
	 * count, give the lp for the forked process the same count.
	 *
	 * This makes the user code and expectations less confusing
	 * in terms of unwinding locks and also allows userland to start
	 * the forked process with signals blocked via the blockallsigs()
	 * mechanism if desired.
	 */
	if (lp1->lwp_lpmap &&
	    (lp1->lwp_lpmap->blockallsigs & 0x7FFFFFFF)) {
		lwp_usermap(lp2, 0);
		if (lp2->lwp_lpmap) {
			lp2->lwp_lpmap->blockallsigs =
				lp1->lwp_lpmap->blockallsigs;
		}
	}
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

/*
 * procctl (idtype_t idtype, id_t id, int cmd, void *arg)
 */
int
sys_procctl(struct sysmsg *sysmsg, const struct procctl_args *uap)
{
	struct proc *p = curproc;
	struct proc *p2;
	struct sysreaper *reap;
	union reaper_info udata;
	int error;

	if (uap->idtype != P_PID || uap->id != (id_t)p->p_pid)
		return EINVAL;

	switch(uap->cmd) {
	case PROC_REAP_ACQUIRE:
		lwkt_gettoken(&p->p_token);
		reap = kmalloc(sizeof(*reap), M_REAPER, M_WAITOK|M_ZERO);
		if (p->p_reaper == NULL || p->p_reaper->p != p) {
			reaper_init(p, reap);
			error = 0;
		} else {
			kfree(reap, M_REAPER);
			error = EALREADY;
		}
		lwkt_reltoken(&p->p_token);
		break;
	case PROC_REAP_RELEASE:
		lwkt_gettoken(&p->p_token);
release_again:
		reap = p->p_reaper;
		KKASSERT(reap != NULL);
		if (reap->p == p) {
			reaper_hold(reap);	/* in case of thread race */
			lockmgr(&reap->lock, LK_EXCLUSIVE);
			if (reap->p != p) {
				lockmgr(&reap->lock, LK_RELEASE);
				reaper_drop(reap);
				goto release_again;
			}
			reap->p = NULL;
			p->p_reaper = reap->parent;
			if (p->p_reaper)
				reaper_hold(p->p_reaper);
			lockmgr(&reap->lock, LK_RELEASE);
			reaper_drop(reap);	/* our ref */
			reaper_drop(reap);	/* old p_reaper ref */
			error = 0;
		} else {
			error = ENOTCONN;
		}
		lwkt_reltoken(&p->p_token);
		break;
	case PROC_REAP_STATUS:
		bzero(&udata, sizeof(udata));
		lwkt_gettoken_shared(&p->p_token);
		if ((reap = p->p_reaper) != NULL && reap->p == p) {
			udata.status.flags = reap->flags;
			udata.status.refs = reap->refs - 1; /* minus ours */
		}
		p2 = LIST_FIRST(&p->p_children);
		udata.status.pid_head = p2 ? p2->p_pid : -1;
		lwkt_reltoken(&p->p_token);

		if (uap->data) {
			error = copyout(&udata, uap->data,
					sizeof(udata.status));
		} else {
			error = 0;
		}
		break;
	default:
		error = EINVAL;
		break;
	}
	return error;
}

/*
 * Bump ref on reaper, preventing destruction
 */
void
reaper_hold(struct sysreaper *reap)
{
	KKASSERT(reap->refs > 0);
	refcount_acquire(&reap->refs);
}

/*
 * Drop ref on reaper, destroy the structure on the 1->0
 * transition and loop on the parent.
 */
void
reaper_drop(struct sysreaper *next)
{
	struct sysreaper *reap;

	while ((reap = next) != NULL) {
		if (refcount_release(&reap->refs)) {
			next = reap->parent;
			KKASSERT(reap->p == NULL);
			lockmgr(&reaper_lock, LK_EXCLUSIVE);
			reap->parent = NULL;
			kfree(reap, M_REAPER);
			lockmgr(&reaper_lock, LK_RELEASE);
		} else {
			next = NULL;
		}
	}
}

/*
 * Initialize a static or newly allocated reaper structure
 */
void
reaper_init(struct proc *p, struct sysreaper *reap)
{
	reap->parent = p->p_reaper;
	reap->p = p;
	if (p == initproc) {
		reap->flags = REAPER_STAT_OWNED | REAPER_STAT_REALINIT;
		reap->refs = 2;
	} else {
		reap->flags = REAPER_STAT_OWNED;
		reap->refs = 1;
	}
	lockinit(&reap->lock, "subrp", 0, 0);
	cpu_sfence();
	p->p_reaper = reap;
}

/*
 * Called with p->p_token held during exit.
 *
 * This is a bit simpler than RELEASE because there are no threads remaining
 * to race.  We only release if we own the reaper, the exit code will handle
 * the final p_reaper release.
 */
struct sysreaper *
reaper_exit(struct proc *p)
{
	struct sysreaper *reap;

	/*
	 * Release acquired reaper
	 */
	if ((reap = p->p_reaper) != NULL && reap->p == p) {
		lockmgr(&reap->lock, LK_EXCLUSIVE);
		p->p_reaper = reap->parent;
		if (p->p_reaper)
			reaper_hold(p->p_reaper);
		reap->p = NULL;
		lockmgr(&reap->lock, LK_RELEASE);
		reaper_drop(reap);
	}

	/*
	 * Return and clear reaper (caller is holding p_token for us)
	 * (reap->p does not equal p).  Caller must drop it.
	 */
	if ((reap = p->p_reaper) != NULL) {
		p->p_reaper = NULL;
	}
	return reap;
}

/*
 * Return a held (PHOLD) process representing the reaper for process (p).
 * NULL should not normally be returned.  Caller should PRELE() the returned
 * reaper process when finished.
 *
 * Remove dead internal nodes while we are at it.
 *
 * Process (p)'s token must be held on call.
 * The returned process's token is NOT acquired by this routine.
 */
struct proc *
reaper_get(struct sysreaper *reap)
{
	struct sysreaper *next;
	struct proc *reproc;

	if (reap == NULL)
		return NULL;

	/*
	 * Extra hold for loop
	 */
	reaper_hold(reap);

	while (reap) {
		lockmgr(&reap->lock, LK_SHARED);
		if (reap->p) {
			/*
			 * Probable reaper
			 */
			if (reap->p) {
				reproc = reap->p;
				PHOLD(reproc);
				lockmgr(&reap->lock, LK_RELEASE);
				reaper_drop(reap);
				return reproc;
			}

			/*
			 * Raced, try again
			 */
			lockmgr(&reap->lock, LK_RELEASE);
			continue;
		}

		/*
		 * Traverse upwards in the reaper topology, destroy
		 * dead internal nodes when possible.
		 *
		 * NOTE: Our ref on next means that a dead node should
		 *	 have 2 (ours and reap->parent's).
		 */
		next = reap->parent;
		while (next) {
			reaper_hold(next);
			if (next->refs == 2 && next->p == NULL) {
				lockmgr(&reap->lock, LK_RELEASE);
				lockmgr(&reap->lock, LK_EXCLUSIVE);
				if (next->refs == 2 &&
				    reap->parent == next &&
				    next->p == NULL) {
					/*
					 * reap->parent inherits ref from next.
					 */
					reap->parent = next->parent;
					next->parent = NULL;
					reaper_drop(next);	/* ours */
					reaper_drop(next);	/* old parent */
					next = reap->parent;
					continue;	/* possible chain */
				}
			}
			break;
		}
		lockmgr(&reap->lock, LK_RELEASE);
		reaper_drop(reap);
		reap = next;
	}
	return NULL;
}

/*
 * Test that the sender is allowed to send a signal to the target.
 * The sender process is assumed to have a stable reaper.  The
 * target can be e.g. from a scan callback.
 *
 * Target cannot be the reaper process itself unless reaper_ok is specified,
 * or sender == target.
 */
int
reaper_sigtest(struct proc *sender, struct proc *target, int reaper_ok)
{
	struct sysreaper *sreap;
	struct sysreaper *reap;
	int r;

	sreap = sender->p_reaper;
	if (sreap == NULL)
		return 1;

	if (sreap == target->p_reaper) {
		if (sreap->p == target && sreap->p != sender && reaper_ok == 0)
			return 0;
		return 1;
	}
	lockmgr(&reaper_lock, LK_SHARED);
	r = 0;
	for (reap = target->p_reaper; reap; reap = reap->parent) {
		if (sreap == reap) {
			if (sreap->p != target || reaper_ok)
				r = 1;
			break;
		}
	}
	lockmgr(&reaper_lock, LK_RELEASE);

	return r;
}
