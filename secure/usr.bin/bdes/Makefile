#	@(#)Makefile	8.1 (Berkeley) 6/6/93
# $FreeBSD: src/secure/usr.bin/bdes/Makefile,v 1.10 1999/08/28 01:30:30 peter Exp $

PROG=	bdes
SRCS=	bdes.c

.if exists(${.OBJDIR}/../../lib/libcipher)
LDFLAGS+= -L${.OBJDIR}/../../lib/libcipher
.else
LDFLAGS+= -L${.CURDIR}/../../lib/libcipher
.endif

LDADD+= -lcipher
DPADD+= libcipher.a

.include <bsd.prog.mk>
