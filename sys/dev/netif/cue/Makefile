# $FreeBSD: src/sys/modules/cue/Makefile,v 1.2 2000/01/28 11:26:27 bde Exp $
# $DragonFly: src/sys/dev/netif/cue/Makefile,v 1.2 2003/06/17 04:28:43 dillon Exp $

S	= ${.CURDIR}/../..
.PATH:	$S/dev/usb
KMOD	= if_cue
SRCS	= if_cue.c opt_bdg.h opt_usb.h device_if.h bus_if.h
SRCS	+= miibus_if.h

.include <bsd.kmod.mk>
