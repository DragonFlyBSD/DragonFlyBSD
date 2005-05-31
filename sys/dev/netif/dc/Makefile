# $FreeBSD: src/sys/modules/dc/Makefile,v 1.2 2000/01/28 11:26:28 bde Exp $
# $DragonFly: src/sys/dev/netif/dc/Makefile,v 1.3 2005/05/31 07:46:17 joerg Exp $

KMOD	= if_dc
SRCS	= if_dc.c opt_bdg.h device_if.h bus_if.h pci_if.h
SRCS	+= miibus_if.h
KMODDEPS = miibus

.include <bsd.kmod.mk>
