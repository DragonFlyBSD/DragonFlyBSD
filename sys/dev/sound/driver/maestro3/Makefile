# $FreeBSD: src/sys/modules/sound/driver/maestro3/Makefile,v 1.3 2003/02/07 13:56:32 nyan Exp $
# $DragonFly: src/sys/dev/sound/driver/maestro3/Makefile,v 1.4 2007/01/04 21:47:01 corecode Exp $

.PATH: ${.CURDIR}/../../pci

KMOD=	snd_maestro3
SRCS=	device_if.h bus_if.h pci_if.h
SRCS+=	maestro3.c

CFLAGS+= -Wall -DM3_DEBUG_LEVEL=-1

.include <bsd.kmod.mk>
