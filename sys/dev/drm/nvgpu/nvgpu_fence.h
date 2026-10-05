/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Native fence boundary for the NVIDIA GPU driver.
 */

#ifndef _NVGPU_FENCE_H_
#define _NVGPU_FENCE_H_

#include <linux/dma-fence.h>
#include <stdbool.h>

/*
 * Native NVGPU fence.
 *
 * The embedded dma fence is the complete public representation.  NVGPU code
 * uses this type and the functions below; explicit casts to struct dma_fence
 * are restricted to the DRM/LinuxKPI boundary.
 */
struct nvgpu_fence {
	struct dma_fence dma;
};

/* Create an unsignaled fence and return its initial owned reference. */
struct nvgpu_fence *nvgpu_fence_create(void);

/* Add one owned reference.  The fence may be NULL. */
void nvgpu_fence_addref(struct nvgpu_fence *fence);

/* Consume one owned reference.  The fence may be NULL. */
void nvgpu_fence_release(struct nvgpu_fence *fence);

/*
 * Complete a fence with success or a positive errno.
 *
 * Returns zero for the first signal and EALREADY if the fence was already
 * complete.  Registered callbacks run synchronously in fence callback context.
 */
int nvgpu_fence_signal(struct nvgpu_fence *fence, int error);

/* Return true after the fence has completed.  A NULL fence is complete. */
bool nvgpu_fence_is_signaled(struct nvgpu_fence *fence);

/*
 * Return EINPROGRESS before completion, zero after success, or the producer's
 * positive errno after failed completion.
 */
int nvgpu_fence_get_error(struct nvgpu_fence *fence);

/*
 * Register one callback atomically unless the fence is already complete.
 *
 * Returns true when callback(arg) was registered and will run exactly once.
 * Returns false when the fence was already complete; the callback will not
 * run.  This function may sleep while allocating its private callback adapter.
 * The caller owns callback and arg and must keep arg valid until callback runs.
 */
bool nvgpu_fence_add_callback_unless_signaled(struct nvgpu_fence *fence,
	void (*callback)(void *), void *callback_arg);

#endif /* _NVGPU_FENCE_H_ */
