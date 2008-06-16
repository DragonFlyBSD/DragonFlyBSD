# $DragonFly: src/usr.bin/tar/Makefile,v 1.15 2008/06/16 08:20:23 pavalos Exp $

PROG=	bsdtar
SRCS=	bsdtar.c getdate.y matching.c read.c siginfo.c subst.c \
	tree.c util.c write.c
DPADD=	${LIBARCHIVE} ${LIBBZ2} ${LIBZ}
LDADD=	-larchive -lbz2 -lz

CONTRIBDIR=	${.CURDIR}/../../contrib/libarchive-2/tar
.PATH: ${CONTRIBDIR}
VERSION=	2.5.4b
CFLAGS+=	-I${.OBJDIR} -I${.CURDIR}/../../lib/libarchive -I${CONTRIBDIR}
CFLAGS+=	-DPLATFORM_CONFIG_H=\"config.h\"
CFLAGS+=	-DBSDTAR_VERSION_STRING=\"${VERSION}\"

SYMLINKS=	${BINDIR}/bsdtar ${BINDIR}/tar
MLINKS=	bsdtar.1 tar.1

.include <bsd.prog.mk>
