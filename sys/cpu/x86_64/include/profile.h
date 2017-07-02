/*
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
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
 *	@(#)profile.h	8.1 (Berkeley) 6/11/93
 * $FreeBSD: src/sys/amd64/include/profile.h,v 1.33 2004/01/06 20:36:21 nectar Exp $
 */

#ifndef _CPU_PROFILE_H_
#define	_CPU_PROFILE_H_

#ifdef _KERNEL

/*
 * Config generates something to tell the compiler to align functions on 16
 * byte boundaries.  A strict alignment is good for keeping the tables small.
 */
#define	FUNCTION_ALIGNMENT	16

/*
 * The kernel uses assembler stubs instead of unportable inlines.
 * This is mainly to save a little time when profiling is not enabled,
 * which is the usual case for the kernel.
 */
#define	_MCOUNT_DECL void mcount
#define	MCOUNT

#ifdef GUPROF
#define	CALIB_SCALE	1000
#define	KCOUNT(p,index)	((p)->kcount[(index) \
			 / (HISTFRACTION * sizeof(HISTCOUNTER))])
#define	MCOUNT_DECL(s)
#define	MCOUNT_ENTER(s)
#define	MCOUNT_EXIT(s)
#define	PC_TO_I(p, pc)	((uintfptr_t)(pc) - (uintfptr_t)(p)->lowpc)
#else
#define	MCOUNT_DECL(s)	u_long s;
extern int	mcount_lock;
#define	MCOUNT_ENTER(s)	{ s = read_rflags(); disable_intr(); \
 			  while (!atomic_cmpset_acq_int(&mcount_lock, 0, 1)) \
			  	/* nothing */ ; }
#define	MCOUNT_EXIT(s)	{ atomic_store_rel_int(&mcount_lock, 0); \
			  write_rflags(s); }
#endif /* GUPROF */

#else /* !_KERNEL */

#define	FUNCTION_ALIGNMENT	4

#define	_MCOUNT_DECL \
static void _mcount(uintfptr_t frompc, uintfptr_t selfpc) __used; \
static void _mcount

#ifdef	__GNUC__
#define	MCOUNT __asm("			\n\
	.text				\n\
	.p2align 4,0x90			\n\
	.globl	.mcount			\n\
	.type	.mcount,@function	\n\
.mcount:				\n\
	pushq	%rdi			\n\
	pushq	%rsi			\n\
	pushq	%rdx			\n\
	pushq	%rcx			\n\
	pushq	%r8			\n\
	pushq	%r9			\n\
	pushq	%rax			\n\
	movq	8(%rbp),%rdi		\n\
	movq	7*8(%rsp),%rsi		\n\
	call	_mcount			\n\
	popq	%rax			\n\
	popq	%r9			\n\
	popq	%r8			\n\
	popq	%rcx			\n\
	popq	%rdx			\n\
	popq	%rsi			\n\
	popq	%rdi			\n\
	ret				\n\
	.size	.mcount, . - .mcount");
#else	/* __GNUC__ */
#define	MCOUNT		\
void			\
mcount()		\
{			\
}
#endif	/* __GNUC__ */

typedef	unsigned long	uintfptr_t;

#endif /* _KERNEL */

/*
 * An unsigned integral type that can hold non-negative difference between
 * function pointers.
 */
typedef	unsigned long	fptrdiff_t;

#ifdef _KERNEL

void	mcount(uintfptr_t frompc, uintfptr_t selfpc);
void	kmupetext(uintfptr_t nhighpc);

#ifdef GUPROF
struct gmonparam;

void	nullfunc_loop_profiled(void);
void	nullfunc_profiled(void);
void	startguprof(struct gmonparam *p);
void	stopguprof(struct gmonparam *p);
#else
#define	startguprof(p)
#define	stopguprof(p)
#endif /* GUPROF */

#else /* !_KERNEL */

#include <sys/cdefs.h>

__BEGIN_DECLS
#ifdef __GNUC__
void	mcount(void) __asm(".mcount");
#endif
__END_DECLS

#endif /* _KERNEL */

#ifdef GUPROF
/* XXX doesn't quite work outside kernel yet. */
extern int	cputime_bias;

__BEGIN_DECLS
int	cputime(void);
void	empty_loop(void);
void	mexitcount(uintfptr_t selfpc);
void	nullfunc(void);
void	nullfunc_loop(void);
__END_DECLS
#endif

#endif /* !_CPU_PROFILE_H_ */
