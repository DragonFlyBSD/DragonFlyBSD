# $FreeBSD: src/sys/modules/sound/driver/csa/Makefile,v 1.4 2003/02/07 13:56:31 nyan Exp $
# $DragonFly: src/sys/dev/sound/driver/csa/Makefile,v 1.3 2007/01/04 21:47:01 corecode Exp $

.PATH: ${.CURDIR}/../../../../dev/sound/pci

KMOD=	snd_csa
SRCS=	device_if.h bus_if.h pci_if.h
SRCS+=	csa.c csapcm.c

.include <bsd.kmod.mk>
