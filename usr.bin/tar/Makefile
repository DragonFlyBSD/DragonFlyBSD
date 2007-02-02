# $DragonFly: src/usr.bin/tar/Makefile,v 1.5 2007/02/02 06:33:17 pavalos Exp $

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

.include <bsd.prog.mk>
