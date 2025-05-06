/*-
 * Copyright (c) 1998 Doug Rabson
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
 * $FreeBSD: src/sys/i386/include/atomic.h,v 1.9.2.1 2000/07/07 00:38:47 obrien Exp $
 */
#ifndef _CPU_ATOMIC_H_
#define _CPU_ATOMIC_H_

#include <sys/types.h>
#include <sys/atomic_common.h>

/*
 * Various simple arithmetic on memory which is atomic in the presence
 * of interrupts and multiple processors.
 *
 * atomic_set_char(P, V)	(*(u_char*)(P) |= (V))
 * atomic_clear_char(P, V)	(*(u_char*)(P) &= ~(V))
 * atomic_add_char(P, V)	(*(u_char*)(P) += (V))
 * atomic_subtract_char(P, V)	(*(u_char*)(P) -= (V))
 *
 * atomic_set_short(P, V)	(*(u_short*)(P) |= (V))
 * atomic_clear_short(P, V)	(*(u_short*)(P) &= ~(V))
 * atomic_add_short(P, V)	(*(u_short*)(P) += (V))
 * atomic_subtract_short(P, V)	(*(u_short*)(P) -= (V))
 *
 * atomic_set_int(P, V)		(*(u_int*)(P) |= (V))
 * atomic_clear_int(P, V)	(*(u_int*)(P) &= ~(V))
 * atomic_add_int(P, V)		(*(u_int*)(P) += (V))
 * atomic_subtract_int(P, V)	(*(u_int*)(P) -= (V))
 *
 * atomic_set_long(P, V)	(*(u_long*)(P) |= (V))
 * atomic_clear_long(P, V)	(*(u_long*)(P) &= ~(V))
 * atomic_add_long(P, V)	(*(u_long*)(P) += (V))
 * atomic_subtract_long(P, V)	(*(u_long*)(P) -= (V))
 * atomic_readandclear_long(P)	(return (*(u_long*)(P)); *(u_long*)(P) = 0;)
 * atomic_readandclear_int(P)	(return (*(u_int*)(P)); *(u_int*)(P) = 0;)
 */

/*
 * locked bus cycle
 * lock elision (backwards compatible)
 */
#define MPLOCKED	"lock ; "
#define XACQUIRE	"repne; "	/* lock elision */
#define XRELEASE	"repe; "	/* lock elision */

/*
 * The assembly is volatilized to demark potential before-and-after side
 * effects if an interrupt or SMP collision were to occur.  The primary
 * atomic instructions are MP safe, the nonlocked instructions are
 * local-interrupt-safe (so we don't depend on C 'X |= Y' generating an
 * atomic instruction).
 *
 * +m - memory is read and written (=m - memory is only written)
 * iq - integer constant or %ax/%bx/%cx/%dx (ir = int constant or any reg)
 *	(Note: byte instructions only work on %ax,%bx,%cx, or %dx).  iq
 *	is good enough for our needs so don't get fancy.
 * r  - any register.
 *
 * NOTE: 64-bit immediate values are not supported for most x86-64
 *	 instructions so we have to use "r".
 */

/* egcs 1.1.2+ version */
#define ATOMIC_ASM(NAME, TYPE, OP, CONS, V)		\
static __inline void					\
atomic_##NAME##_##TYPE(volatile u_##TYPE *p, u_##TYPE v)\
{							\
	__asm __volatile(MPLOCKED OP			\
			 : "+m" (*p)			\
			 : CONS (V)); 			\
}							\
static __inline void					\
atomic_##NAME##_##TYPE##_xacquire(volatile u_##TYPE *p, u_##TYPE v)\
{							\
	__asm __volatile(XACQUIRE MPLOCKED OP		\
			 : "+m" (*p)			\
			 : CONS (V)); 			\
}							\
static __inline void					\
atomic_##NAME##_##TYPE##_xrelease(volatile u_##TYPE *p, u_##TYPE v)\
{							\
	__asm __volatile(XRELEASE MPLOCKED OP		\
			 : "+m" (*p)			\
			 : CONS (V)); 			\
}							\
static __inline void					\
atomic_##NAME##_##TYPE##_nonlocked(volatile u_##TYPE *p, u_##TYPE v)\
{							\
	__asm __volatile(OP				\
			 : "+m" (*p)			\
			 : CONS (V)); 			\
}

