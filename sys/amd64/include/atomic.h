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
 * $FreeBSD: src/sys/amd64/include/atomic.h,v 1.32 2003/11/21 03:02:00 peter Exp $
 * $DragonFly: src/sys/amd64/include/Attic/atomic.h,v 1.2 2006/07/27 00:42:46 corecode Exp $
 */
#ifndef _MACHINE_ATOMIC_H_
#define _MACHINE_ATOMIC_H_

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
 * atomic_readandclear_int(P)	(return  *(u_int*)P; *(u_int*)P = 0;)
 *
 * atomic_set_long(P, V)	(*(u_long*)(P) |= (V))
 * atomic_clear_long(P, V)	(*(u_long*)(P) &= ~(V))
 * atomic_add_long(P, V)	(*(u_long*)(P) += (V))
 * atomic_subtract_long(P, V)	(*(u_long*)(P) -= (V))
 * atomic_readandclear_long(P)	(return  *(u_long*)P; *(u_long*)P = 0;)
 */

/*
 * The above functions are expanded inline in the statically-linked
 * kernel.  Lock prefixes are generated if an SMP kernel is being
 * built.
 *
 * Kernel modules call real functions which are built into the kernel.
 * This allows kernel modules to be portable between UP and SMP systems.
 */
#if defined(KLD_MODULE)
#define ATOMIC_ASM(NAME, TYPE, OP, CONS, V)			\
void atomic_##NAME##_##TYPE(volatile u_##TYPE *p, u_##TYPE v)

int atomic_cmpset_int(volatile u_int *dst, u_int exp, u_int src);
int atomic_cmpset_long(volatile u_long *dst, u_long exp, u_long src);

#define	ATOMIC_STORE_LOAD(TYPE, LOP, SOP)			\
u_##TYPE	atomic_load_acq_##TYPE(volatile u_##TYPE *p);	\
void		atomic_store_rel_##TYPE(volatile u_##TYPE *p, u_##TYPE v)

#else /* !KLD_MODULE */

#ifdef __GNUC__

/*
 * For userland, assume the SMP case and use lock prefixes so that
 * the binaries will run on both types of systems.
 */
#if defined(SMP) || !defined(_KERNEL)
#define MPLOCKED	"lock ; "
#else
#define MPLOCKED
#endif

/*
 * The assembly is volatilized to demark potential before-and-after side
 * effects if an interrupt or SMP collision were to occur.
 */
#define ATOMIC_ASM(NAME, TYPE, OP, CONS, V)		\
static __inline void					\
atomic_##NAME##_##TYPE(volatile u_##TYPE *p, u_##TYPE v)\
{							\
	__asm __volatile(MPLOCKED OP			\
			 : "+m" (*p)			\
			 : CONS (V));			\
}							\
struct __hack

#else /* !__GNUC__ */

#define ATOMIC_ASM(NAME, TYPE, OP, CONS, V)				\
extern void atomic_##NAME##_##TYPE(volatile u_##TYPE *p, u_##TYPE v)

#endif /* __GNUC__ */

/*
 * These functions operate on a 32 bit interrupt interlock which is defined
 * as follows:
 *
 *	bit 0-30	interrupt handler disabled bits (counter)
 *	bit 31		interrupt handler currently running bit (1 = run)
 *
 * atomic_intr_cond_test(P)	Determine if the interlock is in an
 *				acquired state.  Returns 0 if it not
 *				acquired, non-zero if it is.
 *
 * atomic_intr_cond_try(P)
 *				Increment the request counter and attempt to
 *				set bit 31 to acquire the interlock.  If
 *				we are unable to set bit 31 the request
 *				counter is decremented and we return -1,
 *				otherwise we return 0.
 *
 * atomic_intr_cond_enter(P, func, arg)
 *				Increment the request counter and attempt to
 *				set bit 31 to acquire the interlock.  If
 *				we are unable to set bit 31 func(arg) is
 *				called in a loop until we are able to set
 *				bit 31.
 *
 * atomic_intr_cond_exit(P, func, arg)
 *				Decrement the request counter and clear bit
 *				31.  If the request counter is still non-zero
 *				call func(arg) once.
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
 *				the field is ignored.
 */

