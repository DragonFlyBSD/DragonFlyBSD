# $FreeBSD: src/gnu/usr.bin/diff/Makefile,v 1.13 1999/11/26 12:29:48 phk Exp $
# $DragonFly: src/gnu/usr.bin/diff/Makefile,v 1.4 2004/04/13 14:55:02 eirikn Exp $

.PATH: ${.CURDIR}/../../../contrib/diffutils-2.8.1/src \
       ${.CURDIR}/../../../contrib/diffutils-2.8.1/lib \
       ${.CURDIR}/../../../contrib/diffutils-2.8.1/man

PROG=	diff
SRCS=	analyze.c context.c diff.c dir.c ed.c ifdef.c io.c \
     	normal.c side.c util.c version.c

SRCS+=xmalloc.c strtoumax.c cmpbuf.c exitfail.c error.c quotesys.c \
      strftime.c c-stack.c basename.c exclude.c freesoft.c hard-locale.c \
      posixver.c

# Important for ctype macros!
CFLAGS+=-funsigned-char

CFLAGS+=-I${.CURDIR}/../../../contrib/diffutils-2.8.1
CFLAGS+=-I${.CURDIR}/../../../contrib/diffutils-2.8.1/src
CFLAGS+=-I${.CURDIR}/../../../contrib/diffutils-2.8.1/lib
CFLAGS+=-DHAVE_CONFIG_H
CFLAGS+=-DPR_PROGRAM=\"/usr/bin/pr\"
# current contrib/diff needs __FreeBSD__ defined
CFLAGS+=-D__FreeBSD__

SUBDIR+=doc

DPADD+=	${LIBGNUREGEX}
LDADD+=	-lgnuregex

CONTRIBDIR=${.CURDIR}/../../../contrib/diffutils-2.8.1/src
SRCS+=analyze.c.patch context.c.patch diff.c.patch io.c.patch util.c.patch

.include <bsd.prog.mk>
