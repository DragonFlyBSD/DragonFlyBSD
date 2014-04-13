# $FreeBSD: src/lib/libipx/Makefile,v 1.3.6.1 2001/04/25 10:04:31 ru Exp $
# $DragonFly: src/lib/libipx/Makefile,v 1.3 2008/10/28 17:23:45 swildner Exp $

LIB=	ipx
SHLIBDIR?= /lib
#CFLAGS+=-I${.CURDIR}/../../sys
SRCS=	ipx_addr.c ipx_ntoa.c
WARNS?=	2

MAN=	ipx.3

MLINKS+=ipx.3 ipx_addr.3 ipx.3 ipx_ntoa.3

.include <bsd.lib.mk>
