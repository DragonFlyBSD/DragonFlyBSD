GCC_LANG_DIR=	gcc/cp
.include "../Makefile.langs"
.include "../../Makefile.cco"

PROG=	c++
MANPAGEVER= 47
MFILE=	g++${MANPAGEVER}.1
MAN=	${MFILE}

LINKS=	${BINDIR}/c++ ${BINDIR}/g++
LINKS+=	${BINDIR}/c++ ${BINDIR}/CC

OBJS+=	${GCC_SRCS:S/^/..\/cc\//:.c=.o}
OBJS+=	${EXTRA_GCC_SRCS:S/^/..\/cc\//:.c=.o}
SRCS=	g++spec.c

CFLAGS+= -DCONFIGURE_SPECS="\"\""

${MFILE}: ${GCCDIR}/gcc/doc/g++.1
	cp ${.ALLSRC} ${.TARGET}

CLEANFILES+=	${MFILE}

.if defined(IS_PRIMARY)
MLINKS+= ${MFILE} g++.1
.endif

.include <bsd.prog.mk>
