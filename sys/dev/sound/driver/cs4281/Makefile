# $FreeBSD: src/sys/modules/sound/driver/cs4281/Makefile,v 1.3 2003/02/07 13:56:31 nyan Exp $
# $DragonFly: src/sys/dev/sound/driver/cs4281/Makefile,v 1.3 2007/01/04 21:47:01 corecode Exp $

.PATH: ${.CURDIR}/../../../../dev/sound/pci

KMOD=	snd_cs4281
SRCS=	device_if.h bus_if.h pci_if.h
SRCS+=	cs4281.c

.include <bsd.kmod.mk>
