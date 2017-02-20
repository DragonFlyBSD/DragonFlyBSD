/*
 * Copyright 2008 Advanced Micro Devices, Inc.
 * Copyright 2008 Red Hat Inc.
 * Copyright 2009 Jerome Glisse.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: Dave Airlie
 *          Alex Deucher
 *          Jerome Glisse
 *
 * $FreeBSD: head/sys/dev/drm2/radeon/radeon_gart.c 254885 2013-08-25 19:37:15Z dumbbell $
 */

#include <drm/drmP.h>
#include "drm/drm_legacy.h"		/* for drm_dma_handle_t */
#include <uapi_drm/radeon_drm.h>
#include "radeon.h"

/*
 * GART
 * The GART (Graphics Aperture Remapping Table) is an aperture
 * in the GPU's address space.  System pages can be mapped into
 * the aperture and look like contiguous pages from the GPU's
 * perspective.  A page table maps the pages in the aperture
 * to the actual backing pages in system memory.
 *
 * Radeon GPUs support both an internal GART, as described above,
 * and AGP.  AGP works similarly, but the GART table is configured
 * and maintained by the northbridge rather than the driver.
 * Radeon hw has a separate AGP aperture that is programmed to
 * point to the AGP aperture provided by the northbridge and the
 * requests are passed through to the northbridge aperture.
 * Both AGP and internal GART can be used at the same time, however
 * that is not currently supported by the driver.
 *
 * This file handles the common internal GART management.
 */

/*
 * Common GART table functions.
 */
/**
 * radeon_gart_table_ram_alloc - allocate system ram for gart page table
 *
 * @rdev: radeon_device pointer
 *
 * Allocate system memory for GART page table
 * (r1xx-r3xx, non-pcie r4xx, rs400).  These asics require the
 * gart table to be in system memory.
 * Returns 0 for success, -ENOMEM for failure.
 */
int radeon_gart_table_ram_alloc(struct radeon_device *rdev)
{
	drm_dma_handle_t *dmah;

	dmah = drm_pci_alloc(rdev->ddev, rdev->gart.table_size,
	    PAGE_SIZE);
	if (dmah == NULL) {
		return -ENOMEM;
	}
	rdev->gart.dmah = dmah;
	rdev->gart.ptr = dmah->vaddr;
#if defined(__i386) || defined(__amd64)
	if (rdev->family == CHIP_RS400 || rdev->family == CHIP_RS480 ||
	    rdev->family == CHIP_RS690 || rdev->family == CHIP_RS740) {
		pmap_change_attr((vm_offset_t)rdev->gart.ptr,
		    rdev->gart.table_size >> PAGE_SHIFT, PAT_UNCACHED);
	}
#endif
	rdev->gart.table_addr = dmah->busaddr;
	memset((void *)rdev->gart.ptr, 0, rdev->gart.table_size);
	return 0;
}

/**
 * radeon_gart_table_ram_free - free system ram for gart page table
 *
 * @rdev: radeon_device pointer
 *
 * Free system memory for GART page table
 * (r1xx-r3xx, non-pcie r4xx, rs400).  These asics require the
 * gart table to be in system memory.
 */
void radeon_gart_table_ram_free(struct radeon_device *rdev)
{
	if (rdev->gart.ptr == NULL) {
		return;
	}
#if defined(__i386) || defined(__amd64)
	if (rdev->family == CHIP_RS400 || rdev->family == CHIP_RS480 ||
	    rdev->family == CHIP_RS690 || rdev->family == CHIP_RS740) {
		pmap_change_attr((vm_offset_t)rdev->gart.ptr,
		    rdev->gart.table_size >> PAGE_SHIFT, PAT_WRITE_COMBINING);
	}
#endif
	drm_pci_free(rdev->ddev, rdev->gart.dmah);
	rdev->gart.dmah = NULL;
	rdev->gart.ptr = NULL;
	rdev->gart.table_addr = 0;
}

/**
 * radeon_gart_table_vram_alloc - allocate vram for gart page table
 *
 * @rdev: radeon_device pointer
 *
 * Allocate video memory for GART page table
 * (pcie r4xx, r5xx+).  These asics require the
 * gart table to be in video memory.
 * Returns 0 for success, error for failure.
 */
