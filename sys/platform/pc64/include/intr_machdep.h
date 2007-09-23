/*-
 * Copyright (c) 2003 John Baldwin <jhb@FreeBSD.org>
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
 * $FreeBSD: src/sys/amd64/include/intr_machdep.h,v 1.18 2007/05/08 21:29:13 jhb Exp $
 * $DragonFly: src/sys/platform/pc64/include/intr_machdep.h,v 1.1 2007/09/23 04:42:07 yanyh Exp $
 */

#ifndef __MACHINE_INTR_MACHDEP_H__
#define	__MACHINE_INTR_MACHDEP_H__

#ifdef _KERNEL

/*
 * The maximum number of I/O interrupts we allow.  This number is rather
 * arbitrary as it is just the maximum IRQ resource value.  The interrupt
 * source for a given IRQ maps that I/O interrupt to device interrupt
 * source whether it be a pin on an interrupt controller or an MSI interrupt.
 * The 16 ISA IRQs are assigned fixed IDT vectors, but all other device
 * interrupts allocate IDT vectors on demand.  Currently we have 191 IDT
 * vectors available for device interrupts.  On many systems with I/O APICs,
 * a lot of the IRQs are not used, so this number can be much larger than
 * 191 and still be safe since only interrupt sources in actual use will
 * allocate IDT vectors.
 *
 * The first 255 IRQs (0 - 254) are reserved for ISA IRQs and PCI intline IRQs.
 * IRQ values beyond 256 are used by MSI.  We leave 255 unused to avoid
 * confusion since 255 is used in PCI to indicate an invalid IRQ.
 */
#define	NUM_MSI_INTS	128
#define	FIRST_MSI_INT	256
#define	NUM_IO_INTS	(FIRST_MSI_INT + NUM_MSI_INTS)

/*
 * Default base address for MSI messages on x86 platforms.
 */
#define	MSI_INTEL_ADDR_BASE		0xfee00000

/*
 * - 1 ??? dummy counter.
 * - 2 counters for each I/O interrupt.
 * - 1 counter for each CPU for lapic timer.
 * - 7 counters for each CPU for IPI counters for SMP.
 */
#ifdef SMP
#define	INTRCNT_COUNT	(1 + NUM_IO_INTS * 2 + (1 + 7) * MAXCPU)
#else
#define	INTRCNT_COUNT	(1 + NUM_IO_INTS * 2 + 1)
#endif

#endif	/* _KERNEL */
#endif	/* !__MACHINE_INTR_MACHDEP_H__ */
