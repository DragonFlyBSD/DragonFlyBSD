/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * TTM device glue for the native NVIDIA GPU driver.
 */

#include "nvdrm_nouveau_abi.h"
#include "nvgpu_ttm.h"
#include "nvgpu_bo.h"
#include "nvgpu_debug.h"
#include "nvgpu_device.h"
#include "nvgpu_unload.h"
#include "nvgsp_bar.h"
#include "nvgsp_state.h"
#include "nvgsp_vram.h"

#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/rman.h>
#include <sys/systm.h>
#include <vm/vm.h>
#include <machine/atomic.h>

#include <asm/page.h>
#include <linux/highmem.h>
#include <linux/io.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <drm/drmP.h>
#include <drm/ttm/ttm_bo_api.h>
#include <drm/ttm/ttm_bo_driver.h>
#include <drm/ttm/ttm_memory.h>
#include <drm/ttm/ttm_page_alloc.h>
#include <drm/ttm/ttm_placement.h>
#include <drm/ttm/ttm_tt.h>

#define DRM_FILE_PAGE_OFFSET		(0x100000000ULL >> PAGE_SHIFT)
#define NVGPU_TTM_MOVE_BAR1_CHUNK_SIZE	(8ULL << 20)
#define NVGPU_TTM_LPT_PAGE_SIZE		(64ULL << 10)
#define NVGPU_TTM_PD0_PAGE_SIZE		(2ULL << 20)

struct nvgpu_ttm {
	struct nvgpu_device *gpu;
	struct ttm_bo_device bdev;
	struct ttm_bo_global_ref bo_global_ref;
	struct drm_global_reference mem_global_ref;
	bool mem_global_referenced;
	bool bdev_initialized;
	bool tt_initialized;
	bool vram_initialized;
};

struct nvgpu_ttm_dma {
	void *kva;
	bus_addr_t paddr;
	bus_size_t size;
	bus_dma_tag_t tag;
	bus_dmamap_t map;
};

struct nvgpu_ttm_tt {
	struct ttm_dma_tt ttm;
	struct nvgpu_ttm_dma contig;
	bus_size_t contig_alignment;
	unsigned long contig_accounted_pages;
	bool prefer_contig;
	bool contig_populated;
};

static struct nvgpu_ttm *
nvgpu_ttm_from_bdev(struct ttm_bo_device *bdev)
{
	return (container_of(bdev, struct nvgpu_ttm, bdev));
}

static void
nvgpu_ttm_dma_capture_paddr(void *arg, bus_dma_segment_t *segs, int nseg,
    int error)
{
	bus_addr_t *paddr = arg;

	if (error != 0)
		return;
	KKASSERT(nseg == 1);
	*paddr = segs[0].ds_addr;
}

static int
nvgpu_ttm_dma_alloc(struct nvgpu_device *gpu, bus_size_t size,
    bus_size_t alignment, struct nvgpu_ttm_dma *out)
{
	device_t dev = nvgpu_device_get_newbus_dev(gpu);
	void *kva = NULL;
	bus_dma_tag_t tag = NULL;
	bus_dmamap_t map = NULL;
	bus_addr_t paddr = 0;

	if (bus_dma_tag_create(bus_get_dma_tag(dev), alignment, 0,
	    BUS_SPACE_MAXADDR, BUS_SPACE_MAXADDR, size, 1, size, 0,
	    &tag) != 0)
		return (ENOMEM);
	if (bus_dmamem_alloc(tag, &kva, BUS_DMA_WAITOK | BUS_DMA_ZERO,
	    &map) != 0) {
		bus_dma_tag_destroy(tag);
		return (ENOMEM);
	}
	if (bus_dmamap_load(tag, map, kva, size, nvgpu_ttm_dma_capture_paddr,
	    &paddr, BUS_DMA_WAITOK) != 0) {
		bus_dmamem_free(tag, kva, map);
		bus_dma_tag_destroy(tag);
		return (ENOMEM);
	}
	out->kva = kva;
	out->paddr = paddr;
	out->size = size;
	out->tag = tag;
	out->map = map;
	return (0);
}

static void
nvgpu_ttm_dma_free(struct nvgpu_ttm_dma *mem)
{
	if (mem->kva == NULL)
		return;
	bus_dmamap_unload(mem->tag, mem->map);
	bus_dmamem_free(mem->tag, mem->kva, mem->map);
	bus_dma_tag_destroy(mem->tag);
	mem->kva = NULL;
	mem->paddr = 0;
	mem->size = 0;
	mem->tag = NULL;
	mem->map = NULL;
}

static int
nvgpu_ttm_mem_global_init(struct drm_global_reference *ref)
{
	return (ttm_mem_global_init(ref->object));
}

