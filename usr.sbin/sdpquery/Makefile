# $NetBSD: Makefile,v 1.3 2007/05/28 12:06:31 tls Exp $
# $DragonFly: src/usr.sbin/sdpquery/Attic/Makefile,v 1.1 2008/01/06 21:51:30 hasso Exp $

PROG=		sdpquery
MAN=		sdpquery.1
SRCS=		sdpquery.c search.c

CFLAGS+=	-I${.CURDIR}/../../sys
DPADD+=		${LIBBLUETOOTH} ${LIBSDP}
LDADD+=		-lbluetooth -lsdp

.include <bsd.prog.mk>
