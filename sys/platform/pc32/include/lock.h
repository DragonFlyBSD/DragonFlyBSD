/*
 * Copyright (c) 2003, Matthew Dillon, All rights reserved.
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
 * $FreeBSD: src/sys/i386/include/lock.h,v 1.11.2.2 2000/09/30 02:49:34 ps Exp $
 * $DragonFly: src/sys/platform/pc32/include/lock.h,v 1.4 2003/07/08 06:27:27 dillon Exp $
 */

#ifndef _MACHINE_LOCK_H_
#define _MACHINE_LOCK_H_

#ifndef _MACHINE_PSL_H_
#include <machine/psl.h>
#endif

/*
 * MP_FREE_LOCK is used by both assembly and C under SMP.
 */
#ifdef SMP
#define MP_FREE_LOCK		0xffffffff	/* value of lock when free */
#endif

#ifdef LOCORE

/*
 * Spinlock assembly support.  Note: eax and ecx can be tromped.  No
 * other register will be.   Note that these routines are sometimes
 * called with (%edx) as the mem argument.
 *
 * Under UP the spinlock routines still serve to disable/restore 
 * interrupts.
 */


#ifdef SMP

#define SPIN_INIT(mem)						\
	movl	$0,mem ;					\

#define SPIN_INIT_NOREG(mem)					\
	SPIN_INIT(mem) ;					\

#define SPIN_LOCK(mem)						\
	pushfl ;						\
	popl	%ecx ;		/* flags */			\
	cli ;							\
	orl	$PSL_C,%ecx ;	/* make sure non-zero */	\
7: ;								\
	movl	$0,%eax ;	/* expected contents of lock */	\
	lock cmpxchgl %ecx,mem ; /* Z=1 (jz) on success */	\
	jnz	7b ; 						\

#define SPIN_LOCK_PUSH_REGS					\
	subl	$8,%esp ;					\
	movl	%ecx,(%esp) ;					\
	movl	%eax,4(%esp) ;					\

#define SPIN_LOCK_POP_REGS					\
	movl	(%esp),%ecx ;					\
	movl	4(%esp),%eax ;					\
	addl	$8,%esp ;					\

#define SPIN_LOCK_FRAME_SIZE	8

#define SPIN_LOCK_NOREG(mem)					\
	SPIN_LOCK_PUSH_REGS ;					\
	SPIN_LOCK(mem) ;					\
	SPIN_LOCK_POP_REGS ;					\

#define SPIN_UNLOCK(mem)					\
	pushl	mem ;						\
	movl	$0,mem ;					\
	popfl ;							\

#define SPIN_UNLOCK_PUSH_REGS
#define SPIN_UNLOCK_POP_REGS
#define SPIN_UNLOCK_FRAME_SIZE	0

#define SPIN_UNLOCK_NOREG(mem)					\
	SPIN_UNLOCK(mem) ;					\

#else

#define SPIN_LOCK(mem)						\
	pushfl ;						\
	cli ;							\
	orl	$PSL_C,(%esp) ;					\
	popl	mem ;						\

#define SPIN_LOCK_PUSH_RESG
#define SPIN_LOCK_POP_REGS
#define SPIN_LOCK_FRAME_SIZE	0

#define SPIN_UNLOCK(mem)					\
	pushl	mem ;						\
	movl	$0,mem ;					\
	popfl ;							\

#define SPIN_UNLOCK_PUSH_REGS
#define SPIN_UNLOCK_POP_REGS
#define SPIN_UNLOCK_FRAME_SIZE	0

#endif	/* SMP */

#else	/* LOCORE */

/*
 * Spinlock functions (UP and SMP).  Under UP a spinlock still serves
 * to disable/restore interrupts even if it doesn't spin.
 */
struct spinlock {
	volatile int	opaque;
};

typedef struct spinlock *spinlock_t;

void	mpintr_lock(void);	/* disables int / spinlock combo */
void	mpintr_unlock(void);
void	com_lock(void);		/* disables int / spinlock combo */
void	com_unlock(void);
void	imen_lock(void);	/* disables int / spinlock combo */
void	imen_unlock(void);
void	clock_lock(void);	/* disables int / spinlock combo */
void	clock_unlock(void);
void	cons_lock(void);	/* disables int / spinlock combo */
void	cons_unlock(void);

extern struct spinlock smp_rv_spinlock;

void	spin_lock(spinlock_t lock);
void	spin_lock_np(spinlock_t lock);
void	spin_unlock(spinlock_t lock);
void	spin_unlock_np(spinlock_t lock);
#if 0
void	spin_lock_init(spinlock_t lock);
#endif

/*
 * Inline version of spinlock routines -- overrides assembly.  Only unlock
 * and init here please.
 */
static __inline void
spin_lock_init(spinlock_t lock)
{
	lock->opaque = 0;
}

/*
 * MP LOCK functions for SMP and UP.  Under UP the MP lock does not exist
 * but we leave a few functions intact as macros for convenience.
 */
#ifdef SMP

void	get_mplock(void);
int	try_mplock(void);
void	rel_mplock(void);
int	cpu_try_mplock(void);
#if 0
void	cpu_rel_mplock(void);
#endif
void	cpu_get_initial_mplock(void);

extern u_int	mp_lock;

#define MP_LOCK_HELD()   (mp_lock == mycpu->gd_cpuid)
#define ASSERT_MP_LOCK_HELD()   KKASSERT(MP_LOCK_HELD())

static __inline void
cpu_rel_mplock(void)
{
	mp_lock = MP_FREE_LOCK;
}

#else

#define get_mplock()
#define try_mplock()	1
#define rel_mplock()
#define ASSERT_MP_LOCK_HELD()

#endif	/* SMP */
#endif	/* LOCORE */
#endif	/* !_MACHINE_LOCK_H_ */
