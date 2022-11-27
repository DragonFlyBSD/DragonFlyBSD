# This version of nc(1) is from LibreSSL
.PATH: ${.CURDIR}/../../crypto/libressl/apps/nc
.PATH: ${.CURDIR}/../../crypto/libressl/crypto/ecdsa
.PATH: ${.CURDIR}/../../crypto/libressl/crypto/rsa
.PATH: ${.CURDIR}/../../crypto/libressl/tls

PROG=	nc
SRCS=	netcat.c atomicio.c socks.c

# ecdsa
SRCS+=	ecs_lib.c

# rsa
SRCS+=	rsa_lib.c

# TLS block (no separate libtls.a)
SRCS+=	tls_bio_cb.c	\
	tls_client.c	\
	tls_config.c	\
	tls_conninfo.c	\
	tls_keypair.c	\
	tls_peer.c	\
	tls_ocsp.c	\
	tls_server.c	\
	tls_util.c	\
	tls_verify.c	\
	tls.c		\
	tls_signer.c

WARNS?=	2

CFLAGS+= -DLIBRESSL_INTERNAL
CFLAGS+=-I${.CURDIR}/../../crypto/libressl/include
CFLAGS+=-I${.CURDIR}/../../crypto/libressl/crypto/evp

DPADD=	${LIBCRYPTO} ${LIBSSL}
LDADD=	-lprivate_ssl -lprivate_crypto
LDFLAGS+=	${PRIVATELIB_LDFLAGS}

.include <bsd.prog.mk>
