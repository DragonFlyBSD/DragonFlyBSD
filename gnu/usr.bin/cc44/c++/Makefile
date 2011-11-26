GCC_LANG_DIR=	gcc/cp

.include "../Makefile.inc"
.include "../Makefile.langs"

PROG=	c++
LINKS=	${BINDIR}/c++ ${BINDIR}/g++
LINKS+=	${BINDIR}/c++ ${BINDIR}/CC
MAN=	g++${GCCSHORTVER}.1

.if ${GCCDOCSUFFIX} != ${GCCSHORTVER}
MLINKS=	g++${GCCSHORTVER}.1 g++${GCCDOCSUFFIX}.1
.endif

SRCS=	${GCC_SRCS} g++spec.c intl.c prefix.c version.c
SRCS+=	${EXTRA_GCC_SRCS}

CFLAGS+= -DGCC_DRIVER

g++${GCCSHORTVER}.1: ${GCCDIR}/gcc/doc/g++.1
	cp ${.ALLSRC} ${.TARGET}

CLEANFILES+=	g++${GCCSHORTVER}.1

.include <bsd.prog.mk>
