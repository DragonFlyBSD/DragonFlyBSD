/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * DRM PRIME self-sharing boundary for the native NVIDIA driver.
 */

#ifndef _NVDRM_PRIME_H_
#define _NVDRM_PRIME_H_

#include <sys/stdint.h>

struct dma_buf;
struct drm_device;
struct drm_file;
struct drm_gem_object;
struct reservation_object;

/* Export one shareable nvgpu GEM handle as a dma-buf fd. */
int nvdrm_prime_handle_to_fd(struct drm_device *dev, struct drm_file *file,
    uint32_t handle, uint32_t flags, int *prime_fd);

/* Import only dma-bufs exported by this same DRM device. */
int nvdrm_prime_fd_to_handle(struct drm_device *dev, struct drm_file *file,
    int prime_fd, uint32_t *handle);

/* Export obj unless its no-share creation flag forbids dma-buf publication. */
struct dma_buf *nvdrm_prime_export(struct drm_device *dev,
    struct drm_gem_object *obj, int flags);

/* Return obj's borrowed reservation object for the dma-buf core. */
struct reservation_object *nvdrm_prime_get_resv(struct drm_gem_object *obj);

#endif /* _NVDRM_PRIME_H_ */
