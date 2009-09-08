# $FreeBSD: src/sys/modules/sound/driver/mss/Makefile,v 1.5 2002/11/06 13:46:59 nyan Exp $
# $DragonFly: src/sys/dev/sound/driver/mss/Makefile,v 1.3 2007/01/04 21:47:01 corecode Exp $

.PATH: ${.CURDIR}/../../../../dev/sound/isa

KMOD=	snd_mss
SRCS=	device_if.h bus_if.h isa_if.h pci_if.h
SRCS+=	mss.c gusc.c

.include <bsd.kmod.mk>
