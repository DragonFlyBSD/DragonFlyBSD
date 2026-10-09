/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Nouveau syncobj/timeline glue for EXEC and VM_BIND futures.
 */

#include "nvdrm_sync.h"
#include "nvdrm_nouveau_abi.h"
#include "nvgpu_fence.h"

#include <drm/drmP.h>
#include <drm/drm_syncobj.h>
#include <linux/dma-fence-chain.h>
#include <sys/errno.h>
#include <sys/malloc.h>
#include <sys/systm.h>

static MALLOC_DEFINE(M_NVDRM_SYNC, "nvdrm_sync", "nvdrm sync glue");

struct nvdrm_sync_signal {
	uint32_t type;
	uint64_t timeline_value;
	struct drm_syncobj *syncobj;
	struct dma_fence *fence;
	struct dma_fence_chain *chain;
};

static void nvdrm_sync_signal_put(struct nvdrm_sync_signal *signal);

int
nvdrm_sync_collect_wait_fences(struct drm_file *file, uint32_t count,
    uint64_t wait_ptr, struct nvdrm_sync_wait_set *set)
{
	struct drm_nouveau_sync *waits;
	struct nvgpu_fence **fences;
	size_t size;
	uint32_t retained;
	int error;

	set->fences = NULL;
	set->count = 0;
	if (count == 0)
		return (0);
	if (count > 64)
		return (EINVAL);
	size = sizeof(*waits) * count;
	waits = kmalloc(size, M_NVDRM_SYNC, M_WAITOK | M_ZERO);
	fences = kmalloc(sizeof(*fences) * count, M_NVDRM_SYNC,
	    M_WAITOK | M_ZERO);
	error = copyin((const void *)(uintptr_t)wait_ptr, waits, size);
	if (error != 0) {
		_kfree(waits, M_NVDRM_SYNC);
		_kfree(fences, M_NVDRM_SYNC);
		return (EFAULT);
	}
	retained = 0;
	for (uint32_t i = 0; i < count; i++) {
		struct dma_fence *dma;
		uint32_t type;

		type = waits[i].flags & DRM_NOUVEAU_SYNC_TYPE_MASK;
		if (type != DRM_NOUVEAU_SYNC_SYNCOBJ &&
		    type != DRM_NOUVEAU_SYNC_TIMELINE_SYNCOBJ) {
			error = EINVAL;
			break;
		}
		error = -drm_syncobj_find_fence(file, waits[i].handle,
		    type == DRM_NOUVEAU_SYNC_TIMELINE_SYNCOBJ ?
		    waits[i].timeline_value : 0, &dma);
		if (error != 0)
			break;
		if (dma_fence_is_signaled(dma)) {
			struct dma_fence_chain *chain;
			int producer_error;

			chain = to_dma_fence_chain(dma);
			producer_error = dma->error;
			if (producer_error == 0 && chain != NULL &&
			    chain->fence != NULL)
				producer_error = chain->fence->error;
			if (producer_error == 0) {
				dma_fence_put(dma);
				continue;
			}
			fences[retained] = nvgpu_fence_create();
			if (fences[retained] == NULL) {
				dma_fence_put(dma);
				error = ENOMEM;
				break;
			}
			if (producer_error < 0)
				producer_error = -producer_error;
			(void)nvgpu_fence_signal(fences[retained], producer_error);
			retained++;
			dma_fence_put(dma);
			continue;
		}
		fences[retained++] = (struct nvgpu_fence *)dma;
	}
	_kfree(waits, M_NVDRM_SYNC);
	if (error != 0) {
		for (uint32_t i = 0; i < retained; i++)
			nvgpu_fence_release(fences[i]);
		_kfree(fences, M_NVDRM_SYNC);
		set->count = 0;
		return (error);
	}
	set->count = retained;
	if (retained == 0) {
		_kfree(fences, M_NVDRM_SYNC);
		fences = NULL;
	}
	set->fences = fences;
	return (error);
}

int
nvdrm_sync_prepare_signals(struct drm_file *file, uint32_t count,
    uint64_t sig_ptr, struct nvgpu_fence *done_fence,
    struct nvdrm_sync_signal_set *set)
{
	struct drm_nouveau_sync *sigs;
	struct nvdrm_sync_signal *signals;
	size_t size;
	int error;

