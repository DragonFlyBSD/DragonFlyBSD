# $FreeBSD: src/sys/modules/libiconv/Makefile,v 1.4 2003/09/26 20:26:24 fjoe Exp $
# $DragonFly: src/sys/libiconv/Makefile,v 1.1 2004/03/16 07:45:59 hmp Exp $

.PATH: ${.CURDIR}/../libkern ${.CURDIR}/../sys

KMOD=	libiconv
SRCS=	iconv.c iconv_xlat.c
SRCS+=	iconv.h 
SRCS+=	iconv_converter_if.c iconv_converter_if.h
MFILES=	libkern/iconv_converter_if.m

EXPORT_SYMS=	iconv_open	\
		iconv_close	\
		iconv_conv	\
		iconv_conv_case	\
		iconv_convchr	\
		iconv_convchr_case	\
		iconv_convstr	\
		iconv_convmem	\
		iconv_vfs_refcount

.include <bsd.kmod.mk>
