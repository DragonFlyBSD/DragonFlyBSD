# $FreeBSD: src/sys/modules/sound/driver/hda/Makefile,v 1.1 2006/10/01 11:13:00 ariff Exp $
# $DragonFly: src/sys/dev/sound/driver/hda/Makefile,v 1.1 2007/01/04 21:47:01 corecode Exp $

.PATH: ${.CURDIR}/../../../../dev/sound/pci/hda

KMOD=	snd_hda
SRCS=	device_if.h bus_if.h pci_if.h channel_if.h mixer_if.h isa_if.h
SRCS+=	hdac.c hdac_private.h hdac_reg.h hda_reg.h hdac.h

.include <bsd.kmod.mk>
