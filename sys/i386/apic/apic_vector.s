/*
 *	from: vector.s, 386BSD 0.1 unknown origin
 * $FreeBSD: src/sys/i386/isa/apic_vector.s,v 1.47.2.5 2001/09/01 22:33:38 tegge Exp $
 * $DragonFly: src/sys/i386/apic/Attic/apic_vector.s,v 1.7 2003/07/01 20:31:38 dillon Exp $
 */


#include <machine/apic.h>
#include <machine/smp.h>

#include "i386/isa/intr_machdep.h"

/* convert an absolute IRQ# into a bitmask */
#define IRQ_BIT(irq_num)	(1 << (irq_num))

/* make an index into the IO APIC from the IRQ# */
#define REDTBL_IDX(irq_num)	(0x10 + ((irq_num) * 2))


/*
 * Macros for interrupt interrupt entry, call to handler, and exit.
 */

#define	FAST_INTR(irq_num, vec_name)					\
	.text ;								\
	SUPERALIGN_TEXT ;						\
IDTVEC(vec_name) ;							\
	pushl	%eax ;		/* save only call-used registers */	\
	pushl	%ecx ;							\
	pushl	%edx ;							\
	pushl	%ds ;							\
	pushl	%es ; 							\
	pushl	%fs ;							\
	movl	$KDSEL,%eax ;						\
	mov	%ax,%ds ;						\
	movl	%ax,%es ;						\
	movl	$KPSEL,%eax ;						\
	mov	%ax,%fs ;						\
	FAKE_MCOUNT(6*4(%esp)) ;					\
	pushl	intr_unit + (irq_num) * 4 ;				\
	call	*intr_handler + (irq_num) * 4 ; /* do the work ASAP */ \
	addl	$4, %esp ;						\
	movl	$0, lapic_eoi ;						\
	lock ; 								\
	incl	cnt+V_INTR ;	/* book-keeping can wait */		\
	movl	intr_countp + (irq_num) * 4, %eax ;			\
	lock ; 								\
	incl	(%eax) ;						\
	MEXITCOUNT ;							\
	popl	%fs ;							\
	popl	%es ;							\
	popl	%ds ;							\
	popl	%edx ;							\
	popl	%ecx ;							\
	popl	%eax ;							\
	iret

/*
 * 
 */
#define PUSH_FRAME							\
	pushl	$0 ;		/* dummy error code */			\
	pushl	$0 ;		/* dummy trap type */			\
	pushal ;							\
	pushl	%ds ;		/* save data and extra segments ... */	\
	pushl	%es ;							\
	pushl	%fs

#define POP_FRAME							\
	popl	%fs ;							\
	popl	%es ;							\
	popl	%ds ;							\
	popal ;								\
	addl	$4+4,%esp

#define IOAPICADDR(irq_num) CNAME(int_to_apicintpin) + 16 * (irq_num) + 8
#define REDIRIDX(irq_num) CNAME(int_to_apicintpin) + 16 * (irq_num) + 12
	
#define MASK_IRQ(irq_num)						\
	IMASK_LOCK ;				/* into critical reg */	\
	testl	$IRQ_BIT(irq_num), apic_imen ;				\
	jne	7f ;			/* masked, don't mask */	\
	orl	$IRQ_BIT(irq_num), apic_imen ;	/* set the mask bit */	\
	movl	IOAPICADDR(irq_num), %ecx ;	/* ioapic addr */	\
	movl	REDIRIDX(irq_num), %eax ;	/* get the index */	\
	movl	%eax, (%ecx) ;			/* write the index */	\
	movl	IOAPIC_WINDOW(%ecx), %eax ;	/* current value */	\
	orl	$IOART_INTMASK, %eax ;		/* set the mask */	\
	movl	%eax, IOAPIC_WINDOW(%ecx) ;	/* new value */		\
7: ;						/* already masked */	\
	IMASK_UNLOCK
/*
 * Test to see whether we are handling an edge or level triggered INT.
 *  Level-triggered INTs must still be masked as we don't clear the source,
 *  and the EOI cycle would cause redundant INTs to occur.
 */
