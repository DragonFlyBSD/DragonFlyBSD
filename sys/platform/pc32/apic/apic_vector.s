/*
 *	from: vector.s, 386BSD 0.1 unknown origin
 * $FreeBSD: src/sys/i386/isa/apic_vector.s,v 1.47.2.5 2001/09/01 22:33:38 tegge Exp $
 * $DragonFly: src/sys/platform/pc32/apic/apic_vector.s,v 1.31 2005/11/04 21:16:57 dillon Exp $
 */

#include "use_npx.h"
#include "opt_auto_eoi.h"

#include <machine/asmacros.h>
#include <machine/ipl.h>
#include <machine/lock.h>
#include <machine/psl.h>
#include <machine/trap.h>

#include <i386/icu/icu.h>
#include <bus/isa/i386/isa.h>

#include "assym.s"

#include "apicreg.h"
#include "apic_ipl.h"
#include <machine/smp.h>
#include "i386/isa/intr_machdep.h"

/* convert an absolute IRQ# into a bitmask */
#define IRQ_LBIT(irq_num)	(1 << (irq_num))

/* make an index into the IO APIC from the IRQ# */
#define REDTBL_IDX(irq_num)	(0x10 + ((irq_num) * 2))

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
	pushal ;							\
	pushl	%ds ;		/* save data and extra segments ... */	\
	pushl	%es ;							\
	pushl	%fs ;							\
	mov	$KDSEL,%ax ;						\
	mov	%ax,%ds ;						\
	mov	%ax,%es ;						\
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

#define IOAPICADDR(irq_num) CNAME(int_to_apicintpin) + 16 * (irq_num) + 8
#define REDIRIDX(irq_num) CNAME(int_to_apicintpin) + 16 * (irq_num) + 12

#define MASK_IRQ(irq_num)						\
	APIC_IMASK_LOCK ;			/* into critical reg */	\
	testl	$IRQ_LBIT(irq_num), apic_imen ;				\
	jne	7f ;			/* masked, don't mask */	\
	orl	$IRQ_LBIT(irq_num), apic_imen ;	/* set the mask bit */	\
	movl	IOAPICADDR(irq_num), %ecx ;	/* ioapic addr */	\
	movl	REDIRIDX(irq_num), %eax ;	/* get the index */	\
	movl	%eax, (%ecx) ;			/* write the index */	\
	movl	IOAPIC_WINDOW(%ecx), %eax ;	/* current value */	\
	orl	$IOART_INTMASK, %eax ;		/* set the mask */	\
	movl	%eax, IOAPIC_WINDOW(%ecx) ;	/* new value */		\
7: ;						/* already masked */	\
	APIC_IMASK_UNLOCK ;						\

/*
 * Test to see whether we are handling an edge or level triggered INT.
 *  Level-triggered INTs must still be masked as we don't clear the source,
 *  and the EOI cycle would cause redundant INTs to occur.
 */
#define MASK_LEVEL_IRQ(irq_num)						\
	testl	$IRQ_LBIT(irq_num), apic_pin_trigger ;			\
	jz	9f ;				/* edge, don't mask */	\
	MASK_IRQ(irq_num) ;						\
9: ;									\

/*
 * Test to see if the source is currntly masked, clear if so.
 */
#define UNMASK_IRQ(irq_num)					\
	cmpl	$0,%eax ;						\
	jnz	8f ;							\
	APIC_IMASK_LOCK ;			/* into critical reg */	\
	testl	$IRQ_LBIT(irq_num), apic_imen ;				\
	je	7f ;			/* bit clear, not masked */	\
	andl	$~IRQ_LBIT(irq_num), apic_imen ;/* clear mask bit */	\
	movl	IOAPICADDR(irq_num),%ecx ;	/* ioapic addr */	\
	movl	REDIRIDX(irq_num), %eax ;	/* get the index */	\
	movl	%eax,(%ecx) ;			/* write the index */	\
	movl	IOAPIC_WINDOW(%ecx),%eax ;	/* current value */	\
	andl	$~IOART_INTMASK,%eax ;		/* clear the mask */	\
	movl	%eax,IOAPIC_WINDOW(%ecx) ;	/* new value */		\
7: ;									\
	APIC_IMASK_UNLOCK ;						\
8: ;									\

#ifdef APIC_IO

/*
 * Fast interrupt call handlers run in the following sequence:
 *
 *	- Push the trap frame required by doreti
 *	- Mask the interrupt and reenable its source
 *	- If we cannot take the interrupt set its fpending bit and
 *	  doreti.  Note that we cannot mess with mp_lock at all
 *	  if we entered from a critical section!
 *	- If we can take the interrupt clear its fpending bit,
 *	  call the handler, then unmask and doreti.
 *
 * YYY can cache gd base opitner instead of using hidden %fs prefixes.
 */

