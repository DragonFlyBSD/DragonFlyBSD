/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Per-open GPU process boundary.
 */

#ifndef _NVGPU_PROC_H_
#define _NVGPU_PROC_H_

#include <sys/stdint.h>
#include <sys/types.h>

struct drm_file;
struct nvgpu_bo_create_args;
struct nvgpu_bo_info;
struct nvgpu_channel_create_args;
struct nvgpu_channel_push;
struct nvgpu_device;
struct nvgpu_fence;
struct nvgpu_proc;
struct nvgpu_vm_bind_op;

/* Native EXEC request adapted by the syscall layer before entering proc. */
struct nvgpu_proc_exec {
	u_int channel_id;
	struct nvgpu_channel_push *pushes;
	size_t push_count;
	struct nvgpu_fence **waits;
	size_t wait_count;
	struct nvgpu_fence *done;
};

struct nvgpu_vm_remap_args;

/*
 * Create one GPU process bound to device and return its initial owned reference.
 * The DRM file owns that reference and releases it from postclose.
 */
int nvgpu_proc_create(struct nvgpu_device *device,
	struct nvgpu_proc **result);

/* Add one asynchronous owner of proc and all proc-owned GPU resources. */
void nvgpu_proc_addref(struct nvgpu_proc *proc);

/*
 * Consume one proc reference.
 *
 * The final release destroys channels, mappings, and VM state, then releases
 * the device/unload hold.  This function is MPSAFE.
 */
void nvgpu_proc_release(struct nvgpu_proc *proc);

/*
 * Store Mesa's kernel-managed VA window on the proc-owned VM.
 *
 * proc is borrowed.  The function serializes first-VM creation and may sleep
 * while allocating VM state.  It does not create the GSP VMM backend.
 */
int nvgpu_proc_init_vm(struct nvgpu_proc *proc, size_t addr, size_t size);

/* Create or release one channel owned by proc. */
int nvgpu_proc_create_channel(struct nvgpu_proc *proc,
	struct nvgpu_channel_create_args *args);
int nvgpu_proc_release_channel(struct nvgpu_proc *proc, int32_t channel_id);

/* Create or destroy one NVIF object owned by a proc channel. */
/*
 * Return classes supported by the channel selected by token.
 *
 * proc is borrowed.  classes has room for the maximum five nouveau classes
 * exposed by one channel.  This function holds the proc token internally.
 */
int nvgpu_proc_get_channel_classes(struct nvgpu_proc *proc, uint64_t token,
	uint32_t classes[5], uint32_t *count);
int nvgpu_proc_create_channel_object(struct nvgpu_proc *proc,
	uint64_t token, uint64_t nvif_object, uint32_t handle, uint32_t oclass);
int nvgpu_proc_destroy_channel_object(struct nvgpu_proc *proc,
	uint64_t nvif_object);

/* Create one GEM handle whose private reservation state belongs to proc. */
int nvgpu_proc_create_bo_handle(struct nvgpu_proc *proc,
	struct drm_file *file, const struct nvgpu_bo_create_args *args,
	struct nvgpu_bo_info *info);

/*
 * Create and spawn one EXEC future from a borrowed native request.
 *
 * On zero the future owns all references needed after return.  The wait array,
 * pushes, and done fence remain caller-owned.  Completion is reported through
 * done; a successful return does not mean GPU execution has completed.
 */
int nvgpu_proc_spawn(struct nvgpu_proc *proc,
	struct nvgpu_proc_exec *exec);

/*
 * Create and spawn one VM remap future from a borrowed native request.
 *
 * On zero the future retains its own proc, BO, and done-fence references.
 * The request, operation array, wait array, and fence pointers remain owned by
 * the caller.  Completion is reported through remap->done.
 */
int nvgpu_proc_remap(struct nvgpu_proc *proc,
	struct nvgpu_vm_remap_args *remap);

struct nvgpu_device *nvgpu_proc_get_device(struct nvgpu_proc *proc);

#endif /* _NVGPU_PROC_H_ */
