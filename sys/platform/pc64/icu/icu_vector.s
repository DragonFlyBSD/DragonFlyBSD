/*
 * Copyright (c) 2008 The DragonFly Project.  All rights reserved.
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
 * from: vector.s, 386BSD 0.1 unknown origin
 * $FreeBSD: src/sys/i386/isa/icu_vector.s,v 1.14.2.2 2000/07/18 21:12:42 dfr Exp $
 * $DragonFly: src/sys/platform/pc64/icu/icu_vector.s,v 1.1 2008/08/29 17:07:16 dillon Exp $
 */
/*
 * WARNING!  SMP builds can use the ICU now so this code must be MP safe.
 */

#include "opt_auto_eoi.h"

#include <machine/asmacros.h>
#include <machine/lock.h>
#include <machine/psl.h>
#include <machine/trap.h>
#include <machine/segments.h>

#include <machine_base/icu/icu.h>
#include <bus/isa/i386/isa.h>

#include "assym.s"
#include "icu_ipl.h"

#define ICU_IMR_OFFSET		1	/* IO_ICU{1,2} + 1 */

#define	ICU_EOI			0x20	/* XXX - define elsewhere */

#define	IRQ_LBIT(irq_num)	(1 << (irq_num))
#define	IRQ_BIT(irq_num)	(1 << ((irq_num) % 8))
#define	IRQ_BYTE(irq_num)	((irq_num) >> 3)

#ifdef AUTO_EOI_1
#define	ENABLE_ICU1		/* use auto-EOI to reduce i/o */
#define	OUTB_ICU1
#else
#define	ENABLE_ICU1 							\
	movb	$ICU_EOI,%al ;	/* as soon as possible send EOI ... */ 	\
	OUTB_ICU1 ;		/* ... to clear in service bit */	\

#define	OUTB_ICU1 							\
	outb	%al,$IO_ICU1 ;						\

#endif

#ifdef AUTO_EOI_2
/*
 * The data sheet says no auto-EOI on slave, but it sometimes works.
 */
#define	ENABLE_ICU1_AND_2	ENABLE_ICU1
#else
#define	ENABLE_ICU1_AND_2 						\
	movb	$ICU_EOI,%al ;	/* as above */ 				\
	outb	%al,$IO_ICU2 ;	/* but do second icu first ... */ 	\
	OUTB_ICU1 ;	/* ... then first icu (if !AUTO_EOI_1) */	\

#endif

/*
 * Macro helpers
 */
#define ICU_PUSH_FRAME							\
	PUSH_FRAME ;		/* 15 regs + space for 5 extras */	\
	movl $0,TF_XFLAGS(%rsp) ;					\
	movl $0,TF_TRAPNO(%rsp) ;					\
	movl $0,TF_ADDR(%rsp) ;						\
	movl $0,TF_FLAGS(%rsp) ;					\
	movl $0,TF_ERR(%rsp) ;						\
	cld ;								\

#define MASK_IRQ(icu, irq_num)						\
	ICU_IMASK_LOCK ;						\
	movb	icu_imen + IRQ_BYTE(irq_num),%al ;			\
	orb	$IRQ_BIT(irq_num),%al ;					\
	movb	%al,icu_imen + IRQ_BYTE(irq_num) ;			\
	outb	%al,$icu+ICU_IMR_OFFSET ;				\
	ICU_IMASK_UNLOCK ;						\

#define UNMASK_IRQ(icu, irq_num)					\
	cmpl	$0,%eax ;						\
	jnz	8f ;							\
	ICU_IMASK_LOCK ;						\
	movb	icu_imen + IRQ_BYTE(irq_num),%al ;			\
	andb	$~IRQ_BIT(irq_num),%al ;				\
	movb	%al,icu_imen + IRQ_BYTE(irq_num) ;			\
	outb	%al,$icu+ICU_IMR_OFFSET ;				\
	ICU_IMASK_UNLOCK ;						\
8: ;									\
	
/*
 * Fast interrupt call handlers run in the following sequence:
 *
 *	- Push the trap frame required by doreti.
 *	- Mask the interrupt and reenable its source.
 *	- If we cannot take the interrupt set its fpending bit and
 *	  doreti.
 *	- If we can take the interrupt clear its fpending bit,
 *	  call the handler, then unmask the interrupt and doreti.
 *
 *	YYY can cache gd base pointer instead of using hidden %fs
 *	prefixes.
 */

