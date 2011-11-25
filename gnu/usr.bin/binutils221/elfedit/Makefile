.include "../Makefile.bu"

.PATH: ${SRCDIR}/binutils ${SRCDIR}/binutils/doc

PROG=	elfedit
SRCS=   elfedit.c elfcomm.c
MAN=	elfedit221.1
WARNS=	1

elfedit221.1: ${SRCDIR}/binutils/doc/elfedit.1
	cp ${.ALLSRC} ${.TARGET}

CLEANFILES+=	elfedit221.1

.include <bsd.prog.mk>