/* egcs 1.1.2+ version */
ATOMIC_ASM(set,      char,  "orb %b1,%0",  "iq",   v)
ATOMIC_ASM(clear,    char,  "andb %b1,%0", "iq",  ~v)
ATOMIC_ASM(add,      char,  "addb %b1,%0", "iq",   v)
ATOMIC_ASM(subtract, char,  "subb %b1,%0", "iq",   v)

ATOMIC_ASM(set,      short, "orw %w1,%0",  "iq",   v)
ATOMIC_ASM(clear,    short, "andw %w1,%0", "iq",  ~v)
ATOMIC_ASM(add,      short, "addw %w1,%0", "iq",   v)
ATOMIC_ASM(subtract, short, "subw %w1,%0", "iq",   v)

ATOMIC_ASM(set,      int,   "orl %1,%0",  "iq",   v)
ATOMIC_ASM(clear,    int,   "andl %1,%0", "iq",  ~v)
ATOMIC_ASM(add,      int,   "addl %1,%0", "iq",   v)
ATOMIC_ASM(subtract, int,   "subl %1,%0", "iq",   v)

ATOMIC_ASM(set,      long,  "orq %1,%0",  "r",   v)
ATOMIC_ASM(clear,    long,  "andq %1,%0", "r",  ~v)
ATOMIC_ASM(add,      long,  "addq %1,%0", "r",   v)
ATOMIC_ASM(subtract, long,  "subq %1,%0", "r",   v)

static __inline u_long
atomic_readandclear_long(volatile u_long *addr)
{
	u_long res;

	res = 0;
	__asm __volatile(
	"	xchgq	%1,%0 ;		"
	"# atomic_readandclear_long"
	: "+r" (res),			/* 0 */
	  "=m" (*addr)			/* 1 */
	: "m" (*addr));

	return (res);
}

static __inline u_int
atomic_readandclear_int(volatile u_int *addr)
{
	u_int res;

	res = 0;
	__asm __volatile(
	"	xchgl	%1,%0 ;		"
	"# atomic_readandclear_int"
	: "+r" (res),			/* 0 */
	  "=m" (*addr)			/* 1 */
	: "m" (*addr));

	return (res);
}

/*
 * atomic_poll_acquire_int(P)	Returns non-zero on success, 0 if the lock
 *				has already been acquired.
 * atomic_poll_release_int(P)
 *
 * These are used for IPIQ interlocks between CPUs.
 * Both the acquisition and release must be cache-synchronizing instructions.
 */

static __inline int
atomic_swap_int(volatile int *addr, int value)
{
	__asm __volatile("xchgl %0, %1" :
	    "=r" (value), "=m" (*addr) : "0" (value) : "memory");
	return (value);
}

static __inline long
atomic_swap_long(volatile long *addr, long value)
{
	__asm __volatile("xchgq %0, %1" :
	    "=r" (value), "=m" (*addr) : "0" (value) : "memory");
	return (value);
}

static __inline void *
atomic_swap_ptr(volatile void **addr, void *value)
{
	__asm __volatile("xchgq %0, %1" :
	    "=r" (value), "=m" (*addr) : "0" (value) : "memory");
	return (value);
}

static __inline int
atomic_poll_acquire_int(volatile u_int *p)
{
	u_int data;

	__asm __volatile(MPLOCKED "btsl $0,%0; setnc %%al; andl $255,%%eax" : "+m" (*p), "=a" (data));
	return(data);
}

static __inline void
atomic_poll_release_int(volatile u_int *p)
{
	__asm __volatile(MPLOCKED "btrl $0,%0" : "+m" (*p));
}

