/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright © 2021 Intel Corporation
 */

#include <linux/pci.h>
#include <drm/drm.h>
#include <drm/drm_print.h>

/*
 * intel_graphics_stolen_* are defined in sys/bus/pci/pcivar.h
 * and set at early boot from machdep.c. Copy over the values
 * here to a Linux resource struct.
 */
struct linux_resource intel_graphics_stolen_res;

static int __init i915_init(void)
{
#if 0
	intel_graphics_stolen_res = (struct linux_resource)
		DEFINE_RES_MEM(0, 0);
#endif
	intel_graphics_stolen_res = (struct linux_resource)
		DEFINE_RES_MEM(intel_graphics_stolen_base,
		    intel_graphics_stolen_size);
	DRM_INFO("Got Intel graphics stolen memory base 0x%lx, size 0x%lx\n",
	    intel_graphics_stolen_res.start,
	    resource_size(&intel_graphics_stolen_res));
	return 0;
}

/* BSD stuff */
SYSINIT(i915_stolen_init, SI_SUB_DRIVERS, SI_ORDER_FIRST, i915_init, NULL);
