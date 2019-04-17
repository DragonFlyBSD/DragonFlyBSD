# $Revision: 1.6 $
# $FreeBSD: src/sbin/routed/rtquery/Makefile,v 1.7.2.1 2001/04/25 10:58:50 ru Exp $

PROG=	rtquery
MAN=	rtquery.8
NO_WARRAY_BOUNDS=

CFLAGS+=	${PRIVATELIB_CFLAGS}
LDADD=		-lprivate_crypto
DPADD=		${LIBCRYPTO}
LDFLAGS+=	${PRIVATELIB_LDFLAGS}

.include <bsd.prog.mk>
