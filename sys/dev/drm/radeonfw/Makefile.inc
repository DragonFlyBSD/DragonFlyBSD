# $FreeBSD: head/sys/modules/drm2/radeonkmsfw/Makefile.inc 254885 2013-08-25 19:37:15Z dumbbell $
#
# Common rules for building firmware.  Note this gets auto-included
# by the subdir Makefile's as a consequence of included bsd.kmod.mk.

.include <bsd.dragonfly.mk>

_FIRM=	${IMG}.bin

CLEANFILES+=	${_FIRM}

FIRMWS=	${_FIRM}:${KMOD}

${_FIRM}: ${DF.SRCTOP}/sys/contrib/dev/drm/radeonfw/${_FIRM}.uu
	${NORMAL_FW}
