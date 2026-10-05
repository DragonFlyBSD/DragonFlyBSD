/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Native fence boundary for the NVIDIA GPU driver.
 */

#include "nvgpu_fence.h"

#include <sys/errno.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/systm.h>

struct nvgpu_fence_private {
	struct nvgpu_fence fence;
	spinlock_t lock;
};

struct nvgpu_fence_callback {
	struct dma_fence_cb dma;
	void (*callback)(void *);
	void *callback_arg;
};

static MALLOC_DEFINE(M_NVGPU_FENCE, "nvgpu_fence", "nvgpu native fence");

static const char *nvgpu_fence_get_driver_name(struct dma_fence *fence);
static const char *nvgpu_fence_get_timeline_name(struct dma_fence *fence);
static void nvgpu_fence_finalize(struct dma_fence *fence);
static void nvgpu_fence_run_callback(struct dma_fence *fence,
	struct dma_fence_cb *callback);

static const struct dma_fence_ops nvgpu_fence_ops = {
	.get_driver_name = nvgpu_fence_get_driver_name,
	.get_timeline_name = nvgpu_fence_get_timeline_name,
	.release = nvgpu_fence_finalize,
};

static const char *
nvgpu_fence_get_driver_name(struct dma_fence *fence __unused)
{
	return ("nvgpu");
}

static const char *
nvgpu_fence_get_timeline_name(struct dma_fence *fence __unused)
{
	return ("nvgpu");
}

static void
nvgpu_fence_finalize(struct dma_fence *dma)
{
	struct nvgpu_fence_private *private;
	struct nvgpu_fence *fence;

	fence = container_of(dma, struct nvgpu_fence, dma);
	private = container_of(fence, struct nvgpu_fence_private, fence);
	lockuninit(&private->lock);
	_kfree(private, M_NVGPU_FENCE);
}

struct nvgpu_fence *
nvgpu_fence_create(void)
{
	struct nvgpu_fence_private *private;
	u64 context;

	private = kmalloc(sizeof(*private), M_NVGPU_FENCE, M_WAITOK | M_ZERO);
	lockinit(&private->lock, "nvgpuf", 0, 0);
	context = dma_fence_context_alloc(1);
	dma_fence_init(&private->fence.dma, &nvgpu_fence_ops, &private->lock,
	    context, 1);
	return (&private->fence);
}

void
nvgpu_fence_addref(struct nvgpu_fence *fence)
{
	if (fence != NULL)
		dma_fence_get(&fence->dma);
}

void
nvgpu_fence_release(struct nvgpu_fence *fence)
{
	if (fence != NULL)
		dma_fence_put(&fence->dma);
}

int
nvgpu_fence_signal(struct nvgpu_fence *fence, int error)
{
	int result;

	if (fence == NULL || error < 0)
		return (EINVAL);
	crit_enter();
	lockmgr(fence->dma.lock, LK_EXCLUSIVE);
	if (dma_fence_is_signaled_locked(&fence->dma)) {
		result = EALREADY;
	} else {
		fence->dma.error = error == 0 ? 0 : -error;
		result = dma_fence_signal_locked(&fence->dma);
		if (result < 0)
			result = -result;
	}
	lockmgr(fence->dma.lock, LK_RELEASE);
	crit_exit();
	return (result);
}

bool
nvgpu_fence_is_signaled(struct nvgpu_fence *fence)
{
	if (fence == NULL)
		return (true);
	return (dma_fence_is_signaled(&fence->dma));
}

int
nvgpu_fence_get_error(struct nvgpu_fence *fence)
{
	int error;

	if (fence == NULL)
		return (0);
	if (!nvgpu_fence_is_signaled(fence))
		return (EINPROGRESS);
	error = fence->dma.error;
	return (error < 0 ? -error : error);
}

bool
nvgpu_fence_add_callback_unless_signaled(struct nvgpu_fence *fence,
	void (*callback)(void *), void *callback_arg)
{
	struct nvgpu_fence_callback *adapter;
	int error;

	KASSERT(fence != NULL, ("registering callback on NULL nvgpu fence"));
	KASSERT(callback != NULL, ("registering NULL nvgpu fence callback"));
	adapter = kmalloc(sizeof(*adapter), M_NVGPU_FENCE,
	    M_WAITOK | M_ZERO);
	adapter->callback = callback;
	adapter->callback_arg = callback_arg;
	error = dma_fence_add_callback(&fence->dma, &adapter->dma,
	    nvgpu_fence_run_callback);
	if (error == 0)
		return (true);
	KASSERT(error == -ENOENT || error == ENOENT,
	    ("unexpected dma fence callback error %d", error));
	_kfree(adapter, M_NVGPU_FENCE);
	return (false);
}

static void
nvgpu_fence_run_callback(struct dma_fence *dma __unused,
	struct dma_fence_cb *callback)
{
	struct nvgpu_fence_callback *adapter;
	void (*function)(void *);
	void *argument;

	adapter = container_of(callback, struct nvgpu_fence_callback, dma);
	function = adapter->callback;
	argument = adapter->callback_arg;
	function(argument);
	_kfree(adapter, M_NVGPU_FENCE);
}
