/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * User channel lifetime for one GPU process.
 */

#include "nvdrm_nouveau_abi.h"
#include "nvgpu_channel.h"
#include "nvgpu_debug.h"
#include "nvgpu_device.h"
#include "nvgpu_fence.h"
#include "nvgpu_future.h"
#include "nvgpu_intr.h"
#include "nvgsp_channel.h"
#include "nvgsp_vmm.h"

#include <sys/errno.h>
#include <sys/kernel.h>
#include <sys/ktr.h>
#include <sys/malloc.h>
#include <sys/systm.h>
#include <machine/atomic.h>

static MALLOC_DEFINE(M_NVGPU_CHANNEL, "nvgpu_channel", "nvgpu user channel");

#ifndef KTR_NVGPU
#define KTR_NVGPU KTR_ALL
#endif

KTR_INFO_MASTER_EXTERN(nvgpu);
KTR_INFO(KTR_NVGPU, nvgpu, channel_submit, 25,
    "channel submit channel=%u future=%p push=%ju error=%d",
    uint32_t channel, void *future, uintmax_t push_count, int error);
KTR_INFO(KTR_NVGPU, nvgpu, channel_doorbell, 25,
    "channel doorbell channel=%u future=%p target=%u", uint32_t channel,
    void *future, uint32_t target);

static volatile u_int nvgpu_channel_next_id = 1;

static void nvgpu_channel_finalize_objects(struct nvgpu_channel *chan);
static int nvgpu_channel_select_engine(const struct nvgpu_channel_create_args *args,
									   uint32_t *engine_type);

int
nvgpu_channel_create(struct nvgpu_device *device, struct nvgsp_vmm *vmm,
    struct nvgpu_channel_list *channels, struct nvgpu_channel_create_args *args,
    struct nvgpu_channel **result)
{
	struct nvgpu_channel *chan;
	uint32_t engine_type;
	int error;

	if (device == NULL || vmm == NULL || channels == NULL || args == NULL ||
	    result == NULL)
		return (EINVAL);
	error = nvgpu_device_reserve_channel(device);
	if (error != 0)
		return (error);

	error = nvgpu_channel_select_engine(args, &engine_type);
	if (error != 0)
		goto fail_quota;

	chan = kmalloc(sizeof(*chan), M_NVGPU_CHANNEL, M_WAITOK | M_ZERO);
	chan->device = device;
	chan->id = atomic_fetchadd_int(&nvgpu_channel_next_id, 1);
	chan->refs = 1;
	chan->engine_type = engine_type;
	error = nvgsp_channel_create_user(vmm, engine_type, &chan->backend);
	if (error != 0) {
		_kfree(chan, M_NVGPU_CHANNEL);
		goto fail_quota;
	}

	TAILQ_INSERT_TAIL(channels, chan, link);
	args->channel_id = (int32_t)chan->id;
	args->pushbuf_domains = NOUVEAU_GEM_DOMAIN_VRAM;
	args->notifier_handle = 0;
	args->nr_subchan = 0;
	*result = chan;
	return (0);

fail_quota:
	nvgpu_device_release_channel(device);
	return (error);
}

void
nvgpu_channel_addref(struct nvgpu_channel* chan) {
	if (chan != NULL)
		atomic_fetchadd_int(&chan->refs, 1);
}

void
nvgpu_channel_release(struct nvgpu_channel *chan)
{
	u_int refs;

	if (chan == NULL)
		return;
	refs = atomic_fetchadd_int(&chan->refs, -1);
	KASSERT(refs != 0, ("nvgpu channel refs underflow"));
	if (refs != 1)
		return;

	nvgpu_channel_finalize_objects(chan);
	if (chan->backend != NULL)
		nvgsp_channel_destroy_user(chan->backend);
	nvgpu_device_release_channel(chan->device);
	_kfree(chan, M_NVGPU_CHANNEL);
}

int
nvgpu_channel_submit(struct nvgpu_channel *channel,
    struct nvgpu_channel_submit_args *args, struct nvgpu_future *future)
{
	struct nvgsp_channel_completion completion;
	struct nvgsp_channel_submission *submission;
	int error;

	if (channel == NULL || args == NULL || future == NULL ||
	    args->submitted == NULL || args->sema == NULL ||
	    (args->push_count != 0 && args->pushes == NULL) ||
	    args->push_count > NVGPU_CHANNEL_GPFIFO_ENTRIES - 2)
		return (EINVAL);
	error = nvgsp_channel_prepare_submit(channel->backend,
	    (const struct nvgsp_channel_push *)args->pushes,
	    (uint32_t)args->push_count, &submission);
	KTR_LOG(nvgpu_channel_submit, channel->id, future,
	    (uintmax_t)args->push_count, error);
	if (error != 0)
		return (error);
	nvgsp_channel_describe_submit(submission, &completion);
	args->sema->device = channel->device;
	args->sema->address = completion.sema;
	args->sema->target = completion.payload;
	args->sema->chid = completion.chid;
	args->sema->error = 0;
	error = nvgpu_intr_park(args->sema, future);
	if (error != 0) {
		KTR_LOG(nvgpu_channel_submit, channel->id, future,
		    (uintmax_t)args->push_count, error);
		nvgsp_channel_abort_submit(submission);
		memset(args->sema, 0, sizeof(*args->sema));
		return (error);
	}
	error = nvgpu_fence_signal(args->submitted, 0);
	KASSERT(error == 0, ("signaling submitted fence failed: %d", error));
	KTR_LOG(nvgpu_channel_doorbell, channel->id, future, args->sema->target);
	nvgsp_channel_commit_submit(submission);
	return (0);
}

