# $FreeBSD: src/gnu/usr.bin/tar/Makefile,v 1.11.6.4 2002/07/14 13:19:46 sobomax Exp $
# $DragonFly: src/gnu/usr.bin/tar/Attic/Makefile,v 1.3 2004/11/10 00:01:54 joerg Exp $

TARDIR=	${.CURDIR}/../../../contrib/tar
.PATH: ${TARDIR}/lib ${TARDIR}/src

PROG=	gtar
.if !defined(WITH_BSDTAR)
SYMLINKS=	${BINDIR}/gtar ${BINDIR}/tar
MLINKS=	gtar.1 tar.1
.endif

SUBDIR=	doc

# libtar SRCS
SRCS=	addext.c argmatch.c backupfile.c basename.c dirname.c error.c \
	exclude.c full-write.c getdate.c getline.c getopt.c getopt1.c \
	getstr.c hash.c human.c mktime.c modechange.c prepargs.c \
	print-copyr.c quotearg.c safe-read.c save-cwd.c savedir.c unicodeio.c \
	xgetcwd.c xmalloc.c xstrdup.c xstrtoul.c xstrtoumax.c strtoumax.c
SRCS+=	buffer.c compare.c create.c delete.c extract.c incremen.c list.c \
	mangle.c misc.c names.c rtapelib.c tar.c update.c

CFLAGS+= -DHAVE_CONFIG_H -I${.CURDIR} -I${TARDIR}/lib -I${TARDIR}/src

NOSHARED?= YES

gtar.1: tar.1
	cp ${.CURDIR}/tar.1 ${.TARGET}

CLEANFILES+=	gtar.1

.include <bsd.prog.mk>
