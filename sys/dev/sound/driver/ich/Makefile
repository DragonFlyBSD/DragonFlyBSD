# $FreeBSD: src/sys/modules/sound/driver/ich/Makefile,v 1.1.2.1 2001/08/01 05:37:30 cg Exp $
# $DragonFly: src/sys/dev/sound/driver/ich/Makefile,v 1.2 2003/06/17 04:28:46 dillon Exp $

.PATH:		${.CURDIR}/../../../../dev/sound/pci
KMOD		= snd_ich
SRCS		= device_if.h bus_if.h isa_if.h pci_if.h
SRCS		+= ich.c
KMODDEPS 	= snd_pcm

.include <bsd.kmod.mk>
