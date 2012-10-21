.include "../Makefile.langs"
.include "../../Makefile.cco"

PROG=	gcov
MFILE=	gcov${MANPAGEVER}.1
MAN=	${MFILE}

SRCS=	gcov.c

${MFILE}: ${GCCDIR}/gcc/doc/gcov.1
	cp ${.ALLSRC} ${.TARGET}

CLEANFILES+=	${MFILE}

.if defined(IS_PRIMARY)
MLINKS+= ${MFILE} gcov.1
.endif

.include <bsd.prog.mk>
