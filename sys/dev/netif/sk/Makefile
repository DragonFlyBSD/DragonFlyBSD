# $FreeBSD: src/sys/modules/sk/Makefile,v 1.8.2.1 2000/04/27 20:18:34 wpaul Exp $
# $DragonFly: src/sys/dev/netif/sk/Makefile,v 1.2 2003/06/17 04:28:46 dillon Exp $

.PATH:	${.CURDIR}/../../pci
KMOD	= if_sk
SRCS	= if_sk.c opt_bdg.h device_if.h bus_if.h pci_if.h
SRCS	+= miibus_if.h

.include <bsd.kmod.mk>
