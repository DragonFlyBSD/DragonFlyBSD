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
 */

#ifndef _CPU_SIGNAL_H_
#define	_CPU_SIGNAL_H_

/*
 * Machine-dependent signal definitions
 */

typedef int sig_atomic_t;

#if __BSD_VISIBLE

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
	long		sc_xflags;
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
	unsigned int    sc_unused[8];

	/* 64 byte aligned */
	int             sc_fpregs[256]; /* 1024 bytes */
} __attribute__((aligned(64)));

#endif /* __BSD_VISIBLE */

#endif /* !_CPU_SIGNAL_H_ */
