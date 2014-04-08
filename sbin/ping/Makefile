#	@(#)Makefile	8.1 (Berkeley) 6/5/93
# $FreeBSD: src/sbin/ping/Makefile,v 1.12.2.4 2001/12/19 04:49:11 dd Exp $
# $DragonFly: src/sbin/ping/Makefile,v 1.4 2006/10/17 00:55:43 pavalos Exp $

PROG=	ping
MAN=	ping.8
BINMODE=4555
DPADD=	${LIBM}
LDADD=	-lm

CFLAGS+=-DIPSEC
DPADD+=	${LIBIPSEC}
LDADD+=	-lipsec

.include <bsd.prog.mk>
