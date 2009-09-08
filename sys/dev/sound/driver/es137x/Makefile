# $FreeBSD: src/sys/modules/sound/driver/es137x/Makefile,v 1.3 2003/02/07 13:56:32 nyan Exp $
# $DragonFly: src/sys/dev/sound/driver/es137x/Makefile,v 1.3 2007/01/04 21:47:01 corecode Exp $

.PATH: ${.CURDIR}/../../../../dev/sound/pci

KMOD=	snd_es137x
SRCS=	device_if.h bus_if.h pci_if.h
SRCS+=	es137x.c

.include <bsd.kmod.mk>