static void
nvgpu_ttm_mem_global_release(struct drm_global_reference *ref)
{
	ttm_mem_global_release(ref->object);
}

static int
nvgpu_ttm_global_init(struct nvgpu_ttm *ttm)
{
	struct drm_global_reference *global_ref;
	int error;

	global_ref = &ttm->mem_global_ref;
	global_ref->global_type = DRM_GLOBAL_TTM_MEM;
	global_ref->size = sizeof(struct ttm_mem_global);
	global_ref->init = nvgpu_ttm_mem_global_init;
	global_ref->release = nvgpu_ttm_mem_global_release;
	error = drm_global_item_ref(global_ref);
	if (error != 0)
		return (error);

	ttm->bo_global_ref.mem_glob = ttm->mem_global_ref.object;
	global_ref = &ttm->bo_global_ref.ref;
	global_ref->global_type = DRM_GLOBAL_TTM_BO;
	global_ref->size = sizeof(struct ttm_bo_global);
	global_ref->init = ttm_bo_global_init;
	global_ref->release = ttm_bo_global_release;
	error = drm_global_item_ref(global_ref);
	if (error != 0) {
		drm_global_item_unref(&ttm->mem_global_ref);
		return (error);
	}

	ttm->mem_global_referenced = true;
	return (0);
}

static void
nvgpu_ttm_global_fini(struct nvgpu_ttm *ttm)
{
	if (!ttm->mem_global_referenced)
		return;
	drm_global_item_unref(&ttm->bo_global_ref.ref);
	drm_global_item_unref(&ttm->mem_global_ref);
	ttm->mem_global_referenced = false;
}

static int
nvgpu_ttm_tt_bind(struct ttm_tt *ttm __unused, struct ttm_mem_reg *mem __unused)
{
	return (0);
}

static int
nvgpu_ttm_tt_unbind(struct ttm_tt *ttm __unused)
{
	return (0);
}

static bool
nvgpu_ttm_tt_unpopulate_contig(struct nvgpu_ttm *nvttm,
    struct nvgpu_ttm_tt *ntt)
{
	struct ttm_tt *ttm = &ntt->ttm.ttm;
	struct ttm_mem_global *mem_glob = ttm->bdev->glob->mem_glob;

	if (!ntt->contig_populated)
		return (false);
	for (unsigned long i = 0; i < ntt->contig_accounted_pages; i++) {
		if (ttm->pages[i] != NULL)
			ttm_mem_global_free_page(mem_glob, ttm->pages[i], PAGE_SIZE);
		ttm->pages[i] = NULL;
		ntt->ttm.dma_address[i] = 0;
	}
	ntt->contig_accounted_pages = 0;
	nvgpu_ttm_dma_free(&ntt->contig);
	ntt->contig_populated = false;
	ttm->state = tt_unpopulated;
	(void)nvttm;
	return (true);
}

static void
nvgpu_ttm_tt_destroy(struct ttm_tt *ttm)
{
	struct nvgpu_ttm *nvttm = nvgpu_ttm_from_bdev(ttm->bdev);
	struct nvgpu_ttm_tt *ntt = (void *)ttm;

	(void)nvgpu_ttm_tt_unpopulate_contig(nvttm, ntt);
	ttm_dma_tt_fini(&ntt->ttm);
	kfree(ntt);
}

static struct ttm_backend_func nvgpu_ttm_backend_func = {
	.bind = nvgpu_ttm_tt_bind,
	.unbind = nvgpu_ttm_tt_unbind,
	.destroy = nvgpu_ttm_tt_destroy,
};

static struct ttm_tt *
nvgpu_ttm_tt_create(struct ttm_buffer_object *tbo, uint32_t page_flags)
{
	struct nvgpu_bo *bo = nvgpu_bo_from_ttm(tbo);
	struct nvgpu_ttm_tt *ntt;
	uint64_t alignment;

	ntt = kzalloc(sizeof(*ntt), GFP_KERNEL);
	if (ntt == NULL)
		return (NULL);
	ntt->ttm.ttm.func = &nvgpu_ttm_backend_func;
	if (ttm_dma_tt_init(&ntt->ttm, tbo, page_flags) != 0) {
		kfree(ntt);
		return (NULL);
	}
	alignment = (uint64_t)tbo->mem.page_alignment << PAGE_SHIFT;
	if ((bo->domain & NOUVEAU_GEM_DOMAIN_GART) != 0 &&
	    alignment >= NVGPU_TTM_LPT_PAGE_SIZE &&
	    tbo->num_pages >= (NVGPU_TTM_LPT_PAGE_SIZE >> PAGE_SHIFT)) {
		ntt->prefer_contig = true;
		ntt->contig_alignment = (bus_size_t)alignment;
	}
	return (&ntt->ttm.ttm);
}

