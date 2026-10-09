/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * GSP VRAM allocation boundary for the native NVIDIA GPU driver.
 */

#ifndef _NVGSP_VRAM_H_
#define _NVGSP_VRAM_H_

#include <sys/stdint.h>

struct drm_mm_node;
struct nvgpu_device;
struct nvgsp_vram_alloc;

/* Initialize backend VRAM allocation state during boot. */
int nvgsp_vram_init(struct nvgpu_device *gpu);
/* Release backend VRAM allocation state after users stop. */
void nvgsp_vram_fini(struct nvgpu_device *gpu);
/* MPSAFE allocation and release serialize the device-global VRAM allocator. */
struct nvgsp_vram_alloc *nvgsp_vram_alloc_gem(struct nvgpu_device *gpu,
    uint64_t size, uint64_t align, void *owner);
/* Release a GEM VRAM allocation returned by nvgsp_vram_alloc_gem(). */
void nvgsp_vram_free_gem(struct nvgpu_device *gpu,
    struct nvgsp_vram_alloc *alloc, void *owner);

/* Allocate display-owned VRAM; the returned allocation is consumed by free. */
struct nvgsp_vram_alloc *nvgsp_vram_alloc_display(struct nvgpu_device *gpu,
    uint64_t size, uint64_t align, void *owner);
void nvgsp_vram_free_display(struct nvgpu_device *gpu,
    struct nvgsp_vram_alloc *alloc);

/* Return borrowed allocation metadata valid until the allocation is freed. */
struct drm_mm_node *nvgsp_vram_alloc_get_node(struct nvgsp_vram_alloc *alloc);
struct nvgsp_vram_alloc *nvgsp_vram_alloc_from_node(struct drm_mm_node *node);
void *nvgsp_vram_alloc_get_owner(struct nvgsp_vram_alloc *alloc);
uint64_t nvgsp_vram_alloc_get_paddr(const struct nvgsp_vram_alloc *alloc);
uint64_t nvgsp_vram_alloc_get_size(const struct nvgsp_vram_alloc *alloc);

/*
 * Create and remove temporary BAR1 mappings for an existing allocation.
 * Mapping functions may sleep; accessors below require the matching mapping
 * to remain live and never change allocation ownership.  Calls using the same
 * allocation require external serialization; different allocations are MPSAFE.
 */
int nvgsp_vram_alloc_map_bar1_scatter(struct nvgpu_device *gpu,
    struct nvgsp_vram_alloc *alloc, uint64_t size, uint32_t pages);
void nvgsp_vram_alloc_unmap_bar1_scatter(struct nvgpu_device *gpu,
    struct nvgsp_vram_alloc *alloc);
int nvgsp_vram_alloc_map_bar1_range(struct nvgpu_device *gpu,
    struct nvgsp_vram_alloc *alloc, uint64_t size);
void nvgsp_vram_alloc_unmap_bar1_range(struct nvgpu_device *gpu,
    struct nvgsp_vram_alloc *alloc);
uint64_t nvgsp_vram_alloc_get_bar1_range(
    const struct nvgsp_vram_alloc *alloc);
uint64_t nvgsp_vram_alloc_bar1_gva(struct nvgsp_vram_alloc *alloc,
    unsigned long page_offset);
uint32_t nvgsp_vram_alloc_read32(struct nvgpu_device *gpu,
    struct nvgsp_vram_alloc *alloc, uint64_t offset);
void nvgsp_vram_alloc_write32(struct nvgpu_device *gpu,
    struct nvgsp_vram_alloc *alloc, uint64_t offset, uint32_t value);
#endif /* _NVGSP_VRAM_H_ */
