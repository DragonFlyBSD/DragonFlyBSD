# $FreeBSD: src/gnu/usr.bin/diff/Makefile,v 1.13 1999/11/26 12:29:48 phk Exp $
# $DragonFly: src/gnu/usr.bin/diff/Makefile,v 1.3 2004/02/03 10:04:04 rob Exp $

.PATH: ${.CURDIR}/../../../contrib/diff

PROG=	diff
SRCS=	analyze.c cmpbuf.c context.c diff.c dir.c ed.c getopt.c getopt1.c \
	ifdef.c io.c normal.c prepend_args.c side.c util.c version.c

# Important for ctype macros!
CFLAGS+=-funsigned-char

CFLAGS+=-I${.CURDIR}/../../../contrib/diff
CFLAGS+=-DHAVE_CONFIG_H
CFLAGS+=-DPR_PROGRAM=\"/usr/bin/pr\"
# current contrib/diff needs __FreeBSD__ defined
CFLAGS+=-D__FreeBSD__

SUBDIR+=doc

DPADD+=	${LIBGNUREGEX}
LDADD+=	-lgnuregex

.include <bsd.prog.mk>
