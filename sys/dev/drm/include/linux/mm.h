/*-
 * Copyright (c) 2010 Isilon Systems, Inc.
 * Copyright (c) 2010 iX Systems, Inc.
 * Copyright (c) 2010 Panasas, Inc.
 * Copyright (c) 2013, 2014 Mellanox Technologies, Ltd.
 * Copyright (c) 2015 Matthew Dillon <dillon@backplane.com>
 * Copyright (c) 2015-2020 François Tigeot <ftigeot@wolfpond.org>
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

#include <linux/mmdebug.h>
#include <linux/gfp.h>
#include <linux/bug.h>
#include <linux/list.h>
#include <linux/mmzone.h>
#include <linux/rbtree.h>
#include <linux/atomic.h>
#include <linux/mm_types.h>
#include <linux/err.h>
#include <linux/shrinker.h>

#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/processor.h>

static inline struct page *
nth_page(struct page *page, int n)
{
	return page + n;
}

#define PAGE_ALIGN(addr) round_page(addr)

#define VM_FAULT_RETRY		0x0400

#define FAULT_FLAG_ALLOW_RETRY		0x04
#define FAULT_FLAG_RETRY_NOWAIT		0x08

struct vm_fault {
	unsigned int flags;
	void __user *virtual_address;
};

#define VM_FAULT_NOPAGE		0x0001
#define VM_FAULT_SIGBUS		0x0002
#define VM_FAULT_OOM		0x0004

#define VM_DONTDUMP	0x0001
#define VM_DONTEXPAND	0x0002
#define VM_IO		0x0004
#define VM_MIXEDMAP	0x0008

struct vm_operations_struct {
	int (*fault)(struct vm_area_struct *vma, struct vm_fault *vmf);
	void (*open)(struct vm_area_struct *vma);
	void (*close)(struct vm_area_struct *vma);
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

#define offset_in_page(off)	((unsigned long)(off) & PAGE_MASK)

static inline void
set_page_dirty(struct page *page)
{
	vm_page_dirty((struct vm_page *)page);
}

static inline void
get_page(struct vm_page *page)
{
	vm_page_hold(page);
}

extern vm_paddr_t Realmem;

static inline unsigned long get_num_physpages(void)
{
	return Realmem / PAGE_SIZE;
}

int is_vmalloc_addr(const void *x);

static inline void
unmap_mapping_range(struct address_space *mapping,
	loff_t const holebegin, loff_t const holelen, int even_cows)
{
}

#define VM_SHARED	0x00000008

#define VM_PFNMAP	0x00000400

static inline struct page *
vmalloc_to_page(const void *addr)
{
	vm_paddr_t paddr;

	paddr = pmap_kextract((vm_offset_t)addr);
	return (struct page *)(PHYS_TO_VM_PAGE(paddr));
}

static inline void
put_page(struct page *page)
{
	vm_page_busy_wait((struct vm_page *)page, FALSE, "i915gem");
	vm_page_unwire((struct vm_page *)page, 1);
	vm_page_wakeup((struct vm_page *)page);
}

static inline void *
page_address(const struct page *page)
{
	return (void *)VM_PAGE_TO_PHYS((const struct vm_page *)page);
}

void * kvmalloc_array(size_t n, size_t size, gfp_t flags);

#define kvfree(addr)	kfree(addr)

#endif	/* _LINUX_MM_H_ */
