# This version of openssl(1) is from LibreSSL
PROG=	openssl
WARNS?=	2
MAN=	openssl.1 openssl.cnf.5

.include "${.CURDIR}/../../lib/librecrypto/Makefile.inc"

CFLAGS+=	-DLIBRESSL_INTERNAL
CFLAGS+=	-I${.CURDIR}/../../crypto/libressl/include
CFLAGS+= 	-I${.CURDIR}

DPADD+=		${LIBSSL} ${LIBCRYPTO}
LDADD+=		-lprivate_ssl -lprivate_crypto
LDFLAGS+=	${PRIVATELIB_LDFLAGS}

SRCS=	apps_posix.c apps.c asn1pars.c ca.c certhash.c ciphers.c cms.c \
	crl2p7.c crl.c \
	dgst.c dh.c dhparam.c dsa.c dsaparam.c ec.c ecparam.c enc.c errstr.c \
	gendh.c gendsa.c genpkey.c genrsa.c nseq.c ocsp.c openssl.c \
	passwd.c pkcs12.c pkcs7.c pkcs8.c pkey.c pkeyparam.c pkeyutl.c \
	prime.c rand.c req.c rsa.c rsautl.c \
	s_cb.c s_client.c s_server.c s_socket.c s_time.c sess_id.c \
	smime.c speed.c spkac.c ts.c verify.c version.c x509.c

.include <bsd.prog.mk>

.PATH:	${LIBRESSL_SRC}/apps/openssl ${LIBRESSL_SRC}/man
