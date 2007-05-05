# $DragonFly: src/usr.bin/tar/Makefile,v 1.10 2007/05/05 21:16:57 pavalos Exp $

PROG=	bsdtar
SRCS=	bsdtar.c getdate.y matching.c read.c tree.c util.c write.c
WARNS?=	6
DPADD=	${LIBARCHIVE} ${LIBBZ2} ${LIBZ}
LDADD=	-larchive -lbz2 -lz
NOSHARED?=	yes

CONTRIBDIR=	${.CURDIR}/../../contrib/libarchive-2.1/tar
.PATH: ${CONTRIBDIR}
VERSION!=	cat ${CONTRIBDIR}/../version
CFLAGS+=	-I${.OBJDIR} -I${.CURDIR}/../../lib/libarchive -I${CONTRIBDIR} -I-
CFLAGS+=	-DPLATFORM_CONFIG_H=\"config.h\"
CFLAGS+=	-DPACKAGE_STRING=\"libarchive\ ${VERSION}\"
CFLAGS+=	-DPACKAGE_VERSION=\"${VERSION}\"
CFLAGS+=	-DVERSION=\"${VERSION}\"

SYMLINKS=	${BINDIR}/bsdtar ${BINDIR}/tar
MLINKS=	bsdtar.1 tar.1

.include <bsd.prog.mk>
