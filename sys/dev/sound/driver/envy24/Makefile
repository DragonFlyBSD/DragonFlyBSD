# $FreeBSD: src/sys/modules/sound/driver/envy24/Makefile,v 1.3.2.1 2007/05/13 21:03:46 ariff Exp $
# $DragonFly: src/sys/dev/sound/driver/envy24/Makefile,v 1.1 2007/06/16 19:48:05 hasso Exp $

.PATH: ${.CURDIR}/../../../../dev/sound/pci

KMOD=	snd_envy24
SRCS=	device_if.h bus_if.h pci_if.h
SRCS+=	envy24.c

.include <bsd.kmod.mk>
