# $FreeBSD: src/sys/modules/sis/Makefile,v 1.6 2000/01/28 11:26:35 bde Exp $
# $DragonFly: src/sys/dev/netif/sis/Makefile,v 1.2 2003/06/17 04:28:46 dillon Exp $

.PATH:	${.CURDIR}/../../pci
KMOD	= if_sis
SRCS	= if_sis.c opt_bdg.h device_if.h bus_if.h pci_if.h
SRCS	+= miibus_if.h
KMODDEPS = miibus

.include <bsd.kmod.mk>
