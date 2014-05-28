# $FreeBSD: src/usr.sbin/wpa/hostapd_cli/Makefile,v 1.4 2007/07/09 16:26:48 sam Exp $
# $DragonFly: src/usr.sbin/802_11/hostapd_cli/Makefile,v 1.2 2007/08/07 11:25:36 sephe Exp $

.include "${.CURDIR}/../Makefile.inc"
.PATH.c:        ${.CURDIR}/.. ${HOSTAPD_DISTDIR}/hostapd

PROG=	hostapd_cli
SRCS=	common.c edit.c eloop.c hostapd_cli.c os_unix.c wpa_ctrl.c wpa_debug.c

CFLAGS+= -DCONFIG_CTRL_IFACE
CFLAGS+= -DCONFIG_CTRL_IFACE_UNIX

MAN=	hostapd_cli.8

.include <bsd.prog.mk>