/*
 * These functions operate on a 32 bit interrupt interlock which is defined
 * as follows:
 *
 *	bit 0-29	interrupt handler wait counter
 *	bit 30		interrupt handler disabled bit
 *	bit 31		interrupt handler currently running bit (1 = run)
 *
 * atomic_intr_cond_test(P)	Determine if the interlock is in an
 *				acquired state.  Returns 0 if it not
 *				acquired, non-zero if it is. (not MPLOCKed)
 *
 * atomic_intr_cond_try(P) 	Attempt to set bit 31 to acquire the
 *				interlock.  If we are unable to set bit 31
 *				we return 1, otherwise we return 0.
 *
 * atomic_intr_cond_enter(P, func, arg)
 *				Attempt to set bit 31 to acquire the
 *				interlock.  If we are unable to set bit 31,
 *				the wait is incremented counter and func(arg)
 *				is called in a loop until we are able to set
 *				bit 31.  Once we set bit 31, wait counter
 *				is decremented.
 *
 * atomic_intr_cond_exit(P, func, arg)
 *				Clear bit 31.  If the wait counter is still
 *				non-zero call func(arg) once.
 *
 * atomic_intr_handler_disable(P)
 *				Set bit 30, indicating that the interrupt
 *				handler has been disabled.  Must be called
 *				after the hardware is disabled.
 *
 *				Returns bit 31 indicating whether a serialized
 *				accessor is active (typically the interrupt
 *				handler is running).  0 == not active,
 *				non-zero == active.
 *
 * atomic_intr_handler_enable(P)
 *				Clear bit 30, indicating that the interrupt
 *				handler has been enabled.  Must be called
 *				before the hardware is actually enabled.
 *
 * atomic_intr_handler_is_enabled(P)
 *				Returns bit 30, 0 indicates that the handler
 *				is enabled, non-zero indicates that it is
 *				disabled.  The request counter portion of
 *				the field is ignored. (not MPLOCKed)
 *
 * atomic_intr_cond_inc(P)	Increment wait counter by 1.
 * atomic_intr_cond_dec(P)	Decrement wait counter by 1.
 */

static __inline void
atomic_intr_init(__atomic_intr_t *p)
{
	*p = 0;
}

static __inline int
atomic_intr_handler_disable(__atomic_intr_t *p)
{
	int data;

	__asm __volatile(MPLOCKED "orl $0x40000000,%1; movl %1,%%eax; " \
				  "andl $0x80000000,%%eax" \
				  : "=a"(data) , "+m"(*p));
	return(data);
}

static __inline void
atomic_intr_handler_enable(__atomic_intr_t *p)
{
	__asm __volatile(MPLOCKED "andl $0xBFFFFFFF,%0" : "+m" (*p));
}

static __inline int
atomic_intr_handler_is_enabled(__atomic_intr_t *p)
{
	int data;

	__asm __volatile("movl %1,%%eax; andl $0x40000000,%%eax" \
			 : "=a"(data) : "m"(*p));
	return(data);
}

static __inline void
atomic_intr_cond_inc(__atomic_intr_t *p)
{
	__asm __volatile(MPLOCKED "incl %0" : "+m" (*p));
}

static __inline void
atomic_intr_cond_dec(__atomic_intr_t *p)
{
	__asm __volatile(MPLOCKED "decl %0" : "+m" (*p));
}

static __inline void
atomic_intr_cond_enter(__atomic_intr_t *p, void (*func)(void *), void *arg)
{
	__asm __volatile(MPLOCKED "btsl $31,%0; jnc 3f; " \
			 MPLOCKED "incl %0; " \
			 "1: ;" \
			 MPLOCKED "btsl $31,%0; jnc 2f; " \
			 "movq %2,%%rdi; call *%1; " \
			 "jmp 1b; " \
			 "2: ;" \
			 MPLOCKED "decl %0; " \
			 "3: ;" \
			 : "+m" (*p) \
			 : "r"(func), "m"(arg) \
			 : "ax", "cx", "dx", "rsi", "rdi", "r8", "r9", "r10", "r11");
		/* YYY the function call may clobber even more registers? */
}

/*
 * Attempt to enter the interrupt condition variable.  Returns zero on
 * success, 1 on failure.
 */
static __inline int
atomic_intr_cond_try(__atomic_intr_t *p)
{
	int ret;

	__asm __volatile("subl %%eax,%%eax; "			\
			 MPLOCKED "btsl $31,%0; jnc 2f; "	\
			 "movl $1,%%eax;"			\
			 "2: ;"
			 : "+m" (*p), "=&a"(ret)
                         : : "cx", "dx");
	return (ret);
}


static __inline int
atomic_intr_cond_test(__atomic_intr_t *p)
{
	return((int)(*p & 0x80000000));
}

