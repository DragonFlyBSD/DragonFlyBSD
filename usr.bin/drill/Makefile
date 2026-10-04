LDNS=		${.CURDIR}/../../contrib/ldns
LDNSBASE=	${.CURDIR}/../../lib/libldns
.PATH:	${LDNS}/drill

PROG=		drill
MAN=		drill.1
SRCS=		chasetrace.c dnssec.c drill.c drill_util.c \
		error.c root.c securetrace.c work.c

WARNS= 		5

CFLAGS+=	-I${LDNSBASE} -I${LDNSBASE}/ldns -I${LDNS}

LDADD=		-lprivate_ldns -lprivate_crypto
DPADD=		${LIBLDNS} ${LIBCRYPTO}
CFLAGS+=	-I${.CURDIR}/../../crypto/libressl/include
LDFLAGS+=	-rpath /usr/lib/priv ${PRIVATELIB_LDFLAGS}

LDNS_TRUST_ANCHOR_FILE?=/etc/unbound/root.key

FLAGS_GROUPS=		anchor
anchor_FLAGS=		-DLDNS_TRUST_ANCHOR_FILE='"${LDNS_TRUST_ANCHOR_FILE}"'
anchor_FLAGS_FILES=	drill.c

drill.1: drill.1.in
	sed -e 's|@LDNS_TRUST_ANCHOR_FILE[@]|${LDNS_TRUST_ANCHOR_FILE}|g' \
		${.ALLSRC} > ${.TARGET}

CLEANFILES+=	drill.1

.include <bsd.prog.mk>
