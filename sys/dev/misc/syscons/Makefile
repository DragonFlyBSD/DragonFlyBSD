# $FreeBSD: src/sys/modules/syscons/Makefile,v 1.11.2.2 2003/05/15 02:02:39 murray Exp $
# $DragonFly: src/sys/dev/misc/syscons/Makefile,v 1.3 2006/11/07 20:48:11 dillon Exp $
#

.include "../../../machine/${MACHINE}/Makefile.inc"

SUBDIR =
SUBDIR += blank
SUBDIR += daemon
SUBDIR += dragon
SUBDIR += fade
SUBDIR += fire
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
