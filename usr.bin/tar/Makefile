# $DragonFly: src/usr.bin/tar/Makefile,v 1.14 2008/05/17 21:53:39 pavalos Exp $

PROG=	bsdtar
SRCS=	bsdtar.c getdate.y matching.c read.c tree.c util.c write.c
DPADD=	${LIBARCHIVE} ${LIBBZ2} ${LIBZ}
LDADD=	-larchive -lbz2 -lz
NOSHARED?=	yes

CONTRIBDIR=	${.CURDIR}/../../contrib/libarchive-2/tar
.PATH: ${CONTRIBDIR}
VERSION!=	cat ${CONTRIBDIR}/../version
CFLAGS+=	-I${.OBJDIR} -I${.CURDIR}/../../lib/libarchive -I${CONTRIBDIR}
CFLAGS+=	-DPLATFORM_CONFIG_H=\"config.h\"
CFLAGS+=	-DBSDTAR_VERSION_STRING=\"${VERSION}\"

SYMLINKS=	${BINDIR}/bsdtar ${BINDIR}/tar
MLINKS=	bsdtar.1 tar.1

.include <bsd.prog.mk>
