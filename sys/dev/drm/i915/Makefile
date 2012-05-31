KMOD	= i915
SRCS    = i915_dma.c i915_drv.c i915_irq.c i915_mem.c i915_suspend.c
SRCS   += device_if.h bus_if.h pci_if.h opt_drm.h
CFLAGS += ${DEBUG_FLAGS} -I. -I.. -I@/dev/drm -I@/dev/drm/i915

.if defined(DRM_DEBUG)
DRM_DEBUG_OPT= "\#define DRM_DEBUG 1"
.endif

.if !defined(DRM_NOLINUX)
DRM_LINUX_OPT= "\#define DRM_LINUX 1"
.endif

opt_drm.h:
	touch ${.TARGET}
	echo $(DRM_DEBUG_OPT) >> ${.TARGET}
	echo $(DRM_LINUX_OPT) >> ${.TARGET}

.include <bsd.kmod.mk>
