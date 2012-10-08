.include "../Makefile.inc"
.include "../Makefile.langs"

PROG=	cpp
MAN=	cpp${GCCSHORTVER}.1
MLINKS=	cpp${GCCSHORTVER}.1 cpp.1

.if ${GCCSHORTVER} != ${GCCDOCSUFFIX}
MLINKS+=cpp${GCCSHORTVER}.1 cpp${GCCDOCSUFFIX}.1
.endif

OBJS+=	${GCC_SRCS:S/^/..\/cc\//:.c=.o}
OBJS+=	${EXTRA_GCC_SRCS:S/^/..\/cc\//:.c=.o}
SRCS=	cppspec.c

CFLAGS+= -DCONFIGURE_SPECS="\"\""

cpp${GCCSHORTVER}.1: ${GCCDIR}/gcc/doc/cpp.1
	cp ${.ALLSRC} ${.TARGET}

CLEANFILES+=	cpp${GCCSHORTVER}.1

.include <bsd.prog.mk>
