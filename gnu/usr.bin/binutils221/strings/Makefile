.include "../Makefile.bu"

.PATH: ${SRCDIR}/binutils ${SRCDIR}/binutils/doc

PROG=	strings
MAN=	strings221.1
MLINKS+=strings221.1 strings.1
WARNS=	2

strings221.1: ${SRCDIR}/binutils/doc/strings.1
	cp ${.ALLSRC} ${.TARGET}

CLEANFILES+=	strings221.1

.include <bsd.prog.mk>
