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
 *	@(#)kern_sig.c	8.7 (Berkeley) 4/18/94
 * $FreeBSD: src/sys/kern/kern_sig.c,v 1.72.2.17 2003/05/16 16:34:34 obrien Exp $
 */

#include "opt_ktrace.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/sysproto.h>
#include <sys/signalvar.h>
#include <sys/resourcevar.h>
#include <sys/vnode.h>
#include <sys/event.h>
#include <sys/proc.h>
#include <sys/nlookup.h>
#include <sys/pioctl.h>
#include <sys/acct.h>
#include <sys/fcntl.h>
#include <sys/lock.h>
#include <sys/wait.h>
#include <sys/ktrace.h>
#include <sys/syslog.h>
#include <sys/stat.h>
#include <sys/sysent.h>
#include <sys/sysctl.h>
#include <sys/malloc.h>
#include <sys/interrupt.h>
#include <sys/unistd.h>
#include <sys/kern_syscall.h>
#include <sys/vkernel.h>

#include <sys/signal2.h>
#include <sys/thread2.h>
#include <sys/spinlock2.h>

#include <machine/cpu.h>
#include <machine/smp.h>

static int	coredump(struct lwp *, int);
static char	*expand_name(const char *, uid_t, pid_t);
static int	dokillpg(int sig, int pgid, int all);
static int	sig_ffs(sigset_t *set);
static int	sigprop(int sig);
static void	lwp_signotify(struct lwp *lp);
static void	lwp_signotify_remote(void *arg);
static int	kern_sigtimedwait(sigset_t set, siginfo_t *info,
		    struct timespec *timeout);

static int	filt_sigattach(struct knote *kn);
static void	filt_sigdetach(struct knote *kn);
static int	filt_signal(struct knote *kn, long hint);

struct filterops sig_filtops =
	{ 0, filt_sigattach, filt_sigdetach, filt_signal };

static int	kern_logsigexit = 1;
SYSCTL_INT(_kern, KERN_LOGSIGEXIT, logsigexit, CTLFLAG_RW, 
    &kern_logsigexit, 0, 
    "Log processes quitting on abnormal signals to syslog(3)");

/*
 * Can process p, with pcred pc, send the signal sig to process q?
 */
#define CANSIGNAL(q, sig) \
	(!p_trespass(curproc->p_ucred, (q)->p_ucred) || \
	((sig) == SIGCONT && (q)->p_session == curproc->p_session))

/*
 * Policy -- Can real uid ruid with ucred uc send a signal to process q?
 */
#define CANSIGIO(ruid, uc, q) \
	((uc)->cr_uid == 0 || \
	    (ruid) == (q)->p_ucred->cr_ruid || \
	    (uc)->cr_uid == (q)->p_ucred->cr_ruid || \
	    (ruid) == (q)->p_ucred->cr_uid || \
	    (uc)->cr_uid == (q)->p_ucred->cr_uid)

int sugid_coredump;
SYSCTL_INT(_kern, OID_AUTO, sugid_coredump, CTLFLAG_RW, 
	&sugid_coredump, 0, "Enable coredumping set user/group ID processes");

static int	do_coredump = 1;
SYSCTL_INT(_kern, OID_AUTO, coredump, CTLFLAG_RW,
	&do_coredump, 0, "Enable/Disable coredumps");

/*
 * Signal properties and actions.
 * The array below categorizes the signals and their default actions
 * according to the following properties:
 */
#define	SA_KILL		0x01		/* terminates process by default */
#define	SA_CORE		0x02		/* ditto and coredumps */
#define	SA_STOP		0x04		/* suspend process */
#define	SA_TTYSTOP	0x08		/* ditto, from tty */
#define	SA_IGNORE	0x10		/* ignore by default */
#define	SA_CONT		0x20		/* continue if suspended */
#define	SA_CANTMASK	0x40		/* non-maskable, catchable */
#define SA_CKPT         0x80            /* checkpoint process */


static int sigproptbl[NSIG] = {
        SA_KILL,                /* SIGHUP */
        SA_KILL,                /* SIGINT */
        SA_KILL|SA_CORE,        /* SIGQUIT */
        SA_KILL|SA_CORE,        /* SIGILL */
        SA_KILL|SA_CORE,        /* SIGTRAP */
        SA_KILL|SA_CORE,        /* SIGABRT */
        SA_KILL|SA_CORE,        /* SIGEMT */
        SA_KILL|SA_CORE,        /* SIGFPE */
        SA_KILL,                /* SIGKILL */
        SA_KILL|SA_CORE,        /* SIGBUS */
        SA_KILL|SA_CORE,        /* SIGSEGV */
        SA_KILL|SA_CORE,        /* SIGSYS */
        SA_KILL,                /* SIGPIPE */
        SA_KILL,                /* SIGALRM */
        SA_KILL,                /* SIGTERM */
        SA_IGNORE,              /* SIGURG */
        SA_STOP,                /* SIGSTOP */
        SA_STOP|SA_TTYSTOP,     /* SIGTSTP */
        SA_IGNORE|SA_CONT,      /* SIGCONT */
        SA_IGNORE,              /* SIGCHLD */
        SA_STOP|SA_TTYSTOP,     /* SIGTTIN */
        SA_STOP|SA_TTYSTOP,     /* SIGTTOU */
        SA_IGNORE,              /* SIGIO */
        SA_KILL,                /* SIGXCPU */
        SA_KILL,                /* SIGXFSZ */
        SA_KILL,                /* SIGVTALRM */
        SA_KILL,                /* SIGPROF */
        SA_IGNORE,              /* SIGWINCH  */
        SA_IGNORE,              /* SIGINFO */
        SA_KILL,                /* SIGUSR1 */
        SA_KILL,                /* SIGUSR2 */
	SA_IGNORE,              /* SIGTHR */
	SA_CKPT,                /* SIGCKPT */ 
	SA_KILL|SA_CKPT,        /* SIGCKPTEXIT */  
	SA_IGNORE,
	SA_IGNORE,
	SA_IGNORE,
	SA_IGNORE,
	SA_IGNORE,
	SA_IGNORE,
	SA_IGNORE,
	SA_IGNORE,
	SA_IGNORE,
	SA_IGNORE,
	SA_IGNORE,
	SA_IGNORE,
	SA_IGNORE,
	SA_IGNORE,
	SA_IGNORE,
	SA_IGNORE,
	SA_IGNORE,
	SA_IGNORE,
	SA_IGNORE,
	SA_IGNORE,
	SA_IGNORE,
	SA_IGNORE,
	SA_IGNORE,
	SA_IGNORE,
	SA_IGNORE,
	SA_IGNORE,
	SA_IGNORE,
	SA_IGNORE,
	SA_IGNORE,
	SA_IGNORE,

};

static __inline int
sigprop(int sig)
{

	if (sig > 0 && sig < NSIG)
		return (sigproptbl[_SIG_IDX(sig)]);
	return (0);
}

static __inline int
sig_ffs(sigset_t *set)
{
	int i;

	for (i = 0; i < _SIG_WORDS; i++)
		if (set->__bits[i])
			return (ffs(set->__bits[i]) + (i * 32));
	return (0);
}

/* 
 * No requirements. 
 */
int
kern_sigaction(int sig, struct sigaction *act, struct sigaction *oact)
{
	struct thread *td = curthread;
	struct proc *p = td->td_proc;
	struct lwp *lp;
	struct sigacts *ps = p->p_sigacts;

	if (sig <= 0 || sig > _SIG_MAXSIG)
		return (EINVAL);

	lwkt_gettoken(&p->p_token);

	if (oact) {
		oact->sa_handler = ps->ps_sigact[_SIG_IDX(sig)];
		oact->sa_mask = ps->ps_catchmask[_SIG_IDX(sig)];
		oact->sa_flags = 0;
		if (SIGISMEMBER(ps->ps_sigonstack, sig))
			oact->sa_flags |= SA_ONSTACK;
		if (!SIGISMEMBER(ps->ps_sigintr, sig))
			oact->sa_flags |= SA_RESTART;
		if (SIGISMEMBER(ps->ps_sigreset, sig))
			oact->sa_flags |= SA_RESETHAND;
		if (SIGISMEMBER(ps->ps_signodefer, sig))
			oact->sa_flags |= SA_NODEFER;
		if (SIGISMEMBER(ps->ps_siginfo, sig))
			oact->sa_flags |= SA_SIGINFO;
		if (sig == SIGCHLD && p->p_sigacts->ps_flag & PS_NOCLDSTOP)
			oact->sa_flags |= SA_NOCLDSTOP;
		if (sig == SIGCHLD && p->p_sigacts->ps_flag & PS_NOCLDWAIT)
			oact->sa_flags |= SA_NOCLDWAIT;
	}
	if (act) {
		/*
		 * Check for invalid requests.  KILL and STOP cannot be
		 * caught.
		 */
		if (sig == SIGKILL || sig == SIGSTOP) {
			if (act->sa_handler != SIG_DFL) {
				lwkt_reltoken(&p->p_token);
				return (EINVAL);
			}
		}

		/*
		 * Change setting atomically.
		 */
		ps->ps_catchmask[_SIG_IDX(sig)] = act->sa_mask;
		SIG_CANTMASK(ps->ps_catchmask[_SIG_IDX(sig)]);
		if (act->sa_flags & SA_SIGINFO) {
			ps->ps_sigact[_SIG_IDX(sig)] =
			    (__sighandler_t *)act->sa_sigaction;
			SIGADDSET(ps->ps_siginfo, sig);
		} else {
			ps->ps_sigact[_SIG_IDX(sig)] = act->sa_handler;
			SIGDELSET(ps->ps_siginfo, sig);
		}
		if (!(act->sa_flags & SA_RESTART))
			SIGADDSET(ps->ps_sigintr, sig);
		else
			SIGDELSET(ps->ps_sigintr, sig);
		if (act->sa_flags & SA_ONSTACK)
			SIGADDSET(ps->ps_sigonstack, sig);
		else
			SIGDELSET(ps->ps_sigonstack, sig);
		if (act->sa_flags & SA_RESETHAND)
			SIGADDSET(ps->ps_sigreset, sig);
		else
			SIGDELSET(ps->ps_sigreset, sig);
		if (act->sa_flags & SA_NODEFER)
			SIGADDSET(ps->ps_signodefer, sig);
		else
			SIGDELSET(ps->ps_signodefer, sig);
		if (sig == SIGCHLD) {
			if (act->sa_flags & SA_NOCLDSTOP)
				p->p_sigacts->ps_flag |= PS_NOCLDSTOP;
			else
				p->p_sigacts->ps_flag &= ~PS_NOCLDSTOP;
			if (act->sa_flags & SA_NOCLDWAIT) {
				/*
				 * Paranoia: since SA_NOCLDWAIT is implemented
				 * by reparenting the dying child to PID 1 (and
				 * trust it to reap the zombie), PID 1 itself
				 * is forbidden to set SA_NOCLDWAIT.
				 */
				if (p->p_pid == 1)
					p->p_sigacts->ps_flag &= ~PS_NOCLDWAIT;
				else
					p->p_sigacts->ps_flag |= PS_NOCLDWAIT;
			} else {
				p->p_sigacts->ps_flag &= ~PS_NOCLDWAIT;
			}
			if (ps->ps_sigact[_SIG_IDX(SIGCHLD)] == SIG_IGN)
				ps->ps_flag |= PS_CLDSIGIGN;
			else
				ps->ps_flag &= ~PS_CLDSIGIGN;
		}
		/*
		 * Set bit in p_sigignore for signals that are set to SIG_IGN,
		 * and for signals set to SIG_DFL where the default is to
		 * ignore. However, don't put SIGCONT in p_sigignore, as we
		 * have to restart the process.
		 *
		 * Also remove the signal from the process and lwp signal
		 * list.
		 */
		if (ps->ps_sigact[_SIG_IDX(sig)] == SIG_IGN ||
		    (sigprop(sig) & SA_IGNORE &&
		     ps->ps_sigact[_SIG_IDX(sig)] == SIG_DFL)) {
			SIGDELSET(p->p_siglist, sig);
			FOREACH_LWP_IN_PROC(lp, p) {
				spin_lock(&lp->lwp_spin);
				SIGDELSET(lp->lwp_siglist, sig);
				spin_unlock(&lp->lwp_spin);
			}
			if (sig != SIGCONT) {
				/* easier in ksignal */
				SIGADDSET(p->p_sigignore, sig);
			}
			SIGDELSET(p->p_sigcatch, sig);
		} else {
			SIGDELSET(p->p_sigignore, sig);
			if (ps->ps_sigact[_SIG_IDX(sig)] == SIG_DFL)
				SIGDELSET(p->p_sigcatch, sig);
			else
				SIGADDSET(p->p_sigcatch, sig);
		}
	}
	lwkt_reltoken(&p->p_token);
	return (0);
}

int
sys_sigaction(struct sigaction_args *uap)
{
	struct sigaction act, oact;
	struct sigaction *actp, *oactp;
	int error;

	actp = (uap->act != NULL) ? &act : NULL;
	oactp = (uap->oact != NULL) ? &oact : NULL;
	if (actp) {
		error = copyin(uap->act, actp, sizeof(act));
		if (error)
			return (error);
	}
	error = kern_sigaction(uap->sig, actp, oactp);
	if (oactp && !error) {
		error = copyout(oactp, uap->oact, sizeof(oact));
	}
	return (error);
}

/*
 * Initialize signal state for process 0;
 * set to ignore signals that are ignored by default.
 */
void
siginit(struct proc *p)
{
	int i;

	for (i = 1; i <= NSIG; i++)
		if (sigprop(i) & SA_IGNORE && i != SIGCONT)
			SIGADDSET(p->p_sigignore, i);
}

/*
 * Reset signals for an exec of the specified process.
 */
void
execsigs(struct proc *p)
{
	struct sigacts *ps = p->p_sigacts;
	struct lwp *lp;
	int sig;

	lp = ONLY_LWP_IN_PROC(p);

	/*
	 * Reset caught signals.  Held signals remain held
	 * through p_sigmask (unless they were caught,
	 * and are now ignored by default).
	 */
	while (SIGNOTEMPTY(p->p_sigcatch)) {
		sig = sig_ffs(&p->p_sigcatch);
		SIGDELSET(p->p_sigcatch, sig);
		if (sigprop(sig) & SA_IGNORE) {
			if (sig != SIGCONT)
				SIGADDSET(p->p_sigignore, sig);
			SIGDELSET(p->p_siglist, sig);
			/* don't need spinlock */
			SIGDELSET(lp->lwp_siglist, sig);
		}
		ps->ps_sigact[_SIG_IDX(sig)] = SIG_DFL;
	}

	/*
	 * Reset stack state to the user stack.
	 * Clear set of signals caught on the signal stack.
	 */
	lp->lwp_sigstk.ss_flags = SS_DISABLE;
	lp->lwp_sigstk.ss_size = 0;
	lp->lwp_sigstk.ss_sp = NULL;
	lp->lwp_flags &= ~LWP_ALTSTACK;
	/*
	 * Reset no zombies if child dies flag as Solaris does.
	 */
	p->p_sigacts->ps_flag &= ~(PS_NOCLDWAIT | PS_CLDSIGIGN);
	if (ps->ps_sigact[_SIG_IDX(SIGCHLD)] == SIG_IGN)
		ps->ps_sigact[_SIG_IDX(SIGCHLD)] = SIG_DFL;
}

/*
 * kern_sigprocmask() - MP SAFE ONLY IF p == curproc
 *
 *	Manipulate signal mask.  This routine is MP SAFE *ONLY* if
 *	p == curproc.
 */
int
kern_sigprocmask(int how, sigset_t *set, sigset_t *oset)
{
	struct thread *td = curthread;
	struct lwp *lp = td->td_lwp;
	struct proc *p = td->td_proc;
	int error;

	lwkt_gettoken(&p->p_token);

	if (oset != NULL)
		*oset = lp->lwp_sigmask;

	error = 0;
	if (set != NULL) {
		switch (how) {
		case SIG_BLOCK:
			SIG_CANTMASK(*set);
			SIGSETOR(lp->lwp_sigmask, *set);
			break;
		case SIG_UNBLOCK:
			SIGSETNAND(lp->lwp_sigmask, *set);
			break;
		case SIG_SETMASK:
			SIG_CANTMASK(*set);
			lp->lwp_sigmask = *set;
			break;
		default:
			error = EINVAL;
			break;
		}
	}

	lwkt_reltoken(&p->p_token);

	return (error);
}

/*
 * sigprocmask()
 *
 * MPSAFE
 */
int
sys_sigprocmask(struct sigprocmask_args *uap)
{
	sigset_t set, oset;
	sigset_t *setp, *osetp;
	int error;

	setp = (uap->set != NULL) ? &set : NULL;
	osetp = (uap->oset != NULL) ? &oset : NULL;
	if (setp) {
		error = copyin(uap->set, setp, sizeof(set));
		if (error)
			return (error);
	}
	error = kern_sigprocmask(uap->how, setp, osetp);
	if (osetp && !error) {
		error = copyout(osetp, uap->oset, sizeof(oset));
	}
	return (error);
}

/*
 * MPSAFE
 */
int
kern_sigpending(struct __sigset *set)
{
	struct lwp *lp = curthread->td_lwp;

	*set = lwp_sigpend(lp);

	return (0);
}

/*
 * MPSAFE
 */
int
sys_sigpending(struct sigpending_args *uap)
{
	sigset_t set;
	int error;

	error = kern_sigpending(&set);

	if (error == 0)
		error = copyout(&set, uap->set, sizeof(set));
	return (error);
}

/*
 * Suspend process until signal, providing mask to be set
 * in the meantime.
 *
 * MPSAFE
 */
int
kern_sigsuspend(struct __sigset *set)
{
	struct thread *td = curthread;
	struct lwp *lp = td->td_lwp;
	struct proc *p = td->td_proc;
	struct sigacts *ps = p->p_sigacts;

	/*
	 * When returning from sigsuspend, we want
	 * the old mask to be restored after the
	 * signal handler has finished.  Thus, we
	 * save it here and mark the sigacts structure
	 * to indicate this.
	 */
	lp->lwp_oldsigmask = lp->lwp_sigmask;
	lp->lwp_flags |= LWP_OLDMASK;

	SIG_CANTMASK(*set);
	lp->lwp_sigmask = *set;
	while (tsleep(ps, PCATCH, "pause", 0) == 0)
		/* void */;
	/* always return EINTR rather than ERESTART... */
	return (EINTR);
}

/*
 * Note nonstandard calling convention: libc stub passes mask, not
 * pointer, to save a copyin.
 *
 * MPSAFE
 */
int
sys_sigsuspend(struct sigsuspend_args *uap)
{
	sigset_t mask;
	int error;

	error = copyin(uap->sigmask, &mask, sizeof(mask));
	if (error)
		return (error);

	error = kern_sigsuspend(&mask);

	return (error);
}

/*
 * MPSAFE
 */
int
kern_sigaltstack(struct sigaltstack *ss, struct sigaltstack *oss)
{
	struct thread *td = curthread;
	struct lwp *lp = td->td_lwp;
	struct proc *p = td->td_proc;

	if ((lp->lwp_flags & LWP_ALTSTACK) == 0)
		lp->lwp_sigstk.ss_flags |= SS_DISABLE;

	if (oss)
		*oss = lp->lwp_sigstk;

	if (ss) {
		if (ss->ss_flags & ~SS_DISABLE)
			return (EINVAL);
		if (ss->ss_flags & SS_DISABLE) {
			if (lp->lwp_sigstk.ss_flags & SS_ONSTACK)
				return (EINVAL);
			lp->lwp_flags &= ~LWP_ALTSTACK;
			lp->lwp_sigstk.ss_flags = ss->ss_flags;
		} else {
			if (ss->ss_size < p->p_sysent->sv_minsigstksz)
				return (ENOMEM);
			lp->lwp_flags |= LWP_ALTSTACK;
			lp->lwp_sigstk = *ss;
		}
	}

	return (0);
}

/*
 * MPSAFE
 */
int
sys_sigaltstack(struct sigaltstack_args *uap)
{
	stack_t ss, oss;
	int error;

	if (uap->ss) {
		error = copyin(uap->ss, &ss, sizeof(ss));
		if (error)
			return (error);
	}

	error = kern_sigaltstack(uap->ss ? &ss : NULL,
	    uap->oss ? &oss : NULL);

	if (error == 0 && uap->oss)
		error = copyout(&oss, uap->oss, sizeof(*uap->oss));
	return (error);
}

/*
 * Common code for kill process group/broadcast kill.
 * cp is calling process.
 */
struct killpg_info {
	int nfound;
	int sig;
};

static int killpg_all_callback(struct proc *p, void *data);

static int
dokillpg(int sig, int pgid, int all)
{
	struct killpg_info info;
	struct proc *cp = curproc;
	struct proc *p;
	struct pgrp *pgrp;

	info.nfound = 0;
	info.sig = sig;

	if (all) {
		/*
		 * broadcast
		 */
		allproc_scan(killpg_all_callback, &info);
	} else {
		if (pgid == 0) {
			/*
			 * zero pgid means send to my process group.
			 */
			pgrp = cp->p_pgrp;
			pgref(pgrp);
		} else {
			pgrp = pgfind(pgid);
			if (pgrp == NULL)
				return (ESRCH);
		}

		/*
		 * Must interlock all signals against fork
		 */
		lockmgr(&pgrp->pg_lock, LK_EXCLUSIVE);
		LIST_FOREACH(p, &pgrp->pg_members, p_pglist) {
			if (p->p_pid <= 1 || 
			    p->p_stat == SZOMB ||
			    (p->p_flags & P_SYSTEM) ||
			    !CANSIGNAL(p, sig)) {
				continue;
			}
			++info.nfound;
			if (sig)
				ksignal(p, sig);
		}
		lockmgr(&pgrp->pg_lock, LK_RELEASE);
		pgrel(pgrp);
	}
	return (info.nfound ? 0 : ESRCH);
}

static int
killpg_all_callback(struct proc *p, void *data)
{
	struct killpg_info *info = data;

	if (p->p_pid <= 1 || (p->p_flags & P_SYSTEM) ||
	    p == curproc || !CANSIGNAL(p, info->sig)) {
		return (0);
	}
	++info->nfound;
	if (info->sig)
		ksignal(p, info->sig);
	return(0);
}

/*
 * Send a general signal to a process or LWPs within that process.
 *
 * Note that new signals cannot be sent if a process is exiting or already
 * a zombie, but we return success anyway as userland is likely to not handle
 * the race properly.
 * 
 * No requirements.
 */
int
kern_kill(int sig, pid_t pid, lwpid_t tid)
{
	int t;

	if ((u_int)sig > _SIG_MAXSIG)
		return (EINVAL);

	if (pid > 0) {
		struct proc *p;
		struct lwp *lp = NULL;

		/*
		 * Send a signal to a single process.  If the kill() is
		 * racing an exiting process which has not yet been reaped
		 * act as though the signal was delivered successfully but
		 * don't actually try to deliver the signal.
		 */
		if ((p = pfind(pid)) == NULL) {
			if ((p = zpfind(pid)) == NULL)
				return (ESRCH);
			PRELE(p);
			return (0);
		}
		lwkt_gettoken(&p->p_token);
		if (!CANSIGNAL(p, sig)) {
			lwkt_reltoken(&p->p_token);
			PRELE(p);
			return (EPERM);
		}

		/*
		 * NOP if the process is exiting.  Note that lwpsignal() is
		 * called directly with P_WEXIT set to kill individual LWPs
		 * during exit, which is allowed.
		 */
		if (p->p_flags & P_WEXIT) {
			lwkt_reltoken(&p->p_token);
			PRELE(p);
			return (0);
		}
		if (tid != -1) {
			lp = lwp_rb_tree_RB_LOOKUP(&p->p_lwp_tree, tid);
			if (lp == NULL) {
				lwkt_reltoken(&p->p_token);
				PRELE(p);
				return (ESRCH);
			}
		}
		if (sig)
			lwpsignal(p, lp, sig);
		lwkt_reltoken(&p->p_token);
		PRELE(p);

		return (0);
	}

	/*
	 * If we come here, pid is a special broadcast pid.
	 * This doesn't mix with a tid.
	 */
	if (tid != -1)
		return (EINVAL);

	switch (pid) {
	case -1:		/* broadcast signal */
		t = (dokillpg(sig, 0, 1));
		break;
	case 0:			/* signal own process group */
		t = (dokillpg(sig, 0, 0));
		break;
	default:		/* negative explicit process group */
		t = (dokillpg(sig, -pid, 0));
		break;
	}
	return t;
}

int
sys_kill(struct kill_args *uap)
{
	int error;

	error = kern_kill(uap->signum, uap->pid, -1);
	return (error);
}

int
sys_lwp_kill(struct lwp_kill_args *uap)
{
	int error;
	pid_t pid = uap->pid;

	/*
	 * A tid is mandatory for lwp_kill(), otherwise
	 * you could simply use kill().
	 */
	if (uap->tid == -1)
		return (EINVAL);

	/*
	 * To save on a getpid() function call for intra-process
	 * signals, pid == -1 means current process.
	 */
	if (pid == -1)
		pid = curproc->p_pid;

	error = kern_kill(uap->signum, pid, uap->tid);
	return (error);
}

/*
 * Send a signal to a process group.
 */
void
gsignal(int pgid, int sig)
{
	struct pgrp *pgrp;

	if (pgid && (pgrp = pgfind(pgid)))
		pgsignal(pgrp, sig, 0);
}

/*
 * Send a signal to a process group.  If checktty is 1,
 * limit to members which have a controlling terminal.
 *
 * pg_lock interlocks against a fork that might be in progress, to
 * ensure that the new child process picks up the signal.
 */
void
pgsignal(struct pgrp *pgrp, int sig, int checkctty)
{
	struct proc *p;

	/*
	 * Must interlock all signals against fork
	 */
	if (pgrp) {
		pgref(pgrp);
		lockmgr(&pgrp->pg_lock, LK_EXCLUSIVE);
		LIST_FOREACH(p, &pgrp->pg_members, p_pglist) {
			if (checkctty == 0 || p->p_flags & P_CONTROLT)
				ksignal(p, sig);
		}
		lockmgr(&pgrp->pg_lock, LK_RELEASE);
		pgrel(pgrp);
	}
}

/*
 * Send a signal caused by a trap to the current lwp.  If it will be caught
 * immediately, deliver it with correct code.  Otherwise, post it normally.
 *
 * These signals may ONLY be delivered to the specified lwp and may never
 * be delivered to the process generically.
 */
void
trapsignal(struct lwp *lp, int sig, u_long code)
{
	struct proc *p = lp->lwp_proc;
	struct sigacts *ps = p->p_sigacts;

	/*
	 * If we are a virtual kernel running an emulated user process
	 * context, switch back to the virtual kernel context before
	 * trying to post the signal.
	 */
	if (lp->lwp_vkernel && lp->lwp_vkernel->ve) {
		struct trapframe *tf = lp->lwp_md.md_regs;
		tf->tf_trapno = 0;
		vkernel_trap(lp, tf);
	}


	if ((p->p_flags & P_TRACED) == 0 && SIGISMEMBER(p->p_sigcatch, sig) &&
	    !SIGISMEMBER(lp->lwp_sigmask, sig)) {
		lp->lwp_ru.ru_nsignals++;
#ifdef KTRACE
		if (KTRPOINT(lp->lwp_thread, KTR_PSIG))
			ktrpsig(lp, sig, ps->ps_sigact[_SIG_IDX(sig)],
				&lp->lwp_sigmask, code);
#endif
		(*p->p_sysent->sv_sendsig)(ps->ps_sigact[_SIG_IDX(sig)], sig,
						&lp->lwp_sigmask, code);
		SIGSETOR(lp->lwp_sigmask, ps->ps_catchmask[_SIG_IDX(sig)]);
		if (!SIGISMEMBER(ps->ps_signodefer, sig))
			SIGADDSET(lp->lwp_sigmask, sig);
		if (SIGISMEMBER(ps->ps_sigreset, sig)) {
			/*
			 * See kern_sigaction() for origin of this code.
			 */
			SIGDELSET(p->p_sigcatch, sig);
			if (sig != SIGCONT &&
			    sigprop(sig) & SA_IGNORE)
				SIGADDSET(p->p_sigignore, sig);
			ps->ps_sigact[_SIG_IDX(sig)] = SIG_DFL;
		}
	} else {
		lp->lwp_code = code;	/* XXX for core dump/debugger */
		lp->lwp_sig = sig;	/* XXX to verify code */
		lwpsignal(p, lp, sig);
	}
}

/*
 * Find a suitable lwp to deliver the signal to.  Returns NULL if all
 * lwps hold the signal blocked.
 *
 * Caller must hold p->p_token.
 *
 * Returns a lp or NULL.  If non-NULL the lp is held and its token is
 * acquired.
 */
static struct lwp *
find_lwp_for_signal(struct proc *p, int sig)
{
	struct lwp *lp;
	struct lwp *run, *sleep, *stop;

	/*
	 * If the running/preempted thread belongs to the proc to which
	 * the signal is being delivered and this thread does not block
	 * the signal, then we can avoid a context switch by delivering
	 * the signal to this thread, because it will return to userland
	 * soon anyways.
	 */
	lp = lwkt_preempted_proc();
	if (lp != NULL && lp->lwp_proc == p) {
		LWPHOLD(lp);
		lwkt_gettoken(&lp->lwp_token);
		if (!SIGISMEMBER(lp->lwp_sigmask, sig)) {
			/* return w/ token held */
			return (lp);
		}
		lwkt_reltoken(&lp->lwp_token);
		LWPRELE(lp);
	}

	run = sleep = stop = NULL;
	FOREACH_LWP_IN_PROC(lp, p) {
		/*
		 * If the signal is being blocked by the lwp, then this
		 * lwp is not eligible for receiving the signal.
		 */
		LWPHOLD(lp);
		lwkt_gettoken(&lp->lwp_token);

		if (SIGISMEMBER(lp->lwp_sigmask, sig)) {
			lwkt_reltoken(&lp->lwp_token);
			LWPRELE(lp);
			continue;
		}

		switch (lp->lwp_stat) {
		case LSRUN:
			if (sleep) {
				lwkt_token_swap();
				lwkt_reltoken(&sleep->lwp_token);
				LWPRELE(sleep);
				sleep = NULL;
				run = lp;
			} else if (stop) {
				lwkt_token_swap();
				lwkt_reltoken(&stop->lwp_token);
				LWPRELE(stop);
				stop = NULL;
				run = lp;
			} else {
				run = lp;
			}
			break;
		case LSSLEEP:
			if (lp->lwp_flags & LWP_SINTR) {
				if (sleep) {
					lwkt_reltoken(&lp->lwp_token);
					LWPRELE(lp);
				} else if (stop) {
					lwkt_token_swap();
					lwkt_reltoken(&stop->lwp_token);
					LWPRELE(stop);
					stop = NULL;
					sleep = lp;
				} else {
					sleep = lp;
				}
			} else {
				lwkt_reltoken(&lp->lwp_token);
				LWPRELE(lp);
			}
			break;
		case LSSTOP:
			if (sleep) {
				lwkt_reltoken(&lp->lwp_token);
				LWPRELE(lp);
			} else if (stop) {
				lwkt_reltoken(&lp->lwp_token);
				LWPRELE(lp);
			} else {
				stop = lp;
			}
			break;
		}
		if (run)
			break;
	}

	if (run != NULL)
		return (run);
	else if (sleep != NULL)
		return (sleep);
	else
		return (stop);
}

/*
 * Send the signal to the process.  If the signal has an action, the action
 * is usually performed by the target process rather than the caller; we add
 * the signal to the set of pending signals for the process.
 *
 * Exceptions:
 *   o When a stop signal is sent to a sleeping process that takes the
 *     default action, the process is stopped without awakening it.
 *   o SIGCONT restarts stopped processes (or puts them back to sleep)
 *     regardless of the signal action (eg, blocked or ignored).
 *
 * Other ignored signals are discarded immediately.
 *
 * If the caller wishes to call this function from a hard code section the
 * caller must already hold p->p_token (see kern_clock.c).
 *
 * No requirements.
 */
void
ksignal(struct proc *p, int sig)
{
	lwpsignal(p, NULL, sig);
}

/*
 * The core for ksignal.  lp may be NULL, then a suitable thread
 * will be chosen.  If not, lp MUST be a member of p.
 *
 * If the caller wishes to call this function from a hard code section the
 * caller must already hold p->p_token.
 *
 * No requirements.
 */
void
lwpsignal(struct proc *p, struct lwp *lp, int sig)
{
	struct proc *q;
	sig_t action;
	int prop;

	if (sig > _SIG_MAXSIG || sig <= 0) {
		kprintf("lwpsignal: signal %d\n", sig);
		panic("lwpsignal signal number");
	}

	KKASSERT(lp == NULL || lp->lwp_proc == p);

	/*
	 * We don't want to race... well, all sorts of things.  Get appropriate
	 * tokens.
	 *
	 * Don't try to deliver a generic signal to an exiting process,
	 * the signal structures could be in flux.  We check the LWP later
	 * on.
	 */
	PHOLD(p);
	lwkt_gettoken(&p->p_token);
	if (lp) {
		LWPHOLD(lp);
		lwkt_gettoken(&lp->lwp_token);
	} else if (p->p_flags & P_WEXIT) {
		goto out;
	}

	prop = sigprop(sig);

	/*
	 * If proc is traced, always give parent a chance;
	 * if signal event is tracked by procfs, give *that*
	 * a chance, as well.
	 */
	if ((p->p_flags & P_TRACED) || (p->p_stops & S_SIG)) {
		action = SIG_DFL;
	} else {
		/*
		 * Do not try to deliver signals to an exiting lwp.  Note
		 * that we must still deliver the signal if P_WEXIT is set
		 * in the process flags.
		 */
		if (lp && (lp->lwp_mpflags & LWP_MP_WEXIT)) {
			if (lp) {
				lwkt_reltoken(&lp->lwp_token);
				LWPRELE(lp);
			}
			lwkt_reltoken(&p->p_token);
			PRELE(p);
			return;
		}

		/*
		 * If the signal is being ignored, then we forget about
		 * it immediately.  NOTE: We don't set SIGCONT in p_sigignore,
		 * and if it is set to SIG_IGN, action will be SIG_DFL here.
		 */
		if (SIGISMEMBER(p->p_sigignore, sig)) {
			/*
			 * Even if a signal is set SIG_IGN, it may still be
			 * lurking in a kqueue.
			 */
			KNOTE(&p->p_klist, NOTE_SIGNAL | sig);
			if (lp) {
				lwkt_reltoken(&lp->lwp_token);
				LWPRELE(lp);
			}
			lwkt_reltoken(&p->p_token);
			PRELE(p);
			return;
		}
		if (SIGISMEMBER(p->p_sigcatch, sig))
			action = SIG_CATCH;
		else
			action = SIG_DFL;
	}

	/*
	 * If continuing, clear any pending STOP signals.
	 */
	if (prop & SA_CONT)
		SIG_STOPSIGMASK(p->p_siglist);
	
	if (prop & SA_STOP) {
		/*
		 * If sending a tty stop signal to a member of an orphaned
		 * process group, discard the signal here if the action
		 * is default; don't stop the process below if sleeping,
		 * and don't clear any pending SIGCONT.
		 */
		if (prop & SA_TTYSTOP && p->p_pgrp->pg_jobc == 0 &&
		    action == SIG_DFL) {
			if (lp) {
				lwkt_reltoken(&lp->lwp_token);
				LWPRELE(lp);
			}
			lwkt_reltoken(&p->p_token);
			PRELE(p);
		        return;
		}
		SIG_CONTSIGMASK(p->p_siglist);
		p->p_flags &= ~P_CONTINUED;
	}

	if (p->p_stat == SSTOP) {
		/*
		 * Nobody can handle this signal, add it to the lwp or
		 * process pending list 
		 */
		if (lp) {
			spin_lock(&lp->lwp_spin);
			SIGADDSET(lp->lwp_siglist, sig);
			spin_unlock(&lp->lwp_spin);
		} else {
			SIGADDSET(p->p_siglist, sig);
		}

		/*
		 * If the process is stopped and is being traced, then no
		 * further action is necessary.
		 */
		if (p->p_flags & P_TRACED)
			goto out;

		/*
		 * If the process is stopped and receives a KILL signal,
		 * make the process runnable.
		 */
		if (sig == SIGKILL) {
			proc_unstop(p);
			goto active_process;
		}

		/*
		 * If the process is stopped and receives a CONT signal,
		 * then try to make the process runnable again.
		 */
		if (prop & SA_CONT) {
			/*
			 * If SIGCONT is default (or ignored), we continue the
			 * process but don't leave the signal in p_siglist, as
			 * it has no further action.  If SIGCONT is held, we
			 * continue the process and leave the signal in
			 * p_siglist.  If the process catches SIGCONT, let it
			 * handle the signal itself.
			 *
			 * XXX what if the signal is being held blocked?
			 *
			 * Token required to interlock kern_wait().
			 * Reparenting can also cause a race so we have to
			 * hold (q).
			 */
			q = p->p_pptr;
			PHOLD(q);
			lwkt_gettoken(&q->p_token);
			p->p_flags |= P_CONTINUED;
			wakeup(q);
			if (action == SIG_DFL)
				SIGDELSET(p->p_siglist, sig);
			proc_unstop(p);
			lwkt_reltoken(&q->p_token);
			PRELE(q);
			if (action == SIG_CATCH)
				goto active_process;
			goto out;
		}

		/*
		 * If the process is stopped and receives another STOP
		 * signal, we do not need to stop it again.  If we did
		 * the shell could get confused.
		 *
		 * However, if the current/preempted lwp is part of the
		 * process receiving the signal, we need to keep it,
		 * so that this lwp can stop in issignal() later, as
		 * we don't want to wait until it reaches userret!
		 */
		if (prop & SA_STOP) {
			if (lwkt_preempted_proc() == NULL ||
			    lwkt_preempted_proc()->lwp_proc != p)
				SIGDELSET(p->p_siglist, sig);
		}

		/*
		 * Otherwise the process is stopped and it received some
		 * signal, which does not change its stopped state.  When
		 * the process is continued a wakeup(p) will be issued which
		 * will wakeup any threads sleeping in tstop().
		 */
		if (lp == NULL) {
			/* NOTE: returns lp w/ token held */
			lp = find_lwp_for_signal(p, sig);
		}
		goto out;

		/* NOTREACHED */
	}
	/* else not stopped */
active_process:

	/*
	 * Never deliver a lwp-specific signal to a random lwp.
	 */
	if (lp == NULL) {
		/* NOTE: returns lp w/ token held */
		lp = find_lwp_for_signal(p, sig);
		if (lp) {
			if (SIGISMEMBER(lp->lwp_sigmask, sig)) {
				lwkt_reltoken(&lp->lwp_token);
				LWPRELE(lp);
				lp = NULL;
			}
		}
	}

	/*
	 * Deliver to the process generically if (1) the signal is being
	 * sent to any thread or (2) we could not find a thread to deliver
	 * it to.
	 */
	if (lp == NULL) {
		SIGADDSET(p->p_siglist, sig);
		goto out;
	}

	/*
	 * Deliver to a specific LWP whether it masks it or not.  It will
	 * not be dispatched if masked but we must still deliver it.
	 */
	if (p->p_nice > NZERO && action == SIG_DFL && (prop & SA_KILL) &&
	    (p->p_flags & P_TRACED) == 0) {
		p->p_nice = NZERO;
	}

	/*
	 * If the process receives a STOP signal which indeed needs to
	 * stop the process, do so.  If the process chose to catch the
	 * signal, it will be treated like any other signal.
	 */
	if ((prop & SA_STOP) && action == SIG_DFL) {
		/*
		 * If a child holding parent blocked, stopping
		 * could cause deadlock.  Take no action at this
		 * time.
		 */
		if (p->p_flags & P_PPWAIT) {
			SIGADDSET(p->p_siglist, sig);
			goto out;
		}

		/*
		 * Do not actually try to manipulate the process, but simply
		 * stop it.  Lwps will stop as soon as they safely can.
		 *
		 * Ignore stop if the process is exiting.
		 */
		if ((p->p_flags & P_WEXIT) == 0) {
			p->p_xstat = sig;
			proc_stop(p);
		}
		goto out;
	}

	/*
	 * If it is a CONT signal with default action, just ignore it.
	 */
	if ((prop & SA_CONT) && action == SIG_DFL)
		goto out;

	/*
	 * Mark signal pending at this specific thread.
	 */
	spin_lock(&lp->lwp_spin);
	SIGADDSET(lp->lwp_siglist, sig);
	spin_unlock(&lp->lwp_spin);

	lwp_signotify(lp);

out:
	if (lp) {
		lwkt_reltoken(&lp->lwp_token);
		LWPRELE(lp);
	}
	lwkt_reltoken(&p->p_token);
	PRELE(p);
}

/*
 * Notify the LWP that a signal has arrived.  The LWP does not have to be
 * sleeping on the current cpu.
 *
 * p->p_token and lp->lwp_token must be held on call.
 *
 * We can only safely schedule the thread on its current cpu and only if
 * one of the SINTR flags is set.  If an SINTR flag is set AND we are on
 * the correct cpu we are properly interlocked, otherwise we could be
 * racing other thread transition states (or the lwp is on the user scheduler
 * runq but not scheduled) and must not do anything.
 *
 * Since we hold the lwp token we know the lwp cannot be ripped out from
 * under us so we can safely hold it to prevent it from being ripped out
 * from under us if we are forced to IPI another cpu to make the local
 * checks there.
 *
 * Adjustment of lp->lwp_stat can only occur when we hold the lwp_token,
 * which we won't in an IPI so any fixups have to be done here, effectively
 * replicating part of what setrunnable() does.
 */
static void
lwp_signotify(struct lwp *lp)
{
	ASSERT_LWKT_TOKEN_HELD(&lp->lwp_proc->p_token);

	crit_enter();
	if (lp == lwkt_preempted_proc()) {
		/*
		 * lwp is on the current cpu AND it is currently running
		 * (we preempted it).
		 */
		signotify();
	} else if (lp->lwp_flags & LWP_SINTR) {
		/*
		 * lwp is sitting in tsleep() with PCATCH set
		 */
		if (lp->lwp_thread->td_gd == mycpu) {
			setrunnable(lp);
		} else {
			/*
			 * We can only adjust lwp_stat while we hold the
			 * lwp_token, and we won't in the IPI function.
			 */
			LWPHOLD(lp);
			if (lp->lwp_stat == LSSTOP)
				lp->lwp_stat = LSSLEEP;
			lwkt_send_ipiq(lp->lwp_thread->td_gd,
				       lwp_signotify_remote, lp);
		}
	} else if (lp->lwp_thread->td_flags & TDF_SINTR) {
		/*
		 * lwp is sitting in lwkt_sleep() with PCATCH set.
		 */
		if (lp->lwp_thread->td_gd == mycpu) {
			setrunnable(lp);
		} else {
			/*
			 * We can only adjust lwp_stat while we hold the
			 * lwp_token, and we won't in the IPI function.
			 */
			LWPHOLD(lp);
			if (lp->lwp_stat == LSSTOP)
				lp->lwp_stat = LSSLEEP;
			lwkt_send_ipiq(lp->lwp_thread->td_gd,
				       lwp_signotify_remote, lp);
		}
	} else {
		/*
		 * Otherwise the lwp is either in some uninterruptable state
		 * or it is on the userland scheduler's runqueue waiting to
		 * be scheduled to a cpu.
		 */
	}
	crit_exit();
}

/*
 * This function is called via an IPI so we cannot call setrunnable() here
 * (because while we hold the lp we don't own its token, and can't get it
 * from an IPI).
 *
 * We are interlocked by virtue of being on the same cpu as the target.  If
 * we still are and LWP_SINTR or TDF_SINTR is set we can safely schedule
 * the target thread.
 */
static void
lwp_signotify_remote(void *arg)
{
	struct lwp *lp = arg;
	thread_t td = lp->lwp_thread;

	if (lp == lwkt_preempted_proc()) {
		signotify();
		LWPRELE(lp);
	} else if (td->td_gd == mycpu) {
		if ((lp->lwp_flags & LWP_SINTR) ||
		    (td->td_flags & TDF_SINTR)) {
			lwkt_schedule(td);
		}
		LWPRELE(lp);
	} else {
		lwkt_send_ipiq(td->td_gd, lwp_signotify_remote, lp);
		/* LWPHOLD() is forwarded to the target cpu */
	}
}

/*
 * Caller must hold p->p_token
 */
void
proc_stop(struct proc *p)
{
	struct proc *q;
	struct lwp *lp;

	ASSERT_LWKT_TOKEN_HELD(&p->p_token);

	/* If somebody raced us, be happy with it */
	if (p->p_stat == SSTOP || p->p_stat == SZOMB) {
		return;
	}
	p->p_stat = SSTOP;

	FOREACH_LWP_IN_PROC(lp, p) {
		LWPHOLD(lp);
		lwkt_gettoken(&lp->lwp_token);

		switch (lp->lwp_stat) {
		case LSSTOP:
			/*
			 * Do nothing, we are already counted in
			 * p_nstopped.
			 */
			break;

		case LSSLEEP:
			/*
			 * We're sleeping, but we will stop before
			 * returning to userspace, so count us
			 * as stopped as well.  We set LWP_MP_WSTOP
			 * to signal the lwp that it should not
			 * increase p_nstopped when reaching tstop().
			 *
			 * LWP_MP_WSTOP is protected by lp->lwp_token.
			 */
			if ((lp->lwp_mpflags & LWP_MP_WSTOP) == 0) {
				atomic_set_int(&lp->lwp_mpflags, LWP_MP_WSTOP);
				++p->p_nstopped;
			}
			break;

		case LSRUN:
			/*
			 * We might notify ourself, but that's not
			 * a problem.
			 */
			lwp_signotify(lp);
			break;
		}
		lwkt_reltoken(&lp->lwp_token);
		LWPRELE(lp);
	}

	if (p->p_nstopped == p->p_nthreads) {
		/*
		 * Token required to interlock kern_wait().  Reparenting can
		 * also cause a race so we have to hold (q).
		 */
		q = p->p_pptr;
		PHOLD(q);
		lwkt_gettoken(&q->p_token);
		p->p_flags &= ~P_WAITED;
		wakeup(q);
		if ((q->p_sigacts->ps_flag & PS_NOCLDSTOP) == 0)
			ksignal(p->p_pptr, SIGCHLD);
		lwkt_reltoken(&q->p_token);
		PRELE(q);
	}
}

/*
 * Caller must hold proc_token
 */
void
proc_unstop(struct proc *p)
{
	struct lwp *lp;

	ASSERT_LWKT_TOKEN_HELD(&p->p_token);

	if (p->p_stat != SSTOP)
		return;

	p->p_stat = SACTIVE;

	FOREACH_LWP_IN_PROC(lp, p) {
		LWPHOLD(lp);
		lwkt_gettoken(&lp->lwp_token);

		switch (lp->lwp_stat) {
		case LSRUN:
			/*
			 * Uh?  Not stopped?  Well, I guess that's okay.
			 */
			if (bootverbose)
				kprintf("proc_unstop: lwp %d/%d not sleeping\n",
					p->p_pid, lp->lwp_tid);
			break;

		case LSSLEEP:
			/*
			 * Still sleeping.  Don't bother waking it up.
			 * However, if this thread was counted as
			 * stopped, undo this.
			 *
			 * Nevertheless we call setrunnable() so that it
			 * will wake up in case a signal or timeout arrived
			 * in the meantime.
			 *
			 * LWP_MP_WSTOP is protected by lp->lwp_token.
			 */
			if (lp->lwp_mpflags & LWP_MP_WSTOP) {
				atomic_clear_int(&lp->lwp_mpflags,
						 LWP_MP_WSTOP);
				--p->p_nstopped;
			} else {
				if (bootverbose)
					kprintf("proc_unstop: lwp %d/%d sleeping, not stopped\n",
						p->p_pid, lp->lwp_tid);
			}
			/* FALLTHROUGH */

		case LSSTOP:
			/*
			 * This handles any lwp's waiting in a tsleep with
			 * SIGCATCH.
			 */
			lwp_signotify(lp);
			break;

		}
		lwkt_reltoken(&lp->lwp_token);
		LWPRELE(lp);
	}

	/*
	 * This handles any lwp's waiting in tstop().  We have interlocked
	 * the setting of p_stat by acquiring and releasing each lpw's
	 * token.
	 */
	wakeup(p);
}

/* 
 * No requirements.
 */
static int
kern_sigtimedwait(sigset_t waitset, siginfo_t *info, struct timespec *timeout)
{
	sigset_t savedmask, set;
	struct proc *p = curproc;
	struct lwp *lp = curthread->td_lwp;
	int error, sig, hz, timevalid = 0;
	struct timespec rts, ets, ts;
	struct timeval tv;

	error = 0;
	sig = 0;
	ets.tv_sec = 0;		/* silence compiler warning */
	ets.tv_nsec = 0;	/* silence compiler warning */
	SIG_CANTMASK(waitset);
	savedmask = lp->lwp_sigmask;

	if (timeout) {
		if (timeout->tv_sec >= 0 && timeout->tv_nsec >= 0 &&
		    timeout->tv_nsec < 1000000000) {
			timevalid = 1;
			getnanouptime(&rts);
		 	ets = rts;
			timespecadd(&ets, timeout);
		}
	}

	for (;;) {
		set = lwp_sigpend(lp);
		SIGSETAND(set, waitset);
		if ((sig = sig_ffs(&set)) != 0) {
			SIGFILLSET(lp->lwp_sigmask);
			SIGDELSET(lp->lwp_sigmask, sig);
			SIG_CANTMASK(lp->lwp_sigmask);
			sig = issignal(lp, 1);
			/*
			 * It may be a STOP signal, in the case, issignal
			 * returns 0, because we may stop there, and new
			 * signal can come in, we should restart if we got
			 * nothing.
			 */
			if (sig == 0)
				continue;
			else
				break;
		}

		/*
		 * Previous checking got nothing, and we retried but still
		 * got nothing, we should return the error status.
		 */
		if (error)
			break;

		/*
		 * POSIX says this must be checked after looking for pending
		 * signals.
		 */
		if (timeout) {
			if (timevalid == 0) {
				error = EINVAL;
				break;
			}
			getnanouptime(&rts);
			if (timespeccmp(&rts, &ets, >=)) {
				error = EAGAIN;
				break;
			}
			ts = ets;
			timespecsub(&ts, &rts);
			TIMESPEC_TO_TIMEVAL(&tv, &ts);
			hz = tvtohz_high(&tv);
		} else {
			hz = 0;
		}

		lp->lwp_sigmask = savedmask;
		SIGSETNAND(lp->lwp_sigmask, waitset);
		/*
		 * We won't ever be woken up.  Instead, our sleep will
		 * be broken in lwpsignal().
		 */
		error = tsleep(&p->p_sigacts, PCATCH, "sigwt", hz);
		if (timeout) {
			if (error == ERESTART) {
				/* can not restart a timeout wait. */
				error = EINTR;
			} else if (error == EAGAIN) {
				/* will calculate timeout by ourself. */
				error = 0;
			}
		}
		/* Retry ... */
	}

	lp->lwp_sigmask = savedmask;
	if (sig) {
		error = 0;
		bzero(info, sizeof(*info));
		info->si_signo = sig;
		spin_lock(&lp->lwp_spin);
		lwp_delsig(lp, sig);	/* take the signal! */
		spin_unlock(&lp->lwp_spin);

		if (sig == SIGKILL) {
			sigexit(lp, sig);
			/* NOT REACHED */
		}
	}

	return (error);
}

/*
 * MPALMOSTSAFE
 */
int
sys_sigtimedwait(struct sigtimedwait_args *uap)
{
	struct timespec ts;
	struct timespec *timeout;
	sigset_t set;
	siginfo_t info;
	int error;

	if (uap->timeout) {
		error = copyin(uap->timeout, &ts, sizeof(ts));
		if (error)
			return (error);
		timeout = &ts;
	} else {
		timeout = NULL;
	}
	error = copyin(uap->set, &set, sizeof(set));
	if (error)
		return (error);
	error = kern_sigtimedwait(set, &info, timeout);
	if (error)
		return (error);
	if (uap->info)
		error = copyout(&info, uap->info, sizeof(info));
	/* Repost if we got an error. */
	/*
	 * XXX lwp
	 *
	 * This could transform a thread-specific signal to another
	 * thread / process pending signal.
	 */
	if (error) {
		ksignal(curproc, info.si_signo);
	} else {
		uap->sysmsg_result = info.si_signo;
	}
	return (error);
}

/*
 * MPALMOSTSAFE
 */
int
sys_sigwaitinfo(struct sigwaitinfo_args *uap)
{
	siginfo_t info;
	sigset_t set;
	int error;

	error = copyin(uap->set, &set, sizeof(set));
	if (error)
		return (error);
	error = kern_sigtimedwait(set, &info, NULL);
	if (error)
		return (error);
	if (uap->info)
		error = copyout(&info, uap->info, sizeof(info));
	/* Repost if we got an error. */
	/*
	 * XXX lwp
	 *
	 * This could transform a thread-specific signal to another
	 * thread / process pending signal.
	 */
	if (error) {
		ksignal(curproc, info.si_signo);
	} else {
		uap->sysmsg_result = info.si_signo;
	}
	return (error);
}

/*
 * If the current process has received a signal that would interrupt a
 * system call, return EINTR or ERESTART as appropriate.
 */
int
iscaught(struct lwp *lp)
{
	struct proc *p = lp->lwp_proc;
	int sig;

	if (p) {
		if ((sig = CURSIG(lp)) != 0) {
			if (SIGISMEMBER(p->p_sigacts->ps_sigintr, sig))
				return (EINTR);                        
			return (ERESTART);     
		}                         
	}
	return(EWOULDBLOCK);
}

/*
 * If the current process has received a signal (should be caught or cause
 * termination, should interrupt current syscall), return the signal number.
 * Stop signals with default action are processed immediately, then cleared;
 * they aren't returned.  This is checked after each entry to the system for
 * a syscall or trap (though this can usually be done without calling issignal
 * by checking the pending signal masks in the CURSIG macro).
 *
 * This routine is called via CURSIG/__cursig.  We will acquire and release
 * p->p_token but if the caller needs to interlock the test the caller must
 * also hold p->p_token.
 *
 *	while (sig = CURSIG(curproc))
 *		postsig(sig);
 *
 * MPSAFE
 */
int
issignal(struct lwp *lp, int maytrace)
{
	struct proc *p = lp->lwp_proc;
	sigset_t mask;
	int sig, prop;

	lwkt_gettoken(&p->p_token);

	for (;;) {
		int traced = (p->p_flags & P_TRACED) || (p->p_stops & S_SIG);

		/*
		 * If this process is supposed to stop, stop this thread.
		 */
		if (p->p_stat == SSTOP)
			tstop();

		mask = lwp_sigpend(lp);
		SIGSETNAND(mask, lp->lwp_sigmask);
		if (p->p_flags & P_PPWAIT)
			SIG_STOPSIGMASK(mask);
		if (SIGISEMPTY(mask)) {		/* no signal to send */
			lwkt_reltoken(&p->p_token);
			return (0);
		}
		sig = sig_ffs(&mask);

		STOPEVENT(p, S_SIG, sig);

		/*
		 * We should see pending but ignored signals
		 * only if P_TRACED was on when they were posted.
		 */
		if (SIGISMEMBER(p->p_sigignore, sig) && (traced == 0)) {
			spin_lock(&lp->lwp_spin);
			lwp_delsig(lp, sig);
			spin_unlock(&lp->lwp_spin);
			continue;
		}
		if (maytrace &&
		    (p->p_flags & P_TRACED) &&
		    (p->p_flags & P_PPWAIT) == 0) {
			/*
			 * If traced, always stop, and stay stopped until
			 * released by the parent.
			 *
			 * NOTE: SSTOP may get cleared during the loop,
			 * but we do not re-notify the parent if we have 
			 * to loop several times waiting for the parent
			 * to let us continue.
			 *
			 * XXX not sure if this is still true
			 */
			p->p_xstat = sig;
			proc_stop(p);
			do {
				tstop();
			} while (!trace_req(p) && (p->p_flags & P_TRACED));

			/*
			 * If parent wants us to take the signal,
			 * then it will leave it in p->p_xstat;
			 * otherwise we just look for signals again.
			 */
			spin_lock(&lp->lwp_spin);
			lwp_delsig(lp, sig);	/* clear old signal */
			spin_unlock(&lp->lwp_spin);
			sig = p->p_xstat;
			if (sig == 0)
				continue;

			/*
			 * Put the new signal into p_siglist.  If the
			 * signal is being masked, look for other signals.
			 *
			 * XXX lwp might need a call to ksignal()
			 */
			SIGADDSET(p->p_siglist, sig);
			if (SIGISMEMBER(lp->lwp_sigmask, sig))
				continue;

			/*
			 * If the traced bit got turned off, go back up
			 * to the top to rescan signals.  This ensures
			 * that p_sig* and ps_sigact are consistent.
			 */
			if ((p->p_flags & P_TRACED) == 0)
				continue;
		}

		prop = sigprop(sig);

		/*
		 * Decide whether the signal should be returned.
		 * Return the signal's number, or fall through
		 * to clear it from the pending mask.
		 */
		switch ((intptr_t)p->p_sigacts->ps_sigact[_SIG_IDX(sig)]) {
		case (intptr_t)SIG_DFL:
			/*
			 * Don't take default actions on system processes.
			 */
			if (p->p_pid <= 1) {
#ifdef DIAGNOSTIC
				/*
				 * Are you sure you want to ignore SIGSEGV
				 * in init? XXX
				 */
				kprintf("Process (pid %lu) got signal %d\n",
					(u_long)p->p_pid, sig);
#endif
				break;		/* == ignore */
			}

			/*
			 * Handle the in-kernel checkpoint action
			 */
			if (prop & SA_CKPT) {
				checkpoint_signal_handler(lp);
				break;
			}

			/*
			 * If there is a pending stop signal to process
			 * with default action, stop here,
			 * then clear the signal.  However,
			 * if process is member of an orphaned
			 * process group, ignore tty stop signals.
			 */
			if (prop & SA_STOP) {
				if (p->p_flags & P_TRACED ||
		    		    (p->p_pgrp->pg_jobc == 0 &&
				    prop & SA_TTYSTOP))
					break;	/* == ignore */
				if ((p->p_flags & P_WEXIT) == 0) {
					p->p_xstat = sig;
					proc_stop(p);
					tstop();
				}
				break;
			} else if (prop & SA_IGNORE) {
				/*
				 * Except for SIGCONT, shouldn't get here.
				 * Default action is to ignore; drop it.
				 */
				break;		/* == ignore */
			} else {
				lwkt_reltoken(&p->p_token);
				return (sig);
			}

			/*NOTREACHED*/

		case (intptr_t)SIG_IGN:
			/*
			 * Masking above should prevent us ever trying
			 * to take action on an ignored signal other
			 * than SIGCONT, unless process is traced.
			 */
			if ((prop & SA_CONT) == 0 &&
			    (p->p_flags & P_TRACED) == 0)
				kprintf("issignal\n");
			break;		/* == ignore */

		default:
			/*
			 * This signal has an action, let
			 * postsig() process it.
			 */
			lwkt_reltoken(&p->p_token);
			return (sig);
		}
		spin_lock(&lp->lwp_spin);
		lwp_delsig(lp, sig);		/* take the signal! */
		spin_unlock(&lp->lwp_spin);
	}
	/* NOTREACHED */
}

/*
 * Take the action for the specified signal
 * from the current set of pending signals.
 *
 * Caller must hold p->p_token
 */
void
postsig(int sig)
{
	struct lwp *lp = curthread->td_lwp;
	struct proc *p = lp->lwp_proc;
	struct sigacts *ps = p->p_sigacts;
	sig_t action;
	sigset_t returnmask;
	int code;

	KASSERT(sig != 0, ("postsig"));

	KNOTE(&p->p_klist, NOTE_SIGNAL | sig);

	/*
	 * If we are a virtual kernel running an emulated user process
	 * context, switch back to the virtual kernel context before
	 * trying to post the signal.
	 */
	if (lp->lwp_vkernel && lp->lwp_vkernel->ve) {
		struct trapframe *tf = lp->lwp_md.md_regs;
		tf->tf_trapno = 0;
		vkernel_trap(lp, tf);
	}

	spin_lock(&lp->lwp_spin);
	lwp_delsig(lp, sig);
	spin_unlock(&lp->lwp_spin);
	action = ps->ps_sigact[_SIG_IDX(sig)];
#ifdef KTRACE
	if (KTRPOINT(lp->lwp_thread, KTR_PSIG))
		ktrpsig(lp, sig, action, lp->lwp_flags & LWP_OLDMASK ?
			&lp->lwp_oldsigmask : &lp->lwp_sigmask, 0);
#endif
	STOPEVENT(p, S_SIG, sig);

	if (action == SIG_DFL) {
		/*
		 * Default action, where the default is to kill
		 * the process.  (Other cases were ignored above.)
		 */
		sigexit(lp, sig);
		/* NOTREACHED */
	} else {
		/*
		 * If we get here, the signal must be caught.
		 */
		KASSERT(action != SIG_IGN && !SIGISMEMBER(lp->lwp_sigmask, sig),
		    ("postsig action"));

		/*
		 * Reset the signal handler if asked to
		 */
		if (SIGISMEMBER(ps->ps_sigreset, sig)) {
			/*
			 * See kern_sigaction() for origin of this code.
			 */
			SIGDELSET(p->p_sigcatch, sig);
			if (sig != SIGCONT &&
			    sigprop(sig) & SA_IGNORE)
				SIGADDSET(p->p_sigignore, sig);
			ps->ps_sigact[_SIG_IDX(sig)] = SIG_DFL;
		}

		/*
		 * Set the signal mask and calculate the mask to restore
		 * when the signal function returns.
		 *
		 * Special case: user has done a sigsuspend.  Here the
		 * current mask is not of interest, but rather the
		 * mask from before the sigsuspend is what we want
		 * restored after the signal processing is completed.
		 */
		if (lp->lwp_flags & LWP_OLDMASK) {
			returnmask = lp->lwp_oldsigmask;
			lp->lwp_flags &= ~LWP_OLDMASK;
		} else {
			returnmask = lp->lwp_sigmask;
		}

		SIGSETOR(lp->lwp_sigmask, ps->ps_catchmask[_SIG_IDX(sig)]);
		if (!SIGISMEMBER(ps->ps_signodefer, sig))
			SIGADDSET(lp->lwp_sigmask, sig);

		lp->lwp_ru.ru_nsignals++;
		if (lp->lwp_sig != sig) {
			code = 0;
		} else {
			code = lp->lwp_code;
			lp->lwp_code = 0;
			lp->lwp_sig = 0;
		}
		(*p->p_sysent->sv_sendsig)(action, sig, &returnmask, code);
	}
}

/*
 * Kill the current process for stated reason.
 */
void
killproc(struct proc *p, char *why)
{
	log(LOG_ERR, "pid %d (%s), uid %d, was killed: %s\n", 
		p->p_pid, p->p_comm,
		p->p_ucred ? p->p_ucred->cr_uid : -1, why);
	ksignal(p, SIGKILL);
}

/*
 * Force the current process to exit with the specified signal, dumping core
 * if appropriate.  We bypass the normal tests for masked and caught signals,
 * allowing unrecoverable failures to terminate the process without changing
 * signal state.  Mark the accounting record with the signal termination.
 * If dumping core, save the signal number for the debugger.  Calls exit and
 * does not return.
 *
 * This routine does not return.
 */
void
sigexit(struct lwp *lp, int sig)
{
	struct proc *p = lp->lwp_proc;

	lwkt_gettoken(&p->p_token);
	p->p_acflag |= AXSIG;
	if (sigprop(sig) & SA_CORE) {
		lp->lwp_sig = sig;
		/*
		 * Log signals which would cause core dumps
		 * (Log as LOG_INFO to appease those who don't want
		 * these messages.)
		 * XXX : Todo, as well as euid, write out ruid too
		 */
		if (coredump(lp, sig) == 0)
			sig |= WCOREFLAG;
		if (kern_logsigexit)
			log(LOG_INFO,
			    "pid %d (%s), uid %d: exited on signal %d%s\n",
			    p->p_pid, p->p_comm,
			    p->p_ucred ? p->p_ucred->cr_uid : -1,
			    sig &~ WCOREFLAG,
			    sig & WCOREFLAG ? " (core dumped)" : "");
	}
	lwkt_reltoken(&p->p_token);
	exit1(W_EXITCODE(0, sig));
	/* NOTREACHED */
}

static char corefilename[MAXPATHLEN+1] = {"%N.core"};
SYSCTL_STRING(_kern, OID_AUTO, corefile, CTLFLAG_RW, corefilename,
	      sizeof(corefilename), "process corefile name format string");

/*
 * expand_name(name, uid, pid)
 * Expand the name described in corefilename, using name, uid, and pid.
 * corefilename is a kprintf-like string, with three format specifiers:
 *	%N	name of process ("name")
 *	%P	process id (pid)
 *	%U	user id (uid)
 * For example, "%N.core" is the default; they can be disabled completely
 * by using "/dev/null", or all core files can be stored in "/cores/%U/%N-%P".
 * This is controlled by the sysctl variable kern.corefile (see above).
 */

static char *
expand_name(const char *name, uid_t uid, pid_t pid)
{
	char *temp;
	char buf[11];		/* Buffer for pid/uid -- max 4B */
	int i, n;
	char *format = corefilename;
	size_t namelen;

	temp = kmalloc(MAXPATHLEN + 1, M_TEMP, M_NOWAIT);
	if (temp == NULL)
		return NULL;
	namelen = strlen(name);
	for (i = 0, n = 0; n < MAXPATHLEN && format[i]; i++) {
		int l;
		switch (format[i]) {
		case '%':	/* Format character */
			i++;
			switch (format[i]) {
			case '%':
				temp[n++] = '%';
				break;
			case 'N':	/* process name */
				if ((n + namelen) > MAXPATHLEN) {
					log(LOG_ERR, "pid %d (%s), uid (%u):  Path `%s%s' is too long\n",
					    pid, name, uid, temp, name);
					kfree(temp, M_TEMP);
					return NULL;
				}
				memcpy(temp+n, name, namelen);
				n += namelen;
				break;
			case 'P':	/* process id */
				l = ksprintf(buf, "%u", pid);
				if ((n + l) > MAXPATHLEN) {
					log(LOG_ERR, "pid %d (%s), uid (%u):  Path `%s%s' is too long\n",
					    pid, name, uid, temp, name);
					kfree(temp, M_TEMP);
					return NULL;
				}
				memcpy(temp+n, buf, l);
				n += l;
				break;
			case 'U':	/* user id */
				l = ksprintf(buf, "%u", uid);
				if ((n + l) > MAXPATHLEN) {
					log(LOG_ERR, "pid %d (%s), uid (%u):  Path `%s%s' is too long\n",
					    pid, name, uid, temp, name);
					kfree(temp, M_TEMP);
					return NULL;
				}
				memcpy(temp+n, buf, l);
				n += l;
				break;
			default:
			  	log(LOG_ERR, "Unknown format character %c in `%s'\n", format[i], format);
			}
			break;
		default:
			temp[n++] = format[i];
		}
	}
	temp[n] = '\0';
	return temp;
}

/*
 * Dump a process' core.  The main routine does some
 * policy checking, and creates the name of the coredump;
 * then it passes on a vnode and a size limit to the process-specific
 * coredump routine if there is one; if there _is not_ one, it returns
 * ENOSYS; otherwise it returns the error from the process-specific routine.
 *
 * The parameter `lp' is the lwp which triggered the coredump.
 */

static int
coredump(struct lwp *lp, int sig)
{
	struct proc *p = lp->lwp_proc;
	struct vnode *vp;
	struct ucred *cred = p->p_ucred;
	struct flock lf;
	struct nlookupdata nd;
	struct vattr vattr;
	int error, error1;
	char *name;			/* name of corefile */
	off_t limit;
	
	STOPEVENT(p, S_CORE, 0);

	if (((sugid_coredump == 0) && p->p_flags & P_SUGID) || do_coredump == 0)
		return (EFAULT);
	
	/*
	 * Note that the bulk of limit checking is done after
	 * the corefile is created.  The exception is if the limit
	 * for corefiles is 0, in which case we don't bother
	 * creating the corefile at all.  This layout means that
	 * a corefile is truncated instead of not being created,
	 * if it is larger than the limit.
	 */
	limit = p->p_rlimit[RLIMIT_CORE].rlim_cur;
	if (limit == 0)
		return EFBIG;

	name = expand_name(p->p_comm, p->p_ucred->cr_uid, p->p_pid);
	if (name == NULL)
		return (EINVAL);
	error = nlookup_init(&nd, name, UIO_SYSSPACE, NLC_LOCKVP);
	if (error == 0)
		error = vn_open(&nd, NULL, O_CREAT | FWRITE | O_NOFOLLOW, S_IRUSR | S_IWUSR);
	kfree(name, M_TEMP);
	if (error) {
		nlookup_done(&nd);
		return (error);
	}
	vp = nd.nl_open_vp;
	nd.nl_open_vp = NULL;
	nlookup_done(&nd);

	vn_unlock(vp);
	lf.l_whence = SEEK_SET;
	lf.l_start = 0;
	lf.l_len = 0;
	lf.l_type = F_WRLCK;
	error = VOP_ADVLOCK(vp, (caddr_t)p, F_SETLK, &lf, 0);
	if (error)
		goto out2;

	/* Don't dump to non-regular files or files with links. */
	if (vp->v_type != VREG ||
	    VOP_GETATTR(vp, &vattr) || vattr.va_nlink != 1) {
		error = EFAULT;
		goto out1;
	}

	/* Don't dump to files current user does not own */
	if (vattr.va_uid != p->p_ucred->cr_uid) {
		error = EFAULT;
		goto out1;
	}

	VATTR_NULL(&vattr);
	vn_lock(vp, LK_EXCLUSIVE | LK_RETRY);
	vattr.va_size = 0;
	VOP_SETATTR(vp, &vattr, cred);
	p->p_acflag |= ACORE;
	vn_unlock(vp);

	error = p->p_sysent->sv_coredump ?
		  p->p_sysent->sv_coredump(lp, sig, vp, limit) : ENOSYS;

out1:
	lf.l_type = F_UNLCK;
	VOP_ADVLOCK(vp, (caddr_t)p, F_UNLCK, &lf, 0);
out2:
	error1 = vn_close(vp, FWRITE);
	if (error == 0)
		error = error1;
	return (error);
}

/*
 * Nonexistent system call-- signal process (may want to handle it).
 * Flag error in case process won't see signal immediately (blocked or ignored).
 *
 * MPALMOSTSAFE
 */
/* ARGSUSED */
int
sys_nosys(struct nosys_args *args)
{
	lwpsignal(curproc, curthread->td_lwp, SIGSYS);
	return (EINVAL);
}

/*
 * Send a SIGIO or SIGURG signal to a process or process group using
 * stored credentials rather than those of the current process.
 */
void
pgsigio(struct sigio *sigio, int sig, int checkctty)
{
	if (sigio == NULL)
		return;
		
	if (sigio->sio_pgid > 0) {
		if (CANSIGIO(sigio->sio_ruid, sigio->sio_ucred,
		             sigio->sio_proc))
			ksignal(sigio->sio_proc, sig);
	} else if (sigio->sio_pgid < 0) {
		struct proc *p;
		struct pgrp *pg = sigio->sio_pgrp;

		/*
		 * Must interlock all signals against fork
		 */
		pgref(pg);
		lockmgr(&pg->pg_lock, LK_EXCLUSIVE);
		LIST_FOREACH(p, &pg->pg_members, p_pglist) {
			if (CANSIGIO(sigio->sio_ruid, sigio->sio_ucred, p) &&
			    (checkctty == 0 || (p->p_flags & P_CONTROLT)))
				ksignal(p, sig);
		}
		lockmgr(&pg->pg_lock, LK_RELEASE);
		pgrel(pg);
	}
}

static int
filt_sigattach(struct knote *kn)
{
	struct proc *p = curproc;

	kn->kn_ptr.p_proc = p;
	kn->kn_flags |= EV_CLEAR;		/* automatically set */

	/* XXX lock the proc here while adding to the list? */
	knote_insert(&p->p_klist, kn);

	return (0);
}

static void
filt_sigdetach(struct knote *kn)
{
	struct proc *p = kn->kn_ptr.p_proc;

	knote_remove(&p->p_klist, kn);
}

/*
 * signal knotes are shared with proc knotes, so we apply a mask to 
 * the hint in order to differentiate them from process hints.  This
 * could be avoided by using a signal-specific knote list, but probably
 * isn't worth the trouble.
 */
static int
filt_signal(struct knote *kn, long hint)
{
	if (hint & NOTE_SIGNAL) {
		hint &= ~NOTE_SIGNAL;

		if (kn->kn_id == hint)
			kn->kn_data++;
	}
	return (kn->kn_data != 0);
}
