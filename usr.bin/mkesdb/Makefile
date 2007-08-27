# $DragonFly: src/usr.bin/mkesdb/Makefile,v 1.2 2007/08/27 16:50:56 pavalos Exp $

PROG=		mkesdb
SRCS=		yacc.y lex.l
SRCS+=		citrus_bcs.c citrus_db_factory.c citrus_db_hash.c
SRCS+=		citrus_lookup_factory.c
YHEADER=	1
NOMAN=		1

LIBCDIR=	${.CURDIR}/../../lib/libc
CITRUSDIR=	${LIBCDIR}/citrus

CFLAGS+=	-I. -I${.CURDIR} -I${LIBCDIR} -I${CITRUSDIR}

.PATH: ${CITRUSDIR}

.include <bsd.prog.mk>
