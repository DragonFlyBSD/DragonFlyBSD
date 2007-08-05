# $FreeBSD: src/gnu/usr.bin/diff/Makefile,v 1.13 1999/11/26 12:29:48 phk Exp $
# $DragonFly: src/gnu/usr.bin/diff/Makefile,v 1.6 2007/08/05 05:47:00 pavalos Exp $

.PATH: ${.CURDIR}/../../../contrib/diffutils-2.8/src \
       ${.CURDIR}/../../../contrib/diffutils-2.8/lib \
       ${.CURDIR}/../../../contrib/diffutils-2.8/man

PROG=	diff
SRCS=	analyze.c context.c diff.c dir.c ed.c ifdef.c io.c \
     	normal.c side.c util.c

SRCS+=xmalloc.c strtoumax.c cmpbuf.c exitfail.c error.c quotesys.c \
      strftime.c c-stack.c basename.c exclude.c hard-locale.c \
      posixver.c file-type.c version-etc.c

# Important for ctype macros!
CFLAGS+=-funsigned-char

CFLAGS+=-I${.OBJDIR} -I-
CFLAGS+=-I${.CURDIR}/../../../contrib/diffutils-2.8
CFLAGS+=-I${.CURDIR}/../../../contrib/diffutils-2.8/src
CFLAGS+=-I${.CURDIR}/../../../contrib/diffutils-2.8/lib
CFLAGS+=-DHAVE_CONFIG_H

SUBDIR+=doc

DPADD+=	${LIBGNUREGEX}
LDADD+=	-lgnuregex

CONTRIBDIR=${.CURDIR}/../../../contrib/diffutils-2.8/src
SRCS+=diff.c.patch diff.h.patch

.include <bsd.prog.mk>
