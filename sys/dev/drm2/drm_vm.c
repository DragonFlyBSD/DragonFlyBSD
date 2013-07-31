/*-
 * Copyright 2003 Eric Anholt
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
 * ERIC ANHOLT BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * $FreeBSD: src/sys/dev/drm2/drm_vm.c,v 1.1 2012/05/22 11:07:44 kib Exp $
 */

/** @file drm_vm.c
 * Support code for mmaping of DRM maps.
 */

#include <sys/conf.h>
#include <dev/drm2/drmP.h>
#include <dev/drm2/drm.h>
#include <sys/mutex2.h>
#include <vm/vm_page.h>
#include <vm/vm_pager.h>

int
drm_mmap(struct dev_mmap_args *ap)
/*
		struct cdev *kdev, vm_ooffset_t offset, vm_paddr_t *paddr,
    int prot, vm_memattr_t *memattr)*/
{
	struct cdev *kdev = ap->a_head.a_dev;
	vm_offset_t offset = ap->a_offset;
	struct drm_device *dev = drm_get_device_from_kdev(kdev);
	struct drm_file *file_priv = NULL;
	drm_local_map_t *map;
	enum drm_map_type type;
	vm_paddr_t phys;

	/* d_mmap gets called twice, we can only reference file_priv during
	 * the first call.  We need to assume that if error is EBADF the
	 * call was succesful and the client is authenticated.
	 */
	DRM_LOCK(dev);
	file_priv = drm_find_file_by_proc(dev, curthread);
	DRM_UNLOCK(dev);

	if (!file_priv) {
		DRM_ERROR("Could not find authenticator!\n");
		return EINVAL;
	}

	if (!file_priv->authenticated)
		return EACCES;

	DRM_DEBUG("called with offset %016jx\n", (uintmax_t)offset);
	if (dev->dma && offset < ptoa(dev->dma->page_count)) {
		drm_device_dma_t *dma = dev->dma;

		DRM_SPINLOCK(&dev->dma_lock);

		if (dma->pagelist != NULL) {
			unsigned long page = offset >> PAGE_SHIFT;
			phys = dma->pagelist[page];

			DRM_SPINUNLOCK(&dev->dma_lock);
			return 0;
		} else {
			DRM_SPINUNLOCK(&dev->dma_lock);
			return -1;
		}
	}

	/* A sequential search of a linked list is
	   fine here because: 1) there will only be
	   about 5-10 entries in the list and, 2) a
	   DRI client only has to do this mapping
	   once, so it doesn't have to be optimized
	   for performance, even if the list was a
	   bit longer.
	*/
	DRM_LOCK(dev);
	TAILQ_FOREACH(map, &dev->maplist, link) {
		if (offset >> DRM_MAP_HANDLE_SHIFT ==
		    (unsigned long)map->handle >> DRM_MAP_HANDLE_SHIFT)
			break;
	}

	if (map == NULL) {
		DRM_DEBUG("Can't find map, request offset = %016jx\n",
		    (uintmax_t)offset);
		TAILQ_FOREACH(map, &dev->maplist, link) {
			DRM_DEBUG("map offset = %016lx, handle = %016lx\n",
			    map->offset, (unsigned long)map->handle);
		}
		DRM_UNLOCK(dev);
		return -1;
	}
	if (((map->flags & _DRM_RESTRICTED) && !DRM_SUSER(DRM_CURPROC))) {
		DRM_UNLOCK(dev);
		DRM_DEBUG("restricted map\n");
		return -1;
	}
	type = map->type;
	DRM_UNLOCK(dev);

	offset = offset & ((1ULL << DRM_MAP_HANDLE_SHIFT) - 1);

	switch (type) {
	case _DRM_FRAME_BUFFER:
	case _DRM_AGP:
#if 0	/* XXX */
		memattr = VM_MEMATTR_WRITE_COMBINING;
#endif
		/* FALLTHROUGH */
	case _DRM_REGISTERS:
		phys = map->offset + offset;
		break;
	case _DRM_SCATTER_GATHER:
#if 0	/* XXX */
		memattr = VM_MEMATTR_WRITE_COMBINING;
#endif
		/* FALLTHROUGH */
	case _DRM_CONSISTENT:
	case _DRM_SHM:
		phys = vtophys((char *)map->virtual + offset);
		break;
	default:
		DRM_ERROR("bad map type %d\n", type);
		return -1;	/* This should never happen. */
	}

	ap->a_result = atop(phys);
	return 0;
}

