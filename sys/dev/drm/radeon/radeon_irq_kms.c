/*
 * Copyright 2008 Advanced Micro Devices, Inc.
 * Copyright 2008 Red Hat Inc.
 * Copyright 2009 Jerome Glisse.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: Dave Airlie
 *          Alex Deucher
 *          Jerome Glisse
 *
 * $FreeBSD: head/sys/dev/drm2/radeon/radeon_irq_kms.c 254885 2013-08-25 19:37:15Z dumbbell $
 */

#include <drm/drmP.h>
#include <drm/drm_crtc_helper.h>
#include <uapi_drm/radeon_drm.h>
#include "radeon_reg.h"
#include "radeon_irq_kms.h"
#include "radeon.h"
#include "atom.h"

#define RADEON_WAIT_IDLE_TIMEOUT 200

/**
 * radeon_driver_irq_handler_kms - irq handler for KMS
 *
 * @DRM_IRQ_ARGS: args
 *
 * This is the irq handler for the radeon KMS driver (all asics).
 * radeon_irq_process is a macro that points to the per-asic
 * irq handler callback.
 */
irqreturn_t radeon_driver_irq_handler_kms(DRM_IRQ_ARGS)
{
	struct drm_device *dev = (struct drm_device *) arg;
	struct radeon_device *rdev = dev->dev_private;

	return radeon_irq_process(rdev);
}

/*
 * Handle hotplug events outside the interrupt handler proper.
 */
/**
 * radeon_hotplug_work_func - display hotplug work handler
 *
 * @work: work struct
 *
 * This is the hot plug event work handler (all asics).
 * The work gets scheduled from the irq handler if there
 * was a hot plug interrupt.  It walks the connector table
 * and calls the hotplug handler for each one, then sends
 * a drm hotplug event to alert userspace.
 */
static void radeon_hotplug_work_func(void *arg, int pending)
{
	struct radeon_device *rdev = arg;
	struct drm_device *dev = rdev->ddev;
	struct drm_mode_config *mode_config = &dev->mode_config;
	struct drm_connector *connector;

	if (mode_config->num_connector) {
		list_for_each_entry(connector, &mode_config->connector_list, head)
			radeon_connector_hotplug(connector);
	}
	/* Just fire off a uevent and let userspace tell us what to do */
	drm_helper_hpd_irq_event(dev);
}

/**
 * radeon_irq_reset_work_func - execute gpu reset
 *
 * @work: work struct
 *
 * Execute scheduled gpu reset (cayman+).
 * This function is called when the irq handler
 * thinks we need a gpu reset.
 */
static void radeon_irq_reset_work_func(void *arg, int pending)
{
	struct radeon_device *rdev = arg;

	radeon_gpu_reset(rdev);
}

/**
 * radeon_driver_irq_preinstall_kms - drm irq preinstall callback
 *
 * @dev: drm dev pointer
 *
 * Gets the hw ready to enable irqs (all asics).
 * This function disables all interrupt sources on the GPU.
 */
void radeon_driver_irq_preinstall_kms(struct drm_device *dev)
{
	struct radeon_device *rdev = dev->dev_private;
	unsigned i;

	lockmgr(&rdev->irq.lock, LK_EXCLUSIVE);
	/* Disable *all* interrupts */
	for (i = 0; i < RADEON_NUM_RINGS; i++)
		atomic_set(&rdev->irq.ring_int[i], 0);
	rdev->irq.dpm_thermal = false;
	for (i = 0; i < RADEON_MAX_HPD_PINS; i++)
		rdev->irq.hpd[i] = false;
	for (i = 0; i < RADEON_MAX_CRTCS; i++) {
		rdev->irq.crtc_vblank_int[i] = false;
		atomic_set(&rdev->irq.pflip[i], 0);
		rdev->irq.afmt[i] = false;
	}
	radeon_irq_set(rdev);
	lockmgr(&rdev->irq.lock, LK_RELEASE);
	/* Clear bits */
	radeon_irq_process(rdev);
}

