/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Nouveau syscall boundary for one GPU process.
 */

#include "nvgpu_syscall.h"
#include "nvdrm_nouveau_abi.h"
#include "nvdrm_sync.h"
#include "nvgpu_bo.h"
#include "nvgpu_channel.h"
#include "nvgpu_debug.h"
#include "nvgpu_fence.h"
#include "nvgpu_info.h"
#include "nvgpu_nvif.h"
#include "nvgpu_vm.h"
#include "nvgpu_proc.h"

#include <sys/errno.h>
#include <sys/malloc.h>
#include <sys/systm.h>
#include <machine/cpufunc.h>


int
nvgpu_syscall_getparam(struct nvgpu_proc *proc, struct drm_file *file __unused,
    void *data)
{
	struct drm_nouveau_getparam *req = data;

	return (nvgpu_info_get_param(proc, req->param, &req->value));
}

int
nvgpu_syscall_vm_init(struct nvgpu_proc *proc, struct drm_file *file __unused,
    void *data)
{
	struct drm_nouveau_vm_init *req = data;

	return (nvgpu_proc_init_vm(proc, req->kernel_managed_addr,
	    req->kernel_managed_size));
}

int
nvgpu_syscall_nvif(struct nvgpu_proc *proc, struct drm_file *file __unused,
    void *data, size_t data_size)
{
	return (nvgpu_nvif_ioctl(proc, data, data_size));
}
int
nvgpu_syscall_channel_alloc(struct nvgpu_proc *proc,
    struct drm_file *file __unused, void *data)
{
	struct drm_nouveau_channel_alloc *req = data;
	struct nvgpu_channel_create_args args;
	int error;

	memset(&args, 0, sizeof(args));
	args.fb_ctxdma_handle = req->fb_ctxdma_handle;
	args.tt_ctxdma_handle = req->tt_ctxdma_handle;
	error = nvgpu_proc_create_channel(proc, &args);
	if (error != 0)
		return (error);
	req->channel = args.channel_id;
	req->pushbuf_domains = args.pushbuf_domains;
	req->notifier_handle = args.notifier_handle;
	req->nr_subchan = args.nr_subchan;
	return (0);
}

int
nvgpu_syscall_channel_free(struct nvgpu_proc *proc,
    struct drm_file *file __unused, void *data)
{
	struct drm_nouveau_channel_free *req = data;

	return (nvgpu_proc_release_channel(proc, req->channel));
}

int
nvgpu_syscall_gem_new(struct nvgpu_proc *proc, struct drm_file *file,
    void *data)
{
	struct drm_nouveau_gem_new *req = data;
	struct nvgpu_bo_create_args args;
	struct nvgpu_bo_info info;
	int error;

	args.size = req->info.size;
	args.domain = req->info.domain;
	args.align = req->align;
	args.tile_mode = req->info.tile_mode;
	args.tile_flags = req->info.tile_flags;
	error = nvgpu_proc_create_bo_handle(proc, file, &args, &info);
	if (error != 0)
		return (error);
	req->info.handle = info.handle;
	req->info.domain = info.domain;
	req->info.size = info.size;
	req->info.offset = info.offset;
	req->info.map_handle = info.map_handle;
	req->info.tile_mode = info.tile_mode;
	req->info.tile_flags = info.tile_flags;
	return (0);
}

int
nvgpu_syscall_gem_info(struct nvgpu_proc *proc __unused,
    struct drm_file *file, void *data)
{
	struct drm_nouveau_gem_info *req = data;
	struct nvgpu_bo_info info;
	int error;

	error = nvgpu_bo_get_info(file, req->handle, &info);
	if (error != 0)
		return (error);
	req->domain = info.domain;
	req->size = info.size;
	req->offset = info.offset;
	req->map_handle = info.map_handle;
	req->tile_mode = info.tile_mode;
	req->tile_flags = info.tile_flags;
	return (0);
}

int
nvgpu_syscall_gem_cpu_prep(struct nvgpu_proc *proc __unused,
    struct drm_file *file, void *data)
{
	struct drm_nouveau_gem_cpu_prep *req = data;
	struct nvgpu_bo *bo;
	int error;

