/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * DRM file boundary for one userspace open of the nouveau-compatible ABI.
 */

#ifndef _NVDRM_FILE_H_
#define _NVDRM_FILE_H_

struct drm_device;
struct drm_file;
struct nvdrm_file;
struct nvgpu_device;
struct nvgpu_proc;

/*
 * DRM-private state for one open file.
 * drm_file::driver_priv owns it; runtime GPU state lives in nvgpu_proc.
 */
struct nvdrm_file;

/*
 * DRM open callback.
 * Acquires the unload hold and publishes driver_priv only after proc creation.
 */
int nvdrm_file_open(struct drm_device *ddev, struct drm_file *file);

/*
 * DRM postclose callback.
 * Clears driver_priv and releases the file's initial proc reference.
 */
void nvdrm_file_postclose(struct drm_device *ddev, struct drm_file *file);

/* DRM lastclose callback.  Runs only DRM-visible final-close policy. */
void nvdrm_file_lastclose(struct drm_device *ddev);

/* Return the borrowed nvdrm_file from drm_file::driver_priv; no internal locking. */
struct nvdrm_file *nvdrm_file_from_drm(struct drm_file *file);

/* Return the borrowed nvgpu_proc for a live nvdrm_file; no internal locking. */
struct nvgpu_proc *nvdrm_file_get_proc(struct nvdrm_file *file);

/* Return the borrowed physical GPU for a live nvdrm_file; no internal locking. */
struct nvgpu_device *nvdrm_file_get_gpu(struct nvdrm_file *file);

#endif /* _NVDRM_FILE_H_ */
