/*
 * Copyright (c) 2006 The DragonFly Project.  All rights reserved.
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
 * $DragonFly: src/sys/platform/vkernel/include/cpufunc.h,v 1.2 2007/02/18 14:28:18 corecode Exp $
 */
#ifndef _MACHINE_CPUFUNC_H_
#define	_MACHINE_CPUFUNC_H_

#ifdef _KERNEL

/*
 * First declare our overriding functions.  We have to do this to prevent
 * cpu/cpufunc.h to define inline assembler versions.  However, we need
 * cpu/cpufunc.h to define other functions like ``ffs'', which will otherwise
 * be defined by libkern (via sys/systm.h).  This is why the order needs to be:
 *
 * 1. Declare our overrides
 * 2. include cpu/cpufunc.h
 * 3. include the remaining needed headers for our overrides
 */

#define _CPU_ENABLE_INTR_DEFINED
#define _CPU_DISABLE_INTR_DEFINED
#define _CPU_INVLPG_DEFINED
#define _CPU_INVLTLB_DEFINED

void cpu_disable_intr(void);
void cpu_enable_intr(void);
void cpu_invlpg(void *addr);
void cpu_invltlb(void);

#endif

#include <cpu/cpufunc.h>

#ifdef _KERNEL

#include <sys/types.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <vm/pmap.h>

#include <sys/mman.h>
#include <signal.h>

#endif /* _KERNEL */

#endif /* !_MACHINE_CPUFUNC_H_ */
