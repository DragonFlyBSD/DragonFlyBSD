/*
 * Copyright (c) 2010 by The DragonFly Project and Samuel J. Greear.
 * All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Samuel J. Greear <sjg@thesjg.com>
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

#ifndef _CPU_LWBUF_H_
#define _CPU_LWBUF_H_

#ifndef _SYS_TYPES_H_
#include <sys/types.h>
#endif
#ifndef _SYS_GLOBALDATA_H_
#include <sys/globaldata.h>
#endif
#ifndef _VM_PMAP_H_
#include <vm/pmap.h>
#endif
#ifndef _VM_VM_PAGE_H_
#include <vm/vm_page.h>
#endif
#include <machine/atomic.h>
#ifndef _MACHINE_VMPARAM_H_
#include <machine/vmparam.h>
#endif

#if !defined(_KERNEL) && !defined(_KERNEL_STRUCTURES)
#error "This file should not be included by userland programs."
#endif

struct lwbuf {
	vm_page_t	m;	/* currently mapped page */
	vm_offset_t	kva;	/* va of mapping */
};

static __inline vm_page_t
lwbuf_page(struct lwbuf *lwb)
{
	return (lwb->m);
}

static __inline vm_offset_t
lwbuf_kva(struct lwbuf *lwb)
{
	return (lwb->kva);
}

static __inline struct lwbuf *
lwbuf_alloc(vm_page_t m, struct lwbuf *lwb_cache)
{
	struct lwbuf *lwb = lwb_cache;

	lwb->m = m;
	lwb->kva = PHYS_TO_DMAP(VM_PAGE_TO_PHYS(lwb->m));

	return (lwb);
}

static __inline void
lwbuf_free(struct lwbuf *lwb)
{
	lwb->m = NULL;	/* safety */
}

#define lwbuf_set_global(lwb)

/* tell the kernel that lwbufs are cheap */
#define LWBUF_IS_OPTIMAL

#if defined(_KERNEL)

#endif

#endif /* !_CPU_LWBUF_H_ */
