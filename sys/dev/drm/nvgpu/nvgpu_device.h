/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Physical GPU lifetime boundary for the native NVIDIA driver.
 */

#ifndef _NVGPU_DEVICE_H_
#define _NVGPU_DEVICE_H_

#include <sys/bus.h>
#include <sys/stdint.h>

struct nvgsp_state;
struct nvdrm_kms;
struct nvgpu_chip_config;
struct nvgpu_display;
struct nvgpu_intr_state;
struct nvgpu_ttm;
struct nvgpu_unload_state;
struct drm_device;
struct pci_dev;
struct resource;

/*
 * Opaque physical GPU root.
 * PCI attach owns the object; subsystems borrow it while detach is excluded.
 */
struct nvgpu_device;

/* Return gpu's borrowed device_t, or NULL when gpu is NULL. */
device_t nvgpu_device_get_newbus_dev(struct nvgpu_device *gpu);

/* Return borrowed immutable chip metadata for gpu. */
const struct nvgpu_chip_config *nvgpu_device_get_chip(struct nvgpu_device *gpu);

/* Return the borrowed PCI device display name chosen during probe. */
const char *nvgpu_device_get_name(struct nvgpu_device *gpu);

/* Return a borrowed BAR resource, or NULL when the BAR is not mapped. */
struct resource *nvgpu_device_get_bar(struct nvgpu_device *gpu, unsigned int bar);

/* Read or write one BAR0 MMIO register after device boot mapped the BAR. */
uint32_t nvgpu_device_rd32(struct nvgpu_device *gpu, uint32_t offset);
void nvgpu_device_wr32(struct nvgpu_device *gpu, uint32_t offset, uint32_t val);

/* Get or publish the device-owned GSP state without changing its lifetime. */
struct nvgsp_state *nvgpu_device_get_gsp(struct nvgpu_device *gpu);
void nvgpu_device_set_gsp(struct nvgpu_device *gpu, struct nvgsp_state *gsp);

/* Get or publish the device-owned interrupt state without taking a reference. */
struct nvgpu_intr_state *nvgpu_device_get_intr(struct nvgpu_device *gpu);
void nvgpu_device_set_intr(struct nvgpu_device *gpu,
    struct nvgpu_intr_state *intr);

/* Return borrowed DRM objects while DRM registration remains live. */
struct drm_device *nvgpu_device_get_drm_dev(struct nvgpu_device *gpu);
struct pci_dev *nvgpu_device_get_drm_pdev(struct nvgpu_device *gpu);

/* Publish or clear both DRM objects as one device-lifecycle transition. */
void nvgpu_device_set_drm(struct nvgpu_device *gpu, struct drm_device *ddev,
    struct pci_dev *pdev);

/* Get or publish device-owned unload admission state. */
struct nvgpu_unload_state *nvgpu_device_get_unload_state(
    struct nvgpu_device *gpu);
void nvgpu_device_set_unload_state(struct nvgpu_device *gpu,
    struct nvgpu_unload_state *state);

/* Get or publish device-owned TTM state. */
struct nvgpu_ttm *nvgpu_device_get_ttm(struct nvgpu_device *gpu);
void nvgpu_device_set_ttm(struct nvgpu_device *gpu, struct nvgpu_ttm *ttm);

/* Get or publish device-owned display state. */
struct nvgpu_display *nvgpu_device_get_display(struct nvgpu_device *gpu);
void nvgpu_device_set_display(struct nvgpu_device *gpu,
    struct nvgpu_display *display);

/* Get or publish borrowed KMS shim state while DRM registration is live. */
struct nvdrm_kms *nvgpu_device_get_kms(struct nvgpu_device *gpu);
void nvgpu_device_set_kms(struct nvgpu_device *gpu, struct nvdrm_kms *kms);

/* Reserve or release one user channel against the device-global quota. */
int nvgpu_device_reserve_channel(struct nvgpu_device *gpu);
void nvgpu_device_release_channel(struct nvgpu_device *gpu);

/* Return boot-time backend channel capacity after subtracting reserved slots. */
uint32_t nvgpu_device_get_channel_limit(struct nvgpu_device *gpu);
uint32_t nvgpu_device_get_channel_used(struct nvgpu_device *gpu);

#endif /* _NVGPU_DEVICE_H_ */
