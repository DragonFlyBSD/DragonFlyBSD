/*-
 * Copyright (c) 2002 Daniel Eischen <deischen@freebsd.org>.
 * Copyright (c) 2005 David Xu <davidxu@freebsd.org>.
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
 * $FreeBSD: src/lib/libpthread/arch/i386/include/pthread_md.h,v 1.13 2004/11/06 03:35:51 peter Exp $
 * $DragonFly: src/lib/libthread_xu/arch/i386/include/pthread_md.h,v 1.1 2005/02/01 12:38:27 davidxu Exp $
 */

/*
 * Machine-dependent thread prototypes/definitions for the thread kernel.
 */
#ifndef _PTHREAD_MD_H_
#define	_PTHREAD_MD_H_

#include <stddef.h>
#include <sys/types.h>
#include <machine/sysarch.h>

#define	DTV_OFFSET		offsetof(struct tcb, tcb_dtv)

struct pthread;

/*
 * %gs points to a struct tcb.
 */
struct tcb {
	struct tcb		*tcb_self;	/* required by rtld */
	void			*tcb_dtv;	/* required by rtld */
	struct pthread		*tcb_thread;
	int			tcb_ldt;
};

/*
 * Evaluates to the byte offset of the per-thread variable name.
 */
#define	__tcb_offset(name)	__offsetof(struct tcb, name)

/*
 * Evaluates to the type of the per-thread variable name.
 */
#define	__tcb_type(name)	__typeof(((struct tcb *)0)->name)

/*
 * Evaluates to the value of the per-kse variable name.
 */
#define	TCB_GET32(name) ({					\
	__tcb_type(name) __result;				\
								\
	u_int __i;						\
	__asm __volatile("movl %%gs:%1, %0"			\
	    : "=r" (__i)					\
	    : "m" (*(u_int *)(__tcb_offset(name))));		\
	__result = (__tcb_type(name))__i;			\
								\
	__result;						\
})

#ifdef __DragonFly__
static __inline int
atomic_cmpset_int(volatile int *dst, int exp, int src)
{
	int res = exp;

	__asm __volatile (
	"	lock cmpxchgl %1,%2 ;	"
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

#define atomic_cmpset_acq_int	atomic_cmpset_int
#endif

/*
 * The constructors.
 */
struct tcb	*_tcb_ctor(struct pthread *, int);
void		_tcb_dtor(struct tcb *tcb);

/* Called from the thread to set its private data. */
static __inline void
_tcb_set(struct tcb *tcb)
{
#ifndef COMPAT_32BIT
	int val;

	val = (tcb->tcb_ldt << 3) | 7;
	__asm __volatile("movl %0, %%gs" : : "r" (val));
#else
	_amd64_set_gsbase(tcb);
#endif

}

/* Get the current kcb. */
static __inline struct tcb *
_tcb_get(void)
{
	return (TCB_GET32(tcb_self));
}

extern struct pthread *_thr_initial;

/* Get the current thread. */
static __inline struct pthread *
_get_curthread(void)
{
	if (_thr_initial)
		return (TCB_GET32(tcb_thread));
	return (NULL);
}
#endif
