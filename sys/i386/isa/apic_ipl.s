/*-
 * Copyright (c) 1997, by Steve Passe,  All rights reserved.
 * Copyright (c) 2003, by Matthew Dillon,  All rights reserved.
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
 * $FreeBSD: src/sys/i386/isa/apic_ipl.s,v 1.27.2.2 2000/09/30 02:49:35 ps Exp $
 * $DragonFly: src/sys/i386/isa/Attic/apic_ipl.s,v 1.7 2003/07/06 21:23:49 dillon Exp $
 */

	.data
	ALIGN_DATA

	/*
	 * Interrupt mask for APIC interrupts, defaults to all hardware
	 * interrupts turned off.
	 */

	.p2align 2				/* MUST be 32bit aligned */

	.globl apic_imen
apic_imen:
	.long	HWI_MASK

	.text
	SUPERALIGN_TEXT

	/*
	 * Functions to enable and disable a hardware interrupt.  Generally
	 * called with only one bit set in the mask but can handle multiple
	 * bits to present the same API as the ICU.
	 */

ENTRY(INTRDIS)
	IMASK_LOCK			/* enter critical reg */
	movl	4(%esp),%eax
1:
	bsfl	%eax,%ecx
	jz	2f
	btrl	%ecx,%eax
	btsl	%ecx, apic_imen
	shll	$4, %ecx
	movl	CNAME(int_to_apicintpin) + 8(%ecx), %edx
	movl	CNAME(int_to_apicintpin) + 12(%ecx), %ecx
	testl	%edx, %edx
	jz	2f
	movl	%ecx, (%edx)		/* target register index */
	orl	$IOART_INTMASK,16(%edx)	/* set intmask in target apic reg */
	jmp	1b
2:
	IMASK_UNLOCK			/* exit critical reg */
	ret

ENTRY(INTREN)
	IMASK_LOCK			/* enter critical reg */
	movl	4(%esp), %eax		/* mask into %eax */
1:
	bsfl	%eax, %ecx		/* get pin index */
	jz	2f
	btrl	%ecx,%eax
	btrl	%ecx, apic_imen		/* update apic_imen */
	shll	$4, %ecx
	movl	CNAME(int_to_apicintpin) + 8(%ecx), %edx
	movl	CNAME(int_to_apicintpin) + 12(%ecx), %ecx
	testl	%edx, %edx
	jz	2f
	movl	%ecx, (%edx)		/* write the target register index */
	andl	$~IOART_INTMASK, 16(%edx) /* clear mask bit */
	jmp	1b
2:	
	IMASK_UNLOCK			/* exit critical reg */
	ret

/******************************************************************************
 * 
 */

/*
 * u_int io_apic_write(int apic, int select);
 */
ENTRY(io_apic_read)
	movl	4(%esp), %ecx		/* APIC # */
	movl	ioapic, %eax
	movl	(%eax,%ecx,4), %edx	/* APIC base register address */
	movl	8(%esp), %eax		/* target register index */
	movl	%eax, (%edx)		/* write the target register index */
	movl	16(%edx), %eax		/* read the APIC register data */
	ret				/* %eax = register value */

/*
 * void io_apic_write(int apic, int select, int value);
 */
ENTRY(io_apic_write)
	movl	4(%esp), %ecx		/* APIC # */
	movl	ioapic, %eax
	movl	(%eax,%ecx,4), %edx	/* APIC base register address */
	movl	8(%esp), %eax		/* target register index */
	movl	%eax, (%edx)		/* write the target register index */
	movl	12(%esp), %eax		/* target register value */
	movl	%eax, 16(%edx)		/* write the APIC register data */
	ret				/* %eax = void */

/*
 * Send an EOI to the local APIC.
 */
ENTRY(apic_eoi)
	movl	$0, lapic+0xb0
	ret

