/*-
 * Copyright (c) 1990 The Regents of the University of California.
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
 * $FreeBSD: src/sys/i386/i386/exception.s,v 1.65.2.3 2001/08/15 01:23:49 peter Exp $
 */

#include <machine/asmacros.h>
#include <machine/segments.h>
#include <machine/lock.h>
#include <machine/psl.h>
#include <machine/trap.h>

#include "assym.s"

	.text

/*
 * This function is what cpu_heavy_restore jumps to after a new process
 * is created.  The LWKT subsystem switches while holding a critical
 * section and we maintain that abstraction here (e.g. because 
 * cpu_heavy_restore needs it due to PCB_*() manipulation), then get out of
 * it before calling the initial function (typically fork_return()) and/or
 * returning to user mode.
 *
 * The MP lock is not held at any point but the critcount is bumped
 * on entry to prevent interruption of the trampoline at a bad point.
 */
ENTRY(fork_trampoline)
	movl	PCPU(curthread),%eax
	decl	TD_CRITCOUNT(%eax)

	/*
	 * cpu_set_fork_handler intercepts this function call to
	 * have this call a non-return function to stay in kernel mode.
	 *
	 * initproc has its own fork handler, start_init(), which DOES
	 * return.
	 *
	 * The function (set in pcb_esi) gets passed two arguments,
	 * the primary parameter set in pcb_ebx and a pointer to the
	 * trapframe.
	 *   void (func)(int arg, struct trapframe *frame);
	 */
	pushl	%esp			/* pass frame by reference */
	pushl	%ebx			/* arg1 */
	call	*%esi			/* function */
	addl	$8,%esp
	/* cut from syscall */

	call	splz

#if defined(INVARIANTS) && defined(SMP)
	movl	PCPU(curthread),%eax
	cmpl	$0,TD_MPCOUNT(%eax)
	je	1f
	pushl	%esi
	pushl	TD_MPCOUNT(%eax)
	pushl	$pmsg4
	call	panic
pmsg4:  .asciz	"fork_trampoline mpcount %d after calling %p"
	.p2align 2
1:
#endif
	/*
	 * Return via doreti to handle ASTs.
	 */
	MEXITCOUNT
	pushl	$0		/* if_ppl */
	pushl	$0		/* if_vec */
	pushl	%esp		/* pass by reference */
	call	go_user
	/* NOT REACHED */


