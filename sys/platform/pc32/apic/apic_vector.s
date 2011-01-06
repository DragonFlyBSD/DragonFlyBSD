/*
 *	from: vector.s, 386BSD 0.1 unknown origin
 * $FreeBSD: src/sys/i386/isa/apic_vector.s,v 1.47.2.5 2001/09/01 22:33:38 tegge Exp $
 */

#include "opt_auto_eoi.h"

#include <machine/asmacros.h>
#include <machine/lock.h>
#include <machine/psl.h>
#include <machine/trap.h>

#include <machine_base/icu/icu.h>
#include <bus/isa/isa.h>

#include "assym.s"

#include "apicreg.h"
#include "apic_ipl.h"
#include <machine/smp.h>
#include <machine_base/isa/intr_machdep.h>

/* convert an absolute IRQ# into bitmask */
#define IRQ_LBIT(irq_num)	(1 << ((irq_num) & 0x1f))

/* convert an absolute IRQ# into ipending index */
#define IRQ_LIDX(irq_num)	((irq_num) >> 5)

#ifdef SMP
#define MPLOCKED     lock ;
#else
#define MPLOCKED
#endif

/*
 * Push an interrupt frame in a format acceptable to doreti, reload
 * the segment registers for the kernel.
 */
#define PUSH_FRAME							\
	pushl	$0 ;		/* dummy error code */			\
	pushl	$0 ;		/* dummy trap type */			\
	pushl	$0 ;		/* dummy xflags type */			\
	pushal ;							\
	pushl	%ds ;		/* save data and extra segments ... */	\
	pushl	%es ;							\
	pushl	%fs ;							\
	pushl	%gs ;							\
	cld ;								\
	mov	$KDSEL,%ax ;						\
	mov	%ax,%ds ;						\
	mov	%ax,%es ;						\
	mov	%ax,%gs ;						\
	mov	$KPSEL,%ax ;						\
	mov	%ax,%fs ;						\

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
	addl	$3*4,%esp ;	/* dummy xflags, trap & error codes */	\

#define IOAPICADDR(irq_num) \
	CNAME(int_to_apicintpin) + IOAPIC_IM_SIZE * (irq_num) + IOAPIC_IM_ADDR
#define REDIRIDX(irq_num) \
	CNAME(int_to_apicintpin) + IOAPIC_IM_SIZE * (irq_num) + IOAPIC_IM_ENTIDX
#define IOAPICFLAGS(irq_num) \
	CNAME(int_to_apicintpin) + IOAPIC_IM_SIZE * (irq_num) + IOAPIC_IM_FLAGS

#define MASK_IRQ(irq_num)						\
	APIC_IMASK_LOCK ;			/* into critical reg */	\
	testl	$IOAPIC_IM_FLAG_MASKED, IOAPICFLAGS(irq_num) ;		\
	jne	7f ;			/* masked, don't mask */	\
	orl	$IOAPIC_IM_FLAG_MASKED, IOAPICFLAGS(irq_num) ;		\
						/* set the mask bit */	\
	movl	IOAPICADDR(irq_num), %ecx ;	/* ioapic addr */	\
	movl	REDIRIDX(irq_num), %eax ;	/* get the index */	\
	movl	%eax, (%ecx) ;			/* write the index */	\
	orl	$IOART_INTMASK,IOAPIC_WINDOW(%ecx) ;/* set the mask */	\
7: ;						/* already masked */	\
	APIC_IMASK_UNLOCK ;						\

/*
 * Test to see whether we are handling an edge or level triggered INT.
 *  Level-triggered INTs must still be masked as we don't clear the source,
 *  and the EOI cycle would cause redundant INTs to occur.
 */
#define MASK_LEVEL_IRQ(irq_num)						\
	testl	$IOAPIC_IM_FLAG_LEVEL, IOAPICFLAGS(irq_num) ;		\
	jz	9f ;				/* edge, don't mask */	\
	MASK_IRQ(irq_num) ;						\
9: ;									\

/*
 * Test to see if the source is currntly masked, clear if so.
 */
#define UNMASK_IRQ(irq_num)						\
	cmpl	$0,%eax ;						\
	jnz	8f ;							\
	APIC_IMASK_LOCK ;			/* into critical reg */	\
	testl	$IOAPIC_IM_FLAG_MASKED, IOAPICFLAGS(irq_num) ;		\
	je	7f ;			/* bit clear, not masked */	\
	andl	$~IOAPIC_IM_FLAG_MASKED, IOAPICFLAGS(irq_num) ;		\
						/* clear mask bit */	\
	movl	IOAPICADDR(irq_num),%ecx ;	/* ioapic addr */	\
	movl	REDIRIDX(irq_num), %eax ;	/* get the index */	\
	movl	%eax,(%ecx) ;			/* write the index */	\
	andl	$~IOART_INTMASK,IOAPIC_WINDOW(%ecx) ;/* clear the mask */ \
7: ;									\
	APIC_IMASK_UNLOCK ;						\
