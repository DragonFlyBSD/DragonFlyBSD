/*-
 * Copyright (c) 1997 Bruce Evans.
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
 * $FreeBSD: src/sys/i386/isa/icu_ipl.h,v 1.3 1999/08/28 00:44:42 peter Exp $
 * $DragonFly: src/sys/platform/pc64/icu/icu_ipl.h,v 1.1 2008/08/29 17:07:16 dillon Exp $
 */

#ifndef _ARCH_ICU_ICU_IPL_H_
#define	_ARCH_ICU_ICU_IPL_H_

#include <machine_base/isa/isa_intr.h>

#define ICU_HWI_VECTORS	ISA_IRQ_CNT
#define ICU_HWI_MASK	((1 << ICU_HWI_VECTORS) - 1)

#ifdef LOCORE

/*
 * SMP interrupt mask protection.  The first version is used
 * when interrupts might not be disabled, the second version is
 * used when interrupts are disabled.
 */

#define ICU_IMASK_LOCK							\
        SPIN_LOCK(imen_spinlock) ;					\

#define ICU_IMASK_UNLOCK						\
        SPIN_UNLOCK(imen_spinlock) ;					\

#endif	/* LOCORE */

#endif /* !_ARCH_ICU_ICU_IPL_H_ */