static __inline void
atomic_intr_cond_exit(__atomic_intr_t *p, void (*func)(void *), void *arg)
{
	__asm __volatile(MPLOCKED "btrl $31,%0; " \
			 "testl $0x3FFFFFFF,%0; jz 1f; " \
			 "movq %2,%%rdi; call *%1; " \
			 "1: ;" \
			 : "+m" (*p) \
			 : "r"(func), "m"(arg) \
			 : "ax", "cx", "dx", "rsi", "rdi", "r8", "r9", "r10", "r11");
		/* YYY the function call may clobber even more registers? */
}

/*
 * Atomic compare and set
 *
 * if (*_dst == _old) *_dst = _new (all 32 bit words)
 *
 * Returns 0 on failure, non-zero on success.  The inline is designed to
 * allow the compiler to optimize the common case where the caller calls
 * these functions from inside a conditional.
 */

static __inline int
atomic_cmpxchg_int(volatile u_int *_dst, u_int _old, u_int _new)
{
	u_int res = _old;

	__asm __volatile(MPLOCKED "cmpxchgl %2,%1; " \
			 : "+a" (res), "=m" (*_dst) \
			 : "r" (_new), "m" (*_dst) \
			 : "memory");
	return (res);
}

static __inline int
atomic_cmpxchg_long_test(volatile u_long *_dst, u_long _old, u_long _new)
{
	u_int res = _old;

	__asm __volatile(MPLOCKED "cmpxchgq %2,%1; "
				  " setz %%al;"
				  " movsbq %%al,%%rax" \
			 : "+a" (res), "=m" (*_dst) \
			 : "r" (_new), "m" (*_dst) \
			 : "memory");
	return (res);
}

static __inline int
atomic_cmpset_short(volatile u_short *_dst, u_short _old, u_short _new)
{
	u_short res = _old;

	__asm __volatile(MPLOCKED "cmpxchgw %w2,%1; " \
			 : "+a" (res), "=m" (*_dst) \
			 : "r" (_new), "m" (*_dst) \
			 : "memory");
	return (res == _old);
}

static __inline int
atomic_fcmpset_char(volatile u_char *_dst, u_char *_old, u_char _new)
{
	u_char res = *_old;

	__asm __volatile(MPLOCKED "cmpxchgb %2,%0; " \
			 : "+m" (*_dst),		/* 0 */
			   "+a" (*_old)			/* 1 */
			 : "r" (_new)			/* 2 */
			 : "memory", "cc");
	return (res == *_old);
}

static __inline int
atomic_fcmpset_short(volatile u_short *_dst, u_short *_old, u_short _new)
{
	u_short res = *_old;

	__asm __volatile(MPLOCKED "cmpxchgw %2,%0; " \
			 : "+m" (*_dst),		/* 0 */
			   "+a" (*_old)			/* 1 */
			 : "r" (_new)			/* 2 */
			 : "memory", "cc");
	return (res == *_old);
}

static __inline int
atomic_cmpset_int(volatile u_int *_dst, u_int _old, u_int _new)
{
	u_int res = _old;

	__asm __volatile(MPLOCKED "cmpxchgl %2,%1; " \
			 : "+a" (res), "=m" (*_dst) \
			 : "r" (_new), "m" (*_dst) \
			 : "memory");
	return (res == _old);
}

static __inline int
atomic_fcmpset_int(volatile u_int *_dst, u_int *_old, u_int _new)
{
	u_int res = *_old;

	__asm __volatile(MPLOCKED "cmpxchgl %2,%0; " \
			 : "+m" (*_dst),		/* 0 */
			   "+a" (*_old)			/* 1 */
			 : "r" (_new)			/* 2 */
			 : "memory", "cc");
	return (res == *_old);
}

static __inline int
atomic_cmpset_int_xacquire(volatile u_int *_dst, u_int _old, u_int _new)
{
	u_int res = _old;

	__asm __volatile(XACQUIRE MPLOCKED "cmpxchgl %2,%1; " \
			 : "+a" (res), "=m" (*_dst) \
			 : "r" (_new), "m" (*_dst) \
			 : "memory");
	return (res == _old);
}

