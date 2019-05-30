/*
 * Copyright (c) 2015-2019 François Tigeot <ftigeot@wolfpond.org>
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

#ifndef _ASM_CACHEFLUSH_H_
#define _ASM_CACHEFLUSH_H_

#include <asm/special_insns.h>

#include <vm/pmap.h>
#include <vm/vm_page.h>

static inline int
set_memory_uc(unsigned long addr, int numpages)
{
	pmap_change_attr(addr, numpages, PAT_UNCACHED);
	return 0;
}

static inline int set_memory_wc(unsigned long vaddr, int numpages)
{
	pmap_change_attr(vaddr, numpages, PAT_WRITE_COMBINING);
	return 0;
}

static inline int set_memory_wb(unsigned long vaddr, int numpages)
{
	pmap_change_attr(vaddr, numpages, PAT_WRITE_BACK);
	return 0;
}

static inline int set_pages_uc(struct page *page, int num_pages)
{
	struct vm_page *p = (struct vm_page *)page;

	pmap_change_attr(PHYS_TO_DMAP(VM_PAGE_TO_PHYS(p)),
			 num_pages, PAT_UNCACHED);

	return 0;
}

static inline int set_pages_wb(struct page *page, int num_pages)
{
	struct vm_page *p = (struct vm_page *)page;

	pmap_change_attr(PHYS_TO_DMAP(VM_PAGE_TO_PHYS(p)),
			 num_pages, PAT_WRITE_BACK);

	return 0;
}

static inline int
set_pages_array_uc(struct page **pages, int addrinarray)
{
	for (int i = 0; i < addrinarray; i++)
		pmap_page_set_memattr((struct vm_page *)pages[i], VM_MEMATTR_UNCACHEABLE);

	return 0;
}

static inline int
set_pages_array_wb(struct page **pages, int addrinarray)
{
	for (int i = 0; i < addrinarray; i++)
		pmap_page_set_memattr((struct vm_page *)pages[i], VM_MEMATTR_WRITE_BACK);

	return 0;
}

static inline int
set_pages_array_wc(struct page **pages, int addrinarray)
{
	for (int i = 0; i < addrinarray; i++)
		pmap_page_set_memattr((struct vm_page *)pages[i], VM_MEMATTR_WRITE_COMBINING);

	return 0;
}

#endif	/* _ASM_CACHEFLUSH_H_ */
