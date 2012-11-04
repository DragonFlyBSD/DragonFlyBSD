# $DragonFly: src/sys/dev/netif/stge/Makefile,v 1.1 2006/11/16 13:43:55 sephe Exp $

KMOD=	if_stge
SRCS=	if_stge.c
SRCS+=	opt_ifpoll.h
SRCS+=	miibus_if.h device_if.h bus_if.h pci_if.h

.ifndef BUILDING_WITH_KERNEL
opt_ifpoll.h:
	echo '#define IFPOLL_ENABLE 1' > ${.OBJDIR}/${.TARGET}
.endif

.include <bsd.kmod.mk>
