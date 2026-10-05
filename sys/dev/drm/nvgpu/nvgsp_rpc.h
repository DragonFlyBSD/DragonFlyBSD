/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * GSP RPC transport boundary for the native NVIDIA GPU driver.
 */

#ifndef _NVGSP_RPC_H_
#define _NVGSP_RPC_H_

struct nvgpu_device;

/* Initialize GSP RPC transport state before early RPCs. */
int nvgsp_rpc_init(struct nvgpu_device *gpu);
#endif /* _NVGSP_RPC_H_ */
