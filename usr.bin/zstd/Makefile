# $FreeBSD$

PROG=	zstd
SRCS=	\
	benchfn.c \
	benchzstd.c \
	datagen.c \
	dibio.c \
	fileio.c \
	timefn.c \
	util.c \
	zstdcli.c

ZSTDCONTRIB=	${.CURDIR}/../../contrib/zstd

CFLAGS+=	-I${ZSTDCONTRIB}/programs \
		-I${ZSTDCONTRIB}/lib/common \
		-I${ZSTDCONTRIB}/lib/compress \
		-I${ZSTDCONTRIB}/lib/dictBuilder \
		-I${ZSTDCONTRIB}/lib \
		-DXXH_NAMESPACE=ZSTD_ \
		-DHAVE_THREAD=1
CFLAGS+=	${PRIVATELIB_CFLAGS}

#		-DZSTD_MULTITHREAD=1

LINKS=	${BINDIR}/zstd ${BINDIR}/unzstd \
	${BINDIR}/zstd ${BINDIR}/zstdcat \
	${BINDIR}/zstd ${BINDIR}/zstdmt
MLINKS=	zstd.1 unzstd.1 \
	zstd.1 zstdcat.1 \
	zstd.1 zstdmt.1

WARNS?=	2
DPADD=  ${LIBZSTD}
LDADD=  -lprivate_zstd
LDFLAGS+=	${PRIVATELIB_LDFLAGS}

.PATH: ${ZSTDCONTRIB}/programs

.include <bsd.prog.mk>
