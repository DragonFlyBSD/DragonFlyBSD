# $FreeBSD: src/usr.sbin/ancontrol/Makefile,v 1.1.2.5 2002/08/08 10:03:47 ru Exp $
# $DragonFly: src/usr.sbin/ancontrol/Makefile,v 1.3 2005/01/17 01:49:24 cpressey Exp $

PROG=		ancontrol
MAN=		ancontrol.8

CFLAGS+=	-I${.CURDIR}/../../sys -DANCACHE
DPADD=		${LIBMD}
LDADD=		-lmd

WARNS?=		6

.include <bsd.prog.mk>
