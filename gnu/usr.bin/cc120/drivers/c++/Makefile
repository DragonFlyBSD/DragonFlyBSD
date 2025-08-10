GCC_LANG_DIR=	gcc/cp
.include "../Makefile.inc"
.include "../../Makefile.langs"
.include "../../../Makefile.cco"

PROG_CXX=	c++
LINKS=		${BINDIR}/c++ ${BINDIR}/g++
LINKS+=		${BINDIR}/c++ ${BINDIR}/CC
MAN=		# man pages installed by cc (MLINK)

SRCS=		${GCC_SRCS} g++spec.cc ${EXTRA_GCC_SRCS}

CFLAGS+=	-DCONFIGURE_SPECS="\"\"" \
		-DACCEL_DIR_SUFFIX="\"\"" \
		-DDEFAULT_REAL_TARGET_MACHINE="\"${target_machine}\""

.include <bsd.prog.mk>
