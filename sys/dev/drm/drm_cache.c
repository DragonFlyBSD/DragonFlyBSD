/**************************************************************************
 *
 * Copyright (c) 2006-2007 Tungsten Graphics, Inc., Cedar Park, TX., USA
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS, AUTHORS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 **************************************************************************/
/*
 * Authors: Thomas Hellström <thomas-at-tungstengraphics-dot-com>
 */

#include <linux/export.h>
#include <drm/drmP.h>
#include <asm/cpufeature.h>

/*
 * clflushopt is an unordered instruction which needs fencing with mfence or
 * sfence to avoid ordering issues.  For drm_clflush_page this fencing happens
 * in the caller.
 */
static void
drm_clflush_page(struct vm_page *page)
{
	uint8_t *page_virtual;
	unsigned int i;
	const int size = cpu_clflush_line_size;

	if (unlikely(page == NULL))
		return;

	page_virtual = kmap_atomic(page);
	for (i = 0; i < PAGE_SIZE; i += size)
		clflushopt(page_virtual + i);
	kunmap_atomic(page_virtual);
}

static void drm_cache_flush_clflush(struct vm_page *pages[],
				    unsigned long num_pages)
{
	unsigned long i;

	mb();
	for (i = 0; i < num_pages; i++)
		drm_clflush_page(*pages++);
	mb();
}

void
drm_clflush_pages(vm_page_t *pages, unsigned long num_pages)
{
	pmap_invalidate_cache_pages(pages, num_pages);

	if (static_cpu_has(X86_FEATURE_CLFLUSH)) {
		drm_cache_flush_clflush(pages, num_pages);
		return;
	}

	cpu_wbinvd_on_all_cpus();
}
EXPORT_SYMBOL(drm_clflush_pages);

void
drm_clflush_sg(struct sg_table *st)
{
	if (static_cpu_has(X86_FEATURE_CLFLUSH)) {
		struct sg_page_iter sg_iter;

		mb();
		for_each_sg_page(st->sgl, &sg_iter, st->nents, 0)
			drm_clflush_page(sg_page_iter_page(&sg_iter));
		mb();

		return;
	}

	cpu_wbinvd_on_all_cpus();
}
EXPORT_SYMBOL(drm_clflush_sg);

void
drm_clflush_virt_range(void *in_addr, unsigned long length)
{
	char *addr = in_addr;

	if (static_cpu_has(X86_FEATURE_CLFLUSH)) {
		const int size = cpu_clflush_line_size;
		char *end = addr + length;
		addr = (void *)(((unsigned long)addr) & -size);
		mb();
		for (; addr < end; addr += size)
			clflushopt(addr);
		clflushopt(end - 1); /* force serialisation */
		mb();
		return;
	}

	cpu_wbinvd_on_all_cpus();
}
EXPORT_SYMBOL(drm_clflush_virt_range);
