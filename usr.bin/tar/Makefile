PROG=	bsdtar
SRCS=	bsdtar.c cmdline.c creation_set.c read.c subst.c util.c write.c
SRCS+=	err.c line_reader.c passphrase.c
DPADD=	${LIBARCHIVE} ${LIBBZ2} ${LIBLZMA} ${LIBCRYPTO} ${LIBZ}
LDADD=	-larchive -lbz2 -llzma -lprivate_crypto -lz
LDFLAGS+=	${PRIVATELIB_LDFLAGS}

CONTRIBDIR=	${.CURDIR}/../../contrib/libarchive/tar
.PATH: ${CONTRIBDIR} ${CONTRIBDIR}/../libarchive_fe
CFLAGS+=	-DHAVE_CONFIG_H -DHAVE_DIRENT_D_NAMLEN
CFLAGS+=	-I${.CURDIR}/../../lib/libarchive
CFLAGS+=	-I${CONTRIBDIR}
CFLAGS+=	-I${CONTRIBDIR}/../libarchive  -I${CONTRIBDIR}/../libarchive_fe

SYMLINKS=	bsdtar ${BINDIR}/tar
MLINKS=	bsdtar.1 tar.1

.include <bsd.prog.mk>
