/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * DragonFly DRM/KMS shim for the native NVIDIA GPU driver.
 */

#ifndef _NVDRM_KMS_H_
#define _NVDRM_KMS_H_

#include <sys/stdint.h>

struct drm_device;
struct drm_file;
struct drm_mode_create_dumb;
struct nvgpu_device;

/*
 * DRM dumb-buffer callbacks backed by CPU-mappable VRAM BOs.
 *
 * DRM owns file, ddev, and args for each callback.  The callbacks may sleep
 * while allocating, looking up, or releasing GEM/TTM state.
 */
int nvdrm_kms_create_dumb(struct drm_file *file, struct drm_device *ddev,
    struct drm_mode_create_dumb *args);
int nvdrm_kms_get_dumb_map_offset(struct drm_file *file,
    struct drm_device *ddev, uint32_t handle, uint64_t *offset);
int nvdrm_kms_destroy_dumb(struct drm_file *file, struct drm_device *ddev,
    uint32_t handle);

/* Initialize and publish all KMS objects after the display backend is ready. */
int nvdrm_kms_init(struct nvgpu_device *gpu);

/* Unpublish KMS and release its objects after userspace files have drained. */
void nvdrm_kms_fini(struct nvgpu_device *gpu);

/* Rebind the persistent console scanout after the last DRM user closes. */
int nvdrm_kms_restore_console(struct nvgpu_device *gpu);

#endif /* _NVDRM_KMS_H_ */
