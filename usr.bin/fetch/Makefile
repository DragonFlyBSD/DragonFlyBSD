# $FreeBSD: src/usr.bin/fetch/Makefile,v 1.3.6.4 2003/01/09 11:54:58 des Exp $

PROG=		fetch
DPADD=		${LIBFETCH}
LDADD=		-lfetch
.if !defined(NO_CRYPT) && !defined(NO_OPENSSL)
DPADD+=		${LIBSSL} ${LIBCRYPTO}
LDADD+=		-lssl -lcrypto
.endif

.include <bsd.prog.mk>
