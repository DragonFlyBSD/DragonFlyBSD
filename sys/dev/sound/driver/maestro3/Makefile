# $FreeBSD: src/sys/modules/sound/driver/maestro3/Makefile,v 1.1.2.2 2001/03/04 08:19:02 scottl Exp $
# $DragonFly: src/sys/dev/sound/driver/maestro3/Makefile,v 1.3 2003/08/15 08:32:31 dillon Exp $
.PATH:		${.CURDIR}/../../pci ${.CURDIR}/../../pci/gnu
KMOD		= snd_maestro3
SRCS		= device_if.h bus_if.h isa_if.h pci_if.h
SRCS		+= maestro3.c
CFLAGS		+= -Wall

CFLAGS		+= -DM3_DEBUG_LEVEL=-1

KMODDEPS 	= snd_pcm

.include <bsd.kmod.mk>
