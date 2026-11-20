# $FreeBSD: head/sys/modules/iwnfw/Makefile.inc 208591 2010-05-27 12:54:42Z rpaulo $
#
# Common rules for building firmware.  Note this gets auto-included
# by the subdir Makefile's as a consequence of included bsd.kmod.mk.

.include <bsd.dragonfly.mk>

_FIRM=	${IMG}.fw

CLEANFILES+=	${_FIRM}

FIRMWS=	${_FIRM}:${KMOD}

# Note that a license ack is not needed for iwn.
#FIRMWARE_LICENSE=

${_FIRM}: ${DF.SRCTOP}/sys/contrib/dev/iwn/${_FIRM}.uu
	${NORMAL_FW}
