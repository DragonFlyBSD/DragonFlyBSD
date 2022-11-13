# $FreeBSD: src/usr.bin/less/Makefile,v 1.4.2.5 2003/02/02 10:04:50 johan Exp $

PROG=	less
SRCS=	main.c screen.c brac.c ch.c charset.c cmdbuf.c \
	command.c cvt.c decode.c edit.c filename.c forwback.c \
	help.c ifile.c input.c jump.c lesskey_parse.c line.c linenum.c \
	lsystem.c mark.c optfunc.c option.c opttbl.c os.c \
	output.c pattern.c position.c prompt.c search.c signal.c \
	tags.c ttyin.c version.c xbuf.c
CFLAGS+=	-I${.CURDIR} -DSYSDIR=\"/etc\"
DPADD=	${LIBNCURSES}
LDADD=	-lprivate_ncurses

CFLAGS+=	-I${_SHLIBDIRPREFIX}/usr/include/priv/ncurses
LDFLAGS+=	${PRIVATELIB_LDFLAGS}

SCRIPTS= lesspipe.sh zless.sh
SCRIPTSNAME_lesspipe.sh=lesspipe.sh

LINKS=	${BINDIR}/less ${BINDIR}/more \
	${BINDIR}/zless ${BINDIR}/bzless \
	${BINDIR}/zless ${BINDIR}/xzless \
	${BINDIR}/zless ${BINDIR}/lzless \
	${BINDIR}/zless ${BINDIR}/zstdless
MLINKS=	less.1 more.1 \
	less.1 zless.1 \
	less.1 bzless.1 \
	less.1 xzless.1 \
	less.1 lzless.1 \
	less.1 zstdless.1

CLEANFILES= less.1

WARNS?=	2

.include "Makefile.common"
.include <bsd.prog.mk>