int
drm_mmap_single(struct dev_mmap_single_args *ap)
{
	struct cdev *kdev = ap->a_head.a_dev;
	return drm_gem_mmap_single(kdev, ap->a_offset, ap->a_size,
				ap->a_object, ap->a_nprot);
}

/* XXX The following is just temporary hack to replace the
 * vm_phys_fictitious functions available on FreeBSD
 */
#define VM_PHYS_FICTITIOUS_NSEGS        8
static struct vm_phys_fictitious_seg {
        vm_paddr_t      start;
        vm_paddr_t      end;
        vm_page_t       first_page;
} vm_phys_fictitious_segs[VM_PHYS_FICTITIOUS_NSEGS];
static struct mtx vm_phys_fictitious_reg_mtx = MTX_INITIALIZER;

vm_page_t
vm_phys_fictitious_to_vm_page(vm_paddr_t pa)
{
        struct vm_phys_fictitious_seg *seg;
        vm_page_t m;
        int segind;

        m = NULL;
        for (segind = 0; segind < VM_PHYS_FICTITIOUS_NSEGS; segind++) {
                seg = &vm_phys_fictitious_segs[segind];
                if (pa >= seg->start && pa < seg->end) {
                        m = &seg->first_page[atop(pa - seg->start)];
                        KASSERT((m->flags & PG_FICTITIOUS) != 0,
                            ("%p not fictitious", m));
                        break;
                }
        }
        return (m);
}

static void page_init(vm_page_t m, vm_paddr_t paddr, int pat_mode)
{
	bzero(m, sizeof(*m));

	pmap_page_init(m);

        //m->flags = PG_BUSY | PG_FICTITIOUS;
        m->flags = PG_FICTITIOUS;
        m->valid = VM_PAGE_BITS_ALL;
        m->dirty = 0;
        m->busy = 0;
        m->queue = PQ_NONE;
        m->object = NULL;
	m->pat_mode = pat_mode;

        m->wire_count = 1;
        m->hold_count = 0;
        m->phys_addr = paddr;
}

int
vm_phys_fictitious_reg_range(vm_paddr_t start, vm_paddr_t end, int pat_mode)
{
        struct vm_phys_fictitious_seg *seg;
        vm_page_t fp;
        long i, page_count;
        int segind;

        page_count = (end - start) / PAGE_SIZE;

        fp = kmalloc(page_count * sizeof(struct vm_page), DRM_MEM_DRIVER,
                    M_WAITOK | M_ZERO);

        for (i = 0; i < page_count; i++) {
                page_init(&fp[i], start + PAGE_SIZE * i, pat_mode);
        }
        mtx_lock(&vm_phys_fictitious_reg_mtx);
        for (segind = 0; segind < VM_PHYS_FICTITIOUS_NSEGS; segind++) {
                seg = &vm_phys_fictitious_segs[segind];
                if (seg->start == 0 && seg->end == 0) {
                        seg->start = start;
                        seg->end = end;
                        seg->first_page = fp;
                        mtx_unlock(&vm_phys_fictitious_reg_mtx);
                        return (0);
                }
        }
        mtx_unlock(&vm_phys_fictitious_reg_mtx);
        drm_free(fp, DRM_MEM_DRIVER);
        return (EBUSY);
}
