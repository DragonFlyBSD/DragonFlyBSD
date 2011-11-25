.include "../Makefile.bu"

.PATH: ${SRCDIR}/binutils ${SRCDIR}/binutils/doc

PROG=	nm
MAN=	nm222.1
MLINKS+=nm222.1 nm.1
WARNS=	2

nm222.1: ${SRCDIR}/binutils/doc/nm.1
	cp ${.ALLSRC} ${.TARGET}

CLEANFILES+=	nm222.1

.include <bsd.prog.mk>
