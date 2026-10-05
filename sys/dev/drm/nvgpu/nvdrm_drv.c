/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * DRM registration boundary for the native NVIDIA driver.
 */

#include "nvdrm_drv.h"
#include "nvdrm_file.h"
#include "nvdrm_ioctl.h"
#include "nvdrm_kms.h"
#include "nvdrm_prime.h"
#include "nvgpu_bo.h"
#include "nvgpu_device.h"
#include "nvgpu_debug.h"
#include "nvgpu_display.h"
#include "nvgpu_ttm.h"

#include <drm/drmP.h>
#include <drm/drm_drv.h>
#include <drm/drm_vblank.h>
#include <linux/err.h>
#include <sys/ktr.h>

#define NVDRM_DRM_NAME		"nouveau"
#define NVDRM_DRM_DESC		"nVidia Riva/TNT/GeForce (dfly native GSP-RM)"
#define NVDRM_DRM_DATE		"20260708"
#define NVDRM_DRM_MAJOR		1
#define NVDRM_DRM_MINOR		3
#define NVDRM_DRM_PATCH		1

#ifndef KTR_NVGPU
#define KTR_NVGPU KTR_ALL
#endif

KTR_INFO_MASTER_EXTERN(nvgpu);
KTR_INFO(KTR_NVGPU, nvgpu, drm_boot_stage, 18,
    "drm boot stage=%u error=%d", uint32_t stage, int error);

static const struct file_operations nvdrm_fops = {
	.owner = THIS_MODULE,
};

/*
 * Read TU102 head and raster state for precise DRM vblank timestamps.  This
 * callback runs in vblank accounting and only performs BAR0 reads; it must not
 * sleep or acquire an LWKT token.
 */
static bool
nvdrm_drv_get_scanout_position(struct drm_device *ddev, unsigned int pipe,
    bool in_vblank_irq, int *vpos, int *hpos, ktime_t *stime, ktime_t *etime,
    const struct drm_display_mode *mode)
{
	struct nvgpu_device *gpu = ddev->dev_private;
	uint32_t raster = pipe * 0x800u;
	uint32_t head = 0x8000u + pipe * 0x400u;
	int vtotal;
	int vblank_start;
	int vblank_end;
	int line;

	(void)in_vblank_irq;
	(void)mode;
	if (gpu == NULL)
		return (false);

	vtotal = (nvgpu_device_rd32(gpu, 0x682064u + head) >> 16) & 0xffff;
	vblank_end = (nvgpu_device_rd32(gpu, 0x68206cu + head) >> 16) & 0xffff;
	vblank_start = (nvgpu_device_rd32(gpu, 0x682070u + head) >> 16) & 0xffff;
	if (vtotal == 0)
		return (false);

	if (stime != NULL)
		*stime = ktime_get();
	/* Reading vline latches the matching hline register. */
	line = nvgpu_device_rd32(gpu, 0x616330u + raster) & 0xffff;
	*hpos = nvgpu_device_rd32(gpu, 0x616334u + raster) & 0xffff;
	if (etime != NULL)
		*etime = ktime_get();

	if (vblank_end >= vblank_start) {
		if (line >= vblank_start)
			line -= vtotal;
	} else {
		if (line >= vblank_start)
			line -= vtotal;
		line -= vblank_end + 1;
	}
	*vpos = line;
	return (true);
}

static struct drm_driver nvdrm_driver = {
	.driver_features = DRIVER_GEM | DRIVER_RENDER | DRIVER_SYNCOBJ |
	    DRIVER_SYNCOBJ_TIMELINE | DRIVER_PRIME | DRIVER_MODESET |
	    DRIVER_ATOMIC,
	.fops = &nvdrm_fops,
	.get_scanout_position = nvdrm_drv_get_scanout_position,
	.get_vblank_timestamp = drm_calc_vbltimestamp_from_scanoutpos,
	.name = NVDRM_DRM_NAME,
	.desc = NVDRM_DRM_DESC,
	.date = NVDRM_DRM_DATE,
	.major = NVDRM_DRM_MAJOR,
	.minor = NVDRM_DRM_MINOR,
	.patchlevel = NVDRM_DRM_PATCH,
	.ioctls = nvdrm_ioctl_descs,
	.num_ioctls = NVDRM_IOCTL_COUNT,
	.open = nvdrm_file_open,
	.postclose = nvdrm_file_postclose,
	.lastclose = nvdrm_file_lastclose,
	.mmap_single = nvgpu_ttm_mmap_single,
	.gem_free_object_unlocked = nvgpu_bo_release_by_gem,
	.dumb_create = nvdrm_kms_create_dumb,
	.dumb_map_offset = nvdrm_kms_get_dumb_map_offset,
	.dumb_destroy = nvdrm_kms_destroy_dumb,
	.prime_handle_to_fd = nvdrm_prime_handle_to_fd,
	.prime_fd_to_handle = nvdrm_prime_fd_to_handle,
	.gem_prime_export = nvdrm_prime_export,
	.gem_prime_res_obj = nvdrm_prime_get_resv,
	.gem_prime_import = drm_gem_prime_import,
};

