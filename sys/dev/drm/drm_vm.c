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
 * $FreeBSD: head/sys/dev/drm2/drm_vm.c 235783 2012-05-22 11:07:44Z kib $"
 */

/** @file drm_vm.c
 * Support code for mmaping of DRM maps.
 */

#include <sys/conf.h>
#include <sys/devfs.h>
#include <sys/mutex2.h>
#include <vm/vm_page.h>
#include <vm/vm_pager.h>

#include <drm/drmP.h>

int drm_mmap(struct dev_mmap_args *ap)
{
	struct cdev *kdev = ap->a_head.a_dev;
	vm_offset_t offset = ap->a_offset;
	struct drm_device *dev = drm_get_device_from_kdev(kdev);
	struct drm_file *file_priv = NULL;
	struct drm_local_map *map = NULL;
	int error;
	struct drm_hash_item *hash;

	enum drm_map_type type;
	vm_paddr_t phys;

	/* d_mmap gets called twice, we can only reference file_priv during
	 * the first call.  We need to assume that if error is EBADF the
	 * call was succesful and the client is authenticated.
	 */
	error = devfs_get_cdevpriv(ap->a_fp, (void **)&file_priv);
	if (error == ENOENT) {
		DRM_ERROR("Could not find authenticator!\n");
		return EINVAL;
	}

	if (file_priv && !file_priv->authenticated)
		return EACCES;

	DRM_DEBUG("called with offset %016jx\n", (uintmax_t)offset);
	if (dev->dma && offset < ptoa(dev->dma->page_count)) {
		drm_device_dma_t *dma = dev->dma;

		spin_lock(&dev->dma_lock);

		if (dma->pagelist != NULL) {
			unsigned long page = offset >> PAGE_SHIFT;
			unsigned long phys = dma->pagelist[page];

			spin_unlock(&dev->dma_lock);
			// XXX *paddr = phys;
			ap->a_result = phys;
			return 0;
		} else {
			spin_unlock(&dev->dma_lock);
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

	if (drm_ht_find_item(&dev->map_hash, offset, &hash)) {
		DRM_ERROR("Could not find map\n");
		return -EINVAL;
	}

	map = drm_hash_entry(hash, struct drm_map_list, hash)->map;
	if (map == NULL) {
		DRM_DEBUG("Can't find map, request offset = %016jx\n",
		    (uintmax_t)offset);
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

	switch (type) {
	case _DRM_FRAME_BUFFER:
	case _DRM_AGP:
#if 0	/* XXX */
		*memattr = VM_MEMATTR_WRITE_COMBINING;
#endif
		/* FALLTHROUGH */
	case _DRM_REGISTERS:
		phys = map->offset + offset;
		break;
	case _DRM_SCATTER_GATHER:
#if 0	/* XXX */
		*memattr = VM_MEMATTR_WRITE_COMBINING;
#endif
		/* FALLTHROUGH */
	case _DRM_CONSISTENT:
	case _DRM_SHM:
		phys = vtophys((char *)map->handle + offset);
		break;
	default:
		DRM_ERROR("bad map type %d\n", type);
		return -1;	/* This should never happen. */
	}

	ap->a_result = atop(phys);
	return 0;
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
static struct mtx vm_phys_fictitious_reg_mtx = MTX_INITIALIZER("vmphy");

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

int
vm_phys_fictitious_reg_range(vm_paddr_t start, vm_paddr_t end,
    vm_memattr_t memattr)
{
        struct vm_phys_fictitious_seg *seg;
        vm_page_t fp;
        long i, page_count;
        int segind;

        page_count = (end - start) / PAGE_SIZE;

        fp = kmalloc(page_count * sizeof(struct vm_page), M_DRM,
                    M_WAITOK | M_ZERO);

        for (i = 0; i < page_count; i++) {
		vm_page_initfake(&fp[i], start + PAGE_SIZE * i, memattr);
		fp[i].flags &= ~(PG_BUSY | PG_UNMANAGED);
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
        kfree(fp);
        return (EBUSY);
}

void
vm_phys_fictitious_unreg_range(vm_paddr_t start, vm_paddr_t end)
{
	struct vm_phys_fictitious_seg *seg;
	vm_page_t fp;
	int segind;

	mtx_lock(&vm_phys_fictitious_reg_mtx);
	for (segind = 0; segind < VM_PHYS_FICTITIOUS_NSEGS; segind++) {
		seg = &vm_phys_fictitious_segs[segind];
		if (seg->start == start && seg->end == end) {
			seg->start = seg->end = 0;
			fp = seg->first_page;
			seg->first_page = NULL;
			mtx_unlock(&vm_phys_fictitious_reg_mtx);
			kfree(fp);
			return;
		}
	}
	mtx_unlock(&vm_phys_fictitious_reg_mtx);
	KASSERT(0, ("Unregistering not registered fictitious range"));
}
