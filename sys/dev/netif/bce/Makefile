# $DragonFly: src/sys/dev/netif/bce/Makefile,v 1.3 2008/09/17 08:51:29 sephe Exp $

KMOD=	if_bce
SRCS=	if_bce.c
SRCS+=	miibus_if.h device_if.h bus_if.h pci_if.h
SRCS+=	opt_bce.h opt_ifpoll.h

#ifndef BUILDING_WITH_KERNEL
opt_ifpoll.h:
	echo '#define IFPOLL_ENABLE 1' > ${.OBJDIR}/${.TARGET}

opt_bce.h:
	touch ${.OBJDIR}/${.TARGET}
#endif

.include <bsd.kmod.mk>
