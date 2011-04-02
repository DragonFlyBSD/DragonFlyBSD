# $FreeBSD: src/gnu/usr.bin/sdiff/Makefile,v 1.5 1999/08/27 23:37:00 peter Exp $
# $DragonFly: src/gnu/usr.bin/sdiff/Makefile,v 1.4 2007/08/05 05:47:00 pavalos Exp $

.PATH:  ${.CURDIR}/../../../contrib/diffutils/src \
        ${.CURDIR}/../../../contrib/diffutils/lib \
        ${.CURDIR}/../../../contrib/diffutils/man

PROG=		sdiff

SRCS+=	error.c xmalloc.c c-stack.c basename.c strtoumax.c version-etc.c \
      	exitfail.c

# Important for ctype macros!
CFLAGS+=-funsigned-char

CFLAGS+=-I${.CURDIR}/../../../contrib/diffutils
CFLAGS+=-I${.CURDIR}/../../../contrib/diffutils/src
CFLAGS+=-I${.CURDIR}/../../../contrib/diffutils/lib
CFLAGS+=-DHAVE_CONFIG_H
CFLAGS+=-DDEFAULT_DIFF_PROGRAM=\"/usr/bin/diff\"

CONTRIBDIR=${.CURDIR}/../../../contrib/diffutils/src
SRCS+=sdiff.c.patch

.include <bsd.prog.mk>
