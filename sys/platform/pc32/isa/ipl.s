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
 * $DragonFly: src/sys/platform/pc32/isa/ipl.s,v 1.4 2003/06/29 03:28:43 dillon Exp $
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

	.globl	_tty_imask
_tty_imask:	.long	SWI_TTY_MASK
	.globl	_bio_imask
_bio_imask:	.long	SWI_CLOCK_MASK | SWI_CAMBIO_MASK
	.globl	_net_imask
_net_imask:	.long	SWI_NET_MASK | SWI_CAMNET_MASK
	.globl	_cam_imask
_cam_imask:	.long	SWI_CAMBIO_MASK | SWI_CAMNET_MASK
	.globl	_soft_imask
_soft_imask:	.long	SWI_MASK
	.globl	_softnet_imask
_softnet_imask:	.long	SWI_NET_MASK
	.globl	_softtty_imask
_softtty_imask:	.long	SWI_TTY_MASK

	.text

	/*
	 * DORETI
	 *
	 * Handle return from interrupts, traps and syscalls.  This function
	 * checks the cpl for unmasked pending interrupts (fast, normal, or
	 * soft) and schedules them if appropriate, then irets.
	 */
	SUPERALIGN_TEXT
	.type	_doreti,@function
_doreti:
	FAKE_MCOUNT(_bintr)		/* init "from" _bintr -> _doreti */
	popl	%eax			/* cpl to restore */
	movl	_curthread,%ebx
	cli				/* interlock with TDPRI_CRIT */
	movl	%eax,TD_CPL(%ebx)	/* save cpl being restored */
	cmpl	$TDPRI_CRIT,TD_PRI(%ebx) /* can't unpend if in critical sec */
	jge	5f
	addl	$TDPRI_CRIT,TD_PRI(%ebx) /* force all ints to pending */
doreti_next:
	sti				/* allow new interrupts */
	movl	%eax,%ecx		/* cpl being restored */
	notl	%ecx
	cli				/* disallow YYY remove */
	testl	_fpending,%ecx		/* check for an unmasked fast int */
	jne	doreti_fast

	movl	_irunning,%edx		/* check for an unmasked normal int */
	notl	%edx			/* that isn't already running */
	andl	%edx, %ecx
	testl	_ipending,%ecx
	jne	doreti_intr
	testl	$AST_PENDING,_astpending /* any pending ASTs? */
	je	2f
	testl	$PSL_VM,TF_EFLAGS(%esp)
	jz	1f
	cmpl	$1,_in_vm86call
	jnz	doreti_ast
1:
	testb	$SEL_RPL_MASK,TF_CS(%esp)
	jnz	doreti_ast

	/*
	 * Nothing left to do, finish up.  Interrupts are still disabled.
	 */
2:
	subl	$TDPRI_CRIT,TD_PRI(%ebx)	/* interlocked with cli */
5:
	decl	_intr_nesting_level
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
	andl	_fpending,%ecx		/* only check fast ints */
	bsfl	%ecx, %ecx		/* locate the next dispatchable int */
	btrl	%ecx, _fpending		/* is it really still pending? */
	jnc	doreti_next
	pushl	%eax			/* YYY cpl */
	call    *_fastunpend(,%ecx,4)
	popl	%eax
	jmp	doreti_next

	/*
	 *  INTR interrupt pending
	 */
	ALIGN_TEXT
doreti_intr:
	andl	_ipending,%ecx		/* only check normal ints */
	bsfl	%ecx, %ecx		/* locate the next dispatchable int */
	btrl	%ecx, _ipending		/* is it really still pending? */
	jnc	doreti_next
	pushl	%eax
	pushl	%ecx
	call	_sched_ithd		/* YYY must pull in imasks */
	addl	$4,%esp
	popl	%eax
	jmp	doreti_next

	/*
	 * AST pending
	 */
doreti_ast:
	andl	$~AST_PENDING,_astpending
	sti
	movl	$T_ASTFLT,TF_TRAPNO(%esp)
	decl	_intr_nesting_level
	call	_trap
	incl	_intr_nesting_level
	movl	TD_CPL(%ebx),%eax	/* retrieve cpl again for loop */
	jmp	doreti_next


	/*
	 * SPLZ() a C callable procedure to dispatch any unmasked pending
	 *	  interrupts regardless of critical section nesting.  ASTs
	 *	  are not dispatched.
	 */
	SUPERALIGN_TEXT

ENTRY(splz)
	pushl	%ebx
	movl	_curthread,%ebx
	movl	TD_CPL(%ebx),%eax

splz_next:
	movl	%eax,%ecx		/* ecx = ~CPL */
	notl	%ecx
	testl	_fpending,%ecx		/* check for an unmasked fast int */
	jne	splz_fast

	movl	_irunning,%edx		/* check for an unmasked normal int */
	notl	%edx			/* that isn't already running */
	andl	%edx, %ecx
	testl	_ipending,%ecx
	jne	splz_intr

	popl	%ebx
	ret

	/*
	 * FAST interrupt pending
	 */
	ALIGN_TEXT
splz_fast:
	andl	_fpending,%ecx		/* only check fast ints */
	bsfl	%ecx, %ecx		/* locate the next dispatchable int */
	btrl	%ecx, _fpending		/* is it really still pending? */
	jnc	splz_next
	pushl	%eax
	call    *_fastunpend(,%ecx,4)
	popl	%eax
	jmp	splz_next

	/*
	 *  INTR interrupt pending
	 */
	ALIGN_TEXT
splz_intr:
	andl	_ipending,%ecx		/* only check normal ints */
	bsfl	%ecx, %ecx		/* locate the next dispatchable int */
	btrl	%ecx, _ipending		/* is it really still pending? */
	jnc	splz_next
	pushl	%eax
	pushl	%ecx
	call	_sched_ithd		/* YYY must pull in imasks */
	addl	$4,%esp
	popl	%eax
	jmp	splz_next

	/*
	 * APIC/ICU specific ipl functions provide masking and unmasking
	 * calls for userland.
	 */

#ifdef APIC_IO
#include "i386/isa/apic_ipl.s"
#else
#include "i386/isa/icu_ipl.s"
#endif /* APIC_IO */
