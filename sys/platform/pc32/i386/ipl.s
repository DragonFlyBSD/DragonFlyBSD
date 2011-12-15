/*-
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
	 * NOTE: Since SPLs no longer exist, all callers of this function
	 * push $0 for the CPL.  HOWEVER, we *STILL* use the cpl mask within
	 * this function to mark interrupts which could not be dispatched
	 * do to the unavailability of the BGL.
	 */
	SUPERALIGN_TEXT
	.globl	doreti
	.type	doreti,@function
doreti:
	FAKE_MCOUNT(bintr)		/* init "from" bintr -> doreti */
	popl	%eax			/* cpl to restore XXX */
	movl	$0,%eax			/* irq mask unavailable due to BGL */
	movl	PCPU(curthread),%ebx
	cli				/* interlock with td_critcount */
	cmpl	$0,PCPU(reqflags)	/* short cut if nothing to do */
	je	5f
	testl	$-1,TD_CRITCOUNT(%ebx)	/* can't unpend if in critical sec */
	jne	5f
	incl	TD_CRITCOUNT(%ebx)	/* force all ints to pending */
doreti_next:
	cli				/* re-assert cli on loop */
	movl	%eax,%ecx		/* irq mask unavailable due to BGL */
	notl	%ecx
#ifdef SMP
	testl	$RQF_IPIQ,PCPU(reqflags)
	jnz	doreti_ipiq
#endif
	testl	$RQF_TIMER,PCPU(reqflags)
	jnz	doreti_timer
	/*
	 * check for an unmasked int (6 groups)
	 */
	movl	$0,%edx
	testl	PCPU_E4(ipending,%edx),%ecx
	jnz	doreti_fast

	movl	$1,%edx
	testl	PCPU_E4(ipending,%edx),%ecx
	jnz	doreti_fast

	movl	$2,%edx
	testl	PCPU_E4(ipending,%edx),%ecx
	jnz	doreti_fast

	movl	$3,%edx
	testl	PCPU_E4(ipending,%edx),%ecx
	jnz	doreti_fast

	movl	$4,%edx
	testl	PCPU_E4(ipending,%edx),%ecx
	jnz	doreti_fast

	movl	$5,%edx
	testl	PCPU_E4(ipending,%edx),%ecx
	jnz	doreti_fast

	movl	PCPU(spending),%ecx	/* check for a pending software int */
	cmpl	$0,%ecx
	jnz	doreti_soft

	testl	$RQF_AST_MASK,PCPU(reqflags) /* any pending ASTs? */
	jz	2f
	testl	$PSL_VM,TF_EFLAGS(%esp)
	jz	1f
	cmpl	$1,in_vm86call		/* YYY make per 'cpu'? */
	jnz	doreti_ast
1:
	/* ASTs are only applicable when returning to userland */
	testb	$SEL_RPL_MASK,TF_CS(%esp)
	jnz	doreti_ast
2:
	/*
	 * Nothing left to do, finish up.  Interrupts are still disabled.
	 * %eax contains the mask of IRQ's that are not available due to
	 * BGL requirements.  We can only clear RQF_INTPEND if *ALL* pending
	 * interrupts have been processed.
	 */
	decl	TD_CRITCOUNT(%ebx)	/* interlocked with cli */
	testl	%eax,%eax
	jnz	5f
	andl	$~RQF_INTPEND,PCPU(reqflags)
5:
	MEXITCOUNT

	/*
	 * Restore the segment registers.  Since segment register values
	 * can be set from user mode, this can result in a kernel mode
	 * exception.  The trap code will revector to the *_fault code
	 * which then sets up a T_PROTFLT signal.  If the signal is
	 * sent to userland, sendsig() will automatically clean up all
	 * the segment registers to avoid a loop.
	 */
	.globl	doreti_popl_gs
	.globl	doreti_popl_fs
	.globl	doreti_popl_es
	.globl	doreti_popl_ds
	.globl	doreti_iret
	.globl	doreti_syscall_ret
doreti_syscall_ret:
doreti_popl_gs:
	popl	%gs
doreti_popl_fs:
	popl	%fs
doreti_popl_es:
	popl	%es
doreti_popl_ds:
	popl	%ds
	popal
	addl	$3*4,%esp	/* xflags, trap, err */
