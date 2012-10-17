KMOD=	if_bnx
SRCS=	if_bnx.c
SRCS+=	miibus_if.h device_if.h bus_if.h pci_if.h
SRCS+=	opt_ifpoll.h opt_bnx.h

.ifndef BUILDING_WITH_KERNEL

opt_ifpoll.h:
	touch ${.OBJDIR}/${.TARGET}

opt_bnx.h:
	touch ${.OBJDIR}/${.TARGET}

.endif

.include <bsd.kmod.mk>
