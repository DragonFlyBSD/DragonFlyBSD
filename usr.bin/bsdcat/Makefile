PROG=	bsdcat
SRCS=	bsdcat.c cmdline.c
SRCS+=	err.c
DPADD=	${LIBARCHIVE} ${LIBBZ2} ${LIBLZMA} ${LIBMD} ${LIBZ}
LDADD=	-larchive -lbz2 -llzma -lmd -lz

CONTRIBDIR=	${.CURDIR}/../../contrib/libarchive/cat
.PATH: ${CONTRIBDIR} ${CONTRIBDIR}/../libarchive_fe
LIBARCHIVE_VERSION!=	cat ${CONTRIBDIR}/../build/version
VERSION!=	echo $$((${LIBARCHIVE_VERSION} / 1000000)).$$((${LIBARCHIVE_VERSION} / 1000 % 1000)).$$((${LIBARCHIVE_VERSION} % 1000))
CFLAGS+=	-I${.CURDIR}/../../lib/libarchive -I${CONTRIBDIR} -I${CONTRIBDIR}/../libarchive_fe
CFLAGS+=	-DPLATFORM_CONFIG_H=\"config.h\"
CFLAGS+=	-DBSDCAT_VERSION_STRING=\"${VERSION}\"

.include <bsd.prog.mk>
