# $FreeBSD: src/sys/modules/sound/driver/Makefile.inc,v 1.1.2.3 2001/02/24 21:49:05 cg Exp $
# $DragonFly: src/sys/dev/sound/driver/Makefile.inc,v 1.4 2004/01/28 19:27:01 dillon Exp $

SRCS		+= ac97_if.h channel_if.h feeder_if.h mixer_if.h

.include "../Makefile.inc"