doreti_iret:
	iret

	/*
	 * Interrupts are likely disabled due to the above interlock
	 * between cli/iretq.  We must enable them before calling any
	 * high level function.
	 */
	ALIGN_TEXT
	.globl	doreti_iret_fault
doreti_iret_fault:
	subl	$3*4,%esp	/* xflags, trap, err */
	pushal
	pushl	%ds
	.globl	doreti_popl_ds_fault
doreti_popl_ds_fault:
	pushl	%es
	.globl	doreti_popl_es_fault
doreti_popl_es_fault:
	pushl	%fs
	.globl	doreti_popl_fs_fault
doreti_popl_fs_fault:
	pushl	%gs
	.globl	doreti_popl_gs_fault
doreti_popl_gs_fault:
	sti
	movl	$0,TF_ERR(%esp)	/* XXX should be the error code */
	movl	$T_PROTFLT,TF_TRAPNO(%esp)
	jmp	alltraps_with_regs_pushed

	/*
	 * Interrupt pending.  NOTE: stack context holds frame structure
	 * for interrupt procedure, do not do random pushes or pops!
	 */
	ALIGN_TEXT
doreti_fast:
	andl	PCPU_E4(ipending,%edx),%ecx
	sti
	bsfl	%ecx, %ecx		/* locate the next dispatchable int */
	btrl	%ecx, PCPU_E4(ipending,%edx)
					/* is it really still pending? */
	jnc	doreti_next

	shll	$5, %edx
	orl	%edx, %ecx		/* form intr number */

	pushl	%eax			/* save IRQ mask unavailable for BGL */
					/* NOTE: is also CPL in frame */
	call	dofastunpend		/* unpend intr %ecx */
	popl	%eax
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
	pushl	%eax
	pushl	%ecx
	incl	TD_NEST_COUNT(%ebx)	/* prevent doreti/splz nesting */
	decl	TD_CRITCOUNT(%ebx)	/* so we can preempt */
	call	sched_ithd_soft		/* YYY must pull in imasks */
	incl	TD_CRITCOUNT(%ebx)
	decl	TD_NEST_COUNT(%ebx)
	addl	$4,%esp
	popl	%eax
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
	andl	$~(RQF_AST_SIGNAL|RQF_AST_UPCALL),PCPU(reqflags)
	sti
	movl	%eax,%esi		/* save cpl (can't use stack) */
	movl	$T_ASTFLT,TF_TRAPNO(%esp)
	pushl	%esp			/* pass frame by reference */
	decl	TD_CRITCOUNT(%ebx)
	call	trap
	incl	TD_CRITCOUNT(%ebx)
	addl	$4,%esp
	movl	%esi,%eax		/* restore cpl for loop */
	jmp	doreti_next

#ifdef SMP
	/*
	 * IPIQ message pending.  We clear RQF_IPIQ automatically.
	 */
doreti_ipiq:
	movl	%eax,%esi		/* save cpl (can't use stack) */
	incl	PCPU(intr_nesting_level)
	andl	$~RQF_IPIQ,PCPU(reqflags)
	sti
	subl	$8,%esp			/* add dummy vec and ppl */
	pushl	%esp			/* pass frame by reference */
	call	lwkt_process_ipiq_frame
	addl	$12,%esp
	decl	PCPU(intr_nesting_level)
	movl	%esi,%eax		/* restore cpl for loop */
	jmp	doreti_next
#endif

doreti_timer:
	movl	%eax,%esi		/* save cpl (can't use stack) */
	incl	PCPU(intr_nesting_level)
	andl	$~RQF_TIMER,PCPU(reqflags)
	sti
	subl	$8,%esp			/* add dummy vec and ppl */
	pushl	%esp			/* pass frame by reference */
	call	lapic_timer_process_frame
	addl	$12,%esp
	decl	PCPU(intr_nesting_level)
	movl	%esi,%eax		/* restore cpl for loop */
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
	pushfl
	pushl	%ebx
	movl	PCPU(curthread),%ebx
	incl	TD_CRITCOUNT(%ebx)
	movl	$0,%eax

splz_next:
	cli
	movl	%eax,%ecx		/* ecx = ~CPL */
	notl	%ecx
