# $NetBSD: Makefile,v 1.3 2007/05/28 12:06:18 tls Exp $

SYSDIR= ${.CURDIR}/../../sys/

LIB=		bluetooth
SHLIB_MAJOR=	3
SRCS=		bluetooth.c devaddr.c
MAN=		bluetooth.3

CFLAGS+=	-I${.CURDIR} -I${SYSDIR}

INCS=		bluetooth.h
INCSDIR=	/usr/include

MLINKS+=	bluetooth.3 bt_gethostbyname.3
MLINKS+=	bluetooth.3 bt_gethostbyaddr.3
MLINKS+=	bluetooth.3 bt_gethostent.3
MLINKS+=	bluetooth.3 bt_sethostent.3
MLINKS+=	bluetooth.3 bt_endhostent.3

MLINKS+=	bluetooth.3 bt_getprotobyname.3
MLINKS+=	bluetooth.3 bt_getprotobynumber.3
MLINKS+=	bluetooth.3 bt_getprotoent.3
MLINKS+=	bluetooth.3 bt_setprotoent.3
MLINKS+=	bluetooth.3 bt_endprotoent.3

MLINKS+=	bluetooth.3 bt_ntoa.3
MLINKS+=	bluetooth.3 bt_aton.3

MLINKS+=	bluetooth.3 bt_devaddr.3
MLINKS+=	bluetooth.3 bt_devname.3

CFLAGS+=	-I${.CURDIR} -I${SYSDIR}

.include <bsd.lib.mk>