#ifndef __ATOMIC_INTR_T
#define __ATOMIC_INTR_T
typedef volatile int atomic_intr_t;
#endif

#if defined(KLD_MODULE)

void atomic_intr_init(atomic_intr_t *p);
int atomic_intr_handler_disable(atomic_intr_t *p);
void atomic_intr_handler_enable(atomic_intr_t *p);
int atomic_intr_handler_is_enabled(atomic_intr_t *p);
int atomic_intr_cond_test(atomic_intr_t *p);
int atomic_intr_cond_try(atomic_intr_t *p);
void atomic_intr_cond_enter(atomic_intr_t *p, void (*func)(void *), void *arg);
void atomic_intr_cond_exit(atomic_intr_t *p, void (*func)(void *), void *arg);

#else

static __inline
void
atomic_intr_init(atomic_intr_t *p)
{
	*p = 0;
}

static __inline
int
atomic_intr_handler_disable(atomic_intr_t *p)
{
	int data;

	__asm __volatile(MPLOCKED "orl $0x40000000,%1; movl %1,%%eax; " \
				  "andl $0x80000000,%%eax" \
				  : "=a"(data) , "+m"(*p));
	return(data);
}

static __inline
void
atomic_intr_handler_enable(atomic_intr_t *p)
{
	__asm __volatile(MPLOCKED "andl $0xBFFFFFFF,%0" : "+m" (*p));
}

static __inline
int
atomic_intr_handler_is_enabled(atomic_intr_t *p)
{
	int data;

	__asm __volatile("movl %1,%%eax; andl $0x40000000,%%eax" \
			 : "=a"(data) : "m"(*p));
	return(data);
}

static __inline
void
atomic_intr_cond_enter(atomic_intr_t *p, void (*func)(void *), void *arg)
{
	__asm __volatile(MPLOCKED "incl %0; " \
			 "1: ;" \
			 MPLOCKED "btsl $31,%0; jnc 2f; " \
			 "movq %2,%rdi; call *%1; " \
			 "jmp 1b; " \
			 "2: ;" \
			 : "+m" (*p) \
			 : "r"(func), "m"(arg) \
			 : "ax", "cx", "dx", "di");	/* XXX clobbers more regs */
}

/*
 * Attempt to enter the interrupt condition variable.  Returns zero on
 * success, 1 on failure.
 */
static __inline
int
atomic_intr_cond_try(atomic_intr_t *p)
{
	int ret;

	__asm __volatile(MPLOCKED "incl %0; " \
			 "1: ;" \
			 "subl %%eax,%%eax; " \
			 MPLOCKED "btsl $31,%0; jnc 2f; " \
			 MPLOCKED "decl %0; " \
			 "movl $1,%%eax;" \
			 "2: ;" \
			 : "+m" (*p), "=a"(ret) \
			 : : "cx", "dx");
	return (ret);
}


static __inline
int
atomic_intr_cond_test(atomic_intr_t *p)
{
	return((int)(*p & 0x80000000));
}

static __inline
void
atomic_intr_cond_exit(atomic_intr_t *p, void (*func)(void *), void *arg)
{
	__asm __volatile(MPLOCKED "decl %0; " \
			MPLOCKED "btrl $31,%0; " \
			"testl $0x3FFFFFFF,%0; jz 1f; " \
			 "movq %2,%rdi; call *%1; " \
			 "1: ;" \
			 : "+m" (*p) \
			 : "r"(func), "m"(arg) \
			 : "ax", "cx", "dx", "di");	/* XXX clobbers more regs */
}

#endif

/*
 * Atomic compare and set, used by the mutex functions
 *
 * if (*dst == exp) *dst = src (all 32 bit words)
 *
 * Returns 0 on failure, non-zero on success
 */

#if defined(__GNUC__)