#define	FAST_INTR(irq_num, vec_name)					\
	.text ;								\
	SUPERALIGN_TEXT ;						\
IDTVEC(vec_name) ;							\
	PUSH_FRAME ;							\
	FAKE_MCOUNT(13*4(%esp)) ;					\
	MASK_LEVEL_IRQ(irq_num) ;					\
	movl	$0, lapic_eoi ;						\
	movl	PCPU(curthread),%ebx ;					\
	movl	$0,%eax ;	/* CURRENT CPL IN FRAME (REMOVED) */	\
	pushl	%eax ;							\
	cmpl	$TDPRI_CRIT,TD_PRI(%ebx) ;				\
	jl	2f ;							\
1: ;									\
	/* in critical section, make interrupt pending */		\
	/* set the pending bit and return, leave interrupt masked */	\
	orl	$IRQ_LBIT(irq_num),PCPU(fpending) ;			\
	orl	$RQF_INTPEND,PCPU(reqflags) ;				\
	jmp	5f ;							\
2: ;									\
	/* clear pending bit, run handler */				\
	andl	$~IRQ_LBIT(irq_num),PCPU(fpending) ;			\
	pushl	$irq_num ;						\
	call	ithread_fast_handler ;	 /* returns 0 to unmask */	\
	addl	$4, %esp ;						\
	UNMASK_IRQ(irq_num) ;						\
5: ;									\
	MEXITCOUNT ;							\
	jmp	doreti ;						\

/*
 * Slow interrupt call handlers run in the following sequence:
 *
 *	- Push the trap frame required by doreti.
 *	- Mask the interrupt and reenable its source.
 *	- If we cannot take the interrupt set its ipending bit and
 *	  doreti.  In addition to checking for a critical section
 *	  and cpl mask we also check to see if the thread is still
 *	  running.  Note that we cannot mess with mp_lock at all
 *	  if we entered from a critical section!
 *	- If we can take the interrupt clear its ipending bit
 *	  and schedule the thread.  Leave interrupts masked and doreti.
 *
 *	Note that calls to sched_ithd() are made with interrupts enabled
 *	and outside a critical section.  YYY sched_ithd may preempt us
 *	synchronously (fix interrupt stacking).
 *
 *	YYY can cache gd base pointer instead of using hidden %fs
 *	prefixes.
 */

#define SLOW_INTR(irq_num, vec_name, maybe_extra_ipending)		\
	.text ;								\
	SUPERALIGN_TEXT ;						\
IDTVEC(vec_name) ;							\
	PUSH_FRAME ;							\
	maybe_extra_ipending ;						\
;									\
	MASK_LEVEL_IRQ(irq_num) ;					\
	incl	PCPU(cnt) + V_INTR ;					\
	movl	$0, lapic_eoi ;						\
	movl	PCPU(curthread),%ebx ;					\
	movl	$0,%eax ;	/* CURRENT CPL IN FRAME (REMOVED) */	\
	pushl	%eax ;		/* cpl do restore */			\
	cmpl	$TDPRI_CRIT,TD_PRI(%ebx) ;				\
	jl	2f ;							\
1: ;									\
	/* set the pending bit and return, leave the interrupt masked */ \
	orl	$IRQ_LBIT(irq_num), PCPU(ipending) ;			\
	orl	$RQF_INTPEND,PCPU(reqflags) ;				\
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
 * Wrong interrupt call handlers.  We program these into APIC vectors
 * that should otherwise never occur.  For example, we program the SLOW
 * vector for irq N with this when we program the FAST vector with the
 * real interrupt.
 *
 * XXX for now all we can do is EOI it.  We can't call do_wrongintr
 * (yet) because we could be in a critical section.
 */
#define WRONGINTR(irq_num,vec_name)					\
	.text ;								\
	SUPERALIGN_TEXT	 ;						\
IDTVEC(vec_name) ;							\
	PUSH_FRAME ;							\
	movl	$0, lapic_eoi ;	/* End Of Interrupt to APIC */		\
	/*pushl	$irq_num ;*/						\
	/*call	do_wrongintr ;*/					\
	/*addl	$4,%esp ;*/						\
	POP_FRAME ;							\
	iret  ;								\

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
 */
	.text
	SUPERALIGN_TEXT
	.globl	Xinvltlb
Xinvltlb:
	pushl	%eax

	movl	%cr3, %eax		/* invalidate the TLB */
	movl	%eax, %cr3

	ss				/* stack segment, avoid %ds load */
	movl	$0, lapic_eoi		/* End Of Interrupt to APIC */

	popl	%eax
	iret


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
	 */
	MPLOCKED
	btsl	%eax, stopped_cpus	/* stopped_cpus |= (1<<id) */
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
	FAKE_MCOUNT(13*4(%esp))

	movl	PCPU(curthread),%ebx
	cmpl	$TDPRI_CRIT,TD_PRI(%ebx)
	jge	1f
	subl	$8,%esp			/* make same as interrupt frame */
	incl	PCPU(intr_nesting_level)
	addl	$TDPRI_CRIT,TD_PRI(%ebx)
	call	lwkt_process_ipiq_frame
	subl	$TDPRI_CRIT,TD_PRI(%ebx)
	decl	PCPU(intr_nesting_level)
	addl	$8,%esp
	pushl	$0			/* CPL for frame (REMOVED) */
	MEXITCOUNT
	jmp	doreti
