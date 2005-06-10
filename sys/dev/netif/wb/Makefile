# $FreeBSD: src/sys/modules/wb/Makefile,v 1.8 2000/01/28 11:26:43 bde Exp $
# $DragonFly: src/sys/dev/netif/wb/Makefile,v 1.3 2005/06/10 16:02:47 joerg Exp $

KMOD	= if_wb
SRCS	= if_wb.c opt_bdg.h device_if.h
SRCS	+= bus_if.h miibus_if.h pci_if.h
KMODDEPS = miibus

.include <bsd.kmod.mk>
