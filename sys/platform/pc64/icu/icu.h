/*-
 * Copyright (c) 1990 The Regents of the University of California.
 * Copyright (c) 2008 The DragonFly Project.
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
 *	from: @(#)icu.h	5.6 (Berkeley) 5/9/91
 * $FreeBSD: src/sys/i386/isa/icu.h,v 1.18 1999/12/26 12:43:47 bde Exp $
 * $DragonFly: src/sys/platform/pc64/icu/icu.h,v 1.1 2008/08/29 17:07:16 dillon Exp $
 */

/*
 * AT/386 Interrupt Control constants
 * W. Jolitz 8/89
 */

#ifndef _ARCH_ICU_ICU_H_
#define	_ARCH_ICU_ICU_H_

#define	ICU_IMR_OFFSET		1	/* IO_ICU{1,2} + 1 */

/*
 * Interrupt enable bit numbers - in normal order of priority 
 * (which we change)
 */
#define	ICU_IRQ0		0	/* highest priority - timer */
#define	ICU_IRQ1		1
#define	ICU_IRQ_SLAVE		2
#define	ICU_IRQ8		8
#define	ICU_IRQ9		9
#define	ICU_IRQ2		ICU_IRQ9
#define	ICU_IRQ10		10
#define	ICU_IRQ11		11
#define	ICU_IRQ12		12
#define	ICU_IRQ13		13
#define	ICU_IRQ14		14
#define	ICU_IRQ15		15
#define	ICU_IRQ3		3	/* this is highest after rotation */
#define	ICU_IRQ4		4
#define	ICU_IRQ5		5
#define	ICU_IRQ6		6
#define	ICU_IRQ7		7	/* lowest - parallel printer */

#endif /* !_ARCH_ICU_ICU_H_ */
