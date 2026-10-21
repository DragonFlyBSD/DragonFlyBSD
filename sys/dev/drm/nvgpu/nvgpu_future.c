/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Fence dependency glue for native futures.
 */

#include "nvgpu_fence.h"
#include "nvgpu_future.h"
#include "nvgpu_sched.h"

#include <machine/atomic.h>
#include <sys/errno.h>
#include <sys/ktr.h>
#include <sys/malloc.h>
#include <sys/systm.h>

struct nvgpu_future_wait_set {
	struct nvgpu_future *future;
	volatile u_int pending;
};

struct nvgpu_future_wait {
	struct nvgpu_future_wait_set *set;
	struct nvgpu_fence *fence;
};

static MALLOC_DEFINE(M_NVGPU_FUTURE, "nvgpu_future",
    "nvgpu future wait callbacks");

#ifndef KTR_NVGPU
#define KTR_NVGPU KTR_ALL
#endif

KTR_INFO_MASTER_EXTERN(nvgpu);
KTR_INFO(KTR_NVGPU, nvgpu, future_spawn, 23,
    "future spawn future=%p wait_count=%ju", void *future,
    uintmax_t wait_count);
KTR_INFO(KTR_NVGPU, nvgpu, future_wait_done, 23,
    "future wait done future=%p old=%u", void *future, u_int old);

static void nvgpu_future_wait_complete(void *argument);

int
nvgpu_future_spawn(struct nvgpu_future *future, struct nvgpu_fence **waits,
    size_t wait_count)
{
	struct nvgpu_future_wait_set *set;
	struct nvgpu_future_wait *wait;
	int error;

	KTR_LOG(nvgpu_future_spawn, future, (uintmax_t)wait_count);
	if (future == NULL || future->poll == NULL ||
	    (wait_count != 0 && waits == NULL))
		return (EINVAL);
	for (size_t i = 0; i < wait_count; i++) {
		if (waits[i] == NULL)
			return (EINVAL);
	}
	if (wait_count == 0)
		return (nvgpu_sched_put(future));

	set = kmalloc(sizeof(*set), M_NVGPU_FUTURE, M_WAITOK | M_ZERO);
	set->future = future;
	set->pending = 1;

	for (size_t i = 0; i < wait_count; i++) {
		wait = kmalloc(sizeof(*wait), M_NVGPU_FUTURE, M_WAITOK | M_ZERO);
		wait->set = set;
		wait->fence = waits[i];
		nvgpu_fence_addref(wait->fence);
		atomic_fetchadd_int(&set->pending, 1);
		if (!nvgpu_fence_add_callback_unless_signaled(wait->fence,
		    nvgpu_future_wait_complete, wait)) {
			nvgpu_fence_release(wait->fence);
			_kfree(wait, M_NVGPU_FUTURE);
			atomic_fetchadd_int(&set->pending, -1);
		}
	}

	if (atomic_fetchadd_int(&set->pending, -1) != 1)
		return (0);
	error = nvgpu_sched_put(future);
	_kfree(set, M_NVGPU_FUTURE);
	return (error);
}

static void
nvgpu_future_wait_complete(void *argument)
{
	struct nvgpu_future_wait *wait;
	struct nvgpu_future_wait_set *set;
	struct nvgpu_future *future;
	u_int old;
	int error;

	wait = argument;
	set = wait->set;
	nvgpu_fence_release(wait->fence);
	_kfree(wait, M_NVGPU_FUTURE);
	old = atomic_fetchadd_int(&set->pending, -1);
	KTR_LOG(nvgpu_future_wait_done, set->future, old);
	if (old != 1)
		return;
	future = set->future;
	_kfree(set, M_NVGPU_FUTURE);
	error = nvgpu_sched_put(future);
	KASSERT(error == 0, ("future wake after scheduler stop: %d", error));
}
