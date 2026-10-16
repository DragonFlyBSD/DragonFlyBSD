/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Nouveau syncobj/timeline glue for EXEC and VM_BIND futures.
 */

#ifndef _NVDRM_SYNC_H_
#define _NVDRM_SYNC_H_

#include <sys/stdint.h>

struct drm_file;
struct nvgpu_fence;
struct nvdrm_sync_signal;

struct nvdrm_sync_wait_set {
	struct nvgpu_fence **fences;
	uint32_t count;
};

struct nvdrm_sync_signal_set {
	struct nvdrm_sync_signal *signals;
	uint32_t count;
};

/*
 * Resolve userspace wait syncobjs into owned native fence references.
 *
 * On success set owns every fence and must be passed to cleanup_waits().
 * The dma-fence to native-fence cast is confined to this DRM boundary.
 */
int nvdrm_sync_collect_wait_fences(struct drm_file *file, uint32_t count,
    uint64_t wait_ptr, struct nvdrm_sync_wait_set *set);

/*
 * Resolve signal handles and prepare publication of one borrowed done fence.
 *
 * On success set owns all prepared DRM references.  Publishing or cleanup
 * consumes that state; neither operation consumes the caller's done reference.
 */
int nvdrm_sync_prepare_signals(struct drm_file *file, uint32_t count,
    uint64_t sig_ptr, struct nvgpu_fence *done_fence,
    struct nvdrm_sync_signal_set *set);

/* Publish all prepared binary/timeline syncobj points without consuming set. */
void nvdrm_sync_publish_signals(struct nvdrm_sync_signal_set *set);

/* Release every prepared signal resource and reset set to empty. */
void nvdrm_sync_cleanup_signals(struct nvdrm_sync_signal_set *set);

/* Release every owned native wait fence and reset set to empty. */
void nvdrm_sync_cleanup_waits(struct nvdrm_sync_wait_set *set);

/*
 * Wait interruptibly for one borrowed native fence at the DRM boundary.
 * Returns zero after successful completion or a positive wait/producer errno.
 */
int nvdrm_sync_wait_fence(struct nvgpu_fence *fence);

#endif /* _NVDRM_SYNC_H_ */
