/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * GEM/TTM buffer objects for the native NVIDIA driver.
 */

#include "nvdrm_nouveau_abi.h"
#include "nvgpu_bo.h"
#include "nvgpu_debug.h"
#include "nvgpu_device.h"
#include "nvgpu_fence.h"
#include "nvgpu_ttm.h"
#include "nvgsp_state.h"
#include "nvgsp_vram.h"

#include <sys/errno.h>
#include <sys/kernel.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <vm/vm.h>
#include <vm/vm_page.h>
#include <machine/cpufunc.h>
#include <machine/pmap.h>

#include <linux/reservation.h>
#include <linux/slab.h>
#include <drm/drmP.h>
#include <drm/drm_gem.h>
#include <drm/drm_vma_manager.h>
#include <drm/ttm/ttm_bo_driver.h>
#include <drm/ttm/ttm_placement.h>
#include <drm/ttm/ttm_tt.h>

#define NVGPU_GMMU_SPT_SHIFT	12
#define NVGPU_GMMU_LPT_SHIFT	16
#define NVGPU_GMMU_PD0_SHIFT	21
#define NVGPU_GMMU_LPT_SIZE	(1ULL << NVGPU_GMMU_LPT_SHIFT)
#define NVGPU_GMMU_PD0_SIZE	(1ULL << NVGPU_GMMU_PD0_SHIFT)

static int
nvgpu_errno(int error)
{
	return (error < 0 ? -error : error);
}

bool
nvgpu_bo_is_vram(const struct nvgpu_bo *bo)
{
	return (bo != NULL && bo->ttm_backed && bo->tbo.mem.mem_type == TTM_PL_VRAM);
}

bool
nvgpu_bo_can_share(const struct nvgpu_bo *bo)
{
	return (bo != NULL && !bo->no_share);
}

static bool
nvgpu_bo_is_sysmem_ttm(const struct nvgpu_bo *bo)
{
	if (bo == NULL || !bo->ttm_backed || bo->tbo.ttm == NULL)
		return (false);
	return (bo->tbo.mem.mem_type == TTM_PL_TT ||
	    bo->tbo.mem.mem_type == TTM_PL_SYSTEM);
}

bool
nvgpu_bo_has_sysmem(const struct nvgpu_bo *bo)
{
	return (nvgpu_bo_is_sysmem_ttm(bo));
}

bool
nvgpu_bo_cpu_mappable(const struct nvgpu_bo *bo)
{
	if (bo == NULL)
		return (false);
	if (nvgpu_bo_has_sysmem(bo))
		return (true);
	if (nvgpu_bo_is_vram(bo))
		return (bo->vram_alloc != NULL && bo->base.dev != NULL &&
		    nvgpu_device_get_bar(bo->base.dev->dev_private, 1) != NULL);
	return (false);
}

uint64_t
nvgpu_bo_get_mmap_handle(struct nvgpu_bo *bo)
{
	if (bo == NULL || !bo->ttm_backed || !nvgpu_bo_cpu_mappable(bo))
		return (0);
	return (drm_vma_node_offset_addr(&bo->tbo.vma_node));
}

void
nvgpu_bo_refresh_ttm_domain(struct nvgpu_bo *bo, uint32_t req_domain)
{
	uint32_t flags = req_domain & ~(NOUVEAU_GEM_DOMAIN_CPU |
	    NOUVEAU_GEM_DOMAIN_VRAM | NOUVEAU_GEM_DOMAIN_GART);

	if (bo == NULL || !bo->ttm_backed)
		return;
	switch (bo->tbo.mem.mem_type) {
	case TTM_PL_VRAM:
		bo->domain = flags | NOUVEAU_GEM_DOMAIN_VRAM;
		if (bo->vram_alloc != NULL)
			bo->paddr = nvgsp_vram_alloc_get_paddr(bo->vram_alloc);
		break;
	case TTM_PL_TT:
		bo->domain = flags | NOUVEAU_GEM_DOMAIN_GART;
		bo->paddr = 0;
		break;
	case TTM_PL_SYSTEM:
	default:
		bo->domain = flags | NOUVEAU_GEM_DOMAIN_CPU;
		bo->paddr = 0;
		break;
	}
}

