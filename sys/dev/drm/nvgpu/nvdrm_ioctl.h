/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Nouveau-compatible DRM ioctl dispatch table.
 */

#ifndef _NVDRM_IOCTL_H_
#define _NVDRM_IOCTL_H_

#include <drm/drm_ioctl.h>

#define NVDRM_IOCTL_COUNT	0x45

/* Sparse nouveau ABI ioctl table consumed by drm_ioctl(). */
extern const struct drm_ioctl_desc nvdrm_ioctl_descs[];

#endif /* _NVDRM_IOCTL_H_ */
