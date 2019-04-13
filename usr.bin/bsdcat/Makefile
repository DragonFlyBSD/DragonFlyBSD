PROG=	bsdcat
SRCS=	bsdcat.c cmdline.c
SRCS+=	err.c

WARNS?=	2
DPADD=	${LIBARCHIVE} ${LIBBZ2} ${LIBLZMA} ${LIBZ}
LDADD=	-larchive -lbz2 -llzma -lz

CONTRIBDIR=	${.CURDIR}/../../contrib/libarchive/cat
.PATH: ${CONTRIBDIR} ${CONTRIBDIR}/../libarchive_fe
CFLAGS+=	-DHAVE_CONFIG_H -I${.CURDIR}/../../lib/libarchive
CFLAGS+=	-I${CONTRIBDIR}
CFLAGS+=	-I${CONTRIBDIR}/../libarchive -I${CONTRIBDIR}/../libarchive_fe

.include <bsd.prog.mk>
