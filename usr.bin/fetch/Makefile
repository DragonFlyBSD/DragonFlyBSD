# $FreeBSD: head/usr.bin/fetch/Makefile 275042 2014-11-25 14:29:10Z bapt $

PROG=		fetch
DPADD=		${LIBFETCH}
LDADD=		-lfetch

DPADD+=		${LIBSSL} ${LIBCRYPTO}
LDADD+=		-lprivate_ssl -lprivate_crypto
CFLAGS+=	${PRIVATELIB_CFLAGS}
LDFLAGS+=	${PRIVATELIB_LDFLAGS}

.include <bsd.prog.mk>
