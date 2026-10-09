/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * GSP boot and shutdown boundary for the native NVIDIA GPU driver.
 */

#ifndef _NVGSP_BOOT_H_
#define _NVGSP_BOOT_H_

struct nvgpu_device;

/* Boot GSP and complete early RM handshake.  gpu is borrowed; boot LWKT only, may sleep. */
int nvgsp_boot(struct nvgpu_device *gpu);
/* Shut GSP down after DRM users and runtime backend state have stopped. */
void nvgsp_shutdown(struct nvgpu_device *gpu);

#endif /* _NVGSP_BOOT_H_ */
