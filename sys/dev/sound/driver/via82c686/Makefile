# $FreeBSD: src/sys/modules/sound/driver/via82c686/Makefile,v 1.1.2.1 2000/10/05 05:45:55 cg Exp $
# $DragonFly: src/sys/dev/sound/driver/via82c686/Makefile,v 1.2 2003/06/17 04:28:46 dillon Exp $

.PATH:		${.CURDIR}/../../../../dev/sound/pci
KMOD		= snd_via82c686
SRCS		= device_if.h bus_if.h isa_if.h pci_if.h
SRCS		+= via82c686.c
KMODDEPS 	= snd_pcm

.include <bsd.kmod.mk>
