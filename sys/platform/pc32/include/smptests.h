/*
 * Copyright (c) 1996, by Steve Passe
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. The name of the developer may NOT be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD: src/sys/i386/include/smptests.h,v 1.33.2.1 2000/05/16 06:58:10 dillon Exp $
 * $DragonFly: src/sys/platform/pc32/include/Attic/smptests.h,v 1.5 2004/02/21 06:37:07 dillon Exp $
 */

#ifndef _MACHINE_SMPTESTS_H_
#define _MACHINE_SMPTESTS_H_

/*
 * Various 'tests in progress' and configuration parameters.
 */

#ifdef SMP

/*
 * Control the "giant lock" pushdown by logical steps.
 */
#define PUSHDOWN_LEVEL_1
#define PUSHDOWN_LEVEL_2
#define PUSHDOWN_LEVEL_3_NOT
#define PUSHDOWN_LEVEL_4_NOT

/*
 * Put FAST_INTR() ISRs at an APIC priority above the regular INTs.
 * Allow the mp_lock() routines to handle FAST interrupts while spinning.
 */
#ifdef PUSHDOWN_LEVEL_1
#define FAST_HI
#endif


/*
 * These defines enable critical region locking of areas that were
 * protected via cli/sti in the UP kernel.
 *
 * MPINTRLOCK protects all the generic areas.
 * COMLOCK protects the sio/cy drivers.
 * CLOCKLOCK protects clock hardware and data
 * known to be incomplete:
 *	joystick lkm
 *	?
 */
#ifdef PUSHDOWN_LEVEL_1
#define USE_MPINTRLOCK
#define USE_COMLOCK
#define USE_CLOCKLOCK
#endif


/*
 * INTR_SIMPLELOCK has been removed, as the interrupt mechanism will likely
 * not use this sort of optimization if we move to interrupt threads.
 */
#ifdef PUSHDOWN_LEVEL_4
#endif


/*
 * CPL_AND_CML has been removed.  Interrupt threads will eventually not
 * use either mechanism so there is no point trying to optimize it.
 */
#ifdef PUSHDOWN_LEVEL_3
#endif

/*
 * Send CPUSTOP IPI for stop/restart of other CPUs on DDB break.
 *
#define VERBOSE_CPUSTOP_ON_DDBBREAK
 */
#define CPUSTOP_ON_DDBBREAK

/*
 * Misc. counters.
 *
#define COUNT_XINVLTLB_HITS
 */

/**
 * Hack to "fake-out" kernel into thinking it is running on a 'default config'.
 *
 * value == default type
#define TEST_DEFAULT_CONFIG	6
 */

/*
 * Simple test code for IPI interaction, save for future...
 *
#define TEST_TEST1
#define IPI_TARGET_TEST1	1
 */

#endif	/* SMP */

#ifdef APIC_IO

/*
 * Portions of the old TEST_LOPRIO code, back from the grave!
 */
#define GRAB_LOPRIO

/*
 * Don't assume that slow interrupt handler X is called from vector
 * X + ICU_OFFSET.
 */
#define APIC_INTR_REORDER

/*
 * Redirect clock interrupts to a higher priority (fast intr) vector,
 * while still using the slow interrupt handler. Only effective when 
 * APIC_INTR_REORDER is defined.
 */
#define APIC_INTR_HIGHPRI_CLOCK

#endif /* APIC_IO */

#if 0	/* DEPRECATED */

/*
 * Address of POST hardware port.
 * Defining this enables POSTCODE macros.
 *
#define POST_ADDR		0x80
 */


/*
 * POST hardware macros.
 */
#ifdef POST_ADDR
#define ASMPOSTCODE_INC				\
	pushl	%eax ;				\
	movl	_current_postcode, %eax ;	\
	incl	%eax ;				\
	andl	$0xff, %eax ;			\
	movl	%eax, _current_postcode ;	\
	outb	%al, $POST_ADDR ;		\
	popl	%eax

/*
 * Overwrite the current_postcode value.
 */
#define ASMPOSTCODE(X)				\
	pushl	%eax ;				\
	movl	$X, %eax ;			\
	movl	%eax, _current_postcode ;	\
	outb	%al, $POST_ADDR ;		\
	popl	%eax

/*
 * Overwrite the current_postcode low nibble.
 */
#define ASMPOSTCODE_LO(X)			\
	pushl	%eax ;				\
	movl	_current_postcode, %eax ;	\
	andl	$0xf0, %eax ;			\
	orl	$X, %eax ;			\
	movl	%eax, _current_postcode ;	\
	outb	%al, $POST_ADDR ;		\
	popl	%eax

/*
 * Overwrite the current_postcode high nibble.
 */
#define ASMPOSTCODE_HI(X)			\
	pushl	%eax ;				\
	movl	_current_postcode, %eax ;	\
	andl	$0x0f, %eax ;			\
	orl	$(X<<4), %eax ;			\
	movl	%eax, _current_postcode ;	\
	outb	%al, $POST_ADDR ;		\
	popl	%eax
#else
#define ASMPOSTCODE_INC
#define ASMPOSTCODE(X)
#define ASMPOSTCODE_LO(X)
#define ASMPOSTCODE_HI(X)
#endif /* POST_ADDR */

#endif	/* DEPRECATED */

#if 0	/* DEPRECATED */
/*
 * These are all temps for debugging...
 *
#define GUARD_INTS
 */

/*
 * This macro traps unexpected INTs to a specific CPU, eg. GUARD_CPU.
 */
#ifdef GUARD_INTS
#define GUARD_CPU	1
#define MAYBE_PANIC(irq_num)		\
	cmpl	$GUARD_CPU, _cpuid ;	\
	jne	9f ;			\
	cmpl	$1, _ok_test1 ;		\
	jne	9f ;			\
	pushl	lapic_isr3 ;		\
	pushl	lapic_isr2 ;		\
	pushl	lapic_isr1 ;		\
	pushl	lapic_isr0 ;		\
	pushl	lapic_irr3 ;		\
	pushl	lapic_irr2 ;		\
	pushl	lapic_irr1 ;		\
	pushl	lapic_irr0 ;		\
	pushl	$irq_num ;		\
	pushl	_cpuid ;		\
	pushl	$panic_msg ;		\
	call	_printf ;		\
	addl	$44, %esp ;		\
9:
#else
#define MAYBE_PANIC(irq_num)
#endif /* GUARD_INTS */

#endif /* DEPRECATED */

#endif /* _MACHINE_SMPTESTS_H_ */
