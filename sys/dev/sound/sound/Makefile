# $FreeBSD: head/sys/modules/sound/sound/Makefile 212523 2010-09-13 02:05:42Z imp $

.PATH: ${.CURDIR}/..
.PATH: ${.CURDIR}/../pcm

KMOD=	sound
SRCS=	device_if.h bus_if.h pci_if.h
SRCS+=	ac97_if.h channel_if.h feeder_if.h mixer_if.h
SRCS+=	ac97_if.c channel_if.c feeder_if.c mixer_if.c
SRCS+=	feeder.c feeder_rate.c feeder_volume.c
SRCS+=	feeder_chain.c feeder_eq.c feeder_format.c
SRCS+=	feeder_matrix.c feeder_mixer.c
SRCS+=	feeder_eq_gen.h feeder_rate_gen.h snd_fxdiv_gen.h
SRCS+=	ac97.c ac97_patch.c buffer.c channel.c clone.c dsp.c
SRCS+=	mixer.c sndstat.c sound.c unit.c vchan.c

.if !exists(@)
feeder_eq_gen.h:	@
.else
feeder_eq_gen.h:	@/tools/sound/feeder_eq_mkfilter.awk
.endif
	${AWK} -f @/tools/sound/feeder_eq_mkfilter.awk -- ${FEEDER_EQ_PRESETS} > ${.TARGET}

.if !exists(@)
feeder_rate_gen.h:	@
.else
feeder_rate_gen.h:	@/tools/sound/feeder_rate_mkfilter.awk
.endif
	${AWK} -f @/tools/sound/feeder_rate_mkfilter.awk -- ${FEEDER_RATE_PRESETS} > ${.TARGET}

.if !exists(@)
snd_fxdiv_gen.h:	@
.else
snd_fxdiv_gen.h:	@/tools/sound/snd_fxdiv_gen.awk
.endif
	${AWK} -f @/tools/sound/snd_fxdiv_gen.awk -- > ${.TARGET}

CLEANFILES+=	feeder_eq_gen.h feeder_rate_gen.h snd_fxdiv_gen.h

EXPORT_SYMS=	YES	# XXX evaluate

.include <bsd.kmod.mk>
