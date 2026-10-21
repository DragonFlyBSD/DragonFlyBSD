/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Per-open GPU process boundary.
 */

#include "nvgpu_channel.h"
#include "nvgpu_bo.h"
#include "nvgpu_chip.h"
#include "nvgpu_debug.h"
#include "nvgpu_device.h"
#include "nvgpu_fence.h"
#include "nvgpu_future.h"
#include "nvgpu_intr.h"
#include "nvgpu_proc.h"
#include "nvgpu_sched.h"
#include "nvgpu_unload.h"
#include "nvgpu_vm.h"
#include "nvgsp_channel.h"

#include <linux/reservation.h>
#include <machine/atomic.h>
#include <sys/errno.h>
#include <sys/kernel.h>
#include <sys/ktr.h>
#include <sys/malloc.h>
#include <sys/param.h>
#include <sys/systm.h>

static MALLOC_DEFINE(M_NVGPU_PROC, "nvgpu_proc", "nvgpu process state");
static MALLOC_DEFINE(M_NVGPU_EXEC, "nvgpu_exec", "nvgpu exec future");

#ifndef KTR_NVGPU
#define KTR_NVGPU KTR_ALL
#endif

KTR_INFO_MASTER_EXTERN(nvgpu);
KTR_INFO(KTR_NVGPU, nvgpu, exec_spawn, 26,
    "exec spawn proc=%p channel=%u push=%ju wait=%ju", void *proc,
    uint32_t channel, uintmax_t push_count, uintmax_t wait_count);
KTR_INFO(KTR_NVGPU, nvgpu, exec_poll, 26,
    "exec poll future=%p phase=%u error=%d", void *future, u_int phase,
    int error);
KTR_INFO(KTR_NVGPU, nvgpu, exec_submit, 26,
    "exec submit future=%p channel=%u push=%ju error=%d", void *future,
    uint32_t channel, uintmax_t push_count, int error);
KTR_INFO(KTR_NVGPU, nvgpu, exec_complete, 26,
    "exec complete future=%p channel=%u error=%d", void *future,
    uint32_t channel, int error);

struct nvgpu_proc_exec_record {
	TAILQ_ENTRY(nvgpu_proc_exec_record) link;
	struct nvgpu_fence *done;
	struct nvgpu_fence *submitted;
	u_int channel_id;
};
TAILQ_HEAD(nvgpu_proc_exec_list, nvgpu_proc_exec_record);

struct nvgpu_proc {
	struct nvgpu_device *device;
	struct lwkt_token token;
	volatile u_int refs;
	struct nvgpu_channel_list channels;
	struct nvgpu_proc_exec_list execs;
	struct reservation_object vm_resv;
	struct nvgpu_vm *vm;
};

struct nvgpu_exec_future {
	struct nvgpu_future base;
	struct nvgpu_proc *proc;
	struct nvgpu_channel *channel;
	struct nvgpu_proc_exec_record record;
	struct nvgpu_fence *done;
	struct nvgpu_fence *submitted;
	struct nvgpu_sema sema;
	struct nvgpu_channel_push *pushes;
	size_t push_count;
};

static void nvgpu_proc_finalize(struct nvgpu_proc *proc);
static struct nvgpu_future_result nvgpu_proc_poll_exec(
	struct nvgpu_future *future);

struct nvgpu_device *
nvgpu_proc_get_device(struct nvgpu_proc *proc)
{
	return (proc != NULL ? proc->device : NULL);
}

int
nvgpu_proc_create(struct nvgpu_device *device, struct nvgpu_proc **result)
{
	struct nvgpu_proc *proc;

	if (device == NULL || result == NULL)
		return (EINVAL);
	proc = kmalloc(sizeof(*proc), M_NVGPU_PROC, M_WAITOK | M_ZERO);
	proc->device = device;
	proc->refs = 1;
	lwkt_token_init(&proc->token, "nvgprc");
	TAILQ_INIT(&proc->channels);
	TAILQ_INIT(&proc->execs);
	reservation_object_init(&proc->vm_resv);
	*result = proc;
	nvgpu_log(NVGPU_LOG_DEBUG, "proc created proc=%p\n", proc);
	return (0);
}

void
nvgpu_proc_addref(struct nvgpu_proc *proc)
{
	if (proc != NULL)
		atomic_fetchadd_int(&proc->refs, 1);
}