static __inline int
atomic_cmpset_int(volatile u_int *dst, u_int exp, u_int src)
{
	int res = exp;

	__asm __volatile (
		MPLOCKED
	"	cmpxchgl %1,%2 ;	"
	"       setz	%%al ;		"
	"	movzbl	%%al,%0 ;	"
	"1:				"
	"# atomic_cmpset_int"
	: "+a" (res)			/* 0 (result) */
	: "r" (src),			/* 1 */
	  "m" (*(dst))			/* 2 */
	: "memory");				 

	return (res);
}

static __inline int
atomic_cmpset_long(volatile u_long *dst, u_long exp, u_long src)
{
	long res = exp;

	__asm __volatile (
		MPLOCKED
	"	cmpxchgq %1,%2 ;	"
	"       setz	%%al ;		"
	"	movzbq	%%al,%0 ;	"
	"1:				"
	"# atomic_cmpset_long"
	: "+a" (res)			/* 0 (result) */
	: "r" (src),			/* 1 */
	  "m" (*(dst))			/* 2 */
	: "memory");				 

	return (res);
}
#endif /* defined(__GNUC__) */

#if defined(__GNUC__)

#define ATOMIC_STORE_LOAD(TYPE, LOP, SOP)		\
static __inline u_##TYPE				\
atomic_load_acq_##TYPE(volatile u_##TYPE *p)		\
{							\
	u_##TYPE res;					\
							\
	__asm __volatile(MPLOCKED LOP			\
	: "=a" (res),			/* 0 (result) */\
	  "+m" (*p)			/* 1 */		\
	: : "memory");				 	\
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
	: "+m" (*p),			/* 0 */		\
	  "+r" (v)			/* 1 */		\
	: : "memory");				 	\
}							\
struct __hack

#else /* !defined(__GNUC__) */

extern int atomic_cmpset_int(volatile u_int *, u_int, u_int);
extern int atomic_cmpset_long(volatile u_long *, u_long, u_long);

#define ATOMIC_STORE_LOAD(TYPE, LOP, SOP)				\
extern u_##TYPE atomic_load_acq_##TYPE(volatile u_##TYPE *p);		\
extern void atomic_store_rel_##TYPE(volatile u_##TYPE *p, u_##TYPE v)

#endif /* defined(__GNUC__) */

#endif /* KLD_MODULE */

ATOMIC_ASM(set,	     char,  "orb %b1,%0",  "iq",  v);
ATOMIC_ASM(clear,    char,  "andb %b1,%0", "iq", ~v);
ATOMIC_ASM(add,	     char,  "addb %b1,%0", "iq",  v);
ATOMIC_ASM(subtract, char,  "subb %b1,%0", "iq",  v);

ATOMIC_ASM(set,	     short, "orw %w1,%0",  "ir",  v);
ATOMIC_ASM(clear,    short, "andw %w1,%0", "ir", ~v);
ATOMIC_ASM(add,	     short, "addw %w1,%0", "ir",  v);
ATOMIC_ASM(subtract, short, "subw %w1,%0", "ir",  v);

ATOMIC_ASM(set,	     int,   "orl %1,%0",   "ir",  v);
ATOMIC_ASM(clear,    int,   "andl %1,%0",  "ir", ~v);
ATOMIC_ASM(add,	     int,   "addl %1,%0",  "ir",  v);
ATOMIC_ASM(subtract, int,   "subl %1,%0",  "ir",  v);

ATOMIC_ASM(set,	     long,  "orq %1,%0",   "ir",  v);
ATOMIC_ASM(clear,    long,  "andq %1,%0",  "ir", ~v);
ATOMIC_ASM(add,	     long,  "addq %1,%0",  "ir",  v);
ATOMIC_ASM(subtract, long,  "subq %1,%0",  "ir",  v);

ATOMIC_STORE_LOAD(char,	"cmpxchgb %b0,%1", "xchgb %b1,%0");
ATOMIC_STORE_LOAD(short,"cmpxchgw %w0,%1", "xchgw %w1,%0");
ATOMIC_STORE_LOAD(int,	"cmpxchgl %0,%1",  "xchgl %1,%0");
ATOMIC_STORE_LOAD(long,	"cmpxchgq %0,%1",  "xchgq %1,%0");

