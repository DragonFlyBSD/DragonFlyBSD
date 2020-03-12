/*
 * Linux kernel options configuration file
 */

#include <linux/kconfig.h>

#define CONFIG_X86	1
#define CONFIG_X86_PAT	1
#define CONFIG_PCI	1
#define CONFIG_ACPI	1

#define CONFIG_BACKLIGHT_CLASS_DEVICE	1

#define CONFIG_DRM_FBDEV_EMULATION		1
#define CONFIG_DRM_I915_KMS			1
#define CONFIG_DRM_I915_PRELIMINARY_HW_SUPPORT	1
#define CONFIG_DRM_LOAD_EDID_FIRMWARE		1

// CONFIG_GENERIC_ATOMIC64 is not set on x86

/*
   This is perhaps not the best place, but prevents lots of further
   compilation problems in imported Linux code
*/
#pragma GCC diagnostic ignored "-Wcast-qual"
#pragma GCC diagnostic ignored "-Wpointer-arith"
#pragma GCC diagnostic ignored "-Wunused-parameter"
