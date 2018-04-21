# $FreeBSD: src/sbin/ping6/Makefile,v 1.1.2.4 2002/03/08 09:18:59 ume Exp $

PROG=	ping6
MAN=	ping6.8

CFLAGS+=-DINET6 -DHAVE_POLL_H -DHAVE_ARC4RANDOM -DUSE_RFC3542

BINOWN=	root
BINMODE=4555

LDADD=	-lm -lmd -lprivate_crypto
DPADD=	${LIBM} ${LIBMD} ${LIBCRYPTO}
LDFLAGS+=	${PRIVATELIB_LDFLAGS}
CFLAGS+=	-I${.CURDIR}/../../crypto/libressl/include
NO_STRICT_ALIASING=

# kame scopeid hack
CFLAGS+=-DKAME_SCOPEID

.include <bsd.prog.mk>
