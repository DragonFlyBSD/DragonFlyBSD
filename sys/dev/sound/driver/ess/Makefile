# $FreeBSD: src/sys/modules/sound/driver/ess/Makefile,v 1.3 2002/01/23 03:32:36 cg Exp $
# $DragonFly: src/sys/dev/sound/driver/ess/Makefile,v 1.3 2007/01/04 21:47:01 corecode Exp $

.PATH: ${.CURDIR}/../../../../dev/sound/isa

KMOD=	snd_ess
SRCS=	device_if.h bus_if.h isa_if.h pci_if.h
SRCS+=	ess.c

.include <bsd.kmod.mk>
