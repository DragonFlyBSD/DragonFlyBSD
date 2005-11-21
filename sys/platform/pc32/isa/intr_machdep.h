/*-
 * Copyright (c) 1991 The Regents of the University of California.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD: src/sys/i386/isa/intr_machdep.h,v 1.19.2.2 2001/10/14 20:05:50 luigi Exp $
 * $DragonFly: src/sys/platform/pc32/isa/intr_machdep.h,v 1.24 2005/11/21 18:02:42 dillon Exp $
 */

#ifndef _I386_ISA_INTR_MACHDEP_H_
#define	_I386_ISA_INTR_MACHDEP_H_

#ifndef LOCORE
#ifndef _SYS_INTERRUPT_H_
#include <sys/interrupt.h>
#endif
#ifndef _SYS_SERIALIZE_H_
#include <sys/serialize.h>
#endif
#endif

/*
 * Low level interrupt code.
 */ 

#ifdef _KERNEL

#define IDT_OFFSET	32

#if defined(SMP)
/*
 * XXX FIXME: rethink location for all IPI vectors.
 */

/*
    APIC TPR priority vector levels:

	0xff (255) +-------------+
		   |             | 15 (IPIs: Xspuriousint)
	0xf0 (240) +-------------+
		   |             | 14
	0xe0 (224) +-------------+
		   |             | 13
	0xd0 (208) +-------------+
		   |             | 12
	0xc0 (192) +-------------+
		   |             | 11
	0xb0 (176) +-------------+
		   |             | 10 (IPIs: Xcpustop)
	0xa0 (160) +-------------+
		   |             |  9 (IPIs: Xinvltlb)
	0x90 (144) +-------------+
		   |             |  8 (linux/BSD syscall, IGNORE FAST HW INTS)
	0x80 (128) +-------------+
		   |             |  7 (FAST_INTR 16-23)
	0x70 (112) +-------------+
		   |             |  6 (FAST_INTR 0-15)
	0x60 (96)  +-------------+
		   |             |  5 (IGNORE HW INTS)
	0x50 (80)  +-------------+
		   |             |  4 (2nd IO APIC)
	0x40 (64)  +------+------+
		   |      |      |  3 (upper APIC hardware INTs: PCI)
	0x30 (48)  +------+------+
		   |             |  2 (start of hardware INTs: ISA)
	0x20 (32)  +-------------+
		   |             |  1 (exceptions, traps, etc.)
	0x10 (16)  +-------------+
		   |             |  0 (exceptions, traps, etc.)
	0x00 (0)   +-------------+
 */

/* blocking values for local APIC Task Priority Register */
#define TPR_BLOCK_HWI		0x4f		/* hardware INTs */
#define TPR_IGNORE_HWI		0x5f		/* ignore INTs */
#define TPR_BLOCK_FHWI		0x7f		/* hardware FAST INTs */
#define TPR_IGNORE_FHWI		0x8f		/* ignore FAST INTs */
#define TPR_IPI_ONLY		0x8f		/* ignore FAST INTs */
#define TPR_BLOCK_XINVLTLB	0x9f		/*  */
#define TPR_BLOCK_XCPUSTOP	0xaf		/*  */
#define TPR_BLOCK_ALL		0xff		/* all INTs */


/* TLB shootdowns */
#define XINVLTLB_OFFSET		(IDT_OFFSET + 112)

/* unused/open (was inter-cpu clock handling) */
#define XUNUSED113_OFFSET	(IDT_OFFSET + 113)

/* inter-CPU rendezvous */
#define XUNUSED114_OFFSET	(IDT_OFFSET + 114)

/* IPIQ rendezvous */
#define XIPIQ_OFFSET		(IDT_OFFSET + 115)

/* IPI to signal CPUs to stop and wait for another CPU to restart them */
#define XCPUSTOP_OFFSET		(IDT_OFFSET + 128)

/*
 * Note: this vector MUST be xxxx1111, 32 + 223 = 255 = 0xff:
 */
#define XSPURIOUSINT_OFFSET	(IDT_OFFSET + 223)

#endif /* SMP */

#ifndef	LOCORE

/*
 * Type of the first (asm) part of an interrupt handler.
 */
typedef void inthand_t(u_int cs, u_int ef, u_int esp, u_int ss);
typedef void unpendhand_t(void);

#define	IDTVEC(name)	__CONCAT(X,name)

#if defined(SMP)
inthand_t
	Xinvltlb,	/* TLB shootdowns */
	Xcpuast,	/* Additional software trap on other cpu */ 
	Xforward_irq,	/* Forward irq to cpu holding ISR lock */
	Xcpustop,	/* CPU stops & waits for another CPU to restart it */
	Xspuriousint,	/* handle APIC "spurious INTs" */
	Xipiq;		/* handle lwkt_send_ipiq() requests */
#endif /* SMP */

void	call_fast_unpend(int irq);
void	isa_defaultirq (void);
int	isa_nmi (int cd);
void	icu_reinit (void);

#endif /* LOCORE */

#endif /* _KERNEL */

#endif /* !_I386_ISA_INTR_MACHDEP_H_ */
