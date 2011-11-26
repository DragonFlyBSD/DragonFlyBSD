# $FreeBSD: src/sys/modules/sound/sound/Makefile,v 1.16.2.2 2006/09/13 08:40:21 des Exp $
# $DragonFly: src/sys/dev/sound/pcm/Makefile,v 1.3 2007/01/04 21:47:03 corecode Exp $

.PATH: ${.CURDIR}/../isa

KMOD=	sound
SRCS=	device_if.h bus_if.h isa_if.h pci_if.h use_isa.h
SRCS+=	ac97_if.h channel_if.h feeder_if.h mixer_if.h
SRCS+=	ac97_if.c channel_if.c feeder_if.c mixer_if.c
SRCS+=	ac97.c ac97_patch.c buffer.c channel.c dsp.c
SRCS+=	fake.c feeder.c feeder_fmt.c feeder_rate.c feeder_volume.c
SRCS+=	mixer.c sndstat.c sound.c vchan.c

EXPORT_SYMS=	YES	# XXX evaluate

.if ${MACHINE_ARCH} == "sparc64"
# Create an empty opt_isa.h in order to keep kmod.mk from linking in an
# existing one from KERNBUILDDIR which possibly has DEV_ISA defined so
# sound.ko is always built without isadma support.
use_isa.h:
	:> ${.TARGET}
.else
SRCS+=	sndbuf_dma.c

.ifndef BUILDING_WITH_KERNEL
use_isa.h:
	echo "#define NISA 1" > ${.TARGET}
.endif
.endif

.include <bsd.kmod.mk>
