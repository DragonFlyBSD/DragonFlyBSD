# $FreeBSD: src/gnu/usr.bin/diff3/Makefile,v 1.6 1999/08/27 23:35:34 peter Exp $
# $DragonFly: src/gnu/usr.bin/diff3/Makefile,v 1.3 2004/04/13 14:55:02 eirikn Exp $

.PATH:  ${.CURDIR}/../../../contrib/diffutils-2.8.1/src \
        ${.CURDIR}/../../../contrib/diffutils-2.8.1/lib \
        ${.CURDIR}/../../../contrib/diffutils-2.8.1/man

PROG=		diff3
SRCS=		diff3.c version.c
SRCS+=	xmalloc.c error.c c-stack.c exitfail.c freesoft.c cmpbuf.c

# Important for ctype macros!
CFLAGS+=-funsigned-char

CFLAGS+=-I${.CURDIR}/../../../contrib/diffutils-2.8.1
CFLAGS+=-I${.CURDIR}/../../../contrib/diffutils-2.8.1/src
CFLAGS+=-I${.CURDIR}/../../../contrib/diffutils-2.8.1/lib
CFLAGS+=-DHAVE_CONFIG_H
CFLAGS+=-DDIFF_PROGRAM=\"/usr/bin/diff\"

CONTRIBDIR=${.CURDIR}/../../../contrib/diffutils-2.8.1/src
SRCS+=	diff3.c.patch

.include <bsd.prog.mk>
