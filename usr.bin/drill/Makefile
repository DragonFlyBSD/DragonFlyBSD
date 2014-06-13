PROG=	drill
LDNS=	${.CURDIR}/../../contrib/ldns
LDNSBASE=	${.CURDIR}/../../lib/libldns
SRCS=	chasetrace.c dnssec.c drill.c drill_util.c \
	error.c root.c securetrace.c work.c
WARNS?= 2

.PATH:	${.CURDIR}/../../contrib/ldns/drill

LDADD+=	-lldns -lcrypto
DPADD+=	${LIBLDNS} ${LIBCRYPTO}
CFLAGS+=-I${.CURDIR} -I${LDNS} -I${LDNS}/drill -I${LDNSBASE}

.include <bsd.prog.mk>
