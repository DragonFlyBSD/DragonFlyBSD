PROG=	bsdcpio
SRCS=	cmdline.c cpio.c
SRCS+=	err.c line_reader.c passphrase.c
DPADD=	${LIBARCHIVE} ${LIBBZ2} ${LIBLZMA} ${LIBCRYPTO} ${LIBZ} ${LIBZSTD}
LDADD=	-larchive -lbz2 -llzma -lprivate_crypto -lz -lprivate_zstd
LDFLAGS+=	${PRIVATELIB_LDFLAGS}

CONTRIBDIR=	${.CURDIR}/../../contrib/libarchive/cpio
.PATH: ${CONTRIBDIR} ${CONTRIBDIR}/../libarchive_fe
CFLAGS+=	-DHAVE_CONFIG_H -I${.CURDIR}/../../lib/libarchive
CFLAGS+=	-I${CONTRIBDIR}
CFLAGS+=	-I${CONTRIBDIR}/../libarchive -I${CONTRIBDIR}/../libarchive_fe
CFLAGS+=	${PRIVATELIB_CFLAGS}

SYMLINKS=	bsdcpio ${BINDIR}/cpio
MLINKS=	bsdcpio.1 cpio.1

.include <bsd.prog.mk>
