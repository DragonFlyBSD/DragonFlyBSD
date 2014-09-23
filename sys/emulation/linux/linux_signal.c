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
 * $FreeBSD: src/sys/compat/linux/linux_signal.c,v 1.23.2.3 2001/11/05 19:08:23 marcel Exp $
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/lock.h>
#include <sys/proc.h>
#include <sys/signalvar.h>
#include <sys/sysproto.h>
#include <sys/kern_syscall.h>
#include <sys/thread.h>

#include <sys/thread2.h>

#include <arch_linux/linux.h>
#include <arch_linux/linux_proto.h>
#include "linux_emuldata.h"
#include "linux_signal.h"
#include "linux_util.h"

void
linux_to_bsd_sigset(l_sigset_t *lss, sigset_t *bss)
{
	int b, l;

	SIGEMPTYSET(*bss);
	bss->__bits[0] = lss->__bits[0] & ~((1U << LINUX_SIGTBLSZ) - 1);
	bss->__bits[1] = lss->__bits[1];
	for (l = 1; l <= LINUX_SIGTBLSZ; l++) {
		if (LINUX_SIGISMEMBER(*lss, l)) {
			b = linux_to_bsd_signal[_SIG_IDX(l)];
			if (b)
				SIGADDSET(*bss, b);
		}
	}
}

void
bsd_to_linux_sigset(sigset_t *bss, l_sigset_t *lss)
{
	int b, l;

	LINUX_SIGEMPTYSET(*lss);
	lss->__bits[0] = bss->__bits[0] & ~((1U << LINUX_SIGTBLSZ) - 1);
	lss->__bits[1] = bss->__bits[1];
	for (b = 1; b <= LINUX_SIGTBLSZ; b++) {
		if (SIGISMEMBER(*bss, b)) {
			l = bsd_to_linux_signal[_SIG_IDX(b)];
			if (l)
				LINUX_SIGADDSET(*lss, l);
		}
	}
}

void
linux_to_bsd_sigaction(l_sigaction_t *lsa, struct sigaction *bsa)
{

	linux_to_bsd_sigset(&lsa->lsa_mask, &bsa->sa_mask);
	bsa->sa_handler = lsa->lsa_handler;
	bsa->sa_flags = 0;
	if (lsa->lsa_flags & LINUX_SA_NOCLDSTOP)
		bsa->sa_flags |= SA_NOCLDSTOP;
	if (lsa->lsa_flags & LINUX_SA_NOCLDWAIT)
		bsa->sa_flags |= SA_NOCLDWAIT;
	if (lsa->lsa_flags & LINUX_SA_SIGINFO)
		bsa->sa_flags |= SA_SIGINFO;
	if (lsa->lsa_flags & LINUX_SA_ONSTACK)
		bsa->sa_flags |= SA_ONSTACK;
	if (lsa->lsa_flags & LINUX_SA_RESTART)
		bsa->sa_flags |= SA_RESTART;
	if (lsa->lsa_flags & LINUX_SA_ONESHOT)
		bsa->sa_flags |= SA_RESETHAND;
	if (lsa->lsa_flags & LINUX_SA_NOMASK)
		bsa->sa_flags |= SA_NODEFER;
}

void
bsd_to_linux_sigaction(struct sigaction *bsa, l_sigaction_t *lsa)
{

	bsd_to_linux_sigset(&bsa->sa_mask, &lsa->lsa_mask);
	lsa->lsa_handler = bsa->sa_handler;
	lsa->lsa_restorer = NULL;	/* unsupported */
	lsa->lsa_flags = 0;
	if (bsa->sa_flags & SA_NOCLDSTOP)
		lsa->lsa_flags |= LINUX_SA_NOCLDSTOP;
	if (bsa->sa_flags & SA_NOCLDWAIT)
		lsa->lsa_flags |= LINUX_SA_NOCLDWAIT;
	if (bsa->sa_flags & SA_SIGINFO)
		lsa->lsa_flags |= LINUX_SA_SIGINFO;
	if (bsa->sa_flags & SA_ONSTACK)
		lsa->lsa_flags |= LINUX_SA_ONSTACK;
	if (bsa->sa_flags & SA_RESTART)
		lsa->lsa_flags |= LINUX_SA_RESTART;
	if (bsa->sa_flags & SA_RESETHAND)
		lsa->lsa_flags |= LINUX_SA_ONESHOT;
	if (bsa->sa_flags & SA_NODEFER)
		lsa->lsa_flags |= LINUX_SA_NOMASK;
}

/*
 * MPALMOSTSAFE
 */
