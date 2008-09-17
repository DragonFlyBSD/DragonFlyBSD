# $DragonFly: src/sys/dev/netif/jme/Makefile,v 1.2 2008/09/17 08:51:29 sephe Exp $

KMOD=	if_jme
SRCS=	if_jme.c
SRCS+=	miibus_if.h device_if.h bus_if.h pci_if.h
KMODDEPS= miibus

.include <bsd.kmod.mk>
