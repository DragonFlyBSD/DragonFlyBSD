# $FreeBSD: src/sys/modules/sound/driver/via82c686/Makefile,v 1.3 2003/02/07 13:56:33 nyan Exp $
# $DragonFly: src/sys/dev/sound/driver/via82c686/Makefile,v 1.3 2007/01/04 21:47:01 corecode Exp $

.PATH: ${.CURDIR}/../../../../dev/sound/pci

KMOD=	snd_via82c686
SRCS=	device_if.h bus_if.h pci_if.h
SRCS+=	via82c686.c

.include <bsd.kmod.mk>
