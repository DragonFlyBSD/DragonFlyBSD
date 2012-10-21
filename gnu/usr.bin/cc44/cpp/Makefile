.include "../Makefile.inc"
.include "../Makefile.langs"
.include "../../Makefile.cco"

PROG=	cpp
MFILE=	cpp${GCCSHORTVER}.1
MAN=	${MFILE}

SRCS=	${GCC_SRCS} cppspec.c intl.c prefix.c version.c
SRCS+=	${EXTRA_GCC_SRCS}

CFLAGS+= -DGCC_DRIVER

${MFILE}: ${GCCDIR}/gcc/doc/cpp.1
	cp ${.ALLSRC} ${.TARGET}

CLEANFILES+=	${MFILE}

.if defined(IS_PRIMARY)
MLINKS+= ${MFILE} cpp.1
.endif

.include <bsd.prog.mk>
