# This version of libressl is from LibreSSL
.include "../librecrypto/Makefile.inc"

LIB=		private_ssl
SHLIB_MAJOR=	48
PRIVATELIB=	shpub

SHLIBDIR?=	/lib
WARNS?=		2

CFLAGS+=	-DLIBRESSL_INTERNAL

# from vendor makefile
SRCS=	bio_ssl.c bs_cbb.c bs_cbs.c \
	d1_both.c d1_clnt.c d1_lib.c d1_pkt.c d1_srtp.c d1_srvr.c \
	pqueue.c s3_cbc.c s3_lib.c \
	ssl_algs.c ssl_asn1.c ssl_both.c ssl_cert.c ssl_ciph.c ssl_ciphers.c \
	ssl_clnt.c ssl_err.c ssl_init.c \
	ssl_lib.c ssl_kex.c ssl_methods.c ssl_packet.c ssl_pkt.c ssl_rsa.c \
	ssl_sess.c ssl_sigalgs.c ssl_srvr.c ssl_stat.c \
	ssl_tlsext.c ssl_transcript.c ssl_txt.c ssl_versions.c \
	t1_enc.c t1_lib.c \
	tls13_buffer.c tls13_client.c tls13_error.c \
	tls13_handshake.c tls13_handshake_msg.c tls13_key_schedule.c \
	tls13_key_share.c tls13_legacy.c tls13_lib.c tls13_record.c \
	tls13_record_layer.c tls13_server.c

# not referenced:
# bs_ber.c

INCS=	cms.h dtls1.h srtp.h ssl.h ssl2.h ssl23.h ssl3.h tls1.h
INCSDIR=	${INCLUDEDIR}/priv/openssl

LDADD=		-lprivate_crypto
DPADD=		${LIBCRYPTO}
LDFLAGS+=	${PRIVATELIB_LDFLAGS}

.PATH:	${LSSL_SRC} ${LIBRESSL_SRC}/include/openssl

.include <bsd.lib.mk>
