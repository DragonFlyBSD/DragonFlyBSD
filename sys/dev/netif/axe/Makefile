# $DragonFly: src/sys/dev/netif/axe/Makefile,v 1.1 2003/12/30 01:01:46 dillon Exp $

S	= ${.CURDIR}/../..
.PATH:	$S/dev/usb
KMOD	= if_axe
SRCS	= if_axe.c opt_bdg.h opt_usb.h device_if.h bus_if.h
SRCS	+= miibus_if.h
KMODDEPS = miibus

.include <bsd.kmod.mk>