	set->signals = NULL;
	set->count = 0;
	if (count == 0)
		return (0);
	if (done_fence == NULL || count > 64)
		return (EINVAL);
	size = sizeof(*sigs) * count;
	sigs = kmalloc(size, M_NVDRM_SYNC, M_WAITOK | M_ZERO);
	signals = kmalloc(sizeof(*signals) * count, M_NVDRM_SYNC,
	    M_WAITOK | M_ZERO);
	error = copyin((const void *)(uintptr_t)sig_ptr, sigs, size);
	if (error != 0) {
		error = EFAULT;
		goto fail;
	}
	for (uint32_t i = 0; i < count; i++) {
		uint32_t type;

		type = sigs[i].flags & DRM_NOUVEAU_SYNC_TYPE_MASK;
		if (type != DRM_NOUVEAU_SYNC_SYNCOBJ &&
		    type != DRM_NOUVEAU_SYNC_TIMELINE_SYNCOBJ) {
			error = EINVAL;
			goto fail;
		}
		signals[i].type = type;
		signals[i].timeline_value = sigs[i].timeline_value;
		signals[i].syncobj = drm_syncobj_find(file, sigs[i].handle);
		if (signals[i].syncobj == NULL) {
			error = ENOENT;
			goto fail;
		}
		nvgpu_fence_addref(done_fence);
		signals[i].fence = &done_fence->dma;
		if (type == DRM_NOUVEAU_SYNC_TIMELINE_SYNCOBJ) {
			signals[i].chain = dma_fence_chain_alloc();
			if (signals[i].chain == NULL) {
				error = ENOMEM;
				goto fail;
			}
		}
	}
	_kfree(sigs, M_NVDRM_SYNC);
	set->signals = signals;
	set->count = count;
	return (0);

fail:
	if (signals != NULL) {
		for (uint32_t i = 0; i < count; i++)
			nvdrm_sync_signal_put(&signals[i]);
		_kfree(signals, M_NVDRM_SYNC);
	}
	_kfree(sigs, M_NVDRM_SYNC);
	return (error);
}

void
nvdrm_sync_publish_signals(struct nvdrm_sync_signal_set *set)
{
	if (set == NULL || set->signals == NULL)
		return;
	for (uint32_t i = 0; i < set->count; i++) {
		struct nvdrm_sync_signal *signal = &set->signals[i];

		if (signal->syncobj == NULL)
			continue;
		if (signal->type == DRM_NOUVEAU_SYNC_TIMELINE_SYNCOBJ) {
			drm_syncobj_add_point(signal->syncobj, signal->chain,
			    dma_fence_get(signal->fence), signal->timeline_value);
			signal->chain = NULL;
		} else {
			drm_syncobj_replace_fence(signal->syncobj, 0,
			    signal->fence);
		}
		drm_syncobj_put(signal->syncobj);
		signal->syncobj = NULL;
	}
}

void
nvdrm_sync_cleanup_signals(struct nvdrm_sync_signal_set *set)
{
	if (set == NULL || set->signals == NULL)
		return;
	for (uint32_t i = 0; i < set->count; i++)
		nvdrm_sync_signal_put(&set->signals[i]);
	_kfree(set->signals, M_NVDRM_SYNC);
	set->signals = NULL;
	set->count = 0;
}

void
nvdrm_sync_cleanup_waits(struct nvdrm_sync_wait_set *set)
{
	if (set == NULL || set->fences == NULL)
		return;
	for (uint32_t i = 0; i < set->count; i++)
		nvgpu_fence_release(set->fences[i]);
	_kfree(set->fences, M_NVDRM_SYNC);
	set->fences = NULL;
	set->count = 0;
}

int
nvdrm_sync_wait_fence(struct nvgpu_fence *fence)
{
	long waited;

	if (fence == NULL)
		return (EINVAL);
	waited = dma_fence_wait(&fence->dma, true);
	if (waited < 0)
		return ((int)-waited);
	return (nvgpu_fence_get_error(fence));
}

static void
nvdrm_sync_signal_put(struct nvdrm_sync_signal *signal)
{
	if (signal == NULL)
		return;
	dma_fence_put(signal->fence);
	signal->fence = NULL;
	if (signal->chain != NULL) {
		dma_fence_chain_free(signal->chain);
		signal->chain = NULL;
	}
	if (signal->syncobj != NULL) {
		drm_syncobj_put(signal->syncobj);
		signal->syncobj = NULL;
	}
}
