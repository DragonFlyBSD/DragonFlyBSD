# $FreeBSD: head/lib/libkiconv/Makefile 298107 2016-04-16 07:45:30Z gjb $

LIB=		kiconv
SRCS=		kiconv_sysctl.c xlat16_iconv.c xlat16_sysctl.c
SRCS+=		quirks.c
WARNS?=		3

SHLIB_MAJOR=	3
SHLIBDIR?=	/lib

MAN=		kiconv.3

MLINKS+=	kiconv.3 kiconv_add_xlat16_cspair.3 \
		kiconv.3 kiconv_add_xlat16_cspairs.3 \
		kiconv.3 kiconv_add_xlat16_table.3

CFLAGS+=	-I${.CURDIR}/../../sys

.include <bsd.lib.mk>
