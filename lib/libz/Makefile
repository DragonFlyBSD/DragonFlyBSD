#
# $FreeBSD: src/lib/libz/Makefile,v 1.11.2.4 2003/02/01 13:33:12 sobomax Exp $
# $DragonFly: src/lib/libz/Makefile,v 1.5 2004/11/12 18:10:42 joerg Exp $
#

LIB=		z
MAN=		zlib.3
WARNS?=		3

PATCHES!=	echo ${.CURDIR}/patches/*.patch
CONTRIBDIR=	${.CURDIR}/../../contrib/zlib-1.2.2

.PATH: ${CONTRIBDIR}

#CFLAGS+=	-DMAX_WBITS=14 -DMAX_MEM_LEVEL=7
#CFLAGS+=	-g -DDEBUG
#CFLAGS+=	-Wall -Wwrite-strings -Wpointer-arith -Wconversion \
#		-Wstrict-prototypes -Wmissing-prototypes

CLEANFILES+=	example.o example foo.gz minigzip.o minigzip

SRCS=	adler32.c compress.c crc32.c deflate.c example.c gzio.c infback.c
SRCS+=	inffast.c inflate.c inftrees.c trees.c uncompr.c zopen.c zutil.c
SRCS+=	${PATCHES}

INCS=		zconf.h zlib.h

minigzip:	all minigzip.o
	$(CC) -o minigzip minigzip.o -L. -lz

example:	all example.o
	$(CC) -o example example.o -L. -lz

test: example minigzip
	(export LD_LIBRARY_PATH=. ; ./example )
	(export LD_LIBRARY_PATH=. ; \
		echo hello world | ./minigzip | ./minigzip -d )

.include <bsd.lib.mk>