uint8_t
nvgpu_bo_get_gpu_page_shift(const struct nvgpu_bo *bo)
{
	if (bo == NULL || (bo->domain & NOUVEAU_GEM_DOMAIN_VRAM) == 0)
		return (NVGPU_GMMU_SPT_SHIFT);
	if (bo->base.size >= NVGPU_GMMU_PD0_SIZE &&
	    (bo->paddr & (NVGPU_GMMU_PD0_SIZE - 1)) == 0)
		return (NVGPU_GMMU_PD0_SHIFT);
	if (bo->base.size < NVGPU_GMMU_LPT_SIZE)
		return (NVGPU_GMMU_SPT_SHIFT);
	if ((bo->paddr & (NVGPU_GMMU_LPT_SIZE - 1)) != 0)
		return (NVGPU_GMMU_SPT_SHIFT);
	return (NVGPU_GMMU_LPT_SHIFT);
}

int
nvgpu_bo_ensure_ttm_populated(struct nvgpu_bo *bo)
{
	struct ttm_operation_ctx ctx = {
		.interruptible = false,
		.no_wait_gpu = false,
	};
	int error;

	if (bo == NULL || !bo->ttm_backed)
		return (0);
	if (!nvgpu_bo_is_sysmem_ttm(bo) || bo->tbo.ttm == NULL)
		return (ENXIO);
	if (bo->tbo.ttm->state != tt_unpopulated)
		return (0);

	error = ttm_bo_reserve(&bo->tbo, true, false, NULL);
	if (error != 0)
		return (nvgpu_errno(error));
	if (bo->tbo.ttm == NULL)
		error = ENXIO;
	else
		error = nvgpu_errno(ttm_tt_populate(bo->tbo.ttm, &ctx));
	ttm_bo_unreserve(&bo->tbo);
	return (error);
}

int
nvgpu_bo_get_paddr_at(const struct nvgpu_bo *bo, uint64_t offset,
    vm_paddr_t *paddr)
{
	struct ttm_dma_tt *dma;
	unsigned long page_index;

	if (bo == NULL || paddr == NULL || offset >= bo->base.size)
		return (EINVAL);

	if (nvgpu_bo_is_sysmem_ttm(bo)) {
		if (bo->tbo.ttm == NULL)
			return (ENXIO);
		dma = (struct ttm_dma_tt *)bo->tbo.ttm;
		page_index = offset >> PAGE_SHIFT;
		if (page_index >= bo->tbo.ttm->num_pages ||
		    dma->dma_address == NULL || dma->dma_address[page_index] == 0)
			return (ENXIO);
		*paddr = (vm_paddr_t)dma->dma_address[page_index] +
		    (offset & PAGE_MASK);
		return (0);
	}

	if ((bo->domain & NOUVEAU_GEM_DOMAIN_VRAM) != 0) {
		*paddr = bo->paddr + offset;
		return (0);
	}
	return (ENXIO);
}

int
nvgpu_bo_get_paddr_at_mem(const struct nvgpu_bo *bo,
    const struct ttm_mem_reg *mem, uint64_t offset, vm_paddr_t *paddr)
{
	struct nvgsp_vram_alloc *alloc;
	struct ttm_dma_tt *dma;
	unsigned long page_index;

	if (bo == NULL || mem == NULL || paddr == NULL || offset >= bo->base.size)
		return (EINVAL);
	if (!bo->ttm_backed)
		return (nvgpu_bo_get_paddr_at(bo, offset, paddr));

	switch (mem->mem_type) {
	case TTM_PL_VRAM:
		if (mem->mm_node == NULL)
			return (ENXIO);
		alloc = nvgsp_vram_alloc_from_node(mem->mm_node);
		if (offset >= nvgsp_vram_alloc_get_size(alloc))
			return (EINVAL);
		*paddr = nvgsp_vram_alloc_get_paddr(alloc) + offset;
		return (0);
	case TTM_PL_TT:
	case TTM_PL_SYSTEM:
		if (bo->tbo.ttm == NULL)
			return (ENXIO);
		dma = (struct ttm_dma_tt *)bo->tbo.ttm;
		page_index = offset >> PAGE_SHIFT;
		if (page_index >= bo->tbo.ttm->num_pages ||
		    dma->dma_address == NULL || dma->dma_address[page_index] == 0)
			return (ENXIO);
		*paddr = (vm_paddr_t)dma->dma_address[page_index] +
		    (offset & PAGE_MASK);
		return (0);
	default:
		return (ENXIO);
	}
}

