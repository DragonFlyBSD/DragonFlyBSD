/*
 *	from: vector.s, 386BSD 0.1 unknown origin
 * $FreeBSD: src/sys/i386/isa/apic_vector.s,v 1.47.2.5 2001/09/01 22:33:38 tegge Exp $
 * $DragonFly: src/sys/platform/pc32/apic/apic_vector.s,v 1.39 2008/08/02 01:14:43 dillon Exp $
 */

#if 0
#include "use_npx.h"
#include "opt_auto_eoi.h"
#endif

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

/* convert an absolute IRQ# into a bitmask */
#define IRQ_LBIT(irq_num)	(1 << (irq_num))

/* make an index into the IO APIC from the IRQ# */
#define REDTBL_IDX(irq_num)	(0x10 + ((irq_num) * 2))

#ifdef SMP
#define MPLOCKED     lock ;
#else
#define MPLOCKED
#endif

#define APIC_PUSH_FRAME							\
	PUSH_FRAME ;		/* 15 regs + space for 5 extras */	\
	movq $0,TF_XFLAGS(%rsp) ;					\
	movq $0,TF_TRAPNO(%rsp) ;					\
	movq $0,TF_ADDR(%rsp) ;						\
	movq $0,TF_FLAGS(%rsp) ;					\
	movq $0,TF_ERR(%rsp) ;						\
	cld ;								\

/*
 * JG stale? Warning: POP_FRAME can only be used if there is no chance of a
 * segment register being changed (e.g. by procfs), which is why syscalls
 * have to use doreti.
 */
#define APIC_POP_FRAME POP_FRAME

/* sizeof(struct apic_intmapinfo) == 24 */
#define IOAPICADDR(irq_num) CNAME(int_to_apicintpin) + 24 * (irq_num) + 8
#define REDIRIDX(irq_num) CNAME(int_to_apicintpin) + 24 * (irq_num) + 16

#define MASK_IRQ(irq_num)						\
	APIC_IMASK_LOCK ;			/* into critical reg */	\
	testl	$IRQ_LBIT(irq_num), apic_imen ;				\
	jne	7f ;			/* masked, don't mask */	\
	orl	$IRQ_LBIT(irq_num), apic_imen ;	/* set the mask bit */	\
	movq	IOAPICADDR(irq_num), %rcx ;	/* ioapic addr */	\
	movl	REDIRIDX(irq_num), %eax ;	/* get the index */	\
	movl	%eax, (%rcx) ;			/* write the index */	\
	movl	IOAPIC_WINDOW(%rcx), %eax ;	/* current value */	\
	orl	$IOART_INTMASK, %eax ;		/* set the mask */	\
	movl	%eax, IOAPIC_WINDOW(%rcx) ;	/* new value */		\
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
	movq	IOAPICADDR(irq_num),%rcx ;	/* ioapic addr */	\
	movl	REDIRIDX(irq_num), %eax ;	/* get the index */	\
	movl	%eax,(%rcx) ;			/* write the index */	\
	movl	IOAPIC_WINDOW(%rcx),%eax ;	/* current value */	\
	andl	$~IOART_INTMASK,%eax ;		/* clear the mask */	\
	movl	%eax,IOAPIC_WINDOW(%rcx) ;	/* new value */		\
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
	APIC_PUSH_FRAME ;						\
	FAKE_MCOUNT(15*4(%esp)) ;					\
	MASK_LEVEL_IRQ(irq_num) ;					\
	movq	lapic, %rax ;						\
	movl	$0, LA_EOI(%rax) ;					\
	movq	PCPU(curthread),%rbx ;					\
	testl	$-1,TD_NEST_COUNT(%rbx) ;				\
	jne	1f ;							\
	cmpl	$TDPRI_CRIT,TD_PRI(%rbx) ;				\
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
	pushq	$irq_num ;		/* trapframe -> intrframe */	\
	movq	%rsp, %rdi ;		/* pass frame by reference */	\
	call	ithread_fast_handler ;	/* returns 0 to unmask */	\
	addq	$8, %rsp ;		/* intrframe -> trapframe */	\
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
	APIC_PUSH_FRAME ;							\
	maybe_extra_ipending ;						\
;									\
	MASK_LEVEL_IRQ(irq_num) ;					\
	incl	PCPU(cnt) + V_INTR ;					\
	movq	lapic, %rax ;						\
	movl	$0, LA_EOI(%rax) ;					\
	movq	PCPU(curthread),%rbx ;					\
	testl	$-1,TD_NEST_COUNT(%rbx) ;				\
	jne	1f ;							\
	cmpl	$TDPRI_CRIT,TD_PRI(%rbx) ;				\
	jl	2f ;							\
1: ;									\
	/* set the pending bit and return, leave the interrupt masked */ \
	orl	$IRQ_LBIT(irq_num), PCPU(ipending) ;			\
	orl	$RQF_INTPEND,PCPU(reqflags) ;				\
	jmp	5f ;							\
