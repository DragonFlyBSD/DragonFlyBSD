# $DragonFly: src/sys/dev/drm/i915/Makefile,v 1.1 2008/04/05 18:12:29 hasso Exp $

.PATH:	${.CURDIR}/..
KMOD	= i915
KMODDEPS= drm
NO_MAN	= YES
SRCS    = i915_dma.c i915_drv.c i915_irq.c i915_mem.c
SRCS   += device_if.h bus_if.h pci_if.h opt_drm.h
CFLAGS += ${DEBUG_FLAGS} -I. -I..

.if defined(DRM_DEBUG)
DRM_DEBUG_OPT= "\#define DRM_DEBUG 1"
.endif

.if !defined(DRM_NOLINUX)
DRM_LINUX_OPT= "\#define DRM_LINUX 1"
.endif

opt_drm.h:
	touch opt_drm.h
	echo $(DRM_DEBUG_OPT) >> opt_drm.h
	echo $(DRM_LINUX_OPT) >> opt_drm.h

.include <bsd.kmod.mk>
