/*-
 * Copyright (c) 1991 The Regents of the University of California.
 * Copyright (c) 2008 The DragonFly Project.
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
 * 3. Neither the name of the University nor the names of its contributors
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
 */

#ifndef _ARCH_INTR_MACHDEP_H_
#define	_ARCH_INTR_MACHDEP_H_

#ifndef LOCORE
#ifndef _SYS_TYPES_H_
#include <sys/types.h>
#endif
#endif

/*
 * Low level interrupt code.
 */ 

#ifdef _KERNEL

#define IDT_OFFSET		0x20
#define IDT_OFFSET_SYSCALL	0x80
#define IDT_OFFSET_IPI		0xe0

#define IDT_HWI_VECTORS		(IDT_OFFSET_IPI - IDT_OFFSET)

/*
 * Local APIC TPR priority vector levels:
 *
 *	0xff (255) +-------------+
 *		   |             | 15 (IPIs: Xcpustop, Xspuriousint)
 *	0xf0 (240) +-------------+
 *		   |             | 14 (IPIs: Xinvltlb, Xipiq, Xtimer, Xsniff)
 *	0xe0 (224) +-------------+
 *		   |             | 13
 *	0xd0 (208) +-------------+
 *		   |             | 12
 *	0xc0 (192) +-------------+
 *		   |             | 11
 *	0xb0 (176) +-------------+
 *		   |             | 10
 *	0xa0 (160) +-------------+
 *		   |             |  9
 *	0x90 (144) +-------------+
 *		   |             |  8 (syscall at 0x80)
 *	0x80 (128) +-------------+
 *		   |             |  7
 *	0x70 (112) +-------------+
 *		   |             |  6
 *	0x60 (96)  +-------------+
 *		   |             |  5
 *	0x50 (80)  +-------------+
 *		   |             |  4
 *	0x40 (64)  +-------------+
 *		   |             |  3
 *	0x30 (48)  +-------------+
 *		   |             |  2 (hardware INTs)
 *	0x20 (32)  +-------------+
 *		   |             |  1 (exceptions, traps, etc.)
 *	0x10 (16)  +-------------+
 *		   |             |  0 (exceptions, traps, etc.)
 *	0x00 (0)   +-------------+
 */
#define TPR_STEP		0x10

/* Local APIC Task Priority Register */
#define TPR_IPI			(IDT_OFFSET_IPI - 1)


/*
 * IPI group1
 */
#define IDT_OFFSET_IPIG1	IDT_OFFSET_IPI

/* TLB shootdowns */
#define XINVLTLB_OFFSET		(IDT_OFFSET_IPIG1 + 0)

/* IPI group1 1: unused (was inter-cpu clock handling) */
/* IPI group1 2: unused (was inter-cpu rendezvous) */

/* IPIQ rendezvous */
#define XIPIQ_OFFSET		(IDT_OFFSET_IPIG1 + 3)

/* TIMER rendezvous */
#define XTIMER_OFFSET		(IDT_OFFSET_IPIG1 + 4)

/* SNIFF rendezvous */
#define XSNIFF_OFFSET		(IDT_OFFSET_IPIG1 + 5)

/* IPI group1 6 ~ 15: unused */


/*
 * IPI group2
 */
#define IDT_OFFSET_IPIG2	(IDT_OFFSET_IPIG1 + TPR_STEP)

/* IPI to signal CPUs to stop and wait for another CPU to restart them */
#define XCPUSTOP_OFFSET		(IDT_OFFSET_IPIG2 + 0)

/* IPI group2 1 ~ 14: unused */

/* NOTE: this vector MUST be xxxx1111 */
#define XSPURIOUSINT_OFFSET	(IDT_OFFSET_IPIG2 + 15)

#ifndef	LOCORE

/*
 * Type of the first (asm) part of an interrupt handler.
 */
typedef void inthand_t(u_int cs, u_int ef, u_int esp, u_int ss);

#define	IDTVEC(name)	__CONCAT(X,name)

inthand_t
	Xspuriousint,	/* handle APIC "spurious INTs" */
	Xtimer;		/* handle per-cpu timer INT */

inthand_t
	Xinvltlb,	/* TLB shootdowns */
	Xcpustop,	/* CPU stops & waits for another CPU to restart it */
	Xipiq,		/* handle lwkt_send_ipiq() requests */
	Xsniff;		/* sniff CPU */

#endif /* LOCORE */

#endif /* _KERNEL */

#endif /* !_ARCH_INTR_MACHDEP_H_ */
