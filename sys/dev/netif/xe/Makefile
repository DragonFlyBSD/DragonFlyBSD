# $FreeBSD: src/sys/modules/xe/Makefile,v 1.1.2.1 2002/04/08 18:04:18 asmodai Exp $
# $DragonFly: src/sys/dev/netif/xe/Makefile,v 1.2 2003/06/17 04:28:47 dillon Exp $

.PATH:	${.CURDIR}/../../dev/xe
KMOD	= if_xe
SRCS	= if_xe.c opt_bdg.h device_if.h card_if.h bus_if.h pci_if.h
SRCS	+= if_xevar.h if_xereg.h

.include <bsd.kmod.mk>
