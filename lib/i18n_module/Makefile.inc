# $FreeBSD: head/lib/libiconv_modules/Makefile.inc 255298 2013-09-06 10:40:38Z theraven $

.PATH: ${.CURDIR}/../../libc/iconv

SHLIB_MAJOR= 	2
WARNS?=		6
CFLAGS+= 	-I${.CURDIR}/../../libc/citrus

CFLAGS+=	-Dbool=_Bool

LIBDIR=	 	/usr/lib/i18n
NOPROFILE=	yes
