# $FreeBSD: src/gnu/usr.bin/diff3/Makefile,v 1.6 1999/08/27 23:35:34 peter Exp $
# $DragonFly: src/gnu/usr.bin/diff3/Makefile,v 1.2 2003/06/17 04:25:45 dillon Exp $

.PATH:  ${.CURDIR}/../../../contrib/diff

PROG=		diff3
SRCS=		diff3.c getopt.c getopt1.c version.c

# Important for ctype macros!
CFLAGS+=-funsigned-char

CFLAGS+=-I${.CURDIR}/../../../contrib/diff
CFLAGS+=-DHAVE_CONFIG_H
CFLAGS+=-DDIFF_PROGRAM=\"/usr/bin/diff\"

.include <bsd.prog.mk>
