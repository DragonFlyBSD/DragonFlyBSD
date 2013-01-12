/*
 * Copyright (c) 2008 The DragonFly Project.  All rights reserved.
 * 
 * This code is derived from software contributed to The DragonFly Project
 * by Matthew Dillon <dillon@backplane.com>
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name of The DragonFly Project nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific, prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 * 
 * ---
 *
 * Copyright (c) 1989, 1990 William F. Jolitz.
 * Copyright (c) 1990 The Regents of the University of California.
 * All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * William Jolitz.
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
 *	@(#)ipl.s
 *
 * $FreeBSD: src/sys/i386/isa/ipl.s,v 1.32.2.3 2002/05/16 16:03:56 bde Exp $
 */

#include <machine/asmacros.h>
#include <machine/segments.h>
#include <machine/ipl.h>
#include <machine/lock.h>
#include <machine/psl.h>
#include <machine/trap.h>
 
#include "assym.s"

/*
 * AT/386
 * Vector interrupt control section
 *
 *  ipending	- Pending interrupts (set when a masked interrupt occurs)
 *  spending	- Pending software interrupts
 */
	.data
	ALIGN_DATA

	.globl		fastunpend_count
fastunpend_count:	.long	0

	.text
	SUPERALIGN_TEXT

	/*
	 * GENERAL NOTES
	 *
	 *	- interrupts are always called with a critical section held
	 *
	 *	- we release our critical section when scheduling interrupt
	 *	  or softinterrupt threads in order so they can preempt
	 *	  (unless we are called manually from a critical section, in
	 *	  which case there will still be a critical section and
	 *	  they won't preempt anyway).
	 *
	 *	- TD_NEST_COUNT prevents splz from nesting too deeply within
	 *	  itself.  It is *not* actually an interrupt nesting count.
	 *	  PCPU(intr_nesting_level) is an interrupt nesting count.
	 *
	 *	- We have to be careful in regards to local interrupts
	 *	  occuring simultaniously with our doreti and splz 
	 *	  processing.
	 *
	 *	- Interrupts must be enabled when calling higher level
	 *	  functions in order to avoid deadlocking against things
	 *	  like smp_invltlb.
	 */

	/*
	 * DORETI
	 *
	 * Handle return from interrupts, traps and syscalls.  This function
	 * checks the cpl for unmasked pending interrupts (hardware or soft)
	 * and schedules them if appropriate, then irets.
	 *
	 * If we are in a critical section we cannot run any pending ints.
	 *
	 * The stack contains a trapframe at the start of doreti.
	 */
	SUPERALIGN_TEXT
	.globl	doreti
	.type	doreti,@function
doreti:
	FAKE_MCOUNT(bintr)		/* init "from" bintr -> doreti */
	movq	$0,%rax			/* irq mask unavailable due to BGL */
	movq	PCPU(curthread),%rbx
	cli				/* interlock with critical section */
	cmpl	$0,PCPU(reqflags)	/* short cut if nothing to do */
	je	5f
	testl	$-1,TD_CRITCOUNT(%rbx)	/* can't unpend if in critical sec */
	jne	5f
	incl	TD_CRITCOUNT(%rbx)	/* force all ints to pending */
doreti_next:
	cli				/* re-assert cli on loop */
	movq	%rax,%rcx		/* irq mask unavailable due to BGL */
	notq	%rcx
	testl	$RQF_IPIQ,PCPU(reqflags)
	jnz	doreti_ipiq
	testl	$RQF_TIMER,PCPU(reqflags)
	jnz	doreti_timer
	/*
	 * check for an unmasked int (3 groups)
	 */
	movq	$0,%rdx
	testq	PCPU_E8(ipending,%rdx),%rcx
	jnz	doreti_fast

	movq	$1,%rdx
	testq	PCPU_E8(ipending,%rdx),%rcx
	jnz	doreti_fast

	movq	$2,%rdx
	testq	PCPU_E8(ipending,%rdx),%rcx
	jnz	doreti_fast

	movl	PCPU(spending),%ecx	/* check for a pending software int */
	cmpl	$0,%ecx
	jnz	doreti_soft

	testl	$RQF_AST_MASK,PCPU(reqflags) /* any pending ASTs? */
	jz	2f

	/* ASTs are only applicable when returning to userland */
	testb	$SEL_RPL_MASK,TF_CS(%rsp)
	jnz	doreti_ast
