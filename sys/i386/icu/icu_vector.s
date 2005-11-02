/*
 *	from: vector.s, 386BSD 0.1 unknown origin
 * $FreeBSD: src/sys/i386/isa/icu_vector.s,v 1.14.2.2 2000/07/18 21:12:42 dfr Exp $
 * $DragonFly: src/sys/i386/icu/Attic/icu_vector.s,v 1.21 2005/11/02 20:23:16 dillon Exp $
 */

#include "use_npx.h"
#include "opt_auto_eoi.h"

#include <machine/asmacros.h>
#include <machine/ipl.h>
#include <machine/lock.h>
#include <machine/psl.h>
#include <machine/trap.h>
#include <machine/smptests.h>           /** various SMP options */

#include <i386/icu/icu.h>
#include <bus/isa/i386/isa.h>

#include "assym.s"

#ifndef APIC_IO

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
#define PUSH_FRAME							\
	pushl	$0 ;		/* dummy error code */			\
	pushl	$0 ;		/* dummy trap type */			\
	pushal ;		/* 8 registers */			\
	pushl	%ds ;							\
	pushl	%es ;							\
	pushl	%fs ;							\
	mov	$KDSEL,%ax ; 						\
	mov	%ax,%ds ; 						\
	mov	%ax,%es ; 						\
	mov	$KPSEL,%ax ;						\
	mov	%ax,%fs ;						\

#define PUSH_DUMMY							\
	pushfl ;		/* phys int frame / flags */		\
	pushl %cs ;		/* phys int frame / cs */		\
	pushl	12(%esp) ;	/* original caller eip */		\
	pushl	$0 ;		/* dummy error code */			\
	pushl	$0 ;		/* dummy trap type */			\
	subl	$12*4,%esp ;	/* pushal + 3 seg regs (dummy) + CPL */	\

/*
 * Warning: POP_FRAME can only be used if there is no chance of a
 * segment register being changed (e.g. by procfs), which is why syscalls
 * have to use doreti.
 */
#define POP_FRAME							\
	popl	%fs ;							\
	popl	%es ;							\
	popl	%ds ;							\
	popal ;								\
	addl	$2*4,%esp ;	/* dummy trap & error codes */		\

#define POP_DUMMY							\
	addl	$17*4,%esp ;						\

#define MASK_IRQ(icu, irq_num)						\
	movb	icu_imen + IRQ_BYTE(irq_num),%al ;			\
	orb	$IRQ_BIT(irq_num),%al ;					\
	movb	%al,icu_imen + IRQ_BYTE(irq_num) ;			\
	outb	%al,$icu+ICU_IMR_OFFSET ;				\

#define UNMASK_IRQ(icu, irq_num)					\
	cmpl	$0,%eax ;						\
	jnz	8f ;							\
	movb	icu_imen + IRQ_BYTE(irq_num),%al ;			\
	andb	$~IRQ_BIT(irq_num),%al ;				\
	movb	%al,icu_imen + IRQ_BYTE(irq_num) ;			\
	outb	%al,$icu+ICU_IMR_OFFSET ;				\
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

#define	FAST_INTR(irq_num, vec_name, icu, enable_icus, maybe_extra_ipending) \
	.text ; 							\
	SUPERALIGN_TEXT ; 						\
IDTVEC(vec_name) ; 							\
	PUSH_FRAME ;							\
	FAKE_MCOUNT(13*4(%esp)) ; 					\
	maybe_extra_ipending ;						\
	MASK_IRQ(icu, irq_num) ;					\
	enable_icus ;							\
	movl	PCPU(curthread),%ebx ;					\
	pushl	$0 ;			/* DUMMY CPL FOR DORETI */	\
	cmpl	$TDPRI_CRIT,TD_PRI(%ebx) ;				\
	jl	2f ;							\
1: ;									\
	/* set pending bit and return, leave interrupt masked */	\
	orl	$IRQ_LBIT(irq_num),PCPU(fpending) ;			\
	orl	$RQF_INTPEND, PCPU(reqflags) ;				\
	jmp	5f ;							\
