# $FreeBSD: src/sys/modules/sound/driver/cmi/Makefile,v 1.1.2.1 2001/02/27 03:42:29 cg Exp $
# $DragonFly: src/sys/dev/sound/driver/cmi/Makefile,v 1.2 2003/06/17 04:28:46 dillon Exp $

.PATH:		${.CURDIR}/../../../../dev/sound/pci
KMOD		= snd_cmi
SRCS		= device_if.h bus_if.h isa_if.h pci_if.h
SRCS		+= cmi.c
KMODDEPS 	= snd_pcm

.include <bsd.kmod.mk>