int
sys_linux_signal(struct linux_signal_args *args)
{
	l_sigaction_t linux_nsa, linux_osa;
	struct sigaction nsa, osa;
	int error, sig;

#ifdef DEBUG
	if (ldebug(signal))
		kprintf(ARGS(signal, "%d, %p"),
		    args->sig, (void *)args->handler);
#endif
	linux_nsa.lsa_handler = args->handler;
	linux_nsa.lsa_flags = LINUX_SA_ONESHOT | LINUX_SA_NOMASK;
	LINUX_SIGEMPTYSET(linux_nsa.lsa_mask);
	linux_to_bsd_sigaction(&linux_nsa, &nsa);
	if (args->sig <= LINUX_SIGTBLSZ) {
		sig = linux_to_bsd_signal[_SIG_IDX(args->sig)];
	} else {
		sig = args->sig;
	}

	error = kern_sigaction(sig, &nsa, &osa);

	bsd_to_linux_sigaction(&osa, &linux_osa);
	args->sysmsg_result = (int) linux_osa.lsa_handler;
	return (error);
}

/*
 * MPALMOSTSAFE
 */
int
sys_linux_rt_sigaction(struct linux_rt_sigaction_args *args)
{
	l_sigaction_t linux_nsa, linux_osa;
	struct sigaction nsa, osa;
	int error, sig;

#ifdef DEBUG
	if (ldebug(rt_sigaction))
		kprintf(ARGS(rt_sigaction, "%ld, %p, %p, %ld"),
		    (long)args->sig, (void *)args->act,
		    (void *)args->oact, (long)args->sigsetsize);
#endif
	if (args->sigsetsize != sizeof(l_sigset_t))
		return (EINVAL);

	if (args->act) {
		error = copyin(args->act, &linux_nsa, sizeof(linux_nsa));
		if (error)
			return (error);
		linux_to_bsd_sigaction(&linux_nsa, &nsa);
	}
	if (args->sig <= LINUX_SIGTBLSZ) {
		sig = linux_to_bsd_signal[_SIG_IDX(args->sig)];
	} else {
		sig = args->sig;
	}

	error = kern_sigaction(sig, args->act ? &nsa : NULL,
			       args->oact ? &osa : NULL);

	if (error == 0 && args->oact) {
		bsd_to_linux_sigaction(&osa, &linux_osa);
		error = copyout(&linux_osa, args->oact, sizeof(linux_osa));
	}

	return (error);
}

static int
linux_to_bsd_sigprocmask(int how)
{
	switch (how) {
	case LINUX_SIG_BLOCK:
		return SIG_BLOCK;
	case LINUX_SIG_UNBLOCK:
		return SIG_UNBLOCK;
	case LINUX_SIG_SETMASK:
		return SIG_SETMASK;
	default:
		return (-1);
	}
}

/*
 * MPALMOSTSAFE
 */
int
sys_linux_sigprocmask(struct linux_sigprocmask_args *args)
{
	l_osigset_t mask;
	l_sigset_t linux_set, linux_oset;
	sigset_t set, oset;
	int error, how;

#ifdef DEBUG
	if (ldebug(sigprocmask))
		kprintf(ARGS(sigprocmask, "%d, *, *"), args->how);
#endif

	if (args->mask) {
		error = copyin(args->mask, &mask, sizeof(l_osigset_t));
		if (error)
			return (error);
		LINUX_SIGEMPTYSET(linux_set);
		linux_set.__bits[0] = mask;
		linux_to_bsd_sigset(&linux_set, &set);
	}
	how = linux_to_bsd_sigprocmask(args->how);

	error = kern_sigprocmask(how, args->mask ? &set : NULL,
				 args->omask ? &oset : NULL);

	if (error == 0 && args->omask) {
		bsd_to_linux_sigset(&oset, &linux_oset);
		mask = linux_oset.__bits[0];
		error = copyout(&mask, args->omask, sizeof(l_osigset_t));
	}
	return (error);
}

/*
 * MPALMOSTSAFE
 */
int
sys_linux_rt_sigprocmask(struct linux_rt_sigprocmask_args *args)
{
	l_sigset_t linux_set, linux_oset;
	sigset_t set, oset;
	int error, how;

#ifdef DEBUG
	if (ldebug(rt_sigprocmask))
		kprintf(ARGS(rt_sigprocmask, "%d, %p, %p, %ld"),
		    args->how, (void *)args->mask,
		    (void *)args->omask, (long)args->sigsetsize);
#endif

	if (args->sigsetsize != sizeof(l_sigset_t))
		return EINVAL;

	if (args->mask) {
		error = copyin(args->mask, &linux_set, sizeof(l_sigset_t));
		if (error)
			return (error);
		linux_to_bsd_sigset(&linux_set, &set);
	}
	how = linux_to_bsd_sigprocmask(args->how);

	error = kern_sigprocmask(how, args->mask ? &set : NULL,
				 args->omask ? &oset : NULL);

	if (error == 0 && args->omask) {
		bsd_to_linux_sigset(&oset, &linux_oset);
		error = copyout(&linux_oset, args->omask, sizeof(l_sigset_t));
	}

	return (error);
}

