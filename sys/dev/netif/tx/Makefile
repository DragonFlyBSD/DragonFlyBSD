# $FreeBSD: src/sys/modules/tx/Makefile,v 1.4.2.4 2002/10/29 01:43:50 semenu Exp $
# $DragonFly: src/sys/dev/netif/tx/Makefile,v 1.2 2003/06/17 04:28:47 dillon Exp $

.PATH:	${.CURDIR}/../../dev/tx
KMOD	= if_tx
SRCS	= if_tx.c opt_bdg.h device_if.h bus_if.h pci_if.h
SRCS	+= miibus_if.h

.include <bsd.kmod.mk>
