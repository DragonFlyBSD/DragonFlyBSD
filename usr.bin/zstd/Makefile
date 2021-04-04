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
SCRIPTS=zstdgrep
MAN=	zstd.1 \
	zstdgrep.1

# Subset of libzstd with threading enabled
ZSMT_SRCS=	cover.c divsufsort.c entropy_common.c error_private.c
ZSMT_SRCS+=	fastcover.c fse_compress.c fse_decompress.c hist.c
ZSMT_SRCS+=	huf_compress.c huf_decompress.c pool.c threading.c xxhash.c
ZSMT_SRCS+=	zdict.c zstd_common.c zstd_compress.c zstdmt_compress.c
ZSMT_SRCS+=	zstd_compress_literals.c zstd_compress_sequences.c
ZSMT_SRCS+=	zstd_compress_superblock.c zstd_decompress.c
ZSMT_SRCS+=	zstd_decompress_block.c zstd_ddict.c zstd_double_fast.c
ZSMT_SRCS+=	zstd_fast.c zstd_lazy.c zstd_ldm.c zstd_opt.c

ZSTDCONTRIB=	${.CURDIR}/../../contrib/zstd

CFLAGS+=	-I${ZSTDCONTRIB}/programs \
		-I${ZSTDCONTRIB}/lib/common \
		-I${ZSTDCONTRIB}/lib/compress \
		-I${ZSTDCONTRIB}/lib/dictBuilder \
		-I${ZSTDCONTRIB}/lib \
		-DXXH_NAMESPACE=ZSTD_

LINKS=	${BINDIR}/zstd ${BINDIR}/unzstd \
	${BINDIR}/zstd ${BINDIR}/zstdcat \
	${BINDIR}/zstd ${BINDIR}/zstdmt
MLINKS=	zstd.1 unzstd.1 \
	zstd.1 zstdcat.1 \
	zstd.1 zstdmt.1

WARNS?=	2

#.if defined(RESCUE)
#CFLAGS+=	${PRIVATELIB_CFLAGS}
#DPADD=  ${LIBZSTD}
#LDADD=  -lprivate_zstd
#LDFLAGS+=	${PRIVATELIB_LDFLAGS}
#.else
.PATH: ${ZSTDCONTRIB}/lib/common
.PATH: ${ZSTDCONTRIB}/lib/compress
.PATH: ${ZSTDCONTRIB}/lib/decompress
.PATH: ${ZSTDCONTRIB}/lib/dictBuilder
SRCS+=	${ZSMT_SRCS}
CFLAGS+=-DZSTD_MULTITHREAD=1
DPADD+=	${LIBPTHREAD}
LDADD+=	-lpthread
#.endif

.PATH: ${ZSTDCONTRIB}/programs

.include <bsd.prog.mk>
