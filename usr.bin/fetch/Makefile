# $FreeBSD: src/usr.bin/fetch/Makefile,v 1.3.6.4 2003/01/09 11:54:58 des Exp $
# $DragonFly: src/usr.bin/fetch/Makefile,v 1.5 2005/01/04 23:08:13 dillon Exp $

PROG=		fetch
WARNS?=		6
DPADD=		${LIBFETCH}
LDADD=		-lfetch
.if !defined(NOCRYPT) && !defined(NO_OPENSSL)
DPADD+=		${LIBCRYPTO} ${LIBSSL}
LDADD+=		-lcrypto -lssl
.endif

.include <bsd.prog.mk>
