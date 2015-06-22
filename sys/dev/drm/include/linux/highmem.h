/*
 * Copyright (c) 2014 François Tigeot
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

#ifndef _LINUX_HIGHMEM_H_
#define _LINUX_HIGHMEM_H_

#include <machine/vmparam.h>

#include <linux/uaccess.h>

#include <asm/cacheflush.h>

static inline void *kmap(struct vm_page *pg)
{
	return (void *)PHYS_TO_DMAP(VM_PAGE_TO_PHYS(pg));
}

static inline void kunmap(struct vm_page *pg)
{
	/* Nothing to do on systems with a direct memory map */
}

static inline void *kmap_atomic(struct vm_page *pg)
{
	return (void *)PHYS_TO_DMAP(VM_PAGE_TO_PHYS(pg));
}

static inline void kunmap_atomic(void *vaddr)
{
	/* Nothing to do on systems with a direct memory map */
}

#endif	/* _LINUX_HIGHMEM_H_ */