static int
nvgpu_ttm_tt_populate_contig(struct nvgpu_ttm *nvttm,
    struct nvgpu_ttm_tt *ntt, struct ttm_operation_ctx *ctx)
{
	struct ttm_tt *ttm = &ntt->ttm.ttm;
	struct ttm_mem_global *mem_glob = ttm->bdev->glob->mem_glob;
	bus_size_t size = (bus_size_t)ttm->num_pages << PAGE_SHIFT;
	int error;

	if (!ntt->prefer_contig || ntt->contig_populated ||
	    ttm->num_pages > (ULONG_MAX >> PAGE_SHIFT))
		return (EINVAL);

	error = nvgpu_ttm_dma_alloc(nvttm->gpu, size, ntt->contig_alignment,
	    &ntt->contig);
	if (error != 0)
		return (error);

	for (unsigned long i = 0; i < ttm->num_pages; i++) {
		struct page *page;
		vm_paddr_t paddr = (vm_paddr_t)ntt->contig.paddr +
		    ((vm_paddr_t)i << PAGE_SHIFT);

		page = (struct page *)PHYS_TO_VM_PAGE(paddr);
		error = ttm_mem_global_alloc_page(mem_glob, page, PAGE_SIZE, ctx);
		if (error != 0) {
			for (unsigned long j = 0; j < i; j++) {
				if (ttm->pages[j] != NULL)
					ttm_mem_global_free_page(mem_glob, ttm->pages[j],
					    PAGE_SIZE);
				ttm->pages[j] = NULL;
				ntt->ttm.dma_address[j] = 0;
			}
			nvgpu_ttm_dma_free(&ntt->contig);
			ntt->contig_accounted_pages = 0;
			return (error);
		}
		ttm->pages[i] = page;
		ntt->ttm.dma_address[i] = (dma_addr_t)paddr;
		ntt->contig_accounted_pages++;
	}

	ttm->state = tt_unbound;
	ntt->contig_populated = true;
	return (0);
}

static int
nvgpu_ttm_tt_populate(struct ttm_tt *ttm, struct ttm_operation_ctx *ctx)
{
	struct nvgpu_ttm *nvttm = nvgpu_ttm_from_bdev(ttm->bdev);
	struct pci_dev *pdev = nvgpu_device_get_drm_pdev(nvttm->gpu);
	struct nvgpu_ttm_tt *ntt = (void *)ttm;
	struct ttm_dma_tt *dma = (void *)ttm;
	int error;

	if (ttm->state != tt_unpopulated)
		return (0);
	if (ntt->prefer_contig) {
		error = nvgpu_ttm_tt_populate_contig(nvttm, ntt, ctx);
		if (error == 0)
			return (0);
	}
	error = ttm_pool_populate(ttm, ctx);
	if (error != 0)
		return (error);
	for (unsigned long i = 0; i < ttm->num_pages; i++)
		dma->dma_address[i] = pci_map_page(pdev, ttm->pages[i], 0,
		    PAGE_SIZE, PCI_DMA_BIDIRECTIONAL);
	return (0);
}

static void
nvgpu_ttm_tt_unpopulate(struct ttm_tt *ttm)
{
	struct nvgpu_ttm *nvttm = nvgpu_ttm_from_bdev(ttm->bdev);
	struct pci_dev *pdev = nvgpu_device_get_drm_pdev(nvttm->gpu);
	struct nvgpu_ttm_tt *ntt = (void *)ttm;
	struct ttm_dma_tt *dma = (void *)ttm;

	if (nvgpu_ttm_tt_unpopulate_contig(nvttm, ntt))
		return;
	for (unsigned long i = 0; i < ttm->num_pages; i++) {
		if (dma->dma_address[i] != 0)
			pci_unmap_page(pdev, dma->dma_address[i], PAGE_SIZE,
			    PCI_DMA_BIDIRECTIONAL);
		dma->dma_address[i] = 0;
	}
	ttm_pool_unpopulate(ttm);
}

static int
nvgpu_ttm_invalidate_caches(struct ttm_bo_device *bdev __unused,
    uint32_t flags __unused)
{
	return (0);
}

static int
nvgpu_ttm_vram_man_init(struct ttm_mem_type_manager *man,
    unsigned long p_size __unused)
{
	man->priv = nvgpu_ttm_from_bdev(man->bdev);
	return (0);
}

static int
nvgpu_ttm_vram_man_takedown(struct ttm_mem_type_manager *man)
{
	man->priv = NULL;
	return (0);
}

