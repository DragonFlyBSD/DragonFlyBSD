# $DragonFly: src/sys/dev/netif/msk/Makefile,v 1.2 2008/06/26 13:08:55 sephe Exp $

KMOD=	if_msk
SRCS=	if_msk.c
SRCS+=	miibus_if.h device_if.h bus_if.h pci_if.h
SRCS+=	opt_ethernet.h
KMODDEPS = miibus

.ifndef BUILDING_WITH_KERNEL
opt_ethernet.h:
	touch ${.OBJDIR}/${.TARGET}
.endif

.include <bsd.kmod.mk>
