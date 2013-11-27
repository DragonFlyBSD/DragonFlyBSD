KMOD=	if_vge
SRCS=	if_vge.c
SRCS+=	opt_ifpoll.h
SRCS+=	miibus_if.h device_if.h bus_if.h pci_if.h pcidevs.h

.ifndef BUILDING_WITH_KERNEL
opt_ifpoll.h:
	echo '#define IFPOLL_ENABLE 1' > ${.OBJDIR}/${.TARGET}
.endif

.include <bsd.kmod.mk>
