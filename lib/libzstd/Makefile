# $FreeBSD$

LIB=		private_zstd
SHLIBDIR?=	/lib
WARNS?=		4
PRIVATELIB=

SRCS=	entropy_common.c \
	error_private.c \
	fse_decompress.c \
	pool.c \
	threading.c \
	xxhash.c \
	zstd_common.c \
	fse_compress.c \
	huf_compress.c \
	zstd_compress.c \
	zstd_compress_literals.c \
	zstd_compress_sequences.c \
	zstd_compress_superblock.c \
	zstdmt_compress.c \
	huf_decompress.c \
	zstd_ddict.c \
	zstd_decompress.c \
	zstd_decompress_block.c \
	zbuff_common.c \
	zbuff_compress.c \
	zbuff_decompress.c \
	cover.c \
	divsufsort.c \
	zdict.c \
	zstd_fast.c \
	zstd_lazy.c \
	zstd_ldm.c \
	zstd_opt.c \
	zstd_double_fast.c \
	debug.c \
	hist.c \
	fastcover.c

INCS=		zstd.h
INCSDIR=	${INCLUDEDIR}/priv/zstd

CFLAGS+=	-I${ZSTDDIR}/lib -I${ZSTDDIR}/lib/common -DXXH_NAMESPACE=ZSTD_ \
		-fvisibility=hidden

ZSTDDIR=	${.CURDIR}/../../contrib/zstd
.PATH:	${ZSTDDIR}/lib/common ${ZSTDDIR}/lib/compress \
	${ZSTDDIR}/lib/decompress ${ZSTDDIR}/lib/deprecated \
	${ZSTDDIR}/lib/dictBuilder ${ZSTDDIR}/lib

VERSION_DEF=	${.CURDIR}/Versions.def
SYMBOL_MAPS=	${.CURDIR}/Symbol.map
#FILES=		zlib.pc
#FILESDIR=	${LIBDATADIR}/pkgconfig

# https://github.com/facebook/zstd/commit/812e8f2a [zstd 1.4.1]
# "Note that [GCC] autovectorization still does not do a good job on the
# optimized version, so it's turned off via attribute and flag.  I found
# that neither attribute nor command-line flag were entirely successful in
# turning off vectorization, which is why there were both."
.if (${CCVER:Mgcc*})
FLAGS_GROUPS=	autovec
autovec_FLAGS=	-fno-tree-vectorize
autovec_FLAGS_FILES=zstd_decompress_block.c
.endif

.include <bsd.lib.mk>
