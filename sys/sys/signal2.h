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
 *	@(#)signalvar.h	8.6 (Berkeley) 2/19/95
 * $FreeBSD: src/sys/sys/signalvar.h,v 1.34.2.1 2000/05/16 06:58:05 dillon Exp $
 * $DragonFly: src/sys/sys/signal2.h,v 1.3 2008/10/14 21:25:14 swildner Exp $
 */

#ifndef _SYS_SIGNAL2_H
#define _SYS_SIGNAL2_H

#include <sys/proc.h>

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
lwp_delsig(struct lwp *lp, int sig)
{
	SIGDELSET(lp->lwp_siglist, sig);
	SIGDELSET(lp->lwp_proc->p_siglist, sig);
}

#define	CURSIG(lp)		__cursig(lp, 1, 0)
#define	CURSIG_TRACE(lp)	__cursig(lp, 1, 1)
#define CURSIG_NOBLOCK(lp)	__cursig(lp, 0, 0)

/*
 * Determine signal that should be delivered to process p, the current
 * process, 0 if none.  If there is a pending stop signal with default
 * action, the process stops in issignal().
 *
 * This function does not interlock pending signals.  If the caller needs
 * to interlock the caller must acquire the per-proc token.
 *
 * MPSAFE
 */
static __inline
int
__cursig(struct lwp *lp, int mayblock, int maytrace)
{
	struct proc *p = lp->lwp_proc;
	sigset_t tmpset;
	int r;

	tmpset = lwp_sigpend(lp);
	SIGSETNAND(tmpset, lp->lwp_sigmask);

	/* Nothing interesting happening? */
	if (SIGISEMPTY(tmpset)) {
		/*
		 * Quit here, unless
		 *  a) we may block and
		 *  b) somebody is tracing us.
		 */
		if (!(mayblock && (p->p_flag & P_TRACED)))
			return (0);
	}

	if (mayblock)
		r = issignal(lp, maytrace);
	else
		r = TRUE;	/* simply state the fact */

	return(r);
}

#endif
