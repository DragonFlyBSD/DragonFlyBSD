.include "../Makefile.inc"
.include "../../Makefile.langs"
.include "../../../Makefile.cco"

PROG_CXX=	cpp
MFILE=		cpp${MANPAGEVER}.1
MAN=		${MFILE}

SRCS=		${GCC_SRCS} cppspec.cc ${EXTRA_GCC_SRCS}

CFLAGS+=	-DCONFIGURE_SPECS="\"\"" \
		-DACCEL_DIR_SUFFIX="\"\"" \
		-DDEFAULT_REAL_TARGET_MACHINE="\"${target_machine}\""

DOC_Release=	${GCCDIR}/gcc/doc/cpp.1
DOC_Snapshot=	${GCCDIR}/gcc/doc/cpp.1

${MFILE}: ${DOC_${GCCRELEASE}}
	cp ${.ALLSRC} ${.TARGET}

CLEANFILES+=	${MFILE}

.if defined(IS_PRIMARY)
MLINKS+= ${MFILE} cpp.1
.endif

.include <bsd.prog.mk>
