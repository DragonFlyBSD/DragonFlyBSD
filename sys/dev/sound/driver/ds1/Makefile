# $FreeBSD: src/sys/modules/sound/driver/ds1/Makefile,v 1.1.2.1 2000/09/23 19:15:02 cg Exp $
# $DragonFly: src/sys/dev/sound/driver/ds1/Makefile,v 1.2 2003/06/17 04:28:46 dillon Exp $

.PATH:		${.CURDIR}/../../../../dev/sound/pci
KMOD		= snd_ds1
SRCS		= device_if.h bus_if.h isa_if.h pci_if.h
SRCS		+= ds1.c
KMODDEPS 	= snd_pcm

.include <bsd.kmod.mk>
