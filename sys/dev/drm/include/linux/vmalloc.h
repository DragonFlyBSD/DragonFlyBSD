/*
 * Copyright (c) 2015-2016 François Tigeot
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

#ifndef _LINUX_VMALLOC_H_
#define _LINUX_VMALLOC_H_

#include <vm/vm_extern.h>

static inline void *
vmap(struct vm_page **pages, unsigned int count,
		unsigned long flags, unsigned long prot)
{
	vm_offset_t off;
	size_t size;

	size = count * PAGE_SIZE;
	off = kmem_alloc_nofault(&kernel_map, size,
				 VM_SUBSYS_DRM_VMAP, PAGE_SIZE);
	if (off == 0)
		return (NULL);

	pmap_qenter(off, pages, count);

	return (void *)off;
}

/*
 * DragonFly note: kmem_free() requires a page count, linux code augmented
 * to provide it.
 */
static inline void
vunmap(const void *addr, unsigned int count)
{
	pmap_qremove((vm_offset_t)addr, count);
	kmem_free(&kernel_map, (vm_offset_t)addr, IDX_TO_OFF(count));
}

#endif	/* _LINUX_VMALLOC_H_ */
