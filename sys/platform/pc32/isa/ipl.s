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
 * $DragonFly: src/sys/platform/pc32/isa/ipl.s,v 1.16 2004/01/30 05:42:16 dillon Exp $
 */


/*
 * AT/386
 * Vector interrupt control section
 *
 *  *_imask	- Interrupt masks for various spl*() functions
 *  ipending	- Pending interrupts (set when a masked interrupt occurs)
 */

	.data
	ALIGN_DATA

/* current priority (all off) */

	.globl	tty_imask
tty_imask:	.long	SWI_TTY_MASK
	.globl	bio_imask
bio_imask:	.long	SWI_CLOCK_MASK | SWI_CAMBIO_MASK
	.globl	net_imask
net_imask:	.long	SWI_NET_MASK | SWI_CAMNET_MASK
	.globl	cam_imask
cam_imask:	.long	SWI_CAMBIO_MASK | SWI_CAMNET_MASK
	.globl	soft_imask
soft_imask:	.long	SWI_MASK
	.globl	softnet_imask
softnet_imask:	.long	SWI_NET_MASK
	.globl	softtty_imask
softtty_imask:	.long	SWI_TTY_MASK

	.text
	/*
	 * GENERAL NOTES
	 *
	 *	- fast interrupts are always called with a critical section
	 *	  held
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
	 * checks the cpl for unmasked pending interrupts (fast, normal, or
	 * soft) and schedules them if appropriate, then irets.
	 *
	 * If we are in a critical section we cannot run any pending ints
	 * nor can be play with mp_lock.
	 *
	 */
	SUPERALIGN_TEXT
	.type	doreti,@function
doreti:
	FAKE_MCOUNT(bintr)		/* init "from" bintr -> doreti */
	popl	%eax			/* cpl to restore */
	movl	PCPU(curthread),%ebx
	cli				/* interlock with TDPRI_CRIT */
	cmpl	$0,PCPU(reqflags)	/* short cut if nothing to do */
	je	5f
	movl	%eax,TD_CPL(%ebx)	/* save cpl being restored */
	cmpl	$TDPRI_CRIT,TD_PRI(%ebx) /* can't unpend if in critical sec */
	jge	5f
	addl	$TDPRI_CRIT,TD_PRI(%ebx) /* force all ints to pending */
doreti_next:
	sti				/* allow new interrupts */
	movl	%eax,%ecx		/* cpl being restored */
	notl	%ecx
	cli				/* disallow YYY remove */
#ifdef SMP
	testl	$RQF_IPIQ,PCPU(reqflags)
	jnz	doreti_ipiq
#endif
	testl	PCPU(fpending),%ecx	/* check for an unmasked fast int */
	jnz	doreti_fast

	testl	PCPU(ipending),%ecx
	jnz	doreti_intr

	testl	$RQF_AST_MASK,PCPU(reqflags) /* any pending ASTs? */
	jz	2f
	testl	$PSL_VM,TF_EFLAGS(%esp)
	jz	1f
	cmpl	$1,in_vm86call		/* YYY make per 'cpu'? */
	jnz	doreti_ast
1:
	testb	$SEL_RPL_MASK,TF_CS(%esp)
	jnz	doreti_ast
2:
	/*
	 * Nothing left to do, finish up.  Interrupts are still disabled.
	 * If our temporary cpl mask is 0 then we have processed all pending
	 * fast and normal ints including those requiring the MP lock,
	 * and we have processed as many of the reqflags as possible based
	 * on whether we came from user mode or not.   So if %eax is 0 we
	 * can clear the interrupt-related reqflags.
	 */
	subl	$TDPRI_CRIT,TD_PRI(%ebx)	/* interlocked with cli */
	testl	%eax,%eax
	jnz	5f
	andl	$~RQF_INTPEND,PCPU(reqflags)
5:
	MEXITCOUNT
	.globl	doreti_popl_fs
	.globl	doreti_popl_es
	.globl	doreti_popl_ds
	.globl	doreti_iret
	.globl	doreti_syscall_ret
doreti_syscall_ret:
doreti_popl_fs:
	popl	%fs
doreti_popl_es:
	popl	%es
doreti_popl_ds:
	popl	%ds
	popal
	addl	$8,%esp
doreti_iret:
	iret

	ALIGN_TEXT
	.globl	doreti_iret_fault
doreti_iret_fault:
	subl	$8,%esp
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
	movl	$0,TF_ERR(%esp)	/* XXX should be the error code */
	movl	$T_PROTFLT,TF_TRAPNO(%esp)
	jmp	alltraps_with_regs_pushed

	/*
	 * FAST interrupt pending
	 */
	ALIGN_TEXT
doreti_fast:
	andl	PCPU(fpending),%ecx	/* only check fast ints */
	bsfl	%ecx, %ecx		/* locate the next dispatchable int */
	btrl	%ecx, PCPU(fpending)	/* is it really still pending? */
	jnc	doreti_next
	pushl	%eax			/* YYY cpl (expected by frame) */
#ifdef SMP
	pushl	%ecx			/* save ecx */
	call	try_mplock
	popl	%ecx
	testl	%eax,%eax
	jz	1f
	/* MP lock successful */
#endif
	incl	PCPU(intr_nesting_level)
	call    *fastunpend(,%ecx,4)
	decl	PCPU(intr_nesting_level)
#ifdef SMP
	call	rel_mplock
#endif
	popl	%eax
	jmp	doreti_next
