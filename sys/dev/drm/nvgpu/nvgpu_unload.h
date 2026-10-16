/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Unload admission gate for the native NVIDIA GPU driver.
 */

#ifndef _NVGPU_UNLOAD_H_
#define _NVGPU_UNLOAD_H_

struct nvgpu_device;

/* Initialize unload admission state before DRM users can open the device. */
int nvgpu_unload_init(struct nvgpu_device *gpu);
/* Release unload admission state after DRM is unpublished and all users are gone. */
void nvgpu_unload_fini(struct nvgpu_device *gpu);
/* Hold unload against one DRM open lifetime unless unload has started. */
int nvgpu_unload_hold_by_drm(struct nvgpu_device *gpu);
/* Release one DRM unload hold previously acquired by nvgpu_unload_hold_by_drm(). */
void nvgpu_unload_release_by_drm(struct nvgpu_device *gpu);
/* Hold unload while one deduplicated mmap pager object owns a BO. */
int nvgpu_unload_hold_by_mmap(struct nvgpu_device *gpu);
/* Release one mmap pager hold acquired by nvgpu_unload_hold_by_mmap(). */
void nvgpu_unload_release_by_mmap(struct nvgpu_device *gpu);
/* Try to start unload after proving the DRM core has no live users. */
int nvgpu_unload_try_begin(struct nvgpu_device *gpu);
/* Cancel a previously admitted unload before teardown has started. */
void nvgpu_unload_abort(struct nvgpu_device *gpu);

#endif /* _NVGPU_UNLOAD_H_ */
