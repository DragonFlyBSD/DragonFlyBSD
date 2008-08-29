/*
 * Copyright (c) 1986, 1989, 1991, 1993
 *	The Regents of the University of California.
 * Copyright (c) 2008 The DragonFly Project.
 * All rights reserved.
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
 *	@(#)signal.h	8.1 (Berkeley) 6/11/93
 * $FreeBSD: src/sys/i386/include/signal.h,v 1.12 1999/11/12 13:52:11 marcel Exp $
 * $DragonFly: src/sys/cpu/amd64/include/signal.h,v 1.3 2008/08/29 17:07:06 dillon Exp $
 */

#ifndef _CPU_SIGNAL_H_
#define	_CPU_SIGNAL_H_

/** si_code **/
/* codes for SIGILL */
#define ILL_ILLOPC 	1	/* Illegal opcode.			*/
#define ILL_ILLOPN 	2	/* Illegal operand.			*/
#define ILL_ILLADR 	3	/* Illegal addressing mode.		*/
#define ILL_ILLTRP 	4	/* Illegal trap.			*/
#define ILL_PRVOPC 	5	/* Privileged opcode.			*/
#define ILL_PRVREG 	6	/* Privileged register.			*/
#define ILL_COPROC 	7	/* Coprocessor error.			*/
#define ILL_BADSTK 	8	/* Internal stack error.		*/

/* codes for SIGBUS */
#define BUS_ADRALN	1	/* Invalid address alignment.		*/
#define BUS_ADRERR	2	/* Nonexistent physical address.	*/
#define BUS_OBJERR	3	/* Object-specific hardware error.	*/

/* codes for SIGSEGV */
#define SEGV_MAPERR	1	/* Address not mapped to object.	*/
#define SEGV_ACCERR	2	/* Invalid permissions for mapped	*/
				/* object.				*/

/* codes for SIGFPE */
#define FPE_INTOVF	1	/* Integer overflow.			*/
#define FPE_INTDIV	2	/* Integer divide by zero.		*/
#define FPE_FLTDIV	3	/* Floating point divide by zero.	*/
#define FPE_FLTOVF	4	/* Floating point overflow.		*/
#define FPE_FLTUND	5	/* Floating point underflow.		*/
#define FPE_FLTRES	6	/* Floating point inexact result.	*/
#define FPE_FLTINV	7	/* Invalid floating point operation.	*/
#define FPE_FLTSUB	8	/* Subscript out of range.		*/

/* codes for SIGTRAP */
#define TRAP_BRKPT	1	/* Process breakpoint.			*/
#define TRAP_TRACE	2	/* Process trace trap.			*/

/* codes for SIGCHLD */
#define CLD_EXITED	1	/* Child has exited			*/
#define CLD_KILLED	2	/* Child has terminated abnormally but	*/
				/* did not create a core file		*/
#define CLD_DUMPED	3	/* Child has terminated abnormally and	*/
				/* created a core file			*/
#define CLD_TRAPPED	4	/* Traced child has trapped		*/
#define CLD_STOPPED	5	/* Child has stopped			*/
#define CLD_CONTINUED	6	/* Stopped child has continued		*/

/* codes for SIGPOLL */
#define POLL_IN		1	/* Data input available			*/
#define POLL_OUT	2	/* Output buffers available		*/
#define POLL_MSG	3	/* Input message available		*/
#define POLL_ERR	4	/* I/O Error				*/
#define POLL_PRI	5	/* High priority input available	*/
#define POLL_HUP	4	/* Device disconnected			*/

/*
 * Machine-dependent signal definitions
 */

typedef int sig_atomic_t;

#if !defined(_ANSI_SOURCE) && !defined(_POSIX_SOURCE)

#include <machine/trap.h>	/* codes for SIGILL, SIGFPE */

/*
 * Information pushed on stack when a signal is delivered.
 * This is used by the kernel to restore state following
 * execution of the signal handler.  It is also made available
 * to the handler to allow it to restore state properly if
 * a non-standard exit is performed.
 *
 * The sequence of the fields/registers in struct sigcontext should match
 * those in mcontext_t.
 */
struct	sigcontext {
	sigset_t	sc_mask;	/* signal mask to restore */

	long		sc_onstack;	/* sigstack state to restore */
	long		sc_rdi;
	long		sc_rsi;
	long		sc_rdx;
	long		sc_rcx;
	long		sc_r8;
	long		sc_r9;
	long		sc_rax;
	long		sc_rbx;
	long		sc_rbp;
	long		sc_r10;
	long		sc_r11;
	long		sc_r12;
	long		sc_r13;
	long		sc_r14;
	long		sc_r15;
	long		sc_trapno;
	long		sc_addr;
	long		sc_flags;
	long		sc_err;
	long		sc_rip;
	long		sc_cs;
	long		sc_rflags;
	long		sc_rsp;
	long		sc_ss;

	unsigned int	sc_len;
	unsigned int	sc_fpformat;
	unsigned int	sc_ownedfp;
	unsigned int	sc_reserved;
	unsigned int	sc_unused01;
	unsigned int	sc_unused02;

	/* 16 byte aligned */

	int		sc_fpregs[128];
	int		__spare__[16];
};

#endif /* !_ANSI_SOURCE && !_POSIX_SOURCE */

#endif /* !_CPU_SIGNAL_H_ */
