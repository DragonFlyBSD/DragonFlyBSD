/*
 *	from: vector.s, 386BSD 0.1 unknown origin
 * $FreeBSD: src/sys/i386/isa/icu_vector.s,v 1.14.2.2 2000/07/18 21:12:42 dfr Exp $
 * $DragonFly: src/sys/i386/isa/Attic/icu_vector.s,v 1.9 2003/06/30 19:50:31 dillon Exp $
 */

/*
 * modified for PC98 by Kakefuda
 */

#ifdef PC98
#define ICU_IMR_OFFSET		2	/* IO_ICU{1,2} + 2 */
#else
#define ICU_IMR_OFFSET		1	/* IO_ICU{1,2} + 1 */
#endif

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
	subl	$11*4,%esp ;	/* pushal + 3 seg regs (dummy) */	\

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
	addl	$16*4,%esp ;						\

#define MASK_IRQ(icu, irq_num)						\
	movb	imen + IRQ_BYTE(irq_num),%al ;				\
	orb	$IRQ_BIT(irq_num),%al ;					\
	movb	%al,imen + IRQ_BYTE(irq_num) ;				\
	outb	%al,$icu+ICU_IMR_OFFSET ;				\

#define UNMASK_IRQ(icu, irq_num)					\
	movb	imen + IRQ_BYTE(irq_num),%al ;				\
	andb	$~IRQ_BIT(irq_num),%al ;				\
	movb	%al,imen + IRQ_BYTE(irq_num) ;				\
	outb	%al,$icu+ICU_IMR_OFFSET ;				\
	
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

#define	FAST_INTR(irq_num, vec_name, icu, enable_icus) 			\
	.text ; 							\
	SUPERALIGN_TEXT ; 						\
IDTVEC(vec_name) ; 							\
	PUSH_FRAME ;							\
	FAKE_MCOUNT(13*4(%esp)) ; 					\
	MASK_IRQ(icu, irq_num) ;					\
	enable_icus ;							\
	incl	_intr_nesting_level ;					\
	movl	_curthread,%ebx ;					\
	movl	TD_CPL(%ebx),%eax ;	/* save the cpl for doreti */	\
	pushl	%eax ;							\
	cmpl	$TDPRI_CRIT,TD_PRI(%ebx) ;				\
	jge	1f ;							\
	testl	$IRQ_LBIT(irq_num), %eax ;				\
	jz	2f ;							\
1: ;									\
	/* set pending bit and return, leave interrupt masked */	\
	orl	$IRQ_LBIT(irq_num),_fpending ;				\
	movl	$TDPRI_CRIT,_reqpri ;					\
	jmp	5f ;							\
2: ;									\
	/* clear pending bit, run handler */				\
	addl	$TDPRI_CRIT,TD_PRI(%ebx) ;				\
	andl	$~IRQ_LBIT(irq_num),_fpending ;				\
	pushl	intr_unit + (irq_num) * 4 ;				\
	call	*intr_handler + (irq_num) * 4 ;				\
	addl	$4,%esp ;						\
	subl	$TDPRI_CRIT,TD_PRI(%ebx) ;				\
	incl	_cnt+V_INTR ; /* book-keeping YYY make per-cpu */	\
	movl	intr_countp + (irq_num) * 4,%eax ;			\
	incl	(%eax) ;						\
	UNMASK_IRQ(icu, irq_num) ;					\
5: ;									\
	MEXITCOUNT ;							\
	jmp	doreti ;						\

/*
 * Restart fast interrupt held up by critical section or cpl.
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
	pushl	intr_unit + (irq_num) * 4 ;				\
	call	*intr_handler + (irq_num) * 4 ;				\
	addl	$4, %esp ;						\
	incl	_cnt+V_INTR ;						\
	movl	intr_countp + (irq_num) * 4, %eax ;			\
	incl	(%eax) ;						\
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
 *	- If we can take the interrupt clear its ipending bit,
 *	  set its irunning bit, and schedule its thread.  Leave
 *	  interrupts masked and doreti.
 *
 *	The interrupt thread will run its handlers and loop if 
 *	ipending is found to be set.  ipending/irunning interlock
 *	the interrupt thread with the interrupt.  The handler calls
 *	UNPEND when it is through.
 *
 *	Note that we do not enable interrupts when calling sched_ithd.
 *	YYY sched_ithd may preempt us synchronously (fix interrupt stacking)
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
	incl	_intr_nesting_level ;					\
	movl	_curthread,%ebx ;					\
	movl	TD_CPL(%ebx), %eax ;					\
	pushl	%eax ;		/* push CPL for doreti */		\
	cmpl	$TDPRI_CRIT,TD_PRI(%ebx) ;				\
	jge	1f ;							\
	testl	$IRQ_LBIT(irq_num),_irunning ;				\
	jnz	1f ;							\
	testl	$IRQ_LBIT(irq_num), %eax ;				\
	jz	2f ;							\
