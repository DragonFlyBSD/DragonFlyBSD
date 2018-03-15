# $NetBSD: Makefile,v 1.1 1999/11/23 05:28:20 mrg Exp $
# $FreeBSD: src/sbin/rcorder/Makefile,v 1.3 2002/06/21 15:52:05 obrien Exp $

PROG=   rcorder
SRCS=   hash.c rcorder.c
MAN=	rcorder.8

LDADD+=	-lutil
DPADD+=	${LIBUTIL}

# XXX hack for make's hash.[ch]
CFLAGS+= -DORDER -I.


.include <bsd.prog.mk>
