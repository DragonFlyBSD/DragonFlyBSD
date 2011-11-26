# $NetBSD: Makefile,v 1.3 2007/05/28 12:06:31 tls Exp $
# $DragonFly: src/usr.bin/sdpquery/Makefile,v 1.1 2008/02/08 14:06:25 hasso Exp $

PROG=		sdpquery
MAN=		sdpquery.1
SRCS=		sdpquery.c search.c

CFLAGS+=	-I${.CURDIR}/../../sys
DPADD+=		${LIBBLUETOOTH} ${LIBSDP}
LDADD+=		-lbluetooth -lsdp

.include <bsd.prog.mk>
