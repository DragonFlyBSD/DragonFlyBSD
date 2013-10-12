#	@(#)Makefile	8.1 (Berkeley) 6/6/93
# $FreeBSD: src/usr.bin/find/Makefile,v 1.20 2010/02/10 07:15:21 ed Exp $

PROG=	find
SRCS=	find.c function.c ls.c main.c misc.c operator.c option.c \
	getdate.y
YFLAGS=

.if defined(BOOTSTRAPPING)
CFLAGS+= -DBOOTSTRAPPING=1
.endif

.include <bsd.prog.mk>
