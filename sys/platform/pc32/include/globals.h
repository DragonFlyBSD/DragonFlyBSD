/*-
 * Copyright (c) 1999 Luoqi Chen <luoqi@freebsd.org>
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
 * $FreeBSD: src/sys/i386/include/globals.h,v 1.5.2.1 2000/05/16 06:58:10 dillon Exp $
 * $DragonFly: src/sys/platform/pc32/include/Attic/globals.h,v 1.7 2003/06/20 02:09:54 dillon Exp $
 */

#ifndef	_MACHINE_GLOBALS_H_
#define	_MACHINE_GLOBALS_H_

#ifdef _KERNEL

/*
 * mycpu() retrieves the base of the current cpu's globaldata structure.
 * Note that it is *NOT* volatile, meaning that the value may be cached by
 * GCC.  We have to force a dummy memory reference so gcc does not cache
 * the gd pointer across a procedure call (which might block and cause us
 * to wakeup on a different cpu).
 *
 * Also note that in TurtleBSD a thread can be preempted, but it cannot
 * move to another cpu preemptively so the 'gd' pointer is good until you
 * block.
 */
#if defined(SMP) || defined(KLD_MODULE) || defined(ACTUALLY_LKM_NOT_KERNEL)

extern int __mycpu__dummy;

static __inline
struct globaldata *
_get_mycpu(void)
{
    struct globaldata *gd;

    __asm ("movl %%fs:globaldata,%0" : "=r" (gd) : "m"(__mycpu__dummy));
    return(gd);
}

#define mycpu	_get_mycpu()

#else

#define mycpu	(&CPU_prvspace[0].globaldata)

#endif

/*
 * note: curthread is never NULL, but curproc can be.  Also note that
 * in Turtle, the current pcb is stored in the thread structure.
 */
#define curthread	(mycpu->gd_curthread)
#define	curproc		(curthread->td_proc)


#endif	/* _KERNEL */

#endif	/* !_MACHINE_GLOBALS_H_ */