int
nvgpu_bo_get_paddr_run_at(const struct nvgpu_bo *bo, uint64_t offset,
    uint64_t max_size, vm_paddr_t *paddr, uint64_t *run_size)
{
	vm_paddr_t first, next;
	uint64_t limit, run, page_left, chunk;
	int error;

	if (bo == NULL || paddr == NULL || run_size == NULL || max_size == 0 ||
	    offset >= bo->base.size)
		return (EINVAL);
	limit = MIN(max_size, bo->base.size - offset);
	if ((bo->domain & NOUVEAU_GEM_DOMAIN_VRAM) != 0) {
		*paddr = bo->paddr + offset;
		*run_size = limit;
		return (0);
	}

	error = nvgpu_bo_get_paddr_at(bo, offset, &first);
	if (error != 0)
		return (error);
	page_left = PAGE_SIZE - (offset & PAGE_MASK);
	run = MIN(limit, page_left);
	while (run < limit) {
		error = nvgpu_bo_get_paddr_at(bo, offset + run, &next);
		if (error != 0)
			return (error);
		if (next != first + run)
			break;
		chunk = MIN(limit - run, (uint64_t)PAGE_SIZE);
		run += chunk;
	}
	*paddr = first;
	*run_size = run;
	return (0);
}

int
nvgpu_bo_get_paddr_run_at_mem(const struct nvgpu_bo *bo,
    const struct ttm_mem_reg *mem, uint64_t offset, uint64_t max_size,
    vm_paddr_t *paddr, uint64_t *run_size)
{
	vm_paddr_t first, next;
	uint64_t limit, run, page_left, chunk;
	int error;

	if (bo == NULL || mem == NULL || paddr == NULL || run_size == NULL ||
	    max_size == 0 || offset >= bo->base.size)
		return (EINVAL);
	limit = MIN(max_size, bo->base.size - offset);
	if (bo->ttm_backed && mem->mem_type == TTM_PL_VRAM) {
		error = nvgpu_bo_get_paddr_at_mem(bo, mem, offset, paddr);
		if (error != 0)
			return (error);
		*run_size = limit;
		return (0);
	}
	error = nvgpu_bo_get_paddr_at_mem(bo, mem, offset, &first);
	if (error != 0)
		return (error);
	page_left = PAGE_SIZE - (offset & PAGE_MASK);
	run = MIN(limit, page_left);
	while (run < limit) {
		error = nvgpu_bo_get_paddr_at_mem(bo, mem, offset + run, &next);
		if (error != 0)
			return (error);
		if (next != first + run)
			break;
		chunk = MIN(limit - run, (uint64_t)PAGE_SIZE);
		run += chunk;
	}
	*paddr = first;
	*run_size = run;
	return (0);
}

int
nvgpu_bo_read32(struct nvgpu_bo *bo, uint64_t offset, uint32_t *value)
{
	uint32_t result = 0;
	int error;

	if (bo == NULL || value == NULL || offset > bo->base.size ||
	    sizeof(*value) > bo->base.size - offset)
		return (EINVAL);
	error = nvgpu_bo_ensure_ttm_populated(bo);
	if (error != 0)
		return (error);
	if (!nvgpu_bo_has_sysmem(bo))
		return (ENXIO);
	for (uint32_t i = 0; i < sizeof(*value); i++) {
		vm_paddr_t pa;

		error = nvgpu_bo_get_paddr_at(bo, offset + i, &pa);
		if (error != 0)
			return (error);
		result |= (uint32_t)(*(volatile uint8_t *)PHYS_TO_DMAP(pa)) << (i * 8);
	}
	*value = result;
	return (0);
}