static int
nvgpu_ttm_vram_man_get_node(struct ttm_mem_type_manager *man,
    struct ttm_buffer_object *tbo, const struct ttm_place *place,
    struct ttm_mem_reg *mem)
{
	struct nvgpu_ttm *ttm = man->priv;
	struct nvgpu_bo *bo = nvgpu_bo_from_ttm(tbo);
	struct nvgsp_vram_alloc *alloc;
	uint64_t align;
	uint64_t base;
	uint64_t start;
	uint64_t lpfn;
	uint64_t size;

	mem->mm_node = NULL;
	size = (uint64_t)mem->num_pages << PAGE_SHIFT;
	align = mem->page_alignment != 0 ?
	    (uint64_t)mem->page_alignment << PAGE_SHIFT : PAGE_SIZE;
	if (size >= NVGPU_TTM_LPT_PAGE_SIZE && align < NVGPU_TTM_LPT_PAGE_SIZE)
		align = NVGPU_TTM_LPT_PAGE_SIZE;
	base = nvgsp_state_get_fb_usable_base(ttm->gpu);
	lpfn = place->lpfn != 0 ? place->lpfn : man->size;

	alloc = nvgsp_vram_alloc_gem(ttm->gpu, size, align, bo);
	if (alloc == NULL)
		return (0);
	if (nvgsp_vram_alloc_get_paddr(alloc) < base ||
	    ((nvgsp_vram_alloc_get_paddr(alloc) - base) & (PAGE_SIZE - 1)) != 0) {
		nvgsp_vram_free_gem(ttm->gpu, alloc, bo);
		return (0);
	}

	start = (nvgsp_vram_alloc_get_paddr(alloc) - base) >> PAGE_SHIFT;
	if (start < place->fpfn || start + mem->num_pages > lpfn) {
		nvgsp_vram_free_gem(ttm->gpu, alloc, bo);
		return (0);
	}

	mem->mm_node = nvgsp_vram_alloc_get_node(alloc);
	mem->start = start;
	bo->vram_alloc = alloc;
	bo->paddr = nvgsp_vram_alloc_get_paddr(alloc);
	return (0);
}

static void
nvgpu_ttm_vram_man_put_node(struct ttm_mem_type_manager *man,
    struct ttm_mem_reg *mem)
{
	struct nvgpu_ttm *ttm = man->priv;
	struct nvgsp_vram_alloc *alloc;
	struct nvgpu_bo *bo;

	if (mem->mm_node == NULL)
		return;
	alloc = nvgsp_vram_alloc_from_node(mem->mm_node);
	bo = nvgsp_vram_alloc_get_owner(alloc);
	if (bo != NULL && bo->vram_alloc == alloc) {
		bo->vram_alloc = NULL;
		if (bo->paddr == nvgsp_vram_alloc_get_paddr(alloc))
			bo->paddr = 0;
	}
	nvgsp_vram_free_gem(ttm->gpu, alloc, bo);
	mem->mm_node = NULL;
}

static void
nvgpu_ttm_vram_man_debug(struct ttm_mem_type_manager *man,
    struct drm_printer *printer)
{
	drm_printf(printer, "nvgpu vram manager: size=%llu pages\n",
	    (unsigned long long)man->size);
}

static const struct ttm_mem_type_manager_func nvgpu_ttm_vram_manager_func = {
	.init = nvgpu_ttm_vram_man_init,
	.takedown = nvgpu_ttm_vram_man_takedown,
	.get_node = nvgpu_ttm_vram_man_get_node,
	.put_node = nvgpu_ttm_vram_man_put_node,
	.debug = nvgpu_ttm_vram_man_debug,
};

static int
nvgpu_ttm_init_mem_type(struct ttm_bo_device *bdev, uint32_t type,
    struct ttm_mem_type_manager *man)
{
	switch (type) {
	case TTM_PL_SYSTEM:
		man->flags = TTM_MEMTYPE_FLAG_MAPPABLE;
		man->available_caching = TTM_PL_MASK_CACHING;
		man->default_caching = TTM_PL_FLAG_CACHED;
		break;
	case TTM_PL_TT:
		man->func = &ttm_bo_manager_func;
		man->gpu_offset = 0;
		man->available_caching = TTM_PL_MASK_CACHING;
		man->default_caching = TTM_PL_FLAG_CACHED;
		man->flags = TTM_MEMTYPE_FLAG_MAPPABLE | TTM_MEMTYPE_FLAG_CMA;
		break;
	case TTM_PL_VRAM:
		man->func = &nvgpu_ttm_vram_manager_func;
		man->gpu_offset = nvgsp_state_get_fb_usable_base(
		    nvgpu_ttm_from_bdev(bdev)->gpu);
		man->flags = TTM_MEMTYPE_FLAG_FIXED | TTM_MEMTYPE_FLAG_MAPPABLE;
		man->available_caching = TTM_PL_FLAG_UNCACHED | TTM_PL_FLAG_WC;
		man->default_caching = TTM_PL_FLAG_WC;
		man->io_reserve_fastpath = false;
		man->use_io_reserve_lru = true;
		break;
	default:
		nvgpu_log(NVGPU_LOG_INFO, "unsupported TTM memory type %u\n", type);
		return (-EINVAL);
	}
	return (0);
}