/*
 * MPSAFE
 */
int
sys_linux_sgetmask(struct linux_sgetmask_args *args)
{
	struct lwp *lp = curthread->td_lwp;
	l_sigset_t mask;

#ifdef DEBUG
	if (ldebug(sgetmask))
		kprintf(ARGS(sgetmask, ""));
#endif

	bsd_to_linux_sigset(&lp->lwp_sigmask, &mask);
	args->sysmsg_result = mask.__bits[0];
	return (0);
}

/*
 * MPSAFE
 */
int
sys_linux_ssetmask(struct linux_ssetmask_args *args)
{
	struct lwp *lp = curthread->td_lwp;
	l_sigset_t lset;
	sigset_t bset;

#ifdef DEBUG
	if (ldebug(ssetmask))
		kprintf(ARGS(ssetmask, "%08lx"), (unsigned long)args->mask);
#endif

	bsd_to_linux_sigset(&lp->lwp_sigmask, &lset);
	args->sysmsg_result = lset.__bits[0];
	LINUX_SIGEMPTYSET(lset);
	lset.__bits[0] = args->mask;
	linux_to_bsd_sigset(&lset, &bset);
	crit_enter();
	lp->lwp_sigmask = bset;
	SIG_CANTMASK(lp->lwp_sigmask);
	crit_exit();
	return (0);
}

/*
 * MPSAFE
 */
int
sys_linux_sigpending(struct linux_sigpending_args *args)
{
	struct thread *td = curthread;
	struct lwp *lp = td->td_lwp;
	sigset_t set;
	l_sigset_t linux_set;
	l_osigset_t mask;
	int error;

#ifdef DEBUG
	if (ldebug(sigpending))
		kprintf(ARGS(sigpending, "*"));
#endif

	error = kern_sigpending(&set);

	if (error == 0) {
		SIGSETAND(set, lp->lwp_sigmask);
		bsd_to_linux_sigset(&set, &linux_set);
		mask = linux_set.__bits[0];
		error = copyout(&mask, args->mask, sizeof(mask));
	}
	return (error);
}

int
sys_linux_kill(struct linux_kill_args *args)
{
	int error, sig;

#ifdef DEBUG
	if (ldebug(kill))
		kprintf(ARGS(kill, "%d, %d"), args->pid, args->signum);
#endif
	/*
	 * Allow signal 0 as a means to check for privileges
	 */
	if (args->signum < 0 || args->signum > LINUX_NSIG)
		return EINVAL;

	if (args->signum > 0 && args->signum <= LINUX_SIGTBLSZ)
		sig = linux_to_bsd_signal[_SIG_IDX(args->signum)];
	else
		sig = args->signum;

	error = kern_kill(sig, args->pid, -1);

	return(error);
}


static int
linux_do_tkill(l_int tgid, l_int pid, l_int sig)
{
	struct linux_emuldata *em;
	struct proc *p;
	int error = 0;

	/*
	 * Allow signal 0 as a means to check for privileges
	 */
	if (sig < 0 || sig > LINUX_NSIG)
		return (EINVAL);

	if (sig > 0 && sig <= LINUX_SIGTBLSZ)
		sig = linux_to_bsd_signal[_SIG_IDX(sig)];

	if ((p = pfind(pid)) == NULL) {
		if ((p = zpfind(pid)) == NULL) {
			error = ESRCH;
			goto done2;
		}
	}

	EMUL_LOCK();
	em = emuldata_get(p);

	if (em == NULL) {
		EMUL_UNLOCK();
		error = ESRCH;
		goto done1;
	}

	if (tgid > 0 && em->s->group_pid != tgid) {
		EMUL_UNLOCK();
		error = ESRCH;
		goto done1;
	}
	EMUL_UNLOCK();

	error = kern_kill(sig, pid, -1);

done1:
	PRELE(p);
done2:
	return (error);
}

int
sys_linux_tgkill(struct linux_tgkill_args *args)
{

#ifdef DEBUG
	if (ldebug(tgkill))
		kprintf(ARGS(tgkill, "%d, %d, %d"), args->tgid, args->pid, args->sig);
#endif
	if (args->pid <= 0 || args->tgid <= 0)
		return (EINVAL);

	return (linux_do_tkill(args->tgid, args->pid, args->sig));
}

int
sys_linux_tkill(struct linux_tkill_args *args)
{
#ifdef DEBUG
	if (ldebug(tkill))
		kprintf(ARGS(tkill, "%i, %i"), args->tid, args->sig);
#endif
	if (args->tid <= 0)
		return (EINVAL);

	return (linux_do_tkill(0, args->tid, args->sig));
}
