# $FreeBSD: src/gnu/usr.bin/sdiff/Makefile,v 1.5 1999/08/27 23:37:00 peter Exp $
# $DragonFly: src/gnu/usr.bin/sdiff/Makefile,v 1.3 2004/04/13 14:55:02 eirikn Exp $

.PATH:  ${.CURDIR}/../../../contrib/diffutils-2.8.1/src \
        ${.CURDIR}/../../../contrib/diffutils-2.8.1/lib \
        ${.CURDIR}/../../../contrib/diffutils-2.8.1/man

PROG=		sdiff
SRCS=		sdiff.c version.c

SRCS+=	error.c xmalloc.c c-stack.c basename.c freesoft.c strtoumax.c \
      	exitfail.c

# Important for ctype macros!
CFLAGS+=-funsigned-char

CFLAGS+=-I${.CURDIR}/../../../contrib/diffutils-2.8.1
CFLAGS+=-I${.CURDIR}/../../../contrib/diffutils-2.8.1/src
CFLAGS+=-I${.CURDIR}/../../../contrib/diffutils-2.8.1/lib
CFLAGS+=-DHAVE_CONFIG_H
CFLAGS+=-DDIFF_PROGRAM=\"/usr/bin/diff\"

CONTRIBDIR=${.CURDIR}/../../../contrib/diffutils-2.8.1/src
SRCS+=sdiff.c.patch

.include <bsd.prog.mk>