static void
nvgpu_ttm_move_null(struct ttm_buffer_object *tbo, struct ttm_mem_reg *new_mem)
{
	struct ttm_mem_reg old_mem = tbo->mem;

	tbo->mem = *new_mem;
	new_mem->mm_node = NULL;
	ttm_bo_mem_put(tbo, &old_mem);
}

static int
nvgpu_ttm_move_chunk_copy_page(struct ttm_tt *ttm, void *iomap,
    unsigned long page_index, unsigned long chunk_page, bool vram_to_sysmem)
{
	struct page *page;
	void *kva;
	void *io_page;

	if (ttm == NULL || ttm->pages == NULL || page_index >= ttm->num_pages)
		return (-ENXIO);
	page = ttm->pages[page_index];
	if (page == NULL)
		return (-ENOMEM);

	io_page = (uint8_t *)iomap + (chunk_page << PAGE_SHIFT);
	kva = kmap(page);
	if (kva == NULL)
		return (-ENOMEM);
	if (vram_to_sysmem)
		memcpy_fromio(kva, io_page, PAGE_SIZE);
	else
		memcpy_toio(io_page, kva, PAGE_SIZE);
	kunmap(page);
	return (0);
}

static int
nvgpu_ttm_move_chunked_bar1(struct ttm_buffer_object *tbo,
    struct ttm_operation_ctx *ctx, struct ttm_mem_reg *new_mem)
{
	struct nvgpu_bo *bo = nvgpu_bo_from_ttm(tbo);
	struct nvgpu_ttm *ttm = nvgpu_ttm_from_bdev(tbo->bdev);
	struct ttm_mem_reg old_copy = tbo->mem;
	struct nvgsp_vram_alloc *vram_alloc;
	bool vram_to_sysmem;
	uint64_t size;
	uint64_t done;
	int error;

	if (old_copy.mem_type == TTM_PL_VRAM &&
	    new_mem->mem_type == TTM_PL_SYSTEM) {
		vram_to_sysmem = true;
		if (old_copy.mm_node == NULL || tbo->ttm == NULL)
			return (-ENXIO);
		vram_alloc = nvgsp_vram_alloc_from_node(old_copy.mm_node);
	} else if (old_copy.mem_type == TTM_PL_SYSTEM &&
	    new_mem->mem_type == TTM_PL_VRAM) {
		vram_to_sysmem = false;
		if (new_mem->mm_node == NULL || tbo->ttm == NULL)
			return (-ENXIO);
		vram_alloc = nvgsp_vram_alloc_from_node(new_mem->mm_node);
	} else {
		return (-EOPNOTSUPP);
	}

	error = ttm_bo_wait(tbo, ctx->interruptible, ctx->no_wait_gpu);
	if (error != 0)
		return (error);
	error = ttm_tt_populate(tbo->ttm, ctx);
	if (error != 0)
		return (error);

	size = (uint64_t)new_mem->num_pages << PAGE_SHIFT;
	if (size > bo->base.size)
		size = bo->base.size;
	for (done = 0; done < size;) {
		uint64_t chunk = MIN(size - done, NVGPU_TTM_MOVE_BAR1_CHUNK_SIZE);
		uint64_t gva = 0;
		void *iomap;

		chunk = round_page(chunk);
		if (done + chunk > size)
			chunk = size - done;
		error = nvgsp_bar_map_vram_range(ttm->gpu,
		    nvgsp_vram_alloc_get_paddr(vram_alloc) + done, chunk, &gva);
		if (error != 0)
			return (error < 0 ? error : -error);
		iomap = ioremap_wc(rman_get_start(nvgpu_device_get_bar(ttm->gpu, 1)) +
		    gva, chunk);
		if (iomap == NULL) {
			nvgsp_bar_unmap_vram_range(ttm->gpu, gva, chunk);
			return (-ENOMEM);
		}

		for (uint64_t off = 0; off < chunk; off += PAGE_SIZE) {
			unsigned long page_index =
			    (unsigned long)((done + off) >> PAGE_SHIFT);
			unsigned long chunk_page = (unsigned long)(off >> PAGE_SHIFT);

			error = nvgpu_ttm_move_chunk_copy_page(tbo->ttm, iomap,
			    page_index, chunk_page, vram_to_sysmem);
			if (error != 0)
				break;
		}
		mb();
		iounmap(iomap);
		nvgsp_bar_unmap_vram_range(ttm->gpu, gva, chunk);
		if (error != 0)
			return (error);
		done += chunk;
	}