static __inline int
atomic_cmpset_int_xrelease(volatile u_int *_dst, u_int _old, u_int _new)
{
	u_int res = _old;

	__asm __volatile(XRELEASE MPLOCKED "cmpxchgl %2,%1; " \
			 : "+a" (res), "=m" (*_dst) \
			 : "r" (_new), "m" (*_dst) \
			 : "memory");
	return (res == _old);
}

static __inline int
atomic_cmpset_long(volatile u_long *_dst, u_long _old, u_long _new)
{
	u_long res = _old;

	__asm __volatile(MPLOCKED "cmpxchgq %2,%1; " \
			 : "+a" (res), "=m" (*_dst) \
			 : "r" (_new), "m" (*_dst) \
			 : "memory");
	return (res == _old);
}

static __inline int
atomic_fcmpset_long(volatile u_long *_dst, u_long *_old, u_long _new)
{
	u_long res = *_old;

	__asm __volatile(MPLOCKED "cmpxchgq %2,%0; " \
			 : "+m" (*_dst),		/* 0 */
			   "+a" (*_old)			/* 1 */
			 : "r" (_new)			/* 2 */
			 : "memory", "cc");
	return (res == *_old);
}

static __inline int
atomic_cmpset_long_xacquire(volatile u_long *_dst, u_long _old, u_long _new)
{
	u_long res = _old;

	__asm __volatile(XACQUIRE MPLOCKED "cmpxchgq %2,%1; " \
			 : "+a" (res), "=m" (*_dst) \
			 : "r" (_new), "m" (*_dst) \
			 : "memory");
	return (res == _old);
}

static __inline int
atomic_cmpset_long_xrelease(volatile u_long *_dst, u_long _old, u_long _new)
{
	u_long res = _old;

	__asm __volatile(XRELEASE MPLOCKED "cmpxchgq %2,%1; " \
			 : "+a" (res), "=m" (*_dst) \
			 : "r" (_new), "m" (*_dst) \
			 : "memory");
	return (res == _old);
}

static inline void *
atomic_cas_ptr(volatile void *p, void *e, void *n)
{
	__asm volatile(MPLOCKED " cmpxchgq %2, %1"
	    : "=a" (n), "=m" (*(volatile unsigned long *)p)
	    : "r" (n), "a" (e), "m" (*(volatile unsigned long *)p));

	return (n);
}

/*
 * Atomically add the value of v to the integer pointed to by p and return
 * the previous value of *p.
 */
static __inline u_int
atomic_fetchadd_int(volatile u_int *_p, u_int _v)
{
	__asm __volatile(MPLOCKED "xaddl %0,%1; " \
			 : "+r" (_v), "=m" (*_p)	\
			 : "m" (*_p)		\
			 : "memory");
	return (_v);
}

static __inline u_int
atomic_fetchadd_int_xacquire(volatile u_int *_p, u_int _v)
{
	__asm __volatile(XACQUIRE MPLOCKED "xaddl %0,%1; " \
			 : "+r" (_v), "=m" (*_p)	\
			 : "m" (*_p)		\
			 : "memory");
	return (_v);
}

static __inline u_int
atomic_fetchadd_int_xrelease(volatile u_int *_p, u_int _v)
{
	__asm __volatile(XRELEASE MPLOCKED "xaddl %0,%1; " \
			 : "+r" (_v), "=m" (*_p)	\
			 : "m" (*_p)		\
			 : "memory");
	return (_v);
}

static __inline u_long
atomic_fetchadd_long(volatile u_long *_p, u_long _v)
{
	__asm __volatile(MPLOCKED "xaddq %0,%1; " \
			 : "+r" (_v), "=m" (*_p)	\
			 : "m" (*_p)		\
			 : "memory");
	return (_v);
}

static __inline u_long
atomic_fetchadd_long_xacquire(volatile u_long *_p, u_long _v)
{
	__asm __volatile(XACQUIRE MPLOCKED "xaddq %0,%1; " \
			 : "+r" (_v), "=m" (*_p)	\
			 : "m" (*_p)		\
			 : "memory");
	return (_v);
}

static __inline u_long
atomic_fetchadd_long_xrelease(volatile u_long *_p, u_long _v)
{
	__asm __volatile(XRELEASE MPLOCKED "xaddq %0,%1; " \
			 : "+r" (_v), "=m" (*_p)	\
			 : "m" (*_p)		\
			 : "memory");
	return (_v);
}

