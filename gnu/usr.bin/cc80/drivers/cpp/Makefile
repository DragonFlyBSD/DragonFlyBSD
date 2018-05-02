.include "../Makefile.inc"
.include "../../Makefile.langs"
.include "../../../Makefile.cco"

PROG_CXX=	cpp
MFILE=		cpp${MANPAGEVER}.1
MAN=		${MFILE}

SRCS=		${GCC_SRCS} cppspec.c ${EXTRA_GCC_SRCS}

CFLAGS+=	-DCONFIGURE_SPECS="\"\"" \
		-DACCEL_DIR_SUFFIX="\"\"" \
		-DDEFAULT_REAL_TARGET_MACHINE="\"${target_machine}\""

DOC_Release=	cpp.1 #${GCCDIR}/gcc/doc/cpp.1
DOC_Snapshot=	cpp.1

${MFILE}: ${DOC_${GCCRELEASE}}
	cp ${.ALLSRC} ${.TARGET}

CLEANFILES+=	${MFILE}

.if defined(IS_PRIMARY)
MLINKS+= ${MFILE} cpp.1
.endif

# hack to force c++ compiler to compile *.c files to create program
.for cfile in ${SRCS}
${cfile:.c=.o}: ${cfile}
	${CXX} ${STATIC_CXXFLAGS} ${CXXFLAGS} -c ${.IMPSRC} -o ${.TARGET}
.endfor

.include <bsd.prog.mk>