2:
	/*
	 * Nothing left to do, finish up.  Interrupts are still disabled.
	 * %eax contains the mask of IRQ's that are not available due to
	 * BGL requirements.  We can only clear RQF_INTPEND if *ALL* pending
	 * interrupts have been processed.
	 */
	decl	TD_CRITCOUNT(%rbx)	/* interlocked with cli */
	testl	%eax,%eax
	jnz	5f
	andl	$~RQF_INTPEND,PCPU(reqflags)
5:
	MEXITCOUNT

	/*
	 * (interrupts are disabled here)
	 *
	 * Restore register and iret.  iret can fault on %rip (which is
	 * really stupid).  If this occurs we re-fault and vector to
	 * doreti_iret_fault().
	 *
	 * ...
	 * can be set from user mode, this can result in a kernel mode
	 * exception.  The trap code will revector to the *_fault code
	 * which then sets up a T_PROTFLT signal.  If the signal is
	 * sent to userland, sendsig() will automatically clean up all
	 * the segment registers to avoid a loop.
	 */
	.globl	doreti_iret
	.globl	doreti_syscall_ret
doreti_syscall_ret:
	POP_FRAME		/* registers and %gs (+cli) */
	/* WARNING: special global doreti_iret is  also used by exception.S */
doreti_iret:
	iretq

	/*
	 * doreti_iret_fault.  Alternative return code for the case where
	 * we get a fault in the doreti_exit code above.  trap()
	 * (sys/platform/pc64/x86_64/trap.c) catches this specific * case,
	 * sends the process a signal and continues in the corresponding
	 * place in the code below.
	 *
	 * Interrupts are likely disabled due to the above interlock
	 * between cli/iretq.  We must enable them before calling any
	 * high level function.
	 */
	ALIGN_TEXT
	.globl	doreti_iret_fault
doreti_iret_fault:
	PUSH_FRAME_NOSWAP
	sti
	movq	$T_PROTFLT,TF_TRAPNO(%rsp)
	movq	$0,TF_ERR(%rsp)	/* XXX should be the error code */
	movq	$0,TF_ADDR(%rsp)
	FAKE_MCOUNT(TF_RIP(%rsp))
	jmp	calltrap

	/*
	 * Interrupt pending.  NOTE: stack context holds frame structure
	 * for interrupt procedure, do not do random pushes or pops!
	 */
	ALIGN_TEXT
doreti_fast:
	andq	PCPU_E8(ipending,%rdx),%rcx
	sti
	bsfq	%rcx, %rcx		/* locate the next dispatchable int */
	btrq	%rcx, PCPU_E8(ipending,%rdx)
					/* is it really still pending? */
	jnc	doreti_next

	shlq	$6, %rdx
	orq	%rdx, %rcx		/* form intr number */

	pushq	%rax			/* save IRQ mask unavailable for BGL */
					/* NOTE: is also CPL in frame */
	call	dofastunpend		/* unpend intr %rcx */
	popq	%rax
	jmp	doreti_next

	/*
	 *  SOFT interrupt pending
	 *
	 *  Temporarily back-out our critical section to allow an interrupt
	 *  preempt us when we schedule it.  Bump intr_nesting_level to
	 *  prevent the switch code from recursing via splz too deeply.
	 */
	ALIGN_TEXT