int radeon_gart_table_vram_alloc(struct radeon_device *rdev)
{
	int r;

	if (rdev->gart.robj == NULL) {
		r = radeon_bo_create(rdev, rdev->gart.table_size,
				     PAGE_SIZE, true, RADEON_GEM_DOMAIN_VRAM,
				     0, NULL, &rdev->gart.robj);
		if (r) {
			return r;
		}
	}
	return 0;
}

/**
 * radeon_gart_table_vram_pin - pin gart page table in vram
 *
 * @rdev: radeon_device pointer
 *
 * Pin the GART page table in vram so it will not be moved
 * by the memory manager (pcie r4xx, r5xx+).  These asics require the
 * gart table to be in video memory.
 * Returns 0 for success, error for failure.
 */
int radeon_gart_table_vram_pin(struct radeon_device *rdev)
{
	u64 gpu_addr;
	int r;

	r = radeon_bo_reserve(rdev->gart.robj, false);
	if (unlikely(r != 0))
		return r;
	r = radeon_bo_pin(rdev->gart.robj,
				RADEON_GEM_DOMAIN_VRAM, &gpu_addr);
	if (r) {
		radeon_bo_unreserve(rdev->gart.robj);
		return r;
	}
	r = radeon_bo_kmap(rdev->gart.robj, &rdev->gart.ptr);
	if (r)
		radeon_bo_unpin(rdev->gart.robj);
	radeon_bo_unreserve(rdev->gart.robj);
	rdev->gart.table_addr = gpu_addr;
	return r;
}

/**
 * radeon_gart_table_vram_unpin - unpin gart page table in vram
 *
 * @rdev: radeon_device pointer
 *
 * Unpin the GART page table in vram (pcie r4xx, r5xx+).
 * These asics require the gart table to be in video memory.
 */
void radeon_gart_table_vram_unpin(struct radeon_device *rdev)
{
	int r;

	if (rdev->gart.robj == NULL) {
		return;
	}
	r = radeon_bo_reserve(rdev->gart.robj, false);
	if (likely(r == 0)) {
		radeon_bo_kunmap(rdev->gart.robj);
		radeon_bo_unpin(rdev->gart.robj);
		radeon_bo_unreserve(rdev->gart.robj);
		rdev->gart.ptr = NULL;
	}
}

/**
 * radeon_gart_table_vram_free - free gart page table vram
 *
 * @rdev: radeon_device pointer
 *
 * Free the video memory used for the GART page table
 * (pcie r4xx, r5xx+).  These asics require the gart table to
 * be in video memory.
 */
void radeon_gart_table_vram_free(struct radeon_device *rdev)
{
	if (rdev->gart.robj == NULL) {
		return;
	}
	radeon_bo_unref(&rdev->gart.robj);
}

/*
 * Common gart functions.
 */
/**
 * radeon_gart_unbind - unbind pages from the gart page table
 *
 * @rdev: radeon_device pointer
 * @offset: offset into the GPU's gart aperture
 * @pages: number of pages to unbind
 *
 * Unbinds the requested pages from the gart page table and
 * replaces them with the dummy page (all asics).
 */
void radeon_gart_unbind(struct radeon_device *rdev, unsigned offset,
			int pages)
{
	unsigned t;
	unsigned p;
	int i, j;
	u64 page_base;

	if (!rdev->gart.ready) {
		WARN(1, "trying to unbind memory from uninitialized GART !\n");
		return;
	}
	t = offset / RADEON_GPU_PAGE_SIZE;
	p = t / (PAGE_SIZE / RADEON_GPU_PAGE_SIZE);
	for (i = 0; i < pages; i++, p++) {
		if (rdev->gart.pages[p]) {
			rdev->gart.pages[p] = NULL;
			rdev->gart.pages_addr[p] = rdev->dummy_page.addr;
			page_base = rdev->gart.pages_addr[p];
			for (j = 0; j < (PAGE_SIZE / RADEON_GPU_PAGE_SIZE); j++, t++) {
				if (rdev->gart.ptr) {
					radeon_gart_set_page(rdev, t, page_base,
							     RADEON_GART_PAGE_DUMMY);
				}
				page_base += RADEON_GPU_PAGE_SIZE;
			}
		}
	}
	mb();
	radeon_gart_tlb_flush(rdev);
}

