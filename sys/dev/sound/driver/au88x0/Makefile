# $FreeBSD: src/sys/modules/sound/driver/au88x0/Makefile,v 1.1 2003/06/01 11:58:46 des Exp $
# $DragonFly: src/sys/dev/sound/driver/au88x0/Makefile,v 1.1 2007/01/04 21:47:00 corecode Exp $

.PATH: ${.CURDIR}/../../../../dev/sound/pci

KMOD=	snd_au88x0
SRCS=	device_if.h bus_if.h pci_if.h
SRCS+=	au88x0.c

.include <bsd.kmod.mk>