doreti_soft:
	sti
	bsfl	%ecx,%ecx		/* locate the next pending softint */
	btrl	%ecx,PCPU(spending)	/* make sure its still pending */
	jnc	doreti_next
	addl	$FIRST_SOFTINT,%ecx	/* actual intr number */
	pushq	%rax
	movl	%ecx,%edi		/* argument to C call */
	incl	TD_NEST_COUNT(%rbx)	/* prevent doreti/splz nesting */
	decl	TD_CRITCOUNT(%rbx)	/* so we can preempt */
	call	sched_ithd_soft		/* YYY must pull in imasks */
	incl	TD_CRITCOUNT(%rbx)
	decl	TD_NEST_COUNT(%rbx)
	popq	%rax
	jmp	doreti_next

	/*
	 * AST pending.  We clear RQF_AST_SIGNAL automatically, the others
	 * are cleared by the trap as they are processed.
	 *
	 * Temporarily back-out our critical section because trap() can be
	 * a long-winded call, and we want to be more syscall-like.  
	 *
	 * YYY theoretically we can call lwkt_switch directly if all we need
	 * to do is a reschedule.
	 */
doreti_ast:
	andl	$~RQF_AST_SIGNAL,PCPU(reqflags)
	sti
	movl	%eax,%r12d		/* save cpl (can't use stack) */
	movl	$T_ASTFLT,TF_TRAPNO(%rsp)
	movq	%rsp,%rdi		/* pass frame by ref (%edi = C arg) */
	decl	TD_CRITCOUNT(%rbx)
	call	trap
	incl	TD_CRITCOUNT(%rbx)
	movl	%r12d,%eax		/* restore cpl for loop */
	jmp	doreti_next

	/*
	 * IPIQ message pending.  We clear RQF_IPIQ automatically.
	 */
doreti_ipiq:
	movl	%eax,%r12d		/* save cpl (can't use stack) */
	incl	PCPU(intr_nesting_level)
	andl	$~RQF_IPIQ,PCPU(reqflags)
	sti
	subq	$8,%rsp			/* trapframe->intrframe */
	movq	%rsp,%rdi		/* pass frame by ref (C arg) */
	call	lwkt_process_ipiq_frame
	addq	$8,%rsp			/* intrframe->trapframe */
	decl	PCPU(intr_nesting_level)
	movl	%r12d,%eax		/* restore cpl for loop */
	jmp	doreti_next

doreti_timer:
	movl	%eax,%r12d		/* save cpl (can't use stack) */
	incl	PCPU(intr_nesting_level)
	andl	$~RQF_TIMER,PCPU(reqflags)
	sti
	subq	$8,%rsp			/* trapframe->intrframe */
	movq	%rsp,%rdi		/* pass frame by ref (C arg) */
	call	lapic_timer_process_frame
	addq	$8,%rsp			/* intrframe->trapframe */
	decl	PCPU(intr_nesting_level)
	movl	%r12d,%eax		/* restore cpl for loop */
	jmp	doreti_next

	/*
	 * SPLZ() a C callable procedure to dispatch any unmasked pending
	 *	  interrupts regardless of critical section nesting.  ASTs
	 *	  are not dispatched.
	 *
	 * 	  Use %eax to track those IRQs that could not be processed
	 *	  due to BGL requirements.
	 */
	SUPERALIGN_TEXT

ENTRY(splz)
	pushfq
	pushq	%rbx
	movq	PCPU(curthread),%rbx
	incl	TD_CRITCOUNT(%rbx)
	movq	$0,%rax

splz_next:
	cli
	movq	%rax,%rcx		/* rcx = ~CPL */
	notq	%rcx
	testl	$RQF_IPIQ,PCPU(reqflags)
	jnz	splz_ipiq
	testl	$RQF_TIMER,PCPU(reqflags)
	jnz	splz_timer
	/*
	 * check for an unmasked int (3 groups)
	 */
	movq	$0,%rdx
	testq	PCPU_E8(ipending,%rdx),%rcx
	jnz	splz_fast

	movq	$1,%rdx
	testq	PCPU_E8(ipending,%rdx),%rcx
	jnz	splz_fast

	movq	$2,%rdx
	testq	PCPU_E8(ipending,%rdx),%rcx
	jnz	splz_fast

	movl	PCPU(spending),%ecx
	cmpl	$0,%ecx
	jnz	splz_soft

	decl	TD_CRITCOUNT(%rbx)

	/*
	 * Nothing left to do, finish up.  Interrupts are still disabled.
	 * If our mask of IRQs we couldn't process due to BGL requirements
	 * is 0 then there are no pending interrupt sources left and we
	 * can clear RQF_INTPEND.
	 */
	testl	%eax,%eax
	jnz	5f
	andl	$~RQF_INTPEND,PCPU(reqflags)