1: ;									\
	/* set the pending bit and return, leave interrupt masked */	\
	orl	$IRQ_LBIT(irq_num),_ipending ;				\
	movl	$TDPRI_CRIT,_reqpri ;					\
	jmp	5f ;							\
2: ;									\
	addl	$TDPRI_CRIT,TD_PRI(%ebx) ;				\
	/* set running bit, clear pending bit, run handler */		\
	orl	$IRQ_LBIT(irq_num),_irunning ;				\
	andl	$~IRQ_LBIT(irq_num),_ipending ;				\
	sti ;								\
	pushl	$irq_num ;						\
	call	_sched_ithd ;						\
	addl	$4,%esp ;						\
	subl	$TDPRI_CRIT,TD_PRI(%ebx) ;				\
	incl	_cnt+V_INTR ; /* book-keeping YYY make per-cpu */	\
	movl	intr_countp + (irq_num) * 4,%eax ;			\
	incl	(%eax) ;						\
5: ;									\
	MEXITCOUNT ;							\
	jmp	doreti ;						\

/*
 * Unmask a slow interrupt.  This function is used by interrupt threads
 * after they have descheduled themselves to reenable interrupts and
 * possibly cause a reschedule to occur.  The interrupt's irunning bit
 * is cleared prior to unmasking.
 */

#define INTR_UNMASK(irq_num, vec_name, icu)				\
	.text ;								\
	SUPERALIGN_TEXT ;						\
IDTVEC(vec_name) ;							\
	pushl %ebp ;	 /* frame for ddb backtrace */			\
	movl	%esp, %ebp ;						\
	andl	$~IRQ_LBIT(irq_num),_irunning ;				\
	UNMASK_IRQ(icu, irq_num) ;					\
	popl %ebp ;							\
	ret ;								\

MCOUNT_LABEL(bintr)
	FAST_INTR(0,fastintr0, IO_ICU1, ENABLE_ICU1)
	FAST_INTR(1,fastintr1, IO_ICU1, ENABLE_ICU1)
	FAST_INTR(2,fastintr2, IO_ICU1, ENABLE_ICU1)
	FAST_INTR(3,fastintr3, IO_ICU1, ENABLE_ICU1)
	FAST_INTR(4,fastintr4, IO_ICU1, ENABLE_ICU1)
	FAST_INTR(5,fastintr5, IO_ICU1, ENABLE_ICU1)
	FAST_INTR(6,fastintr6, IO_ICU1, ENABLE_ICU1)
	FAST_INTR(7,fastintr7, IO_ICU1, ENABLE_ICU1)
	FAST_INTR(8,fastintr8, IO_ICU2, ENABLE_ICU1_AND_2)
	FAST_INTR(9,fastintr9, IO_ICU2, ENABLE_ICU1_AND_2)
	FAST_INTR(10,fastintr10, IO_ICU2, ENABLE_ICU1_AND_2)
	FAST_INTR(11,fastintr11, IO_ICU2, ENABLE_ICU1_AND_2)
	FAST_INTR(12,fastintr12, IO_ICU2, ENABLE_ICU1_AND_2)
	FAST_INTR(13,fastintr13, IO_ICU2, ENABLE_ICU1_AND_2)
	FAST_INTR(14,fastintr14, IO_ICU2, ENABLE_ICU1_AND_2)
	FAST_INTR(15,fastintr15, IO_ICU2, ENABLE_ICU1_AND_2)

#define	CLKINTR_PENDING	movl $1,CNAME(clkintr_pending)
	INTR(0,intr0, IO_ICU1, ENABLE_ICU1, al, CLKINTR_PENDING)
	INTR(1,intr1, IO_ICU1, ENABLE_ICU1, al,)
	INTR(2,intr2, IO_ICU1, ENABLE_ICU1, al,)
	INTR(3,intr3, IO_ICU1, ENABLE_ICU1, al,)
	INTR(4,intr4, IO_ICU1, ENABLE_ICU1, al,)
	INTR(5,intr5, IO_ICU1, ENABLE_ICU1, al,)
	INTR(6,intr6, IO_ICU1, ENABLE_ICU1, al,)
	INTR(7,intr7, IO_ICU1, ENABLE_ICU1, al,)
	INTR(8,intr8, IO_ICU2, ENABLE_ICU1_AND_2, ah,)
	INTR(9,intr9, IO_ICU2, ENABLE_ICU1_AND_2, ah,)
	INTR(10,intr10, IO_ICU2, ENABLE_ICU1_AND_2, ah,)
	INTR(11,intr11, IO_ICU2, ENABLE_ICU1_AND_2, ah,)
	INTR(12,intr12, IO_ICU2, ENABLE_ICU1_AND_2, ah,)
	INTR(13,intr13, IO_ICU2, ENABLE_ICU1_AND_2, ah,)
	INTR(14,intr14, IO_ICU2, ENABLE_ICU1_AND_2, ah,)
	INTR(15,intr15, IO_ICU2, ENABLE_ICU1_AND_2, ah,)

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
