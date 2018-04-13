/*
 * Copyright (c) 2003,2004 The DragonFly Project.  All rights reserved.
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
 * $FreeBSD: src/sys/i386/include/lock.h,v 1.11.2.2 2000/09/30 02:49:34 ps Exp $
 */

#ifndef _MACHINE_LOCK_H_
#define _MACHINE_LOCK_H_

#ifndef _CPU_PSL_H_
#include <machine/psl.h>
#endif

#ifdef LOCORE

/*
 * Spinlock assembly support.  Note: rax and rcx can be tromped.  No
 * other register will be.   Note that these routines are sometimes
 * called with (%edx) as the mem argument.
 *
 * Under UP the spinlock routines still serve to disable/restore 
 * interrupts.
 */

#define SPIN_INIT(mem)						\
	movq	$0,mem ;					\

#define SPIN_INIT_NOREG(mem)					\
	SPIN_INIT(mem) ;					\

#define SPIN_LOCK(mem)						\
	pushfq ;						\
	popq	%rcx ;		/* flags */			\
	cli ;							\
	orq	$PSL_C,%rcx ;	/* make sure non-zero */	\
906: ;								\
	movq	mem, %rax ;					\
907: ;								\
	cmpq	$0,%rax ;					\
	jnz	908f ;						\
	lock cmpxchgq %rcx,mem ; /* Z=1 (jz) on success */	\
	jz	909f ; 						\
	pause ;							\
	jmp	907b ;						\
908: ;								\
	pause ;							\
	jmp	906b ;						\
909: ;								\

#define SPIN_LOCK_PUSH_REGS					\
	subq	$16,%rsp ;					\
	movq	%rcx,(%rsp) ;					\
	movq	%rax,8(%rsp) ;					\

#define SPIN_LOCK_POP_REGS					\
	movq	(%rsp),%rcx ;					\
	movq	8(%rsp),%rax ;					\
	addq	$16,%rsp ;					\

#define SPIN_LOCK_FRAME_SIZE	16

#define SPIN_LOCK_NOREG(mem)					\
	SPIN_LOCK_PUSH_REGS ;					\
	SPIN_LOCK(mem) ;					\
	SPIN_LOCK_POP_REGS ;					\

#define SPIN_UNLOCK(mem)					\
	pushq	mem ;						\
	movq	$0,mem ;					\
	popfq ;							\

#define SPIN_UNLOCK_PUSH_REGS
#define SPIN_UNLOCK_POP_REGS
#define SPIN_UNLOCK_FRAME_SIZE	0

#define SPIN_UNLOCK_NOREG(mem)					\
	SPIN_UNLOCK(mem) ;					\

#else	/* !LOCORE */

#ifdef _KERNEL

/*
 * Spinlock functions (UP and SMP).  Under UP a spinlock still serves
 * to disable/restore interrupts even if it doesn't spin.
 */
struct spinlock_deprecated {
	volatile long	opaque;
};

void	com_lock(void);		/* disables int / spinlock combo */
void	com_unlock(void);
void	imen_lock(void);	/* disables int / spinlock combo */
void	imen_unlock(void);
void	clock_lock(void);	/* disables int / spinlock combo */
void	clock_unlock(void);

void	spin_lock_deprecated(struct spinlock_deprecated *lock);
void	spin_unlock_deprecated(struct spinlock_deprecated *lock);

/*
 * Inline version of spinlock routines -- overrides assembly.  Only unlock
 * and init here please.
 */
static __inline void
spin_init_deprecated(struct spinlock_deprecated *lock)
{
	lock->opaque = 0;
}

#endif  /* _KERNEL */

#endif	/* LOCORE */
#endif	/* !_MACHINE_LOCK_H_ */
