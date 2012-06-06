.PATH:	${.CURDIR}/..
KMOD	= drm
SRCS    = \
	ati_pcigart.c \
	drm_agpsupport.c \
	drm_auth.c \
	drm_bufs.c \
	drm_context.c \
	drm_dma.c \
	drm_drawable.c \
	drm_drv.c \
	drm_fops.c \
	drm_hashtab.c \
	drm_ioctl.c \
	drm_irq.c \
	drm_lock.c \
	drm_memory.c \
	drm_mm.c \
	drm_pci.c \
	drm_scatter.c \
	drm_sman.c \
	drm_sysctl.c \
	drm_vm.c

SRCS   += device_if.h bus_if.h pci_if.h opt_drm.h
CFLAGS += ${DEBUG_FLAGS} -I. -I..

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
