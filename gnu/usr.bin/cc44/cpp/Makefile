.include "../Makefile.inc"
.include "../Makefile.langs"

PROG=	cpp
MAN=	cpp${GCCSHORTVER}.1
MLINKS=	cpp${GCCSHORTVER}.1 cpp.1

.if ${GCCSHORTVER} != ${GCCDOCSUFFIX}
MLINKS+=cpp${GCCSHORTVER}.1 cpp${GCCDOCSUFFIX}.1
.endif

SRCS=	${GCC_SRCS} cppspec.c intl.c prefix.c version.c
SRCS+=	${EXTRA_GCC_SRCS}

CFLAGS+= -DGCC_DRIVER

cpp${GCCSHORTVER}.1: ${GCCDIR}/gcc/doc/cpp.1
	cp ${.ALLSRC} ${.TARGET}

CLEANFILES+=	cpp${GCCSHORTVER}.1

.include <bsd.prog.mk>
