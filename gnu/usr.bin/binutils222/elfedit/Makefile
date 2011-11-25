.include "../Makefile.bu"

.PATH: ${SRCDIR}/binutils ${SRCDIR}/binutils/doc

PROG=	elfedit
SRCS=   elfedit.c elfcomm.c
MAN=	elfedit222.1
MLINKS+=elfedit222.1 elfedit.1
WARNS=	1

elfedit222.1: ${SRCDIR}/binutils/doc/elfedit.1
	cp ${.ALLSRC} ${.TARGET}

CLEANFILES+=	elfedit222.1

.include <bsd.prog.mk>
