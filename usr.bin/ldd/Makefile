# $FreeBSD: src/usr.bin/ldd/Makefile,v 1.11 1999/08/28 01:02:42 peter Exp $
# $DragonFly: src/usr.bin/ldd/Makefile,v 1.3 2007/08/27 16:50:55 pavalos Exp $

PROG=		ldd
SRCS=		ldd.c
.if ${MACHINE_ARCH} == "i386"
SRCS+=		sods.c
.endif
WARNS?=	1

.include <bsd.prog.mk>