1:
	btsl	%ecx, PCPU(fpending)	/* oops, couldn't get the MP lock */
	popl	%eax			/* add to temp. cpl mask to ignore */
	orl	PCPU(fpending),%eax
	jmp	doreti_next

	/*
	 *  INTR interrupt pending
	 *
	 *  Temporarily back-out our critical section to allow an interrupt
	 *  preempt us when we schedule it.  Bump intr_nesting_level to
	 *  prevent the switch code from recursing via splz too deeply.
	 */
	ALIGN_TEXT
doreti_intr:
	andl	PCPU(ipending),%ecx	/* only check normal ints */
	bsfl	%ecx, %ecx		/* locate the next dispatchable int */
	btrl	%ecx, PCPU(ipending)	/* is it really still pending? */
	jnc	doreti_next
	pushl	%eax
	pushl	%ecx
	incl	TD_NEST_COUNT(%ebx)	/* prevent doreti/splz nesting */
	subl	$TDPRI_CRIT,TD_PRI(%ebx) /* so we can preempt */
	call	sched_ithd		/* YYY must pull in imasks */
	addl	$TDPRI_CRIT,TD_PRI(%ebx)
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
	subl	$TDPRI_CRIT,TD_PRI(%ebx)
1:	call	trap
	addl	$TDPRI_CRIT,TD_PRI(%ebx)
	movl	%esi,%eax		/* restore cpl for loop */
	jmp	doreti_next

#ifdef SMP
	/*
	 * IPIQ message pending.  We clear RQF_IPIQ automatically.
	 */
doreti_ipiq:
	incl	PCPU(intr_nesting_level)
	andl	$~RQF_IPIQ,PCPU(reqflags)
	subl	$8,%esp			/* add dummy vec and ppl */
	call	lwkt_process_ipiq_frame
	addl	$8,%esp
	decl	PCPU(intr_nesting_level)
	movl	TD_CPL(%ebx),%eax	/* retrieve cpl again for loop */
	jmp	doreti_next

#endif

	/*
	 * SPLZ() a C callable procedure to dispatch any unmasked pending
	 *	  interrupts regardless of critical section nesting.  ASTs
	 *	  are not dispatched.
	 *
	 *	YYY at the moment I leave us in a critical section so as
	 *	not to have to mess with the cpls which will soon be obsolete.
	 */
	SUPERALIGN_TEXT

ENTRY(splz)
	pushfl
	pushl	%ebx
	movl	PCPU(curthread),%ebx
	movl	TD_CPL(%ebx),%eax
	addl	$TDPRI_CRIT,TD_PRI(%ebx)

splz_next:
	cli
	movl	%eax,%ecx		/* ecx = ~CPL */
	notl	%ecx
#ifdef SMP
	testl	$RQF_IPIQ,PCPU(reqflags)
	jnz	splz_ipiq
#endif
	testl	PCPU(fpending),%ecx	/* check for an unmasked fast int */
	jnz	splz_fast

	testl	PCPU(ipending),%ecx
	jnz	splz_intr

	subl	$TDPRI_CRIT,TD_PRI(%ebx)

	/*
	 * Nothing left to do, finish up.  Interrupts are still disabled.
	 * If our temporary cpl mask is 0 then we have processed everything
	 * (including any pending fast ints requiring the MP lock), and
	 * we can clear RQF_INTPEND.
	 */
	testl	%eax,%eax
	jnz	5f
	andl	$~RQF_INTPEND,PCPU(reqflags)
5:
	popl	%ebx
	popfl
	ret

	/*
	 * FAST interrupt pending
	 */
	ALIGN_TEXT
splz_fast:
	andl	PCPU(fpending),%ecx	/* only check fast ints */
	bsfl	%ecx, %ecx		/* locate the next dispatchable int */
	btrl	%ecx, PCPU(fpending)	/* is it really still pending? */
	jnc	splz_next
	pushl	%eax
#ifdef SMP
	pushl	%ecx
	call	try_mplock
	popl	%ecx
	testl	%eax,%eax
	jz	1f
#endif
	incl	PCPU(intr_nesting_level)
	call    *fastunpend(,%ecx,4)
	decl	PCPU(intr_nesting_level)
#ifdef SMP
	call	rel_mplock
#endif
	popl	%eax
	jmp	splz_next
1:
	btsl	%ecx, PCPU(fpending)	/* oops, couldn't get the MP lock */
	popl	%eax
	orl	PCPU(fpending),%eax
	jmp	splz_next

	/*
	 *  INTR interrupt pending
	 *
	 *  Temporarily back-out our critical section to allow the interrupt
	 *  preempt us.
	 */
	ALIGN_TEXT
splz_intr:
	andl	PCPU(ipending),%ecx	/* only check normal ints */
	bsfl	%ecx, %ecx		/* locate the next dispatchable int */
	btrl	%ecx, PCPU(ipending)	/* is it really still pending? */
	jnc	splz_next
	sti
	pushl	%eax
	pushl	%ecx
	subl	$TDPRI_CRIT,TD_PRI(%ebx)
	incl	TD_NEST_COUNT(%ebx)	/* prevent doreti/splz nesting */
	call	sched_ithd		/* YYY must pull in imasks */
	addl	$TDPRI_CRIT,TD_PRI(%ebx)
	decl	TD_NEST_COUNT(%ebx)	/* prevent doreti/splz nesting */
	addl	$4,%esp
	popl	%eax
	jmp	splz_next

#ifdef SMP
splz_ipiq:
	andl	$~RQF_IPIQ,PCPU(reqflags)
	pushl	%eax
	call	lwkt_process_ipiq
	popl	%eax
	jmp	splz_next
#endif

	/*
	 * APIC/ICU specific ipl functions provide masking and unmasking
	 * calls for userland.
	 */

#ifdef APIC_IO
#include "i386/isa/apic_ipl.s"
#else
#include "i386/isa/icu_ipl.s"
#endif /* APIC_IO */
