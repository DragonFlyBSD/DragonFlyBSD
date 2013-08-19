# $FreeBSD: src/sys/modules/usb/urtwnfw/Makefile.inc,v 1.1 2013/07/11 00:28:45 svnexp Exp $
#
# Common rules for building firmware.  Note this gets auto-included
# by the subdir Makefile's as a consequence of included bsd.kmod.mk.

_FIRM=	${IMG}.fw

CLEANFILES+=	${_FIRM}

FIRMWS=	${_FIRM}:${KMOD}:111

FIRMWARE_LICENSE=	realtek

${_FIRM}: ${.CURDIR}/../../../../../contrib/dev/urtwn/${_FIRM}.uu
	uudecode -p $? > ${.TARGET}
