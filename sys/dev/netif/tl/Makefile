# $FreeBSD: src/sys/modules/tl/Makefile,v 1.11 2000/01/28 11:26:39 bde Exp $
# $DragonFly: src/sys/dev/netif/tl/Makefile,v 1.3 2005/06/14 12:38:04 joerg Exp $

KMOD	= if_tl
SRCS	= if_tl.c opt_bdg.h device_if.h bus_if.h pci_if.h
SRCS	+= miibus_if.h
KMODDEPS = miibus

.include <bsd.kmod.mk>
