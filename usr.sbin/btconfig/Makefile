# $NetBSD: Makefile,v 1.2 2007/05/28 12:06:34 tls Exp $

PROG=		btconfig
MAN = 		btconfig.8

CFLAGS+=	-I${.CURDIR}/../../sys
DPADD+=		${LIBBLUETOOTH}
LDADD+=		-lbluetooth

.include <bsd.prog.mk>
