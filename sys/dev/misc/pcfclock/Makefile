# $FreeBSD: src/sys/modules/pcfclock/Makefile,v 1.1 2000/02/29 10:57:42 sheldonh Exp $
# $DragonFly: src/sys/dev/misc/pcfclock/Makefile,v 1.2 2003/06/17 04:28:46 dillon Exp $

.PATH:	${.CURDIR}/../../dev/ppbus
KMOD=	pcfclock
SRCS=	pcfclock.c opt_pcfclock.h
SRCS+=	bus_if.h device_if.h ppbus_if.h

.include <bsd.kmod.mk>
