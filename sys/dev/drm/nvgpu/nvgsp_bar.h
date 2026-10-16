/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * GSP BAR aperture boundary for the native NVIDIA GPU driver.
 */

#ifndef _NVGSP_BAR_H_
#define _NVGSP_BAR_H_

#include <sys/stdint.h>

struct nvgpu_device;

/* Initialize BAR2 backend state during boot; gpu is borrowed. */
int nvgsp_bar_init_bar2(struct nvgpu_device *gpu);
/* Release BAR2 state after all BAR2 users have stopped. */
void nvgsp_bar_fini_bar2(struct nvgpu_device *gpu);
/* Initialize BAR1 backend state during boot; gpu is borrowed. */
int nvgsp_bar_init_bar1(struct nvgpu_device *gpu);
/* Release BAR1 state after CPU mappings have drained. */
void nvgsp_bar_fini_bar1(struct nvgpu_device *gpu);
/*
 * Map one existing contiguous VRAM allocation through BAR1.
 *
 * On success pgva receives the owned GPU virtual address of the mapping.  The
 * caller keeps the physical storage alive and must unmap the same range before
 * releasing it.  These MPSAFE operations serialize the device BAR1 allocator
 * internally and may sleep while updating page-table state.
 */
int nvgsp_bar_map_vram_range(struct nvgpu_device *gpu,
    uint64_t paddr, uint64_t size, uint64_t *pgva);
void nvgsp_bar_unmap_vram_range(struct nvgpu_device *gpu,
    uint64_t gva, uint64_t size);

#endif /* _NVGSP_BAR_H_ */