#define	FAST_INTR(irq_num, vec_name, icu, enable_icus)			\
	.text ; 							\
	SUPERALIGN_TEXT ; 						\
IDTVEC(vec_name) ; 							\
	ICU_PUSH_FRAME ;						\
	FAKE_MCOUNT(TF_RIP(%rsp)) ; 					\
	MASK_IRQ(icu, irq_num) ;					\
	enable_icus ;							\
	movq	PCPU(curthread),%rbx ;					\
	testl	$-1,TD_NEST_COUNT(%rbx) ;				\
	jne	1f ;							\
	testl	$-1,TD_CRITCOUNT(%rbx) ;				\
	je	2f ;							\
1: ;									\
	/* set pending bit and return, leave interrupt masked */	\
	orl	$IRQ_LBIT(irq_num),PCPU(fpending) ;			\
	orl	$RQF_INTPEND, PCPU(reqflags) ;				\
	jmp	5f ;							\
2: ;									\
	/* clear pending bit, run handler */				\
	andl	$~IRQ_LBIT(irq_num),PCPU(fpending) ;			\
	pushq	$irq_num ;						\
	movq	%rsp,%rdi ;		/* rdi = call argument */	\
	incl	TD_CRITCOUNT(%rbx) ;					\
	sti ;								\
	call	ithread_fast_handler ;	/* returns 0 to unmask int */	\
	decl	TD_CRITCOUNT(%rbx) ;					\
	addq	$8,%rsp ;		/* intr frame -> trap frame */	\
	UNMASK_IRQ(icu, irq_num) ;					\
5: ;									\
	MEXITCOUNT ;							\
	jmp	doreti ;						\

/*
 * Unmask a slow interrupt.  This function is used by interrupt threads
 * after they have descheduled themselves to reenable interrupts and
 * possibly cause a reschedule to occur.
 */

#define INTR_UNMASK(irq_num, vec_name, icu)				\
	.text ;								\
	SUPERALIGN_TEXT ;						\
IDTVEC(vec_name) ;							\
	pushq	%rbp ;	 /* frame for ddb backtrace */			\
	movq	%rsp, %rbp ;						\
	subq	%rax, %rax ;						\
	UNMASK_IRQ(icu, irq_num) ;					\
	popq	%rbp ;							\
	ret ;								\

MCOUNT_LABEL(bintr)
	FAST_INTR(0,icu_fastintr0, IO_ICU1, ENABLE_ICU1)
	FAST_INTR(1,icu_fastintr1, IO_ICU1, ENABLE_ICU1)
	FAST_INTR(2,icu_fastintr2, IO_ICU1, ENABLE_ICU1)
	FAST_INTR(3,icu_fastintr3, IO_ICU1, ENABLE_ICU1)
	FAST_INTR(4,icu_fastintr4, IO_ICU1, ENABLE_ICU1)
	FAST_INTR(5,icu_fastintr5, IO_ICU1, ENABLE_ICU1)
	FAST_INTR(6,icu_fastintr6, IO_ICU1, ENABLE_ICU1)
	FAST_INTR(7,icu_fastintr7, IO_ICU1, ENABLE_ICU1)
	FAST_INTR(8,icu_fastintr8, IO_ICU2, ENABLE_ICU1_AND_2)
	FAST_INTR(9,icu_fastintr9, IO_ICU2, ENABLE_ICU1_AND_2)
	FAST_INTR(10,icu_fastintr10, IO_ICU2, ENABLE_ICU1_AND_2)
	FAST_INTR(11,icu_fastintr11, IO_ICU2, ENABLE_ICU1_AND_2)
	FAST_INTR(12,icu_fastintr12, IO_ICU2, ENABLE_ICU1_AND_2)
	FAST_INTR(13,icu_fastintr13, IO_ICU2, ENABLE_ICU1_AND_2)
	FAST_INTR(14,icu_fastintr14, IO_ICU2, ENABLE_ICU1_AND_2)
	FAST_INTR(15,icu_fastintr15, IO_ICU2, ENABLE_ICU1_AND_2)
MCOUNT_LABEL(eintr)

	.data

	.text
