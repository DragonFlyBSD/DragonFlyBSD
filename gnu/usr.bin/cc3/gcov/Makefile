# $FreeBSD: src/gnu/usr.bin/cc/gcov/Makefile,v 1.3 2003/02/24 22:58:19 ru Exp $
# $DragonFly: src/gnu/usr.bin/cc3/gcov/Attic/Makefile,v 1.4 2004/02/02 23:35:11 dillon Exp $

.include "../Makefile.inc"

CFLAGS+=        -I${.CURDIR}/../config

.PATH: ${GCCDIR}/gcc ${GCCDIR}/gcc/doc ${GCCDIR}/../libiberty

PROG=	gcov
SRCS=	gcov.c version.c

DPADD=	${LIBCC_INT}
LDADD=	${LIBCC_INT}

.include <bsd.prog.mk>
