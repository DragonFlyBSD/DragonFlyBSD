/*
 * Copyright (c) 1991, 1993
 *	The Regents of the University of California.  All rights reserved.
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
 *	@(#)signalvar.h	8.6 (Berkeley) 2/19/95
 * $FreeBSD: src/sys/sys/signalvar.h,v 1.34.2.1 2000/05/16 06:58:05 dillon Exp $
 */

#ifndef _SYS_SIGNAL2_H_
#define _SYS_SIGNAL2_H_

#include <sys/param.h>
#include <sys/proc.h>
#include <sys/signalvar.h>
#include <sys/systm.h>

/*
 * Can curproc send the signal sig to process q?  Only processes within
 * the current reaper or children of the current reaper can be signaled.
 * Normally the reaper itself cannot be signalled, unless initok is set.
 */
#define	CANSIGNAL(q, sig, initok)				\
	((!p_trespass(curproc->p_ucred, (q)->p_ucred) &&	\
	reaper_sigtest(curproc, q, initok)) ||			\
	((sig) == SIGCONT && (q)->p_session == curproc->p_session))

/*
 * Inline functions:
 */
/*
 * Determine which signals are pending for a lwp.
 *
 * (Does not need to be interlocked with lwp_spin.  If caller holds a
 *  critical section races will be resolved through an AST).
 */
static __inline sigset_t
lwp_sigpend(struct lwp *lp)
{
	sigset_t set;

	set = lp->lwp_proc->p_siglist;
	SIGSETOR(set, lp->lwp_siglist);

	return (set);
}

/*
 * Mark a signal as handled by the lwp.
 *
 * (p->p_token must be held, lp->lwp_spin must be held)
 */
static __inline void
lwp_delsig(struct lwp *lp, int sig, int fromproc)
{
	SIGDELSET(lp->lwp_siglist, sig);
	if (fromproc)
		SIGDELSET_ATOMIC(lp->lwp_proc->p_siglist, sig);
}

#define	CURSIG(lp)			__cursig(lp, 1, 0, NULL)
#define	CURSIG_TRACE(lp)		__cursig(lp, 1, 1, NULL)
#define	CURSIG_LCK_TRACE(lp, ptok)	__cursig(lp, 1, 1, ptok)
#define CURSIG_NOBLOCK(lp)		__cursig(lp, 0, 0, NULL)

/*
 * This inline checks lpmap->blockallsigs, a user r/w accessible
 * memory-mapped variable that allows a user thread to instantly
 * mask and unmask all maskable signals without having to issue a
 * system call.
 *
 * On the unmask count reaching 0, userland can check and clear
 * bit 31 to determine if any signals arrived, then issue a dummy
 * system call to ensure delivery.
 */
static __inline
void
__sig_condblockallsigs(sigset_t *mask, struct lwp *lp)
{
	struct sys_lpmap *lpmap;
	uint32_t bas;
	sigset_t tmp;
	int trapsig;

	if ((lpmap = lp->lwp_lpmap) == NULL)
		return;

	bas = lpmap->blockallsigs;
	while (bas & 0x7FFFFFFFU) {
		tmp = *mask;			/* check maskable signals */
		SIG_CANTMASK(tmp);
		if (SIGISEMPTY(tmp))		/* no unmaskable signals */
			return;

		/*
		 * Upon successful update to lpmap->blockallsigs remove
		 * all maskable signals, leaving only unmaskable signals.
		 *
		 * If lwp_sig is non-zero it represents a syncronous 'trap'
		 * signal which, being a synchronous trap, must be allowed.
		 */
		if (atomic_fcmpset_int(&lpmap->blockallsigs, &bas,
				       bas | 0x80000000U)) {
			trapsig = lp->lwp_sig;
			if (trapsig && SIGISMEMBER(*mask, trapsig)) {
				SIGSETAND(*mask, sigcantmask_mask);
				SIGADDSET(*mask, trapsig);
			} else {
				SIGSETAND(*mask, sigcantmask_mask);
			}
			break;
		}
	}
}

/*
 * Determine signal that should be delivered to process p, the current
 * process, 0 if none.  If there is a pending stop signal with default
 * action, the process stops in issignal().
 *
 * This function does not interlock pending signals.  If the caller needs
 * to interlock the caller must acquire the per-proc token.
 *
 * If ptok is non-NULL this function may return with proc->p_token held,
 * indicating that the signal came from the process structure.  This is
 * used by postsig to avoid holding p_token when possible.  Only applicable
 * if mayblock is non-zero.
 */
static __inline
int
__cursig(struct lwp *lp, int mayblock, int maytrace, int *ptok)
{
	struct proc *p = lp->lwp_proc;
	sigset_t tmpset;
	int r;

	tmpset = lwp_sigpend(lp);
	SIGSETNAND(tmpset, lp->lwp_sigmask);
	SIG_CONDBLOCKALLSIGS(tmpset, lp);

	/* Nothing interesting happening? */
	if (SIGISEMPTY(tmpset)) {
		/*
		 * Quit here, unless
		 *  a) we may block and
		 *  b) somebody is tracing us.
		 */
		if (mayblock == 0 || (p->p_flags & P_TRACED) == 0)
			return (0);
	}

	if (mayblock)
		r = issignal(lp, maytrace, ptok);
	else
		r = TRUE;	/* simply state the fact */

	return(r);
}

/*
 * Generic (non-directed) signal processing on process is in progress
 */
static __inline
void
sigirefs_hold(struct proc *p)
{
	atomic_add_int(&p->p_sigirefs, 1);
}

/*
 * Signal processing complete
 */
static __inline
void
sigirefs_drop(struct proc *p)
{
	if (atomic_fetchadd_int(&p->p_sigirefs, -1) == 0x80000001U) {
		atomic_clear_int(&p->p_sigirefs, 0x80000000U);
		wakeup(&p->p_sigirefs);
	}
}

/*
 * Wait for generic (non directed) signal processing on process to
 * complete to interlock against races.  Called after lwp_sigmask
 * has been changed.
 */
static __inline
void
sigirefs_wait(struct proc *p)
{
	uint32_t refs;

	cpu_mfence();
	refs = *(volatile uint32_t *)&p->p_sigirefs;
	if (refs & 0x7FFFFFFF) {
		while (refs & 0x7FFFFFFF) {
			tsleep_interlock(&p->p_sigirefs, 0);
			if (atomic_fcmpset_int(&p->p_sigirefs, &refs,
						refs | 0x80000000U))
			{
				tsleep(&p->p_sigirefs, PINTERLOCKED,
				       "sirefs", 0);
			}
		}
	}
}

#endif /* !_SYS_SIGNAL2_H_ */
