GCC_LANG_DIR=	gcc/cp
.include "../Makefile.langs"

PROG=	c++
LINKS=	${BINDIR}/c++ ${BINDIR}/g++
LINKS+=	${BINDIR}/c++ ${BINDIR}/CC
MAN=	g++${GCCSHORTVER}.1

.if ${GCCDOCSUFFIX} != ${GCCSHORTVER}
MLINKS=	g++${GCCSHORTVER}.1 g++${GCCDOCSUFFIX}.1
.endif

OBJS+=	${GCC_SRCS:S/^/..\/cc\//:.c=.o}
OBJS+=	${EXTRA_GCC_SRCS:S/^/..\/cc\//:.c=.o}
SRCS=	g++spec.c

CFLAGS+= -DCONFIGURE_SPECS="\"\""

g++${GCCSHORTVER}.1: ${GCCDIR}/gcc/doc/g++.1
	cp ${.ALLSRC} ${.TARGET}

CLEANFILES+=	g++${GCCSHORTVER}.1

.include <bsd.prog.mk>
