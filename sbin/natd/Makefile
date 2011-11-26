# $FreeBSD: src/sbin/natd/Makefile,v 1.4.2.1 2001/04/25 10:58:41 ru Exp $
# $DragonFly: src/sbin/natd/Makefile,v 1.5 2006/10/17 00:55:43 pavalos Exp $

PROG=	natd
SRCS=	natd.c icmp.c
LDADD=	-lalias
DPADD=	${LIBALIAS}
MAN=	natd.8

.include <bsd.prog.mk>
