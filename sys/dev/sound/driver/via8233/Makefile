# $FreeBSD: src/sys/modules/sound/driver/via8233/Makefile,v 1.2 2003/02/07 13:56:33 nyan Exp $
# $DragonFly: src/sys/dev/sound/driver/via8233/Makefile,v 1.3 2007/01/04 21:47:01 corecode Exp $

.PATH: ${.CURDIR}/../../../../dev/sound/pci

KMOD=	snd_via8233
SRCS=	device_if.h bus_if.h pci_if.h
SRCS+=	via8233.c

.include <bsd.kmod.mk>
