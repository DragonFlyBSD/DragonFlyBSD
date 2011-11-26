# $FreeBSD: src/sys/modules/sound/driver/vibes/Makefile,v 1.4 2003/02/07 13:56:33 nyan Exp $
# $DragonFly: src/sys/dev/sound/driver/vibes/Makefile,v 1.3 2007/01/04 21:47:02 corecode Exp $

.PATH: ${.CURDIR}/../../../../dev/sound/pci

KMOD=	snd_vibes
SRCS=	device_if.h bus_if.h pci_if.h
SRCS+=	vibes.c

.include <bsd.kmod.mk>
