# $DragonFly: src/usr.bin/tar/Makefile,v 1.12 2007/08/27 16:50:59 pavalos Exp $

PROG=	bsdtar
SRCS=	bsdtar.c getdate.y matching.c read.c tree.c util.c write.c
DPADD=	${LIBARCHIVE} ${LIBBZ2} ${LIBZ}
LDADD=	-larchive -lbz2 -lz
NOSHARED?=	yes

CONTRIBDIR=	${.CURDIR}/../../contrib/libarchive-2/tar
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
