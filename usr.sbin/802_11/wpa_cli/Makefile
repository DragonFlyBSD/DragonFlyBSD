# $FreeBSD: src/usr.sbin/wpa/wpa_cli/Makefile,v 1.3 2007/07/11 16:04:07 sam Exp $

.include "${.CURDIR}/../Makefile.inc"

.PATH.c:	${.CURDIR}/..  \
		${WPA_DISTDIR}/wpa_supplicant
PROG=	wpa_cli
SRCS=	wpa_cli.c wpa_ctrl.c os_unix.c

MAN=	wpa_cli.8

WARNS=	0

CFLAGS+= -I${WPA_DISTDIR}
CFLAGS+= -DCONFIG_CTRL_IFACE
CFLAGS+= -DCONFIG_CTRL_IFACE_UNIX
CFLAGS+= -DCONFIG_READLINE

LDADD+= -ledit -ltermcap
DPADD+= ${LIBEDIT} ${LIBTERMCAP}

.include <bsd.prog.mk>
