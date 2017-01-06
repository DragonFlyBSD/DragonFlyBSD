/*
 * Copyright (c) 2014-2016 François Tigeot
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

#include <machine/pmap.h>
#include <vm/pmap.h>
#include <vm/vm.h>

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/bug.h>
#include <asm/page.h>
#include <asm/io.h>

SLIST_HEAD(iomap_list_head, iomap) iomap_list = SLIST_HEAD_INITIALIZER(iomap_list);

void __iomem *
__ioremap_common(unsigned long phys_addr, unsigned long size, int cache_mode)
{
	struct iomap *imp;

	/* Ensure mappings are page-aligned */
	BUG_ON(phys_addr & PAGE_MASK);
	BUG_ON(size & PAGE_MASK);

	imp = kmalloc(sizeof(struct iomap), M_DRM, M_WAITOK);
	imp->paddr = phys_addr;
	imp->npages = size / PAGE_SIZE;
	imp->pmap_addr = pmap_mapdev_attr(phys_addr, size, cache_mode);
	SLIST_INSERT_HEAD(&iomap_list, imp, im_iomaps);

	return imp->pmap_addr;
}

void iounmap(void __iomem *ptr)
{
	struct iomap *imp, *tmp_imp;
	int found = 0;
	int indx;
	vm_paddr_t paddr_end;

	SLIST_FOREACH_MUTABLE(imp, &iomap_list, im_iomaps, tmp_imp) {
		if (imp->pmap_addr == ptr) {
			found = 1;
			break;
		}
	}

	if (!found) {
		kprintf("iounmap: invalid address %p\n", ptr);
		return;
	}

	paddr_end = imp->paddr + (imp->npages * PAGE_SIZE) - 1;
	/* Is this address space range backed by regular memory ? */
	for (indx = 0; phys_avail[indx].phys_end != 0; ++indx) {
		vm_paddr_t range_start = phys_avail[indx].phys_beg;
		vm_paddr_t size = phys_avail[indx].phys_end -
				  phys_avail[indx].phys_beg;
		vm_paddr_t range_end = range_start + size - 1;

		if ((imp->paddr >= range_start) && (paddr_end <= range_end)) {
			/* Yes, change page caching attributes */
			pmap_change_attr(imp->paddr, imp->npages, PAT_WRITE_BACK);
			break;
		}

	}

	pmap_unmapdev((vm_offset_t)imp->pmap_addr, imp->npages * PAGE_SIZE);

	SLIST_REMOVE(&iomap_list, imp, iomap, im_iomaps);
	kfree(imp);
}
