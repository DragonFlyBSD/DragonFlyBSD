/*
 *	from: vector.s, 386BSD 0.1 unknown origin
 * $FreeBSD: src/sys/i386/isa/apic_vector.s,v 1.47.2.5 2001/09/01 22:33:38 tegge Exp $
 */

#if 0
#include "opt_auto_eoi.h"
#endif

#include <machine/asmacros.h>
#include <machine/lock.h>
#include <machine/psl.h>
#include <machine/trap.h>
#include <machine/segments.h>

#include <machine_base/icu/icu.h>
#include <bus/isa/isa.h>

#include "assym.s"

#include "apicreg.h"
#include "apic_ipl.h"
#include <machine/smp.h>
#include <machine_base/isa/intr_machdep.h>

#ifdef foo
/* convert an absolute IRQ# into bitmask */
#define IRQ_LBIT(irq_num)	(1UL << (irq_num & 0x3f))
#endif

#define IRQ_SBITS(irq_num)	((irq_num) & 0x3f)

/* convert an absolute IRQ# into gd_ipending index */
#define IRQ_LIDX(irq_num)	((irq_num) >> 6)

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
	movq	IOAPICADDR(irq_num), %rcx ;	/* ioapic addr */	\
	movl	REDIRIDX(irq_num), %eax ;	/* get the index */	\
	movl	%eax, (%rcx) ;			/* write the index */	\
	orl	$IOART_INTMASK,IOAPIC_WINDOW(%rcx) ;/* set the mask */	\
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
#define UNMASK_IRQ(irq_num)					\
	cmpl	$0,%eax ;						\
	jnz	8f ;							\
	APIC_IMASK_LOCK ;			/* into critical reg */	\
	testl	$IOAPIC_IM_FLAG_MASKED, IOAPICFLAGS(irq_num) ;		\
	je	7f ;			/* bit clear, not masked */	\
	andl	$~IOAPIC_IM_FLAG_MASKED, IOAPICFLAGS(irq_num) ;		\
						/* clear mask bit */	\
	movq	IOAPICADDR(irq_num),%rcx ;	/* ioapic addr */	\
	movl	REDIRIDX(irq_num), %eax ;	/* get the index */	\
	movl	%eax,(%rcx) ;			/* write the index */	\
	andl	$~IOART_INTMASK,IOAPIC_WINDOW(%rcx) ;/* clear the mask */ \
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
	APIC_PUSH_FRAME ;						\
	FAKE_MCOUNT(TF_RIP(%rsp)) ;					\
	MASK_LEVEL_IRQ(irq_num) ;					\
	movq	lapic, %rax ;						\
	movl	$0, LA_EOI(%rax) ;					\
	movq	PCPU(curthread),%rbx ;					\
	testl	$-1,TD_NEST_COUNT(%rbx) ;				\
	jne	1f ;							\
	testl	$-1,TD_CRITCOUNT(%rbx) ;				\
	je	2f ;							\
1: ;									\
	/* in critical section, make interrupt pending */		\
	/* set the pending bit and return, leave interrupt masked */	\
	movq	$1,%rcx ;						\
	shlq	$IRQ_SBITS(irq_num),%rcx ;				\
	movl	$IRQ_LIDX(irq_num),%edx ;				\
	orq	%rcx,PCPU_E8(ipending,%edx) ;				\
	orl	$RQF_INTPEND,PCPU(reqflags) ;				\
	jmp	5f ;							\
2: ;									\
	/* clear pending bit, run handler */				\
	movq	$1,%rcx ;						\
	shlq	$IRQ_SBITS(irq_num),%rcx ;				\
	notq	%rcx ;							\
	movl	$IRQ_LIDX(irq_num),%edx ;				\
	andq	%rcx,PCPU_E8(ipending,%edx) ;				\
	pushq	$irq_num ;		/* trapframe -> intrframe */	\
	movq	%rsp, %rdi ;		/* pass frame by reference */	\
	incl	TD_CRITCOUNT(%rbx) ;					\
	sti ;								\
	call	ithread_fast_handler ;	/* returns 0 to unmask */	\
	decl	TD_CRITCOUNT(%rbx) ;					\
	addq	$8, %rsp ;		/* intrframe -> trapframe */	\
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

	jmp	doreti_iret


/*
 * Handle TLB shootdowns.
 *
 * NOTE: interrupts are left disabled.
 */
	.text
	SUPERALIGN_TEXT
	.globl	Xinvltlb
Xinvltlb:
	APIC_PUSH_FRAME
	movq	lapic, %rax
	movl	$0, LA_EOI(%rax)	/* End Of Interrupt to APIC */
	FAKE_MCOUNT(TF_RIP(%rsp))
	subq	$8,%rsp			/* make same as interrupt frame */
	movq	%rsp,%rdi		/* pass frame by reference */
	call	smp_invltlb_intr
	addq	$8,%rsp			/* turn into trapframe */
	MEXITCOUNT
	APIC_POP_FRAME
	jmp	doreti_iret