#undef ATOMIC_ASM
#undef ATOMIC_STORE_LOAD

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
#define atomic_cmpset_acq_int		atomic_cmpset_int
#define atomic_cmpset_rel_int		atomic_cmpset_int

#define	atomic_set_acq_long		atomic_set_long
#define	atomic_set_rel_long		atomic_set_long
#define	atomic_clear_acq_long		atomic_clear_long
#define	atomic_clear_rel_long		atomic_clear_long
#define	atomic_add_acq_long		atomic_add_long
#define	atomic_add_rel_long		atomic_add_long
#define	atomic_subtract_acq_long	atomic_subtract_long
#define	atomic_subtract_rel_long	atomic_subtract_long

#define atomic_cmpset_acq_ptr		atomic_cmpset_ptr
#define atomic_cmpset_rel_ptr		atomic_cmpset_ptr

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
#define	atomic_cmpset_acq_32	atomic_cmpset_acq_int
#define	atomic_cmpset_rel_32	atomic_cmpset_rel_int
#define	atomic_readandclear_32	atomic_readandclear_int

#if !defined(WANT_FUNCTIONS)
static __inline int
atomic_cmpset_ptr(volatile void *dst, void *exp, void *src)
{

	return (atomic_cmpset_long((volatile u_long *)dst,
	    (u_long)exp, (u_long)src));
}

static __inline void *
atomic_load_acq_ptr(volatile void *p)
{
	/*
	 * The apparently-bogus cast to intptr_t in the following is to
	 * avoid a warning from "gcc -Wbad-function-cast".
	 */
	return ((void *)(intptr_t)atomic_load_acq_long((volatile u_long *)p));
}

static __inline void
atomic_store_rel_ptr(volatile void *p, void *v)
{
	atomic_store_rel_long((volatile u_long *)p, (u_long)v);
}

#define ATOMIC_PTR(NAME)				\
static __inline void					\
atomic_##NAME##_ptr(volatile void *p, uintptr_t v)	\
{							\
	atomic_##NAME##_long((volatile u_long *)p, v);	\
}							\
							\
static __inline void					\
atomic_##NAME##_acq_ptr(volatile void *p, uintptr_t v)	\
{							\
	atomic_##NAME##_acq_long((volatile u_long *)p, v);\
}							\
							\
static __inline void					\
atomic_##NAME##_rel_ptr(volatile void *p, uintptr_t v)	\
{							\
	atomic_##NAME##_rel_long((volatile u_long *)p, v);\
}

ATOMIC_PTR(set)
ATOMIC_PTR(clear)
ATOMIC_PTR(add)
ATOMIC_PTR(subtract)

#undef ATOMIC_PTR

#if defined(__GNUC__)

static __inline u_int
atomic_readandclear_int(volatile u_int *addr)
{
	u_int result;

	__asm __volatile (
	"	xorl	%0,%0 ;		"
	"	xchgl	%1,%0 ;		"
	"# atomic_readandclear_int"
	: "=&r" (result)		/* 0 (result) */
	: "m" (*addr));			/* 1 (addr) */

	return (result);
}

static __inline u_long
atomic_readandclear_long(volatile u_long *addr)
{
	u_long result;

	__asm __volatile (
	"	xorq	%0,%0 ;		"
	"	xchgq	%1,%0 ;		"
	"# atomic_readandclear_int"
	: "=&r" (result)		/* 0 (result) */
	: "m" (*addr));			/* 1 (addr) */

	return (result);
}

#else /* !defined(__GNUC__) */

extern u_long	atomic_readandclear_long(volatile u_long *);
extern u_int	atomic_readandclear_int(volatile u_int *);

#endif /* defined(__GNUC__) */

#endif	/* !defined(WANT_FUNCTIONS) */
#endif /* ! _MACHINE_ATOMIC_H_ */
