KMOD=	if_emx
SRCS=	if_emx.c
SRCS+=	device_if.h bus_if.h pci_if.h
SRCS+=	opt_ifpoll.h opt_ktr.h opt_emx.h

.ifndef BUILDING_WITH_KERNEL

opt_ifpoll.h:
	touch ${.OBJDIR}/${.TARGET}

opt_emx.h:
	touch ${.OBJDIR}/${.TARGET}

.endif

.include <bsd.kmod.mk>
