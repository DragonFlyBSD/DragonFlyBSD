/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026 Abdelkader Boudih <dragonflybsd@seuros.com>
 *
 * DragonFlyBSD Apple SMC driver - debug macros
 */

#ifndef _DEV_APPLE_SMC_DEBUG_H_
#define _DEV_APPLE_SMC_DEBUG_H_

#ifdef APPLE_SMC_DEBUG
#define SMC_DEBUG(dev, fmt, ...)	\
	device_printf((dev), "%s: " fmt, __func__, ##__VA_ARGS__)
#define SMC_DEBUG_KEY(dev, key, val)	\
	device_printf((dev), "key[%.4s] = 0x%08x\n", (key), (val))
#else
#define SMC_DEBUG(dev, fmt, ...)	do {} while (0)
#define SMC_DEBUG_KEY(dev, key, val)	do {} while (0)
#endif

#endif /* !_DEV_APPLE_SMC_DEBUG_H_ */
