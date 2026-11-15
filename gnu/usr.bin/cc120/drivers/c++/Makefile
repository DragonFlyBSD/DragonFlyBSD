GCC_LANG_DIR=	gcc/cp
.include "../Makefile.inc"
.include "../../Makefile.langs"

PROG_CXX=	c++
LINKS=		${BINDIR}/c++ ${BINDIR}/g++
LINKS+=		${BINDIR}/c++ ${BINDIR}/CC
NOMAN=		# man pages installed by cc (MLINK)

objs=		${GCC_OBJS} g++spec.o ${EXTRA_GCC_OBJS}
SRCS=		${objs:.o=.cc}

.include <bsd.prog.mk>