#define MASK_LEVEL_IRQ(irq_num)						\
	testl	$IRQ_BIT(irq_num), apic_pin_trigger ;			\
	jz	9f ;				/* edge, don't mask */	\
	MASK_IRQ(irq_num) ;						\
9:


#ifdef APIC_INTR_REORDER
#define EOI_IRQ(irq_num)						\
	movl	apic_isrbit_location + 8 * (irq_num), %eax ;		\
	movl	(%eax), %eax ;						\
	testl	apic_isrbit_location + 4 + 8 * (irq_num), %eax ;	\
	jz	9f ;				/* not active */	\
	movl	$0, lapic_eoi ;						\
	APIC_ITRACE(apic_itrace_eoi, irq_num, APIC_ITRACE_EOI) ;	\
9:

#else
#define EOI_IRQ(irq_num)						\
	testl	$IRQ_BIT(irq_num), lapic_isr1;				\
	jz	9f	;			/* not active */	\
	movl	$0, lapic_eoi;						\
	APIC_ITRACE(apic_itrace_eoi, irq_num, APIC_ITRACE_EOI) ;	\
9:
#endif
	
	
/*
 * Test to see if the source is currntly masked, clear if so.
 */
#define UNMASK_IRQ(irq_num)					\
	IMASK_LOCK ;				/* into critical reg */	\
	testl	$IRQ_BIT(irq_num), apic_imen ;				\
	je	7f ;			/* bit clear, not masked */	\
	andl	$~IRQ_BIT(irq_num), apic_imen ;/* clear mask bit */	\
	movl	IOAPICADDR(irq_num),%ecx ;	/* ioapic addr */	\
	movl	REDIRIDX(irq_num), %eax ;	/* get the index */	\
	movl	%eax,(%ecx) ;			/* write the index */	\
	movl	IOAPIC_WINDOW(%ecx),%eax ;	/* current value */	\
	andl	$~IOART_INTMASK,%eax ;		/* clear the mask */	\
	movl	%eax,IOAPIC_WINDOW(%ecx) ;	/* new value */		\
7: ;									\
	IMASK_UNLOCK

#ifdef APIC_INTR_DIAGNOSTIC
#ifdef APIC_INTR_DIAGNOSTIC_IRQ
log_intr_event:
	pushf
	cli
	pushl	$CNAME(apic_itrace_debuglock)
	call	CNAME(s_lock_np)
	addl	$4, %esp
	movl	CNAME(apic_itrace_debugbuffer_idx), %ecx
	andl	$32767, %ecx
	movl	PCPU(cpuid), %eax
	shll	$8,	%eax
	orl	8(%esp), %eax
	movw	%ax,	CNAME(apic_itrace_debugbuffer)(,%ecx,2)
	incl	%ecx
	andl	$32767, %ecx
	movl	%ecx,	CNAME(apic_itrace_debugbuffer_idx)
	pushl	$CNAME(apic_itrace_debuglock)
	call	CNAME(s_unlock_np)
	addl	$4, %esp
	popf
	ret
	

#define APIC_ITRACE(name, irq_num, id)					\
	lock ;					/* MP-safe */		\
	incl	CNAME(name) + (irq_num) * 4 ;				\
	pushl	%eax ;							\
	pushl	%ecx ;							\
	pushl	%edx ;							\
	movl	$(irq_num), %eax ;					\
	cmpl	$APIC_INTR_DIAGNOSTIC_IRQ, %eax ;			\
	jne	7f ;							\
	pushl	$id ;							\
	call	log_intr_event ;					\
	addl	$4, %esp ;						\
7: ;									\
	popl	%edx ;							\
	popl	%ecx ;							\
	popl	%eax
#else
#define APIC_ITRACE(name, irq_num, id)					\
	lock ;					/* MP-safe */		\
	incl	CNAME(name) + (irq_num) * 4
#endif

