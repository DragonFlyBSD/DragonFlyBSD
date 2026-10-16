/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * GSP RM object boundary for the native NVIDIA GPU driver.
 */

#ifndef _NVGSP_RM_H_
#define _NVGSP_RM_H_

struct nvgpu_device;

/* Free an RM graphics object after GPU use has stopped. */
int nvgsp_rm_free_graphics_object(struct nvgpu_device *gpu);

#endif /* _NVGSP_RM_H_ */
