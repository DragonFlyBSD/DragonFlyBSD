/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Nouveau GETPARAM implementation.
 */

#ifndef _NVGPU_INFO_H_
#define _NVGPU_INFO_H_

#include <sys/stdint.h>

struct nvgpu_proc;

/* Fill one nouveau GETPARAM value for a live process.  proc is borrowed. */
int nvgpu_info_get_param(struct nvgpu_proc *proc, uint64_t param,
    uint64_t *value);

#endif /* _NVGPU_INFO_H_ */