	tbo->mem = *new_mem;
	new_mem->mm_node = NULL;
	if (vram_to_sysmem) {
		bo->vram_alloc = NULL;
		if (bo->paddr == nvgsp_vram_alloc_get_paddr(vram_alloc))
			bo->paddr = 0;
	} else {
		bo->vram_alloc = vram_alloc;
		bo->paddr = nvgsp_vram_alloc_get_paddr(vram_alloc);
		ttm_tt_destroy(tbo->ttm);
		tbo->ttm = NULL;
	}
	ttm_bo_mem_put(tbo, &old_copy);
	return (0);
}

static int
nvgpu_ttm_bo_move(struct ttm_buffer_object *tbo, bool evict __unused,
    struct ttm_operation_ctx *ctx, struct ttm_mem_reg *new_mem)
{
	struct nvgpu_bo *bo = nvgpu_bo_from_ttm(tbo);
	struct ttm_mem_reg *old_mem = &tbo->mem;
	bool chunked_vram_move;
	int error;

	if (old_mem->mem_type == TTM_PL_SYSTEM && tbo->ttm == NULL) {
		nvgpu_ttm_move_null(tbo, new_mem);
		error = 0;
	} else if ((old_mem->mem_type == TTM_PL_TT &&
	    new_mem->mem_type == TTM_PL_SYSTEM) ||
	    (old_mem->mem_type == TTM_PL_SYSTEM &&
	    new_mem->mem_type == TTM_PL_TT)) {
		error = ttm_bo_move_ttm(tbo, ctx, new_mem);
	} else {
		chunked_vram_move = (old_mem->mem_type == TTM_PL_VRAM &&
		    new_mem->mem_type == TTM_PL_SYSTEM) ||
		    (old_mem->mem_type == TTM_PL_SYSTEM &&
		    new_mem->mem_type == TTM_PL_VRAM);
		if (chunked_vram_move)
			error = nvgpu_ttm_move_chunked_bar1(tbo, ctx, new_mem);
		else
			error = ttm_bo_move_memcpy(tbo, ctx, new_mem);
	}
	if (error == 0)
		nvgpu_bo_refresh_ttm_domain(bo, bo->preferred_domain);
	return (error);
}

static void
nvgpu_ttm_evict_flags(struct ttm_buffer_object *bo __unused,
    struct ttm_placement *placement)
{
	static const struct ttm_place system = {
		.fpfn = 0,
		.lpfn = 0,
		.flags = TTM_PL_FLAG_SYSTEM | TTM_PL_FLAG_CACHED,
	};

	placement->placement = &system;
	placement->busy_placement = &system;
	placement->num_placement = 1;
	placement->num_busy_placement = 1;
}

static int
nvgpu_ttm_verify_access(struct ttm_buffer_object *bo __unused,
    struct file *filp __unused)
{
	return (0);
}

static int
nvgpu_ttm_io_mem_reserve(struct ttm_bo_device *bdev, struct ttm_mem_reg *mem)
{
	struct nvgpu_ttm *ttm = nvgpu_ttm_from_bdev(bdev);
	struct ttm_mem_type_manager *man = &bdev->man[mem->mem_type];
	struct nvgsp_vram_alloc *alloc;
	uint32_t pages;
	int error;

	mem->bus.addr = NULL;
	mem->bus.offset = 0;
	mem->bus.size = mem->num_pages << PAGE_SHIFT;
	mem->bus.base = 0;
	mem->bus.is_iomem = false;
	if ((man->flags & TTM_MEMTYPE_FLAG_MAPPABLE) == 0)
		return (-EINVAL);

	switch (mem->mem_type) {
	case TTM_PL_SYSTEM:
	case TTM_PL_TT:
		return (0);
	case TTM_PL_VRAM:
		if (mem->mm_node == NULL || nvgpu_device_get_bar(ttm->gpu, 1) == NULL)
			return (-EINVAL);
		alloc = nvgsp_vram_alloc_from_node(mem->mm_node);
		pages = (uint32_t)mem->num_pages;
		if (pages == 0 || (uint64_t)pages != mem->num_pages)
			return (-EINVAL);
		error = nvgsp_vram_alloc_map_bar1_scatter(ttm->gpu, alloc,
		    mem->bus.size, pages);
		if (error != 0)
			return (error < 0 ? error : -error);
		mem->bus.offset = nvgsp_vram_alloc_bar1_gva(alloc, 0);
		mem->bus.base = rman_get_start(nvgpu_device_get_bar(ttm->gpu, 1));
		mem->bus.is_iomem = true;
		return (0);
	default:
		return (-EINVAL);
	}
}