#ifdef SMP
	testl	$RQF_IPIQ,PCPU(reqflags)
	jnz	splz_ipiq
#endif
	testl	$RQF_TIMER,PCPU(reqflags)
	jnz	splz_timer

	/*
	 * check for an unmasked int (6 groups)
	 */
	movl	$0,%edx
	testl	PCPU_E4(ipending,%edx),%ecx
	jnz	splz_fast

	movl	$1,%edx
	testl	PCPU_E4(ipending,%edx),%ecx
	jnz	splz_fast

	movl	$2,%edx
	testl	PCPU_E4(ipending,%edx),%ecx
	jnz	splz_fast

	movl	$3,%edx
	testl	PCPU_E4(ipending,%edx),%ecx
	jnz	splz_fast

	movl	$4,%edx
	testl	PCPU_E4(ipending,%edx),%ecx
	jnz	splz_fast

	movl	$5,%edx
	testl	PCPU_E4(ipending,%edx),%ecx
	jnz	splz_fast

	movl	PCPU(spending),%ecx
	cmpl	$0,%ecx
	jnz	splz_soft

	decl	TD_CRITCOUNT(%ebx)

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
	popl	%ebx
	popfl
	ret

	/*
	 * Interrupt pending
	 */
	ALIGN_TEXT
splz_fast:
	andl	PCPU_E4(ipending,%edx),%ecx
	sti
	bsfl	%ecx, %ecx		/* locate the next dispatchable int */
	btrl	%ecx, PCPU_E4(ipending,%edx)
					/* is it really still pending? */
	jnc	splz_next

	shll	$5, %edx
	orl	%edx, %ecx		/* form intr number */

	pushl	%eax
	call	dofastunpend		/* unpend intr %ecx */
	popl	%eax
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
	pushl	%eax
	pushl	%ecx
	decl	TD_CRITCOUNT(%ebx)
	incl	TD_NEST_COUNT(%ebx)	/* prevent doreti/splz nesting */
	call	sched_ithd_soft		/* YYY must pull in imasks */
	incl	TD_CRITCOUNT(%ebx)
	decl	TD_NEST_COUNT(%ebx)	/* prevent doreti/splz nesting */
	addl	$4,%esp
	popl	%eax
	jmp	splz_next

#ifdef SMP
splz_ipiq:
	andl	$~RQF_IPIQ,PCPU(reqflags)
	sti
	pushl	%eax
	call	lwkt_process_ipiq
	popl	%eax
	jmp	splz_next
#endif

splz_timer:
	andl	$~RQF_TIMER,PCPU(reqflags)
	sti
	pushl	%eax
	call	lapic_timer_process
	popl	%eax
	jmp	splz_next

	/*
	 * dofastunpend(%ecx:intr)
	 *
	 * An interrupt previously made pending can now be run,
	 * execute it by pushing a dummy interrupt frame and 
	 * calling ithread_fast_handler to execute or schedule it.
	 * 
	 * ithread_fast_handler() returns 0 if it wants us to unmask
	 * further interrupts.
	 */
#define PUSH_DUMMY							\
	pushfl ;		/* phys int frame / flags */		\
	pushl	%cs ;		/* phys int frame / cs */		\
	pushl	12(%esp) ;	/* original caller eip */		\
	pushl	$0 ;		/* dummy error code */			\
	pushl	$0 ;		/* dummy trap type */			\
	pushl	$0 ;		/* dummy xflags */			\
	subl	$13*4,%esp ;	/* pushal + 4 seg regs (dummy) + CPL */	\

#define POP_DUMMY							\
	addl	$19*4,%esp ;						\

dofastunpend:
	pushl	%ebp			/* frame for backtrace */
	movl	%esp,%ebp
	PUSH_DUMMY
	pushl	%ecx			/* last part of intrframe = intr */
	incl	fastunpend_count
	pushl	%esp			/* pass frame by reference */
	call	ithread_fast_handler	/* returns 0 to unmask */
	addl	$4,%esp			/* remove pointer, now intr on top */
	cmpl	$0,%eax
	jnz	1f
	movl	MachIntrABI + MACHINTR_INTREN, %eax
	call	*%eax			/* MachIntrABI.intren(intr) */
1:
	addl	$4,%esp
	POP_DUMMY
	popl	%ebp
	ret

