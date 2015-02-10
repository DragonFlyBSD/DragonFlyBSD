GCC_LANG_DIR=	gcc/cp
.include "../Makefile.inc"
.include "../../Makefile.langs"
.include "../../../Makefile.cco"

PROG_CXX=	c++
LINKS=		${BINDIR}/c++ ${BINDIR}/g++
LINKS+=		${BINDIR}/c++ ${BINDIR}/CC
MAN=		# man pages installed by cc (MLINK)

SRCS=		${GCC_SRCS} g++spec.c ${EXTRA_GCC_SRCS}

CFLAGS+=	-DCONFIGURE_SPECS="\"\"" \
		-DACCEL_DIR_SUFFIX="\"\"" \
		-DDEFAULT_REAL_TARGET_MACHINE="\"${target_machine}\""

# hack to force c++ compiler to compile *.c files to create program
.for cfile in ${SRCS}
${cfile:.c=.o}: ${cfile}
	${CXX} ${STATIC_CXXFLAGS} ${CXXFLAGS} -c ${.IMPSRC} -o ${.TARGET}
.endfor

.include <bsd.prog.mk>
