# $FreeBSD: src/sys/modules/sound/driver/Makefile.inc,v 1.3 2001/04/08 21:50:41 obrien Exp $
# $DragonFly: src/sys/dev/sound/driver/Makefile.inc,v 1.5 2007/01/04 21:47:00 corecode Exp $

SRCS+=	ac97_if.h channel_if.h feeder_if.h mixer_if.h

.include "../Makefile.inc"
