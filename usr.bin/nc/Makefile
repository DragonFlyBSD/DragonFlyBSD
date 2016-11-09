.PATH: ${.CURDIR}/../../crypto/libressl/apps/nc
.PATH: ${.CURDIR}/../../crypto/libressl/tls

PROG=	nc
SRCS=	netcat.c atomicio.c socks.c

# TLS block (no separate libtls.a)
SRCS+=	tls_client.c	\
	tls_config.c	\
	tls_conninfo.c	\
	tls_peer.c	\
	tls_server.c	\
	tls_util.c	\
	tls_verify.c	\
	tls.c

WARNS?=	2

CFLAGS+=-DHAVE_ARC4RANDOM_BUF -DHAVE_STRTONUM	\
	-I${.CURDIR}/../../crypto/libressl/include		\
	-I${.CURDIR}/../../crypto/libressl/include/compat

DPADD=	${LIBRECRYPTO} ${LIBRESSL}
LDADD=	-lprivate_ssl -lprivate_crypto
LDFLAGS+=	${PRIVATELIB_LDFLAGS}

.include <bsd.prog.mk>
