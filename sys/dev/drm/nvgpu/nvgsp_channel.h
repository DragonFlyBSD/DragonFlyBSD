/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * GSP channel backend boundary for the native NVIDIA GPU driver.
 */

#ifndef _NVGSP_CHANNEL_H_
#define _NVGSP_CHANNEL_H_

#include <stdbool.h>
#include <sys/stdint.h>

#define NVGSP_CHANNEL_ENGINE_GRAPHICS	1u
#define NVGSP_CHANNEL_ENGINE_COPY0	9u
#define NVGSP_CHANNEL_ENGINE_COPY1	10u
#define NVGSP_CHANNEL_ENGINE_COPY2	11u
#define NVGSP_CHANNEL_PUSH_NO_PREFETCH	0x1u

struct nvgpu_device;
struct nvgsp_channel;
struct nvgsp_channel_object;
struct nvgsp_channel_submission;
struct nvgsp_vmm;

struct nvgsp_channel_completion {
	volatile uint32_t *sema;
	uint32_t payload;
	uint32_t chid;
};

struct nvgsp_channel_push {
	uint64_t va;
	uint32_t va_len;
	uint32_t flags;
};

/*
 * Prepare one GPFIFO submission without ringing the doorbell.  Success keeps
 * the backend submit token held; the same scheduler LWKT must immediately
 * publish its no-fail pending record and then call commit.
 */
int nvgsp_channel_prepare_submit(struct nvgsp_channel *chan,
    const struct nvgsp_channel_push *pushes, uint32_t push_count,
    struct nvgsp_channel_submission **out);

/* Copy immutable completion identity before publishing the pending fence. */
void nvgsp_channel_describe_submit(struct nvgsp_channel_submission *submission,
    struct nvgsp_channel_completion *completion);

/* Publish GP_PUT, ring the doorbell, and consume submission. */
void nvgsp_channel_commit_submit(struct nvgsp_channel_submission *submission);

/* Roll back one prepared submission and consume it without ringing doorbell. */
void nvgsp_channel_abort_submit(struct nvgsp_channel_submission *submission);

/* Mark the current backend channel for chid faulted; future submits fail. */
void nvgsp_channel_mark_fault(struct nvgpu_device *gpu, uint32_t chid,
    int error);

/* Return backend host-channel capacity and already reserved boot channels. */
int nvgsp_channel_query_capacity(struct nvgpu_device *gpu, uint32_t *total,
    uint32_t *reserved);

/* Create the bootstrap channel during boot. */
int nvgsp_channel_create_bootstrap(struct nvgpu_device *gpu);
/* Destroy the bootstrap channel after submissions have stopped. */
void nvgsp_channel_destroy_bootstrap(struct nvgpu_device *gpu);
/* Create the golden channel used as a template for user channels. */
int nvgsp_channel_create_golden(struct nvgpu_device *gpu);
/* Destroy golden channel state after user channel creation has stopped. */
void nvgsp_channel_destroy_golden(struct nvgpu_device *gpu);
/* Create a user submission channel on an existing per-process VMM. */
int nvgsp_channel_create_user(struct nvgsp_vmm *vmm, uint32_t engine_type,
    struct nvgsp_channel **out);
/* Destroy a user submission channel after scheduler work has drained. */
void nvgsp_channel_destroy_user(struct nvgsp_channel *chan);
/* Promote GR context buffers for a user channel before GR-class object allocation. */
int nvgsp_channel_promote_graphics_context(struct nvgsp_channel *chan);
/* Allocate an RM engine object under a user channel. */
int nvgsp_channel_alloc_object(struct nvgsp_channel *chan, uint32_t handle,
    uint32_t oclass, struct nvgsp_channel_object **out);
/* Free an RM engine object allocated by nvgsp_channel_alloc_object(). */
void nvgsp_channel_free_object(struct nvgsp_channel_object *obj);

#endif /* _NVGSP_CHANNEL_H_ */
