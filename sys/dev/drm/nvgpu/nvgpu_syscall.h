/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Nouveau syscall boundary for one GPU process.
 */

#ifndef _NVGPU_SYSCALL_H_
#define _NVGPU_SYSCALL_H_

#include <sys/types.h>

struct drm_file;
struct nvgpu_proc;

/*
 * Adapt one validated nouveau ioctl payload to native subsystem arguments.
 *
 * Every entry borrows proc, file, and the DRM-owned data buffer for the call.
 * Entries may sleep for copyin, object lookup, allocation, or synchronous
 * completion, and return positive DragonFly errno values.
 */
int nvgpu_syscall_getparam(struct nvgpu_proc *proc, struct drm_file *file,
    void *data);
int nvgpu_syscall_vm_init(struct nvgpu_proc *proc, struct drm_file *file,
    void *data);
int nvgpu_syscall_nvif(struct nvgpu_proc *proc, struct drm_file *file,
    void *data, size_t data_size);
int nvgpu_syscall_channel_alloc(struct nvgpu_proc *proc,
    struct drm_file *file, void *data);
int nvgpu_syscall_channel_free(struct nvgpu_proc *proc, struct drm_file *file,
    void *data);
int nvgpu_syscall_gem_new(struct nvgpu_proc *proc, struct drm_file *file,
    void *data);
int nvgpu_syscall_gem_info(struct nvgpu_proc *proc, struct drm_file *file,
    void *data);
int nvgpu_syscall_gem_cpu_prep(struct nvgpu_proc *proc, struct drm_file *file,
    void *data);
int nvgpu_syscall_gem_cpu_fini(struct nvgpu_proc *proc, struct drm_file *file,
    void *data);
int nvgpu_syscall_vm_bind(struct nvgpu_proc *proc, struct drm_file *file,
    void *data);
int nvgpu_syscall_exec(struct nvgpu_proc *proc, struct drm_file *file,
    void *data);

#endif /* _NVGPU_SYSCALL_H_ */
