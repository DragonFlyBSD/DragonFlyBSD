# $FreeBSD: src/lib/libbz2/Makefile,v 1.2.2.2 2002/07/22 14:21:49 ru Exp $
# $DragonFly: src/lib/libbz2/Makefile,v 1.4 2005/04/21 13:42:33 joerg Exp $

BZ2DIR=	${.CURDIR}/../../contrib/bzip2
.PATH: ${BZ2DIR}

LIB=		bz2
SHLIB_MAJOR=	2
SRCS=		bzlib.c blocksort.c compress.c crctable.c decompress.c \
		huffman.c randtable.c
INCS=		bzlib.h
CFLAGS+=	-I${BZ2DIR}

.include <bsd.lib.mk>
