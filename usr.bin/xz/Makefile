PROG=	xz
MAN=	xz.1

LINKS=	${BINDIR}/xz ${BINDIR}/unxz
LINKS+=	${BINDIR}/xz ${BINDIR}/xzcat
LINKS+=	${BINDIR}/xz ${BINDIR}/lzma
LINKS+=	${BINDIR}/xz ${BINDIR}/unlzma
LINKS+=	${BINDIR}/xz ${BINDIR}/lzcat
MLINKS=	xz.1 unxz.1 \
	xz.1 xzcat.1 \
	xz.1 lzma.1 \
	xz.1 unlzma.1 \
	xz.1 lzcat.1

XZDIR=	${.CURDIR}/../../contrib/xz/src

.PATH: ${XZDIR}/xz
SRCS=	main.c args.c coder.c file_io.c hardware.c message.c \
	mytime.c list.c options.c signals.c suffix.c util.c

.PATH: ${XZDIR}/common
SRCS+=	tuklib_exit.c tuklib_mbstr_fw.c tuklib_mbstr_width.c \
	tuklib_open_stdxxx.c tuklib_progname.c

WARNS?=	3

CFLAGS+= -I${XZDIR}/common
LIBLZMADIR=	${.CURDIR}/../../lib/liblzma

#
# Use non threaded variant for crunchgen rescue
.if defined(RESCUE)
CFLAGS+= -DHAVE_CONFIG_H -I${.CURDIR}/../../lib/liblzma
DPADD=	${LIBLZMA}
LDADD=	-llzma
.else
# NOTE: different config than lib/liblzma/config.h
CFLAGS+=	-DHAVE_CONFIG_H -I${.CURDIR}
CFLAGS+=	-I${LZMADIR}/api -I${LZMADIR}/api/lzma
LZMADIR=	${XZDIR}/liblzma
SRCS+=		${XZMT_SRCS}
FLAGS_GROUPS=	threaded
threaded_FLAGS=	-DTUKLIB_SYMBOL_PREFIX=lzma_ -I${LZMADIR}/common
threaded_FLAGS+=-I${LZMADIR}/check -I${LZMADIR}/lz -I${LZMADIR}/lzma
threaded_FLAGS+=-I${LZMADIR}/rangecoder -I${LZMADIR}/delta -I${LZMADIR}/simple
threaded_FLAGS_FILES+=	${XZMT_SRCS}
.PATH: ${LZMADIR}/common ${LZMADIR}/check ${LZMADIR}/lz ${LZMADIR}/lzma
.PATH: ${LZMADIR}/rangecoder ${LZMADIR}/delta ${LZMADIR}/simple

DPADD+=	${LIBPTHREAD}
LDADD+=	-lpthread
.endif

# Subset of liblzma with threading enabled.
XZMT_SRCS=	tuklib_cpucores.c tuklib_physmem.c
XZMT_SRCS+=	hardware_cputhreads.c outqueue.c stream_encoder_mt.c
XZMT_SRCS+= 	alone_decoder.c alone_encoder.c block_buffer_encoder.c \
		block_header_decoder.c block_header_encoder.c block_decoder.c \
		block_encoder.c block_util.c common.c easy_preset.c index.c \
		index_decoder.c index_encoder.c index_hash.c
XZMT_SRCS+=	filter_common.c filter_decoder.c filter_encoder.c \
		filter_flags_decoder.c filter_flags_encoder.c
XZMT_SRCS+=	stream_flags_common.c hardware_physmem.c stream_decoder.c \
		stream_encoder.c stream_flags_decoder.c stream_flags_encoder.c \
		vli_decoder.c vli_encoder.c vli_size.c sha256.c
XZMT_SRCS+=	check.c crc32_fast.c crc64_fast.c crc32_table.c crc64_table.c
XZMT_SRCS+=	lz_decoder.c lz_encoder.c lz_encoder_mf.c
XZMT_SRCS+=	fastpos_table.c lzma_decoder.c lzma_encoder.c \
		lzma_encoder_presets.c lzma_encoder_optimum_fast.c \
		lzma_encoder_optimum_normal.c lzma2_decoder.c lzma2_encoder.c
XZMT_SRCS+=	price_table.c
XZMT_SRCS+=	delta_common.c delta_encoder.c delta_decoder.c
XZMT_SRCS+=	simple_coder.c simple_decoder.c simple_encoder.c \
		arm.c armthumb.c ia64.c powerpc.c sparc.c x86.c

.PATH: ${XZDIR}/scripts
SCRIPTS=	xzdiff xzgrep xzless xzmore
MAN+=	${SCRIPTS:S/$/.1/g}
PKG_NAME!=	grep -w PACKAGE_NAME ${LIBLZMADIR}/config.h | awk -F\" '{print $$2}'
VERSION!=	grep -w VERSION ${LIBLZMADIR}/config.h | awk -F\" '{print $$2}'
PKG_BUG!=	grep -w PACKAGE_BUGREPORT ${LIBLZMADIR}/config.h | awk -F\" '{print $$2}'

${SCRIPTS}:	${.TARGET}.in
	sed -e s,@POSIX_SHELL@,/bin/sh,g	\
	    -e s,@xz@,xz,g			\
	    -e s,@PACKAGE_NAME@,${PKG_NAME:Q},g	\
	    -e s,@VERSION@,${VERSION:Q},g	\
	    -e s,@PACKAGE_BUGREPORT@,${PKG_BUG:Q},g ${.ALLSRC} > ${.TARGET}

CLEANFILES=	${SCRIPTS}
LINKS+=	${SCRIPTSDIR}/xzdiff ${SCRIPTSDIR}/xzcmp \
	${SCRIPTSDIR}/xzgrep ${SCRIPTSDIR}/xzegrep \
	${SCRIPTSDIR}/xzgrep ${SCRIPTSDIR}/xzfgrep \
	${SCRIPTSDIR}/xzdiff ${SCRIPTSDIR}/lzdiff \
	${SCRIPTSDIR}/xzdiff ${SCRIPTSDIR}/lzcmp \
	${SCRIPTSDIR}/xzgrep ${SCRIPTSDIR}/lzgrep \
	${SCRIPTSDIR}/xzgrep ${SCRIPTSDIR}/lzegrep \
	${SCRIPTSDIR}/xzgrep ${SCRIPTSDIR}/lzfgrep \
	${SCRIPTSDIR}/xzmore ${SCRIPTSDIR}/lzmore \
	${SCRIPTSDIR}/xzless ${SCRIPTSDIR}/lzless
MLINKS+=xzdiff.1 xzcmp.1 \
	xzgrep.1 xzegrep.1 \
	xzgrep.1 xzfgrep.1 \
	xzdiff.1 lzdiff.1 \
	xzdiff.1 lzcmp.1 \
	xzgrep.1 lzgrep.1 \
	xzgrep.1 lzegrep.1 \
	xzgrep.1 lzfgrep.1 \
	xzmore.1 lzmore.1 \
	xzless.1 lzless.1

.include <bsd.prog.mk>