8: ;									\

#ifdef SMP /* APIC-IO */

/*
 * Interrupt call handlers run in the following sequence:
 *
 *	- Push the trap frame required by doreti
 *	- Mask the interrupt and reenable its source
 *	- If we cannot take the interrupt set its ipending bit and
 *	  doreti.
 *	- If we can take the interrupt clear its ipending bit,
 *	  call the handler, then unmask and doreti.
 *
 * YYY can cache gd base opitner instead of using hidden %fs prefixes.
 */

#define	INTR_HANDLER(irq_num)						\
	.text ;								\
	SUPERALIGN_TEXT ;						\
IDTVEC(apic_intr##irq_num) ;						\
	PUSH_FRAME ;							\
	FAKE_MCOUNT(15*4(%esp)) ;					\
	MASK_LEVEL_IRQ(irq_num) ;					\
	movl	$0, lapic_eoi ;						\
	movl	PCPU(curthread),%ebx ;					\
	movl	$0,%eax ;	/* CURRENT CPL IN FRAME (REMOVED) */	\
	pushl	%eax ;							\
	testl	$-1,TD_NEST_COUNT(%ebx) ;				\
	jne	1f ;							\
	testl	$-1,TD_CRITCOUNT(%ebx) ;				\
	je	2f ;							\
1: ;									\
	/* in critical section, make interrupt pending */		\
	/* set the pending bit and return, leave interrupt masked */	\
	movl	$IRQ_LIDX(irq_num),%edx ;				\
	orl	$IRQ_LBIT(irq_num),PCPU_E4(ipending,%edx) ;		\
	orl	$RQF_INTPEND,PCPU(reqflags) ;				\
	jmp	5f ;							\
2: ;									\
	/* clear pending bit, run handler */				\
	movl	$IRQ_LIDX(irq_num),%edx ;				\
	andl	$~IRQ_LBIT(irq_num),PCPU_E4(ipending,%edx) ;		\
	pushl	$irq_num ;						\
	pushl	%esp ;			 /* pass frame by reference */	\
	incl	TD_CRITCOUNT(%ebx) ;					\
	sti ;								\
	call	ithread_fast_handler ;	 /* returns 0 to unmask */	\
	decl	TD_CRITCOUNT(%ebx) ;					\
	addl	$8, %esp ;						\
	UNMASK_IRQ(irq_num) ;						\
5: ;									\
	MEXITCOUNT ;							\
	jmp	doreti ;						\

#endif

/*
 * Handle "spurious INTerrupts".
 * Notes:
 *  This is different than the "spurious INTerrupt" generated by an
 *   8259 PIC for missing INTs.  See the APIC documentation for details.
 *  This routine should NOT do an 'EOI' cycle.
 */
	.text
	SUPERALIGN_TEXT
	.globl Xspuriousint
Xspuriousint:

	/* No EOI cycle used here */

	iret


/*
 * Handle TLB shootdowns.
 *
 * NOTE: Interrupts remain disabled.
 */
	.text
	SUPERALIGN_TEXT
	.globl	Xinvltlb
Xinvltlb:
	PUSH_FRAME
	movl	$0, lapic_eoi		/* End Of Interrupt to APIC */
	FAKE_MCOUNT(15*4(%esp))

	subl	$8,%esp			/* make same as interrupt frame */
	pushl	%esp			/* pass frame by reference */
	call	smp_invltlb_intr
	addl	$12,%esp

	MEXITCOUNT
	jmp	doreti_syscall_ret

/*
 * Executed by a CPU when it receives an Xcpustop IPI from another CPU,
 *
 *  - Signals its receipt.
 *  - Waits for permission to restart.
 *  - Processing pending IPIQ events while waiting.
 *  - Signals its restart.
 */

	.text
	SUPERALIGN_TEXT
	.globl Xcpustop
Xcpustop:
	pushl	%ebp
	movl	%esp, %ebp
	pushl	%eax
	pushl	%ecx
	pushl	%edx
	pushl	%ds			/* save current data segment */
	pushl	%fs

	movl	$KDSEL, %eax
	mov	%ax, %ds		/* use KERNEL data segment */
	movl	$KPSEL, %eax
	mov	%ax, %fs

	movl	$0, lapic_eoi		/* End Of Interrupt to APIC */

	movl	PCPU(cpuid), %eax
	imull	$PCB_SIZE, %eax
	leal	CNAME(stoppcbs)(%eax), %eax
	pushl	%eax
	call	CNAME(savectx)		/* Save process context */
	addl	$4, %esp
	
		
	movl	PCPU(cpuid), %eax

	/*
	 * Indicate that we have stopped and loop waiting for permission
	 * to start again.  We must still process IPI events while in a
	 * stopped state.
	 *
	 * Interrupts must remain enabled for non-IPI'd per-cpu interrupts
	 * (e.g. Xtimer, Xinvltlb).
	 */
	MPLOCKED
	btsl	%eax, stopped_cpus	/* stopped_cpus |= (1<<id) */
	sti
1:
	andl	$~RQF_IPIQ,PCPU(reqflags)
	pushl	%eax
	call	lwkt_smp_stopped
	popl	%eax
	btl	%eax, started_cpus	/* while (!(started_cpus & (1<<id))) */
	jnc	1b

	MPLOCKED
	btrl	%eax, started_cpus	/* started_cpus &= ~(1<<id) */
	MPLOCKED
	btrl	%eax, stopped_cpus	/* stopped_cpus &= ~(1<<id) */

	test	%eax, %eax
	jnz	2f

	movl	CNAME(cpustop_restartfunc), %eax
	test	%eax, %eax
	jz	2f
	movl	$0, CNAME(cpustop_restartfunc)	/* One-shot */

	call	*%eax
2:
	popl	%fs
	popl	%ds			/* restore previous data segment */
	popl	%edx
	popl	%ecx
	popl	%eax
	movl	%ebp, %esp
	popl	%ebp
	iret

	/*
	 * For now just have one ipiq IPI, but what we really want is
	 * to have one for each source cpu to the APICs don't get stalled
	 * backlogging the requests.
	 */
	.text
	SUPERALIGN_TEXT
	.globl Xipiq
Xipiq:
	PUSH_FRAME
	movl	$0, lapic_eoi		/* End Of Interrupt to APIC */
	FAKE_MCOUNT(15*4(%esp))

	incl    PCPU(cnt) + V_IPI
	movl	PCPU(curthread),%ebx
	testl	$-1,TD_CRITCOUNT(%ebx)
	jne	1f
	subl	$8,%esp			/* make same as interrupt frame */
	pushl	%esp			/* pass frame by reference */
	incl	PCPU(intr_nesting_level)
	incl	TD_CRITCOUNT(%ebx)
	sti
	call	lwkt_process_ipiq_frame
	decl	TD_CRITCOUNT(%ebx)
	decl	PCPU(intr_nesting_level)
	addl	$12,%esp
	pushl	$0			/* CPL for frame (REMOVED) */
	MEXITCOUNT
	jmp	doreti
1:
	orl	$RQF_IPIQ,PCPU(reqflags)
	MEXITCOUNT
	jmp	doreti_syscall_ret

	.text
	SUPERALIGN_TEXT
	.globl Xtimer
Xtimer:
	PUSH_FRAME
	movl	$0, lapic_eoi		/* End Of Interrupt to APIC */
	FAKE_MCOUNT(15*4(%esp))

	incl    PCPU(cnt) + V_TIMER
	movl	PCPU(curthread),%ebx
	testl	$-1,TD_CRITCOUNT(%ebx)
	jne	1f
	testl	$-1,TD_NEST_COUNT(%ebx)
	jne	1f
	subl	$8,%esp			/* make same as interrupt frame */
	pushl	%esp			/* pass frame by reference */
	incl	PCPU(intr_nesting_level)
	incl	TD_CRITCOUNT(%ebx)
	sti
	call	lapic_timer_process_frame
	decl	TD_CRITCOUNT(%ebx)
	decl	PCPU(intr_nesting_level)
	addl	$12,%esp
	pushl	$0			/* CPL for frame (REMOVED) */
	MEXITCOUNT
	jmp	doreti
1:
	orl	$RQF_TIMER,PCPU(reqflags)
	MEXITCOUNT
	jmp	doreti_syscall_ret

#ifdef SMP /* APIC-IO */

MCOUNT_LABEL(bintr)
	INTR_HANDLER(0)
	INTR_HANDLER(1)
	INTR_HANDLER(2)
	INTR_HANDLER(3)
	INTR_HANDLER(4)
	INTR_HANDLER(5)
	INTR_HANDLER(6)
	INTR_HANDLER(7)
	INTR_HANDLER(8)
	INTR_HANDLER(9)
	INTR_HANDLER(10)
	INTR_HANDLER(11)
	INTR_HANDLER(12)
	INTR_HANDLER(13)
	INTR_HANDLER(14)
	INTR_HANDLER(15)
	INTR_HANDLER(16)
	INTR_HANDLER(17)
	INTR_HANDLER(18)
	INTR_HANDLER(19)
	INTR_HANDLER(20)
	INTR_HANDLER(21)
	INTR_HANDLER(22)
	INTR_HANDLER(23)
	INTR_HANDLER(24)
	INTR_HANDLER(25)
	INTR_HANDLER(26)
	INTR_HANDLER(27)
	INTR_HANDLER(28)
	INTR_HANDLER(29)
	INTR_HANDLER(30)
	INTR_HANDLER(31)
MCOUNT_LABEL(eintr)

#endif

	.data

/* variables used by stop_cpus()/restart_cpus()/Xcpustop */
	.globl stopped_cpus, started_cpus
stopped_cpus:
	.long	0
started_cpus:
	.long	0

	.globl CNAME(cpustop_restartfunc)
CNAME(cpustop_restartfunc):
	.long 0
		
	.text

