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
 *	@(#)kern_exit.c	8.7 (Berkeley) 2/12/94
 * $FreeBSD: src/sys/kern/kern_exit.c,v 1.92.2.11 2003/01/13 22:51:16 dillon Exp $
 */

#include "opt_compat.h"
#include "opt_ktrace.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/sysproto.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/proc.h>
#include <sys/ktrace.h>
#include <sys/pioctl.h>
#include <sys/tty.h>
#include <sys/wait.h>
#include <sys/vnode.h>
#include <sys/resourcevar.h>
#include <sys/signalvar.h>
#include <sys/taskqueue.h>
#include <sys/ptrace.h>
#include <sys/acct.h>		/* for acct_process() function prototype */
#include <sys/filedesc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <sys/jail.h>
#include <sys/kern_syscall.h>
#include <sys/unistd.h>
#include <sys/eventhandler.h>
#include <sys/dsched.h>

#include <vm/vm.h>
#include <vm/vm_param.h>
#include <sys/lock.h>
#include <vm/pmap.h>
#include <vm/vm_map.h>
#include <vm/vm_extern.h>
#include <sys/user.h>

#include <sys/refcount.h>
#include <sys/thread2.h>
#include <sys/sysref2.h>
#include <sys/mplock2.h>

static void reaplwps(void *context, int dummy);
static void reaplwp(struct lwp *lp);
static void killlwps(struct lwp *lp);

static MALLOC_DEFINE(M_ATEXIT, "atexit", "atexit callback");

static struct lwkt_token deadlwp_token = LWKT_TOKEN_INITIALIZER(deadlwp_token);

/*
 * callout list for things to do at exit time
 */
struct exitlist {
	exitlist_fn function;
	TAILQ_ENTRY(exitlist) next;
};

TAILQ_HEAD(exit_list_head, exitlist);
static struct exit_list_head exit_list = TAILQ_HEAD_INITIALIZER(exit_list);

/*
 * LWP reaper data
 */
struct task *deadlwp_task[MAXCPU];
struct lwplist deadlwp_list[MAXCPU];

/*
 * exit --
 *	Death of process.
 *
 * SYS_EXIT_ARGS(int rval)
 */
int
sys_exit(struct exit_args *uap)
{
	exit1(W_EXITCODE(uap->rval, 0));
	/* NOTREACHED */
}

/*
 * Extended exit --
 *	Death of a lwp or process with optional bells and whistles.
 *
 * MPALMOSTSAFE
 */
int
sys_extexit(struct extexit_args *uap)
{
	struct proc *p = curproc;
	int action, who;
	int error;

	action = EXTEXIT_ACTION(uap->how);
	who = EXTEXIT_WHO(uap->how);

	/* Check parameters before we might perform some action */
	switch (who) {
	case EXTEXIT_PROC:
	case EXTEXIT_LWP:
		break;
	default:
		return (EINVAL);
	}

	switch (action) {
	case EXTEXIT_SIMPLE:
		break;
	case EXTEXIT_SETINT:
		error = copyout(&uap->status, uap->addr, sizeof(uap->status));
		if (error)
			return (error);
		break;
	default:
		return (EINVAL);
	}

	lwkt_gettoken(&p->p_token);

	switch (who) {
	case EXTEXIT_LWP:
		/*
		 * Be sure only to perform a simple lwp exit if there is at
		 * least one more lwp in the proc, which will call exit1()
		 * later, otherwise the proc will be an UNDEAD and not even a
		 * SZOMB!
		 */
		if (p->p_nthreads > 1) {
			lwp_exit(0);	/* called w/ p_token held */
			/* NOT REACHED */
		}
		/* else last lwp in proc:  do the real thing */
		/* FALLTHROUGH */
	default:	/* to help gcc */
	case EXTEXIT_PROC:
		lwkt_reltoken(&p->p_token);
		exit1(W_EXITCODE(uap->status, 0));
		/* NOTREACHED */
	}

	/* NOTREACHED */
	lwkt_reltoken(&p->p_token);	/* safety */
}

/*
 * Kill all lwps associated with the current process except the
 * current lwp.   Return an error if we race another thread trying to
 * do the same thing and lose the race.
 *
 * If forexec is non-zero the current thread and process flags are
 * cleaned up so they can be reused.
 *
 * Caller must hold curproc->p_token
 */
int
killalllwps(int forexec)
{
	struct lwp *lp = curthread->td_lwp;
	struct proc *p = lp->lwp_proc;

	/*
	 * Interlock against P_WEXIT.  Only one of the process's thread
	 * is allowed to do the master exit.
	 */
	if (p->p_flags & P_WEXIT)
		return (EALREADY);
	p->p_flags |= P_WEXIT;

	/*
	 * Interlock with LWP_MP_WEXIT and kill any remaining LWPs
	 */
	atomic_set_int(&lp->lwp_mpflags, LWP_MP_WEXIT);
	if (p->p_nthreads > 1)
		killlwps(lp);

	/*
	 * If doing this for an exec, clean up the remaining thread
	 * (us) for continuing operation after all the other threads
	 * have been killed.
	 */
	if (forexec) {
		atomic_clear_int(&lp->lwp_mpflags, LWP_MP_WEXIT);
		p->p_flags &= ~P_WEXIT;
	}
	return(0);
}

/*
 * Kill all LWPs except the current one.  Do not try to signal
 * LWPs which have exited on their own or have already been
 * signaled.
 */
static void
killlwps(struct lwp *lp)
{
	struct proc *p = lp->lwp_proc;
	struct lwp *tlp;

	/*
	 * Kill the remaining LWPs.  We must send the signal before setting
	 * LWP_MP_WEXIT.  The setting of WEXIT is optional but helps reduce
	 * races.  tlp must be held across the call as it might block and
	 * allow the target lwp to rip itself out from under our loop.
	 */
	FOREACH_LWP_IN_PROC(tlp, p) {
		LWPHOLD(tlp);
		lwkt_gettoken(&tlp->lwp_token);
		if ((tlp->lwp_mpflags & LWP_MP_WEXIT) == 0) {
			lwpsignal(p, tlp, SIGKILL);
			atomic_set_int(&tlp->lwp_mpflags, LWP_MP_WEXIT);
		}
		lwkt_reltoken(&tlp->lwp_token);
		LWPRELE(tlp);
	}

	/*
	 * Wait for everything to clear out.
	 */
	while (p->p_nthreads > 1) {
		tsleep(&p->p_nthreads, 0, "killlwps", 0);
	}
}

/*
 * Exit: deallocate address space and other resources, change proc state
 * to zombie, and unlink proc from allproc and parent's lists.  Save exit
 * status and rusage for wait().  Check for child processes and orphan them.
 */
void
exit1(int rv)
{
	struct thread *td = curthread;
	struct proc *p = td->td_proc;
	struct lwp *lp = td->td_lwp;
	struct proc *q;
	struct vmspace *vm;
	struct vnode *vtmp;
	struct exitlist *ep;
	int error;

	lwkt_gettoken(&p->p_token);

	if (p->p_pid == 1) {
		kprintf("init died (signal %d, exit %d)\n",
		    WTERMSIG(rv), WEXITSTATUS(rv));
		panic("Going nowhere without my init!");
	}
	varsymset_clean(&p->p_varsymset);
	lockuninit(&p->p_varsymset.vx_lock);

	/*
	 * Kill all lwps associated with the current process, return an
	 * error if we race another thread trying to do the same thing
	 * and lose the race.
	 */
	error = killalllwps(0);
	if (error) {
		lwp_exit(0);
		/* NOT REACHED */
	}

	/* are we a task leader? */
	if (p == p->p_leader) {
        	struct kill_args killArgs;
		killArgs.signum = SIGKILL;
		q = p->p_peers;
		while(q) {
			killArgs.pid = q->p_pid;
			/*
		         * The interface for kill is better
			 * than the internal signal
			 */
			sys_kill(&killArgs);
			q = q->p_peers;
		}
		while (p->p_peers) 
			tsleep((caddr_t)p, 0, "exit1", 0);
	}

#ifdef PGINPROF
	vmsizmon();
#endif
	STOPEVENT(p, S_EXIT, rv);
	p->p_flags |= P_POSTEXIT;	/* stop procfs stepping */

	/* 
	 * Check if any loadable modules need anything done at process exit.
	 * e.g. SYSV IPC stuff
	 * XXX what if one of these generates an error?
	 */
	p->p_xstat = rv;
	EVENTHANDLER_INVOKE(process_exit, p);

	/*
	 * XXX: imho, the eventhandler stuff is much cleaner than this.
	 *	Maybe we should move everything to use eventhandler.
	 */
	TAILQ_FOREACH(ep, &exit_list, next) 
		(*ep->function)(td);

	if (p->p_flags & P_PROFIL)
		stopprofclock(p);

	SIGEMPTYSET(p->p_siglist);
	SIGEMPTYSET(lp->lwp_siglist);
	if (timevalisset(&p->p_realtimer.it_value))
		callout_stop_sync(&p->p_ithandle);

	/*
	 * Reset any sigio structures pointing to us as a result of
	 * F_SETOWN with our pid.
	 */
	funsetownlst(&p->p_sigiolst);

	/*
	 * Close open files and release open-file table.
	 * This may block!
	 */
	fdfree(p, NULL);

	if(p->p_leader->p_peers) {
		q = p->p_leader;
		while(q->p_peers != p)
			q = q->p_peers;
		q->p_peers = p->p_peers;
		wakeup((caddr_t)p->p_leader);
	}

	/*
	 * XXX Shutdown SYSV semaphores
	 */
	semexit(p);

	KKASSERT(p->p_numposixlocks == 0);

	/* The next two chunks should probably be moved to vmspace_exit. */
	vm = p->p_vmspace;

	/*
	 * Clean up data related to virtual kernel operation.  Clean up
	 * any vkernel context related to the current lwp now so we can
	 * destroy p_vkernel.
	 */
	if (p->p_vkernel) {
		vkernel_lwp_exit(lp);
		vkernel_exit(p);
	}

	/*
	 * Release user portion of address space.
	 * This releases references to vnodes,
	 * which could cause I/O if the file has been unlinked.
	 * Need to do this early enough that we can still sleep.
	 * Can't free the entire vmspace as the kernel stack
	 * may be mapped within that space also.
	 *
	 * Processes sharing the same vmspace may exit in one order, and
	 * get cleaned up by vmspace_exit() in a different order.  The
	 * last exiting process to reach this point releases as much of
	 * the environment as it can, and the last process cleaned up
	 * by vmspace_exit() (which decrements exitingcnt) cleans up the
	 * remainder.
	 */
	vmspace_exitbump(vm);
	sysref_put(&vm->vm_sysref);

	if (SESS_LEADER(p)) {
		struct session *sp = p->p_session;

		if (sp->s_ttyvp) {
			/*
			 * We are the controlling process.  Signal the 
			 * foreground process group, drain the controlling
			 * terminal, and revoke access to the controlling
			 * terminal.
			 *
			 * NOTE: while waiting for the process group to exit
			 * it is possible that one of the processes in the
			 * group will revoke the tty, so the ttyclosesession()
			 * function will re-check sp->s_ttyvp.
			 */
			if (sp->s_ttyp && (sp->s_ttyp->t_session == sp)) {
				if (sp->s_ttyp->t_pgrp)
					pgsignal(sp->s_ttyp->t_pgrp, SIGHUP, 1);
				ttywait(sp->s_ttyp);
				ttyclosesession(sp, 1); /* also revoke */
			}
			/*
			 * Release the tty.  If someone has it open via
			 * /dev/tty then close it (since they no longer can
			 * once we've NULL'd it out).
			 */
			ttyclosesession(sp, 0);

			/*
			 * s_ttyp is not zero'd; we use this to indicate
			 * that the session once had a controlling terminal.
			 * (for logging and informational purposes)
			 */
		}
		sp->s_leader = NULL;
	}
	fixjobc(p, p->p_pgrp, 0);
	(void)acct_process(p);
#ifdef KTRACE
	/*
	 * release trace file
	 */
	if (p->p_tracenode)
		ktrdestroy(&p->p_tracenode);
	p->p_traceflag = 0;
#endif
	/*
	 * Release reference to text vnode
	 */
	if ((vtmp = p->p_textvp) != NULL) {
		p->p_textvp = NULL;
		vrele(vtmp);
	}

	/* Release namecache handle to text file */
	if (p->p_textnch.ncp)
		cache_drop(&p->p_textnch);

	/*
	 * We have to handle PPWAIT here or proc_move_allproc_zombie()
	 * will block on the PHOLD() the parent is doing.
	 */
	if (p->p_flags & P_PPWAIT) {
		p->p_flags &= ~P_PPWAIT;
		wakeup(p->p_pptr);
	}

	/*
	 * Move the process to the zombie list.  This will block
	 * until the process p_lock count reaches 0.  The process will
	 * not be reaped until TDF_EXITING is set by cpu_thread_exit(),
	 * which is called from cpu_proc_exit().
	 */
	proc_move_allproc_zombie(p);

	/*
	 * Reparent all of this process's children to the init process.
	 * We must hold initproc->p_token in order to mess with
	 * initproc->p_children.  We already hold p->p_token (to remove
	 * the children from our list).
	 */
	q = LIST_FIRST(&p->p_children);
	if (q) {
		lwkt_gettoken(&initproc->p_token);
		while ((q = LIST_FIRST(&p->p_children)) != NULL) {
			PHOLD(q);
			lwkt_gettoken(&q->p_token);
			if (q != LIST_FIRST(&p->p_children)) {
				lwkt_reltoken(&q->p_token);
				PRELE(q);
				continue;
			}
			LIST_REMOVE(q, p_sibling);
			LIST_INSERT_HEAD(&initproc->p_children, q, p_sibling);
			q->p_pptr = initproc;
			q->p_sigparent = SIGCHLD;

			/*
			 * Traced processes are killed
			 * since their existence means someone is screwing up.
			 */
			if (q->p_flags & P_TRACED) {
				q->p_flags &= ~P_TRACED;
				ksignal(q, SIGKILL);
			}
			lwkt_reltoken(&q->p_token);
			PRELE(q);
		}
		lwkt_reltoken(&initproc->p_token);
		wakeup(initproc);
	}

	/*
	 * Save exit status and final rusage info, adding in child rusage
	 * info and self times.
	 */
	calcru_proc(p, &p->p_ru);
	ruadd(&p->p_ru, &p->p_cru);

	/*
	 * notify interested parties of our demise.
	 */
	KNOTE(&p->p_klist, NOTE_EXIT);

	/*
	 * Notify parent that we're gone.  If parent has the PS_NOCLDWAIT
	 * flag set, or if the handler is set to SIG_IGN, notify process 1
	 * instead (and hope it will handle this situation).
	 */
	if (p->p_pptr->p_sigacts->ps_flag & (PS_NOCLDWAIT | PS_CLDSIGIGN)) {
		proc_reparent(p, initproc);
	}

	/* lwkt_gettoken(&proc_token); */
	q = p->p_pptr;
	PHOLD(q);
	if (p->p_sigparent && q != initproc) {
	        ksignal(q, p->p_sigparent);
	} else {
	        ksignal(q, SIGCHLD);
	}

	p->p_flags &= ~P_TRACED;
	wakeup(p->p_pptr);

	PRELE(q);
	/* lwkt_reltoken(&proc_token); */
	/* NOTE: p->p_pptr can get ripped out */
	/*
	 * cpu_exit is responsible for clearing curproc, since
	 * it is heavily integrated with the thread/switching sequence.
	 *
	 * Other substructures are freed from wait().
	 */
	plimit_free(p);

	/*
	 * Release the current user process designation on the process so
	 * the userland scheduler can work in someone else.
	 */
	p->p_usched->release_curproc(lp);

	/*
	 * Finally, call machine-dependent code to release as many of the
	 * lwp's resources as we can and halt execution of this thread.
	 */
	lwp_exit(1);
}

/*
 * Eventually called by every exiting LWP
 *
 * p->p_token must be held.  mplock may be held and will be released.
 */
void
lwp_exit(int masterexit)
{
	struct thread *td = curthread;
	struct lwp *lp = td->td_lwp;
	struct proc *p = lp->lwp_proc;
	int dowake = 0;

	/*
	 * lwp_exit() may be called without setting LWP_MP_WEXIT, so
	 * make sure it is set here.
	 */
	ASSERT_LWKT_TOKEN_HELD(&p->p_token);
	atomic_set_int(&lp->lwp_mpflags, LWP_MP_WEXIT);

	/*
	 * Clean up any virtualization
	 */
	if (lp->lwp_vkernel)
		vkernel_lwp_exit(lp);

	/*
	 * Clean up select/poll support
	 */
	kqueue_terminate(&lp->lwp_kqueue);

	/*
	 * Clean up any syscall-cached ucred
	 */
	if (td->td_ucred) {
		crfree(td->td_ucred);
		td->td_ucred = NULL;
	}

	/*
	 * Nobody actually wakes us when the lock
	 * count reaches zero, so just wait one tick.
	 */
	while (lp->lwp_lock > 0)
		tsleep(lp, 0, "lwpexit", 1);

	/* Hand down resource usage to our proc */
	ruadd(&p->p_ru, &lp->lwp_ru);

	/*
	 * If we don't hold the process until the LWP is reaped wait*()
	 * may try to dispose of its vmspace before all the LWPs have
	 * actually terminated.
	 */
	PHOLD(p);

	/*
	 * Do any remaining work that might block on us.  We should be
	 * coded such that further blocking is ok after decrementing
	 * p_nthreads but don't take the chance.
	 */
	dsched_exit_thread(td);
	biosched_done(curthread);

	/*
	 * We have to use the reaper for all the LWPs except the one doing
	 * the master exit.  The LWP doing the master exit can just be
	 * left on p_lwps and the process reaper will deal with it
	 * synchronously, which is much faster.
	 *
	 * Wakeup anyone waiting on p_nthreads to drop to 1 or 0.
	 *
	 * The process is left held until the reaper calls lwp_dispose() on
	 * the lp (after calling lwp_wait()).
	 */
	if (masterexit == 0) {
		lwp_rb_tree_RB_REMOVE(&p->p_lwp_tree, lp);
		--p->p_nthreads;
		if (p->p_nthreads <= 1)
			dowake = 1;
		lwkt_gettoken(&deadlwp_token);
		LIST_INSERT_HEAD(&deadlwp_list[mycpuid], lp, u.lwp_reap_entry);
		taskqueue_enqueue(taskqueue_thread[mycpuid],
				  deadlwp_task[mycpuid]);
		lwkt_reltoken(&deadlwp_token);
	} else {
		--p->p_nthreads;
		if (p->p_nthreads <= 1)
			dowake = 1;
	}

	/*
	 * Release p_token.  Issue the wakeup() on p_nthreads if necessary,
	 * as late as possible to give us a chance to actually deschedule and
	 * switch away before another cpu core hits reaplwp().
	 */
	lwkt_reltoken(&p->p_token);
	if (dowake)
		wakeup(&p->p_nthreads);

	/*
	 * Tell the userland scheduler that we are going away
	 */
	p->p_usched->heuristic_exiting(lp, p);

	cpu_lwp_exit();
}

/*
 * Wait until a lwp is completely dead.  The final interlock in this drama
 * is when TDF_EXITING is set in cpu_thread_exit() just before the final
 * switchout.
 *
 * At the point TDF_EXITING is set a complete exit is accomplished when
 * TDF_RUNNING and TDF_PREEMPT_LOCK are both clear.  td_mpflags has two
 * post-switch interlock flags that can be used to wait for the TDF_
 * flags to clear.
 *
 * Returns non-zero on success, and zero if the caller needs to retry
 * the lwp_wait().
 */
static int
lwp_wait(struct lwp *lp)
{
	struct thread *td = lp->lwp_thread;
	u_int mpflags;

	KKASSERT(lwkt_preempted_proc() != lp);

	/*
	 * This bit of code uses the thread destruction interlock
	 * managed by lwkt_switch_return() to wait for the lwp's
	 * thread to completely disengage.
	 *
	 * It is possible for us to race another cpu core so we
	 * have to do this correctly.
	 */
	for (;;) {
		mpflags = td->td_mpflags;
		cpu_ccfence();
		if (mpflags & TDF_MP_EXITSIG)
			break;
		tsleep_interlock(td, 0);
		if (atomic_cmpset_int(&td->td_mpflags, mpflags,
				      mpflags | TDF_MP_EXITWAIT)) {
			tsleep(td, PINTERLOCKED, "lwpxt", 0);
		}
	}

	/*
	 * We've already waited for the core exit but there can still
	 * be other refs from e.g. process scans and such.
	 */
	if (lp->lwp_lock > 0) {
		tsleep(lp, 0, "lwpwait1", 1);
		return(0);
	}
	if (td->td_refs) {
		tsleep(td, 0, "lwpwait2", 1);
		return(0);
	}

	/*
	 * Now that we have the thread destruction interlock these flags
	 * really should already be cleaned up, keep a check for safety.
	 *
	 * We can't rip its stack out from under it until TDF_EXITING is
	 * set and both TDF_RUNNING and TDF_PREEMPT_LOCK are clear.
	 * TDF_PREEMPT_LOCK must be checked because TDF_RUNNING
	 * will be cleared temporarily if a thread gets preempted.
	 */
	while ((td->td_flags & (TDF_RUNNING |
			        TDF_PREEMPT_LOCK |
			        TDF_EXITING)) != TDF_EXITING) {
		tsleep(lp, 0, "lwpwait3", 1);
		return (0);
	}

	KASSERT((td->td_flags & (TDF_RUNQ|TDF_TSLEEPQ)) == 0,
		("lwp_wait: td %p (%s) still on run or sleep queue",
		td, td->td_comm));
	return (1);
}

/*
 * Release the resources associated with a lwp.
 * The lwp must be completely dead.
 */
void
lwp_dispose(struct lwp *lp)
{
	struct thread *td = lp->lwp_thread;

	KKASSERT(lwkt_preempted_proc() != lp);
	KKASSERT(td->td_refs == 0);
	KKASSERT((td->td_flags & (TDF_RUNNING |
				  TDF_PREEMPT_LOCK |
				  TDF_EXITING)) == TDF_EXITING);

	PRELE(lp->lwp_proc);
	lp->lwp_proc = NULL;
	if (td != NULL) {
		td->td_proc = NULL;
		td->td_lwp = NULL;
		lp->lwp_thread = NULL;
		lwkt_free_thread(td);
	}
	kfree(lp, M_LWP);
}

/*
 * MPSAFE
 */
int
sys_wait4(struct wait_args *uap)
{
	struct rusage rusage;
	int error, status;

	error = kern_wait(uap->pid, (uap->status ? &status : NULL),
			  uap->options, (uap->rusage ? &rusage : NULL),
			  &uap->sysmsg_result);

	if (error == 0 && uap->status)
		error = copyout(&status, uap->status, sizeof(*uap->status));
	if (error == 0 && uap->rusage)
		error = copyout(&rusage, uap->rusage, sizeof(*uap->rusage));
	return (error);
}

/*
 * wait1()
 *
 * wait_args(int pid, int *status, int options, struct rusage *rusage)
 *
 * MPALMOSTSAFE
 */
int
kern_wait(pid_t pid, int *status, int options, struct rusage *rusage, int *res)
{
	struct thread *td = curthread;
	struct lwp *lp;
	struct proc *q = td->td_proc;
	struct proc *p, *t;
	struct pargs *pa;
	struct sigacts *ps;
	int nfound, error;

	if (pid == 0)
		pid = -q->p_pgid;
	if (options &~ (WUNTRACED|WNOHANG|WCONTINUED|WLINUXCLONE))
		return (EINVAL);

	lwkt_gettoken(&q->p_token);
loop:
	/*
	 * All sorts of things can change due to blocking so we have to loop
	 * all the way back up here.
	 *
	 * The problem is that if a process group is stopped and the parent
	 * is doing a wait*(..., WUNTRACED, ...), it will see the STOP
	 * of the child and then stop itself when it tries to return from the
	 * system call.  When the process group is resumed the parent will
	 * then get the STOP status even though the child has now resumed
	 * (a followup wait*() will get the CONT status).
	 *
	 * Previously the CONT would overwrite the STOP because the tstop
	 * was handled within tsleep(), and the parent would only see
	 * the CONT when both are stopped and continued together.  This little
	 * two-line hack restores this effect.
	 */
	while (q->p_stat == SSTOP)
            tstop();

	nfound = 0;

	/*
	 * Loop on children.
	 *
	 * NOTE: We don't want to break q's p_token in the loop for the
	 *	 case where no children are found or we risk breaking the
	 *	 interlock between child and parent.
	 */
	LIST_FOREACH(p, &q->p_children, p_sibling) {
		if (pid != WAIT_ANY &&
		    p->p_pid != pid && p->p_pgid != -pid) {
			continue;
		}

		/*
		 * This special case handles a kthread spawned by linux_clone
		 * (see linux_misc.c).  The linux_wait4 and linux_waitpid 
		 * functions need to be able to distinguish between waiting
		 * on a process and waiting on a thread.  It is a thread if
		 * p_sigparent is not SIGCHLD, and the WLINUXCLONE option
		 * signifies we want to wait for threads and not processes.
		 */
		if ((p->p_sigparent != SIGCHLD) ^ 
		    ((options & WLINUXCLONE) != 0)) {
			continue;
		}

		nfound++;
		if (p->p_stat == SZOMB) {
			/*
			 * We may go into SZOMB with threads still present.
			 * We must wait for them to exit before we can reap
			 * the master thread, otherwise we may race reaping
			 * non-master threads.
			 *
			 * Only this routine can remove a process from
			 * the zombie list and destroy it, use PACQUIREZOMB()
			 * to serialize us and loop if it blocks (interlocked
			 * by the parent's q->p_token).
			 *
			 * WARNING!  (p) can be invalid when PHOLDZOMB(p)
			 *	     returns non-zero.  Be sure not to
			 *	     mess with it.
			 */
			if (PHOLDZOMB(p))
				goto loop;
			lwkt_gettoken(&p->p_token);
			if (p->p_pptr != q) {
				lwkt_reltoken(&p->p_token);
				PRELEZOMB(p);
				goto loop;
			}
			while (p->p_nthreads > 0) {
				tsleep(&p->p_nthreads, 0, "lwpzomb", hz);
			}

			/*
			 * Reap any LWPs left in p->p_lwps.  This is usually
			 * just the last LWP.  This must be done before
			 * we loop on p_lock since the lwps hold a ref on
			 * it as a vmspace interlock.
			 *
			 * Once that is accomplished p_nthreads had better
			 * be zero.
			 */
			while ((lp = RB_ROOT(&p->p_lwp_tree)) != NULL) {
				lwp_rb_tree_RB_REMOVE(&p->p_lwp_tree, lp);
				reaplwp(lp);
			}
			KKASSERT(p->p_nthreads == 0);

			/*
			 * Don't do anything really bad until all references
			 * to the process go away.  This may include other
			 * LWPs which are still in the process of being
			 * reaped.  We can't just pull the rug out from under
			 * them because they may still be using the VM space.
			 *
			 * Certain kernel facilities such as /proc will also
			 * put a hold on the process for short periods of
			 * time.
			 */
			PRELE(p);
			PSTALL(p, "reap3", 0);

			/* Take care of our return values. */
			*res = p->p_pid;

			if (status)
				*status = p->p_xstat;
			if (rusage)
				*rusage = p->p_ru;
			/*
			 * If we got the child via a ptrace 'attach',
			 * we need to give it back to the old parent.
			 */
			if (p->p_oppid && (t = pfind(p->p_oppid)) != NULL) {
				PHOLD(p);
				p->p_oppid = 0;
				proc_reparent(p, t);
				ksignal(t, SIGCHLD);
				wakeup((caddr_t)t);
				error = 0;
				PRELE(t);
				lwkt_reltoken(&p->p_token);
				PRELEZOMB(p);
				goto done;
			}

			/*
			 * Unlink the proc from its process group so that
			 * the following operations won't lead to an
			 * inconsistent state for processes running down
			 * the zombie list.
			 */
			proc_remove_zombie(p);
			lwkt_reltoken(&p->p_token);
			leavepgrp(p);

			p->p_xstat = 0;
			ruadd(&q->p_cru, &p->p_ru);

			/*
			 * Decrement the count of procs running with this uid.
			 */
			chgproccnt(p->p_ucred->cr_ruidinfo, -1, 0);

			/*
			 * Free up credentials.
			 */
			crfree(p->p_ucred);
			p->p_ucred = NULL;

			/*
			 * Remove unused arguments
			 */
			pa = p->p_args;
			p->p_args = NULL;
			if (pa && refcount_release(&pa->ar_ref)) {
				kfree(pa, M_PARGS);
				pa = NULL;
			}

			ps = p->p_sigacts;
			p->p_sigacts = NULL;
			if (ps && refcount_release(&ps->ps_refcnt)) {
				kfree(ps, M_SUBPROC);
				ps = NULL;
			}

			/*
			 * Our exitingcount was incremented when the process
			 * became a zombie, now that the process has been
			 * removed from (almost) all lists we should be able
			 * to safely destroy its vmspace.  Wait for any current
			 * holders to go away (so the vmspace remains stable),
			 * then scrap it.
			 */
			PSTALL(p, "reap4", 0);
			vmspace_exitfree(p);
			PSTALL(p, "reap5", 0);

			/*
			 * NOTE: We have to officially release ZOMB in order
			 *	 to ensure that a racing thread in kern_wait()
			 *	 which blocked on ZOMB is woken up.
			 */
			PHOLD(p);
			PRELEZOMB(p);
			kfree(p, M_PROC);
			atomic_add_int(&nprocs, -1);
			error = 0;
			goto done;
		}
		if (p->p_stat == SSTOP && (p->p_flags & P_WAITED) == 0 &&
		    ((p->p_flags & P_TRACED) || (options & WUNTRACED))) {
			PHOLD(p);
			lwkt_gettoken(&p->p_token);
			if (p->p_pptr != q) {
				lwkt_reltoken(&p->p_token);
				PRELE(p);
				goto loop;
			}
			if (p->p_stat != SSTOP ||
			    (p->p_flags & P_WAITED) != 0 ||
			    ((p->p_flags & P_TRACED) == 0 &&
			     (options & WUNTRACED) == 0)) {
				lwkt_reltoken(&p->p_token);
				PRELE(p);
				goto loop;
			}

			p->p_flags |= P_WAITED;

			*res = p->p_pid;
			if (status)
				*status = W_STOPCODE(p->p_xstat);
			/* Zero rusage so we get something consistent. */
			if (rusage)
				bzero(rusage, sizeof(*rusage));
			error = 0;
			lwkt_reltoken(&p->p_token);
			PRELE(p);
			goto done;
		}
		if ((options & WCONTINUED) && (p->p_flags & P_CONTINUED)) {
			PHOLD(p);
			lwkt_gettoken(&p->p_token);
			if (p->p_pptr != q) {
				lwkt_reltoken(&p->p_token);
				PRELE(p);
				goto loop;
			}
			if ((p->p_flags & P_CONTINUED) == 0) {
				lwkt_reltoken(&p->p_token);
				PRELE(p);
				goto loop;
			}

			*res = p->p_pid;
			p->p_flags &= ~P_CONTINUED;

			if (status)
				*status = SIGCONT;
			error = 0;
			lwkt_reltoken(&p->p_token);
			PRELE(p);
			goto done;
		}
	}
	if (nfound == 0) {
		error = ECHILD;
		goto done;
	}
	if (options & WNOHANG) {
		*res = 0;
		error = 0;
		goto done;
	}

	/*
	 * Wait for signal - interlocked using q->p_token.
	 */
	error = tsleep(q, PCATCH, "wait", 0);
	if (error) {
done:
		lwkt_reltoken(&q->p_token);
		return (error);
	}
	goto loop;
}

/*
 * Change child's parent process to parent.
 *
 * p_children/p_sibling requires the parent's token, and
 * changing pptr requires the child's token, so we have to
 * get three tokens to do this operation.  We also need to
 * hold pointers that might get ripped out from under us to
 * preserve structural integrity.
 *
 * It is possible to race another reparent or disconnect or other
 * similar operation.  We must retry when this situation occurs.
 * Once we successfully reparent the process we no longer care
 * about any races.
 */
void
proc_reparent(struct proc *child, struct proc *parent)
{
	struct proc *opp;

	PHOLD(parent);
	while ((opp = child->p_pptr) != parent) {
		PHOLD(opp);
		lwkt_gettoken(&opp->p_token);
		lwkt_gettoken(&child->p_token);
		lwkt_gettoken(&parent->p_token);
		if (child->p_pptr != opp) {
			lwkt_reltoken(&parent->p_token);
			lwkt_reltoken(&child->p_token);
			lwkt_reltoken(&opp->p_token);
			PRELE(opp);
			continue;
		}
		LIST_REMOVE(child, p_sibling);
		LIST_INSERT_HEAD(&parent->p_children, child, p_sibling);
		child->p_pptr = parent;
		lwkt_reltoken(&parent->p_token);
		lwkt_reltoken(&child->p_token);
		lwkt_reltoken(&opp->p_token);
		if (LIST_EMPTY(&opp->p_children))
			wakeup(opp);
		PRELE(opp);
		break;
	}
	PRELE(parent);
}

/*
 * The next two functions are to handle adding/deleting items on the
 * exit callout list
 * 
 * at_exit():
 * Take the arguments given and put them onto the exit callout list,
 * However first make sure that it's not already there.
 * returns 0 on success.
 */

int
at_exit(exitlist_fn function)
{
	struct exitlist *ep;

#ifdef INVARIANTS
	/* Be noisy if the programmer has lost track of things */
	if (rm_at_exit(function)) 
		kprintf("WARNING: exit callout entry (%p) already present\n",
		    function);
#endif
	ep = kmalloc(sizeof(*ep), M_ATEXIT, M_NOWAIT);
	if (ep == NULL)
		return (ENOMEM);
	ep->function = function;
	TAILQ_INSERT_TAIL(&exit_list, ep, next);
	return (0);
}

/*
 * Scan the exit callout list for the given item and remove it.
 * Returns the number of items removed (0 or 1)
 */
int
rm_at_exit(exitlist_fn function)
{
	struct exitlist *ep;

	TAILQ_FOREACH(ep, &exit_list, next) {
		if (ep->function == function) {
			TAILQ_REMOVE(&exit_list, ep, next);
			kfree(ep, M_ATEXIT);
			return(1);
		}
	}	
	return (0);
}

/*
 * LWP reaper related code.
 */
static void
reaplwps(void *context, int dummy)
{
	struct lwplist *lwplist = context;
	struct lwp *lp;

	lwkt_gettoken(&deadlwp_token);
	while ((lp = LIST_FIRST(lwplist))) {
		LIST_REMOVE(lp, u.lwp_reap_entry);
		reaplwp(lp);
	}
	lwkt_reltoken(&deadlwp_token);
}

static void
reaplwp(struct lwp *lp)
{
	while (lwp_wait(lp) == 0)
		;
	lwp_dispose(lp);
}

static void
deadlwp_init(void)
{
	int cpu;

	for (cpu = 0; cpu < ncpus; cpu++) {
		LIST_INIT(&deadlwp_list[cpu]);
		deadlwp_task[cpu] = kmalloc(sizeof(*deadlwp_task[cpu]),
					    M_DEVBUF, M_WAITOK);
		TASK_INIT(deadlwp_task[cpu], 0, reaplwps, &deadlwp_list[cpu]);
	}
}

SYSINIT(deadlwpinit, SI_SUB_CONFIGURE, SI_ORDER_ANY, deadlwp_init, NULL);
