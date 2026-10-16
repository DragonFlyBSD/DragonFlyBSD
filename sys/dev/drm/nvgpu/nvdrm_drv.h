/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * DRM registration boundary for the native NVIDIA driver.
 */

#ifndef _NVDRM_DRV_H_
#define _NVDRM_DRV_H_

struct drm_device;
struct nvgpu_device;

/* Register DRM after GPU boot.  gpu is borrowed; may sleep and must not hold GSP/VM tokens. */
int nvdrm_register(struct nvgpu_device *gpu);

/* Unregister DRM before backend teardown.  gpu is borrowed; callers must have rejected new users. */
void nvdrm_unregister(struct nvgpu_device *gpu);

/* Return the borrowed drm_device for gpu.  Caller must already pin the GPU/DRM lifetime. */
struct drm_device *nvdrm_get_device(struct nvgpu_device *gpu);

#endif /* _NVDRM_DRV_H_ */