static void
nvgpu_ttm_io_mem_free(struct ttm_bo_device *bdev, struct ttm_mem_reg *mem)
{
	struct nvgpu_ttm *ttm = nvgpu_ttm_from_bdev(bdev);
	struct nvgsp_vram_alloc *alloc;

	if (mem->mem_type != TTM_PL_VRAM || mem->mm_node == NULL)
		return;
	alloc = nvgsp_vram_alloc_from_node(mem->mm_node);
	nvgsp_vram_alloc_unmap_bar1_scatter(ttm->gpu, alloc);
}

static unsigned long
nvgpu_ttm_io_mem_pfn(struct ttm_buffer_object *tbo, unsigned long page_offset)
{
	struct nvgpu_bo *bo = nvgpu_bo_from_ttm(tbo);
	uint64_t gva;

	if (tbo->mem.mem_type != TTM_PL_VRAM || tbo->mem.mm_node == NULL ||
	    bo->base.dev == NULL || nvgpu_device_get_bar(bo->base.dev->dev_private, 1) == NULL)
		return (0);
	gva = nvgsp_vram_alloc_bar1_gva(nvgsp_vram_alloc_from_node(tbo->mem.mm_node),
	    page_offset);
	if (gva == 0)
		return (0);
	return ((unsigned long)((rman_get_start(nvgpu_device_get_bar(
	    bo->base.dev->dev_private, 1)) + gva) >> PAGE_SHIFT));
}

static struct ttm_bo_driver nvgpu_ttm_bo_driver = {
	.ttm_tt_create = nvgpu_ttm_tt_create,
	.ttm_tt_populate = nvgpu_ttm_tt_populate,
	.ttm_tt_unpopulate = nvgpu_ttm_tt_unpopulate,
	.invalidate_caches = nvgpu_ttm_invalidate_caches,
	.init_mem_type = nvgpu_ttm_init_mem_type,
	.eviction_valuable = ttm_bo_eviction_valuable,
	.evict_flags = nvgpu_ttm_evict_flags,
	.move = nvgpu_ttm_bo_move,
	.verify_access = nvgpu_ttm_verify_access,
	.io_mem_reserve = nvgpu_ttm_io_mem_reserve,
	.io_mem_free = nvgpu_ttm_io_mem_free,
	.io_mem_pfn = nvgpu_ttm_io_mem_pfn,
};

int
nvgpu_ttm_init(struct nvgpu_device *gpu, struct drm_device *ddev)
{
	struct nvgpu_ttm *ttm;
	uint64_t tt_size;
	uint64_t vram_size;
	int error;

	ttm = kzalloc(sizeof(*ttm), GFP_KERNEL);
	if (ttm == NULL)
		return (ENOMEM);
	ttm->gpu = gpu;

	error = nvgpu_ttm_global_init(ttm);
	if (error != 0)
		goto fail;

	error = ttm_bo_device_init(&ttm->bdev, ttm->bo_global_ref.ref.object,
	    &nvgpu_ttm_bo_driver, NULL, DRM_FILE_PAGE_OFFSET, false);
	if (error != 0)
		goto fail_global;
	ttm->bdev_initialized = true;
	ttm->bdev.no_retry = true;

	vram_size = nvgsp_state_get_fb_usable_size(gpu);
	if (vram_size != 0) {
		error = ttm_bo_init_mm(&ttm->bdev, TTM_PL_VRAM,
		    vram_size >> PAGE_SHIFT);
		if (error != 0)
			goto fail_bdev;
		ttm->vram_initialized = true;
	}

	tt_size = vram_size;
	if (tt_size == 0)
		tt_size = 256ULL << 20;
	error = ttm_bo_init_mm(&ttm->bdev, TTM_PL_TT, tt_size >> PAGE_SHIFT);
	if (error != 0)
		goto fail_vram;
	ttm->tt_initialized = true;

	nvgpu_device_set_ttm(gpu, ttm);
	ddev->drm_ttm_bdev = &ttm->bdev;
	nvgpu_log(NVGPU_LOG_INFO,
	    "ttm initialized: vram=0x%llx tt=0x%llx\n",
	    (unsigned long long)vram_size, (unsigned long long)tt_size);
	return (0);

fail_vram:
	if (ttm->vram_initialized)
		ttm_bo_clean_mm(&ttm->bdev, TTM_PL_VRAM);
fail_bdev:
	(void)ttm_bo_device_release(&ttm->bdev);
fail_global:
	nvgpu_ttm_global_fini(ttm);
fail:
	kfree(ttm);
	return (error < 0 ? -error : error);
}

