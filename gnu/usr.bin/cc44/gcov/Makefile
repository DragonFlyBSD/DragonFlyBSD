.include "../Makefile.inc"
.include "../Makefile.langs"

PROG=	gcov
MAN=	gcov${GCCSHORTVER}.1
MLINKS=	gcov${GCCSHORTVER}.1 gcov.1

.if ${GCCDOCSUFFIX} != ${GCCSHORTVER}
MLINKS+=gcov${GCCSHORTVER}.1 gcov${GCCDOCSUFFIX}.1
.endif

SRCS=	gcov.c intl.c errors.c version.c

gcov${GCCSHORTVER}.1: ${GCCDIR}/gcc/doc/gcov.1
	cp ${.ALLSRC} ${.TARGET}

CLEANFILES+=	gcov${GCCSHORTVER}.1

.include <bsd.prog.mk>
