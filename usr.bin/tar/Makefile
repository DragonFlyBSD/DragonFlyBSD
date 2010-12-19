# $DragonFly: src/usr.bin/tar/Makefile,v 1.16 2008/07/12 14:57:33 pavalos Exp $

PROG=	bsdtar
SRCS=	bsdtar.c cmdline.c err.c getdate.c line_reader.c matching.c \
	pathmatch.c read.c subst.c \
	tree.c util.c write.c
DPADD=	${LIBARCHIVE} ${LIBBZ2} ${LIBLZMA} ${LIBMD} ${LIBZ}
LDADD=	-larchive -lbz2 -llzma -lmd -lz

CONTRIBDIR=	${.CURDIR}/../../contrib/libarchive/tar
.PATH: ${CONTRIBDIR} ${CONTRIBDIR}/../libarchive_fe
LIBARCHIVE_VERSION!=	cat ${CONTRIBDIR}/../build/version
VERSION!=	echo $$((${LIBARCHIVE_VERSION} / 1000000)).$$((${LIBARCHIVE_VERSION} / 1000 % 1000)).$$((${LIBARCHIVE_VERSION} % 1000))
CFLAGS+=	-I${.CURDIR}/../../lib/libarchive -I${CONTRIBDIR} -I${CONTRIBDIR}/../libarchive_fe
CFLAGS+=	-DPLATFORM_CONFIG_H=\"config.h\"
CFLAGS+=	-DBSDTAR_VERSION_STRING=\"${VERSION}\"

SYMLINKS=	${BINDIR}/bsdtar ${BINDIR}/tar
MLINKS=	bsdtar.1 tar.1

.include <bsd.prog.mk>
