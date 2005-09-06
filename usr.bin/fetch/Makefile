# $FreeBSD: src/usr.bin/fetch/Makefile,v 1.3.6.4 2003/01/09 11:54:58 des Exp $
# $DragonFly: src/usr.bin/fetch/Makefile,v 1.6 2005/09/06 18:55:42 dillon Exp $

PROG=		fetch
WARNS?=		6
DPADD=		${LIBFETCH}
LDADD=		-lfetch
.if !defined(NO_CRYPT) && !defined(NO_OPENSSL)
DPADD+=		${LIBCRYPTO} ${LIBSSL}
LDADD+=		-lcrypto -lssl
.endif

.include <bsd.prog.mk>
