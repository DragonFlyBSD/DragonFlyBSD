PROG=		mkesdb
SRCS=		yacc.y lex.l
SRCS+=		citrus_bcs.c citrus_db_factory.c citrus_db_hash.c
SRCS+=		citrus_lookup_factory.c
YHEADER=	1

LIBCDIR=	${.CURDIR}/../../lib/libc
CITRUSDIR=	${LIBCDIR}/citrus

CFLAGS+=	-I. -I${.CURDIR} -I${LIBCDIR} -I${CITRUSDIR}

.PATH: ${CITRUSDIR}

.include <bsd.prog.mk>
