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
#include <machine_base/apic/ioapic_ipl.h>
#include <machine/intr_machdep.h>

#ifdef foo
/* convert an absolute IRQ# into bitmask */
#define IRQ_LBIT(irq_num)	(1UL << (irq_num & 0x3f))
#endif

#define IRQ_SBITS(irq_num)	((irq_num) & 0x3f)

/* convert an absolute IRQ# into gd_ipending index */
#define IRQ_LIDX(irq_num)	((irq_num) >> 6)

#define MPLOCKED     lock ;

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
#define APIC_POP_FRAME							\
	POP_FRAME ;							\

#define IOAPICADDR(irq_num) \
	CNAME(ioapic_irqs) + IOAPIC_IRQI_SIZE * (irq_num) + IOAPIC_IRQI_ADDR
#define REDIRIDX(irq_num) \
	CNAME(ioapic_irqs) + IOAPIC_IRQI_SIZE * (irq_num) + IOAPIC_IRQI_IDX
#define IOAPICFLAGS(irq_num) \
	CNAME(ioapic_irqs) + IOAPIC_IRQI_SIZE * (irq_num) + IOAPIC_IRQI_FLAGS
 
#define MASK_IRQ(irq_num)						\
	IOAPIC_IMASK_LOCK ;			/* into critical reg */	\
	testl	$IOAPIC_IRQI_FLAG_MASKED, IOAPICFLAGS(irq_num) ;	\
	jne	7f ;			/* masked, don't mask */	\
	orl	$IOAPIC_IRQI_FLAG_MASKED, IOAPICFLAGS(irq_num) ;	\
						/* set the mask bit */	\
	movq	IOAPICADDR(irq_num), %rcx ;	/* ioapic addr */	\
	movl	REDIRIDX(irq_num), %eax ;	/* get the index */	\
	movl	%eax, (%rcx) ;			/* write the index */	\
	orl	$IOART_INTMASK,IOAPIC_WINDOW(%rcx) ;/* set the mask */	\
7: ;						/* already masked */	\
	IOAPIC_IMASK_UNLOCK ;						\

/*
 * Test to see whether we are handling an edge or level triggered INT.
 *  Level-triggered INTs must still be masked as we don't clear the source,
 *  and the EOI cycle would cause redundant INTs to occur.
 */
#define MASK_LEVEL_IRQ(irq_num)						\
	testl	$IOAPIC_IRQI_FLAG_LEVEL, IOAPICFLAGS(irq_num) ;		\
	jz	9f ;				/* edge, don't mask */	\
	MASK_IRQ(irq_num) ;						\
9: ;									\

/*
 * Test to see if the source is currntly masked, clear if so.
 */
#define UNMASK_IRQ(irq_num)					\
	cmpl	$0,%eax ;						\
	jnz	8f ;							\
	IOAPIC_IMASK_LOCK ;			/* into critical reg */	\
	testl	$IOAPIC_IRQI_FLAG_MASKED, IOAPICFLAGS(irq_num) ;	\
	je	7f ;			/* bit clear, not masked */	\
	andl	$~IOAPIC_IRQI_FLAG_MASKED, IOAPICFLAGS(irq_num) ;	\
						/* clear mask bit */	\
	movq	IOAPICADDR(irq_num),%rcx ;	/* ioapic addr */	\
	movl	REDIRIDX(irq_num), %eax ;	/* get the index */	\
	movl	%eax,(%rcx) ;			/* write the index */	\
	andl	$~IOART_INTMASK,IOAPIC_WINDOW(%rcx) ;/* clear the mask */ \
7: ;									\
	IOAPIC_IMASK_UNLOCK ;						\