void
nvgpu_ttm_fini(struct nvgpu_device *gpu)
{
	struct nvgpu_ttm *ttm = nvgpu_device_get_ttm(gpu);
	struct drm_device *ddev = nvgpu_device_get_drm_dev(gpu);

	if (ttm == NULL)
		return;
	if (ddev != NULL && ddev->drm_ttm_bdev == &ttm->bdev)
		ddev->drm_ttm_bdev = NULL;
	if (ttm->tt_initialized)
		ttm_bo_clean_mm(&ttm->bdev, TTM_PL_TT);
	if (ttm->vram_initialized)
		ttm_bo_clean_mm(&ttm->bdev, TTM_PL_VRAM);
	if (ttm->bdev_initialized)
		(void)ttm_bo_device_release(&ttm->bdev);
	nvgpu_ttm_global_fini(ttm);
	nvgpu_device_set_ttm(gpu, NULL);
	kfree(ttm);
}

struct ttm_bo_device *
nvgpu_ttm_get_bo_device(struct nvgpu_device *gpu)
{
	struct nvgpu_ttm *ttm = nvgpu_device_get_ttm(gpu);

	return (ttm != NULL ? &ttm->bdev : NULL);
}

static struct nvgpu_device *
nvgpu_ttm_tbo_gpu(struct ttm_buffer_object *tbo)
{
	struct nvgpu_bo *bo = nvgpu_bo_from_ttm(tbo);

	return (bo->base.dev != NULL ? bo->base.dev->dev_private : NULL);
}

static int
nvgpu_ttm_pager_hold(void *handle, vm_ooffset_t size __unused,
    vm_prot_t prot __unused, vm_ooffset_t foff __unused,
    struct ucred *cred __unused, u_short *color)
{
	struct ttm_buffer_object *tbo = handle;
	struct nvgpu_bo *bo = nvgpu_bo_from_ttm(tbo);
	struct nvgpu_device *gpu = nvgpu_ttm_tbo_gpu(tbo);
	int error;

	if (atomic_cmpset_int(&bo->mmap_pager_active, 0, 1)) {
		if (gpu != NULL) {
			error = nvgpu_unload_hold_by_mmap(gpu);
			if (error != 0) {
				atomic_store_rel_int(&bo->mmap_pager_active, 0);
				return (error);
			}
		}
		nvgpu_bo_addref(bo);
	}
	*color = 0;
	return (0);
}

static void
nvgpu_ttm_pager_release(void *handle)
{
	struct ttm_buffer_object *tbo = handle;
	struct nvgpu_bo *bo = nvgpu_bo_from_ttm(tbo);
	struct nvgpu_device *gpu = nvgpu_ttm_tbo_gpu(tbo);
	int error;

	atomic_store_rel_int(&bo->mmap_pager_active, 0);
	error = ttm_bo_reserve(tbo, false, true, NULL);
	if (error == 0) {
		ttm_bo_unmap_virtual(tbo);
		ttm_bo_unreserve(tbo);
	}
	nvgpu_bo_release(bo);
	if (gpu != NULL)
		nvgpu_unload_release_by_mmap(gpu);
}

static int
nvgpu_ttm_pager_fault(vm_object_t vm_obj, vm_ooffset_t offset, int prot,
    vm_page_t *mres)
{
	struct ttm_buffer_object *tbo = vm_obj->handle;

	return (ttm_bo_vm_fault_bo_dfly(tbo, vm_obj, offset, prot, mres));
}

static struct cdev_pager_ops nvgpu_ttm_pager_ops = {
	.cdev_pg_ctor = nvgpu_ttm_pager_hold,
	.cdev_pg_dtor = nvgpu_ttm_pager_release,
	.cdev_pg_fault = nvgpu_ttm_pager_fault,
};

int
nvgpu_ttm_mmap_single(struct file *fp, struct drm_device *dev,
    vm_ooffset_t *offset, vm_size_t size, struct vm_object **obj_res,
    int nprot)
{
	struct ttm_buffer_object *tbo;
	struct vm_area_struct vma;
	struct vm_object *vm_obj;
	int ret;

	*obj_res = NULL;
	if (dev->drm_ttm_bdev == NULL)
		return (ENODEV);

	bzero(&vma, sizeof(vma));
	vma.vm_start = *offset;
	vma.vm_end = vma.vm_start + size;
	vma.vm_pgoff = vma.vm_start >> PAGE_SHIFT;
	ret = ttm_bo_mmap(fp, &vma, dev->drm_ttm_bdev);
	if (ret != 0)
		return (ret);

	tbo = vma.vm_private_data;
	vm_obj = cdev_pager_allocate(tbo, OBJT_MGTDEVICE, &nvgpu_ttm_pager_ops,
	    size, nprot, 0, curthread->td_ucred);
	ttm_bo_put(tbo);
	if (vm_obj == NULL)
		return (EINVAL);
	*obj_res = vm_obj;
	*offset = 0;
	return (0);
}
