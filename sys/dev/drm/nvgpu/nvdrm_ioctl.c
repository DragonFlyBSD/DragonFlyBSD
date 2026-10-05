/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Nouveau-compatible DRM ioctl dispatch table.
 */

#include "nvdrm_ioctl.h"
#include "nvdrm_nouveau_abi.h"
#include "nvdrm_file.h"
#include "nvgpu_syscall.h"

#include <drm/drmP.h>
#include <drm/drm_ioctl.h>

static int
nvdrm_ioctl_getparam(struct drm_device *ddev __unused, void *data,
    struct drm_file *file_priv)
{
	struct nvdrm_file *file;
	struct nvgpu_proc *proc;

	file = nvdrm_file_from_drm(file_priv);
	proc = nvdrm_file_get_proc(file);
	if (proc == NULL)
		return (ENXIO);
	return (nvgpu_syscall_getparam(proc, file_priv, data));
}

static int
nvdrm_ioctl_vm_init(struct drm_device *ddev __unused, void *data,
    struct drm_file *file_priv)
{
	struct nvdrm_file *file;
	struct nvgpu_proc *proc;

	file = nvdrm_file_from_drm(file_priv);
	proc = nvdrm_file_get_proc(file);
	if (proc == NULL)
		return (ENXIO);
	return (nvgpu_syscall_vm_init(proc, file_priv, data));
}

static int
nvdrm_ioctl_nvif(struct drm_device *ddev __unused, void *data,
    struct drm_file *file_priv, size_t data_size)
{
	struct nvdrm_file *file;
	struct nvgpu_proc *proc;

	file = nvdrm_file_from_drm(file_priv);
	proc = nvdrm_file_get_proc(file);
	if (proc == NULL)
		return (ENXIO);
	return (nvgpu_syscall_nvif(proc, file_priv, data, data_size));
}

static int
nvdrm_ioctl_channel_alloc(struct drm_device *ddev __unused, void *data,
    struct drm_file *file_priv)
{
	struct nvdrm_file *file;
	struct nvgpu_proc *proc;

	file = nvdrm_file_from_drm(file_priv);
	proc = nvdrm_file_get_proc(file);
	if (proc == NULL)
		return (ENXIO);
	return (nvgpu_syscall_channel_alloc(proc, file_priv, data));
}

static int
nvdrm_ioctl_channel_free(struct drm_device *ddev __unused, void *data,
    struct drm_file *file_priv)
{
	struct nvdrm_file *file;
	struct nvgpu_proc *proc;

	file = nvdrm_file_from_drm(file_priv);
	proc = nvdrm_file_get_proc(file);
	if (proc == NULL)
		return (ENXIO);
	return (nvgpu_syscall_channel_free(proc, file_priv, data));
}

static int
nvdrm_ioctl_gem_new(struct drm_device *ddev __unused, void *data,
    struct drm_file *file_priv)
{
	struct nvdrm_file *file;
	struct nvgpu_proc *proc;

	file = nvdrm_file_from_drm(file_priv);
	proc = nvdrm_file_get_proc(file);
	if (proc == NULL)
		return (ENXIO);
	return (nvgpu_syscall_gem_new(proc, file_priv, data));
}

static int
nvdrm_ioctl_gem_info(struct drm_device *ddev __unused, void *data,
    struct drm_file *file_priv)
{
	struct nvdrm_file *file;
	struct nvgpu_proc *proc;

	file = nvdrm_file_from_drm(file_priv);
	proc = nvdrm_file_get_proc(file);
	if (proc == NULL)
		return (ENXIO);
	return (nvgpu_syscall_gem_info(proc, file_priv, data));
}

static int
nvdrm_ioctl_gem_cpu_prep(struct drm_device *ddev __unused, void *data,
    struct drm_file *file_priv)
{
	struct nvdrm_file *file;
	struct nvgpu_proc *proc;

	file = nvdrm_file_from_drm(file_priv);
	proc = nvdrm_file_get_proc(file);
	if (proc == NULL)
		return (ENXIO);
	return (nvgpu_syscall_gem_cpu_prep(proc, file_priv, data));
}

static int
nvdrm_ioctl_gem_cpu_fini(struct drm_device *ddev __unused, void *data,
    struct drm_file *file_priv)
{
	struct nvdrm_file *file;
	struct nvgpu_proc *proc;

	file = nvdrm_file_from_drm(file_priv);
	proc = nvdrm_file_get_proc(file);
	if (proc == NULL)
		return (ENXIO);
	return (nvgpu_syscall_gem_cpu_fini(proc, file_priv, data));
}

static int
nvdrm_ioctl_vm_bind(struct drm_device *ddev __unused, void *data,
    struct drm_file *file_priv)
{
	struct nvdrm_file *file;
	struct nvgpu_proc *proc;

	file = nvdrm_file_from_drm(file_priv);
	proc = nvdrm_file_get_proc(file);
	if (proc == NULL)
		return (ENXIO);
	return (nvgpu_syscall_vm_bind(proc, file_priv, data));
}

static int
nvdrm_ioctl_exec(struct drm_device *ddev __unused, void *data,
    struct drm_file *file_priv)
{
	struct nvdrm_file *file;
	struct nvgpu_proc *proc;

	file = nvdrm_file_from_drm(file_priv);
	proc = nvdrm_file_get_proc(file);
	if (proc == NULL)
		return (ENXIO);
	return (nvgpu_syscall_exec(proc, file_priv, data));
}

const struct drm_ioctl_desc nvdrm_ioctl_descs[NVDRM_IOCTL_COUNT] = {
	DRM_IOCTL_DEF_DRV(NOUVEAU_GETPARAM, nvdrm_ioctl_getparam,
	    DRM_AUTH | DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(NOUVEAU_VM_INIT, nvdrm_ioctl_vm_init,
	    DRM_AUTH | DRM_RENDER_ALLOW),
	[DRM_IOCTL_NR(DRM_IOCTL_NOUVEAU_NVIF) - DRM_COMMAND_BASE] = {
		.cmd = DRM_IOCTL_NOUVEAU_NVIF,
		.func_ext = nvdrm_ioctl_nvif,
		.flags = DRM_AUTH | DRM_RENDER_ALLOW,
		.name = "NOUVEAU_NVIF"
	},
	DRM_IOCTL_DEF_DRV(NOUVEAU_CHANNEL_ALLOC, nvdrm_ioctl_channel_alloc,
	    DRM_AUTH | DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(NOUVEAU_CHANNEL_FREE, nvdrm_ioctl_channel_free,
	    DRM_AUTH | DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(NOUVEAU_GEM_NEW, nvdrm_ioctl_gem_new,
	    DRM_AUTH | DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(NOUVEAU_GEM_INFO, nvdrm_ioctl_gem_info,
	    DRM_AUTH | DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(NOUVEAU_GEM_CPU_PREP, nvdrm_ioctl_gem_cpu_prep,
	    DRM_AUTH | DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(NOUVEAU_GEM_CPU_FINI, nvdrm_ioctl_gem_cpu_fini,
	    DRM_AUTH | DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(NOUVEAU_VM_BIND, nvdrm_ioctl_vm_bind,
	    DRM_AUTH | DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(NOUVEAU_EXEC, nvdrm_ioctl_exec,
	    DRM_AUTH | DRM_RENDER_ALLOW),
};
