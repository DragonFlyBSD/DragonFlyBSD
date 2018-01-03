/*
 *	from: vector.s, 386BSD 0.1 unknown origin
 * $FreeBSD: src/sys/i386/isa/apic_vector.s,v 1.47.2.5 2001/09/01 22:33:38 tegge Exp $
 */

#include <machine/asmacros.h>
#include <machine/lock.h>
#include <machine/psl.h>
#include <machine/trap.h>
#include <machine/segments.h>

#include "assym.s"

#ifdef foo
/* convert an absolute IRQ# into bitmask */
#define IRQ_LBIT(irq_num)	(1UL << (irq_num & 0x3f))
#endif

#define IRQ_SBITS(irq_num)	((irq_num) & 0x3f)

/* convert an absolute IRQ# into gd_ipending index */
#define IRQ_LIDX(irq_num)	((irq_num) >> 6)

#define MSI_PUSH_FRAME							\
	PUSH_FRAME_TFRIP ;	/* 15 regs + space for 5 extras */	\
	movq $0,TF_XFLAGS(%rsp) ;					\
	movq $0,TF_TRAPNO(%rsp) ;					\
	movq $0,TF_ADDR(%rsp) ;						\
	movq $0,TF_FLAGS(%rsp) ;					\
	movq $0,TF_ERR(%rsp) ;						\
	cld ;								\

/*
 * Interrupt call handlers run in the following sequence:
 *
 *	- Push the trap frame required by doreti
 *	- If we cannot take the interrupt set its ipending bit and
 *	  doreti.
 *	- If we can take the interrupt clear its ipending bit,
 *	  call the handler and doreti.
 *
 * YYY can cache gd base opitner instead of using hidden %fs prefixes.
 */

#define	MSI_HANDLER(irq_num)						\
	.text ;								\
	SUPERALIGN_TEXT ;						\
