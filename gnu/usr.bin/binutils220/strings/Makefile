.include "../Makefile.bu"

.PATH: ${SRCDIR}/binutils ${SRCDIR}/binutils/doc

PROG=	strings
MAN=	strings220.1
WARNS=	2

strings220.1: ${SRCDIR}/binutils/doc/strings.1
	cp ${.ALLSRC} ${.TARGET}

CLEANFILES+=	strings220.1

.include <bsd.prog.mk>
