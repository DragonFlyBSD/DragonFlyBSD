# $FreeBSD: src/gnu/usr.bin/cc/gcov/Makefile,v 1.3 2003/02/24 22:58:19 ru Exp $
# $DragonFly: src/gnu/usr.bin/cc3/gcov/Attic/Makefile,v 1.2 2004/01/24 14:43:42 joerg Exp $

.include "../Makefile.inc"

.PATH: ${GCCDIR} ${GCCDIR}/doc		${GCCDIR}/../libiberty

PROG=	gcov
SRCS=	gcov.c version.c

DPADD=	${LIBCC_INT}
LDADD=	${LIBCC_INT}

CLEANFILES= gcov3.1

gcov3.1: gcov.1
	cat ${.ALLSRC} > ${.TARGET}

.include <bsd.prog.mk>
