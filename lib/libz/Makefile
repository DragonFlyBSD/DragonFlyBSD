#
# $FreeBSD: src/lib/libz/Makefile,v 1.11.2.4 2003/02/01 13:33:12 sobomax Exp $
#

LIB=		z
SHLIBDIR?=	/lib
MAN=		zlib.3
WARNS?=		3

ZLIBDIR=	${.CURDIR}/../../contrib/zlib

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

ZLIB_VERSION!=	sed -n '/define.*ZLIB_VERSION/{s,[^0-9.],,gp;q;}' \
		${ZLIBDIR}/zlib.h

zlib.pc: zlib.pc.in
	sed -e 's,@prefix@,/usr,g ; \
		s,@exec_prefix@,$${prefix},g ; \
		s,@libdir@,${LIBDIR},g ; \
		s,@sharedlibdir@,${SHLIBDIR},g ; \
		s,@includedir@,${INCLUDEDIR},g ; \
		s,@VERSION@,${ZLIB_VERSION},g ;' \
		${.ALLSRC} > ${.TARGET}

CLEANFILES+=	zlib.pc

.include <bsd.lib.mk>
