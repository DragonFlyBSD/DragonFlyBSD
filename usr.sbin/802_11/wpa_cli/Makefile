# $FreeBSD: src/usr.sbin/wpa/wpa_cli/Makefile,v 1.3 2007/07/11 16:04:07 sam Exp $

.include "${.CURDIR}/../Makefile.inc"

.PATH.c:	${WPA_DISTDIR}/wpa_supplicant

PROG=	wpa_cli
SRCS=	common.c cli.c edit_simple.c eloop.c os_unix.c wpa_cli.c wpa_ctrl.c \
	wpa_debug.c

MAN=	wpa_cli.8

CFLAGS+= -DCONFIG_CTRL_IFACE
CFLAGS+= -DCONFIG_CTRL_IFACE_UNIX

.include <bsd.prog.mk>
