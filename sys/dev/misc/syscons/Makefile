# $FreeBSD: src/sys/modules/syscons/Makefile,v 1.11.2.2 2003/05/15 02:02:39 murray Exp $
#

.include "../../../platform/${MACHINE_PLATFORM}/Makefile.inc"

SUBDIR =
SUBDIR += blank
SUBDIR += dragon
SUBDIR += fade
SUBDIR += fire
SUBDIR += fred
SUBDIR += green
SUBDIR += logo
SUBDIR += rain
SUBDIR += snake
SUBDIR += star
SUBDIR += warp

.ifdef SYSCONS_APM_SUPPORT
SUBDIR += apm
.endif

.include <bsd.subdir.mk>
