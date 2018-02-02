# This version of libssl is from LibreSSL
.include "../librecrypto/Makefile.inc"

LIB=		private_ssl
SHLIB_MAJOR=	39
PRIVATELIB=

SHLIBDIR?=	/lib
WARNS?=		2

# from vendor makefile
libssl_la_SOURCES= \
	bio_ssl.c bs_ber.c bs_cbb.c bs_cbs.c d1_both.c \
	d1_clnt.c d1_enc.c d1_lib.c d1_meth.c d1_pkt.c d1_srtp.c \
	d1_srvr.c pqueue.c s23_clnt.c s23_lib.c s23_pkt.c s23_srvr.c \
	s3_both.c s3_cbc.c s3_clnt.c s3_lib.c s3_pkt.c s3_srvr.c \
	ssl_algs.c ssl_asn1.c ssl_cert.c ssl_ciph.c ssl_err.c \
	ssl_err2.c ssl_lib.c ssl_rsa.c ssl_sess.c ssl_stat.c ssl_txt.c \
	t1_clnt.c t1_enc.c t1_lib.c t1_meth.c t1_reneg.c t1_srvr.c

SRCS=	${libssl_la_SOURCES}

INCS=	dtls1.h srtp.h ssl.h ssl2.h ssl23.h ssl3.h tls1.h
INCSDIR=	${INCLUDEDIR}/priv/openssl

LDADD=		-lprivate_crypto
DPADD=		${LIBCRYPTO}
LDFLAGS+=	${PRIVATELIB_LDFLAGS}

.PATH:	${LSSL_SRC} ${LIBRESSL_SRC}/include/openssl

.include <bsd.lib.mk>
