/**
 * \file drm_memory.c
 * Memory management wrappers for DRM
 *
 * \author Rickard E. (Rik) Faith <faith@valinux.com>
 * \author Gareth Hughes <gareth@valinux.com>
 */

/*
 * Created: Thu Feb  4 14:00:34 1999 by faith@valinux.com
 *
 * Copyright 1999 Precision Insight, Inc., Cedar Park, Texas.
 * Copyright 2000 VA Linux Systems, Inc., Sunnyvale, California.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * VA LINUX SYSTEMS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#include <linux/highmem.h>
#include <linux/export.h>
#include <linux/kconfig.h>
#include <drm/drmP.h>
#include "drm_legacy.h"

#if IS_ENABLED(CONFIG_AGP)

#ifdef HAVE_PAGE_AGP
# include <asm/agp.h>
#else
# ifdef __powerpc__
#  define PAGE_AGP	pgprot_noncached_wc(PAGE_KERNEL)
# else
#  define PAGE_AGP	PAGE_KERNEL
# endif
#endif


#else /*  CONFIG_AGP  */
static inline void *agp_remap(unsigned long offset, unsigned long size,
			      struct drm_device * dev)
{
	return NULL;
}

#endif /* CONFIG_AGP */

void drm_legacy_ioremap(struct drm_local_map *map, struct drm_device *dev)
{
	map->handle = ioremap(map->offset, map->size);
}
EXPORT_SYMBOL(drm_legacy_ioremap);

void drm_legacy_ioremap_wc(struct drm_local_map *map, struct drm_device *dev)
{
	map->handle = ioremap_wc(map->offset, map->size);
}
EXPORT_SYMBOL(drm_legacy_ioremap_wc);

void drm_legacy_ioremapfree(struct drm_local_map *map, struct drm_device *dev)
{
	if (!map->handle || !map->size)
		return;

	pmap_unmapdev((vm_offset_t) map->handle, map->size);
}
EXPORT_SYMBOL(drm_legacy_ioremapfree);

#if 0
u64 drm_get_max_iomem(void)
{
	struct resource *tmp;
	resource_size_t max_iomem = 0;

	for (tmp = iomem_resource.child; tmp; tmp = tmp->sibling) {
		max_iomem = max(max_iomem,  tmp->end);
	}

	return max_iomem;
}
EXPORT_SYMBOL(drm_get_max_iomem);
#endif