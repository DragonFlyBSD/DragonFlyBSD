#
# $FreeBSD: src/lib/libz/Makefile,v 1.11.2.4 2003/02/01 13:33:12 sobomax Exp $
# $DragonFly: src/lib/libz/Makefile,v 1.9 2005/07/21 16:07:41 joerg Exp $
#

LIB=		z
MAN=		zlib.3
WARNS?=		2

CONTRIBDIR=	${.CURDIR}/../../contrib/zlib-1.2

.PATH: ${CONTRIBDIR}

SRCS=	adler32.c compress.c crc32.c deflate.c gzclose.c gzlib.c gzread.c
SRCS+=	gzwrite.c infback.c
SRCS+=	inffast.c inflate.c inftrees.c trees.c uncompr.c zopen.c zutil.c

CFLAGS+= -I${CONTRIBDIR}

INCS=		zconf.h zlib.h

.include <bsd.lib.mk>
