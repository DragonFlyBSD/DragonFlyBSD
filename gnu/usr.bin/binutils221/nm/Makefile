.include "../Makefile.bu"

.PATH: ${SRCDIR}/binutils ${SRCDIR}/binutils/doc

PROG=	nm
MAN=	nm221.1
MLINKS+=nm221.1 nm.1
WARNS=	2

nm221.1: ${SRCDIR}/binutils/doc/nm.1
	cp ${.ALLSRC} ${.TARGET}

CLEANFILES+=	nm221.1

.include <bsd.prog.mk>
