/*
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

#ifndef _LINUX_IOMAPPING_H_
#define _LINUX_IOMAPPING_H_

#include <linux/types.h>
#include <linux/slab.h>
#include <linux/bug.h>
#include <linux/io.h>
#include <asm/page.h>

#include <asm/iomap.h>

struct io_mapping {
	resource_size_t base;
	unsigned long size;
	unsigned long prot;
	void *vaddr;
};

static inline struct io_mapping *
io_mapping_create_wc(resource_size_t base, unsigned long size)
{
	struct io_mapping *map;

	map = kmalloc(sizeof(struct io_mapping), M_DRM, M_WAITOK);
	map->base = base;
	map->size = size;
	map->prot = VM_MEMATTR_WRITE_COMBINING;

	map->vaddr = pmap_mapdev_attr(base, size,
					VM_MEMATTR_WRITE_COMBINING);
	if (map->vaddr == NULL)
		return NULL;

	return map;
}

static inline void io_mapping_free(struct io_mapping *mapping)
{
	/* Default memory attribute is write-back */
	pmap_mapdev_attr(mapping->base, mapping->size, VM_MEMATTR_WRITE_BACK);
	kfree(mapping);
}

static inline void *
io_mapping_map_wc(struct io_mapping *mapping,
		  unsigned long offset,
		  unsigned long size)
{
	BUG_ON(offset >= mapping->size);

	return ioremap_wc(mapping->base + offset, size);
}

static inline void *
io_mapping_map_atomic_wc(struct io_mapping *mapping, unsigned long offset)
{
	return ioremap_wc(mapping->base + offset, PAGE_SIZE);
}

static inline void
io_mapping_unmap(void *vaddr)
{
	iounmap(vaddr);
}

static inline void
io_mapping_unmap_atomic(void *vaddr)
{
	iounmap(vaddr);
}

static inline struct io_mapping *
io_mapping_init_wc(struct io_mapping *iomap,
		   resource_size_t base,
		   unsigned long size)
{
	iomap->base = base;
	iomap->size = size;
	iomap->vaddr = ioremap_wc(base, size);
	iomap->prot = pgprot_writecombine(PAGE_KERNEL_IO);

	return iomap;
}

static inline void
io_mapping_fini(struct io_mapping *mapping)
{
	iounmap(mapping->vaddr);
}

#endif	/* _LINUX_IOMAPPING_H_ */
