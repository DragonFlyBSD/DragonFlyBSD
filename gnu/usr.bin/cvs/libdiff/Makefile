# $FreeBSD: src/gnu/usr.bin/cvs/libdiff/Makefile,v 1.4.2.4 2003/01/21 23:06:52 peter Exp $
# $DragonFly: src/gnu/usr.bin/cvs/libdiff/Makefile,v 1.2 2003/06/17 04:25:45 dillon Exp $

MAINTAINER=	peter@FreeBSD.org

.include "${.CURDIR}/../Makefile.inc"

.PATH:	${CVSDIR}/diff

LIB=		diff
INTERNALLIB=	YES

CFLAGS+= -I../lib -I${CVSDIR}/lib -DHAVE_CONFIG_H

SRCS =	diff.c diff3.c analyze.c cmpbuf.c cmpbuf.h io.c context.c ed.c \
        normal.c ifdef.c util.c dir.c version.c diff.h side.c

.include <bsd.lib.mk>