static __inline int
atomic_testandset_int(volatile u_int *p, u_int v)
{
	u_char res;

	__asm __volatile(
	"	" MPLOCKED "		"
	"	btsl	%2,%1 ;		"
	"	setc	%0 ;		"
	"# atomic_testandset_int"
	: "=q" (res),			/* 0 */
	  "+m" (*p)			/* 1 */
	: "Ir" (v & 0x1f)		/* 2 */
	: "cc");
	return (res);
}

static __inline int
atomic_testandset_long(volatile u_long *p, u_long v)
{
	u_char res;

	__asm __volatile(
	"	" MPLOCKED "		"
	"	btsq	%2,%1 ;		"
	"	setc	%0 ;		"
	"# atomic_testandset_long"
	: "=q" (res),			/* 0 */
	  "+m" (*p)			/* 1 */
	: "Ir" (v & 0x3f)		/* 2 */
	: "cc");
	return (res);
}

static __inline int
atomic_testandclear_int(volatile u_int *p, u_int v)
{
	u_char res;

	__asm __volatile(
	"	" MPLOCKED "		"
	"	btrl	%2,%1 ;		"
	"	setc	%0 ;		"
	"# atomic_testandclear_int"
	: "=q" (res),			/* 0 */
	  "+m" (*p)			/* 1 */
	: "Ir" (v & 0x1f)		/* 2 */
	: "cc");
	return (res);
}

static __inline int
atomic_testandclear_long(volatile u_long *p, u_long v)
{
	u_char res;

	__asm __volatile(
	"	" MPLOCKED "		"
	"	btrq	%2,%1 ;		"
	"	setc	%0 ;		"
	"# atomic_testandclear_long"
	: "=q" (res),			/* 0 */
	  "+m" (*p)			/* 1 */
	: "Ir" (v & 0x3f)		/* 2 */
	: "cc");
	return (res);
}

#define ATOMIC_STORE_LOAD(TYPE, LOP, SOP)		\
static __inline u_##TYPE				\
atomic_load_acq_##TYPE(volatile u_##TYPE *p)		\
{							\
	u_##TYPE res; /* accumulator can be anything */	\
							\
	__asm __volatile(MPLOCKED LOP			\
	: "=a" (res),			/* 0 */		\
	  "=m" (*p)			/* 1 */		\
	: "m" (*p)			/* 2 */		\
	: "memory");					\
							\
	return (res);					\
}							\
							\
/*							\
 * The XCHG instruction asserts LOCK automagically.	\
 */							\
static __inline void					\
atomic_store_rel_##TYPE(volatile u_##TYPE *p, u_##TYPE v)\
{							\
	__asm __volatile(SOP				\
	: "=m" (*p),			/* 0 */		\
	  "+r" (v)			/* 1 */		\
	: "m" (*p));			/* 2 */		\
}							\
struct __hack

ATOMIC_STORE_LOAD(char, "cmpxchgb %b0,%1", "xchgb %b1,%0");
ATOMIC_STORE_LOAD(short,"cmpxchgw %w0,%1", "xchgw %w1,%0");
ATOMIC_STORE_LOAD(int,  "cmpxchgl %0,%1",  "xchgl %1,%0");
ATOMIC_STORE_LOAD(long, "cmpxchgq %0,%1",  "xchgq %1,%0");

#undef ATOMIC_ASM
#undef ATOMIC_STORE_LOAD

/* Acquire and release variants are identical to the normal ones. */
#define	atomic_set_acq_char		atomic_set_char
#define	atomic_set_rel_char		atomic_set_char
#define	atomic_clear_acq_char		atomic_clear_char
#define	atomic_clear_rel_char		atomic_clear_char
#define	atomic_add_acq_char		atomic_add_char
#define	atomic_add_rel_char		atomic_add_char
#define	atomic_subtract_acq_char	atomic_subtract_char
#define	atomic_subtract_rel_char	atomic_subtract_char

#define	atomic_set_acq_short		atomic_set_short
#define	atomic_set_rel_short		atomic_set_short
#define	atomic_clear_acq_short		atomic_clear_short
#define	atomic_clear_rel_short		atomic_clear_short
#define	atomic_add_acq_short		atomic_add_short
#define	atomic_add_rel_short		atomic_add_short
#define	atomic_subtract_acq_short	atomic_subtract_short
#define	atomic_subtract_rel_short	atomic_subtract_short

