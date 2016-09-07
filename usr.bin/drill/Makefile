PROG=		drill
LDNS=		${.CURDIR}/../../contrib/ldns
LDNSBASE=	${.CURDIR}/../../lib/libldns
SRCS=		chasetrace.c dnssec.c drill.c drill_util.c \
		error.c root.c securetrace.c work.c
WARNS?= 2

.PATH:	${.CURDIR}/../../contrib/ldns/drill

LDADD=		-lprivate_ldns -lprivate_crypto
DPADD=		${LIBLDNS} ${LIBRECRYPTO}
CFLAGS+=	-I${.CURDIR} -I${LDNS} -I${LDNS}/drill -I${LDNSBASE}
CFLAGS+=	-I${.CURDIR}/../../crypto/libressl/include
LDFLAGS+=	-rpath /lib/priv:/usr/lib/priv -L \
		${_SHLIBDIRPREFIX}/usr/lib/priv

.include <bsd.prog.mk>
