# $FreeBSD: src/sys/modules/nge/Makefile,v 1.2.2.2 2001/12/04 20:01:53 brooks Exp $
# $DragonFly: src/sys/dev/netif/nge/Makefile,v 1.2 2003/06/17 04:28:46 dillon Exp $

.PATH: ${.CURDIR}/../../dev/nge

KMOD=	if_nge
SRCS=	if_nge.c miibus_if.h opt_bdg.h device_if.h bus_if.h pci_if.h
KMODDEPS = miibus

.include <bsd.kmod.mk>
