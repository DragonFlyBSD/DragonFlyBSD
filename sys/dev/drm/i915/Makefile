KMOD	= i915kms
SRCS	= \
	i915_debugfs.c \
	i915_dma.c \
	i915_drv.c \
	i915_gem.c \
	i915_gem_context.c \
	i915_gem_execbuffer.c \
	i915_gem_evict.c \
	i915_gem_gtt.c \
	i915_gem_tiling.c \
	i915_irq.c \
	i915_suspend.c \
	intel_bios.c \
	intel_crt.c \
	intel_ddi.c \
	intel_display.c \
	intel_dp.c \
	intel_fb.c \
	intel_hdmi.c \
	intel_i2c.c \
	intel_lvds.c \
	intel_modes.c \
	intel_opregion.c \
	intel_overlay.c \
	intel_panel.c \
	intel_pm.c \
	intel_ringbuffer.c \
	intel_sdvo.c \
	intel_sprite.c \
	intel_tv.c

SRCS   += acpi_if.h device_if.h bus_if.h pci_if.h iicbus_if.h iicbb_if.h opt_drm.h \
	  opt_ktr.h

CFLAGS+= -I${SYSDIR}/dev/drm/include
CFLAGS+= -I${.CURDIR}/../../../contrib/dev/acpica/source/include

.include <bsd.kmod.mk>