/*
 * Executed by a CPU when it receives an Xcpustop IPI from another CPU,
 *
 *  - We cannot call doreti
 *  - Signals its receipt.
 *  - Waits for permission to restart.
 *  - Processing pending IPIQ events while waiting.
 *  - Signals its restart.
 */

	.text
	SUPERALIGN_TEXT
	.globl Xcpustop
Xcpustop:
	APIC_PUSH_FRAME
	movq	lapic, %rax
	movl	$0, LA_EOI(%rax)	/* End Of Interrupt to APIC */

	movl	PCPU(cpuid), %eax
	imull	$PCB_SIZE, %eax
	leaq	CNAME(stoppcbs), %rdi
	addq	%rax, %rdi
	call	CNAME(savectx)		/* Save process context */

	movslq	PCPU(cpuid), %rax

	/*
	 * Indicate that we have stopped and loop waiting for permission
	 * to start again.  We must still process IPI events while in a
	 * stopped state.
	 *
	 * Interrupts must remain enabled for non-IPI'd per-cpu interrupts
	 * (e.g. Xtimer, Xinvltlb).
	 */
	MPLOCKED
	btsq	%rax, stopped_cpus	/* stopped_cpus |= (1<<id) */
	sti
1:
	andl	$~RQF_IPIQ,PCPU(reqflags)
	pushq	%rax
	call	lwkt_smp_stopped
	popq	%rax
	pause
	btq	%rax, started_cpus	/* while (!(started_cpus & (1<<id))) */
	jnc	1b

	MPLOCKED
	btrq	%rax, started_cpus	/* started_cpus &= ~(1<<id) */
	MPLOCKED
	btrq	%rax, stopped_cpus	/* stopped_cpus &= ~(1<<id) */

	test	%eax, %eax
	jnz	2f

	movq	CNAME(cpustop_restartfunc), %rax
	test	%rax, %rax
	jz	2f
	movq	$0, CNAME(cpustop_restartfunc)	/* One-shot */

	call	*%rax
2:
	MEXITCOUNT
	APIC_POP_FRAME
	jmp	doreti_iret

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
	FAKE_MCOUNT(TF_RIP(%rsp))

	incl    PCPU(cnt) + V_IPI
	movq	PCPU(curthread),%rbx
	testl	$-1,TD_CRITCOUNT(%rbx)
	jne	1f
	subq	$8,%rsp			/* make same as interrupt frame */
	movq	%rsp,%rdi		/* pass frame by reference */
	incl	PCPU(intr_nesting_level)
	incl	TD_CRITCOUNT(%rbx)
	sti
	call	lwkt_process_ipiq_frame
	decl	TD_CRITCOUNT(%rbx)
	decl	PCPU(intr_nesting_level)
	addq	$8,%rsp			/* turn into trapframe */
	MEXITCOUNT
	jmp	doreti
1:
	orl	$RQF_IPIQ,PCPU(reqflags)
	MEXITCOUNT
	APIC_POP_FRAME
	jmp	doreti_iret

	.text
	SUPERALIGN_TEXT
	.globl Xtimer
Xtimer:
	APIC_PUSH_FRAME
	movq	lapic, %rax
	movl	$0, LA_EOI(%rax)	/* End Of Interrupt to APIC */
	FAKE_MCOUNT(TF_RIP(%rsp))

	subq	$8,%rsp			/* make same as interrupt frame */
	movq	%rsp,%rdi		/* pass frame by reference */
	call	lapic_timer_always
	addq	$8,%rsp			/* turn into trapframe */

	incl    PCPU(cnt) + V_TIMER
	movq	PCPU(curthread),%rbx
	testl	$-1,TD_CRITCOUNT(%rbx)
	jne	1f
	testl	$-1,TD_NEST_COUNT(%rbx)
	jne	1f
	subq	$8,%rsp			/* make same as interrupt frame */
	movq	%rsp,%rdi		/* pass frame by reference */
	incl	PCPU(intr_nesting_level)
	incl	TD_CRITCOUNT(%rbx)
	sti
	call	lapic_timer_process_frame
	decl	TD_CRITCOUNT(%rbx)
	decl	PCPU(intr_nesting_level)
	addq	$8,%rsp			/* turn into trapframe */
	MEXITCOUNT
	jmp	doreti
1:
	orl	$RQF_TIMER,PCPU(reqflags)
	MEXITCOUNT
	APIC_POP_FRAME
	jmp	doreti_iret

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
MCOUNT_LABEL(eintr)

#endif

	.data

/* variables used by stop_cpus()/restart_cpus()/Xcpustop */
	.globl stopped_cpus, started_cpus
stopped_cpus:
	.quad	0
started_cpus:
	.quad	0

	.globl CNAME(cpustop_restartfunc)
CNAME(cpustop_restartfunc):
	.quad 0
		
	.text

