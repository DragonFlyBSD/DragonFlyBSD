# $FreeBSD: src/sys/modules/sf/Makefile,v 1.9 2000/01/28 11:26:35 bde Exp $
# $DragonFly: src/sys/dev/netif/sf/Makefile,v 1.2 2003/06/17 04:28:46 dillon Exp $

.PATH:	${.CURDIR}/../../pci
KMOD	= if_sf
SRCS	= if_sf.c opt_bdg.h device_if.h bus_if.h pci_if.h
SRCS	+= miibus_if.h
KMODDEPS = miibus

.include <bsd.kmod.mk>
