PROG=		drill
LDNS=		${.CURDIR}/../../contrib/ldns
LDNSBASE=	${.CURDIR}/../../lib/libldns
SRCS=		chasetrace.c dnssec.c drill.c drill_util.c \
		error.c root.c securetrace.c work.c
WARNS?= 2

.PATH:	${.CURDIR}/../../contrib/ldns/drill

CFLAGS+=	-DHAVE_CONFIG_H -I${LDNSBASE}
CFLAGS+=	-I${LDNSBASE}/ldns -I${LDNS} -I${LDNS}/drill

LDADD=		-lprivate_ldns -lprivate_crypto
DPADD=		${LIBLDNS} ${LIBCRYPTO}
CFLAGS+=	-I${.CURDIR}/../../crypto/libressl/include
LDFLAGS+=	-rpath /usr/lib/priv ${PRIVATELIB_LDFLAGS}

.include <bsd.prog.mk>
