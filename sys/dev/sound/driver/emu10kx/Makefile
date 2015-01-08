# $FreeBSD: head/sys/modules/sound/driver/emu10kx/Makefile 230915 2012-02-02 17:25:39Z joel $
.PATH: ${.CURDIR}/../../../../dev/sound/pci

KMOD=	snd_emu10kx

SRCS=	device_if.h bus_if.h pci_if.h
SRCS+=	channel_if.h ac97_if.h mixer_if.h mpufoi_if.h
# Master, PCM and MIDI devices
SRCS+=	emu10kx.c
SRCS+=	emu10kx-pcm.c
SRCS+=	emu10kx-midi.c

.include <bsd.kmod.mk>