	if (req == NULL)
		return (EINVAL);
	error = nvgpu_bo_lookup(file, req->handle, &bo);
	if (error != 0)
		return (error);
	error = nvgpu_bo_resv_wait(bo, true,
	    (req->flags & NOUVEAU_GEM_CPU_PREP_WRITE) != 0,
	    (req->flags & NOUVEAU_GEM_CPU_PREP_NOWAIT) != 0);
	nvgpu_bo_release(bo);
	return (error);
}

int
nvgpu_syscall_gem_cpu_fini(struct nvgpu_proc *proc __unused,
    struct drm_file *file, void *data)
{
	struct drm_nouveau_gem_cpu_fini *req = data;
	struct nvgpu_bo *bo;
	int error;

	if (req == NULL)
		return (EINVAL);
	error = nvgpu_bo_lookup(file, req->handle, &bo);
	if (error != 0)
		return (error);
	cpu_sfence();
	nvgpu_bo_release(bo);
	return (0);
}

int
nvgpu_syscall_vm_bind(struct nvgpu_proc *proc, struct drm_file *file,
    void *data)
{
	struct drm_nouveau_vm_bind *req = data;
	struct drm_nouveau_vm_bind_op *ops;
	struct nvgpu_vm_bind_op *vm_ops;
	struct nvdrm_sync_wait_set waits;
	struct nvdrm_sync_signal_set signals;
	struct nvgpu_vm_remap_args remap;
	struct nvgpu_fence *done_fence;
	struct nvgpu_fence *sync_fence;
	size_t size;
	bool async;
	int error;

	if (req == NULL)
		return (EINVAL);
	if ((req->flags & ~DRM_NOUVEAU_VM_BIND_RUN_ASYNC) != 0 ||
	    req->op_count > 1024)
		return (EINVAL);
	async = (req->flags & DRM_NOUVEAU_VM_BIND_RUN_ASYNC) != 0;
	if (!async && (req->wait_count != 0 || req->sig_count != 0))
		return (EINVAL);

	ops = NULL;
	vm_ops = NULL;
	if (req->op_count != 0) {
		size = sizeof(*ops) * req->op_count;
		ops = kmalloc(size, M_TEMP, M_WAITOK | M_ZERO);
		vm_ops = kmalloc(sizeof(*vm_ops) * req->op_count, M_TEMP,
		    M_WAITOK | M_ZERO);
		error = copyin((const void *)(uintptr_t)req->op_ptr, ops, size);
		if (error != 0) {
			error = EFAULT;
			goto cleanup_ops;
		}
	}
	for (uint32_t i = 0; i < req->op_count; i++) {
		if (ops[i].pad != 0 ||
		    (ops[i].flags & ~(0xffu | DRM_NOUVEAU_VM_BIND_SPARSE)) != 0 ||
		    ops[i].range == 0 ||
		    ops[i].addr > UINT64_MAX - ops[i].range ||
		    ((ops[i].addr | ops[i].bo_offset | ops[i].range) &
		    (PAGE_SIZE - 1)) != 0 ||
		    (ops[i].op != DRM_NOUVEAU_VM_BIND_OP_MAP &&
		    ops[i].op != DRM_NOUVEAU_VM_BIND_OP_UNMAP)) {
			error = EINVAL;
			goto cleanup_ops;
		}
		vm_ops[i].op = ops[i].op;
		vm_ops[i].flags = ops[i].flags;
		vm_ops[i].handle = ops[i].handle;
		vm_ops[i].addr = ops[i].addr;
		vm_ops[i].bo_offset = ops[i].bo_offset;
		vm_ops[i].range = ops[i].range;
		if (ops[i].op == DRM_NOUVEAU_VM_BIND_OP_MAP &&
		    (ops[i].flags & DRM_NOUVEAU_VM_BIND_SPARSE) == 0 &&
		    ops[i].handle != 0) {
			error = nvgpu_bo_lookup(file, ops[i].handle, &vm_ops[i].bo);
			if (error != 0)
				goto cleanup_ops;
			if (ops[i].bo_offset > nvgpu_bo_get_size(vm_ops[i].bo) ||
			    ops[i].range > nvgpu_bo_get_size(vm_ops[i].bo) -
			    ops[i].bo_offset) {
				error = EINVAL;
				goto cleanup_ops;
			}
		}
	}

	error = nvdrm_sync_collect_wait_fences(file, req->wait_count,
	    req->wait_ptr, &waits);
	if (error != 0)
		goto cleanup_ops;
	done_fence = nvgpu_fence_create();
	if (done_fence == NULL) {
		error = ENOMEM;
		goto cleanup_waits;
	}
	error = nvdrm_sync_prepare_signals(file, req->sig_count, req->sig_ptr,
	    done_fence, &signals);
	if (error != 0) {
		nvgpu_fence_release(done_fence);
		goto cleanup_waits;
	}
	sync_fence = NULL;
	if (!async) {
		nvgpu_fence_addref(done_fence);
		sync_fence = done_fence;
	}
	remap.ops = vm_ops;
	remap.op_count = req->op_count;
	remap.done = done_fence;
	remap.waits = waits.fences;
	remap.wait_count = waits.count;
	error = nvgpu_proc_remap(proc, &remap);
	if (error == 0)
		nvdrm_sync_publish_signals(&signals);
	nvdrm_sync_cleanup_signals(&signals);
	nvgpu_fence_release(done_fence);
	if (error == 0 && sync_fence != NULL)
		error = nvdrm_sync_wait_fence(sync_fence);
	nvgpu_fence_release(sync_fence);

cleanup_waits:
	nvdrm_sync_cleanup_waits(&waits);
cleanup_ops:
	if (vm_ops != NULL) {
		for (uint32_t i = 0; i < req->op_count; i++)
			nvgpu_bo_release(vm_ops[i].bo);
	}
	if (vm_ops != NULL)
		_kfree(vm_ops, M_TEMP);
	if (ops != NULL)
		_kfree(ops, M_TEMP);
	return (error);
}

