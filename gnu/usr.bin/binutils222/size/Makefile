.include "../Makefile.bu"

.PATH: ${SRCDIR}/binutils ${SRCDIR}/binutils/doc

PROG=	size
MAN=	size222.1
MLINKS+=size222.1 size.1
WARNS=	2

size222.1: ${SRCDIR}/binutils/doc/size.1
	cp ${.ALLSRC} ${.TARGET}

CLEANFILES+=	size222.1

.include <bsd.prog.mk>
