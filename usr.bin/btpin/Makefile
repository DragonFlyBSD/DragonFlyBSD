# $NetBSD: Makefile,v 1.2 2006/09/26 19:18:19 plunky Exp $
# $DragonFly: src/usr.bin/btpin/Makefile,v 1.2 2008/08/07 10:45:58 swildner Exp $

PROG=		btpin
SRCS=		btpin.c
MAN=		btpin.1

CFLAGS+=	-I${.CURDIR}/../../sys
DPADD+=		${LIBBLUETOOTH}
LDADD+=		-lbluetooth

.include <bsd.prog.mk>
