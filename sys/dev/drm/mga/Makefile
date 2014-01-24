KMOD= 	mga
SRCS=		mga_drv.c mga_state.c mga_warp.c mga_dma.c mga_irq.c
SRCS+=	device_if.h bus_if.h pci_if.h opt_drm.h opt_ktr.h
CFLAGS+=	${DEBUG_FLAGS} -I. -I.. -I@/dev/drm -I@/dev/drm/mga

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

CFLAGS+= -I${SYSDIR}/dev/drm/include

.include <bsd.kmod.mk>