struct reservation_object *
nvgpu_bo_get_resv(struct nvgpu_bo *bo)
{
	if (bo->no_share && bo->vm_resv != NULL)
		return (bo->vm_resv);
	if (bo->ttm_backed)
		return (bo->tbo.resv);
	return (&bo->resv);
}

static int
nvgpu_bo_ttm_pin_record(struct nvgpu_bo *bo, uint32_t *record_count,
    uint32_t *no_evict_record_count, bool set_no_evict, bool *no_evict_pinned)
{
	struct ttm_buffer_object *tbo;
	int error;

	if (no_evict_pinned == NULL)
		return (EINVAL);
	*no_evict_pinned = false;
	if (bo == NULL || !bo->ttm_backed)
		return (0);

	tbo = &bo->tbo;
	error = ttm_bo_reserve(tbo, false, false, NULL);
	if (error != 0)
		return (nvgpu_errno(error));
	if (*record_count == UINT32_MAX ||
	    (set_no_evict && (bo->ttm_pin_count == UINT32_MAX ||
	    *no_evict_record_count == UINT32_MAX))) {
		error = EOVERFLOW;
		goto out_unreserve;
	}

	(*record_count)++;
	if (set_no_evict) {
		if (bo->ttm_pin_count == 0)
			tbo->mem.placement |= TTM_PL_FLAG_NO_EVICT;
		bo->ttm_pin_count++;
		(*no_evict_record_count)++;
		*no_evict_pinned = true;
	}
	error = 0;

out_unreserve:
	ttm_bo_unreserve(tbo);
	return (error);
}

static int
nvgpu_bo_ttm_unpin_record(struct nvgpu_bo *bo, uint32_t *record_count,
    uint32_t *no_evict_record_count, bool no_evict_pinned)
{
	struct ttm_buffer_object *tbo;
	int error;

	if (bo == NULL || !bo->ttm_backed)
		return (0);
	tbo = &bo->tbo;
	error = ttm_bo_reserve(tbo, false, false, NULL);
	if (error != 0)
		return (nvgpu_errno(error));
	if (*record_count == 0) {
		error = EINVAL;
		goto out_unreserve;
	}
	if (no_evict_pinned) {
		if (*no_evict_record_count == 0 || bo->ttm_pin_count == 0) {
			error = EINVAL;
			goto out_unreserve;
		}
		(*no_evict_record_count)--;
		bo->ttm_pin_count--;
		if (bo->ttm_pin_count == 0 && !bo->ttm_permanent_no_evict)
			tbo->mem.placement &= ~TTM_PL_FLAG_NO_EVICT;
	}
	(*record_count)--;
	error = 0;

out_unreserve:
	ttm_bo_unreserve(tbo);
	return (error);
}

int
nvgpu_bo_vm_bind_pin(struct nvgpu_bo *bo, bool *no_evict_pinned)
{
	return (nvgpu_bo_ttm_pin_record(bo, &bo->vm_bind_pin_count,
	    &bo->vm_bind_no_evict_pin_count, true, no_evict_pinned));
}

int
nvgpu_bo_vm_bind_unpin(struct nvgpu_bo *bo, bool no_evict_pinned)
{
	return (nvgpu_bo_ttm_unpin_record(bo, &bo->vm_bind_pin_count,
	    &bo->vm_bind_no_evict_pin_count, no_evict_pinned));
}

int
nvgpu_bo_scanout_pin(struct nvgpu_bo *bo)
{
	bool no_evict_pinned;

	return (nvgpu_bo_ttm_pin_record(bo, &bo->scanout_pin_count,
	    &bo->scanout_no_evict_pin_count, true, &no_evict_pinned));
}

int
nvgpu_bo_scanout_unpin(struct nvgpu_bo *bo)
{
	return (nvgpu_bo_ttm_unpin_record(bo, &bo->scanout_pin_count,
	    &bo->scanout_no_evict_pin_count, true));
}

int
nvgpu_bo_add_bookkeeping_fence(struct nvgpu_bo *bo,
    struct nvgpu_fence *fence)
{
	struct reservation_object *resv;
	int error;

