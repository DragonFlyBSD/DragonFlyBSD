# $FreeBSD: src/usr.sbin/cdcontrol/Makefile,v 1.2.2.2 2002/03/15 22:34:25 mikeh Exp $
# $DragonFly: src/usr.sbin/cdcontrol/Makefile,v 1.3 2005/01/17 01:49:24 cpressey Exp $

PROG= cdcontrol
WARNS?=	6

DPADD=	${LIBEDIT} ${LIBTERMCAP}
LDADD=	-ledit -ltermcap

.include <bsd.prog.mk>