/**
 * radeon_driver_irq_postinstall_kms - drm irq preinstall callback
 *
 * @dev: drm dev pointer
 *
 * Handles stuff to be done after enabling irqs (all asics).
 * Returns 0 on success.
 */
int radeon_driver_irq_postinstall_kms(struct drm_device *dev)
{
	dev->max_vblank_count = 0x001fffff;
	return 0;
}

/**
 * radeon_driver_irq_uninstall_kms - drm irq uninstall callback
 *
 * @dev: drm dev pointer
 *
 * This function disables all interrupt sources on the GPU (all asics).
 */
void radeon_driver_irq_uninstall_kms(struct drm_device *dev)
{
	struct radeon_device *rdev = dev->dev_private;
	unsigned i;

	if (rdev == NULL) {
		return;
	}
	lockmgr(&rdev->irq.lock, LK_EXCLUSIVE);
	/* Disable *all* interrupts */
	for (i = 0; i < RADEON_NUM_RINGS; i++)
		atomic_set(&rdev->irq.ring_int[i], 0);
	rdev->irq.dpm_thermal = false;
	for (i = 0; i < RADEON_MAX_HPD_PINS; i++)
		rdev->irq.hpd[i] = false;
	for (i = 0; i < RADEON_MAX_CRTCS; i++) {
		rdev->irq.crtc_vblank_int[i] = false;
		atomic_set(&rdev->irq.pflip[i], 0);
		rdev->irq.afmt[i] = false;
	}
	radeon_irq_set(rdev);
	lockmgr(&rdev->irq.lock, LK_RELEASE);
}

/**
 * radeon_msi_ok - asic specific msi checks
 *
 * @rdev: radeon device pointer
 *
 * Handles asic specific MSI checks to determine if
 * MSIs should be enabled on a particular chip (all asics).
 * Returns true if MSIs should be enabled, false if MSIs
 * should not be enabled.
 */
int radeon_msi_ok(struct drm_device *dev, unsigned long flags)
{
	int family;

	family = flags & RADEON_FAMILY_MASK;

	/* RV370/RV380 was first asic with MSI support */
	if (family < CHIP_RV380)
		return false;

	/* MSIs don't work on AGP */
	if (drm_device_is_agp(dev))
		return false;

	/* force MSI on */
	if (radeon_msi == 1)
		return true;
	else if (radeon_msi == 0)
		return false;

	/* Quirks */
	/* HP RS690 only seems to work with MSIs. */
	if ((dev->pci_device == 0x791f) &&
	    (dev->pci_subvendor == 0x103c) &&
	    (dev->pci_subdevice == 0x30c2))
		return true;

	/* Dell RS690 only seems to work with MSIs. */
	if ((dev->pci_device == 0x791f) &&
	    (dev->pci_subvendor == 0x1028) &&
	    (dev->pci_subdevice == 0x01fc))
		return true;

	/* Dell RS690 only seems to work with MSIs. */
	if ((dev->pci_device == 0x791f) &&
	    (dev->pci_subvendor == 0x1028) &&
	    (dev->pci_subdevice == 0x01fd))
		return true;

	/* Gateway RS690 only seems to work with MSIs. */
	if ((dev->pci_device == 0x791f) &&
	    (dev->pci_subvendor == 0x107b) &&
	    (dev->pci_subdevice == 0x0185))
		return true;

	/* try and enable MSIs by default on all RS690s */
	if (family == CHIP_RS690)
		return true;

	/* RV515 seems to have MSI issues where it loses
	 * MSI rearms occasionally. This leads to lockups and freezes.
	 * disable it by default.
	 */
	if (family == CHIP_RV515)
		return false;
	if (flags & RADEON_IS_IGP) {
		/* APUs work fine with MSIs */
		if (family >= CHIP_PALM)
			return true;
		/* lots of IGPs have problems with MSIs */
		return false;
	}

	return true;
}

