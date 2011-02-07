#	@(#)Makefile	8.1 (Berkeley) 6/4/93
# $FreeBSD: src/libexec/rlogind/Makefile,v 1.15.2.3 2001/04/25 10:40:55 ru Exp $

PROG=	rlogind
MAN=	rlogind.8

DPADD=	${LIBUTIL}
LDADD=	-lutil
CFLAGS+= -DINET6

.include <bsd.prog.mk>