#define	atomic_set_acq_int		atomic_set_int
#define	atomic_set_rel_int		atomic_set_int
#define	atomic_clear_acq_int		atomic_clear_int
#define	atomic_clear_rel_int		atomic_clear_int
#define	atomic_add_acq_int		atomic_add_int
#define	atomic_add_rel_int		atomic_add_int
#define	atomic_subtract_acq_int		atomic_subtract_int
#define	atomic_subtract_rel_int		atomic_subtract_int
#define	atomic_cmpset_acq_int		atomic_cmpset_int
#define	atomic_cmpset_rel_int		atomic_cmpset_int

#define	atomic_set_acq_long		atomic_set_long
#define	atomic_set_rel_long		atomic_set_long
#define	atomic_clear_acq_long		atomic_clear_long
#define	atomic_clear_rel_long		atomic_clear_long
#define	atomic_add_acq_long		atomic_add_long
#define	atomic_add_rel_long		atomic_add_long
#define	atomic_subtract_acq_long	atomic_subtract_long
#define	atomic_subtract_rel_long	atomic_subtract_long
#define	atomic_cmpset_acq_long		atomic_cmpset_long
#define	atomic_cmpset_rel_long		atomic_cmpset_long

/* cpumask_t is 64-bits on x86-64 */
#define	atomic_set_cpumask		atomic_set_long
#define	atomic_clear_cpumask		atomic_clear_long
#define	atomic_cmpset_cpumask		atomic_cmpset_long
#define	atomic_store_rel_cpumask	atomic_store_rel_long
#define	atomic_load_acq_cpumask		atomic_load_acq_long

/* Operations on 8-bit bytes. */
#define	atomic_set_8		atomic_set_char
#define	atomic_set_acq_8	atomic_set_acq_char
#define	atomic_set_rel_8	atomic_set_rel_char
#define	atomic_clear_8		atomic_clear_char
#define	atomic_clear_acq_8	atomic_clear_acq_char
#define	atomic_clear_rel_8	atomic_clear_rel_char
#define	atomic_add_8		atomic_add_char
#define	atomic_add_acq_8	atomic_add_acq_char
#define	atomic_add_rel_8	atomic_add_rel_char
#define	atomic_subtract_8	atomic_subtract_char
#define	atomic_subtract_acq_8	atomic_subtract_acq_char
#define	atomic_subtract_rel_8	atomic_subtract_rel_char
#define	atomic_load_acq_8	atomic_load_acq_char
#define	atomic_store_rel_8	atomic_store_rel_char
#define	atomic_fcmpset_8	atomic_fcmpset_char

/* Operations on 16-bit words. */
#define	atomic_set_16		atomic_set_short
#define	atomic_set_acq_16	atomic_set_acq_short
#define	atomic_set_rel_16	atomic_set_rel_short
#define	atomic_clear_16		atomic_clear_short
#define	atomic_clear_acq_16	atomic_clear_acq_short
#define	atomic_clear_rel_16	atomic_clear_rel_short
#define	atomic_add_16		atomic_add_short
#define	atomic_add_acq_16	atomic_add_acq_short
#define	atomic_add_rel_16	atomic_add_rel_short
#define	atomic_subtract_16	atomic_subtract_short
#define	atomic_subtract_acq_16	atomic_subtract_acq_short
#define	atomic_subtract_rel_16	atomic_subtract_rel_short
#define	atomic_load_acq_16	atomic_load_acq_short
#define	atomic_store_rel_16	atomic_store_rel_short
#define	atomic_fcmpset_16	atomic_fcmpset_short

