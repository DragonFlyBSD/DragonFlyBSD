.include "../Makefile.inc"
.include "../Makefile.langs"
.include "../../Makefile.cco"

PROG_CXX=	gcov
MFILE=		gcov${MANPAGEVER}.1
MAN=		${MFILE}

SRCS=		gcov.c

DOC_Release=	${GCCDIR}/gcc/doc/gcov.1
DOC_Snapshot=	gcov.1

${MFILE}: ${DOC_${GCCRELEASE}}
	cp ${.ALLSRC} ${.TARGET}

CLEANFILES+=	${MFILE}

.if defined(IS_PRIMARY)
MLINKS+= ${MFILE} gcov.1
.endif

# hack to force c++ compiler to compile *.c files to create program
.for cfile in ${SRCS}
${cfile:.c=.o}: ${cfile}
	${CXX} ${STATIC_CXXFLAGS} ${CXXFLAGS} -c ${.IMPSRC} -o ${.TARGET}
.endfor

.include <bsd.prog.mk>
