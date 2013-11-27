KMOD=	if_jme
SRCS=	if_jme.c
SRCS+=	miibus_if.h device_if.h bus_if.h pci_if.h pcidevs.h
SRCS+=	opt_ifpoll.h opt_jme.h

.ifndef BUILDING_WITH_KERNEL

opt_ifpoll.h:
	echo '#define IFPOLL_ENABLE 1' > ${.OBJDIR}/${.TARGET}

opt_jme.h:
	touch ${.OBJDIR}/${.TARGET}

.endif

.include <bsd.kmod.mk>
