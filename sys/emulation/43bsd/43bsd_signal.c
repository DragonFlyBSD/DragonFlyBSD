/*
 * 43BSD_SIGNAL.C	- 4.3BSD compatibility signal syscalls
 *
 * Copyright (c) 1989, 1993
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
 * $DragonFly: src/sys/emulation/43bsd/43bsd_signal.c,v 1.5 2007/03/12 21:07:42 corecode Exp $
 * 	from: DragonFly kern/kern_sig.c,v 1.22
 *
 * These syscalls used to live in kern/kern_sig.c.  They are modified
 * to use the new split syscalls.
 */

#include "opt_compat.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/sysproto.h>
#include <sys/conf.h>
#include <sys/kernel.h>
#include <sys/kern_syscall.h>
#include <sys/proc.h>
#include <sys/signal.h>
#include <sys/signalvar.h>

#include <sys/thread2.h>

#define	ONSIG	32		/* NSIG for osig* syscalls.  XXX. */

#define SIG2OSIG(sig, osig)	osig = (sig).__bits[0]
#define OSIG2SIG(osig, sig)	SIGEMPTYSET(sig); (sig).__bits[0] = osig

#define SIGSETLO(set1, set2)	((set1).__bits[0] = (set2).__bits[0])

/*
 * These syscalls are unncessary because it is next to impossible to
 * find 4.3BSD binaries built for i386.  The current libc has routines
 * which fake the 4.3BSD family of signal syscalls, so anything built
 * from source won't be using these.
 *
 * This file is provided for educational purposes only.  The osigvec()
 * syscall is probably broken because the current signal code uses
 * a different signal trampoline.
 *
 * MPALMOSTSAFE
 */
int
sys_osigvec(struct osigvec_args *uap)
{
	struct sigvec vec;
	struct sigaction nsa, osa;
	struct sigaction *nsap, *osap;
	int error;

	if (uap->signum <= 0 || uap->signum >= ONSIG)
		return (EINVAL);
	nsap = (uap->nsv != NULL) ? &nsa : NULL;
	osap = (uap->osv != NULL) ? &osa : NULL;
	if (nsap) {
		error = copyin(uap->nsv, &vec, sizeof(vec));
		if (error)
			return (error);
		nsap->sa_handler = vec.sv_handler;
		OSIG2SIG(vec.sv_mask, nsap->sa_mask);
		nsap->sa_flags = vec.sv_flags;
		nsap->sa_flags ^= SA_RESTART;	/* opposite of SV_INTERRUPT */
	}

	error = kern_sigaction(uap->signum, nsap, osap);

	if (osap && !error) {
		vec.sv_handler = osap->sa_handler;
		SIG2OSIG(osap->sa_mask, vec.sv_mask);
		vec.sv_flags = osap->sa_flags;
		vec.sv_flags &= ~SA_NOCLDWAIT;
		vec.sv_flags ^= SA_RESTART;
		error = copyout(&vec, uap->osv, sizeof(vec));
	}
	return (error);
}

/*
 * MPSAFE
 */
int
sys_osigblock(struct osigblock_args *uap)
{
	struct lwp *lp = curthread->td_lwp;
	sigset_t set;

	OSIG2SIG(uap->mask, set);
	SIG_CANTMASK(set);
	crit_enter();
	SIG2OSIG(lp->lwp_sigmask, uap->sysmsg_iresult);
	SIGSETOR(lp->lwp_sigmask, set);
	crit_exit();
	return (0);
}

/*
 * MPSAFE
 */
int
sys_osigsetmask(struct osigsetmask_args *uap)
{
	struct lwp *lp = curthread->td_lwp;
	sigset_t set;

	OSIG2SIG(uap->mask, set);
	SIG_CANTMASK(set);
	crit_enter();
	SIG2OSIG(lp->lwp_sigmask, uap->sysmsg_iresult);
	SIGSETLO(lp->lwp_sigmask, set);
	crit_exit();
	return (0);
}

/*
 * MPSAFE
 */
int
sys_osigstack(struct osigstack_args *uap)
{
	struct lwp *lp = curthread->td_lwp;
	struct sigstack ss;
	int error = 0;

	ss.ss_sp = lp->lwp_sigstk.ss_sp;
	ss.ss_onstack = lp->lwp_sigstk.ss_flags & SS_ONSTACK;
	if (uap->oss && (error = copyout(&ss, uap->oss,
					 sizeof(struct sigstack)))) {
		return (error);
	}
	if (uap->nss && (error = copyin(uap->nss, &ss, sizeof(ss))) == 0) {
		lp->lwp_sigstk.ss_sp = ss.ss_sp;
		lp->lwp_sigstk.ss_size = 0;
		lp->lwp_sigstk.ss_flags |= ss.ss_onstack & SS_ONSTACK;
		lp->lwp_flags |= LWP_ALTSTACK;
	}
	return (error);
}

int
sys_okillpg(struct okillpg_args *uap)
{
	int error;

	error = kern_kill(uap->signum, -uap->pgid, -1);

	return (error);
}