	if (bo == NULL || fence == NULL)
		return (EINVAL);
	resv = nvgpu_bo_get_resv(bo);
	reservation_object_lock(resv, NULL);
	error = reservation_object_reserve_shared(resv);
	if (error == 0)
		reservation_object_add_shared_fence(resv, &fence->dma);
	reservation_object_unlock(resv);
	return (nvgpu_errno(error));
}

int
nvgpu_bo_resv_wait(struct nvgpu_bo *bo, bool intr, bool write, bool nowait)
{
	struct reservation_object *resv;
	long ret;

	if (bo == NULL)
		return (EINVAL);
	resv = nvgpu_bo_get_resv(bo);
	if (nowait) {
		if (reservation_object_test_signaled_rcu(resv, write))
			return (0);
		return (EBUSY);
	}
	ret = reservation_object_wait_timeout_rcu(resv, write, intr,
	    MAX_SCHEDULE_TIMEOUT);
	if (ret < 0)
		return ((int)-ret);
	if (ret == 0)
		return (ETIME);
	return (0);
}

static void
nvgpu_bo_finalize_from_ttm(struct ttm_buffer_object *tbo)
{
	struct nvgpu_bo *bo = nvgpu_bo_from_ttm(tbo);

	KASSERT(bo->refs == 0, ("finalizing referenced BO"));
	KASSERT(LIST_EMPTY(&bo->vm_mappings),
	    ("destroying BO with live GPUVA mappings"));
	lwkt_token_uninit(&bo->vm_mapping_token);
	reservation_object_fini(&bo->resv);
	drm_gem_object_release(&bo->base);
	kfree(bo);
}

static int
nvgpu_bo_init_ttm(struct nvgpu_device *gpu, struct nvgpu_bo *bo,
    uint64_t size, uint32_t domain, uint32_t align)
{
	struct ttm_place places[3];
	struct ttm_placement placement = {
		.placement = places,
		.busy_placement = places,
	};
	struct ttm_operation_ctx ctx = {
		.interruptible = false,
		.no_wait_gpu = false,
	};
	struct ttm_bo_device *bdev;
	uint32_t placement_domain;
	uint32_t page_alignment;
	unsigned int n = 0;
	size_t acc_size;
	int error;

	bdev = nvgpu_ttm_get_bo_device(gpu);
	if (bdev == NULL)
		return (ENODEV);

	placement_domain = domain & (NOUVEAU_GEM_DOMAIN_CPU |
	    NOUVEAU_GEM_DOMAIN_VRAM | NOUVEAU_GEM_DOMAIN_GART);
	if (placement_domain == 0)
		placement_domain = NOUVEAU_GEM_DOMAIN_GART;
	if ((placement_domain & NOUVEAU_GEM_DOMAIN_VRAM) != 0) {
		places[n].fpfn = 0;
		places[n].lpfn = 0;
		places[n].flags = TTM_PL_FLAG_VRAM | TTM_PL_FLAG_WC |
		    TTM_PL_FLAG_NO_EVICT;
		bo->ttm_permanent_no_evict = true;
		n++;
	}
	if ((placement_domain & NOUVEAU_GEM_DOMAIN_GART) != 0) {
		places[n].fpfn = 0;
		places[n].lpfn = 0;
		places[n].flags = TTM_PL_FLAG_TT | TTM_PL_FLAG_CACHED |
		    TTM_PL_FLAG_NO_EVICT;
		n++;
	}
	if ((placement_domain & NOUVEAU_GEM_DOMAIN_CPU) != 0) {
		places[n].fpfn = 0;
		places[n].lpfn = 0;
		places[n].flags = TTM_PL_FLAG_SYSTEM | TTM_PL_FLAG_CACHED |
		    TTM_PL_FLAG_NO_EVICT;
		n++;
	}
	placement.num_placement = n;
	placement.num_busy_placement = n;

	bo->ttm_backed = true;
	bo->ttm_permanent_no_evict = true;
	bo->preferred_domain = placement_domain;
	bo->domain = domain;
	page_alignment = 1;
	if (align > PAGE_SIZE)
		page_alignment = howmany(align, PAGE_SIZE);
	if ((placement_domain & NOUVEAU_GEM_DOMAIN_VRAM) != 0) {
		if (size >= NVGPU_GMMU_PD0_SIZE)
			page_alignment = MAX(page_alignment,
			    NVGPU_GMMU_PD0_SIZE >> PAGE_SHIFT);
		else if (size >= NVGPU_GMMU_LPT_SIZE)
			page_alignment = MAX(page_alignment,
			    NVGPU_GMMU_LPT_SIZE >> PAGE_SHIFT);
	}
	acc_size = ttm_bo_dma_acc_size(bdev, size, sizeof(*bo));
	error = ttm_bo_init_reserved(bdev, &bo->tbo, size, ttm_bo_type_device,
	    &placement, page_alignment, &ctx, acc_size, NULL, NULL,
	    nvgpu_bo_finalize_from_ttm);
	if (error == 0) {
		bo->refs = 1;
		nvgpu_bo_refresh_ttm_domain(bo, domain);
		ttm_bo_unreserve(&bo->tbo);
	}
	return (nvgpu_errno(error));
}

