# $FreeBSD: src/sys/modules/sound/driver/uaudio/Makefile,v 1.1.2.1 2002/08/24 08:06:13 nsayer Exp $
# $DragonFly: src/sys/dev/sound/driver/uaudio/Makefile,v 1.2 2003/06/17 04:28:46 dillon Exp $

.PATH:		${.CURDIR}/../../../../dev/sound/usb
KMOD		= snd_uaudio
SRCS		= device_if.h bus_if.h isa_if.h opt_usb.h
SRCS		+= uaudio.c uaudio_pcm.c
KMODDEPS 	= snd_pcm

.include <bsd.kmod.mk>
