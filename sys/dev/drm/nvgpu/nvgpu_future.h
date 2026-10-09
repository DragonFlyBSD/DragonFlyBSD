/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Stackless future interface for the native NVIDIA GPU driver.
 */

#ifndef _NVGPU_FUTURE_H_
#define _NVGPU_FUTURE_H_

#include <stdbool.h>
#include <sys/queue.h>
#include <sys/types.h>

struct nvgpu_fence;
struct nvgpu_future;
struct nvgpu_sema;

/* Result returned by one concrete future poll. */
struct nvgpu_future_result {
	int result;
	bool ready;
};

#define NVGPU_FUTURE_PENDING 	((struct nvgpu_future_result){ .result = 0, .ready = false })

#define NVGPU_FUTURE_READY(value) 	((struct nvgpu_future_result){ .result = (value), .ready = true })

/*
 * A future exposes only its consuming poll operation plus one queue link.
 * Before returning pending, poll transfers ownership to exactly one waker.
 * Before returning ready, poll releases all concrete resources and frees itself.
 */
struct nvgpu_future {
	TAILQ_ENTRY(nvgpu_future) link;
	struct nvgpu_future_result (*poll)(struct nvgpu_future *future);
	struct nvgpu_sema *parked_sema;
};

/*
 * Spawn a future after all borrowed wait fences have completed.
 * On success this function consumes future.  A zero-length wait set queues it
 * immediately; otherwise callback closures transfer it to the scheduler.
 */
int nvgpu_future_spawn(struct nvgpu_future *future,
	struct nvgpu_fence **waits, size_t wait_count);

#endif /* _NVGPU_FUTURE_H_ */
