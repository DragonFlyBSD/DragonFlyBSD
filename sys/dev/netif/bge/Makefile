# $FreeBSD: src/sys/modules/bge/Makefile,v 1.1.2.2 2001/12/04 20:01:53 brooks Exp $
# $DragonFly: src/sys/dev/netif/bge/Makefile,v 1.6 2006/06/25 11:02:38 corecode Exp $

KMOD=	if_bge
SRCS=	if_bge.c
SRCS+=	miibus_if.h device_if.h bus_if.h pci_if.h
KMODDEPS = miibus

.include <bsd.kmod.mk>
