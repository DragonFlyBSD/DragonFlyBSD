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
 */

#ifndef	_MACHINE_THREAD_H_
#define	_MACHINE_THREAD_H_

#include <machine/vframe.h>
#include <machine/npx.h>

struct md_thread {
    unsigned int	mtd_unused;	/* used to be mtd_cpl */
    union savefpu	*mtd_savefpu;	/* pointer to current fpu context */
    struct vextframe	mtd_savevext;
};

#ifdef _KERNEL

#define td_savefpu	td_mach.mtd_savefpu
#define td_tls		td_mach.mtd_savevext.vx_tls
#define td_savevext	td_mach.mtd_savevext

/*
 * mycpu() retrieves the base of the current cpu's globaldata structure.
 * Note that it is *NOT* volatile, meaning that the value may be cached by
 * GCC.  We have to force a dummy memory reference so gcc does not cache
 * the gd pointer across a procedure call (which might block and cause us
 * to wakeup on a different cpu).
 *
 * Also note that in DragonFly a thread can be preempted, but only by an
 * interrupt thread and the original thread will resume after the
 * interrupt thread finishes or blocks.  A thread cannot move to another
 * cpu preemptively or at all, in fact, while you are in the kernel, even
 * if you block.
 */

struct globaldata;

extern int __mycpu__dummy;

static __inline
struct globaldata *
_get_mycpu(void)
{
    struct globaldata *gd;

    __asm ("movq %%gs:globaldata,%0" : "=r" (gd) : "m"(__mycpu__dummy));
    return(gd);
}

#define mycpu  	_get_mycpu()

#ifdef SMP
#define	mycpuid	(_get_mycpu()->gd_cpuid)
#else
#define	mycpuid	0
#endif

/*
 * note: curthread is never NULL, but curproc can be.  Also note that
 * that only processes really use the PCB.  Threads fill in some fields
 * but mostly store contextual data on the stack and do not use (much of)
 * the PCB.
 */
#define curthread	mycpu->gd_curthread
#define	curproc		curthread->td_proc

#endif	/* _KERNEL */

#endif	/* !_MACHINE_THREAD_H_ */
