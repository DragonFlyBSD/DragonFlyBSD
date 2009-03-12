KMOD=	if_emx
SRCS=	if_emx.c
SRCS+=	device_if.h bus_if.h pci_if.h
SRCS+=	opt_polling.h opt_ktr.h opt_serializer.h opt_emx.h

KMODDEPS = ig_hal

.ifndef BUILDING_WITH_KERNEL

opt_polling.h:
	echo '#define DEVICE_POLLING 1' > ${.OBJDIR}/${.TARGET}

opt_serializer.h:
	touch ${.OBJDIR}/${.TARGET}

opt_emx.h:
	touch ${.OBJDIR}/${.TARGET}

.endif

.include <bsd.kmod.mk>
