# $FreeBSD: src/sys/modules/bge/Makefile,v 1.1.2.2 2001/12/04 20:01:53 brooks Exp $
# $DragonFly: src/sys/dev/netif/bge/Makefile,v 1.5 2006/03/23 13:45:12 sephe Exp $

KMOD=	if_bge
SRCS=	if_bge.c
SRCS+=	opt_bdg.h
SRCS+=	miibus_if.h device_if.h bus_if.h pci_if.h
KMODDEPS = miibus

.include <bsd.kmod.mk>