/**
 * radeon_gart_bind - bind pages into the gart page table
 *
 * @rdev: radeon_device pointer
 * @offset: offset into the GPU's gart aperture
 * @pages: number of pages to bind
 * @pagelist: pages to bind
 * @dma_addr: DMA addresses of pages
 * @flags: RADEON_GART_PAGE_* flags
 *
 * Binds the requested pages to the gart page table
 * (all asics).
 * Returns 0 for success, -EINVAL for failure.
 */
int radeon_gart_bind(struct radeon_device *rdev, unsigned offset,
		     int pages, vm_page_t *pagelist, dma_addr_t *dma_addr,
		     uint32_t flags)
{
	unsigned t;
	unsigned p;
	uint64_t page_base;
	int i, j;

	if (!rdev->gart.ready) {
		WARN(1, "trying to bind memory to uninitialized GART !\n");
		return -EINVAL;
	}
	t = offset / RADEON_GPU_PAGE_SIZE;
	p = t / (PAGE_SIZE / RADEON_GPU_PAGE_SIZE);

	for (i = 0; i < pages; i++, p++) {
		rdev->gart.pages_addr[p] = dma_addr[i];
		rdev->gart.pages[p] = pagelist[i];
		if (rdev->gart.ptr) {
			page_base = rdev->gart.pages_addr[p];
			for (j = 0; j < (PAGE_SIZE / RADEON_GPU_PAGE_SIZE); j++, t++) {
				radeon_gart_set_page(rdev, t, page_base, flags);
				page_base += RADEON_GPU_PAGE_SIZE;
			}
		}
	}
	mb();
	radeon_gart_tlb_flush(rdev);
	return 0;
}

/**
 * radeon_gart_init - init the driver info for managing the gart
 *
 * @rdev: radeon_device pointer
 *
 * Allocate the dummy page and init the gart driver info (all asics).
 * Returns 0 for success, error for failure.
 */
int radeon_gart_init(struct radeon_device *rdev)
{
	int r, i;

	if (rdev->gart.pages) {
		return 0;
	}
	/* We need PAGE_SIZE >= RADEON_GPU_PAGE_SIZE */
	if (PAGE_SIZE < RADEON_GPU_PAGE_SIZE) {
		DRM_ERROR("Page size is smaller than GPU page size!\n");
		return -EINVAL;
	}
	r = radeon_dummy_page_init(rdev);
	if (r)
		return r;
	/* Compute table size */
	rdev->gart.num_cpu_pages = rdev->mc.gtt_size / PAGE_SIZE;
	rdev->gart.num_gpu_pages = rdev->mc.gtt_size / RADEON_GPU_PAGE_SIZE;
	DRM_INFO("GART: num cpu pages %u, num gpu pages %u\n",
		 rdev->gart.num_cpu_pages, rdev->gart.num_gpu_pages);
	/* Allocate pages table */
	rdev->gart.pages = kmalloc(sizeof(void *) * rdev->gart.num_cpu_pages,
				   M_DRM, M_ZERO | M_WAITOK);
	if (rdev->gart.pages == NULL) {
		radeon_gart_fini(rdev);
		return -ENOMEM;
	}
	rdev->gart.pages_addr = kmalloc(sizeof(dma_addr_t) * rdev->gart.num_cpu_pages,
					M_DRM, M_ZERO | M_WAITOK);
	if (rdev->gart.pages_addr == NULL) {
		radeon_gart_fini(rdev);
		return -ENOMEM;
	}
	/* set GART entry to point to the dummy page by default */
	for (i = 0; i < rdev->gart.num_cpu_pages; i++) {
		rdev->gart.pages_addr[i] = rdev->dummy_page.addr;
	}
	return 0;
}

/**
 * radeon_gart_fini - tear down the driver info for managing the gart
 *
 * @rdev: radeon_device pointer
 *
 * Tear down the gart driver info and free the dummy page (all asics).
 */
void radeon_gart_fini(struct radeon_device *rdev)
{
	if (rdev->gart.pages && rdev->gart.pages_addr && rdev->gart.ready) {
		/* unbind pages */
		radeon_gart_unbind(rdev, 0, rdev->gart.num_cpu_pages);
	}
	rdev->gart.ready = false;
	kfree(rdev->gart.pages);
	kfree(rdev->gart.pages_addr);
	rdev->gart.pages = NULL;
	rdev->gart.pages_addr = NULL;

	radeon_dummy_page_fini(rdev);
}
