# $FreeBSD: src/sys/modules/canbus/Makefile,v 1.1.2.1 2003/02/10 13:11:51 nyan Exp $
# $DragonFly: src/sys/bus/canbus/Attic/Makefile,v 1.2 2003/06/17 04:28:43 dillon Exp $

.PATH:	${.CURDIR}/../../pc98/pc98
KMOD	= canbus
SRCS	= canbus.c
SRCS	+= device_if.h bus_if.h canbus_if.h canbus_if.c

.include <bsd.kmod.mk>