#define APIC_ITRACE_ENTER 1
#define APIC_ITRACE_EOI 2
#define APIC_ITRACE_TRYISRLOCK 3
#define APIC_ITRACE_GOTISRLOCK 4
#define APIC_ITRACE_ENTER2 5
#define APIC_ITRACE_LEAVE 6
#define APIC_ITRACE_UNMASK 7
#define APIC_ITRACE_ACTIVE 8
#define APIC_ITRACE_MASKED 9
#define APIC_ITRACE_NOISRLOCK 10
#define APIC_ITRACE_MASKED2 11
#define APIC_ITRACE_SPLZ 12
#define APIC_ITRACE_DORETI 13	
	
#else	
#define APIC_ITRACE(name, irq_num, id)
#endif
		
#define	INTR(irq_num, vec_name, maybe_extra_ipending)			\
	.text ;								\
	SUPERALIGN_TEXT ;						\
/* XintrNN: entry point used by IDT/HWIs & splz_unpend via _vec[]. */	\
IDTVEC(vec_name) ;							\
	PUSH_FRAME ;							\
	movl	$KDSEL, %eax ;	/* reload with kernel's data segment */	\
	mov	%ax, %ds ;						\
	mov	%ax, %es ;						\
	movl	$KPSEL, %eax ;						\
	mov	%ax, %fs ;						\
;									\
	maybe_extra_ipending ;						\
;									\
	APIC_ITRACE(apic_itrace_enter, irq_num, APIC_ITRACE_ENTER) ;	\
	lock ;					/* MP-safe */		\
	btsl	$(irq_num), iactive ;		/* lazy masking */	\
	jc	1f ;				/* already active */	\
;									\
	MASK_LEVEL_IRQ(irq_num) ;					\
	EOI_IRQ(irq_num) ;						\
0: ;									\
	APIC_ITRACE(apic_itrace_tryisrlock, irq_num, APIC_ITRACE_TRYISRLOCK) ;\
	MP_TRYLOCK ;		/* XXX this is going away... */		\
	testl	%eax, %eax ;			/* did we get it? */	\
	jz	3f ;				/* no */		\
;									\
	APIC_ITRACE(apic_itrace_gotisrlock, irq_num, APIC_ITRACE_GOTISRLOCK) ;\
	movl	PCPU(curthread),%ebx ;					\
	testl	$IRQ_BIT(irq_num), TD_MACH+MTD_CPL(%eax) ;		\
	jne	2f ;				/* this INT masked */	\
	cmpl	$TDPRI_CRIT,TD_PRI(%ebx) ;				\
	jge	2f ;				/* in critical sec */	\
;									\
	incb	PCPU(intr_nesting_level) ;				\
;	 								\
  /* entry point used by doreti_unpend for HWIs. */			\
__CONCAT(Xresume,irq_num): ;						\
	FAKE_MCOUNT(13*4(%esp)) ;		/* XXX avoid dbl cnt */ \
	lock ;	incl	_cnt+V_INTR ;		/* tally interrupts */	\
	movl	_intr_countp + (irq_num) * 4, %eax ;			\
	lock ;	incl	(%eax) ;					\
;									\
	movl	PCPU(curthread), %ebx ;					\
	movl	TD_MACH+MTD_CPL(%ebx), %eax ;				\
	pushl	%eax ;	 /* cpl restored by doreti */			\
	orl	_intr_mask + (irq_num) * 4, %eax ;			\
	movl	%eax, TD_MACH+MTD_CPL(%ebx) ;				\
	lock ;								\
	andl	$~IRQ_BIT(irq_num), PCPU(ipending) ;			\
;									\
	pushl	_intr_unit + (irq_num) * 4 ;				\
	APIC_ITRACE(apic_itrace_enter2, irq_num, APIC_ITRACE_ENTER2) ;	\
	sti ;								\
	call	*_intr_handler + (irq_num) * 4 ;			\
	cli ;								\
	APIC_ITRACE(apic_itrace_leave, irq_num, APIC_ITRACE_LEAVE) ;	\
	addl	$4,%esp ;						\
