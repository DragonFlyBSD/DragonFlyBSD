# $FreeBSD: src/tools/tools/net80211/w00t/Makefile.inc,v 1.2 2010/02/25 14:42:26 ru Exp $

W00T=	${.CURDIR}/../libw00t
# NB: we get crc32 from -lz
DPADD=		${W00T}/libw00t.a ${LIBZ}
LDADD=		-lw00t -lz
LDFLAGS=	-L${W00T}

DPADD+=		${LIBRECRYPTO}
LDADD+=		-lprivate_crypto
LDFLAGS+=	${PRIVATELIB_LDFLAGS}
CFLAGS+=	-I${.CURDIR}/../../../../../crypto/libressl/include

BINDIR=	/usr/local/bin
CFLAGS+=	-g -I${W00T}

NOMAN=
