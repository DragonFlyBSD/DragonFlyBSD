/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * GSP backend state boundary for the native NVIDIA GPU driver.
 */

#ifndef _NVGSP_STATE_H_
#define _NVGSP_STATE_H_

#include <sys/stdint.h>

struct nvgpu_device;

struct nvgsp_intr_masks {
	uint32_t nonstall;
	uint32_t stall;
	uint32_t engine;
	uint32_t display;
};

/* Return usable VRAM base parsed from static GSP info. */
uint64_t nvgsp_state_get_fb_usable_base(struct nvgpu_device *gpu);
/* Return usable VRAM bytes parsed from static GSP info. */
uint64_t nvgsp_state_get_fb_usable_size(struct nvgpu_device *gpu);
/* Initialize CPU-side GSP backend state. */
int nvgsp_state_init(struct nvgpu_device *gpu);
/* Release CPU-side GSP state after GSP shutdown. */
void nvgsp_state_fini(struct nvgpu_device *gpu);
/* Enable the finite CPU leaf vectors returned by GSP after the ithread is wired. */
int nvgsp_state_enable_intr(struct nvgpu_device *gpu);
/* Return GSP's classification for one CPU interrupt leaf. */
void nvgsp_state_get_intr_masks(struct nvgpu_device *gpu, uint32_t leaf,
    struct nvgsp_intr_masks *masks);
#endif /* _NVGSP_STATE_H_ */