;									\
	lock ;	andl	$~IRQ_BIT(irq_num), iactive ;			\
	UNMASK_IRQ(irq_num) ;						\
	APIC_ITRACE(apic_itrace_unmask, irq_num, APIC_ITRACE_UNMASK) ;	\
	sti ;				/* doreti repeats cli/sti */	\
	MEXITCOUNT ;							\
	jmp	doreti ;						\
;									\
	ALIGN_TEXT ;							\
1: ;						/* active  */		\
	APIC_ITRACE(apic_itrace_active, irq_num, APIC_ITRACE_ACTIVE) ;	\
	MASK_IRQ(irq_num) ;						\
	EOI_IRQ(irq_num) ;						\
	lock ;								\
	orl	$IRQ_BIT(irq_num), PCPU(ipending) ;			\
	movl	$TDPRI_CRIT, PCPU(reqpri) ;				\
	lock ;								\
	btsl	$(irq_num), iactive ;		/* still active */	\
	jnc	0b ;				/* retry */		\
	POP_FRAME ;							\
	iret ;		/* XXX:	 iactive bit might be 0 now */		\
	ALIGN_TEXT ;							\
2: ;				/* masked by cpl, leave iactive set */	\
	APIC_ITRACE(apic_itrace_masked, irq_num, APIC_ITRACE_MASKED) ;	\
	lock ;								\
	orl	$IRQ_BIT(irq_num), PCPU(ipending) ;			\
	movl	$TDPRI_CRIT, PCPU(reqpri) ;				\
	MP_RELLOCK ;							\
	POP_FRAME ;							\
	iret ;								\
	ALIGN_TEXT ;							\
3: ; 			/* other cpu has isr lock */			\
	APIC_ITRACE(apic_itrace_noisrlock, irq_num, APIC_ITRACE_NOISRLOCK) ;\
	lock ;								\
	orl	$IRQ_BIT(irq_num), PCPU(ipending) ;			\
	movl	$TDPRI_CRIT,_reqpri ;					\
	testl	$IRQ_BIT(irq_num), TD_MACH+MTD_CPL(%ebx) ;		\
	jne	4f ;				/* this INT masked */	\
	call	forward_irq ;	 /* forward irq to lock holder */	\
	POP_FRAME ;	 			/* and return */	\
	iret ;								\
	ALIGN_TEXT ;							\
4: ;	 					/* blocked */		\
	APIC_ITRACE(apic_itrace_masked2, irq_num, APIC_ITRACE_MASKED2) ;\
	POP_FRAME ;	 			/* and return */	\
	iret

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
	.globl	_Xinvltlb
_Xinvltlb:
	pushl	%eax

#ifdef COUNT_XINVLTLB_HITS
	pushl	%fs
	movl	$KPSEL, %eax
	mov	%ax, %fs
	movl	PCPU(cpuid), %eax
	popl	%fs
	ss
	incl	_xhits(,%eax,4)
#endif /* COUNT_XINVLTLB_HITS */

	movl	%cr3, %eax		/* invalidate the TLB */
	movl	%eax, %cr3

	ss				/* stack segment, avoid %ds load */
	movl	$0, lapic_eoi		/* End Of Interrupt to APIC */

	popl	%eax
	iret


#ifdef BETTER_CLOCK

/*
 * Executed by a CPU when it receives an Xcpucheckstate IPI from another CPU,
 *
 *  - Stores current cpu state in checkstate_cpustate[cpuid]
 *      0 == user, 1 == sys, 2 == intr
 *  - Stores current process in checkstate_curproc[cpuid]
 *
 *  - Signals its receipt by setting bit cpuid in checkstate_probed_cpus.
 *
 * stack: 0->ds, 4->fs, 8->ebx, 12->eax, 16->eip, 20->cs, 24->eflags
 */

	.text
	SUPERALIGN_TEXT
	.globl Xcpucheckstate
	.globl checkstate_cpustate
	.globl checkstate_curproc
	.globl checkstate_pc
