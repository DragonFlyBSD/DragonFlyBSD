/*
 * Copyright (c) 2015 François Tigeot
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _LINUX_GFP_H_
#define _LINUX_GFP_H_

#include <vm/vm_page.h>
#include <machine/bus_dma.h>

#define GFP_ATOMIC	M_NOWAIT
#define GFP_KERNEL	M_WAITOK
#define GFP_TEMPORARY	M_WAITOK
#define __GFP_ZERO	M_ZERO

#define GFP_DMA32	0x10000	/* XXX: MUST NOT collide with the M_XXX definitions */

static inline void __free_page(struct vm_page *page)
{
	vm_page_free_contig(page, PAGE_SIZE);
}

static inline struct vm_page * alloc_page(int flags)
{
	vm_paddr_t high = ~0LLU;

	if (flags & GFP_DMA32)
		high = BUS_SPACE_MAXADDR_32BIT;

	return vm_page_alloc_contig(0LLU, ~0LLU,
			PAGE_SIZE, PAGE_SIZE, PAGE_SIZE,
			VM_MEMATTR_DEFAULT);
}

#endif	/* _LINUX_GFP_H_ */
