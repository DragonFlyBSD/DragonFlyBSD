# $DragonFly: src/usr.bin/tar/Makefile,v 1.3 2006/09/18 01:47:10 pavalos Exp $

PROG=	bsdtar
SRCS=	bsdtar.c getdate.y matching.c read.c tree.c util.c write.c
WARNS?=	6
DPADD=	${LIBARCHIVE} ${LIBBZ2} ${LIBZ}
LDADD=	-larchive -lbz2 -lz
NOSHARED?=	yes

PATCHES!=	echo ${.CURDIR}/patches/*.patch
CONTRIBDIR=	${.CURDIR}/../../contrib/libarchive-1.3.1/tar
.PATH: ${CONTRIBDIR}
SRCS+=	${PATCHES}
NAME=		bsdtar
VERSION=	1.3.1
CFLAGS+=	-I${.OBJDIR} -I${.CURDIR}/../../lib/libarchive -I${CONTRIBDIR} -I-
CFLAGS+=	-DHAVE_CONFIG_H

.if defined(WITH_BSDTAR)
SYMLINKS=	${BINDIR}/bsdtar ${BINDIR}/tar
MLINKS=	bsdtar.1 tar.1
.endif

.include <bsd.prog.mk>