2: ;									\
	/* set running bit, clear pending bit, run handler */		\
	andl	$~IRQ_LBIT(irq_num), PCPU(ipending) ;			\
	incl	TD_NEST_COUNT(%rbx) ;					\
	sti ;								\
	movq	$irq_num,%rdi ;					\
	call	sched_ithd ;						\
	cli ;								\
	decl	TD_NEST_COUNT(%rbx) ;					\
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
	APIC_PUSH_FRAME ;						\
	movq	lapic,%rax ;						\
	movl	$0,LA_EOI(%rax) ;	/* End Of Interrupt to APIC */	\
	/*pushl	$irq_num ;*/						\
	/*call	do_wrongintr ;*/					\
	/*addl	$4,%esp ;*/						\
	APIC_POP_FRAME ;						\
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
	pushq	%rax

	movq	%cr3, %rax		/* invalidate the TLB */
	movq	%rax, %cr3

	movq	lapic, %rax
	movl	$0, LA_EOI(%rax)	/* End Of Interrupt to APIC */

	popq	%rax
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
	pushq	%rbp
	movq	%rsp, %rbp
	/* We save registers that are not preserved across function calls. */
	/* JG can be re-written with mov's */
	pushq	%rax
	pushq	%rcx
	pushq	%rdx
	pushq	%rsi
	pushq	%rdi
	pushq	%r8
	pushq	%r9
	pushq	%r10
	pushq	%r11

#if JG
	/* JGXXX switch to kernel %gs? */
	pushl	%ds			/* save current data segment */
	pushl	%fs

	movl	$KDSEL, %eax
	mov	%ax, %ds		/* use KERNEL data segment */
	movl	$KPSEL, %eax
	mov	%ax, %fs
#endif

	movq	lapic, %rax
	movl	$0, LA_EOI(%rax)	/* End Of Interrupt to APIC */

	/* JG */
	movl	PCPU(cpuid), %eax
	imull	$PCB_SIZE, %eax
	leaq	CNAME(stoppcbs), %rdi
	addq	%rax, %rdi
	call	CNAME(savectx)		/* Save process context */
	
		
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
	pushq	%rax
	call	lwkt_smp_stopped
	popq	%rax
	btl	%eax, started_cpus	/* while (!(started_cpus & (1<<id))) */
	jnc	1b

	MPLOCKED
	btrl	%eax, started_cpus	/* started_cpus &= ~(1<<id) */
	MPLOCKED
	btrl	%eax, stopped_cpus	/* stopped_cpus &= ~(1<<id) */

	test	%eax, %eax
	jnz	2f

	movq	CNAME(cpustop_restartfunc), %rax
	test	%rax, %rax
	jz	2f
	movq	$0, CNAME(cpustop_restartfunc)	/* One-shot */

	call	*%rax
2:
	popq	%r11
	popq	%r10
	popq	%r9
	popq	%r8
	popq	%rdi
	popq	%rsi
	popq	%rdx
	popq	%rcx
	popq	%rax

#if JG
	popl	%fs
	popl	%ds			/* restore previous data segment */
#endif
	movq	%rbp, %rsp
	popq	%rbp
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
	APIC_PUSH_FRAME
	movq	lapic, %rax
	movl	$0, LA_EOI(%rax)	/* End Of Interrupt to APIC */
	FAKE_MCOUNT(15*4(%esp))

	incl    PCPU(cnt) + V_IPI
	movq	PCPU(curthread),%rbx
	cmpl	$TDPRI_CRIT,TD_PRI(%rbx)
	jge	1f
	subq	$8,%rsp			/* make same as interrupt frame */
	movq	%rsp,%rdi		/* pass frame by reference */
	incl	PCPU(intr_nesting_level)
	addl	$TDPRI_CRIT,TD_PRI(%rbx)
	call	lwkt_process_ipiq_frame
	subl	$TDPRI_CRIT,TD_PRI(%rbx)
	decl	PCPU(intr_nesting_level)
	addq	$8,%rsp			/* turn into trapframe */
	MEXITCOUNT
	jmp	doreti
1:
	orl	$RQF_IPIQ,PCPU(reqflags)
	MEXITCOUNT
	APIC_POP_FRAME
	iret

	.text
	SUPERALIGN_TEXT
	.globl Xtimer
Xtimer:
	APIC_PUSH_FRAME
	movq	lapic, %rax
	movl	$0, LA_EOI(%rax)	/* End Of Interrupt to APIC */
	FAKE_MCOUNT(15*4(%esp))

	incl    PCPU(cnt) + V_TIMER
	movq	PCPU(curthread),%rbx
	cmpl	$TDPRI_CRIT,TD_PRI(%rbx)
	jge	1f
	testl	$-1,TD_NEST_COUNT(%rbx)
	jne	1f
	subq	$8,%rsp			/* make same as interrupt frame */
	movq	%rsp,%rdi		/* pass frame by reference */
	incl	PCPU(intr_nesting_level)
	addl	$TDPRI_CRIT,TD_PRI(%rbx)
	call	lapic_timer_process_frame
	subl	$TDPRI_CRIT,TD_PRI(%rbx)
	decl	PCPU(intr_nesting_level)
	addq	$8,%rsp			/* turn into trapframe */
	MEXITCOUNT
	jmp	doreti
1:
	orl	$RQF_TIMER,PCPU(reqflags)
	MEXITCOUNT
	APIC_POP_FRAME
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
	.quad 0
		
	.globl	apic_pin_trigger
apic_pin_trigger:
	.long	0

	.text

