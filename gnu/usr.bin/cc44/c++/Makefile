GCC_LANG_DIR=	gcc/cp

.include "../Makefile.inc"
.include "../Makefile.langs"
.include "../../Makefile.cco"

PROG=	c++
LINKS=	${BINDIR}/c++ ${BINDIR}/g++
LINKS+=	${BINDIR}/c++ ${BINDIR}/CC
MFILE=	g++${MANPAGEVER}.1
MAN=	${MFILE}

SRCS=	${GCC_SRCS} g++spec.c intl.c prefix.c version.c
SRCS+=	${EXTRA_GCC_SRCS}

CFLAGS+= -DGCC_DRIVER

${MFILE}: ${GCCDIR}/gcc/doc/g++.1
	cp ${.ALLSRC} ${.TARGET}

CLEANFILES+=	${MFILE}

.if defined(IS_PRIMARY)
MLINKS+= ${MFILE} g++.1
.endif

.include <bsd.prog.mk>
