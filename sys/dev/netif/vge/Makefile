# $DragonFly: src/sys/dev/netif/vge/Makefile,v 1.1 2006/05/20 07:15:17 sephe Exp $

KMOD=	if_vge
SRCS=	if_vge.c
SRCS+=	opt_ifpoll.h
SRCS+=	miibus_if.h device_if.h bus_if.h pci_if.h

.ifndef BUILDING_WITH_KERNEL
opt_ifpoll.h:
	echo '#define IFPOLL_ENABLE 1' > ${.OBJDIR}/${.TARGET}
.endif

.include <bsd.kmod.mk>
