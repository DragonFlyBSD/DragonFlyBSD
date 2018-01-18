.include "../Makefile.inc"
.include "../Makefile.langs"
.include "../../Makefile.cco"

PROG=	cpp
MANPAGEVER= 47
MFILE=	cpp${MANPAGEVER}.1
MAN=	${MFILE}

OBJS+=	${GCC_SRCS:S/^/..\/cc\//:.c=.o}
OBJS+=	${EXTRA_GCC_SRCS:S/^/..\/cc\//:.c=.o}
SRCS=	cppspec.c

CFLAGS+= -DCONFIGURE_SPECS="\"\""

${MFILE}: ${GCCDIR}/gcc/doc/cpp.1
	cp ${.ALLSRC} ${.TARGET}

CLEANFILES+=	${MFILE}

.if defined(IS_PRIMARY)
MLINKS+= ${MFILE} cpp.1
.endif

.include <bsd.prog.mk>