2: ;									\
	/* clear pending bit, run handler */				\
	andl	$~IRQ_LBIT(irq_num),PCPU(fpending) ;			\
	pushl	$irq_num ;						\
	call	ithread_fast_handler ;	/* returns 0 to unmask int */	\
	addl	$4,%esp ;						\
	UNMASK_IRQ(icu, irq_num) ;					\
5: ;									\
	MEXITCOUNT ;							\
	jmp	doreti ;						\

/*
 * Restart fast interrupt held up by critical section.
 *
 *	- Push a dummy trap frame as required by doreti.
 *	- The interrupt source is already masked.
 *	- Clear the fpending bit
 *	- Run the handler
 *	- Unmask the interrupt
 *	- Pop the dummy frame and do a normal return
 *
 *	YYY can cache gd base pointer instead of using hidden %fs
 *	prefixes.
 */
#define FAST_UNPEND(irq_num, vec_name, icu)				\
	.text ;								\
	SUPERALIGN_TEXT ;						\
IDTVEC(vec_name) ;							\
	pushl	%ebp ;							\
	movl	%esp,%ebp ;						\
	PUSH_DUMMY ;							\
	pushl	$irq_num ;						\
	call	ithread_fast_handler ;	/* returns 0 to unmask int */	\
	addl	$4, %esp ;						\
	UNMASK_IRQ(icu, irq_num) ;					\
	POP_DUMMY ;							\
	popl %ebp ;							\
	ret ;								\

/*
 * Slow interrupt call handlers run in the following sequence:
 *
 *	- Push the trap frame required by doreti.
 *	- Mask the interrupt and reenable its source.
 *	- If we cannot take the interrupt set its ipending bit and
 *	  doreti.  In addition to checking for a critical section
 *	  and cpl mask we also check to see if the thread is still
 *	  running.
 *	- If we can take the interrupt clear its ipending bit
 *	  and schedule its thread.  Leave interrupts masked and doreti.
 *
 *	sched_ithd() is called with interrupts enabled and outside of a
 *	critical section (so it can preempt us).
 *
 *	YYY sched_ithd may preempt us synchronously (fix interrupt stacking)
 *
 *	Note that intr_nesting_level is not bumped during sched_ithd because
 *	blocking allocations are allowed in the preemption case.
 *
 *	YYY can cache gd base pointer instead of using hidden %fs
 *	prefixes.
 */

#define	INTR(irq_num, vec_name, icu, enable_icus, reg, maybe_extra_ipending) \
	.text ; 							\
	SUPERALIGN_TEXT ; 						\
IDTVEC(vec_name) ; 							\
	PUSH_FRAME ;							\
	FAKE_MCOUNT(13*4(%esp)) ;					\
	maybe_extra_ipending ; 						\
	MASK_IRQ(icu, irq_num) ;					\
	enable_icus ;							\
	movl	PCPU(curthread),%ebx ;					\
	pushl	$0 ;			/* DUMMY CPL FOR DORETI */	\
	cmpl	$TDPRI_CRIT,TD_PRI(%ebx) ;				\
	jl	2f ;							\
1: ;									\
	/* set the pending bit and return, leave interrupt masked */	\
	orl	$IRQ_LBIT(irq_num), PCPU(ipending) ;			\
	orl	$RQF_INTPEND, PCPU(reqflags) ;				\
	jmp	5f ;							\
2: ;									\
	/* set running bit, clear pending bit, run handler */		\
	andl	$~IRQ_LBIT(irq_num), PCPU(ipending) ;			\
	sti ;								\
	pushl	$irq_num ;						\
	call	sched_ithd ;						\
	addl	$4,%esp ;						\
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
	pushl %ebp ;	 /* frame for ddb backtrace */			\
	movl	%esp, %ebp ;						\
	subl	%eax, %eax ;						\
	UNMASK_IRQ(icu, irq_num) ;					\
	popl %ebp ;							\
	ret ;								\

