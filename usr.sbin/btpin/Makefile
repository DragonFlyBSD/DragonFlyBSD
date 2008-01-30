# $NetBSD: Makefile,v 1.2 2006/09/26 19:18:19 plunky Exp $
# $DragonFly: src/usr.sbin/btpin/Attic/Makefile,v 1.1 2008/01/30 14:10:19 hasso Exp $

PROG=		btpin
SRCS=		btpin.c
MAN=		btpin.1

CFLAGS+=	-I${.CURDIR}/../../sys
DPADD+=		${LIBBLUETOOTH}
LDADD+=		-lbluetooth
WARNS?=		6

.include <bsd.prog.mk>
