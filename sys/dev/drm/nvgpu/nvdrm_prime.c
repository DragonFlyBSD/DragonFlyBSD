/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * DRM PRIME self-sharing boundary for the native NVIDIA driver.
 */

#include "nvdrm_prime.h"
#include "nvgpu_bo.h"

#include <drm/drmP.h>
#include <drm/drm_prime.h>
#include <linux/err.h>

int
nvdrm_prime_handle_to_fd(struct drm_device *dev, struct drm_file *file,
    uint32_t handle, uint32_t flags, int *prime_fd)
{
	return (drm_gem_prime_handle_to_fd(dev, file, handle, flags, prime_fd));
}

int
nvdrm_prime_fd_to_handle(struct drm_device *dev, struct drm_file *file,
    int prime_fd, uint32_t *handle)
{
	return (drm_gem_prime_fd_to_handle(dev, file, prime_fd, handle));
}

struct dma_buf *
nvdrm_prime_export(struct drm_device *dev, struct drm_gem_object *obj,
    int flags)
{
	if (!nvgpu_bo_can_share(nvgpu_bo_from_gem(obj)))
		return (ERR_PTR(-EPERM));
	return (drm_gem_prime_export(dev, obj, flags));
}

struct reservation_object *
nvdrm_prime_get_resv(struct drm_gem_object *obj)
{
	return (nvgpu_bo_get_resv(nvgpu_bo_from_gem(obj)));
}
