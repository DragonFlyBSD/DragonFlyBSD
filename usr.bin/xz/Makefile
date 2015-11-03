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
SRCS=	args.c \
	coder.c \
	file_io.c \
	hardware.c \
	list.c \
	main.c \
	message.c \
	mytime.c \
	options.c \
	signals.c \
	suffix.c \
	util.c

.PATH: ${XZDIR}/common
SRCS+=	tuklib_open_stdxxx.c \
	tuklib_progname.c \
	tuklib_exit.c \
	tuklib_cpucores.c \
	tuklib_mbstr_width.c \
	tuklib_mbstr_fw.c

WARNS?=	3

LIBLZMADIR=	${.CURDIR}/../../lib/liblzma
CFLAGS+= -DHAVE_CONFIG_H
CFLAGS+= -I${LIBLZMADIR} -I${XZDIR}/common

DPADD=	${LIBLZMA}
LDADD=	-llzma

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
