#	@(#)Makefile	8.1 (Berkeley) 6/6/93
# $FreeBSD: src/usr.bin/gcore/Makefile,v 1.6 1999/11/14 13:54:43 marcel Exp $

PROG=	gcore
SRCS=	elfcore.c gcore.c
DPADD=	${LIBKVM}
LDADD=	-lkvm
WARNS?=	2

.include <bsd.prog.mk>
