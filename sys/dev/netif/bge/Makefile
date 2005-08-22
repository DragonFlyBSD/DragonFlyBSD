# $FreeBSD: src/sys/modules/bge/Makefile,v 1.1.2.2 2001/12/04 20:01:53 brooks Exp $
# $DragonFly: src/sys/dev/netif/bge/Makefile,v 1.4 2005/08/22 18:29:52 joerg Exp $

KMOD=	if_bge
SRCS=	if_bge.c
SRCS+=	opt_bge.h opt_bdg.h
SRCS+=	miibus_if.h device_if.h bus_if.h pci_if.h
KMODDEPS = miibus

.include <bsd.kmod.mk>
