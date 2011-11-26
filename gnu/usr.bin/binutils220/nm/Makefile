.include "../Makefile.bu"

.PATH: ${SRCDIR}/binutils ${SRCDIR}/binutils/doc

PROG=	nm
MAN=	nm220.1
WARNS=	2

nm220.1: ${SRCDIR}/binutils/doc/nm.1
	cp ${.ALLSRC} ${.TARGET}

CLEANFILES+=	nm220.1

.include <bsd.prog.mk>
