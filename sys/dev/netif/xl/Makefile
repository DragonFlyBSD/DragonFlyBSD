# $FreeBSD: src/sys/modules/xl/Makefile,v 1.9 2000/01/28 11:26:45 bde Exp $
# $DragonFly: src/sys/dev/netif/xl/Makefile,v 1.2 2003/06/17 04:28:47 dillon Exp $

.PATH:	${.CURDIR}/../../pci
KMOD	= if_xl
SRCS	= if_xl.c opt_bdg.h device_if.h bus_if.h pci_if.h
SRCS	+= miibus_if.h
KMODDEPS = miibus

.include <bsd.kmod.mk>
