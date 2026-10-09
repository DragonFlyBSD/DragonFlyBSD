/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Per-process GPU virtual address space owner.
 */

#ifndef _NVGPU_VM_H_
#define _NVGPU_VM_H_

#include <sys/types.h>

struct nvgpu_vm;
struct nvgpu_bo;
struct nvgpu_device;
struct nvgpu_fence;
struct nvgpu_proc;
struct nvgpu_vm_remap_args;
struct nvgsp_vmm;

struct nvgpu_vm_bind_op {
	uint32_t op;
	uint32_t flags;
	uint32_t handle;
	uint64_t addr;
	uint64_t bo_offset;
	uint64_t range;
	struct nvgpu_bo *bo;
};

/* Native VM remap request adapted by the syscall layer before entering proc. */
struct nvgpu_vm_remap_args {
	struct nvgpu_vm_bind_op *ops;
	size_t op_count;
	struct nvgpu_fence **waits;
	size_t wait_count;
	struct nvgpu_fence *done;
	struct nvgpu_proc *proc;
};

#define NVGPU_VM_BIND_OP_MAP	0u
#define NVGPU_VM_BIND_OP_UNMAP	1u
#define NVGPU_VM_BIND_SPARSE	(1u << 8)

/*
 * Store Mesa's kernel-managed VA window on the proc-owned VM.
 *
 * proc is borrowed.  The function serializes first-VM creation and may sleep
 * while allocating VM state.  It does not create the GSP VMM backend.
 */
int nvgpu_vm_init(struct nvgpu_device *device, struct nvgpu_vm **vmp,
	size_t addr, size_t size);

/*
 * Ensure the proc-owned VM and its GSP VMM backend exist.
 *
 * proc is borrowed.  On success vmm receives a borrowed backend pointer valid
 * while proc remains alive.  The function serializes first creation and may
 * sleep during allocation and GSP RPC.
 */
int nvgpu_vm_ensure_backend(struct nvgpu_device *device,
	struct nvgpu_vm **vmp, struct nvgsp_vmm **vmm);

/* Spawn one remap future against an explicitly borrowed proc-owned VM. */
int nvgpu_vm_remap(struct nvgpu_vm *vm, struct nvgpu_vm_remap_args *remap);

/*
 * Destroy the proc-owned VM after all channels and futures have drained.
 *
 * This finalization operation may sleep while releasing the GSP VMM.  No
 * concurrent proc operation is permitted, and proc remains caller-owned.
 */
void nvgpu_vm_destroy(struct nvgpu_vm *vm);

#endif /* _NVGPU_VM_H_ */
