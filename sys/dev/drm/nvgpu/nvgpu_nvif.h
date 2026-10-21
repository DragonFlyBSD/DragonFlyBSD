/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Nouveau NVIF syscall implementation.
 */

#ifndef _NVGPU_NVIF_H_
#define _NVGPU_NVIF_H_

#include <sys/types.h>

struct nvgpu_proc;

/* Handle one validated variable-size NVIF ioctl payload copied by the DRM shim. */
int nvgpu_nvif_ioctl(struct nvgpu_proc *proc, void *data, size_t size);

#endif /* _NVGPU_NVIF_H_ */
