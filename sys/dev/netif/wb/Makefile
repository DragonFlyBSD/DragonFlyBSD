# $FreeBSD: src/sys/modules/wb/Makefile,v 1.8 2000/01/28 11:26:43 bde Exp $
# $DragonFly: src/sys/dev/netif/wb/Makefile,v 1.2 2003/06/17 04:28:47 dillon Exp $

.PATH:	${.CURDIR}/../../pci
KMOD	= if_wb
SRCS	= if_wb.c opt_bdg.h device_if.h
SRCS	+= bus_if.h miibus_if.h pci_if.h
KMODDEPS = miibus

.include <bsd.kmod.mk>
