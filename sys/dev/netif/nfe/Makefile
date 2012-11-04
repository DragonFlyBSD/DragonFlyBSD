# $DragonFly: src/sys/dev/netif/nfe/Makefile,v 1.3 2008/09/17 08:51:29 sephe Exp $

KMOD=	if_nfe
SRCS=	if_nfe.c
SRCS+=	miibus_if.h device_if.h bus_if.h pci_if.h
SRCS+=	opt_ifpoll.h

.ifndef BUILDING_WITH_KERNEL
opt_ifpoll.h:
	echo '#define IFPOLL_ENABLE 1' > ${.OBJDIR}/${.TARGET}
.endif

.include <bsd.kmod.mk>
