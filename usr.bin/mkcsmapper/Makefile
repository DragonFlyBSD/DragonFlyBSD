PROG=		mkcsmapper
SRCS=		yacc.y lex.l \
		citrus_bcs.c \
		citrus_db_factory.c \
		citrus_db_hash.c \
		citrus_lookup_factory.c \
		citrus_pivot_factory.c

CITRUSDIR=	${.CURDIR}/../../lib/libc/citrus
CFLAGS+=	-I. -I${.CURDIR}
CFLAGS+=	-I${CITRUSDIR}
CFLAGS+=	-I${.CURDIR}/../../lib/i18n_module/mapper_std
CFLAGS+=	--param max-inline-insns-single=64

.PATH: ${CITRUSDIR}

.include <bsd.prog.mk>