1:
	orl	$RQF_IPIQ,PCPU(reqflags)
	MEXITCOUNT
	POP_FRAME
	iret

#ifdef APIC_IO

MCOUNT_LABEL(bintr)
	FAST_INTR(0,apic_fastintr0)
	FAST_INTR(1,apic_fastintr1)
	FAST_INTR(2,apic_fastintr2)
	FAST_INTR(3,apic_fastintr3)
	FAST_INTR(4,apic_fastintr4)
	FAST_INTR(5,apic_fastintr5)
	FAST_INTR(6,apic_fastintr6)
	FAST_INTR(7,apic_fastintr7)
	FAST_INTR(8,apic_fastintr8)
	FAST_INTR(9,apic_fastintr9)
	FAST_INTR(10,apic_fastintr10)
	FAST_INTR(11,apic_fastintr11)
	FAST_INTR(12,apic_fastintr12)
	FAST_INTR(13,apic_fastintr13)
	FAST_INTR(14,apic_fastintr14)
	FAST_INTR(15,apic_fastintr15)
	FAST_INTR(16,apic_fastintr16)
	FAST_INTR(17,apic_fastintr17)
	FAST_INTR(18,apic_fastintr18)
	FAST_INTR(19,apic_fastintr19)
	FAST_INTR(20,apic_fastintr20)
	FAST_INTR(21,apic_fastintr21)
	FAST_INTR(22,apic_fastintr22)
	FAST_INTR(23,apic_fastintr23)
	
	/* YYY what is this garbage? */

	SLOW_INTR(0,apic_slowintr0,)
	SLOW_INTR(1,apic_slowintr1,)
	SLOW_INTR(2,apic_slowintr2,)
	SLOW_INTR(3,apic_slowintr3,)
	SLOW_INTR(4,apic_slowintr4,)
	SLOW_INTR(5,apic_slowintr5,)
	SLOW_INTR(6,apic_slowintr6,)
	SLOW_INTR(7,apic_slowintr7,)
	SLOW_INTR(8,apic_slowintr8,)
	SLOW_INTR(9,apic_slowintr9,)
	SLOW_INTR(10,apic_slowintr10,)
	SLOW_INTR(11,apic_slowintr11,)
	SLOW_INTR(12,apic_slowintr12,)
	SLOW_INTR(13,apic_slowintr13,)
	SLOW_INTR(14,apic_slowintr14,)
	SLOW_INTR(15,apic_slowintr15,)
	SLOW_INTR(16,apic_slowintr16,)
	SLOW_INTR(17,apic_slowintr17,)
	SLOW_INTR(18,apic_slowintr18,)
	SLOW_INTR(19,apic_slowintr19,)
	SLOW_INTR(20,apic_slowintr20,)
	SLOW_INTR(21,apic_slowintr21,)
	SLOW_INTR(22,apic_slowintr22,)
	SLOW_INTR(23,apic_slowintr23,)

	WRONGINTR(0,apic_wrongintr0)
	WRONGINTR(1,apic_wrongintr1)
	WRONGINTR(2,apic_wrongintr2)
	WRONGINTR(3,apic_wrongintr3)
	WRONGINTR(4,apic_wrongintr4)
	WRONGINTR(5,apic_wrongintr5)
	WRONGINTR(6,apic_wrongintr6)
	WRONGINTR(7,apic_wrongintr7)
	WRONGINTR(8,apic_wrongintr8)
	WRONGINTR(9,apic_wrongintr9)
	WRONGINTR(10,apic_wrongintr10)
	WRONGINTR(11,apic_wrongintr11)
	WRONGINTR(12,apic_wrongintr12)
	WRONGINTR(13,apic_wrongintr13)
	WRONGINTR(14,apic_wrongintr14)
	WRONGINTR(15,apic_wrongintr15)
	WRONGINTR(16,apic_wrongintr16)
	WRONGINTR(17,apic_wrongintr17)
	WRONGINTR(18,apic_wrongintr18)
	WRONGINTR(19,apic_wrongintr19)
	WRONGINTR(20,apic_wrongintr20)
	WRONGINTR(21,apic_wrongintr21)
	WRONGINTR(22,apic_wrongintr22)
	WRONGINTR(23,apic_wrongintr23)
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
		
	.globl	apic_pin_trigger
apic_pin_trigger:
	.long	0

	.text

