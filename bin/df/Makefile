#	@(#)Makefile	8.3 (Berkeley) 5/8/95
# $FreeBSD: src/bin/df/Makefile,v 1.9.2.3 2002/04/05 00:19:21 iedowse Exp $

PROG=	df
SRCS=	df.c vfslist.c

.PATH:	${.CURDIR}/../../sbin/mount

DPADD=	${LIBUTIL}
LDADD=	-lutil

.include <bsd.prog.mk>