static struct nvgpu_bo *
nvgpu_bo_create(struct drm_device *ddev, uint64_t size, uint32_t domain,
    uint32_t align, uint32_t tile_mode, uint32_t tile_flags)
{
	struct nvgpu_device *gpu = ddev->dev_private;
	struct nvgpu_bo *bo;
	int error;

	size = roundup(size, PAGE_SIZE);
	if (size == 0)
		return (NULL);
	bo = kzalloc(sizeof(*bo), GFP_KERNEL);
	if (bo == NULL)
		return (NULL);
	reservation_object_init(&bo->resv);
	lwkt_token_init(&bo->vm_mapping_token, "nvgbo");
	LIST_INIT(&bo->vm_mappings);
	drm_gem_private_object_init(ddev, &bo->base, size);
	bo->tile_mode = tile_mode;
	bo->tile_flags = tile_flags;
	bo->no_share = (domain & NOUVEAU_GEM_DOMAIN_NO_SHARE) != 0;

	error = nvgpu_bo_init_ttm(gpu, bo, size, domain, align);
	if (error != 0) {
		nvgpu_log(NVGPU_LOG_INFO,
		    "BO TTM create failed error=%d domain=0x%x size=0x%llx\n",
		    error, domain, (unsigned long long)size);
		if (!bo->ttm_backed) {
			lwkt_token_uninit(&bo->vm_mapping_token);
			reservation_object_fini(&bo->resv);
			drm_gem_object_release(&bo->base);
			kfree(bo);
		}
		return (NULL);
	}
	return (bo);
}

void
nvgpu_bo_release_by_gem(struct drm_gem_object *obj)
{
	if (obj == NULL)
		return;
	nvgpu_bo_release(nvgpu_bo_from_gem(obj));
}

