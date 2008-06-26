# $DragonFly: src/sys/dev/netif/bfe/Makefile,v 1.3 2008/06/26 12:05:19 sephe Exp $

KMOD=	if_bfe
SRCS= 	if_bfe.c
SRCS+=	miibus_if.h device_if.h bus_if.h pci_if.h
SRCS+=	opt_ethernet.h
KMODDEPS = miibus

.ifndef BUILDING_WITH_KERNEL
opt_ethernet.h:
	touch ${.OBJDIR}/${.TARGET}
.endif

.include <bsd.kmod.mk>
