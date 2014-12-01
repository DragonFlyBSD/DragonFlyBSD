# $FreeBSD: src/lib/libbz2/Makefile,v 1.2.2.2 2002/07/22 14:21:49 ru Exp $

BZ2DIR=	${.CURDIR}/../../contrib/bzip2
.PATH: ${BZ2DIR}

LIB=		bz2
SHLIB_MAJOR=	2
SRCS=		bzlib.c blocksort.c compress.c crctable.c decompress.c \
		huffman.c randtable.c
INCS=		bzlib.h
CFLAGS+=	-I${BZ2DIR}
WARNS?=		3

.if defined(RELEASE36)
VERSION_DEF=	${.CURDIR}/Versions.def
SYMBOL_MAPS=	${.CURDIR}/Symbol.map
.endif

.include <bsd.lib.mk>
