/*-
 * Copyright (c) 1997, by Steve Passe
 * Copyright (c) 2008 The DragonFly Project.
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
 * $FreeBSD: src/sys/i386/isa/apic_ipl.h,v 1.3 1999/08/28 00:44:36 peter Exp $
 * $DragonFly: src/sys/platform/pc64/apic/apic_ipl.h,v 1.1 2008/08/29 17:07:12 dillon Exp $
 */

#ifndef _ARCH_APIC_IOAPIC_IPL_H_
#define	_ARCH_APIC_IOAPIC_IPL_H_

#ifdef LOCORE

/*
 * Interrupts may or may not be disabled when using these functions.
 */
#define IOAPIC_IMASK_LOCK						\
        SPIN_LOCK(imen_spinlock) ;					\

#define IOAPIC_IMASK_UNLOCK						\
        SPIN_UNLOCK(imen_spinlock) ;					\

#endif

#endif /* !_ARCH_APIC_IOAPIC_IPL_H_ */
