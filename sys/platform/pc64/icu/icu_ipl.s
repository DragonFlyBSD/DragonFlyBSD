/*
 * Copyright (c) 2003,2004,2008 The DragonFly Project.  All rights reserved.
 * 
 * This code is derived from software contributed to The DragonFly Project
 * by Matthew Dillon <dillon@backplane.com>
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name of The DragonFly Project nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific, prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 * 
 * Copyright (c) 1989, 1990 William F. Jolitz.
 * Copyright (c) 1990 The Regents of the University of California.
 * All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * William Jolitz.
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
 * $FreeBSD: src/sys/i386/isa/icu_ipl.s,v 1.6 1999/08/28 00:44:42 peter Exp $
 * $DragonFly: src/sys/platform/pc64/icu/icu_ipl.s,v 1.1 2008/08/29 17:07:16 dillon Exp $
 */

#include <machine/asmacros.h>
#include <machine/segments.h>
#include <machine/lock.h>
#include <machine/psl.h>
#include <machine/trap.h>
#include <machine_base/icu/icu.h>
#include <machine_base/icu/icu_ipl.h>

#include <bus/isa/isareg.h>

#include "assym.s"

/*
 * WARNING!  SMP builds can use the ICU now so this code must be MP safe.
 */

	.data
	ALIGN_DATA

	/*
	 * Interrupt mask for ICU interrupts, defaults to all hardware
	 * interrupts turned off.
	 */
	.p2align 2			/* MUST be 32bit aligned */

	.globl	icu_imen
icu_imen:
	.long	ICU_HWI_MASK

	.text
	SUPERALIGN_TEXT

	/*
	 * Functions to enable and disable a hardware interrupt.  Only
	 * 16 ICU interrupts exist.
	 *
	 * INTREN(irq:%edi)
	 * INTRDIS(irq:%edi)
	 */
ENTRY(ICU_INTRDIS)
	ICU_IMASK_LOCK
	cmpl	$8,%edi
	jl	1f
	movl	%edi,%eax	/* C argument */
	btsl	%eax,icu_imen
	movl	icu_imen,%eax
	mov	%ah,%al
	outb	%al,$IO_ICU2+ICU_IMR_OFFSET
	ICU_IMASK_UNLOCK
	ret
1:
	movl	%edi,%eax	/* C argument */
	btsl	%eax,icu_imen
	movl	icu_imen,%eax
	outb	%al,$IO_ICU1+ICU_IMR_OFFSET
	ICU_IMASK_UNLOCK
	ret

ENTRY(ICU_INTREN)
	ICU_IMASK_LOCK
	cmpl	$8,%edi
	jl	1f
	movl	%edi,%eax	/* C argument */
	btrl	%eax,icu_imen
	movl	icu_imen,%eax
	mov	%ah,%al
	outb	%al,$IO_ICU2+ICU_IMR_OFFSET
	ICU_IMASK_UNLOCK
	ret
1:
	movl	%edi,%eax	/* C argument */
	btrl	%eax,icu_imen
	movl	icu_imen,%eax
	outb	%al,$IO_ICU1+ICU_IMR_OFFSET
	ICU_IMASK_UNLOCK
	ret
