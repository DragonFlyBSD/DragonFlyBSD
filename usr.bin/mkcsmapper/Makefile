# $DragonFly: src/usr.bin/mkcsmapper/Makefile,v 1.1 2005/03/11 20:17:11 joerg Exp $

PROG=		mkcsmapper
SRCS=		yacc.y lex.l
SRCS+=		citrus_db_factory.c citrus_bcs.c citrus_db_hash.c
SRCS+=		citrus_lookup_factory.c citrus_pivot_factory.c
NOMAN=		1
WARNS?=		6

LIBCDIR=	${.CURDIR}/../../lib/libc
CITRUSDIR=	${LIBCDIR}/citrus

.PATH: ${CITRUSDIR}

CFLAGS+=	-I. -I${.CURDIR} -I${LIBCDIR} -I${CITRUSDIR}
CFLAGS+=	-I${CITRUSDIR}/modules
YHEADER=	1

.include <bsd.prog.mk>
