/*-
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
 *	@(#)signal.h	8.3 (Berkeley) 3/30/94
 *
 * $FreeBSD: src/include/signal.h,v 1.14 1999/10/02 19:33:23 marcel Exp $
 */

#ifndef _SIGNAL_H_
#define	_SIGNAL_H_

#include <sys/cdefs.h>
#include <sys/_posix.h>
#include <machine/stdint.h>
#include <sys/signal.h>
#include <sys/time.h>

#if __BSD_VISIBLE
extern const char * const sys_signame[NSIG];
extern const char * const sys_siglist[NSIG];
extern const int sys_nsig;
#endif

__BEGIN_DECLS
int	raise(int);

#if __POSIX_VISIBLE
int	kill(__pid_t, int);
int	pthread_kill(pthread_t, int);
int	pthread_sigmask(int, const sigset_t * __restrict,
	    sigset_t * __restrict);
int	sigaction(int, const struct sigaction * __restrict,
	    struct sigaction * __restrict);
int	sigaddset(sigset_t *, int);
int	sigdelset(sigset_t *, int);
int	sigemptyset(sigset_t *);
int	sigfillset(sigset_t *);
int	sigismember(const sigset_t *, int);
int	sigpending(sigset_t *);
int	sigprocmask(int, const sigset_t * __restrict, sigset_t * __restrict);
int	sigsuspend(const sigset_t *);
int	sigwait(const sigset_t * __restrict, int * __restrict);
#endif /* __POSIX_VISIBLE */

#if __XSI_VISIBLE
int	killpg(__pid_t, int);
int	sigaltstack(const stack_t * __restrict, stack_t * __restrict);
int	siginterrupt(int, int);
int	sigpause(int);
#if __BSD_VISIBLE || __XSI_VISIBLE <= 500
int	sigstack(const struct sigstack *, struct sigstack *);
#endif
#endif /* __XSI_VISIBLE */

#if __POSIX_VISIBLE >= 199506
int	sigqueue(__pid_t, int, const union sigval);
int	sigtimedwait(const sigset_t * __restrict, siginfo_t * __restrict,
	    const struct timespec * __restrict);
int	sigwaitinfo(const sigset_t * __restrict, siginfo_t * __restrict);
#endif

#if __POSIX_VISIBLE >= 200809
void	psiginfo(const siginfo_t *, const char *);
void	psignal(unsigned int, const char *); /* XXX signum should be int */
#endif

#if __BSD_VISIBLE
int	lwp_kill(__pid_t, lwpid_t, int);
int	sigblock(int);
int	sigreturn(ucontext_t *);
int	sigsetmask(int);
int	sigvec(int, struct sigvec *, struct sigvec *);
#endif /* __BSD_VISIBLE */
__END_DECLS

#endif /* !_SIGNAL_H_ */
