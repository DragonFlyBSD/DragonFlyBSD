/*
 *	from: vector.s, 386BSD 0.1 unknown origin
 * $FreeBSD: src/sys/i386/isa/icu_vector.s,v 1.14.2.2 2000/07/18 21:12:42 dfr Exp $
 */
/*
 * WARNING!  SMP builds can use the ICU now so this code must be MP safe.
 */

#include "opt_auto_eoi.h"

#include <machine/asmacros.h>
#include <machine/lock.h>
#include <machine/psl.h>
#include <machine/trap.h>

#include <machine_base/icu/icu.h>
#include <bus/isa/isa.h>

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
#define PUSH_FRAME							\
	pushl	$0 ;		/* dummy error code */			\
	pushl	$0 ;		/* dummy trap type */			\
	pushl	$0 ;		/* dummy xflags */			\
	pushal ;		/* 8 registers */			\
	pushl	%ds ;							\
	pushl	%es ;							\
	pushl	%fs ;							\
	pushl	%gs ;							\
	cld ;								\
	mov	$KDSEL,%ax ; 						\
	mov	%ax,%ds ; 						\
	mov	%ax,%es ; 						\
	mov	%ax,%gs ; 						\
	mov	$KPSEL,%ax ;						\
	mov	%ax,%fs ;						\

#define PUSH_DUMMY							\
	pushfl ;		/* phys int frame / flags */		\
	pushl %cs ;		/* phys int frame / cs */		\
	pushl	12(%esp) ;	/* original caller eip */		\
	pushl	$0 ;		/* dummy error code */			\
	pushl	$0 ;		/* dummy trap type */			\
	pushl	$0 ;		/* dummy xflags */			\
	subl	$13*4,%esp ;	/* pushal + 4 seg regs (dummy) + CPL */	\

/*
 * Warning: POP_FRAME can only be used if there is no chance of a
 * segment register being changed (e.g. by procfs), which is why syscalls
 * have to use doreti.
 */
#define POP_FRAME							\
	popl	%gs ;							\
	popl	%fs ;							\
	popl	%es ;							\
	popl	%ds ;							\
	popal ;								\
	addl	$2*4,%esp ;	/* dummy trap & error codes */		\

#define POP_DUMMY							\
	addl	$19*4,%esp ;						\

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
 * Interrupt call handlers run in the following sequence:
 *
 *	- Push the trap frame required by doreti.
 *	- Mask the interrupt and reenable its source.
 *	- If we cannot take the interrupt set its ipending bit and
 *	  doreti.
 *	- If we can take the interrupt clear its ipending bit,
 *	  call the handler, then unmask the interrupt and doreti.
 *
 *	YYY can cache gd base pointer instead of using hidden %fs
 *	prefixes.
 */

#define	INTR_HANDLER(irq_num, icu, enable_icus)				\
	.text ; 							\
	SUPERALIGN_TEXT ; 						\
IDTVEC(icu_intr##irq_num) ; 						\
	PUSH_FRAME ;							\
	FAKE_MCOUNT(15*4(%esp)) ; 					\
	MASK_IRQ(icu, irq_num) ;					\
	enable_icus ;							\
	movl	PCPU(curthread),%ebx ;					\
	pushl	$0 ;			/* DUMMY CPL FOR DORETI */	\
	testl	$-1,TD_NEST_COUNT(%ebx) ;				\
	jne	1f ;							\
	testl	$-1,TD_CRITCOUNT(%ebx) ;				\
	je	2f ;							\
1: ;									\
	/* set pending bit and return, leave interrupt masked */	\
	movl	$0,%edx ;						\
	orl	$IRQ_LBIT(irq_num),PCPU_E4(ipending,%edx) ;		\
	orl	$RQF_INTPEND, PCPU(reqflags) ;				\
	jmp	5f ;							\
2: ;									\
	/* clear pending bit, run handler */				\
	movl	$0,%edx ;						\
	andl	$~IRQ_LBIT(irq_num),PCPU_E4(ipending,%edx) ;		\
	pushl	$irq_num ;						\
	pushl	%esp ;			/* pass frame by reference */	\
	incl	TD_CRITCOUNT(%ebx) ;					\
	sti ;								\
	call	ithread_fast_handler ;	/* returns 0 to unmask int */	\
	decl	TD_CRITCOUNT(%ebx) ;					\
	addl	$8,%esp ;						\
	UNMASK_IRQ(icu, irq_num) ;					\
5: ;									\
	MEXITCOUNT ;							\
	jmp	doreti ;						\

MCOUNT_LABEL(bintr)
	INTR_HANDLER(0, IO_ICU1, ENABLE_ICU1)
	INTR_HANDLER(1, IO_ICU1, ENABLE_ICU1)
	INTR_HANDLER(2, IO_ICU1, ENABLE_ICU1)
	INTR_HANDLER(3, IO_ICU1, ENABLE_ICU1)
	INTR_HANDLER(4, IO_ICU1, ENABLE_ICU1)
	INTR_HANDLER(5, IO_ICU1, ENABLE_ICU1)
	INTR_HANDLER(6, IO_ICU1, ENABLE_ICU1)
	INTR_HANDLER(7, IO_ICU1, ENABLE_ICU1)
	INTR_HANDLER(8, IO_ICU2, ENABLE_ICU1_AND_2)
	INTR_HANDLER(9, IO_ICU2, ENABLE_ICU1_AND_2)
	INTR_HANDLER(10, IO_ICU2, ENABLE_ICU1_AND_2)
	INTR_HANDLER(11, IO_ICU2, ENABLE_ICU1_AND_2)
	INTR_HANDLER(12, IO_ICU2, ENABLE_ICU1_AND_2)
	INTR_HANDLER(13, IO_ICU2, ENABLE_ICU1_AND_2)
	INTR_HANDLER(14, IO_ICU2, ENABLE_ICU1_AND_2)
	INTR_HANDLER(15, IO_ICU2, ENABLE_ICU1_AND_2)
MCOUNT_LABEL(eintr)

	.data

	.text
