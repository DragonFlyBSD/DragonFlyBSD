# $FreeBSD: src/sbin/natd/Makefile,v 1.4.2.1 2001/04/25 10:58:41 ru Exp $
# $DragonFly: src/sbin/natd/Makefile,v 1.4 2005/06/01 18:37:20 swildner Exp $

PROG=	natd
SRCS=	natd.c icmp.c
WARNS?= 6
LDADD=	-lalias
DPADD=	${LIBALIAS}
MAN=	natd.8

.include <bsd.prog.mk>