static struct nvgpu_channel_object *
nvgpu_channel_object_slot(struct nvgpu_channel *chan)
{
	uint32_t i;

	for (i = 0; i < NVGPU_MAX_CHANNEL_OBJECTS; i++) {
		if (chan->objects[i].oclass == 0)
			return (&chan->objects[i]);
	}
	return (NULL);
}

static void
nvgpu_channel_finalize_objects(struct nvgpu_channel *chan)
{
	uint32_t i;

	for (i = 0; i < NVGPU_MAX_CHANNEL_OBJECTS; i++) {
		struct nvgpu_channel_object *obj = &chan->objects[i];

		if (obj->oclass == 0)
			continue;
		if (obj->backend != NULL)
			nvgsp_channel_free_object(obj->backend);
		memset(obj, 0, sizeof(*obj));
	}
}

static int
nvgpu_channel_select_engine(const struct nvgpu_channel_create_args *args,
    uint32_t *engine_type)
{
	*engine_type = NVGSP_CHANNEL_ENGINE_GRAPHICS;
	if (args->fb_ctxdma_handle != ~0u)
		return (0);

	switch (args->tt_ctxdma_handle) {
	case NOUVEAU_FIFO_ENGINE_GR:
		*engine_type = NVGSP_CHANNEL_ENGINE_GRAPHICS;
		return (0);
	case NOUVEAU_FIFO_ENGINE_CE:
		*engine_type = NVGSP_CHANNEL_ENGINE_COPY0;
		return (0);
	default:
		return (ENOSYS);
	}
}

/* Create an NVIF engine object under a channel selected by the NVIF token. */
int
nvgpu_channel_create_object(struct nvgpu_channel *chan,
    uint64_t nvif_object, uint32_t handle, uint32_t oclass,
    int needs_gr_context)
{
	struct nvgpu_channel_object *obj;
	struct nvgsp_channel_object *backend;
	int error;
	uint32_t rm_handle;
	uint32_t slot;

	if (chan == NULL)
		return (EINVAL);
	if (chan->backend == NULL)
		return (ENOENT);
	obj = nvgpu_channel_object_slot(chan);
	if (obj == NULL)
		return (ENOMEM);
	slot = (uint32_t)(obj - chan->objects);
	rm_handle = handle;
	if (rm_handle == 0) {
		rm_handle = ((oclass & 0xffffu) << 16) |
		    ((chan->id + slot) & 0xffffu);
		nvgpu_log(NVGPU_LOG_DEBUG,
		    "channel object generated RM handle=0x%x class=0x%x channel=%u slot=%u\n",
		    rm_handle, oclass, chan->id, slot);
	}
	if (needs_gr_context) {
		error = nvgsp_channel_promote_graphics_context(chan->backend);
		if (error != 0)
			return (error);
	}
	error = nvgsp_channel_alloc_object(chan->backend, rm_handle, oclass,
	    &backend);
	if (error != 0)
		return (error);
	obj->handle = rm_handle;
	obj->oclass = oclass;
	obj->nvif_object = nvif_object;
	obj->backend = backend;
	return (0);
}

/* Delete an NVIF engine object if it is still live.  Unknown objects are ignored. */
int
nvgpu_channel_destroy_object(struct nvgpu_channel_list *channels,
    uint64_t nvif_object)
{
	struct nvgpu_channel *chan, *found;
	uint32_t i;

	if (channels == NULL)
		return (EINVAL);
	found = NULL;
	TAILQ_FOREACH(chan, channels, link) {
		for (i = 0; i < NVGPU_MAX_CHANNEL_OBJECTS; i++) {
			struct nvgpu_channel_object *obj = &chan->objects[i];

			if (obj->oclass == 0 || obj->nvif_object != nvif_object)
				continue;
			found = chan;
			break;
		}
		if (found != NULL)
			break;
	}
	if (found == NULL)
		return (0);
	for (i = 0; i < NVGPU_MAX_CHANNEL_OBJECTS; i++) {
		struct nvgpu_channel_object *obj = &found->objects[i];

		if (obj->oclass == 0 || obj->nvif_object != nvif_object)
			continue;
		if (obj->backend != NULL)
			nvgsp_channel_free_object(obj->backend);
		memset(obj, 0, sizeof(*obj));
		break;
	}
	return (0);
}
