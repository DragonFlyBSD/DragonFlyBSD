/*
 * Copyright (c) 2005,2008 The DragonFly Project.  All rights reserved.
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

#ifndef	_CPU_TLS_H_
#define	_CPU_TLS_H_

#ifndef _SYS_TYPES_H_
#include <sys/types.h>
#endif
#ifndef _SYS_TLS_H_
#include <sys/tls.h>
#endif

/*
 * NOTE: the tcb_{self,dtv,pthread,errno) fields must be declared
 * in the structure in the specified order as assembly will access the
 * fields with a hardwired offset.
 *
 * Outside of this file, the system call layer generation will hardwire
 * the offset for tcb_errno.
 */
struct tls_tcb {
	struct tls_tcb *tcb_self;	/* pointer to self*/
	void *tcb_dtv;			/* Dynamic Thread Vector */
	void *tcb_pthread;		/* thread library's data*/
	int *tcb_errno_p;		/* pointer to per-thread errno */
	void *tcb_segstack;		/* used for segmented stacks */
					/* e.g. by LLVM to store stack bound */
	void *tcb_unused[3];
};

struct tls_dtv {
	uintptr_t dtv_generation;
	uintptr_t dtv_max_index;
	void *dtv_offset[__ARRAY_ZERO];
};

#define	RTLD_TCB_HAS_SELF_POINTER
#define	RTLD_STATIC_TLS_ALIGN           16
#define	RTLD_STATIC_TLS_ALIGN_MASK      (RTLD_STATIC_TLS_ALIGN - 1)
#define	RTLD_STATIC_TLS_EXTRA		1280
#define RTLD_STATIC_TLS_VARIANT_II

/* Get the current TCB. */
static __inline struct tls_tcb *
tls_get_tcb(void)
{
	void *self;

	__asm __volatile ("movq %%fs:%1, %0"
	    : "=r" (self)
	    : "m" (((struct tls_tcb *)0)->tcb_self));

	return(self);
}

/* Get the current thread. */
static __inline void *
tls_get_curthread(void)
{
	void *self;

	__asm __volatile ("movq %%fs:%1, %0"
	    : "=r" (self)
	    : "m" (((struct tls_tcb *)0)->tcb_pthread));

	return(self);
}

static __inline void
tls_set_tcb(struct tls_tcb *tcb)
{
	struct tls_info info;
	int seg;

	info.base = tcb;
	info.size = -1;
	seg = set_tls_area(0, &info, sizeof(info));
	/*__asm __volatile("movl %0, %%fs" : : "r" (seg));*/
}

static __inline void
tls_set_gs(void *base, size_t bytes)
{
	struct tls_info info;
	int seg;

	info.base = base;
	info.size = bytes;
	seg = set_tls_area(1, &info, sizeof(info));
	/*__asm __volatile("mov %0, %%fs" : : "g" (seg));*/
}

#ifndef _KERNEL

struct tls_tcb	*_rtld_allocate_tls(void);
struct tls_tcb	*_libc_allocate_tls(void);
void		 _rtld_free_tls(struct tls_tcb *);
void		 _libc_free_tls(struct tls_tcb *);
void		 _rtld_call_init(void);
struct tls_tcb	*_libc_init_tls(void);
struct tls_tcb	*_init_tls(void);

#endif

#endif	/* !_CPU_TLS_H_ */
