/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Unload admission gate for the native NVIDIA GPU driver.
 */

#include "nvgpu_unload.h"
#include "nvgpu_device.h"
#include "nvgpu_debug.h"

#include <drm/drmP.h>

#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/thread.h>

static MALLOC_DEFINE(M_NVGPU_UNLOAD, "nvgpu_unload", "nvgpu unload gate");

struct nvgpu_unload_state {
	struct lwkt_token token;
	bool unloading;
	uint32_t drm_refs;
	uint32_t mmap_refs;
	uint32_t last_open_count;
	uint32_t last_file_count;
	uint32_t last_mmap_count;
	uint32_t last_drm_ref_count;
	uint32_t busy_count;
};

/* Owned by drm.ko; serializes DRM open, close, and lastclose. */
extern struct lock drm_global_mutex;
/* Initialize unload admission state before DRM users can open the device. */
int
nvgpu_unload_init(struct nvgpu_device *gpu)
{
	struct nvgpu_unload_state *state;

	state = kmalloc(sizeof(*state), M_NVGPU_UNLOAD, M_WAITOK | M_ZERO);
	lwkt_token_init(&state->token, "nvgpuunl");
	nvgpu_device_set_unload_state(gpu, state);
	nvgpu_log(NVGPU_LOG_DEBUG, "unload init\n");
	return (0);
}

/* Release unload admission state after DRM is unpublished and all users are gone. */
void
nvgpu_unload_fini(struct nvgpu_device *gpu)
{
	struct nvgpu_unload_state *state;

	state = nvgpu_device_get_unload_state(gpu);
	if (state == NULL)
		return;
	if (state->drm_refs != 0)
		nvgpu_log(NVGPU_LOG_INFO,
		    "unload fini with %u DRM refs\n", state->drm_refs);
	KASSERT(state->mmap_refs == 0,
	    ("unload fini with live mmap pager references"));
	nvgpu_device_set_unload_state(gpu, NULL);
	lwkt_token_uninit(&state->token);
	_kfree(state, M_NVGPU_UNLOAD);
}

int
nvgpu_unload_hold_by_mmap(struct nvgpu_device *gpu)
{
	struct nvgpu_unload_state *state;
	int error = 0;

	state = nvgpu_device_get_unload_state(gpu);
	if (state == NULL)
		return (ENXIO);
	lwkt_gettoken(&state->token);
	if (state->unloading)
		error = EBUSY;
	else
		state->mmap_refs++;
	lwkt_reltoken(&state->token);
	return (error);
}

void
nvgpu_unload_release_by_mmap(struct nvgpu_device *gpu)
{
	struct nvgpu_unload_state *state;

	state = nvgpu_device_get_unload_state(gpu);
	if (state == NULL)
		return;
	lwkt_gettoken(&state->token);
	KASSERT(state->mmap_refs > 0, ("mmap unload refs underflow"));
	state->mmap_refs--;
	wakeup(&state->mmap_refs);
	lwkt_reltoken(&state->token);
}

/* Hold unload against one DRM open lifetime unless unload has started. */
int
nvgpu_unload_hold_by_drm(struct nvgpu_device *gpu)
{
	struct nvgpu_unload_state *state;
	int error = 0;

	state = nvgpu_device_get_unload_state(gpu);
	if (state == NULL)
		return (ENXIO);
	lwkt_gettoken(&state->token);
	if (state->unloading) {
		error = EBUSY;
	} else {
		state->drm_refs++;
		nvgpu_log(NVGPU_LOG_DEBUG,
		    "DRM hold gpu=%p state=%p count=%u\n", gpu, state, state->drm_refs);
	}
	lwkt_reltoken(&state->token);
	return (error);
}

/* Release one DRM unload hold previously acquired by nvgpu_unload_hold_by_drm(). */
void
nvgpu_unload_release_by_drm(struct nvgpu_device *gpu)
{
	struct nvgpu_unload_state *state;

	state = nvgpu_device_get_unload_state(gpu);
	if (state == NULL)
		return;
	lwkt_gettoken(&state->token);
	if (state->drm_refs == 0) {
		nvgpu_log(NVGPU_LOG_INFO, "DRM release without hold\n");
	} else {
		state->drm_refs--;
		nvgpu_log(NVGPU_LOG_DEBUG,
		    "DRM release gpu=%p state=%p count=%u\n", gpu, state, state->drm_refs);
	}
	lwkt_reltoken(&state->token);
}

/* Try to start unload after proving the DRM core has no live users. */
int
nvgpu_unload_try_begin(struct nvgpu_device *gpu)
{
	struct nvgpu_unload_state *state;
	struct drm_device *ddev;
	struct drm_file *file_priv;
	uint32_t file_count = 0;
	uint32_t mmap_count = 0;
	int error = 0;

	state = nvgpu_device_get_unload_state(gpu);
	if (state == NULL)
		return (0);
	ddev = nvgpu_device_get_drm_dev(gpu);
	if (ddev == NULL) {
		lwkt_gettoken(&state->token);
		state->unloading = true;
		lwkt_reltoken(&state->token);
		return (0);
	}

	/*
	 * Match the legacy driver gate: the decision is made while DRM open and
	 * close are serialized.  A racing open is either visible in open_count or
	 * reaches nvdrm_open() after unloading is set and gets refused.
	 */
	mutex_lock(&drm_global_mutex);
	mutex_lock(&ddev->filelist_mutex);
	list_for_each_entry(file_priv, &ddev->filelist, lhead)
		file_count++;
	mutex_unlock(&ddev->filelist_mutex);

	lwkt_gettoken(&state->token);
	mmap_count = state->mmap_refs;
	if (ddev->open_count != 0 || file_count != 0 || mmap_count != 0 ||
	    state->drm_refs != 0) {
		state->busy_count++;
		state->last_open_count = ddev->open_count;
		state->last_file_count = file_count;
		state->last_mmap_count = mmap_count;
		state->last_drm_ref_count = state->drm_refs;
		error = EBUSY;
	} else {
		state->unloading = true;
	}
	lwkt_reltoken(&state->token);
	mutex_unlock(&drm_global_mutex);

	nvgpu_log(NVGPU_LOG_DEBUG,
	    "unload gate observed: gpu=%p state=%p open=%u files=%u mmap=%u drm_refs=%u\n",
	    gpu, state, ddev->open_count, file_count, mmap_count,
	    state->drm_refs);

	if (error != 0) {
		nvgpu_log(NVGPU_LOG_INFO,
		    "unload busy: open=%u files=%u mmap=%u drm_refs=%u\n",
		    state->last_open_count, state->last_file_count,
		    state->last_mmap_count,
		    state->last_drm_ref_count);
	} else {
		nvgpu_log(NVGPU_LOG_DEBUG, "unload admitted\n");
	}
	return (error);
}

/* Cancel unload admission when detach is interrupted before teardown starts. */
void
nvgpu_unload_abort(struct nvgpu_device *gpu)
{
	struct nvgpu_unload_state *state;

	state = nvgpu_device_get_unload_state(gpu);
	if (state == NULL)
		return;
	lwkt_gettoken(&state->token);
	state->unloading = false;
	lwkt_reltoken(&state->token);
	nvgpu_log(NVGPU_LOG_DEBUG, "unload aborted\n");
}
