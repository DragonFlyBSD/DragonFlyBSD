# $DragonFly: src/usr.bin/tar/Makefile,v 1.7 2007/03/26 00:03:44 pavalos Exp $

PROG=	bsdtar
SRCS=	bsdtar.c getdate.y matching.c read.c tree.c util.c write.c
WARNS?=	6
DPADD=	${LIBARCHIVE} ${LIBBZ2} ${LIBZ}
LDADD=	-larchive -lbz2 -lz
NOSHARED?=	yes

PATCHES!=	echo ${.CURDIR}/patches/*.patch
CONTRIBDIR=	${.CURDIR}/../../contrib/libarchive-2.0/tar
.PATH: ${CONTRIBDIR}
SRCS+=	${PATCHES}
CFLAGS+=	-I${.OBJDIR} -I${.CURDIR}/../../lib/libarchive -I${CONTRIBDIR} -I-
CFLAGS+=	-DHAVE_CONFIG_H

SYMLINKS=	${BINDIR}/bsdtar ${BINDIR}/tar
MLINKS=	bsdtar.1 tar.1

.include <bsd.prog.mk>