IDTVEC(msi_intr##irq_num) ;						\
	MSI_PUSH_FRAME ;						\
	FAKE_MCOUNT(TF_RIP(%rsp)) ;					\
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
	call	ithread_fast_handler ;					\
	decl	TD_CRITCOUNT(%rbx) ;					\
	addq	$8, %rsp ;		/* intrframe -> trapframe */	\
5: ;									\
	MEXITCOUNT ;							\
	jmp	doreti ;						\


MCOUNT_LABEL(bintr)
	MSI_HANDLER(0)
	MSI_HANDLER(1)
	MSI_HANDLER(2)
	MSI_HANDLER(3)
	MSI_HANDLER(4)
	MSI_HANDLER(5)
	MSI_HANDLER(6)
	MSI_HANDLER(7)
	MSI_HANDLER(8)
	MSI_HANDLER(9)
	MSI_HANDLER(10)
	MSI_HANDLER(11)
	MSI_HANDLER(12)
	MSI_HANDLER(13)
	MSI_HANDLER(14)
	MSI_HANDLER(15)
	MSI_HANDLER(16)
	MSI_HANDLER(17)
	MSI_HANDLER(18)
	MSI_HANDLER(19)
	MSI_HANDLER(20)
	MSI_HANDLER(21)
	MSI_HANDLER(22)
	MSI_HANDLER(23)
	MSI_HANDLER(24)
	MSI_HANDLER(25)
	MSI_HANDLER(26)
	MSI_HANDLER(27)
	MSI_HANDLER(28)
	MSI_HANDLER(29)
	MSI_HANDLER(30)
	MSI_HANDLER(31)
	MSI_HANDLER(32)
	MSI_HANDLER(33)
	MSI_HANDLER(34)
	MSI_HANDLER(35)
	MSI_HANDLER(36)
	MSI_HANDLER(37)
	MSI_HANDLER(38)
	MSI_HANDLER(39)
	MSI_HANDLER(40)
	MSI_HANDLER(41)
	MSI_HANDLER(42)
	MSI_HANDLER(43)
	MSI_HANDLER(44)
	MSI_HANDLER(45)
	MSI_HANDLER(46)
	MSI_HANDLER(47)
	MSI_HANDLER(48)
	MSI_HANDLER(49)
	MSI_HANDLER(50)
	MSI_HANDLER(51)
	MSI_HANDLER(52)
	MSI_HANDLER(53)
	MSI_HANDLER(54)
	MSI_HANDLER(55)
	MSI_HANDLER(56)
	MSI_HANDLER(57)
	MSI_HANDLER(58)
	MSI_HANDLER(59)
	MSI_HANDLER(60)
	MSI_HANDLER(61)
	MSI_HANDLER(62)
	MSI_HANDLER(63)
	MSI_HANDLER(64)
	MSI_HANDLER(65)
	MSI_HANDLER(66)
	MSI_HANDLER(67)
	MSI_HANDLER(68)
	MSI_HANDLER(69)
	MSI_HANDLER(70)
	MSI_HANDLER(71)
	MSI_HANDLER(72)
	MSI_HANDLER(73)
	MSI_HANDLER(74)
	MSI_HANDLER(75)
	MSI_HANDLER(76)
	MSI_HANDLER(77)
	MSI_HANDLER(78)
	MSI_HANDLER(79)
	MSI_HANDLER(80)
	MSI_HANDLER(81)
	MSI_HANDLER(82)
	MSI_HANDLER(83)
	MSI_HANDLER(84)
	MSI_HANDLER(85)
	MSI_HANDLER(86)
	MSI_HANDLER(87)
	MSI_HANDLER(88)
	MSI_HANDLER(89)
	MSI_HANDLER(90)
	MSI_HANDLER(91)
	MSI_HANDLER(92)
	MSI_HANDLER(93)
	MSI_HANDLER(94)
	MSI_HANDLER(95)
	MSI_HANDLER(96)
	MSI_HANDLER(97)
	MSI_HANDLER(98)
	MSI_HANDLER(99)
	MSI_HANDLER(100)
	MSI_HANDLER(101)
	MSI_HANDLER(102)
	MSI_HANDLER(103)
	MSI_HANDLER(104)
	MSI_HANDLER(105)
	MSI_HANDLER(106)
	MSI_HANDLER(107)
	MSI_HANDLER(108)
	MSI_HANDLER(109)
	MSI_HANDLER(110)
	MSI_HANDLER(111)
	MSI_HANDLER(112)
	MSI_HANDLER(113)
	MSI_HANDLER(114)
	MSI_HANDLER(115)
	MSI_HANDLER(116)
	MSI_HANDLER(117)
	MSI_HANDLER(118)
	MSI_HANDLER(119)
	MSI_HANDLER(120)
	MSI_HANDLER(121)
	MSI_HANDLER(122)
	MSI_HANDLER(123)
	MSI_HANDLER(124)
	MSI_HANDLER(125)
	MSI_HANDLER(126)
	MSI_HANDLER(127)
	MSI_HANDLER(128)
	MSI_HANDLER(129)
	MSI_HANDLER(130)
	MSI_HANDLER(131)
	MSI_HANDLER(132)
	MSI_HANDLER(133)
	MSI_HANDLER(134)
	MSI_HANDLER(135)
	MSI_HANDLER(136)
	MSI_HANDLER(137)
	MSI_HANDLER(138)
	MSI_HANDLER(139)
	MSI_HANDLER(140)
	MSI_HANDLER(141)
	MSI_HANDLER(142)
	MSI_HANDLER(143)
	MSI_HANDLER(144)
	MSI_HANDLER(145)
	MSI_HANDLER(146)
	MSI_HANDLER(147)
	MSI_HANDLER(148)
	MSI_HANDLER(149)
	MSI_HANDLER(150)
	MSI_HANDLER(151)
	MSI_HANDLER(152)
	MSI_HANDLER(153)
	MSI_HANDLER(154)
	MSI_HANDLER(155)
	MSI_HANDLER(156)
	MSI_HANDLER(157)
	MSI_HANDLER(158)
	MSI_HANDLER(159)
	MSI_HANDLER(160)
	MSI_HANDLER(161)
	MSI_HANDLER(162)
	MSI_HANDLER(163)
	MSI_HANDLER(164)
	MSI_HANDLER(165)
	MSI_HANDLER(166)
	MSI_HANDLER(167)
	MSI_HANDLER(168)
	MSI_HANDLER(169)
	MSI_HANDLER(170)
	MSI_HANDLER(171)
	MSI_HANDLER(172)
	MSI_HANDLER(173)
	MSI_HANDLER(174)
	MSI_HANDLER(175)
	MSI_HANDLER(176)
	MSI_HANDLER(177)
	MSI_HANDLER(178)
	MSI_HANDLER(179)
	MSI_HANDLER(180)
	MSI_HANDLER(181)
	MSI_HANDLER(182)
	MSI_HANDLER(183)
	MSI_HANDLER(184)
	MSI_HANDLER(185)
	MSI_HANDLER(186)
	MSI_HANDLER(187)
	MSI_HANDLER(188)
	MSI_HANDLER(189)
	MSI_HANDLER(190)
	MSI_HANDLER(191)
MCOUNT_LABEL(eintr)

	.data

	.text
