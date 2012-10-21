.include "../Makefile.inc"
.include "../Makefile.langs"
.include "../../Makefile.cco"

PROG=	gcov
MFILE=	gcov${GCCSHORTVER}.1
MAN=	${MFILE}

SRCS=	gcov.c intl.c errors.c version.c

${MFILE}: ${GCCDIR}/gcc/doc/gcov.1
	cp ${.ALLSRC} ${.TARGET}

CLEANFILES+=	${MFILE}

.if defined(IS_PRIMARY)
MLINKS+= ${MFILE} gcov.1
.endif

.include <bsd.prog.mk>
