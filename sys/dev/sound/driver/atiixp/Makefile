# $FreeBSD: src/sys/modules/sound/driver/atiixp/Makefile,v 1.1.2.1 2005/12/30 19:55:55 netchild Exp $
# $DragonFly: src/sys/dev/sound/driver/atiixp/Makefile,v 1.1 2007/01/04 21:47:00 corecode Exp $

.PATH: ${.CURDIR}/../../../../dev/sound/pci

KMOD=	snd_atiixp
SRCS=	device_if.h bus_if.h pci_if.h
SRCS+=	atiixp.c

.include <bsd.kmod.mk>
