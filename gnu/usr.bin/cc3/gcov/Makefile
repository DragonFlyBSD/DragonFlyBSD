# $FreeBSD: src/gnu/usr.bin/cc/gcov/Makefile,v 1.3 2003/02/24 22:58:19 ru Exp $
# $DragonFly: src/gnu/usr.bin/cc3/gcov/Attic/Makefile,v 1.1 2004/01/17 22:40:16 drhodus Exp $

.include "../Makefile.inc"

.PATH: ${GCCDIR} ${GCCDIR}/doc		${GCCDIR}/../libiberty

PROG=	gcov3
SRCS=	gcov.c version.c

DPADD=	${LIBCC_INT}
LDADD=	${LIBCC_INT}

CLEANFILES= gcov3.1

gcov3.1: gcov.1
	cat ${.ALLSRC} > ${.TARGET}

.include <bsd.prog.mk>
