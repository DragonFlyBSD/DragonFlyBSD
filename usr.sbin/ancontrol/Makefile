# $FreeBSD: src/usr.sbin/ancontrol/Makefile,v 1.1.2.5 2002/08/08 10:03:47 ru Exp $
# $DragonFly: src/usr.sbin/ancontrol/Makefile,v 1.2 2003/06/17 04:29:52 dillon Exp $

PROG=		ancontrol
MAN=		ancontrol.8

CFLAGS+=	-I${.CURDIR}/../../sys -DANCACHE
DPADD=		${LIBMD}
LDADD=		-lmd

WARNS?=		2

.include <bsd.prog.mk>