/* Copy the EXEC UAPI payload and spawn a real GPU submission future. */
int
nvgpu_syscall_exec(struct nvgpu_proc *proc, struct drm_file *file,
    void *data)
{
	struct drm_nouveau_exec *req = data;
	struct nvdrm_sync_wait_set waits;
	struct nvdrm_sync_signal_set signals;
	struct nvgpu_proc_exec exec;
	struct nvgpu_channel_push *pushes;
	struct nvgpu_fence *done;
	int error;

	if (req == NULL)
		return (EINVAL);
	if (req->push_count > NVGPU_CHANNEL_GPFIFO_ENTRIES - 2)
		return (EINVAL);
	pushes = NULL;
	if (req->push_count != 0) {
		pushes = kmalloc((size_t)req->push_count * sizeof(*pushes),
		    M_TEMP, M_WAITOK);
		error = copyin((const void *)(uintptr_t)req->push_ptr, pushes,
		    (size_t)req->push_count * sizeof(*pushes));
		if (error != 0) {
			_kfree(pushes, M_TEMP);
			return (error);
		}
	}
	error = nvdrm_sync_collect_wait_fences(file, req->wait_count,
	    req->wait_ptr, &waits);
	if (error != 0) {
		if (pushes != NULL)
			_kfree(pushes, M_TEMP);
		return (error);
	}
	done = nvgpu_fence_create();
	if (done == NULL) {
		error = ENOMEM;
		goto cleanup_waits;
	}
	error = nvdrm_sync_prepare_signals(file, req->sig_count, req->sig_ptr,
	    done, &signals);
	if (error != 0) {
		nvgpu_fence_release(done);
		goto cleanup_waits;
	}

	exec.channel_id = req->channel_id;
	exec.pushes = pushes;
	exec.push_count = req->push_count;
	exec.waits = waits.fences;
	exec.wait_count = waits.count;
	exec.done = done;
	error = nvgpu_proc_spawn(proc, &exec);
	if (error == 0) {
		nvdrm_sync_publish_signals(&signals);
		pushes = NULL;
	}
	nvdrm_sync_cleanup_signals(&signals);
	nvgpu_fence_release(done);

cleanup_waits:
	nvdrm_sync_cleanup_waits(&waits);
	if (pushes != NULL)
		_kfree(pushes, M_TEMP);
	return (error);
}
