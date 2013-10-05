#	$NetBSD: Makefile,v 1.14 2013/07/19 04:17:02 uwe Exp $
#	@(#)Makefile	8.1 (Berkeley) 6/6/93

PROG=	man
SRCS=	man.c manconf.c
MAN=	man.1 man.conf.5

DPADD+=	${LIBUTIL}
LDADD+=	-lutil

.include <bsd.prog.mk>
