# $FreeBSD: src/usr.sbin/wpa/wpa_cli/Makefile,v 1.3 2007/07/11 16:04:07 sam Exp $
# $DragonFly: src/usr.sbin/802_11/wpa_cli/Makefile,v 1.2 2007/08/07 11:25:36 sephe Exp $

CONTRIBDIR= ${.CURDIR}/../../../contrib/wpa_supplicant-0.5.8
.PATH: ${.CURDIR}/.. ${CONTRIBDIR}

PROG=	wpa_cli
SRCS=	wpa_cli.c wpa_ctrl.c os_unix.c
SRCS+=	os_unix.c.patch

CFLAGS+= -I${CONTRIBDIR}
CFLAGS+= -DCONFIG_CTRL_IFACE
CFLAGS+= -DCONFIG_CTRL_IFACE_UNIX

MAN=	wpa_cli.8

.include <bsd.prog.mk>
