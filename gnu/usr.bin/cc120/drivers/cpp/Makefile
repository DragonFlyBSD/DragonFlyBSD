.include "../Makefile.inc"
.include "../../Makefile.langs"
.include "../../../Makefile.cco"

PROG_CXX=	cpp
MFILE=		cpp${MANPAGEVER}.1
MAN=		${MFILE}

objs=		${GCC_OBJS} cppspec.o ${EXTRA_GCC_OBJS}
SRCS=		${objs:.o=.cc}

${MFILE}: ${GCCDIR}/gcc/doc/cpp.1
	cp ${.ALLSRC} ${.TARGET}

CLEANFILES+=	${MFILE}

.if defined(IS_PRIMARY)
MLINKS+= ${MFILE} cpp.1
.endif

.include <bsd.prog.mk>
