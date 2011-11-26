# $FreeBSD: src/usr.bin/fetch/Makefile,v 1.3.6.4 2003/01/09 11:54:58 des Exp $
# $DragonFly: src/usr.bin/fetch/Makefile,v 1.7 2007/08/27 16:50:53 pavalos Exp $

PROG=		fetch
DPADD=		${LIBFETCH}
LDADD=		-lfetch
.if !defined(NO_CRYPT) && !defined(NO_OPENSSL)
DPADD+=		${LIBCRYPTO} ${LIBSSL}
LDADD+=		-lcrypto -lssl
.endif

.include <bsd.prog.mk>
