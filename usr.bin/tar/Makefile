# $DragonFly: src/usr.bin/tar/Makefile,v 1.16 2008/07/12 14:57:33 pavalos Exp $

PROG=	bsdtar
SRCS=	bsdtar.c getdate.y matching.c read.c siginfo.c subst.c \
	tree.c util.c write.c
DPADD=	${LIBARCHIVE} ${LIBBZ2} ${LIBZ}
LDADD=	-larchive -lbz2 -lz

CONTRIBDIR=	${.CURDIR}/../../contrib/libarchive-2/tar
.PATH: ${CONTRIBDIR}
LIBARCHIVE_VERSION!=	cat ${CONTRIBDIR}/../version
VERSION!=	echo $$((${LIBARCHIVE_VERSION} / 1000000)).$$((${LIBARCHIVE_VERSION} / 1000 % 1000)).$$((${LIBARCHIVE_VERSION} % 1000))
CFLAGS+=	-I${.OBJDIR} -I${.CURDIR}/../../lib/libarchive -I${CONTRIBDIR}
CFLAGS+=	-DPLATFORM_CONFIG_H=\"config.h\"
CFLAGS+=	-DBSDTAR_VERSION_STRING=\"${VERSION}\"

SYMLINKS=	${BINDIR}/bsdtar ${BINDIR}/tar
MLINKS=	bsdtar.1 tar.1

.include <bsd.prog.mk>
