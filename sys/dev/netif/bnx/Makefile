KMOD=	if_bnx
SRCS=	if_bnx.c
SRCS+=	miibus_if.h device_if.h bus_if.h pci_if.h pcidevs.h
SRCS+=	opt_ifpoll.h opt_bnx.h

.ifndef BUILDING_WITH_KERNEL

opt_ifpoll.h:
	echo '#define IFPOLL_ENABLE 1' > ${.OBJDIR}/${.TARGET}

opt_bnx.h:
	touch ${.OBJDIR}/${.TARGET}

.endif

.include <bsd.kmod.mk>
