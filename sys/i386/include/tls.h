/*
 * Copyright (c) 2005 The DragonFly Project.  All rights reserved.
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
 * $DragonFly: src/sys/i386/include/Attic/tls.h,v 1.2 2005/03/29 23:04:36 joerg Exp $
 */

#ifndef	_MACHINE_TLS_H_
#define	_MACHINE_TLS_H_

#include <sys/types.h>
#include <sys/tls.h>

struct tls_tcb {
	struct tls_tcb *tcb_self;	/* pointer to self*/
	void *tcb_dtv;			/* Dynamic Thread Vector */
	void *tcb_pthread;		/* thread library's data*/
};

struct tls_dtv {
	uintptr_t dtv_generation;
	uintptr_t dtv_max_index;
	void *dtv_offset[__ARRAY_ZERO];
};

#define	RTLD_TCB_HAS_SELF_POINTER
#define	RTLD_STATIC_TLS_ALIGN           16
#define	RTLD_STATIC_TLS_ALIGN_MASK      (RTLD_STATIC_TLS_ALIGN - 1)
#define RTLD_STATIC_TLS_VARIANT_II

/* Get the current TCB. */
static __inline struct tls_tcb *
tls_get_tcb(void)
{
	void *self;

#if 0
	__asm __volatile ("movl %%gs:%1, %0"
	    : "=r" (self)
	    : "i" (__offsetof(struct tls_tcb, tcb_self)));
#else
	__asm __volatile ("movl %%gs:0, %0" : "=r" (self));
#endif

	return(self);
}

/* Get the current thread. */
static __inline void *
tls_get_curthread(void)
{
	void *self;

#if 0
	__asm __volatile ("movl %%gs:%1, %0"
	    : "=r" (self)
	    : "i" (__offsetof(struct tls_tcb, tcb_pthread)));
#else
	__asm __volatile ("movl %%gs:8, %0" : "=r" (self));
#endif

	return(self);
}

static __inline void
tls_set_tcb(struct tls_tcb *tcb)
{
	struct tls_info info;
	int seg;

	info.base = tcb;
	info.size = -1;
	seg = sys_set_tls_area(0, &info, sizeof(info));
	__asm __volatile("movl %0, %%gs" : : "r" (seg));
}

struct tls_tcb	*_rtld_allocate_tls(struct tls_tcb *);
void		 _rtld_free_tls(struct tls_tcb *);

#endif	/* !_MACHINE_TLS_H_ */
