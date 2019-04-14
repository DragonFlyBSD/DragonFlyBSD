LIB=	lzma
LZMADIR=	${.CURDIR}/../../contrib/xz/src/liblzma

.PATH: ${LZMADIR}/api/lzma
MAININCS=	../lzma.h
LZMAINCS=	base.h \
		bcj.h \
		block.h \
		check.h \
		container.h \
		delta.h \
		filter.h \
		hardware.h \
		index.h \
		index_hash.h \
		lzma12.h \
		stream_flags.h \
		version.h \
		vli.h
LZMAINCSDIR=	${INCLUDEDIR}/lzma
INCSGROUPS=	MAININCS LZMAINCS

.PATH: ${LZMADIR}/../common
SRCS+=	tuklib_physmem.c

.PATH: ${LZMADIR}/common
SRCS+=	alone_decoder.c \
	alone_encoder.c \
	auto_decoder.c \
	block_buffer_decoder.c \
	block_buffer_encoder.c \
	block_decoder.c \
	block_encoder.c \
	block_header_decoder.c \
	block_header_encoder.c \
	block_util.c \
	common.c \
	easy_buffer_encoder.c \
	easy_decoder_memusage.c \
	easy_encoder.c \
	easy_encoder_memusage.c \
	easy_preset.c \
	filter_buffer_decoder.c \
	filter_buffer_encoder.c \
	filter_common.c \
	filter_decoder.c \
	filter_encoder.c \
	filter_flags_decoder.c \
	filter_flags_encoder.c \
	hardware_physmem.c \
	index.c \
	index_decoder.c \
	index_encoder.c \
	index_hash.c \
	stream_buffer_decoder.c \
	stream_buffer_encoder.c \
	stream_decoder.c \
	stream_encoder.c \
	stream_flags_common.c \
	stream_flags_decoder.c \
	stream_flags_encoder.c \
	vli_decoder.c \
	vli_encoder.c \
	vli_size.c

# only for multi-threaded version (used directly in usr.bin/xz)
#SRCS+= tuklib_cpucores.c hardware_cputhreads.c outqueue.c stream_encoder_mt.c

.PATH: ${LZMADIR}/check
SRCS+=	check.c \
	crc32_table.c \
	crc64_table.c \
	sha256.c

# For x86_64 (other arch could have their own versions)
SRCS+=	crc32_fast.c crc64_fast.c

.PATH: ${LZMADIR}/lz
SRCS+=	lz_decoder.c \
	lz_encoder.c \
	lz_encoder_mf.c

.PATH: ${LZMADIR}/lzma
SRCS+=	fastpos_table.c \
	lzma_decoder.c \
	lzma_encoder.c \
	lzma_encoder_presets.c \
	lzma_encoder_optimum_fast.c \
	lzma_encoder_optimum_normal.c \
	lzma2_decoder.c \
	lzma2_encoder.c

.PATH: ${LZMADIR}/rangecoder
SRCS+=	price_table.c

.PATH: ${LZMADIR}/delta
SRCS+=	delta_common.c \
	delta_decoder.c \
	delta_encoder.c

.PATH: ${LZMADIR}/simple
SRCS+=	simple_coder.c \
	simple_decoder.c \
	simple_encoder.c \
	arm.c \
	armthumb.c \
	ia64.c \
	powerpc.c \
	sparc.c \
	x86.c

WARNS?=	3

CFLAGS+= -DHAVE_CONFIG_H -DTUKLIB_SYMBOL_PREFIX=lzma_ \
	-I${.CURDIR} \
	-I${LZMADIR}/api \
	-I${LZMADIR}/common \
	-I${LZMADIR}/check \
	-I${LZMADIR}/lz \
	-I${LZMADIR}/rangecoder \
	-I${LZMADIR}/lzma \
	-I${LZMADIR}/delta \
	-I${LZMADIR}/simple \
	-I${LZMADIR}/../common

VERSION_DEF=	${.CURDIR}/Versions.def
SYMBOL_MAPS=	${.CURDIR}/Symbol.map

.include <bsd.lib.mk>