/* Register DRM after GPU boot.  gpu is borrowed; may sleep and must not hold GSP/VM tokens. */
int
nvdrm_register(struct nvgpu_device *gpu)
{
	struct pci_dev *pdev = NULL;
	struct drm_device *ddev;
	int error;

	KTR_LOG(nvgpu_drm_boot_stage, 0u, 0);
	drm_init_pdev(nvgpu_device_get_newbus_dev(gpu), &pdev);
	if (pdev == NULL) {
		nvgpu_log(NVGPU_LOG_INFO, "drm_init_pdev failed\n");
		return (ENOMEM);
	}

	ddev = drm_dev_alloc(&nvdrm_driver, &pdev->dev);
	if (IS_ERR(ddev)) {
		error = -PTR_ERR(ddev);
		nvgpu_log(NVGPU_LOG_INFO, "drm_dev_alloc failed error=%d\n", error);
		drm_fini_pdev(&pdev);
		return (error);
	}

	ddev->dev_private = gpu;
	ddev->pdev = pdev;
	nvgpu_device_set_drm(gpu, ddev, pdev);

	error = nvgpu_ttm_init(gpu, ddev);
	KTR_LOG(nvgpu_drm_boot_stage, 1u, error);
	if (error != 0) {
		nvgpu_log(NVGPU_LOG_INFO, "nvgpu_ttm_init failed error=%d\n", error);
		nvgpu_ttm_fini(gpu);
		nvgpu_device_set_drm(gpu, NULL, NULL);
		if (ddev->sysctl != NULL)
			drm_sysctl_cleanup(ddev);
		drm_dev_put(ddev);
		drm_fini_pdev(&pdev);
		return (error);
	}
	error = nvgpu_display_init(gpu);
	KTR_LOG(nvgpu_drm_boot_stage, 2u, error);
	if (error != 0) {
		nvgpu_log(NVGPU_LOG_INFO, "nvgpu_display_init failed error=%d\n", error);
		nvgpu_ttm_fini(gpu);
		nvgpu_device_set_drm(gpu, NULL, NULL);
		if (ddev->sysctl != NULL)
			drm_sysctl_cleanup(ddev);
		drm_dev_put(ddev);
		drm_fini_pdev(&pdev);
		return (error);
	}
	error = nvdrm_kms_init(gpu);
	KTR_LOG(nvgpu_drm_boot_stage, 3u, error);
	if (error != 0) {
		nvgpu_log(NVGPU_LOG_INFO, "nvdrm_kms_init failed error=%d\n", error);
		nvgpu_display_fini(gpu);
		nvgpu_ttm_fini(gpu);
		nvgpu_device_set_drm(gpu, NULL, NULL);
		if (ddev->sysctl != NULL)
			drm_sysctl_cleanup(ddev);
		drm_dev_put(ddev);
		drm_fini_pdev(&pdev);
		return (error);
	}

	error = drm_dev_register(ddev, 0);
	KTR_LOG(nvgpu_drm_boot_stage, 4u, error);
	if (error != 0) {
		nvgpu_log(NVGPU_LOG_INFO, "drm_dev_register failed error=%d\n", error);
		nvdrm_kms_fini(gpu);
		nvgpu_display_fini(gpu);
		nvgpu_ttm_fini(gpu);
		nvgpu_device_set_drm(gpu, NULL, NULL);
		if (ddev->sysctl != NULL)
			drm_sysctl_cleanup(ddev);
		drm_dev_put(ddev);
		drm_fini_pdev(&pdev);
		return (error);
	}
	error = nvdrm_kms_restore_console(gpu);
	KTR_LOG(nvgpu_drm_boot_stage, 5u, error);
	if (error != 0)
		nvgpu_log(NVGPU_LOG_INFO,
		    "initial console modeset deferred error=%d\n", error);

	nvgpu_log(NVGPU_LOG_INFO, "drm registered as %s\n", NVDRM_DRM_NAME);
	return (0);
}

/* Unregister DRM before backend teardown.  gpu is borrowed; callers must have rejected new users. */
void
nvdrm_unregister(struct nvgpu_device *gpu)
{
	struct drm_device *ddev;
	struct pci_dev *pdev;

	ddev = nvgpu_device_get_drm_dev(gpu);
	pdev = nvgpu_device_get_drm_pdev(gpu);
	if (ddev != NULL) {
		drm_dev_unregister(ddev);
		nvdrm_kms_fini(gpu);
		nvgpu_display_fini(gpu);
		nvgpu_ttm_fini(gpu);
		/* drm_dev_fini() does not run DragonFly's per-device sysctl cleanup. */
		if (ddev->sysctl != NULL)
			drm_sysctl_cleanup(ddev);
		drm_dev_put(ddev);
	}
	drm_fini_pdev(&pdev);
	nvgpu_device_set_drm(gpu, NULL, NULL);
	nvgpu_log(NVGPU_LOG_DEBUG, "unregister\n");
}

struct drm_device *
nvdrm_get_device(struct nvgpu_device *gpu)
{
	return (nvgpu_device_get_drm_dev(gpu));
}