Xcpucheckstate:
	pushl	%eax
	pushl	%ebx		
	pushl	%ds			/* save current data segment */
	pushl	%fs

	movl	$KDSEL, %eax
	mov	%ax, %ds		/* use KERNEL data segment */
	movl	$KPSEL, %eax
	mov	%ax, %fs

	movl	$0, lapic_eoi		/* End Of Interrupt to APIC */

	movl	$0, %ebx		
	movl	20(%esp), %eax	
	andl	$3, %eax
	cmpl	$3, %eax
	je	1f
	testl	$PSL_VM, 24(%esp)
	jne	1f
	incl	%ebx			/* system or interrupt */
1:	
	movl	PCPU(cpuid), %eax
	movl	%ebx, checkstate_cpustate(,%eax,4)
	movl	PCPU(curthread), %ebx
	movl	TD_PROC(%ebx),%ebx
	movl	%ebx, checkstate_curproc(,%eax,4)
	movl	16(%esp), %ebx
	movl	%ebx, checkstate_pc(,%eax,4)

	lock				/* checkstate_probed_cpus |= (1<<id) */
	btsl	%eax, checkstate_probed_cpus

	popl	%fs
	popl	%ds			/* restore previous data segment */
	popl	%ebx
	popl	%eax
	iret

#endif /* BETTER_CLOCK */

/*
 * Executed by a CPU when it receives an Xcpuast IPI from another CPU,
 *
 *  - Signals its receipt by clearing bit cpuid in checkstate_need_ast.
 *
 *  - We need a better method of triggering asts on other cpus.
 */

	.text
	SUPERALIGN_TEXT
	.globl Xcpuast
Xcpuast:
	PUSH_FRAME
	movl	$KDSEL, %eax
	mov	%ax, %ds		/* use KERNEL data segment */
	mov	%ax, %es
	movl	$KPSEL, %eax
	mov	%ax, %fs

	movl	PCPU(cpuid), %eax
	lock				/* checkstate_need_ast &= ~(1<<id) */
	btrl	%eax, checkstate_need_ast
	movl	$0, lapic_eoi		/* End Of Interrupt to APIC */

	lock
	btsl	%eax, checkstate_pending_ast
	jc	1f

	FAKE_MCOUNT(13*4(%esp))

	/* 
	 * Giant locks do not come cheap.
	 * A lot of cycles are going to be wasted here.
	 */
	call	get_mplock

	movl	PCPU(curthread), %eax
	pushl	TD_MACH+MTD_CPL(%eax)		/* cpl restored by doreti */

	orl	$AST_PENDING, PCPU(astpending)	/* XXX */
	incb	PCPU(intr_nesting_level)
	sti
	
	movl	PCPU(cpuid), %eax
	lock	
	btrl	%eax, checkstate_pending_ast
	lock	
	btrl	%eax, CNAME(resched_cpus)
	jnc	2f
	orl	$AST_PENDING+AST_RESCHED,PCPU(astpending)
	lock
	incl	CNAME(want_resched_cnt)
2:		
	lock
	incl	CNAME(cpuast_cnt)
	MEXITCOUNT
	jmp	doreti
1:
	/* We are already in the process of delivering an ast for this CPU */
	POP_FRAME
	iret			


/*
 *	 Executed by a CPU when it receives an XFORWARD_IRQ IPI.
 */

	.text
	SUPERALIGN_TEXT
	.globl Xforward_irq
Xforward_irq:
	PUSH_FRAME
	movl	$KDSEL, %eax
	mov	%ax, %ds		/* use KERNEL data segment */
	mov	%ax, %es
	movl	$KPSEL, %eax
	mov	%ax, %fs

	movl	$0, lapic_eoi		/* End Of Interrupt to APIC */

	FAKE_MCOUNT(13*4(%esp))

	MP_TRYLOCK
	testl	%eax,%eax		/* Did we get the lock ? */
	jz  1f				/* No */

	lock
	incl	CNAME(forward_irq_hitcnt)
	cmpb	$4, PCPU(intr_nesting_level)
	jae	2f
	
	movl	PCPU(curthread), %eax
	pushl	TD_MACH+MTD_CPL(%eax)		/* cpl restored by doreti */

	incb	PCPU(intr_nesting_level)
	sti
	
	MEXITCOUNT
	jmp	doreti			/* Handle forwarded interrupt */