5:
	popq	%rbx
	popfq
	ret

	/*
	 * Interrupt pending
	 */
	ALIGN_TEXT
splz_fast:
	andq	PCPU_E8(ipending,%rdx),%rcx
	sti
	bsfq	%rcx, %rcx		/* locate the next dispatchable int */
	btrq	%rcx, PCPU_E8(ipending,%rdx)
					/* is it really still pending? */
	jnc	splz_next

	shlq	$6, %rdx
	orq	%rdx, %rcx		/* form intr number */

	pushq	%rax
	call	dofastunpend		/* unpend intr %rcx */
	popq	%rax
	jmp	splz_next

	/*
	 *  SOFT interrupt pending
	 *
	 *  Temporarily back-out our critical section to allow the interrupt
	 *  preempt us.
	 */
	ALIGN_TEXT
splz_soft:
	sti
	bsfl	%ecx,%ecx		/* locate the next pending softint */
	btrl	%ecx,PCPU(spending)	/* make sure its still pending */
	jnc	splz_next
	addl	$FIRST_SOFTINT,%ecx	/* actual intr number */
	sti
	pushq	%rax
	movl	%ecx,%edi		/* C argument */
	incl	TD_NEST_COUNT(%rbx)	/* prevent doreti/splz nesting */
	decl	TD_CRITCOUNT(%rbx)
	call	sched_ithd_soft		/* YYY must pull in imasks */
	incl	TD_CRITCOUNT(%rbx)
	decl	TD_NEST_COUNT(%rbx)	/* prevent doreti/splz nesting */
	popq	%rax
	jmp	splz_next

splz_ipiq:
	andl	$~RQF_IPIQ,PCPU(reqflags)
	sti
	pushq	%rax
	call	lwkt_process_ipiq
	popq	%rax
	jmp	splz_next

splz_timer:
	andl	$~RQF_TIMER,PCPU(reqflags)
	sti
	pushq	%rax
	call	lapic_timer_process
	popq	%rax
	jmp	splz_next

	/*
	 * dofastunpend(%rcx:intr)
	 *
	 * A interrupt previously made pending can now be run,
	 * execute it by pushing a dummy interrupt frame and 
	 * calling ithread_fast_handler to execute or schedule it.
	 * 
	 * ithread_fast_handler() returns 0 if it wants us to unmask
	 * further interrupts.
	 */
#define PUSH_DUMMY							\
	pushfq ;			/* phys int frame / flags */	\
	xorq	%rax,%rax ;		/* something not SEL_UPL */	\
	pushq	%rax ;			/* phys int frame / cs */	\
	pushq	3*8(%rsp) ;		/* original caller eip */	\
	subq	$TF_RIP,%rsp ;		/* trap frame */		\
	movq	$0,TF_XFLAGS(%rsp) ;	/* extras */			\
	movq	$0,TF_TRAPNO(%rsp) ;	/* extras */			\
	movq	$0,TF_ADDR(%rsp) ;	/* extras */			\
	movq	$0,TF_FLAGS(%rsp) ;	/* extras */			\
	movq	$0,TF_ERR(%rsp) ;	/* extras */			\

#define POP_DUMMY							\
	addq	$TF_RIP+(3*8),%rsp ;					\

dofastunpend:
	pushq	%rbp			/* frame for backtrace */
	movq	%rsp,%rbp
	PUSH_DUMMY
	pushq	%rcx			/* last part of intrframe = intr */
	incl	fastunpend_count
	movq	%rsp,%rdi		/* pass frame by reference C arg */
	call	ithread_fast_handler	/* returns 0 to unmask */
	popq	%rdi			/* intrframe->trapframe */
					/* + also rdi C arg to next call */
	cmpl	$0,%eax
	jnz	1f
	movq	MachIntrABI + MACHINTR_INTREN, %rax
	callq	*%rax			/* MachIntrABI.intren(intr) */
1:
	POP_DUMMY
	popq	%rbp
	ret

