.include "../Makefile.langs"
.include "../../Makefile.cco"

PROG=	gcov
MANPAGEVER= 47
MFILE=	gcov${MANPAGEVER}.1
MAN=	${MFILE}

${MFILE}: ${GCCDIR}/gcc/doc/gcov.1
	cp ${.ALLSRC} ${.TARGET}

CLEANFILES+=	${MFILE}

.if defined(IS_PRIMARY)
MLINKS+= ${MFILE} gcov.1
.endif

.include <bsd.prog.mk>
