# $FreeBSD: src/gnu/usr.bin/diff3/Makefile,v 1.6 1999/08/27 23:35:34 peter Exp $
# $DragonFly: src/gnu/usr.bin/diff3/Makefile,v 1.4 2007/08/05 05:47:00 pavalos Exp $

.PATH:  ${.CURDIR}/../../../contrib/diffutils/src \
        ${.CURDIR}/../../../contrib/diffutils/lib \
        ${.CURDIR}/../../../contrib/diffutils/man

PROG=		diff3
SRCS+=	xmalloc.c error.c c-stack.c exitfail.c cmpbuf.c version-etc.c

# Important for ctype macros!
CFLAGS+=-funsigned-char

CFLAGS+=-I${.CURDIR}/../../../contrib/diffutils
CFLAGS+=-I${.CURDIR}/../../../contrib/diffutils/src
CFLAGS+=-I${.CURDIR}/../../../contrib/diffutils/lib
CFLAGS+=-DHAVE_CONFIG_H
CFLAGS+=-DDEFAULT_DIFF_PROGRAM=\"/usr/bin/diff\"

CONTRIBDIR=${.CURDIR}/../../../contrib/diffutils/src
SRCS+=	diff3.c.patch

.include <bsd.prog.mk>
