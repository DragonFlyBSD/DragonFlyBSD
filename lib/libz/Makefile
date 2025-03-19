#
# $FreeBSD: src/lib/libz/Makefile,v 1.11.2.4 2003/02/01 13:33:12 sobomax Exp $
#

LIB=		z
SHLIBDIR?=	/lib
MAN=		zlib.3
WARNS?=		2

ZLIBDIR=	${.CURDIR}/../../contrib/zlib-1.2

VERSION_DEF=	${.CURDIR}/Versions.def
SYMBOL_MAPS=	${.CURDIR}/Symbol.map

.PATH: ${ZLIBDIR}

SRCS=	adler32.c compress.c crc32.c deflate.c \
	gzclose.c gzlib.c gzread.c gzwrite.c \
	infback.c inffast.c inflate.c inftrees.c \
	trees.c uncompr.c zutil.c

CFLAGS+=-I${ZLIBDIR} -I${.CURDIR}
CFLAGS+=-DHAVE_HIDDEN
#CFLAGS+=-DZLIB_DEBUG

INCS=		zconf.h zlib.h

FILES=		zlib.pc
FILESDIR=	${LIBDATADIR}/pkgconfig

.include <bsd.lib.mk>
