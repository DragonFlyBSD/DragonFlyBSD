/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * DRM file boundary for one userspace open of the nouveau-compatible ABI.
 *
 * This file translates DRM open and postclose into nvgpu_proc lifetime events.
 */

#include "nvdrm_file.h"
#include "nvdrm_kms.h"
#include "nvgpu_debug.h"
#include "nvgpu_device.h"
#include "nvgpu_display.h"
#include "nvgpu_proc.h"
#include "nvgpu_unload.h"

#include <drm/drmP.h>

#include <sys/malloc.h>

static MALLOC_DEFINE(M_NVDRM_FILE, "nvdrm_file", "nvdrm per-open file state");

struct nvdrm_file {
	struct nvgpu_device *gpu;
	struct nvgpu_proc *proc;
};

/* Return the borrowed nvdrm_file from drm_file::driver_priv; no internal locking. */
struct nvdrm_file *
nvdrm_file_from_drm(struct drm_file *file)
{
	if (file == NULL)
		return (NULL);
	return (file->driver_priv);
}

/* Return the borrowed nvgpu_proc for a live nvdrm_file; no internal locking. */
struct nvgpu_proc *
nvdrm_file_get_proc(struct nvdrm_file *file)
{
	if (file == NULL)
		return (NULL);
	return (file->proc);
}

/* Return the borrowed physical GPU for a live nvdrm_file; no internal locking. */
struct nvgpu_device *
nvdrm_file_get_gpu(struct nvdrm_file *file)
{
	if (file == NULL)
		return (NULL);
	return (file->gpu);
}

/* DRM open callback.  Publishes one nvdrm_file only after proc LWKT startup. */
int
nvdrm_file_open(struct drm_device *ddev, struct drm_file *file_priv)
{
	struct nvgpu_device *gpu;
	struct nvdrm_file *file;
	struct nvgpu_proc *proc;
	int error;

	gpu = ddev->dev_private;
	error = nvgpu_unload_hold_by_drm(gpu);
	if (error != 0)
		return (error);

	file = kmalloc(sizeof(*file), M_NVDRM_FILE, M_WAITOK | M_ZERO);

	error = nvgpu_proc_create(gpu, &proc);
	if (error != 0) {
		_kfree(file, M_NVDRM_FILE);
		nvgpu_unload_release_by_drm(gpu);
		return (error);
	}

	file->gpu = gpu;
	file->proc = proc;
	file_priv->driver_priv = file;
	nvgpu_log(NVGPU_LOG_DEBUG, "DRM file opened gpu=%p proc=%p\n", gpu, proc);
	return (0);
}

/* DRM postclose callback.  Disconnect the file and release its proc owner. */
void
nvdrm_file_postclose(struct drm_device *ddev __unused, struct drm_file *file_priv)
{
	struct nvdrm_file *file;
	struct nvgpu_proc *proc;

	file = nvdrm_file_from_drm(file_priv);
	if (file == NULL)
		return;

	file_priv->driver_priv = NULL;
	proc = file->proc;
	file->proc = NULL;
	nvgpu_log(NVGPU_LOG_DEBUG, "DRM file postclose gpu=%p proc=%p\n", file->gpu, proc);
	nvgpu_proc_release(proc);
	_kfree(file, M_NVDRM_FILE);
}

/* DRM lastclose callback.  Restore the internal console before returning. */
void
nvdrm_file_lastclose(struct drm_device *ddev)
{
	struct nvgpu_device *gpu = ddev != NULL ? ddev->dev_private : NULL;
	int error;

	if (gpu == NULL)
		return;
	error = nvgpu_display_disable_primary(gpu, 0);
	if (error != 0)
		nvgpu_log(NVGPU_LOG_DEBUG,
		    "lastclose primary retire failed error=%d\n", error);
	error = nvdrm_kms_restore_console(gpu);
	if (error != 0)
		nvgpu_log(NVGPU_LOG_INFO,
		    "lastclose console restore failed error=%d\n", error);
}