1:
	lock
	incl	CNAME(forward_irq_misscnt)
	call	forward_irq	/* Oops, we've lost the isr lock */
	MEXITCOUNT
	POP_FRAME
	iret
2:
	lock
	incl	CNAME(forward_irq_toodeepcnt)
3:	
	MP_RELLOCK
	MEXITCOUNT
	POP_FRAME
	iret

/*
 * 
 */
forward_irq:
	MCOUNT
	cmpl	$0,_invltlb_ok
	jz	4f

	cmpl	$0, CNAME(forward_irq_enabled)
	jz	4f

	movl	_mp_lock,%eax
	cmpl	$FREE_LOCK,%eax
	jne	1f
	movl	$0, %eax		/* Pick CPU #0 if noone has lock */
1:
	shrl	$24,%eax
	movl	_cpu_num_to_apic_id(,%eax,4),%ecx
	shll	$24,%ecx
	movl	lapic_icr_hi, %eax
	andl	$~APIC_ID_MASK, %eax
	orl	%ecx, %eax
	movl	%eax, lapic_icr_hi

2:
	movl	lapic_icr_lo, %eax
	andl	$APIC_DELSTAT_MASK,%eax
	jnz	2b
	movl	lapic_icr_lo, %eax
	andl	$APIC_RESV2_MASK, %eax
	orl	$(APIC_DEST_DESTFLD|APIC_DELMODE_FIXED|XFORWARD_IRQ_OFFSET), %eax
	movl	%eax, lapic_icr_lo
3:
	movl	lapic_icr_lo, %eax
	andl	$APIC_DELSTAT_MASK,%eax
	jnz	3b
4:		
	ret
	
/*
 * Executed by a CPU when it receives an Xcpustop IPI from another CPU,
 *
 *  - Signals its receipt.
 *  - Waits for permission to restart.
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

	lock
	btsl	%eax, stopped_cpus	/* stopped_cpus |= (1<<id) */
1:
	btl	%eax, started_cpus	/* while (!(started_cpus & (1<<id))) */
	jnc	1b

	lock
	btrl	%eax, started_cpus	/* started_cpus &= ~(1<<id) */
	lock
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


MCOUNT_LABEL(bintr)
	FAST_INTR(0,fastintr0)
	FAST_INTR(1,fastintr1)
	FAST_INTR(2,fastintr2)
	FAST_INTR(3,fastintr3)
	FAST_INTR(4,fastintr4)
	FAST_INTR(5,fastintr5)
	FAST_INTR(6,fastintr6)
	FAST_INTR(7,fastintr7)
	FAST_INTR(8,fastintr8)
	FAST_INTR(9,fastintr9)
	FAST_INTR(10,fastintr10)
	FAST_INTR(11,fastintr11)
	FAST_INTR(12,fastintr12)
	FAST_INTR(13,fastintr13)
	FAST_INTR(14,fastintr14)
	FAST_INTR(15,fastintr15)
	FAST_INTR(16,fastintr16)
	FAST_INTR(17,fastintr17)
	FAST_INTR(18,fastintr18)
	FAST_INTR(19,fastintr19)
	FAST_INTR(20,fastintr20)
	FAST_INTR(21,fastintr21)
	FAST_INTR(22,fastintr22)
	FAST_INTR(23,fastintr23)
	
#define	CLKINTR_PENDING							\
	pushl $clock_lock ;						\
	call s_lock ;							\
	movl $1,CNAME(clkintr_pending) ;				\
	call s_unlock ;							\
	addl $4, %esp

	INTR(0,intr0, CLKINTR_PENDING)
	INTR(1,intr1,)
	INTR(2,intr2,)
	INTR(3,intr3,)
	INTR(4,intr4,)
	INTR(5,intr5,)
	INTR(6,intr6,)
	INTR(7,intr7,)
	INTR(8,intr8,)
	INTR(9,intr9,)
	INTR(10,intr10,)
	INTR(11,intr11,)
	INTR(12,intr12,)
	INTR(13,intr13,)
	INTR(14,intr14,)
	INTR(15,intr15,)
	INTR(16,intr16,)
	INTR(17,intr17,)
	INTR(18,intr18,)
	INTR(19,intr19,)
	INTR(20,intr20,)
	INTR(21,intr21,)
	INTR(22,intr22,)
	INTR(23,intr23,)
