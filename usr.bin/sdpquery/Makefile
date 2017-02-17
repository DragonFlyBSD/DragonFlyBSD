# $NetBSD: Makefile,v 1.3 2007/05/28 12:06:31 tls Exp $

PROG=		sdpquery
MAN=		sdpquery.1
SRCS=		sdpquery.c search.c

CFLAGS+=	-I${.CURDIR}/../../sys
DPADD+=		${LIBBLUETOOTH} ${LIBSDP}
LDADD+=		-lbluetooth -lsdp

WARNS?=		3
NO_STRICT_ALIASING=

.include <bsd.prog.mk>
