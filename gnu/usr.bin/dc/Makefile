# $FreeBSD: src/gnu/usr.bin/dc/Makefile,v 1.11 1999/08/27 23:35:30 peter Exp $
# $DragonFly: src/gnu/usr.bin/dc/Attic/Makefile,v 1.2 2003/06/17 04:25:45 dillon Exp $

BCDIR=	${.CURDIR}/../../../contrib/bc

.PATH:	${BCDIR}/dc ${BCDIR}/lib ${BCDIR}/doc

PROG=	dc
SRCS=	array.c dc.c eval.c misc.c numeric.c stack.c string.c \
	getopt.c getopt1.c number.c
CFLAGS+=-I${.CURDIR}/../bc -I${BCDIR}/h -DHAVE_CONFIG_H
DPADD=	${LIBM}
LDADD=	-lm
SUBDIR=	doc

.include <bsd.prog.mk>
