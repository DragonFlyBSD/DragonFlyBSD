# $DragonFly: src/sys/dev/netif/axe/Makefile,v 1.2 2005/05/25 11:38:58 joerg Exp $

KMOD	= if_axe
SRCS	= if_axe.c opt_bdg.h opt_usb.h device_if.h bus_if.h
SRCS	+= miibus_if.h
KMODDEPS = miibus

.include <bsd.kmod.mk>
