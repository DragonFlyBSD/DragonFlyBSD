/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Proc-owned GPU channel interface.
 */

#ifndef _NVGPU_CHANNEL_H_
#define _NVGPU_CHANNEL_H_

#include <stdbool.h>
#include <sys/queue.h>
#include <sys/stdint.h>
#include <sys/types.h>

#define NVGPU_CHANNEL_GPFIFO_ENTRIES	512u
#define NVGPU_CHANNEL_PUSH_NO_PREFETCH	0x1u

struct nvgpu_channel;
struct nvgpu_device;
struct nvgpu_fence;
struct nvgpu_future;
struct nvgpu_sema;
struct nvgsp_vmm;

#define NVGPU_MAX_CHANNEL_OBJECTS 16u

struct nvgpu_channel_object {
	uint32_t handle;
	uint32_t oclass;
	uint64_t nvif_object;
	struct nvgsp_channel_object *backend;
};

struct nvgpu_channel {
	TAILQ_ENTRY(nvgpu_channel) link;
	volatile u_int refs;
	uint32_t id;
	uint32_t engine_type;
	struct nvgpu_device *device;
	struct nvgsp_channel *backend;
	struct nvgpu_channel_object objects[NVGPU_MAX_CHANNEL_OBJECTS];
};

TAILQ_HEAD(nvgpu_channel_list, nvgpu_channel);

/* One native GPFIFO push adapted from the nouveau EXEC payload. */
struct nvgpu_channel_push {
	uint64_t va;
	uint32_t va_len;
	uint32_t flags;
};

/*
 * Native channel creation request and result.
 *
 * The channel module fills the result fields after creating the channel.  Its
 * caller decides whether and where to publish the returned owned channel.
 */
struct nvgpu_channel_create_args {
	uint32_t fb_ctxdma_handle;
	uint32_t tt_ctxdma_handle;
	int32_t channel_id;
	uint32_t pushbuf_domains;
	uint32_t notifier_handle;
	uint32_t nr_subchan;
};

/* Native data consumed by one atomic channel submission transaction. */
struct nvgpu_channel_submit_args {
	const struct nvgpu_channel_push *pushes;
	size_t push_count;
	struct nvgpu_fence *submitted;
	struct nvgpu_sema *sema;
};

/*
 * Create a user channel for a borrowed device and VMM backend.
 *
 * On success result receives the owned channel.  This function may sleep while
 * creating GSP channel state.
 */
int nvgpu_channel_create(struct nvgpu_device *device, struct nvgsp_vmm *vmm,
	struct nvgpu_channel_list *channels, struct nvgpu_channel_create_args *args,
	struct nvgpu_channel **result);

/* Hold or release one channel reference.  The final release destroys the backend channel. */
void nvgpu_channel_addref(struct nvgpu_channel *channel);
void nvgpu_channel_release(struct nvgpu_channel *channel);

/*
 * Atomically write one FIFO submission, park future, and ring the doorbell.
 *
 * This function is MPSAFE.  On zero interrupt state owns future.  EAGAIN means
 * the FIFO was full and caller still owns future; no GPU work was published.
 * Every other error also leaves future with caller and no submission in flight.
 */
int nvgpu_channel_submit(struct nvgpu_channel *channel,
	struct nvgpu_channel_submit_args *args,
	struct nvgpu_future *future);

int nvgpu_channel_create_object(struct nvgpu_channel *channel,
	uint64_t nvif_object, uint32_t handle, uint32_t oclass,
	int needs_gr_context);
int nvgpu_channel_destroy_object(struct nvgpu_channel_list *channels,
	uint64_t nvif_object);

#endif /* _NVGPU_CHANNEL_H_ */
