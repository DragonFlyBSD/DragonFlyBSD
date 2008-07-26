# $DragonFly: src/sys/dev/netif/jme/Makefile,v 1.1 2008/07/26 14:00:31 sephe Exp $

KMOD=	if_jme
SRCS=	if_jme.c
SRCS+=	miibus_if.h device_if.h bus_if.h pci_if.h
SRCS+=	opt_ethernet.h
KMODDEPS= miibus

.ifndef BUILDING_WITH_KERNEL
opt_ethernet.h:
	touch ${.OBJDIR}/${.TARGET}
.endif

.include <bsd.kmod.mk>