/**
 * radeon_irq_kms_init - init driver interrupt info
 *
 * @rdev: radeon device pointer
 *
 * Sets up the work irq handlers, vblank init, MSIs, etc. (all asics).
 * Returns 0 for success, error for failure.
 */
int radeon_irq_kms_init(struct radeon_device *rdev)
{
	int r = 0;


	lockinit(&rdev->irq.lock, "drm__radeon_device__irq__lock", 0, LK_CANRECURSE);
	r = drm_vblank_init(rdev->ddev, rdev->num_crtc);
	if (r) {
		return r;
	}
	/* enable msi */
	rdev->msi_enabled = (rdev->ddev->irq_type == PCI_INTR_TYPE_MSI);

	rdev->irq.installed = true;
	DRM_UNLOCK(rdev->ddev);
	r = drm_irq_install(rdev->ddev);
	DRM_LOCK(rdev->ddev);
	if (r) {
		rdev->irq.installed = false;
		return r;
	}

	TASK_INIT(&rdev->hotplug_work, 0, radeon_hotplug_work_func, rdev);
	TASK_INIT(&rdev->audio_work, 0, r600_audio_update_hdmi, rdev);
	TASK_INIT(&rdev->reset_work, 0, radeon_irq_reset_work_func, rdev);

	DRM_INFO("radeon: irq initialized.\n");
	return 0;
}

/**
 * radeon_irq_kms_fini - tear down driver interrupt info
 *
 * @rdev: radeon device pointer
 *
 * Tears down the work irq handlers, vblank handlers, MSIs, etc. (all asics).
 */
void radeon_irq_kms_fini(struct radeon_device *rdev)
{
	drm_vblank_cleanup(rdev->ddev);
	if (rdev->irq.installed) {
		drm_irq_uninstall(rdev->ddev);
		rdev->irq.installed = false;
	}
	taskqueue_drain(rdev->tq, &rdev->hotplug_work);
}

/**
 * radeon_irq_kms_sw_irq_get - enable software interrupt
 *
 * @rdev: radeon device pointer
 * @ring: ring whose interrupt you want to enable
 *
 * Enables the software interrupt for a specific ring (all asics).
 * The software interrupt is generally used to signal a fence on
 * a particular ring.
 */
void radeon_irq_kms_sw_irq_get(struct radeon_device *rdev, int ring)
{
	if (!rdev->ddev->irq_enabled)
		return;

	if (atomic_inc_return(&rdev->irq.ring_int[ring]) == 1) {
		lockmgr(&rdev->irq.lock, LK_EXCLUSIVE);
		radeon_irq_set(rdev);
		lockmgr(&rdev->irq.lock, LK_RELEASE);
	}
}

/**
 * radeon_irq_kms_sw_irq_put - disable software interrupt
 *
 * @rdev: radeon device pointer
 * @ring: ring whose interrupt you want to disable
 *
 * Disables the software interrupt for a specific ring (all asics).
 * The software interrupt is generally used to signal a fence on
 * a particular ring.
 */
void radeon_irq_kms_sw_irq_put(struct radeon_device *rdev, int ring)
{
	if (!rdev->ddev->irq_enabled)
		return;

	if (atomic_dec_and_test(&rdev->irq.ring_int[ring])) {
		lockmgr(&rdev->irq.lock, LK_EXCLUSIVE);
		radeon_irq_set(rdev);
		lockmgr(&rdev->irq.lock, LK_RELEASE);
	}
}

/**
 * radeon_irq_kms_pflip_irq_get - enable pageflip interrupt
 *
 * @rdev: radeon device pointer
 * @crtc: crtc whose interrupt you want to enable
 *
 * Enables the pageflip interrupt for a specific crtc (all asics).
 * For pageflips we use the vblank interrupt source.
 */
void radeon_irq_kms_pflip_irq_get(struct radeon_device *rdev, int crtc)
{
	if (crtc < 0 || crtc >= rdev->num_crtc)
		return;

	if (!rdev->ddev->irq_enabled)
		return;

	if (atomic_inc_return(&rdev->irq.pflip[crtc]) == 1) {
		lockmgr(&rdev->irq.lock, LK_EXCLUSIVE);
		radeon_irq_set(rdev);
		lockmgr(&rdev->irq.lock, LK_RELEASE);
	}
}