8: ;									\

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
IDTVEC(ioapic_intr##irq_num) ;						\
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
	movq	$IRQ_LIDX(irq_num),%rdx ;				\
	orq	%rcx,PCPU_E8(ipending,%rdx) ;				\
	orl	$RQF_INTPEND,PCPU(reqflags) ;				\
	jmp	5f ;							\
2: ;									\
	/* clear pending bit, run handler */				\
	movq	$1,%rcx ;						\
	shlq	$IRQ_SBITS(irq_num),%rcx ;				\
	notq	%rcx ;							\
	movq	$IRQ_LIDX(irq_num),%rdx ;				\
	andq	%rcx,PCPU_E8(ipending,%rdx) ;				\
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

/*
 * Handle "spurious INTerrupts".
 *
 * NOTE: This is different than the "spurious INTerrupt" generated by an
 *	 8259 PIC for missing INTs.  See the APIC documentation for details.
 *	 This routine should NOT do an 'EOI' cycle.
 *
 * NOTE: Even though we don't do anything here we must still swapgs if
 *	 coming from a user frame in case the iretq faults... just use
 *	 the nominal APIC_PUSH_FRAME sequence to get it done.
 */
	.text
	SUPERALIGN_TEXT
	.globl Xspuriousint
Xspuriousint:
	APIC_PUSH_FRAME
	/* No EOI cycle used here */
	FAKE_MCOUNT(TF_RIP(%rsp))
	MEXITCOUNT
	APIC_POP_FRAME
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

	/*
	 * Indicate that we have stopped and loop waiting for permission
	 * to start again.  We must still process IPI events while in a
	 * stopped state.
	 *
	 * Interrupts must remain enabled for non-IPI'd per-cpu interrupts
	 * (e.g. Xtimer, Xinvltlb).
	 */
#if CPUMASK_ELEMENTS != 4
#error "assembly incompatible with cpumask_t"
#endif
	movq	PCPU(cpumask)+0,%rax	/* stopped_cpus |= 1 << cpuid */
	MPLOCKED orq %rax, stopped_cpus+0
	movq	PCPU(cpumask)+8,%rax
	MPLOCKED orq %rax, stopped_cpus+8
	movq	PCPU(cpumask)+16,%rax
	MPLOCKED orq %rax, stopped_cpus+16
	movq	PCPU(cpumask)+24,%rax
	MPLOCKED orq %rax, stopped_cpus+24
	sti
1:
	andl	$~RQF_IPIQ,PCPU(reqflags)
	call	lwkt_smp_stopped
	pause

	subq	%rdi,%rdi
	movq	started_cpus+0,%rax	/* while (!(started_cpus & (1<<id))) */
	andq	PCPU(cpumask)+0,%rax
	orq	%rax,%rdi
	movq	started_cpus+8,%rax
	andq	PCPU(cpumask)+8,%rax
	orq	%rax,%rdi
	movq	started_cpus+16,%rax
	andq	PCPU(cpumask)+16,%rax
	orq	%rax,%rdi
	movq	started_cpus+24,%rax
	andq	PCPU(cpumask)+24,%rax
	orq	%rax,%rdi
	testq	%rdi,%rdi
	jz	1b

	movq	PCPU(other_cpus)+0,%rax	/* started_cpus &= ~(1 << cpuid) */
	MPLOCKED andq %rax, started_cpus+0
	movq	PCPU(other_cpus)+8,%rax
	MPLOCKED andq %rax, started_cpus+8
	movq	PCPU(other_cpus)+16,%rax
	MPLOCKED andq %rax, started_cpus+16
	movq	PCPU(other_cpus)+24,%rax
	MPLOCKED andq %rax, started_cpus+24

	movq	PCPU(other_cpus)+0,%rax	/* stopped_cpus &= ~(1 << cpuid) */
	MPLOCKED andq %rax, stopped_cpus+0
	movq	PCPU(other_cpus)+8,%rax
	MPLOCKED andq %rax, stopped_cpus+8
	movq	PCPU(other_cpus)+16,%rax
	MPLOCKED andq %rax, stopped_cpus+16
	movq	PCPU(other_cpus)+24,%rax
	MPLOCKED andq %rax, stopped_cpus+24

	cmpl	$0,PCPU(cpuid)
	jnz	2f

	movq	CNAME(cpustop_restartfunc), %rax
	testq	%rax, %rax
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
	movq	TF_RIP(%rsp),%rbx	/* sample addr before checking crit */
	movq	%rbx,PCPU(sample_pc)
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
	INTR_HANDLER(32)
	INTR_HANDLER(33)
	INTR_HANDLER(34)
	INTR_HANDLER(35)
	INTR_HANDLER(36)
	INTR_HANDLER(37)
	INTR_HANDLER(38)
	INTR_HANDLER(39)
	INTR_HANDLER(40)
	INTR_HANDLER(41)
	INTR_HANDLER(42)
	INTR_HANDLER(43)
	INTR_HANDLER(44)
	INTR_HANDLER(45)
	INTR_HANDLER(46)
	INTR_HANDLER(47)
	INTR_HANDLER(48)
	INTR_HANDLER(49)
	INTR_HANDLER(50)
	INTR_HANDLER(51)
	INTR_HANDLER(52)
	INTR_HANDLER(53)
	INTR_HANDLER(54)
	INTR_HANDLER(55)
	INTR_HANDLER(56)
	INTR_HANDLER(57)
	INTR_HANDLER(58)
	INTR_HANDLER(59)
	INTR_HANDLER(60)
	INTR_HANDLER(61)
	INTR_HANDLER(62)
	INTR_HANDLER(63)
	INTR_HANDLER(64)
	INTR_HANDLER(65)
	INTR_HANDLER(66)
	INTR_HANDLER(67)
	INTR_HANDLER(68)
	INTR_HANDLER(69)
	INTR_HANDLER(70)
	INTR_HANDLER(71)
	INTR_HANDLER(72)
	INTR_HANDLER(73)
	INTR_HANDLER(74)
	INTR_HANDLER(75)
	INTR_HANDLER(76)
	INTR_HANDLER(77)
	INTR_HANDLER(78)
	INTR_HANDLER(79)
	INTR_HANDLER(80)
	INTR_HANDLER(81)
	INTR_HANDLER(82)
	INTR_HANDLER(83)
	INTR_HANDLER(84)
	INTR_HANDLER(85)
	INTR_HANDLER(86)
	INTR_HANDLER(87)
	INTR_HANDLER(88)
	INTR_HANDLER(89)
	INTR_HANDLER(90)
	INTR_HANDLER(91)
	INTR_HANDLER(92)
	INTR_HANDLER(93)
	INTR_HANDLER(94)
	INTR_HANDLER(95)
	INTR_HANDLER(96)
	INTR_HANDLER(97)
	INTR_HANDLER(98)
	INTR_HANDLER(99)
	INTR_HANDLER(100)
	INTR_HANDLER(101)
	INTR_HANDLER(102)
	INTR_HANDLER(103)
	INTR_HANDLER(104)
	INTR_HANDLER(105)
	INTR_HANDLER(106)
	INTR_HANDLER(107)
	INTR_HANDLER(108)
	INTR_HANDLER(109)
	INTR_HANDLER(110)
	INTR_HANDLER(111)
	INTR_HANDLER(112)
	INTR_HANDLER(113)
	INTR_HANDLER(114)
	INTR_HANDLER(115)
	INTR_HANDLER(116)
	INTR_HANDLER(117)
	INTR_HANDLER(118)
	INTR_HANDLER(119)
	INTR_HANDLER(120)
	INTR_HANDLER(121)
	INTR_HANDLER(122)
	INTR_HANDLER(123)
	INTR_HANDLER(124)
	INTR_HANDLER(125)
	INTR_HANDLER(126)
	INTR_HANDLER(127)
	INTR_HANDLER(128)
	INTR_HANDLER(129)
	INTR_HANDLER(130)
	INTR_HANDLER(131)
	INTR_HANDLER(132)
	INTR_HANDLER(133)
	INTR_HANDLER(134)
	INTR_HANDLER(135)
	INTR_HANDLER(136)
	INTR_HANDLER(137)
	INTR_HANDLER(138)
	INTR_HANDLER(139)
	INTR_HANDLER(140)
	INTR_HANDLER(141)
	INTR_HANDLER(142)
	INTR_HANDLER(143)
	INTR_HANDLER(144)
	INTR_HANDLER(145)
	INTR_HANDLER(146)
	INTR_HANDLER(147)
	INTR_HANDLER(148)
	INTR_HANDLER(149)
	INTR_HANDLER(150)
	INTR_HANDLER(151)
	INTR_HANDLER(152)
	INTR_HANDLER(153)
	INTR_HANDLER(154)
	INTR_HANDLER(155)
	INTR_HANDLER(156)
	INTR_HANDLER(157)
	INTR_HANDLER(158)
	INTR_HANDLER(159)
	INTR_HANDLER(160)
	INTR_HANDLER(161)
	INTR_HANDLER(162)
	INTR_HANDLER(163)
	INTR_HANDLER(164)
	INTR_HANDLER(165)
	INTR_HANDLER(166)
	INTR_HANDLER(167)
	INTR_HANDLER(168)
	INTR_HANDLER(169)
	INTR_HANDLER(170)
	INTR_HANDLER(171)
	INTR_HANDLER(172)
	INTR_HANDLER(173)
	INTR_HANDLER(174)
	INTR_HANDLER(175)
	INTR_HANDLER(176)
	INTR_HANDLER(177)
	INTR_HANDLER(178)
	INTR_HANDLER(179)
	INTR_HANDLER(180)
	INTR_HANDLER(181)
	INTR_HANDLER(182)
	INTR_HANDLER(183)
	INTR_HANDLER(184)
	INTR_HANDLER(185)
	INTR_HANDLER(186)
	INTR_HANDLER(187)
	INTR_HANDLER(188)
	INTR_HANDLER(189)
	INTR_HANDLER(190)
	INTR_HANDLER(191)
MCOUNT_LABEL(eintr)

	.data

#if CPUMASK_ELEMENTS != 4
#error "assembly incompatible with cpumask_t"
#endif
/* variables used by stop_cpus()/restart_cpus()/Xcpustop */
	.globl stopped_cpus, started_cpus
stopped_cpus:
	.quad	0
	.quad	0
	.quad	0
	.quad	0
started_cpus:
	.quad	0
	.quad	0
	.quad	0
	.quad	0

	.globl CNAME(cpustop_restartfunc)
CNAME(cpustop_restartfunc):
	.quad 0
		
	.text