int
nvgpu_bo_create_handle(struct nvgpu_device *device,
    struct reservation_object *vm_resv, struct drm_file *file,
    const struct nvgpu_bo_create_args *args, struct nvgpu_bo_info *info)
{
	struct drm_device *ddev;
	struct nvgpu_bo *bo;
	uint32_t req_domain;
	uint32_t create_domain;
	uint32_t handle = 0;
	bool mappable_req;
	int error;

	if (device == NULL || vm_resv == NULL || file == NULL || args == NULL ||
	    info == NULL)
		return (EINVAL);
	ddev = nvgpu_device_get_drm_dev(device);
	if (ddev == NULL)
		return (ENXIO);

	req_domain = args->domain;
	mappable_req = (req_domain & NOUVEAU_GEM_DOMAIN_MAPPABLE) != 0;
	create_domain = req_domain;
	if (mappable_req && (req_domain & NOUVEAU_GEM_DOMAIN_GART) != 0)
		create_domain = (req_domain & ~NOUVEAU_GEM_DOMAIN_VRAM) |
		    NOUVEAU_GEM_DOMAIN_GART;

	bo = nvgpu_bo_create(ddev, args->size, create_domain, args->align,
	    args->tile_mode, args->tile_flags);
	if (bo == NULL)
		return (ENOMEM);
	if (mappable_req && (bo->domain & NOUVEAU_GEM_DOMAIN_VRAM) != 0 &&
	    !nvgpu_bo_cpu_mappable(bo)) {
		if ((req_domain & NOUVEAU_GEM_DOMAIN_GART) != 0) {
			drm_gem_object_put_unlocked(&bo->base);
			bo = nvgpu_bo_create(ddev, args->size,
			    (req_domain & ~NOUVEAU_GEM_DOMAIN_VRAM) |
			    NOUVEAU_GEM_DOMAIN_GART, args->align, args->tile_mode,
			    args->tile_flags);
			if (bo == NULL)
				return (ENOMEM);
		} else {
			drm_gem_object_put_unlocked(&bo->base);
			return (ENXIO);
		}
	}
	if (bo->no_share)
		bo->vm_resv = vm_resv;

	error = drm_gem_handle_create(file, &bo->base, &handle);
	drm_gem_object_put_unlocked(&bo->base);
	if (error != 0)
		return (error);

	info->handle = handle;
	info->domain = bo->domain;
	info->size = bo->base.size;
	info->offset = 0;
	info->map_handle = nvgpu_bo_get_mmap_handle(bo);
	info->tile_mode = bo->tile_mode;
	info->tile_flags = bo->tile_flags;
	return (0);
}

int
nvgpu_bo_get_info(struct drm_file *file, uint32_t handle,
    struct nvgpu_bo_info *info)
{
	struct drm_gem_object *obj;
	struct nvgpu_bo *bo;

	if (file == NULL || info == NULL)
		return (EINVAL);
	obj = drm_gem_object_lookup(file, handle);
	if (obj == NULL)
		return (ENOENT);
	bo = nvgpu_bo_from_gem(obj);
	info->handle = handle;
	info->domain = bo->domain;
	info->size = obj->size;
	info->offset = 0;
	info->map_handle = nvgpu_bo_get_mmap_handle(bo);
	info->tile_mode = bo->tile_mode;
	info->tile_flags = bo->tile_flags;
	drm_gem_object_put_unlocked(obj);
	return (0);
}

int
nvgpu_bo_lookup(struct drm_file *file, uint32_t handle, struct nvgpu_bo **out)
{
	struct drm_gem_object *obj;
	struct nvgpu_bo *bo;

	if (file == NULL || out == NULL)
		return (EINVAL);
	obj = drm_gem_object_lookup(file, handle);
	if (obj == NULL)
		return (ENOENT);
	bo = nvgpu_bo_from_gem(obj);
	nvgpu_bo_addref(bo);
	drm_gem_object_put_unlocked(obj);
	*out = bo;
	return (0);
}

void
nvgpu_bo_addref(struct nvgpu_bo *bo)
{
	u_int refs;

	if (bo == NULL)
		return;
	refs = atomic_fetchadd_int(&bo->refs, 1);
	KASSERT(refs != 0, ("adding reference to released BO"));
}

void
nvgpu_bo_release(struct nvgpu_bo *bo)
{
	u_int refs;

	if (bo == NULL)
		return;
	refs = atomic_fetchadd_int(&bo->refs, -1);
	KASSERT(refs != 0, ("nvgpu BO refs underflow"));
	if (refs != 1)
		return;
	if (bo->ttm_backed) {
		ttm_bo_put(&bo->tbo);
		return;
	}
	(void)nvgpu_bo_resv_wait(bo, false, true, false);
	KASSERT(LIST_EMPTY(&bo->vm_mappings),
	    ("releasing BO with live GPUVA mappings"));
	lwkt_token_uninit(&bo->vm_mapping_token);
	reservation_object_fini(&bo->resv);
	drm_gem_object_release(&bo->base);
	kfree(bo);
}

uint64_t
nvgpu_bo_get_size(const struct nvgpu_bo *bo)
{
	return (bo != NULL ? bo->base.size : 0);
}