/**
 * radeon_irq_kms_pflip_irq_put - disable pageflip interrupt
 *
 * @rdev: radeon device pointer
 * @crtc: crtc whose interrupt you want to disable
 *
 * Disables the pageflip interrupt for a specific crtc (all asics).
 * For pageflips we use the vblank interrupt source.
 */
void radeon_irq_kms_pflip_irq_put(struct radeon_device *rdev, int crtc)
{
	if (crtc < 0 || crtc >= rdev->num_crtc)
		return;

	if (!rdev->ddev->irq_enabled)
		return;

	if (atomic_dec_and_test(&rdev->irq.pflip[crtc])) {
		lockmgr(&rdev->irq.lock, LK_EXCLUSIVE);
		radeon_irq_set(rdev);
		lockmgr(&rdev->irq.lock, LK_RELEASE);
	}
}

/**
 * radeon_irq_kms_enable_afmt - enable audio format change interrupt
 *
 * @rdev: radeon device pointer
 * @block: afmt block whose interrupt you want to enable
 *
 * Enables the afmt change interrupt for a specific afmt block (all asics).
 */
void radeon_irq_kms_enable_afmt(struct radeon_device *rdev, int block)
{
	if (!rdev->ddev->irq_enabled)
		return;

	lockmgr(&rdev->irq.lock, LK_EXCLUSIVE);
	rdev->irq.afmt[block] = true;
	radeon_irq_set(rdev);
	lockmgr(&rdev->irq.lock, LK_RELEASE);
}

/**
 * radeon_irq_kms_disable_afmt - disable audio format change interrupt
 *
 * @rdev: radeon device pointer
 * @block: afmt block whose interrupt you want to disable
 *
 * Disables the afmt change interrupt for a specific afmt block (all asics).
 */
void radeon_irq_kms_disable_afmt(struct radeon_device *rdev, int block)
{
	if (!rdev->ddev->irq_enabled)
		return;

	lockmgr(&rdev->irq.lock, LK_EXCLUSIVE);
	rdev->irq.afmt[block] = false;
	radeon_irq_set(rdev);
	lockmgr(&rdev->irq.lock, LK_RELEASE);
}

/**
 * radeon_irq_kms_enable_hpd - enable hotplug detect interrupt
 *
 * @rdev: radeon device pointer
 * @hpd_mask: mask of hpd pins you want to enable.
 *
 * Enables the hotplug detect interrupt for a specific hpd pin (all asics).
 */
void radeon_irq_kms_enable_hpd(struct radeon_device *rdev, unsigned hpd_mask)
{
	int i;

	if (!rdev->ddev->irq_enabled)
		return;

	lockmgr(&rdev->irq.lock, LK_EXCLUSIVE);
	for (i = 0; i < RADEON_MAX_HPD_PINS; ++i)
		rdev->irq.hpd[i] |= !!(hpd_mask & (1 << i));
	radeon_irq_set(rdev);
	lockmgr(&rdev->irq.lock, LK_RELEASE);
}

/**
 * radeon_irq_kms_disable_hpd - disable hotplug detect interrupt
 *
 * @rdev: radeon device pointer
 * @hpd_mask: mask of hpd pins you want to disable.
 *
 * Disables the hotplug detect interrupt for a specific hpd pin (all asics).
 */
void radeon_irq_kms_disable_hpd(struct radeon_device *rdev, unsigned hpd_mask)
{
	int i;

	if (!rdev->ddev->irq_enabled)
		return;

	lockmgr(&rdev->irq.lock, LK_EXCLUSIVE);
	for (i = 0; i < RADEON_MAX_HPD_PINS; ++i)
		rdev->irq.hpd[i] &= !(hpd_mask & (1 << i));
	radeon_irq_set(rdev);
	lockmgr(&rdev->irq.lock, LK_RELEASE);
}