MCOUNT_LABEL(eintr)

/*
 * Executed by a CPU when it receives a RENDEZVOUS IPI from another CPU.
 *
 * - Calls the generic rendezvous action function.
 */
	.text
	SUPERALIGN_TEXT
	.globl	_Xrendezvous
_Xrendezvous:
	PUSH_FRAME
	movl	$KDSEL, %eax
	mov	%ax, %ds		/* use KERNEL data segment */
	mov	%ax, %es
	movl	$KPSEL, %eax
	mov	%ax, %fs

	call	_smp_rendezvous_action

	movl	$0, lapic_eoi		/* End Of Interrupt to APIC */
	POP_FRAME
	iret
	
	
	.data

#if 0
/*
 * Addresses of interrupt handlers.
 *  XresumeNN: Resumption addresses for HWIs.
 */
	.globl _ihandlers
_ihandlers:
/*
 * used by:
 *  ipl.s:	doreti_unpend
 */
	.long	Xresume0,  Xresume1,  Xresume2,  Xresume3 
	.long	Xresume4,  Xresume5,  Xresume6,  Xresume7
	.long	Xresume8,  Xresume9,  Xresume10, Xresume11
	.long	Xresume12, Xresume13, Xresume14, Xresume15 
	.long	Xresume16, Xresume17, Xresume18, Xresume19
	.long	Xresume20, Xresume21, Xresume22, Xresume23
/*
 * used by:
 *  ipl.s:	doreti_unpend
 *  apic_ipl.s:	splz_unpend
 */
	.long	_swi_null, swi_net, _swi_null, _swi_null
	.long	_swi_vm, _swi_null, _softclock

imasks:				/* masks for interrupt handlers */
	.space	NHWI*4		/* padding; HWI masks are elsewhere */

	.long	SWI_TTY_MASK, SWI_NET_MASK, SWI_CAMNET_MASK, SWI_CAMBIO_MASK
	.long	SWI_VM_MASK, SWI_TQ_MASK, SWI_CLOCK_MASK
#endif

/* active flag for lazy masking */
iactive:
	.long	0

#ifdef COUNT_XINVLTLB_HITS
	.globl	xhits
xhits:
	.space	(NCPU * 4), 0
#endif /* COUNT_XINVLTLB_HITS */

/* variables used by stop_cpus()/restart_cpus()/Xcpustop */
	.globl stopped_cpus, started_cpus
stopped_cpus:
	.long	0
started_cpus:
	.long	0

#ifdef BETTER_CLOCK
	.globl checkstate_probed_cpus
checkstate_probed_cpus:
	.long	0	
#endif /* BETTER_CLOCK */
	.globl checkstate_need_ast
checkstate_need_ast:
	.long	0
checkstate_pending_ast:
	.long	0
	.globl CNAME(forward_irq_misscnt)
	.globl CNAME(forward_irq_toodeepcnt)
	.globl CNAME(forward_irq_hitcnt)
	.globl CNAME(resched_cpus)
	.globl CNAME(want_resched_cnt)
	.globl CNAME(cpuast_cnt)
	.globl CNAME(cpustop_restartfunc)
CNAME(forward_irq_misscnt):	
	.long 0
CNAME(forward_irq_hitcnt):	
	.long 0
CNAME(forward_irq_toodeepcnt):
	.long 0
CNAME(resched_cpus):
	.long 0
CNAME(want_resched_cnt):
	.long 0
CNAME(cpuast_cnt):
	.long 0
CNAME(cpustop_restartfunc):
	.long 0
		


	.globl	apic_pin_trigger
apic_pin_trigger:
	.long	0

	.text
