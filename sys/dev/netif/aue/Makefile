# $FreeBSD: src/sys/modules/aue/Makefile,v 1.2 2000/01/28 11:26:27 bde Exp $
# $DragonFly: src/sys/dev/netif/aue/Makefile,v 1.3 2005/05/24 07:36:29 joerg Exp $

KMOD	= if_aue
SRCS	= if_aue.c opt_bdg.h opt_usb.h device_if.h bus_if.h
SRCS	+= miibus_if.h
KMODDEPS = miibus

.include <bsd.kmod.mk>
