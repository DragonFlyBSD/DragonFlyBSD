# $FreeBSD: src/usr.sbin/watch/Makefile,v 1.8.2.2 2002/03/11 01:26:42 dd Exp $
# $DragonFly: src/usr.sbin/watch/Makefile,v 1.3 2005/01/17 01:49:24 cpressey Exp $

PROG=	watch
MAN=	watch.8

WARNS?=	6
NO_WERROR=	yes

LDADD=	-ltermcap
DPADD=	${LIBTERMCAP}

.include <bsd.prog.mk>
