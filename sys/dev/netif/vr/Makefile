# $FreeBSD: src/sys/modules/vr/Makefile,v 1.8 2000/01/28 11:26:42 bde Exp $
# $DragonFly: src/sys/dev/netif/vr/Makefile,v 1.2 2003/06/17 04:28:47 dillon Exp $

.PATH:	${.CURDIR}/../../pci
KMOD	= if_vr
SRCS	= if_vr.c opt_bdg.h device_if.h bus_if.h pci_if.h
SRCS	+= miibus_if.h
KMODDEPS = miibus

.include <bsd.kmod.mk>