void
nvgpu_proc_release(struct nvgpu_proc *proc)
{
	u_int refs;

	if (proc == NULL)
		return;
	refs = atomic_fetchadd_int(&proc->refs, -1);
	KASSERT(refs != 0, ("nvgpu proc refs underflow"));
	if (refs == 1)
		nvgpu_proc_finalize(proc);
}

int
nvgpu_proc_init_vm(struct nvgpu_proc *proc, uint64_t addr,
    uint64_t size)
{
	int error;

	if (proc == NULL)
		return (EINVAL);
	lwkt_gettoken(&proc->token);
	error = nvgpu_vm_init(proc->device, &proc->vm, addr, size);
	lwkt_reltoken(&proc->token);
	return (error);
}

int
nvgpu_proc_create_channel(struct nvgpu_proc *proc,
    struct nvgpu_channel_create_args *args)
{
	struct nvgpu_channel *channel;
	struct nvgsp_vmm *vmm;
	int error;

	if (proc == NULL || args == NULL)
		return (EINVAL);
	lwkt_gettoken(&proc->token);
	error = nvgpu_vm_ensure_backend(proc->device, &proc->vm, &vmm);
	if (error == 0)
		error = nvgpu_channel_create(proc->device, vmm, &proc->channels,
		    args, &channel);
	lwkt_reltoken(&proc->token);
	return (error);
}

int
nvgpu_proc_release_channel(struct nvgpu_proc *proc, int32_t channel_id)
{
	struct nvgpu_channel *chan;

	if (proc == NULL)
		return (EINVAL);
	lwkt_gettoken(&proc->token);
	TAILQ_FOREACH(chan, &proc->channels, link) {
		if (chan->id == channel_id) {
			TAILQ_REMOVE(&proc->channels, chan, link);
			break;
		}
	}
	lwkt_reltoken(&proc->token);
	if (chan != NULL)
		nvgpu_channel_release(chan);
	return 0;
}

int
nvgpu_proc_get_channel_classes(struct nvgpu_proc *proc, uint64_t token,
    uint32_t classes[5], uint32_t *count)
{
	struct nvgpu_channel *channel;
	const struct nvgpu_chip_config *chip;
	uint32_t class_count;
	int error;

	if (proc == NULL || classes == NULL || count == NULL)
		return (EINVAL);
	class_count = 0;
	error = 0;
	lwkt_gettoken(&proc->token);
	TAILQ_FOREACH(channel, &proc->channels, link) {
		if (channel->id == (uint32_t)token)
			break;
	}
	if (channel == NULL) {
		error = ENOENT;
	} else {
		chip = nvgpu_device_get_chip(proc->device);
		if (chip == NULL) {
			error = ENXIO;
		} else {
			switch (channel->engine_type) {
			case NVGSP_CHANNEL_ENGINE_GRAPHICS:
				classes[class_count++] = chip->class_3d;
				classes[class_count++] = chip->class_compute;
				classes[class_count++] = chip->class_copy;
				classes[class_count++] = chip->class_twod;
				classes[class_count++] = chip->class_m2mf;
				break;
			case NVGSP_CHANNEL_ENGINE_COPY0:
				classes[class_count++] = chip->class_copy;
				break;
			default:
				error = EINVAL;
				break;
			}
		}
	}
	if (error == 0)
		*count = class_count;
	lwkt_reltoken(&proc->token);
	return (error);
}

int
nvgpu_proc_create_channel_object(struct nvgpu_proc *proc, uint64_t token,
    uint64_t nvif_object, uint32_t handle, uint32_t oclass)
{
	struct nvgpu_channel *channel;
	const struct nvgpu_chip_config *chip;
	int needs_gr_context;
	int error;

	if (proc == NULL)
		return (EINVAL);
	lwkt_gettoken(&proc->token);
	TAILQ_FOREACH(channel, &proc->channels, link) {
		if (channel->id == (uint32_t)token)
			break;
	}
	if (channel == NULL)
		error = ENOENT;
	else {
		chip = nvgpu_device_get_chip(proc->device);
		needs_gr_context = 0;
		if (chip == NULL) {
			error = ENXIO;
		} else if (channel->engine_type ==
		    NVGSP_CHANNEL_ENGINE_GRAPHICS) {
			if (oclass == chip->class_3d ||
			    oclass == chip->class_compute || oclass == chip->class_twod ||
			    oclass == chip->class_m2mf) {
				needs_gr_context = 1;
				error = 0;
			} else if (oclass == chip->class_copy) {
				error = 0;
			} else {
				error = EINVAL;
			}
		} else if (channel->engine_type == NVGSP_CHANNEL_ENGINE_COPY0) {
			error = oclass == chip->class_copy ? 0 : EINVAL;
		} else {
			error = EINVAL;
		}
		if (error == 0)
			error = nvgpu_channel_create_object(channel, nvif_object, handle,
			    oclass, needs_gr_context);
	}
	lwkt_reltoken(&proc->token);
	return (error);
}

