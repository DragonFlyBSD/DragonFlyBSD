#	@(#)Makefile	8.1 (Berkeley) 7/19/93
# $FreeBSD: src/sbin/init/Makefile,v 1.20.2.4 2001/08/01 06:37:01 obrien Exp $

PROG=	oinit
DPADD=	${LIBUTIL}
LDADD=	-lutil

.include <bsd.prog.mk>

.ORDER: clean cleandepend cleandir obj depend all install
