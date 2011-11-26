# $FreeBSD: src/sys/modules/sound/driver/ad1816/Makefile,v 1.2 2001/01/06 14:00:15 obrien Exp $
# $DragonFly: src/sys/dev/sound/driver/ad1816/Makefile,v 1.3 2007/01/04 21:47:00 corecode Exp $

.PATH: ${.CURDIR}/../../../../dev/sound/isa

KMOD=	snd_ad1816
SRCS=	device_if.h bus_if.h isa_if.h pci_if.h
SRCS+=	ad1816.c

.include <bsd.kmod.mk>