int
nvgpu_proc_destroy_channel_object(struct nvgpu_proc *proc,
    uint64_t nvif_object)
{
	int error;

	if (proc == NULL)
		return (EINVAL);
	lwkt_gettoken(&proc->token);
	error = nvgpu_channel_destroy_object(&proc->channels, nvif_object);
	lwkt_reltoken(&proc->token);
	return (error);
}

int
nvgpu_proc_create_bo_handle(struct nvgpu_proc *proc, struct drm_file *file,
    const struct nvgpu_bo_create_args *args, struct nvgpu_bo_info *info)
{
	if (proc == NULL)
		return (EINVAL);
	return (nvgpu_bo_create_handle(proc->device, &proc->vm_resv, file,
	    args, info));
}

int
nvgpu_proc_remap(struct nvgpu_proc *proc, struct nvgpu_vm_remap_args *remap)
{
	struct nvgpu_vm *vm;
	int error;

	if (proc == NULL || remap == NULL)
		return (EINVAL);
	lwkt_gettoken(&proc->token);
	error = nvgpu_vm_ensure_backend(proc->device, &proc->vm, NULL);
	vm = proc->vm;
	lwkt_reltoken(&proc->token);
	if (error != 0)
		return (error);
	remap->proc = proc;
	return (nvgpu_vm_remap(vm, remap));
}

int
nvgpu_proc_spawn(struct nvgpu_proc *proc, struct nvgpu_proc_exec *args)
{
	struct nvgpu_exec_future *exec;
	struct nvgpu_proc_exec_record *record;
	struct nvgpu_fence **waits;
	struct nvgpu_fence *wait;
	size_t wait_count;
	int error;

	KTR_LOG(nvgpu_exec_spawn, proc, args != NULL ? args->channel_id : 0u,
	    args != NULL ? (uintmax_t)args->push_count : 0,
	    args != NULL ? (uintmax_t)args->wait_count : 0);
	if (proc == NULL || args == NULL || args->done == NULL ||
	    (args->push_count != 0 && args->pushes == NULL) ||
	    (args->wait_count != 0 && args->waits == NULL) ||
	    args->push_count > NVGPU_CHANNEL_GPFIFO_ENTRIES - 2)
		return (EINVAL);
	for (size_t i = 0; i < args->push_count; i++) {
		if ((args->pushes[i].flags & ~NVGPU_CHANNEL_PUSH_NO_PREFETCH) != 0 ||
		    ((args->pushes[i].va | args->pushes[i].va_len) & 3) != 0 ||
		    args->pushes[i].va_len == 0 ||
		    args->pushes[i].va_len >= (1u << 23))
			return (EINVAL);
	}
	for (size_t i = 0; i < args->wait_count; i++) {
		if (args->waits[i] == NULL)
			return (EINVAL);
	}

	exec = kmalloc(sizeof(*exec), M_NVGPU_EXEC, M_WAITOK | M_ZERO);
	if (args->push_count != 0) {
		exec->pushes = args->pushes;
		exec->push_count = args->push_count;
	}
	waits = NULL;
	if (args->wait_count != 0)
		waits = kmalloc(args->wait_count * sizeof(*waits),
		    M_NVGPU_EXEC, M_WAITOK | M_ZERO);
	exec->base.poll = nvgpu_proc_poll_exec;
	exec->proc = proc;
	exec->done = args->done;
	exec->submitted = nvgpu_fence_create();
	if (exec->submitted == NULL) {
		if (waits != NULL)
			_kfree(waits, M_NVGPU_EXEC);
		_kfree(exec, M_NVGPU_EXEC);
		return (ENOMEM);
	}
	exec->record.done = exec->done;
	exec->record.submitted = exec->submitted;
	exec->record.channel_id = args->channel_id;
	nvgpu_proc_addref(proc);
	nvgpu_fence_addref(exec->done);

	lwkt_gettoken(&proc->token);
	TAILQ_FOREACH(exec->channel, &proc->channels, link) {
		if (exec->channel->id == args->channel_id)
			break;
	}
	if (exec->channel == NULL) {
		lwkt_reltoken(&proc->token);
		error = ENOENT;
		goto fail;
	}
	wait_count = 0;
	for (size_t i = 0; i < args->wait_count; i++) {
		wait = args->waits[i];
		TAILQ_FOREACH(record, &proc->execs, link) {
			if (record->channel_id == args->channel_id &&
			    record->done == wait) {
				wait = record->submitted;
				break;
			}
		}
		waits[wait_count++] = wait;
	}
	nvgpu_channel_addref(exec->channel);
	TAILQ_INSERT_TAIL(&proc->execs, &exec->record, link);
	error = nvgpu_future_spawn(&exec->base, waits, wait_count);
	if (error != 0) {
		TAILQ_REMOVE(&proc->execs, &exec->record, link);
		nvgpu_channel_release(exec->channel);
	}
	lwkt_reltoken(&proc->token);
	if (waits != NULL)
		_kfree(waits, M_NVGPU_EXEC);
	waits = NULL;
	if (error == 0) {
		reservation_object_lock(&proc->vm_resv, NULL);
		reservation_object_add_excl_fence(&proc->vm_resv,
		    &args->done->dma);
		reservation_object_unlock(&proc->vm_resv);
		return (0);
	}

fail:
	(void)nvgpu_fence_signal(exec->submitted, error);
	(void)nvgpu_fence_signal(exec->done, error);
	nvgpu_fence_release(exec->submitted);
	nvgpu_fence_release(exec->done);
	nvgpu_proc_release(proc);
	if (waits != NULL)
		_kfree(waits, M_NVGPU_EXEC);
	_kfree(exec, M_NVGPU_EXEC);
	return (error);
}