MCOUNT_LABEL(bintr)
	FAST_INTR(0,icu_fastintr0, IO_ICU1, ENABLE_ICU1,)
	FAST_INTR(1,icu_fastintr1, IO_ICU1, ENABLE_ICU1,)
	FAST_INTR(2,icu_fastintr2, IO_ICU1, ENABLE_ICU1,)
	FAST_INTR(3,icu_fastintr3, IO_ICU1, ENABLE_ICU1,)
	FAST_INTR(4,icu_fastintr4, IO_ICU1, ENABLE_ICU1,)
	FAST_INTR(5,icu_fastintr5, IO_ICU1, ENABLE_ICU1,)
	FAST_INTR(6,icu_fastintr6, IO_ICU1, ENABLE_ICU1,)
	FAST_INTR(7,icu_fastintr7, IO_ICU1, ENABLE_ICU1,)
	FAST_INTR(8,icu_fastintr8, IO_ICU2, ENABLE_ICU1_AND_2,)
	FAST_INTR(9,icu_fastintr9, IO_ICU2, ENABLE_ICU1_AND_2,)
	FAST_INTR(10,icu_fastintr10, IO_ICU2, ENABLE_ICU1_AND_2,)
	FAST_INTR(11,icu_fastintr11, IO_ICU2, ENABLE_ICU1_AND_2,)
	FAST_INTR(12,icu_fastintr12, IO_ICU2, ENABLE_ICU1_AND_2,)
	FAST_INTR(13,icu_fastintr13, IO_ICU2, ENABLE_ICU1_AND_2,)
	FAST_INTR(14,icu_fastintr14, IO_ICU2, ENABLE_ICU1_AND_2,)
	FAST_INTR(15,icu_fastintr15, IO_ICU2, ENABLE_ICU1_AND_2,)

	INTR(0,icu_slowintr0, IO_ICU1, ENABLE_ICU1, al,)
	INTR(1,icu_slowintr1, IO_ICU1, ENABLE_ICU1, al,)
	INTR(2,icu_slowintr2, IO_ICU1, ENABLE_ICU1, al,)
	INTR(3,icu_slowintr3, IO_ICU1, ENABLE_ICU1, al,)
	INTR(4,icu_slowintr4, IO_ICU1, ENABLE_ICU1, al,)
	INTR(5,icu_slowintr5, IO_ICU1, ENABLE_ICU1, al,)
	INTR(6,icu_slowintr6, IO_ICU1, ENABLE_ICU1, al,)
	INTR(7,icu_slowintr7, IO_ICU1, ENABLE_ICU1, al,)
	INTR(8,icu_slowintr8, IO_ICU2, ENABLE_ICU1_AND_2, ah,)
	INTR(9,icu_slowintr9, IO_ICU2, ENABLE_ICU1_AND_2, ah,)
	INTR(10,icu_slowintr10, IO_ICU2, ENABLE_ICU1_AND_2, ah,)
	INTR(11,icu_slowintr11, IO_ICU2, ENABLE_ICU1_AND_2, ah,)
	INTR(12,icu_slowintr12, IO_ICU2, ENABLE_ICU1_AND_2, ah,)
	INTR(13,icu_slowintr13, IO_ICU2, ENABLE_ICU1_AND_2, ah,)
	INTR(14,icu_slowintr14, IO_ICU2, ENABLE_ICU1_AND_2, ah,)
	INTR(15,icu_slowintr15, IO_ICU2, ENABLE_ICU1_AND_2, ah,)

	FAST_UNPEND(0,fastunpend0, IO_ICU1)
	FAST_UNPEND(1,fastunpend1, IO_ICU1)
	FAST_UNPEND(2,fastunpend2, IO_ICU1)
	FAST_UNPEND(3,fastunpend3, IO_ICU1)
	FAST_UNPEND(4,fastunpend4, IO_ICU1)
	FAST_UNPEND(5,fastunpend5, IO_ICU1)
	FAST_UNPEND(6,fastunpend6, IO_ICU1)
	FAST_UNPEND(7,fastunpend7, IO_ICU1)
	FAST_UNPEND(8,fastunpend8, IO_ICU2)
	FAST_UNPEND(9,fastunpend9, IO_ICU2)
	FAST_UNPEND(10,fastunpend10, IO_ICU2)
	FAST_UNPEND(11,fastunpend11, IO_ICU2)
	FAST_UNPEND(12,fastunpend12, IO_ICU2)
	FAST_UNPEND(13,fastunpend13, IO_ICU2)
	FAST_UNPEND(14,fastunpend14, IO_ICU2)
	FAST_UNPEND(15,fastunpend15, IO_ICU2)
MCOUNT_LABEL(eintr)

	.data

	.text

#endif
