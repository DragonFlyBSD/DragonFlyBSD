.include "../Makefile.bu"

.PATH: ${SRCDIR}/binutils ${SRCDIR}/binutils/doc

PROG=	size
MAN=	size221.1
MLINKS+=size221.1 size.1
WARNS=	2

size221.1: ${SRCDIR}/binutils/doc/size.1
	cp ${.ALLSRC} ${.TARGET}

CLEANFILES+=	size221.1

.include <bsd.prog.mk>
