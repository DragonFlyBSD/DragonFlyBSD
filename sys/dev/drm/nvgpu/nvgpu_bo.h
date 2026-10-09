/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * GEM/TTM buffer objects for the native NVIDIA driver.
 */

#ifndef _NVGPU_BO_H_
#define _NVGPU_BO_H_

#include <sys/queue.h>
#include <sys/stdint.h>
#include <sys/thread.h>

#include <drm/drm_gem.h>
#include <drm/ttm/ttm_bo_api.h>
#include <linux/reservation.h>
#include <vm/vm_param.h>

struct drm_file;
struct nvgpu_fence;
struct nvgpu_vm_binding;
struct nvgpu_device;
struct nvgsp_vram_alloc;
struct reservation_object;
struct ttm_mem_reg;

struct nvgpu_bo_create_args {
	uint64_t size;
	uint32_t domain;
	uint32_t align;
	uint32_t tile_mode;
	uint32_t tile_flags;
};

struct nvgpu_bo_info {
	uint32_t handle;
	uint32_t domain;
	uint64_t size;
	uint64_t offset;
	uint64_t map_handle;
	uint32_t tile_mode;
	uint32_t tile_flags;
};

LIST_HEAD(nvgpu_bo_vm_mapping_list, nvgpu_vm_binding);

/* Internal BO representation shared by the BO and TTM modules. */
struct nvgpu_bo {
	struct drm_gem_object base;
	struct ttm_buffer_object tbo;
	/* Logical owners are GEM, one active mmap pager, and each VM mapping. */
	volatile u_int refs;
	struct reservation_object resv;
	struct reservation_object *vm_resv;
	struct nvgsp_vram_alloc *vram_alloc;
	uint64_t paddr;
	uint32_t domain;
	uint32_t preferred_domain;
	uint32_t tile_mode;
	uint32_t tile_flags;
	bool no_share;
	bool ttm_backed;
	bool ttm_permanent_no_evict;
	bool vm_bound_tiled;
	bool vm_bound_mixed_kind;
	uint8_t vm_bound_kind;
	uint32_t ttm_pin_count;
	uint32_t vm_bind_pin_count;
	uint32_t vm_bind_no_evict_pin_count;
	uint32_t scanout_pin_count;
	uint32_t scanout_no_evict_pin_count;
	u_int mmap_pager_active;
	struct lwkt_token vm_mapping_token;
	struct nvgpu_bo_vm_mapping_list vm_mappings;
	uint32_t vm_mapping_count;
};

static __inline struct nvgpu_bo *
nvgpu_bo_from_gem(struct drm_gem_object *obj)
{
	return ((struct nvgpu_bo *)obj);
}

static __inline struct nvgpu_bo *
nvgpu_bo_from_ttm(struct ttm_buffer_object *tbo)
{
	return (container_of(tbo, struct nvgpu_bo, tbo));
}

/* Create, describe, or look up one GEM-backed BO for a borrowed DRM file. */
int nvgpu_bo_create_handle(struct nvgpu_device *device,
	struct reservation_object *vm_resv, struct drm_file *file,
    const struct nvgpu_bo_create_args *args, struct nvgpu_bo_info *info);
int nvgpu_bo_get_info(struct drm_file *file, uint32_t handle,
    struct nvgpu_bo_info *info);
int nvgpu_bo_lookup(struct drm_file *file, uint32_t handle,
    struct nvgpu_bo **out);
/* Add or release one logical BO owner; the last release drops TTM ownership. */
void nvgpu_bo_addref(struct nvgpu_bo *bo);
void nvgpu_bo_release(struct nvgpu_bo *bo);

/* Query immutable placement, sharing, mapping, and GPU-page properties. */
uint64_t nvgpu_bo_get_size(const struct nvgpu_bo *bo);
bool nvgpu_bo_is_vram(const struct nvgpu_bo *bo);
bool nvgpu_bo_can_share(const struct nvgpu_bo *bo);
bool nvgpu_bo_has_sysmem(const struct nvgpu_bo *bo);
bool nvgpu_bo_cpu_mappable(const struct nvgpu_bo *bo);
uint64_t nvgpu_bo_get_mmap_handle(struct nvgpu_bo *bo);
uint8_t nvgpu_bo_get_gpu_page_shift(const struct nvgpu_bo *bo);

/* Populate TTM storage before physical-address or VM mapping operations. */
int nvgpu_bo_ensure_ttm_populated(struct nvgpu_bo *bo);

/* Return one physical address or the longest contiguous run at an offset. */
int nvgpu_bo_get_paddr_at(const struct nvgpu_bo *bo, uint64_t offset,
    vm_paddr_t *paddr);
int nvgpu_bo_get_paddr_run_at(const struct nvgpu_bo *bo, uint64_t offset,
    uint64_t max_size, vm_paddr_t *paddr, uint64_t *run_size);
int nvgpu_bo_get_paddr_at_mem(const struct nvgpu_bo *bo,
    const struct ttm_mem_reg *mem, uint64_t offset, vm_paddr_t *paddr);
int nvgpu_bo_get_paddr_run_at_mem(const struct nvgpu_bo *bo,
    const struct ttm_mem_reg *mem, uint64_t offset, uint64_t max_size,
    vm_paddr_t *paddr, uint64_t *run_size);

/* Read one BO dword through the active CPU/BAR mapping. */
int nvgpu_bo_read32(struct nvgpu_bo *bo, uint64_t offset, uint32_t *value);

/* Pin or unpin VM_BIND and scanout ownership without enabling eviction. */
int nvgpu_bo_vm_bind_pin(struct nvgpu_bo *bo, bool *no_evict_pinned);
int nvgpu_bo_vm_bind_unpin(struct nvgpu_bo *bo, bool no_evict_pinned);
int nvgpu_bo_scanout_pin(struct nvgpu_bo *bo);
int nvgpu_bo_scanout_unpin(struct nvgpu_bo *bo);

/* Borrow the reservation object or add/wait for native bookkeeping fences. */
struct reservation_object *nvgpu_bo_get_resv(struct nvgpu_bo *bo);
int nvgpu_bo_add_bookkeeping_fence(struct nvgpu_bo *bo,
    struct nvgpu_fence *fence);
int nvgpu_bo_resv_wait(struct nvgpu_bo *bo, bool intr, bool write,
    bool nowait);

/* Refresh the reported GEM domain after a TTM placement transition. */
void nvgpu_bo_refresh_ttm_domain(struct nvgpu_bo *bo, uint32_t req_domain);
/* Release GEM's aggregate ownership after its own refcount reaches zero. */
void nvgpu_bo_release_by_gem(struct drm_gem_object *obj);

#endif /* _NVGPU_BO_H_ */
