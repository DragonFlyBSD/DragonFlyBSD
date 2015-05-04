/*-
 * Copyright (c) 2010 Isilon Systems, Inc.
 * Copyright (c) 2010 iX Systems, Inc.
 * Copyright (c) 2010 Panasas, Inc.
 * Copyright (c) 2013, 2014 Mellanox Technologies, Ltd.
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
#ifndef	_LINUX_MM_H_
#define	_LINUX_MM_H_

#include <linux/errno.h>
#include <linux/kernel.h>

#include <asm/page.h>
#include <asm/pgtable.h>

#define PAGE_ALIGN(addr) round_page(addr)

struct vm_area_struct {
	vm_offset_t	vm_start;
	vm_offset_t	vm_end;
	vm_offset_t	vm_pgoff;
	vm_paddr_t	vm_pfn;		/* PFN For mmap. */
	vm_memattr_t	vm_page_prot;
};

/*
 * Compute log2 of the power of two rounded up count of pages
 * needed for size bytes.
 */
static inline int
get_order(unsigned long size)
{
	int order;

	size = (size - 1) >> PAGE_SHIFT;
	order = 0;
	while (size) {
		order++;
		size >>= 1;
	}
	return (order);
}

/*
 * This only works via mmap ops.
 */
static inline int
io_remap_pfn_range(struct vm_area_struct *vma,
    unsigned long addr, unsigned long pfn, unsigned long size,
    vm_memattr_t prot)
{
	vma->vm_page_prot = prot;
	vma->vm_pfn = pfn;

	return (0);
}

static inline unsigned long
vma_pages(struct vm_area_struct *vma)
{
	unsigned long size;

	size = vma->vm_end - vma->vm_start;

	return size >> PAGE_SHIFT;
}

#define offset_in_page(off)	((off) & PAGE_MASK)

static inline void
set_page_dirty(struct vm_page *page)
{
	vm_page_dirty(page);
}

#endif	/* _LINUX_MM_H_ */
