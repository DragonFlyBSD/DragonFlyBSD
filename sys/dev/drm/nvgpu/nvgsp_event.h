/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * GSP event dispatch boundary for the native NVIDIA GPU driver.
 */

#ifndef _NVGSP_EVENT_H_
#define _NVGSP_EVENT_H_

struct nvgpu_device;

/* Initialize GSP event state before init-done polling. */
int nvgsp_event_init(struct nvgpu_device *gpu);
/* Dispatch pending GSP events; keep interrupt-facing work short. */
void nvgsp_event_dispatch(struct nvgpu_device *gpu);

#endif /* _NVGSP_EVENT_H_ */