/* Operations on 32-bit double words. */
#define	atomic_set_32		atomic_set_int
#define	atomic_set_acq_32	atomic_set_acq_int
#define	atomic_set_rel_32	atomic_set_rel_int
#define	atomic_clear_32		atomic_clear_int
#define	atomic_clear_acq_32	atomic_clear_acq_int
#define	atomic_clear_rel_32	atomic_clear_rel_int
#define	atomic_add_32		atomic_add_int
#define	atomic_add_acq_32	atomic_add_acq_int
#define	atomic_add_rel_32	atomic_add_rel_int
#define	atomic_subtract_32	atomic_subtract_int
#define	atomic_subtract_acq_32	atomic_subtract_acq_int
#define	atomic_subtract_rel_32	atomic_subtract_rel_int
#define	atomic_load_acq_32	atomic_load_acq_int
#define	atomic_store_rel_32	atomic_store_rel_int
#define	atomic_cmpset_32	atomic_cmpset_int
#define	atomic_fcmpset_32	atomic_fcmpset_int
#define	atomic_cmpset_acq_32	atomic_cmpset_acq_int
#define	atomic_cmpset_rel_32	atomic_cmpset_rel_int
#define	atomic_readandclear_32	atomic_readandclear_int
#define	atomic_fetchadd_32	atomic_fetchadd_int

/* Operations on 64-bit quad words. */
#define	atomic_load_acq_64	atomic_load_acq_long
#define	atomic_store_rel_64	atomic_store_rel_long
#define	atomic_swap_64		atomic_swap_long
#define	atomic_fetchadd_64	atomic_fetchadd_long
#define	atomic_add_64		atomic_add_long
#define	atomic_subtract_64	atomic_subtract_long
#define	atomic_cmpset_64	atomic_cmpset_long
#define	atomic_fcmpset_64	atomic_fcmpset_long
#define	atomic_set_64		atomic_set_long
#define	atomic_clear_64		atomic_clear_long

/* Operations on pointers. */
#define atomic_set_ptr(p, v) \
	atomic_set_long((volatile u_long *)(p), (u_long)(v))
#define atomic_set_acq_ptr(p, v) \
	atomic_set_acq_long((volatile u_long *)(p), (u_long)(v))
#define atomic_set_rel_ptr(p, v) \
	atomic_set_rel_long((volatile u_long *)(p), (u_long)(v))
#define atomic_clear_ptr(p, v) \
	atomic_clear_long((volatile u_long *)(p), (u_long)(v))
#define atomic_clear_acq_ptr(p, v) \
	atomic_clear_acq_long((volatile u_long *)(p), (u_long)(v))
#define atomic_clear_rel_ptr(p, v) \
	atomic_clear_rel_long((volatile u_long *)(p), (u_long)(v))
#define atomic_add_ptr(p, v) \
	atomic_add_long((volatile u_long *)(p), (u_long)(v))
#define atomic_add_acq_ptr(p, v) \
	atomic_add_acq_long((volatile u_long *)(p), (u_long)(v))
#define atomic_add_rel_ptr(p, v) \
	atomic_add_rel_long((volatile u_long *)(p), (u_long)(v))
#define atomic_subtract_ptr(p, v) \
	atomic_subtract_long((volatile u_long *)(p), (u_long)(v))
#define atomic_subtract_acq_ptr(p, v) \
	atomic_subtract_acq_long((volatile u_long *)(p), (u_long)(v))
#define atomic_subtract_rel_ptr(p, v) \
	atomic_subtract_rel_long((volatile u_long *)(p), (u_long)(v))
#define atomic_load_acq_ptr(p) \
	atomic_load_acq_long((volatile u_long *)(p))
#define atomic_store_rel_ptr(p, v) \
	atomic_store_rel_long((volatile u_long *)(p), (v))
#define atomic_cmpset_ptr(dst, old, new) 				\
	atomic_cmpset_long((volatile u_long *)(dst), (u_long)(old),	\
				(u_long)(new))
#define atomic_fcmpset_ptr(dst, old, new) 				\
	atomic_fcmpset_long((volatile u_long *)(dst), (u_long *)(old),	\
				(u_long)(new))
#define atomic_cmpset_acq_ptr(dst, old, new)				\
	atomic_cmpset_acq_long((volatile u_long *)(dst), (u_long)(old), \
				(u_long)(new))
#define atomic_cmpset_rel_ptr(dst, old, new)				\
	atomic_cmpset_rel_long((volatile u_long *)(dst), (u_long)(old), \
				(u_long)(new))
#define atomic_readandclear_ptr(p)					\
	atomic_readandclear_long((volatile u_long *)(p))

#endif /* ! _CPU_ATOMIC_H_ */