static struct nvgpu_future_result
nvgpu_proc_poll_exec(struct nvgpu_future *future)
{
	struct nvgpu_channel_submit_args submit;
	struct nvgpu_exec_future *exec;
	struct nvgpu_proc *proc;
	int error;

	exec = (struct nvgpu_exec_future *)future;
	error = 0;
	KTR_LOG(nvgpu_exec_poll, future, exec->sema.address == NULL ? 0u : 1u, 0);
	if (exec->sema.address == NULL) {
		submit.pushes = exec->pushes;
		submit.push_count = exec->push_count;
		submit.submitted = exec->submitted;
		submit.sema = &exec->sema;
		error = nvgpu_channel_submit(exec->channel, &submit, future);
		KTR_LOG(nvgpu_exec_submit, future, exec->channel != NULL ? exec->channel->id : 0u,
		    (uintmax_t)exec->push_count, error);
		if (error == 0)
			return (NVGPU_FUTURE_PENDING);
		if (error == EAGAIN) {
			error = nvgpu_sched_put(future);
			if (error == 0)
				return (NVGPU_FUTURE_PENDING);
		}
		(void)nvgpu_fence_signal(exec->submitted, error);
	} else {
		error = exec->sema.error;
	}
	KTR_LOG(nvgpu_exec_complete, future, exec->channel != NULL ? exec->channel->id : 0u, error);

complete:
	proc = exec->proc;
	lwkt_gettoken(&proc->token);
	TAILQ_REMOVE(&proc->execs, &exec->record, link);
	lwkt_reltoken(&proc->token);
	nvgpu_channel_release(exec->channel);
	(void)nvgpu_fence_signal(exec->done, error);
	nvgpu_fence_release(exec->submitted);
	nvgpu_fence_release(exec->done);
	nvgpu_proc_release(proc);
	if (exec->pushes != NULL)
		_kfree(exec->pushes, M_TEMP);
	_kfree(exec, M_NVGPU_EXEC);
	return (NVGPU_FUTURE_READY(error));
}

static void
nvgpu_proc_finalize(struct nvgpu_proc *proc)
{
	struct nvgpu_channel *chan;

	KASSERT(TAILQ_EMPTY(&proc->execs),
	    ("finalizing proc with active EXEC futures"));
	while ((chan = TAILQ_FIRST(&proc->channels)) != NULL) {
		TAILQ_REMOVE(&proc->channels, chan, link);
        KASSERT(chan->refs == 1,
			("finalize proc while channel was still referenced"));
		nvgpu_channel_release(chan);
	}
	nvgpu_vm_destroy(proc->vm);
	proc->vm = NULL;
	reservation_object_fini(&proc->vm_resv);
	nvgpu_unload_release_by_drm(proc->device);
	nvgpu_log(NVGPU_LOG_DEBUG, "proc finalized proc=%p\n", proc);
	lwkt_token_uninit(&proc->token);
	_kfree(proc, M_NVGPU_PROC);
}
