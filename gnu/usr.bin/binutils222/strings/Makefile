.include "../Makefile.bu"

.PATH: ${SRCDIR}/binutils ${SRCDIR}/binutils/doc

PROG=	strings
MAN=	strings222.1
MLINKS+=strings222.1 strings.1
WARNS=	2

strings222.1: ${SRCDIR}/binutils/doc/strings.1
	cp ${.ALLSRC} ${.TARGET}

CLEANFILES+=	strings222.1

.include <bsd.prog.mk>
