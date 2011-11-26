.include "../Makefile.bu"

.PATH: ${SRCDIR}/binutils ${SRCDIR}/binutils/doc

PROG=	size
MAN=	size220.1
WARNS=	2

size220.1: ${SRCDIR}/binutils/doc/size.1
	cp ${.ALLSRC} ${.TARGET}

CLEANFILES+=	size220.1

.include <bsd.prog.mk>
