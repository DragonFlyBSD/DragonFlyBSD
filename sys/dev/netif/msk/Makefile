# $DragonFly: src/sys/dev/netif/msk/Makefile,v 1.1 2007/12/26 14:02:36 sephe Exp $

KMOD=	if_msk
SRCS=	if_msk.c
SRCS+=	miibus_if.h device_if.h bus_if.h pci_if.h
KMODDEPS = miibus

.include <bsd.kmod.mk>
