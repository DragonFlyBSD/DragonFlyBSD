# $FreeBSD: src/sys/modules/sound/driver/sb16/Makefile,v 1.2 2001/01/06 14:00:20 obrien Exp $
# $DragonFly: src/sys/dev/sound/driver/sb16/Makefile,v 1.3 2007/01/04 21:47:01 corecode Exp $

.PATH: ${.CURDIR}/../../../../dev/sound/isa

KMOD=	snd_sb16
SRCS=	device_if.h bus_if.h isa_if.h pci_if.h
SRCS+=	sb16.c

.include <bsd.kmod.mk>
