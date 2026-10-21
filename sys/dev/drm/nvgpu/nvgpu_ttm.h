/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * TTM device glue for the native NVIDIA GPU driver.
 */

#ifndef _NVGPU_TTM_H_
#define _NVGPU_TTM_H_

#include <sys/types.h>
#include <vm/vm.h>

struct drm_device;
struct file;
struct nvgpu_bo;
struct nvgpu_device;
struct ttm_bo_device;
struct vm_object;

/* Initialize the TTM device and publish it through drm_device::drm_ttm_bdev. */
int nvgpu_ttm_init(struct nvgpu_device *gpu, struct drm_device *ddev);
/* Stop TTM managers after DRM has been unpublished and users are gone. */
void nvgpu_ttm_fini(struct nvgpu_device *gpu);
/* Return the borrowed TTM BO device for BO creation. */
struct ttm_bo_device *nvgpu_ttm_get_bo_device(struct nvgpu_device *gpu);
/* DRM mmap_single hook for TTM-backed GEM objects. */
int nvgpu_ttm_mmap_single(struct file *fp, struct drm_device *dev,
    vm_ooffset_t *offset, vm_size_t size, struct vm_object **obj_res,
    int nprot);

#endif /* _NVGPU_TTM_H_ */
