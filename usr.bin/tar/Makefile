# $DragonFly: src/usr.bin/tar/Makefile,v 1.1 2004/11/10 18:02:24 joerg Exp $

PROG=	bsdtar
SRCS=	bsdtar.c matching.c read.c util.c write.c
WARNS?=	6
DPADD=	${LIBARCHIVE} ${LIBBZ2} ${LIBZ}
LDADD=	-larchive -lbz2 -lz
NOSHARED?=	yes

PATCHES!=	echo ${.CURDIR}/patches/*.patch
CONTRIBDIR=	${.CURDIR}/../../contrib/bsdtar
.PATH: ${CONTRIBDIR}
SRCS+=	${PATCHES}
NAME=		bsdtar
VERSION=	1.01.015
CFLAGS+=	-DPACKAGE_VERSION=\"${VERSION}\" -DPACKAGE_NAME=\"${NAME}\"
CFLAGS+=	-I${.CURDIR} -I${CONTRIBDIR} -DHAVE_CONFIG_H

.if defined(WITH_BSDTAR)
SYMLINKS=	${BINDIR}/bsdtar ${BINDIR}/tar
MLINKS=	bsdtar.1 tar.1
.endif

.include <bsd.prog.mk>
