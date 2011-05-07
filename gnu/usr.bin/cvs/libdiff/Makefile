# $FreeBSD: src/gnu/usr.bin/cvs/libdiff/Makefile,v 1.4.2.4 2003/01/21 23:06:52 peter Exp $

.include "${.CURDIR}/../Makefile.inc"

.PATH:	${CVSDIR}/diff

LIB=		diff
INTERNALLIB=	yes

CFLAGS+=	-I../lib -I${CVSDIR}/lib
CFLAGS+=	-DHAVE_CONFIG_H

SRCS =	diff.c \
	diff3.c \
	analyze.c \
	cmpbuf.c \
	io.c \
	context.c \
	ed.c \
	normal.c \
	ifdef.c \
	util.c \
	dir.c \
	version.c \
	side.c

.include <bsd.lib.mk>
